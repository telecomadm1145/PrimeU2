#include "MemoryManager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>   // memcpy
#include <algorithm>

#include "MemoryBlock.h"
#include "executor.h"

MemoryManager* MemoryManager::_instance = nullptr;

// 仅在构造时一次性把虚拟堆映射到 Unicorn；后续动态分配只操作 C++ 侧元数据
MemoryManager::MemoryManager()
    : _dynamicHeapBlock(nullptr), _heapSize(0)
{
    // 映射 32MB 到 Unicorn
    const size_t size = MEM_DYNAMIC_HEAP_SIZE;

    // 对齐到页
    uint32_t pageCount = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t pageAlignedSize = pageCount * PAGE_SIZE;

    RealPtr realMemory = reinterpret_cast<RealPtr>(calloc(pageCount, PAGE_SIZE));
    if (realMemory == nullptr) {
        fprintf(stderr, "FATAL: calloc for dynamic heap failed\n");
        abort();
    }

    auto err = uc_mem_map_ptr(
        sExecutor->GetUcInstance(),
        MEM_DYNAMIC_HEAP_BASE,
        pageAlignedSize,
        UC_PROT_ALL,
        realMemory
    );
    if (err != UC_ERR_OK) {
        fprintf(stderr, "FATAL: uc_mem_map_ptr for dynamic heap failed, uc_err=%d\n", err);
        free(realMemory);
        abort();
    }

    // 用一个 MemoryBlock 记录这段已映射区域，但不进行子分配
    MemoryBlock* heapBlock = new MemoryBlock(MEM_DYNAMIC_HEAP_BASE, realMemory, pageCount);
	heapBlock->VirtualAlloc(pageAlignedSize); // 整块映射为已分配
    _blocks.insert(heapBlock);
    _dynamicHeapBlock = heapBlock;
    _heapSize = pageAlignedSize;

    // 初始化虚拟堆自由表：整块 32MB 为空闲
    _heapFree.clear();
    _heapAlloc.clear();
    _heapFree.emplace(MEM_DYNAMIC_HEAP_BASE, _heapSize);
}

MemoryManager::~MemoryManager()
{
    // 释放所有映射（包括动态堆）
    for (auto block : _blocks) {
        uc_mem_unmap(sExecutor->GetUcInstance(), block->GetVAddr(), block->GetSize());
        free(block->GetRAddr());
        delete block;
    }
    _blocks.clear();
    _dynamicHeapBlock = nullptr;
}

// 判断区间是否与任何已映射块（包括虚拟堆）重叠
bool MemoryManager::OverlapsAnyMappedBlock(VirtPtr addr, size_t size) const
{
    for (auto block : _blocks) {
        VirtPtr b = block->GetVAddr();
        size_t   s = block->GetSize();
        if (RangeOverlaps(addr, size, b, s)) return true;
    }
    return false;
}

ErrorCode MemoryManager::StaticAlloc(VirtPtr addr, size_t size, MemoryBlock** memoryBlock)
{
    if (size == 0) {
        if (memoryBlock) *memoryBlock = nullptr;
        return ERROR_OK;
    }

    // 防止与已映射区域（包含虚拟堆）重叠
    if (OverlapsAnyMappedBlock(addr, size)) {
        return ERROR_MEM_ALREADY_ALLOCATED;
    }

    // 页对齐映射
    uint32_t pageCount = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t   pageAlignedSize = pageCount * PAGE_SIZE;

    RealPtr realMemory = reinterpret_cast<RealPtr>(calloc(pageCount, PAGE_SIZE));
    if (!realMemory) {
        return ERROR_MEM_ALLOC_FAIL;
    }

    auto err = uc_mem_map_ptr(sExecutor->GetUcInstance(), addr, pageAlignedSize, UC_PROT_ALL, realMemory);
    if (err != UC_ERR_OK) {
        free(realMemory);
        return ERROR_UC_MAP;
    }

    MemoryBlock* newBlock = new MemoryBlock(addr, realMemory, pageCount);
	newBlock->VirtualAlloc(pageAlignedSize); // 整块映射为已分配

    _blocks.insert(newBlock);

    if (memoryBlock) *memoryBlock = newBlock;
    return ERROR_OK;
}

ErrorCode MemoryManager::StaticFree(VirtPtr addr)
{
    for (auto it = _blocks.begin(); it != _blocks.end(); ++it) {
        auto block = *it;
        if (block->GetVAddr() == addr) {
            // 禁止通过 StaticFree 释放主虚拟堆
            if (block == _dynamicHeapBlock) {
                return ERROR_MEM_STATIC_NOT_FREEABLE;
            }
            if (uc_mem_unmap(sExecutor->GetUcInstance(), addr, block->GetSize()) != UC_ERR_OK) {
                return ERROR_UC_UNMAP;
            }
            free(block->GetRAddr());
            delete block;
            _blocks.erase(it);
            return ERROR_OK;
        }
    }
    return ERROR_MEM_ADDR_NOT_ALLOCATED;
}

