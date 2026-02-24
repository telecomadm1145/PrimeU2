#ifndef THREAD_HANDLER_H
#define THREAD_HANDLER_H

#include "common.h"
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>


class Thread;

class StateManager {
public:
	static StateManager* GetInstance() {
		return !_instance ? _instance = new StateManager : _instance;
	}

	int NewThread(VirtPtr start, uint32_t arg = 0,
		uint8_t priority = THREAD_PRIORITY_NORMAL,
		size_t stackSize = 0x2000);

	int SetThreadPriority(int threadId, uint8_t priority);
	Thread* GetThread(int threadId);

	// Called from within an emulated thread to get its active Thread back-pointer
	Thread* GetCurrentThread();
	int GetCurrentThreadId();

	bool interrupting = false;
	bool pausing = false;
	VirtPtr interruptPC = 0;

private:
	StateManager() {}
	~StateManager() {}
	StateManager(StateManager const&) = delete;
	void operator=(StateManager const&) = delete;
	static StateManager* _instance;

	std::mutex _threadMapMutex;
	std::unordered_map<int, Thread*> _threads;
	friend class Thread;
};

#define sThreadHandler StateManager::GetInstance()

#endif
