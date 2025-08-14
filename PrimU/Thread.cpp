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


// ʵ�� ���� ���ˡ�FIFO �ȴ����У������ͷ�ʱ������Ȩֱ�� transfer ������
void Thread::EnterCriticalSection(CriticalSection* criticalSection)
{
    if (criticalSection == nullptr)
        return;

    int me = GetId();

    // ����Ѿ��� owner���ݹ�����
    if (criticalSection->ownerHandle == me) {
        ++criticalSection->recursionCount;
        _ownedCriticalSections[criticalSection] += 1;
        return;
    }

    // ���������û�еȴ��ߣ�����·������ֱ�ӻ��
    if (criticalSection->ownerHandle == -1 && criticalSection->waiters.empty()) {
        criticalSection->ownerHandle = me;
        criticalSection->recursionCount = 1;
        _ownedCriticalSections[criticalSection] = 1;
        return;
    }

    // ����Ѿ��ڵȴ���� CS�������ظ���ӣ���ֱ�ӷ��ر��ֵȴ�״̬
    if (_requested == criticalSection) {
        return;
    }

    // �������˳��л����еȴ��ߣ��� FIFO ��Ӳ����Ϊ����
    criticalSection->waiters.push_back(this);
    ++criticalSection->contentionCount;
    _requested = criticalSection;

    // ע�⣺���ַ�������Ϊ�����ɵ�����������ʱ�ٴ����и��̣߳�CanRun��
}

void Thread::LeaveCriticalSection(CriticalSection* criticalSection)
{
    if (criticalSection == nullptr)
        return;

    auto it = _ownedCriticalSections.find(criticalSection);
    if (it == _ownedCriticalSections.end()) {
#ifdef _DEBUG
        // �ǳ������ͷ� ���� ������ bug���ϵ�����
        __debugbreak();
#endif
        return;
    }

    // �̶߳˼�¼����
    int& threadCount = it->second;
    --threadCount;

    // ͬ�������ٽ����ĵݹ������Ӧ���̼߳�¼һ�£�
    if (criticalSection->recursionCount > 0)
        --criticalSection->recursionCount;
    else {
#ifdef _DEBUG
        // ��Ӧ����
        __debugbreak();
#endif
    }

    // ����̶߳Ը� CS �ļ������㣬���ͷ� ownership ��ת�������׵ȴ���
    if (threadCount == 0) {
        _ownedCriticalSections.erase(it);

        // ��������ڵȴ���FIFO����������Ȩֱ��ת�������߳�
        if (!criticalSection->waiters.empty()) {
            Thread* next = criticalSection->waiters.front();
            criticalSection->waiters.pop_front();
            if (criticalSection->contentionCount > 0)
                --criticalSection->contentionCount;

            // ���� next ����Ȩ
            criticalSection->ownerHandle = next->GetId();
            criticalSection->recursionCount = 1;

            // �ڵ��෶Χ�ڣ�����ֱ�ӷ�����һ��ʵ����˽���ֶΣ�C++ ����
            // ���� next �������־������ next �ĳ��б��м�¼���ѻ�ø� CS
            next->_requested = nullptr;
            next->_ownedCriticalSections[criticalSection] = 1;
        }
        else {
            // �޵ȴ��ߣ��ͷ��ٽ���
            criticalSection->ownerHandle = -1;
            criticalSection->recursionCount = 0;
        }
    }
    else {
        // ���еݹ�ռ�ã��߳��Գ��У������ͷ� owner
    }
}


// ================================
// Event API ʵ��
// ================================

Event* Thread::CreateEvent(bool bManualReset, bool bInitialState)
{
    // ע�⣺���÷����� later delete����������Ը�Ϊʹ������ָ�벢�ں���ʱɾ����
    return new Event(bManualReset, bInitialState);
}

void Thread::SetEvent(Event* ev)
{
    if (ev == nullptr) return;

    // ����� manual reset�����ź���Ϊ true�����������еȴ���
    if (ev->manualReset) {
        ev->signaled = true;

        // �������еȴ��ߣ�FIFO ˳�򣬱���ȫ����
        while (!ev->waiters.empty()) {
            Thread* t = ev->waiters.front();
            ev->waiters.pop_front();
            if (ev->contentionCount > 0) --ev->contentionCount;

            // ����ȴ�״̬��ʹ�߳����´ε��ȿ����У�CanRun �ῴ�� _waitingEvent == nullptr��
            t->_waitingEvent = nullptr;
            t->_waitingInfinite = false;
            // t->_waitTimeoutEnd ����Ҫר�����ã���������ȴ���־����
        }
    }
    else {
        // auto-reset:����еȴ��ߣ����Ѷ���һ������ signaled ��գ��������ѣ�
        if (!ev->waiters.empty()) {
            Thread* t = ev->waiters.front();
            ev->waiters.pop_front();
            if (ev->contentionCount > 0) --ev->contentionCount;

            // transfer ownership: �����̵߳ȴ���־
            t->_waitingEvent = nullptr;
            t->_waitingInfinite = false;

            // Ensure signaled stays false (auto consumed)
            ev->signaled = false;
        }
        else {
            // û�еȴ��ߣ�����¼���Ϊ signaled����һ�� WaitForEvent ���������ز������auto��
            ev->signaled = true;
        }
    }
}

