#ifndef THREAD_HANDLER_H
#define THREAD_HANDLER_H

#include "common.h"

struct CriticalSection
{
	uint32_t isLocked = 0;
	int32_t ownerHandle = 0;
	int32_t ContentionCount = 0;
	uint32_t SectionHandle = 0;
	uint32_t UNUSED_1 = 0;
};

class Thread;

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

	void InitCriticalSection(CriticalSection* criticalSection);
	void CurrentThreadEnterCriticalSection(CriticalSection* criticalSection);
	void CurrentThreadExitCriticalSection(CriticalSection* criticalSection);
	void CurrentThreadSleep(uint32_t time);

	bool interrupting = false;
	bool pausing = false;
	VirtPtr interruptPC = 0;

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