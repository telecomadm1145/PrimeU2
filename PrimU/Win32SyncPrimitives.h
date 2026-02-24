#ifndef WIN32_SYNC_PRIMITIVES_H
#define WIN32_SYNC_PRIMITIVES_H

#ifdef _WIN32

#include "SyncPrimitives.h"
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
  bool Wait(uint32_t timeoutMillis) override {
    DWORD timeout = (timeoutMillis == 0xFFFFFFFF) ? INFINITE : timeoutMillis;
    return WaitForSingleObject(_handle, timeout) == WAIT_OBJECT_0;
  }

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
  void Release(int releaseCount, int *previousCount) override {
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

class Win32SyncFactory : public ISyncFactory {
public:
  IEvent *CreateEventObject(bool bManualReset, bool bInitialState) override {
    return new Win32Event(bManualReset, bInitialState);
  }
  ISemaphore *CreateSemaphoreObject(int initialCount, int maxCount) override {
    return new Win32Semaphore(initialCount, maxCount);
  }
  ICriticalSection *CreateCriticalSectionObject() override {
    return new Win32CriticalSection();
  }
  void SleepMillis(uint32_t ms) override { Sleep(ms); }
  void YieldThread() override { SwitchToThread(); }
};

#endif // _WIN32

#endif // WIN32_SYNC_PRIMITIVES_H
