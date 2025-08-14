#include "Thread.h"
#include "ThreadHandler.h"

int Thread::GenerateUniqueId()
{
    static int currentInt = -1;
    currentInt++;
    return currentInt;
}

void Thread::LoadState()
{
    _state->LoadState();
}

/*
* Do not call function outside of a callback!!
*   as it may save an errant PC value because
*   of a UC engine bug
*/
void Thread::SaveState()
{
    _state->SaveState();
}

void ThreadState::LoadState()
{
    if (!_isNewThread) {
        uc_context_restore(sExecutor->GetUcInstance(), _state);
    }

    uc_reg_write_batch(sExecutor->GetUcInstance(), reinterpret_cast<int*>(_regs), reinterpret_cast<void**>(_args), 16);
}

/*
* Do not call function outside of a callback!!
*   as it may save an errant PC value because
*   of a UC engine bug
*/
void ThreadState::SaveState()
{
    uc_context_save(sExecutor->GetUcInstance(), _state);
    uc_reg_read_batch(sExecutor->GetUcInstance(), reinterpret_cast<int*>(_regs), reinterpret_cast<void**>(_args), 16);

    size_t thumb;
    uc_query(sExecutor->GetUcInstance(), UC_QUERY_MODE, &thumb);
    _pc += (thumb & UC_MODE_THUMB) ? 1 : 0;

    if (_isNewThread)
        _isNewThread = false;
}


uint32_t Thread::GetTimeQuantum()
{
    if (this->_id == 0)
        return 4000;
    return 400 - _priority;
}


// 实现 —— 单核、FIFO 等待队列，并在释放时把所有权直接 transfer 给队首
void Thread::EnterCriticalSection(CriticalSection* criticalSection)
{
    if (criticalSection == nullptr)
        return;

    int me = GetId();

    // 如果已经是 owner：递归重入
    if (criticalSection->ownerHandle == me) {
        ++criticalSection->recursionCount;
        _ownedCriticalSections[criticalSection] += 1;
        return;
    }

    // 如果空闲且没有等待者（快速路径），直接获得
    if (criticalSection->ownerHandle == -1 && criticalSection->waiters.empty()) {
        criticalSection->ownerHandle = me;
        criticalSection->recursionCount = 1;
        _ownedCriticalSections[criticalSection] = 1;
        return;
    }

    // 如果已经在等待这个 CS（避免重复入队），直接返回保持等待状态
    if (_requested == criticalSection) {
        return;
    }

    // 否则：有人持有或已有等待者，按 FIFO 入队并标记为请求
    criticalSection->waiters.push_back(this);
    ++criticalSection->contentionCount;
    _requested = criticalSection;

    // 注意：保持非阻塞行为——由调度器决定何时再次运行该线程（CanRun）
}

void Thread::LeaveCriticalSection(CriticalSection* criticalSection)
{
    if (criticalSection == nullptr)
        return;

    auto it = _ownedCriticalSections.find(criticalSection);
    if (it == _ownedCriticalSections.end()) {
#ifdef _DEBUG
        // 非持有者释放 —— 可能是 bug，断点或忽略
        __debugbreak();
#endif
        return;
    }

    // 线程端记录减少
    int& threadCount = it->second;
    --threadCount;

    // 同步减少临界区的递归计数（应与线程记录一致）
    if (criticalSection->recursionCount > 0)
        --criticalSection->recursionCount;
    else {
#ifdef _DEBUG
        // 不应发生
        __debugbreak();
#endif
    }

    // 如果线程对该 CS 的计数归零，则释放 ownership 或转交给队首等待者
    if (threadCount == 0) {
        _ownedCriticalSections.erase(it);

        // 如果有人在等待（FIFO），把所有权直接转给队首线程
        if (!criticalSection->waiters.empty()) {
            Thread* next = criticalSection->waiters.front();
            criticalSection->waiters.pop_front();
            if (criticalSection->contentionCount > 0)
                --criticalSection->contentionCount;

            // 赋予 next 所有权
            criticalSection->ownerHandle = next->GetId();
            criticalSection->recursionCount = 1;

            // 在单类范围内，允许直接访问另一个实例的私有字段（C++ 允许）
            // 清理 next 的请求标志，并在 next 的持有表中记录它已获得该 CS
            next->_requested = nullptr;
            next->_ownedCriticalSections[criticalSection] = 1;
        }
        else {
            // 无等待者，释放临界区
            criticalSection->ownerHandle = -1;
            criticalSection->recursionCount = 0;
        }
    }
    else {
        // 仍有递归占用（线程仍持有），不释放 owner
    }
}


// ================================
// Event API 实现
// ================================

Event* Thread::CreateEvent(bool bManualReset, bool bInitialState)
{
    // 注意：调用方负责 later delete（或者你可以改为使用智能指针并在合适时删除）
    return new Event(bManualReset, bInitialState);
}

void Thread::SetEvent(Event* ev)
{
    if (ev == nullptr) return;

    // 如果是 manual reset：把信号置为 true，并唤醒所有等待者
    if (ev->manualReset) {
        ev->signaled = true;

        // 唤醒所有等待者（FIFO 顺序，遍历全部）
        while (!ev->waiters.empty()) {
            Thread* t = ev->waiters.front();
            ev->waiters.pop_front();
            if (ev->contentionCount > 0) --ev->contentionCount;

            // 清理等待状态，使线程在下次调度可运行（CanRun 会看到 _waitingEvent == nullptr）
            t->_waitingEvent = nullptr;
            t->_waitingInfinite = false;
            // t->_waitTimeoutEnd 不需要专门设置，这里清理等待标志即可
        }
    }
    else {
        // auto-reset:如果有等待者，唤醒队首一个并把 signaled 清空（立即消费）
        if (!ev->waiters.empty()) {
            Thread* t = ev->waiters.front();
            ev->waiters.pop_front();
            if (ev->contentionCount > 0) --ev->contentionCount;

            // transfer ownership: 清理线程等待标志
            t->_waitingEvent = nullptr;
            t->_waitingInfinite = false;

            // Ensure signaled stays false (auto consumed)
            ev->signaled = false;
        }
        else {
            // 没有等待者，则把事件置为 signaled，下一次 WaitForEvent 会立即返回并清除（auto）
            ev->signaled = true;
        }
    }
}

