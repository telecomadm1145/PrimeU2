#include "MemoryManager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>   // memcpy
#include <algorithm>

#include "MemoryBlock.h"
#include "executor.h"

// 定义堆分配前后缀的 "cookie" 或 "canary"
// 用于检测缓冲区溢出/下溢
static const uint64_t kHeapCookie = 0xacc1c01201110210ull;
static const size_t   kCookieSize = sizeof(kHeapCookie);

MemoryManager* MemoryManager::_instance = nullptr;

// ... [构造函数和其它未修改的函数保持不变] ...
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

// ... [StaticAlloc, StaticFree, OverlapsAnyMappedBlock 等函数保持不变] ...
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

// ====== 虚拟堆：子分配实现 (增加了Cookie) ======

// 辅助函数：在指定虚拟地址写入 cookie
void MemoryManager::WriteCookie(VirtPtr addr) {
	RealPtr realAddr = GetRealAddr(addr);
	if (realAddr) {
		*reinterpret_cast<uint64_t*>(realAddr) = kHeapCookie;
	}
}

// 辅助函数：检查指定虚拟地址的 cookie，若损坏则中止程序
void MemoryManager::CheckCookie(VirtPtr addr) {
	RealPtr realAddr = GetRealAddr(addr);
	if (!realAddr) {
		fprintf(stderr, "FATAL: Heap corruption check failed. Cookie address 0x%08x is not mapped.\n", addr);
		abort();
	}
	uint64_t value = *reinterpret_cast<uint64_t*>(realAddr);
	if (value != kHeapCookie) {
		fprintf(stderr, "FATAL: Heap corruption detected at address 0x%08x! Expected cookie 0x%08x, found 0x%08x.\n", addr, kHeapCookie, value);
		abort();
	}
}

ErrorCode MemoryManager::HeapAlloc(VirtPtr* out, size_t size)
{
	if (!_dynamicHeapBlock) return ERROR_MEM_ALLOC_FAIL;

	if (size == 0) {
		*out = 0;
		return ERROR_OK;
	}

	size_t alignedUserSize = AlignUp(size, kHeapAlign);
	// 总分配大小 = 前缀cookie + 对齐后的用户区 + 后缀cookie
	size_t totalAllocSize = alignedUserSize + 2 * kCookieSize;

	// First-fit
	for (auto it = _heapFree.begin(); it != _heapFree.end(); ++it) {
		VirtPtr freeStart = it->first;
		size_t  freeSize = it->second;

		// 简单处理：要求整个分配块对齐。更优化的分配器可以只对齐用户区。
		VirtPtr alignedStart = static_cast<VirtPtr>(AlignUp(freeStart, kHeapAlign));
		size_t  prefix = static_cast<size_t>(alignedStart - freeStart);

		if (freeSize < prefix) continue;
		size_t usable = freeSize - prefix;
		if (usable < totalAllocSize) continue;

		// 从该空闲块中切出
		VirtPtr blockStart = alignedStart; // 包含cookie的整个块的起始地址
		VirtPtr blockEnd = blockStart + static_cast<VirtPtr>(totalAllocSize);
		VirtPtr freeEnd = freeStart + static_cast<VirtPtr>(freeSize);

		// 移除旧的空闲块
		_heapFree.erase(it);

		// 归还前缀和尾部空闲
		if (prefix > 0) {
			_heapFree.emplace(freeStart, prefix);
		}
		if (blockEnd < freeEnd) {
			_heapFree.emplace(blockEnd, static_cast<size_t>(freeEnd - blockEnd));
		}

		// 写入 cookies
		VirtPtr userPtr = blockStart + kCookieSize;
		WriteCookie(blockStart); // 前缀 cookie
		WriteCookie(userPtr + static_cast<VirtPtr>(alignedUserSize)); // 后缀 cookie

		// 记录已分配（key是用户指针，value是用户大小）
		_heapAlloc.emplace(userPtr, alignedUserSize);

		*out = userPtr;
		return ERROR_OK;
	}
	__debugbreak();
	return ERROR_MEM_ALLOC_FAIL;
}

