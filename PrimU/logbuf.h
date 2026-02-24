#pragma once
// ============================================================================
//  RollingLogBuffer<T> — 线程安全的滚动环形日志缓冲区
//  - 支持任意自定义日志结构体
//  - 多线程写入 + 多线程读取 (shared_mutex 读写锁)
//  - 满时自动淘汰最旧条目
//  - ReadView RAII 视图：锁定期间支持 O(1) 索引访问 & range-for
//  - 序列号追踪：支持增量消费 (readSince)
// ============================================================================

#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <mutex>
#include <shared_mutex>
#include <vector>

template <typename LogEntry>
class RollingLogBuffer {
public:
    // =====================================================================
    //  ReadView — RAII 锁定的只读视图
    //  构造时自动获取 shared_lock，析构时自动释放
    //  支持 operator[]、range-for、size() 等
    // =====================================================================
    class ReadView {
    public:
        // ----- 迭代器 -----
        class Iterator {
        public:
            using iterator_category = std::random_access_iterator_tag;
            using value_type = LogEntry;
            using difference_type = std::ptrdiff_t;
            using pointer = const LogEntry*;
            using reference = const LogEntry&;

            Iterator() : view_(nullptr), idx_(0) {}
            Iterator(const ReadView* v, size_t i) : view_(v), idx_(i) {}

            reference operator*()  const { return (*view_)[idx_]; }
            pointer   operator->() const { return &(*view_)[idx_]; }
            reference operator[](difference_type n) const { return (*view_)[idx_ + n]; }

            Iterator& operator++() { ++idx_; return *this; }
            Iterator  operator++(int) { auto t = *this; ++idx_; return t; }
            Iterator& operator--() { --idx_; return *this; }
            Iterator  operator--(int) { auto t = *this; --idx_; return t; }

            Iterator& operator+=(difference_type n) { idx_ += n; return *this; }
            Iterator& operator-=(difference_type n) { idx_ -= n; return *this; }

            Iterator operator+(difference_type n) const { return { view_, idx_ + n }; }
            Iterator operator-(difference_type n) const { return { view_, idx_ - n }; }
            difference_type operator-(const Iterator& o) const {
                return static_cast<difference_type>(idx_) -
                    static_cast<difference_type>(o.idx_);
            }
            friend Iterator operator+(difference_type n, const Iterator& it) {
                return { it.view_, it.idx_ + n };
            }

            bool operator==(const Iterator& o) const { return idx_ == o.idx_; }
            bool operator!=(const Iterator& o) const { return idx_ != o.idx_; }
            bool operator< (const Iterator& o) const { return idx_ < o.idx_; }
            bool operator> (const Iterator& o) const { return idx_ > o.idx_; }
            bool operator<=(const Iterator& o) const { return idx_ <= o.idx_; }
            bool operator>=(const Iterator& o) const { return idx_ >= o.idx_; }

        private:
            const ReadView* view_;
            size_t idx_;
        };

        ReadView(const RollingLogBuffer& buf)
            : buf_(buf), lock_(buf.mutex_) {
        }

        ReadView(const ReadView&) = delete;
        ReadView& operator=(const ReadView&) = delete;
        ReadView(ReadView&&) = default;
        ReadView& operator=(ReadView&&) = default;

        size_t size()  const { return buf_.count_; }
        bool   empty() const { return buf_.count_ == 0; }

        /// 按逻辑索引访问 (0 = 最旧)
        const LogEntry& operator[](size_t i) const {
            assert(i < buf_.count_);
            return buf_.buffer_[buf_.physIdx(i)];
        }
        const LogEntry& front() const { return (*this)[0]; }
        const LogEntry& back()  const { return (*this)[size() - 1]; }

        Iterator begin() const { return { this, 0 }; }
        Iterator end()   const { return { this, size() }; }

    private:
        const RollingLogBuffer& buf_;
        std::shared_lock<std::shared_mutex> lock_;
    };

    // =====================================================================
    //  构造
    // =====================================================================
    explicit RollingLogBuffer(size_t capacity = 1024)
        : capacity_(capacity), buffer_(capacity), head_(0), count_(0), nextSeq_(1)
    {
        assert(capacity > 0 && "capacity must be > 0");
    }

    // =====================================================================
    //  写入操作 (线程安全，自动唤醒等待者)
    // =====================================================================
    void push(const LogEntry& entry) {
        { std::unique_lock lk(mutex_); doPush(entry); }
        cv_.notify_all();
    }

    void push(LogEntry&& entry) {
        { std::unique_lock lk(mutex_); doPush(std::move(entry)); }
        cv_.notify_all();
    }

    /// 就地构造一条日志
    template <typename... Args>
    void emplace(Args&&... args) {
        {
            std::unique_lock lk(mutex_);
            buffer_[head_] = LogEntry{ std::forward<Args>(args)... };
            advance();
        }
        cv_.notify_all();
    }

    /// 批量写入
    template <typename Iter>
    void pushBatch(Iter first, Iter last) {
        {
            std::unique_lock lk(mutex_);
            for (; first != last; ++first) doPush(*first);
        }
        cv_.notify_all();
    }

    // =====================================================================
    //  读取操作 (线程安全)
    // =====================================================================