// ====== 虚拟堆：子分配实现 ======

ErrorCode MemoryManager::HeapAlloc(VirtPtr* out, size_t size)
{
    if (!_dynamicHeapBlock) return ERROR_MEM_ALLOC_FAIL;

    if (size == 0) {
        *out = 0;
        return ERROR_OK;
    }

    size = AlignUp(size, kHeapAlign);

    // First-fit，考虑对齐造成的前缀切分
    for (auto it = _heapFree.begin(); it != _heapFree.end(); ++it) {
        VirtPtr freeStart = it->first;
        size_t  freeSize = it->second;

        VirtPtr alignedStart = static_cast<VirtPtr>(AlignUp(freeStart, kHeapAlign));
        size_t  prefix = static_cast<size_t>(alignedStart - freeStart);

        if (freeSize < prefix) continue; // 不够放对齐前缀
        size_t usable = freeSize - prefix;
        if (usable < size) continue;

        // 从该空闲块中切出 [alignedStart, alignedStart+size)
        VirtPtr allocStart = alignedStart;
        VirtPtr allocEnd = allocStart + static_cast<VirtPtr>(size);
        VirtPtr freeEnd = freeStart + static_cast<VirtPtr>(freeSize);

        // 移除旧的空闲块
        _heapFree.erase(it);

        // 归还前缀空闲
        if (prefix > 0) {
            _heapFree.emplace(freeStart, prefix);
        }
        // 归还尾部空闲
        if (allocEnd < freeEnd) {
            _heapFree.emplace(allocEnd, static_cast<size_t>(freeEnd - allocEnd));
        }

        // 记录已分配
        _heapAlloc.emplace(allocStart, size);

        *out = allocStart;
        return ERROR_OK;
    }

    return ERROR_MEM_ALLOC_FAIL;
}

void MemoryManager::CoalesceAround(VirtPtr start, size_t size)
{
    // 合并邻接空闲块：[prevEnd==start] 或 [end==nextStart]
    VirtPtr end = start + static_cast<VirtPtr>(size);

    // 找到第一个 key > start 的迭代器
    auto nextIt = _heapFree.upper_bound(start);

    // 尝试与前一个合并
    if (nextIt != _heapFree.begin()) {
        auto prevIt = std::prev(nextIt);
        VirtPtr prevStart = prevIt->first;
        size_t  prevSize = prevIt->second;
        VirtPtr prevEnd = prevStart + static_cast<VirtPtr>(prevSize);
        if (prevEnd == start) {
            // merge 到前块
            start = prevStart;
            size += prevSize;
            _heapFree.erase(prevIt);
        }
    }

    // 尝试与后一个合并（必须用新的 start/size 计算 end）
    end = start + static_cast<VirtPtr>(size);
    nextIt = _heapFree.lower_bound(start);
    if (nextIt != _heapFree.end()) {
        VirtPtr nextStart = nextIt->first;
        size_t  nextSize = nextIt->second;
        if (end == nextStart) {
            // merge 到后块
            size += nextSize;
            _heapFree.erase(nextIt);
        }
    }

    // 放回合并后的块
    _heapFree.emplace(start, size);
}

ErrorCode MemoryManager::HeapFree(VirtPtr addr)
{
    auto it = _heapAlloc.find(addr);
    if (it == _heapAlloc.end()) return ERROR_MEM_ADDR_NOT_ALLOCATED;

    size_t size = it->second;
    _heapAlloc.erase(it);

    // 放回空闲并合并邻接
    CoalesceAround(addr, size);
    return ERROR_OK;
}

// ====== 动态 API：转调到虚拟堆 ======

ErrorCode MemoryManager::DyanmicAlloc(VirtPtr* addr, size_t size)
{
    if (size == 0) {
        *addr = 0;
        return ERROR_OK;
    }
    return HeapAlloc(addr, size);
}

ErrorCode MemoryManager::DynamicFree(VirtPtr addr)
{
    if (addr == 0) return ERROR_OK;

    // 虚拟堆内：只改元数据，不做 uc_mem_unmap
    if (HeapContains(addr)) {
        return HeapFree(addr);
    }

    // 非虚拟堆地址（历史兼容）：尝试在静态块中处理（如果外部有“子分配”的实现，可在这里接管）
    for (auto block : _blocks) {
        if (block == _dynamicHeapBlock) continue;
        if (block->ContainsVAddr(addr)) {
            // 如需支持在其他块做内部分配/释放，这里可调用 block->VirtualFree(addr)
            // 目前按“不支持”处理
            return ERROR_MEM_ADDR_NOT_ALLOCATED;
        }
    }

    return ERROR_MEM_ADDR_NOT_ALLOCATED;
}

