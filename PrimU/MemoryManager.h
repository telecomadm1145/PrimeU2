#pragma once
#include "common.h"

#include <unordered_set>
#include <unordered_map>
#include <map>
#include <stdexcept>

#include "MemoryBlock.h"
#include "MemoryChunk.h"

// 预分配的动态堆（虚拟堆）位置与大小
constexpr VirtPtr MEM_DYNAMIC_HEAP_BASE = 0x20000000;
constexpr size_t  MEM_DYNAMIC_HEAP_SIZE = 32 * 1024 * 1024; // 32MB

class MemoryManager
{
public:
    static MemoryManager* GetInstance() {
        if (!_instance) {
            _instance = new MemoryManager();
        }
        return _instance;
    }

    // 静态映射：将一段主机内存映射到指定虚拟地址区间（一次性映射，不做子分配）
    ErrorCode StaticAlloc(VirtPtr addr, size_t size, MemoryBlock** memoryBlock = nullptr);
    ErrorCode StaticFree(VirtPtr addr);

    // 动态分配：仅在预映射的 32MB 虚拟堆内做子分配（不触发新的 uc_mem_map/uc_mem_unmap）
    ErrorCode DyanmicAlloc(VirtPtr* addr, size_t size);
    ErrorCode DynamicFree(VirtPtr addr);
    ErrorCode DynamicRealloc(VirtPtr* addr, size_t newsize);

    VirtPtr GetVirtualAddr(RealPtr realPtr);
    RealPtr GetRealAddr(VirtPtr virtPtr);
    bool isVAddrAllocated(VirtPtr virtPtr);

    size_t GetAllocSize(VirtPtr addr);

private:
    MemoryManager();
    ~MemoryManager();

    MemoryManager(MemoryManager const&) = delete;
    void operator=(MemoryManager const&) = delete;

    static MemoryManager* _instance;

    // 预映射的动态堆（作为 Unicorn 中的一整块内存区域）
    MemoryBlock* _dynamicHeapBlock = nullptr;
    size_t       _heapSize = 0;

    // 简单的虚拟堆元数据：有序自由表 + 已分配表
    // 自由表使用按地址排序的 map，便于邻接块合并
    std::map<VirtPtr, size_t>           _heapFree;   // key=start, value=size
    std::unordered_map<VirtPtr, size_t> _heapAlloc;  // key=start, value=size

    std::unordered_set<MemoryBlock*> _blocks;

    // 辅助函数
    static constexpr size_t kHeapAlign = 16;

    static inline size_t AlignUp(size_t v, size_t a) {
        return (v + (a - 1)) & ~(a - 1);
    }

    inline bool HeapContains(VirtPtr a) const {
        if (!_dynamicHeapBlock) return false;
        VirtPtr base = _dynamicHeapBlock->GetVAddr();
        return (a >= base) && (a < base + _heapSize);
    }

    inline bool RangeOverlaps(VirtPtr a1, size_t s1, VirtPtr a2, size_t s2) const {
        if (s1 == 0 || s2 == 0) return false;
        uint64_t b1 = static_cast<uint64_t>(a1);
        uint64_t e1 = b1 + static_cast<uint64_t>(s1);
        uint64_t b2 = static_cast<uint64_t>(a2);
        uint64_t e2 = b2 + static_cast<uint64_t>(s2);
        return !(e1 <= b2 || e2 <= b1);
    }

    bool OverlapsAnyMappedBlock(VirtPtr addr, size_t size) const;

    // 虚拟堆子分配/释放/合并
    ErrorCode HeapAlloc(VirtPtr* out, size_t size);
    ErrorCode HeapFree(VirtPtr addr);
    bool TryHeapReallocInPlace(VirtPtr addr, size_t oldSize, size_t newSize);
    void CoalesceAround(VirtPtr start, size_t size);
};

#define sMemoryManager MemoryManager::GetInstance()
#define __GET(T, a) reinterpret_cast<T>(sMemoryManager->GetRealAddr(a))