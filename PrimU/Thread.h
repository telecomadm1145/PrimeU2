#ifndef THREAD_H
#define THREAD_H

#include "SyncPrimitives.h"
#include "executor.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <unicorn/unicorn.h>
#include <unordered_map>

// Note: CriticalSection, Event, and Semaphore will now be wrappers around
// the ISyncFactory interfaces.
inline thread_local Thread* currentThread = nullptr;

class Thread {
public:
	// Instead of static chaining, threads will just run independently.
	Thread(VirtPtr start, uint32_t arg, uint8_t priority, size_t stackSize);
	~Thread();

	// Used by the system to start the standard thread.
	void Start();

	// Sleep this thread natively
	void Sleep(uint32_t time);

	void SetPriority(uint8_t priority) { _priority = priority; }
	uint8_t GetPriority() const { return _priority; }

	// Sync API wrappers calling into SyncFactory
	IEvent* CreateEvent(bool bManualReset, bool bInitialState);
	void SetEvent(IEvent* ev);
	void ResetEvent(IEvent* ev);
	void WaitForEvent(IEvent* ev, int timeoutMillis);

	ISemaphore* CreateSemaphore(int initialCount, int maxCount);
	void WaitForSemaphore(ISemaphore* sem, int timeoutMillis);
	void ReleaseSemaphore(ISemaphore* sem, int releaseCount, int* previousCount);
	using Arg = std::optional<uint32_t>;
	void ExecuteCustomCode(VirtPtr pc, Arg a = {}, Arg b = {}, Arg c = {}, Arg d = {});

	int GetId() const { return _id; }

	// Each thread gets its own UC Instance pointing to the shared memory blocks.
	uc_engine* engine() const { return _uc; }

private:
	static int GenerateUniqueId();

	void ThreadProc();
	bool MapSharedMemoryToLocalEngine();

	uc_engine* _uc;
	std::thread _nativeThread;

	bool inited = false;

	uint8_t _priority;
	int _id;
	size_t _stackSize = 0x2000;
	VirtPtr _stackAddr;

	VirtPtr _startPc;
	uint32_t _startArg;
	VirtPtr _startSp;
	friend class StateManager;
};

#endif