ErrorCode MemoryManager::HeapFree(VirtPtr addr)
{
	for (auto& item : _heapAlloc) {
		size_t userSize = item.second;
		VirtPtr userPtr = item.first;

		// 计算实际分配块的边界
		VirtPtr blockStart = userPtr - kCookieSize;
		VirtPtr suffixCookieAddr = userPtr + static_cast<VirtPtr>(userSize);
		size_t totalSize = userSize + 2 * kCookieSize;
		CheckCookie(blockStart);       // 检查前缀
		CheckCookie(suffixCookieAddr); // 检查后缀
	}
	{
		auto it = _heapAlloc.find(addr);
		if (it == _heapAlloc.end()) return ERROR_MEM_ADDR_NOT_ALLOCATED;

		size_t userSize = it->second;
		VirtPtr userPtr = it->first;

		// 计算实际分配块的边界
		VirtPtr blockStart = userPtr - kCookieSize;
		VirtPtr suffixCookieAddr = userPtr + static_cast<VirtPtr>(userSize);
		size_t totalSize = userSize + 2 * kCookieSize;

		// 从已分配表中移除
		_heapAlloc.erase(it);

		// 放回空闲并合并邻接（归还的是包含 cookie 的整个块）
		CoalesceAround(blockStart, totalSize);
		return ERROR_OK;
	}
}


// ... [CoalesceAround 函数保持不变] ...
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

// ... [DyanmicAlloc, DynamicFree 保持不变，因为它们调用 HeapAlloc/HeapFree] ...
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


// ====== Realloc 逻辑 (增加了Cookie) ======

bool MemoryManager::TryHeapReallocInPlace(VirtPtr addr, size_t oldSize, size_t newSize)
{
	// realloc前先检查cookie的完整性
	VirtPtr blockStart = addr - kCookieSize;
	CheckCookie(blockStart);
	CheckCookie(addr + static_cast<VirtPtr>(oldSize));

	if (newSize <= oldSize) {
		// 就地缩小：把尾部释放回自由表
		size_t shrink = oldSize - newSize;
		if (shrink > 0) {
			VirtPtr tail = addr + static_cast<VirtPtr>(newSize);
			// 在新的结尾处写入新的后缀cookie
			WriteCookie(tail);
			// 将 [new_suffix_cookie_end, old_suffix_cookie_end) 之间的空间释放
			CoalesceAround(tail + kCookieSize, shrink);
		}
		_heapAlloc[addr] = newSize;
		return true;
	}

	// 需要扩展：检测后邻是否空闲且足够
	size_t  need = newSize - oldSize;
	// 寻找从旧块后缀cookie之后开始的空闲块
	VirtPtr tail = addr + static_cast<VirtPtr>(oldSize) + kCookieSize;

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
			// 更新分配大小并写入新的后缀cookie
			_heapAlloc[addr] = newSize;
			WriteCookie(addr + static_cast<VirtPtr>(newSize));
			return true;
		}
	}

	return false;
}

ErrorCode MemoryManager::DynamicRealloc(VirtPtr* addr, size_t newsize)
{
	VirtPtr oldAddr = *addr;

	if (oldAddr == 0) {
		return DyanmicAlloc(addr, newsize);
	}
	if (newsize == 0) {
		*addr = 0;
		return DynamicFree(oldAddr);
	}

	if (HeapContains(oldAddr)) {
		auto it = _heapAlloc.find(oldAddr);
		if (it == _heapAlloc.end()) return ERROR_MEM_ADDR_NOT_ALLOCATED;
		size_t oldSize = it->second;

		size_t alignedNewSize = AlignUp(newsize, kHeapAlign);

		if (TryHeapReallocInPlace(oldAddr, oldSize, alignedNewSize)) {
			// *addr 保持不变
			return ERROR_OK;
		}

		// 分配新块 + 拷贝 + 释放旧块
		VirtPtr newAddr = 0;
		ErrorCode err = HeapAlloc(&newAddr, alignedNewSize);
		if (err != ERROR_OK) return err;

		// 拷贝旧数据 (只拷贝用户区)
		size_t copySize = std::min(oldSize, alignedNewSize);
		RealPtr dst = GetRealAddr(newAddr);
		RealPtr src = GetRealAddr(oldAddr);
		if (dst && src) {
			memcpy(dst, src, copySize);
		}
		else {
			// 理论上不应发生，因为地址都是刚分配/验证过的
			HeapFree(newAddr); // 回滚分配
			return ERROR_MEM_ADDR_NOT_ALLOCATED;
		}

		// 释放旧块 (HeapFree会检查cookie)
		HeapFree(oldAddr);

		*addr = newAddr;
		return ERROR_OK;
	}

	return ERROR_MEM_ADDR_NOT_ALLOCATED;
}

// ... [其它工具函数保持不变] ...
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
		if (it != _heapAlloc.end()) return it->second; // 返回用户可见大小
		return 0;
	}

	// 其它映射块：如支持子分配，可用 block->GetChunk(addr).GetSize()
	// 默认视为不支持
	return 0;
}