bool MemoryManager::TryHeapReallocInPlace(VirtPtr addr, size_t oldSize, size_t newSize)
{
    if (newSize <= oldSize) {
        // 允许“就地缩小”：把尾部释放回自由表
        size_t shrink = oldSize - newSize;
        if (shrink > 0) {
            VirtPtr tail = addr + static_cast<VirtPtr>(newSize);
            CoalesceAround(tail, shrink);
        }
        _heapAlloc[addr] = newSize;
        return true;
    }

    // 需要扩展：检测后邻是否空闲且足够
    size_t  need = newSize - oldSize;
    VirtPtr tail = addr + static_cast<VirtPtr>(oldSize);

    auto nextIt = _heapFree.find(tail);
    if (nextIt != _heapFree.end()) {
        size_t nextSize = nextIt->second;
        if (nextSize >= need) {
            // 从后邻空闲块吃掉 need
            _heapFree.erase(nextIt);
            size_t remain = nextSize - need;
            if (remain > 0) {
                _heapFree.emplace(tail + static_cast<VirtPtr>(need), remain);
            }
            _heapAlloc[addr] = newSize;
            return true;
        }
    }

    return false;
}

ErrorCode MemoryManager::DynamicRealloc(VirtPtr* addr, size_t newsize)
{
    VirtPtr oldAddr = *addr;

    if (oldAddr == 0) {
        // realloc(NULL, n) == malloc(n)
        return DyanmicAlloc(addr, newsize);
    }
    if (newsize == 0) {
        // realloc(p, 0) == free(p) + return NULL
        *addr = 0;
        return DynamicFree(oldAddr);
    }

    // 虚拟堆内：优先尝试就地扩容/缩小
    if (HeapContains(oldAddr)) {
        auto it = _heapAlloc.find(oldAddr);
        if (it == _heapAlloc.end()) return ERROR_MEM_ADDR_NOT_ALLOCATED;
        size_t oldSize = it->second;

        newsize = AlignUp(newsize, kHeapAlign);

        if (TryHeapReallocInPlace(oldAddr, oldSize, newsize)) {
            return ERROR_OK;
        }

        // 分配新块 + 拷贝 + 释放旧块
        VirtPtr newAddr = 0;
        ErrorCode err = HeapAlloc(&newAddr, newsize);
        if (err != ERROR_OK) return err;

        // 拷贝旧数据
        RealPtr dst = _dynamicHeapBlock->GetRAddr(newAddr);
        RealPtr src = _dynamicHeapBlock->GetRAddr(oldAddr);
        memcpy(dst, src, oldSize);

        // 释放旧块
        HeapFree(oldAddr);

        *addr = newAddr;
        return ERROR_OK;
    }

    // 非虚拟堆地址（历史兼容路径）：这里如果你的 MemoryBlock/MemoryChunk 支持子分配，
    // 可以保留原逻辑；默认不支持，返回错误。
    return ERROR_MEM_ADDR_NOT_ALLOCATED;
}

// ====== 其它工具函数 ======

bool MemoryManager::isVAddrAllocated(VirtPtr virtPtr)
{
    // 若在任一已映射块（包含虚拟堆）范围内，即视为已被占用（用于静态映射冲突检测）
    for (auto block : _blocks) {
        if (block->ContainsVAddr(virtPtr)) return true;
    }
    return false;
}

RealPtr MemoryManager::GetRealAddr(VirtPtr virtPtr)
{
    for (auto block : _blocks) {
        if (block->ContainsVAddr(virtPtr)) {
            return block->GetRAddr(virtPtr);
        }
    }
    return nullptr;
}

VirtPtr MemoryManager::GetVirtualAddr(RealPtr realPtr)
{
    for (auto block : _blocks) {
        if (block->ContainsRAddr(realPtr)) {
            return block->GetVAddr(realPtr);
        }
    }
    return 0x0;
}

size_t MemoryManager::GetAllocSize(VirtPtr addr)
{
    // 虚拟堆内的分配大小直接查 _heapAlloc
    if (HeapContains(addr)) {
        auto it = _heapAlloc.find(addr);
        if (it != _heapAlloc.end()) return it->second;
        return 0;
    }

    // 其它映射块：如支持子分配，可用 block->GetChunk(addr).GetSize()
    // 默认视为不支持
    return 0;
}