void Thread::ResetEvent(Event* ev)
{
    if (ev == nullptr) return;
    ev->signaled = false;
}

// WaitForEvent: �������ذ��߳���Ϊ�ȴ�״̬���� EnterCriticalSection �ķ��һ�£�
// timeoutMillis: ���룻 <0 ��ʾ���޵ȴ��� ==0 ��ʾ���������������أ�
// ע�⣺�������غ��̴߳��ڵȴ�״̬�������¼��ѱ� signaled�������� CanRun ���ƺ�ʱʵ�ʼ�������
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

    // If timeout == 0, do not wait (poll) �� just return (caller gets no event)
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

    // Non-blocking: ���غ��̴߳��ڵȴ�״̬������������ CanRun() ����������ֱ���¼���ʱ�� Resume
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
    // ����߳����ڵȴ��¼����ٽ����������ڱ� Resume��CanRun() �������������Ƿ������
}

// ================================
// �Ƴ�ĳ�߳����¼� waiters �����еĸ����������ɷ��� cpp �ļ��ڣ��ǳ�Ա��
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
// �޸İ� CanRun() ���� ���¼��ȴ��� suspend �����ж�
// ================================
bool Thread::CanRun()
{
    // 1) ������ң�Suspend�����ȼ��
    if (_isSuspended)
        return false;

    // 2) ˯���߼������У�
    if (_isSleeping) {
        if (_sleepEnd > std::chrono::high_resolution_clock::now())
            return false;
        _isSleeping = false;
    }

    // 3) ������ڵȴ��ٽ�����������֮ǰ���߼������ֲ��䣩
    if (_requested != nullptr) {
        CriticalSection* cs = _requested;
        // ���������ٽ��������ѱ���������ǣ���������һ���̵߳� Leave ��ת������
        if (cs->ownerHandle == GetId()) {
            // ��¼/ȷ���̶߳˵ĳ��У������δ��
            auto it = _ownedCriticalSections.find(cs);
            if (it == _ownedCriticalSections.end()) {
                _ownedCriticalSections[cs] = 1;
            }
            _requested = nullptr;
            return true;
        }
        // ������Ȼ�޷����
        return false;
    }

    // 4) ������ڵȴ��¼������鳬ʱ���¼��Ƿ��ѱ� Set��SetEvent ���ڻ���ʱ�� _waitingEvent �� nullptr��
    if (_waitingEvent != nullptr) {
        Event* ev = _waitingEvent;

        // ����¼��ڱ𴦱� Set ��δ�Ƴ����ǵĵȴ����������Σ���ҲҪ��ȫ����
        if (ev->signaled) {
            if (!ev->manualReset) {
                // auto reset����� signaled �����˶��У������ǲ��Ƕ��ף���
                // Ϊ������������ﳢ�������������������Ȼ�ڶ����У�������ǴӶ����Ƴ���
                remove_from_event_waiters(ev, this);
                ev->signaled = false;
            }
            else {
                // manual reset��ֱ���Ƴ��ȴ�������
                remove_from_event_waiters(ev, this);
            }
            _waitingEvent = nullptr;
            _waitingInfinite = false;
            return true;
        }

        // ��鳬ʱ
        if (!_waitingInfinite) {
            auto now = std::chrono::high_resolution_clock::now();
            if (now >= _waitTimeoutEnd) {
                // ��ʱ�����¼������Ƴ��Լ�������ȴ���־�������ؿ�����
                remove_from_event_waiters(ev, this);
                _waitingEvent = nullptr;
                _waitingInfinite = false;
                return true; // ��ʱ���غ��̻߳����ִ�У������������м�鳬ʱ���壩
            }
        }

        // �������ڵȴ�����������
        return false;
    }

    // 5) ����û�������������߳̿�������
    return true;
}
void Thread::Sleep(uint32_t time)
{
    _isSleeping = true;
    _sleepEnd = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(time);
}