void Thread::ResetEvent(Event* ev)
{
    if (ev == nullptr) return;
    ev->signaled = false;
}

// WaitForEvent: 非阻塞地把线程置为等待状态（与 EnterCriticalSection 的风格一致）
// timeoutMillis: 毫秒； <0 表示无限等待； ==0 表示不阻塞（立即返回）
// 注意：函数返回后线程处于等待状态（除非事件已被 signaled）——由 CanRun 控制何时实际继续运行
void Thread::WaitForEvent(Event* ev, int timeoutMillis)
{
    if (ev == nullptr)
        return;

    // If event is signaled already:
    if (ev->signaled) {
        if (!ev->manualReset) {
            // auto-reset: consume the signal and return immediately
            ev->signaled = false;
        }
        // manual reset: leave signaled true and return immediately
        return;
    }

    // If timeout == 0, do not wait (poll) — just return (caller gets no event)
    if (timeoutMillis == 0)
        return;

    // If already waiting on this event, don't enqueue again
    if (_waitingEvent == ev)
        return;

    // Enqueue this thread on event waiters
    ev->waiters.push_back(this);
    ++ev->contentionCount;
    _waitingEvent = ev;
    _waitingInfinite = (timeoutMillis < 0);

    if (!_waitingInfinite) {
        _waitTimeoutEnd = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(timeoutMillis);
    }
    else {
        // set to a sentinel (not strictly necessary)
        _waitTimeoutEnd = std::chrono::high_resolution_clock::time_point::max();
    }

    // Non-blocking: 返回后线程处于等待状态；调度器会在 CanRun() 中阻塞它，直到事件或超时或 Resume
}

// ================================
// Suspend / Resume
// ================================

void Thread::Suspend()
{
    ++_suspendCount;
    _isSuspended = true;
}

void Thread::Resume()
{
    if (_suspendCount > 0) --_suspendCount;
    if (_suspendCount == 0) {
        _isSuspended = false;
    }
    // 如果线程正在等待事件或临界区，但现在被 Resume，CanRun() 会重新评估它是否可运行
}

// ================================
// 移除某线程在事件 waiters 队列中的辅助函数（可放在 cpp 文件内，非成员）
// ================================
static void remove_from_event_waiters(Event* ev, Thread* t)
{
    if (ev == nullptr) return;
    for (auto it = ev->waiters.begin(); it != ev->waiters.end(); ++it) {
        if (*it == t) {
            ev->waiters.erase(it);
            if (ev->contentionCount > 0) --ev->contentionCount;
            break;
        }
    }
}

// ================================
// 修改版 CanRun() —— 将事件等待和 suspend 纳入判断
// ================================
bool Thread::CanRun()
{
    // 1) 如果悬挂（Suspend）优先检查
    if (_isSuspended)
        return false;

    // 2) 睡眠逻辑（已有）
    if (_isSleeping) {
        if (_sleepEnd > std::chrono::high_resolution_clock::now())
            return false;
        _isSleeping = false;
    }

    // 3) 如果正在等待临界区，沿用你之前的逻辑（保持不变）
    if (_requested != nullptr) {
        CriticalSection* cs = _requested;
        // 如果请求的临界区现在已被授予给我们（可能在另一个线程的 Leave 中转交），
        if (cs->ownerHandle == GetId()) {
            // 记录/确认线程端的持有（如果尚未）
            auto it = _ownedCriticalSections.find(cs);
            if (it == _ownedCriticalSections.end()) {
                _ownedCriticalSections[cs] = 1;
            }
            _requested = nullptr;
            return true;
        }
        // 否则仍然无法获得
        return false;
    }

    // 4) 如果正在等待事件，则检查超时或事件是否已被 Set（SetEvent 会在唤醒时把 _waitingEvent 置 nullptr）
    if (_waitingEvent != nullptr) {
        Event* ev = _waitingEvent;

        // 如果事件在别处被 Set 而未移除我们的等待（极端情形），也要安全处理：
        if (ev->signaled) {
            if (!ev->manualReset) {
                // auto reset：如果 signaled 且无人队列（或我们不是队首），
                // 为简单起见，在这里尝试消费它（如果我们仍然在队列中，则把我们从队列移除）
                remove_from_event_waiters(ev, this);
                ev->signaled = false;
            }
            else {
                // manual reset：直接移除等待并继续
                remove_from_event_waiters(ev, this);
            }
            _waitingEvent = nullptr;
            _waitingInfinite = false;
            return true;
        }

        // 检查超时
        if (!_waitingInfinite) {
            auto now = std::chrono::high_resolution_clock::now();
            if (now >= _waitTimeoutEnd) {
                // 超时：从事件队列移除自己，清理等待标志，并返回可运行
                remove_from_event_waiters(ev, this);
                _waitingEvent = nullptr;
                _waitingInfinite = false;
                return true; // 超时返回后线程会继续执行（调用者需自行检查超时语义）
            }
        }

        // 否则仍在等待，不能运行
        return false;
    }

    // 5) 否则没有阻塞条件，线程可以运行
    return true;
}
void Thread::Sleep(uint32_t time)
{
    _isSleeping = true;
    _sleepEnd = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(time);
}