    /// 获取 RAII 锁定视图 —— 适合 UI 渲染循环中使用
    ///   auto v = buf.view();
    ///   for (auto& e : v) render(e);      // 持有读锁期间安全索引
    ReadView view() const { return ReadView(*this); }

    /// 快照：拷贝全部条目（最旧在前）
    std::vector<LogEntry> snapshot() const {
        std::shared_lock lk(mutex_);
        return copyRange(0, count_);
    }

    /// 获取最新 n 条
    std::vector<LogEntry> latest(size_t n) const {
        std::shared_lock lk(mutex_);
        n = std::min(n, count_);
        return copyRange(count_ - n, n);
    }

    /// 获取 [offset, offset+count) 范围的条目（0 = 最旧）
    std::vector<LogEntry> range(size_t offset, size_t count) const {
        std::shared_lock lk(mutex_);
        if (offset >= count_) return {};
        size_t n = std::min(count, count_ - offset);
        return copyRange(offset, n);
    }

    /// 增量读取：获取自 lastSeq 以来的新条目
    /// 使用方式:
    ///   uint64_t seq = 0;
    ///   while (running) {
    ///       auto newLogs = buf.readSince(seq);  // seq 会被更新
    ///       for (auto& e : newLogs) display(e);
    ///   }
    std::vector<LogEntry> readSince(uint64_t& lastSeq) const {
        std::shared_lock lk(mutex_);
        uint64_t curSeq = nextSeq_ - 1;
        if (count_ == 0 || lastSeq >= curSeq) {
            lastSeq = curSeq;
            return {};
        }
        uint64_t delta = curSeq - lastSeq;
        size_t   actual = static_cast<size_t>(std::min(delta,
            static_cast<uint64_t>(count_)));
        lastSeq = curSeq;
        return copyRange(count_ - actual, actual);
    }

    // =====================================================================
    //  查询
    // =====================================================================
    size_t   size()     const { std::shared_lock lk(mutex_); return count_; }
    size_t   capacity() const { return capacity_; }           // 不变量，无需锁
    bool     empty()    const { std::shared_lock lk(mutex_); return count_ == 0; }
    bool     full()     const { std::shared_lock lk(mutex_); return count_ == capacity_; }
    uint64_t sequence() const { std::shared_lock lk(mutex_); return nextSeq_ - 1; }

    // =====================================================================
    //  修改
    // =====================================================================
    void clear() {
        std::unique_lock lk(mutex_);
        head_ = 0;
        count_ = 0;
        // 不重置 nextSeq_，保证 readSince 语义正确
    }

    // =====================================================================
    //  遍历 & 过滤
    // =====================================================================
    void forEach(const std::function<void(const LogEntry&)>& fn) const {
        std::shared_lock lk(mutex_);
        for (size_t i = 0; i < count_; ++i)
            fn(buffer_[physIdx(i)]);
    }

    std::vector<LogEntry> filter(
        const std::function<bool(const LogEntry&)>& pred) const {
        std::shared_lock lk(mutex_);
        std::vector<LogEntry> out;
        for (size_t i = 0; i < count_; ++i) {
            const auto& e = buffer_[physIdx(i)];
            if (pred(e)) out.push_back(e);
        }
        return out;
    }

    // =====================================================================
    //  等待新日志 (阻塞式消费)
    // =====================================================================
    void waitForNew(uint64_t sinceSeq) const {
        std::shared_lock lk(mutex_);
        cv_.wait(lk, [&] { return nextSeq_ - 1 > sinceSeq; });
    }

    template <typename Rep, typename Period>
    bool waitForNew(uint64_t sinceSeq,
        const std::chrono::duration<Rep, Period>& timeout) const {
        std::shared_lock lk(mutex_);
        return cv_.wait_for(lk, timeout,
            [&] { return nextSeq_ - 1 > sinceSeq; });
    }

    // =====================================================================
    //  禁止拷贝，允许移动
    // =====================================================================
    RollingLogBuffer(const RollingLogBuffer&) = delete;
    RollingLogBuffer& operator=(const RollingLogBuffer&) = delete;
    RollingLogBuffer(RollingLogBuffer&&) = default;
    RollingLogBuffer& operator=(RollingLogBuffer&&) = default;

private:
    // 逻辑索引 → 物理索引  (0 = 最旧条目)
    size_t physIdx(size_t logical) const {
        size_t oldest = (head_ + capacity_ - count_) % capacity_;
        return (oldest + logical) % capacity_;
    }

    void doPush(const LogEntry& e) { buffer_[head_] = e;            advance(); }
    void doPush(LogEntry&& e) { buffer_[head_] = std::move(e); advance(); }

    void advance() {
        ++nextSeq_;
        head_ = (head_ + 1) % capacity_;
        if (count_ < capacity_) ++count_;
    }

    std::vector<LogEntry> copyRange(size_t off, size_t n) const {
        std::vector<LogEntry> r;
        r.reserve(n);
        for (size_t i = 0; i < n; ++i)
            r.push_back(buffer_[physIdx(off + i)]);
        return r;
    }

    // ----- 数据成员 -----
    size_t                capacity_;
    std::vector<LogEntry> buffer_;     // 预分配环形存储
    size_t                head_;       // 下一个写入位置
    size_t                count_;      // 当前有效条目数
    uint64_t              nextSeq_;    // 下一个序列号

    mutable std::shared_mutex           mutex_;
    mutable std::condition_variable_any cv_;
};