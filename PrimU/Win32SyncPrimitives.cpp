#include "Win32SyncPrimitives.h"
#include <windows.h>

class Win32Event : public IEvent {
public:
	Win32Event(bool bManualReset, bool bInitialState) {
		_handle = CreateEventA(nullptr, bManualReset, bInitialState, nullptr);
	}
	~Win32Event() override {
		if (_handle)
			CloseHandle(_handle);
	}
	void Set() override { SetEvent(_handle); }
	void Reset() override { ResetEvent(_handle); }
	bool Wait(uint32_t timeoutMillis) override;

private:
	HANDLE _handle;
};

class Win32Semaphore : public ISemaphore {
public:
	Win32Semaphore(int initialCount, int maxCount) {
		_handle = CreateSemaphoreA(nullptr, initialCount, maxCount, nullptr);
	}
	~Win32Semaphore() override {
		if (_handle)
			CloseHandle(_handle);
	}
	bool Wait(uint32_t timeoutMillis) override {
		DWORD timeout = (timeoutMillis == 0xFFFFFFFF) ? INFINITE : timeoutMillis;
		return WaitForSingleObject(_handle, timeout) == WAIT_OBJECT_0;
	}
	void Release(int releaseCount, int* previousCount) override {
		LONG prev;
		ReleaseSemaphore(_handle, releaseCount, &prev);
		if (previousCount)
			*previousCount = prev;
	}

private:
	HANDLE _handle;
};

class Win32CriticalSection : public ICriticalSection {
public:
	Win32CriticalSection() { InitializeCriticalSection(&_cs); }
	~Win32CriticalSection() override { DeleteCriticalSection(&_cs); }
	void Enter() override { EnterCriticalSection(&_cs); }
	void Leave() override { LeaveCriticalSection(&_cs); }

private:
	CRITICAL_SECTION _cs;
};

bool Win32Event::Wait(uint32_t timeoutMillis) {
	DWORD timeout = (timeoutMillis == 0xFFFFFFFF) ? INFINITE : timeoutMillis;
	return WaitForSingleObject(_handle, timeout) == WAIT_OBJECT_0;
}

IEvent* Win32SyncFactory::CreateEventObject(bool bManualReset, bool bInitialState) {
	return new Win32Event(bManualReset, bInitialState);
}

ISemaphore* Win32SyncFactory::CreateSemaphoreObject(int initialCount, int maxCount) {
	return new Win32Semaphore(initialCount, maxCount);
}

ICriticalSection* Win32SyncFactory::CreateCriticalSectionObject() {
	return new Win32CriticalSection();
}

void Win32SyncFactory::SleepMillis(uint32_t ms) { Sleep(ms); }

void Win32SyncFactory::YieldThread() { SwitchToThread(); }
