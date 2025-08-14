#ifndef THREAD_HANDLER_H
#define THREAD_HANDLER_H

#include "common.h"
#include <queue>

class Thread;

struct CriticalSection
{
	uint32_t isLocked = 0;
	int ownerHandle = -1;
	int recursionCount = 0;
	std::deque<Thread*> waiters;
	int contentionCount = 0;
};


// 前提：Thread 类已经存在，这里只展示需要新增/修改的成员和方法实现
class Event; // forward

// Event 本体 —— 单核 VM 版本
struct Event {
	bool manualReset;            // true = manual reset, false = auto reset
	bool signaled;               // 当前是否为信号状态
	std::deque<Thread*> waiters; // FIFO 等待队列（线程指针）
	int contentionCount = 0;     // 等待者计数（等于 waiters.size()）

	Event(bool manual, bool initial)
		: manualReset(manual), signaled(initial), waiters(), contentionCount(0) {
	}
};

class StateManager
{
public:

	static StateManager* GetInstance() { return !_instance ? _instance = new StateManager : _instance; }

	int NewThread(VirtPtr start, uint32_t arg = 0, uint8_t priority = THREAD_PRIORITY_NORMAL, size_t stackSize = 0x2000);
	void SwitchThread();
	void LoadCurrentThreadState();
	void SaveCurrentThreadState();

	uint32_t GetCurrentThreadQuantum() const;
	uint32_t GetCurrentThreadPC();
	bool CanCurrentThreadRun();
	int GetCurrentThreadId() const;

	int SetThreadPriority(int threadId, uint8_t priority);

	void WakeThread(int threadId);
	void CurrentThreadYield() {
		yielding = true;
	}

	void InitCriticalSection(CriticalSection* criticalSection);
	void CurrentThreadEnterCriticalSection(CriticalSection* criticalSection);
	void CurrentThreadExitCriticalSection(CriticalSection* criticalSection);
	void CurrentThreadSleep(uint32_t time);
	Thread& GetCurrentThread() {
		return *_currentThread;
	}

	bool interrupting = false;
	bool pausing = false;
	VirtPtr interruptPC = 0;

	bool yielding = false;

private:
	StateManager() {}
	~StateManager() {}
	StateManager(StateManager const&) = delete;
	void operator=(StateManager const&) = delete;
	static StateManager* _instance;

	Thread* _currentThread = nullptr;
};

#define sThreadHandler StateManager::GetInstance()


#endif