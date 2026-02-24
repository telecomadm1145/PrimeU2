// svc_thread.cpp — 线程、同步原语 handler
#include "svc_common.h"

#include "Thread.h"
#include "ThreadHandler.h"
#include "executor.h"

// ================================================================
//  线程操作 — 使用 AutoBind 自动提取参数
// ================================================================

static uint32_t OSCreateThread_impl(VirtPtr start, uint32_t arg,
                                    uint8_t priority, size_t stackSize) {
  return sThreadHandler->NewThread(start, arg, priority, stackSize);
}
uint32_t OSCreateThread(SystemServiceArguments *args) {
  return AutoBind<decltype(OSCreateThread_impl)>::thunk<OSCreateThread_impl>(
      args);
}

static uint32_t OSSetThreadPriority_impl(VirtPtr thread, uint8_t priority) {
  sThreadHandler->SetThreadPriority(thread, priority);
  return 0;
}
uint32_t OSSetThreadPriority(SystemServiceArguments *args) {
  return AutoBind<decltype(OSSetThreadPriority_impl)>::thunk<
      OSSetThreadPriority_impl>(args);
}

static uint32_t OSSleep_impl(uint32_t ms) {
  // If we had a generic GetCurrentThread() we'd use it,
  // but g_SyncFactory->SleepMillis covers it natively for the active thread.
  g_SyncFactory->SleepMillis(ms);
  return ms;
}
uint32_t OSSleep(SystemServiceArguments *args) {
  return AutoBind<decltype(OSSleep_impl)>::thunk<OSSleep_impl>(args);
}

uint32_t OSSuspendThread(SystemServiceArguments *args) { return 0; }
uint32_t OSResumeThread(SystemServiceArguments *args) { return 0; }

// ================================================================
//  Critical Section (Assuming we want to mock these entirely via ISyncFactory
//  later, for now we leave them as no-ops or simple placeholders since the
//  interface is opaque to emulator)
// ================================================================

// Not fully implemented to use ICriticalSection yet since the struct map was
// custom. If CriticalSection is an opaque struct in guest, we'd map it. Let's
// stub it.
uint32_t OSInitCriticalSection(SystemServiceArguments *args) {
  return args->r0;
}
uint32_t OSEnterCriticalSection(SystemServiceArguments *args) {
  return args->r0;
}
uint32_t OSLeaveCriticalSection(SystemServiceArguments *args) {
  return args->r0;
}

// ================================================================
//  Event
// ================================================================

static std::map<uint32_t, IEvent *> g_events;
static uint32_t g_next_event_id = 1;

static uint32_t CreateEvent_impl(uint32_t param0, uint32_t param1) {
  // Using the global factory directly to avoid current thread lookup hacks
  IEvent *ev = g_SyncFactory->CreateEventObject(param0, param1);
  g_events[g_next_event_id] = ev;
  return g_next_event_id++;
}
uint32_t OSCreateEvent(SystemServiceArguments *args) {
  return AutoBind<decltype(CreateEvent_impl)>::thunk<CreateEvent_impl>(args);
}

// OSResetEvent
static uint32_t ResetEvent_impl(uint32_t id) {
  auto it = g_events.find(id);
  if (it != g_events.end()) {
    it->second->Reset();
  }
  return 0;
}
uint32_t OSResetEvent(SystemServiceArguments *args) {
  return AutoBind<decltype(ResetEvent_impl)>::thunk<ResetEvent_impl>(args);
}

// OSCloseEvent
static uint32_t CloseEvent_impl(uint32_t id) {
  auto it = g_events.find(id);
  if (it != g_events.end()) {
    delete it->second;
    g_events.erase(it);
    return 1; // Success
  }
  return 0; // Failure
}
uint32_t OSCloseEvent(SystemServiceArguments *args) {
  return AutoBind<decltype(CloseEvent_impl)>::thunk<CloseEvent_impl>(args);
}

// OSWaitForEvent
static uint32_t WaitForEvent_impl(uint32_t id, uint32_t timeout) {
  auto it = g_events.find(id);
  if (it != g_events.end()) {
    // This natively blocks the std::thread executing the emulator loop!
    it->second->Wait(timeout);
  }
  return 0;
}
uint32_t OSWaitForEvent(SystemServiceArguments *args) {
  return AutoBind<decltype(WaitForEvent_impl)>::thunk<WaitForEvent_impl>(args);
}

static uint32_t SetEvent_impl(uint32_t id) {
  auto it = g_events.find(id);
  if (it != g_events.end()) {
    it->second->Set();
  }
  return 0;
}
uint32_t OSSetEvent(SystemServiceArguments *args) {
  return AutoBind<decltype(SetEvent_impl)>::thunk<SetEvent_impl>(args);
}

// ================================================================
//  Semaphore
// ================================================================

static std::map<uint32_t, ISemaphore *> g_semaphores;
static uint32_t g_next_sem_id = 1;

// OSCreateSemaphore(int initialCount, int maxCount)
static uint32_t CreateSemaphore_impl(int initialCount, int maxCount) {
  ISemaphore *sem =
      g_SyncFactory->CreateSemaphoreObject(initialCount, maxCount);
  g_semaphores[g_next_sem_id] = sem;
  return g_next_sem_id++;
}
uint32_t OSCreateSemaphore(SystemServiceArguments *args) {
  return AutoBind<decltype(CreateSemaphore_impl)>::thunk<CreateSemaphore_impl>(
      args);
}

// OSWaitForSemaphore(uint32_t id, uint32_t timeout)
static uint32_t WaitForSemaphore_impl(uint32_t id, uint32_t timeout) {
  auto it = g_semaphores.find(id);
  if (it != g_semaphores.end()) {
    // This blocks the std::thread natively!
    it->second->Wait(timeout);
  }
  return 0;
}
uint32_t OSWaitForSemaphore(SystemServiceArguments *args) {
  return AutoBind<decltype(WaitForSemaphore_impl)>::thunk<
      WaitForSemaphore_impl>(args);
}

// OSReleaseSemaphore(uint32_t id, int releaseCount, int* previousCount)
static uint32_t ReleaseSemaphore_impl(uint32_t id, int releaseCount,
                                      uint32_t prevCountPtr) {
  auto it = g_semaphores.find(id);
  if (it != g_semaphores.end()) {
    int prev = 0;
    it->second->Release(releaseCount, &prev);

    if (prevCountPtr != 0) {
      // For a threaded emulator lacking immediate access to uc_engine from
      // outside, we must get the engine. But wait, Executor has the MAIN
      // instance. We can't use Main instance safely here if memory is truly
      // isolated. Luckily, memory mapping means we can write via
      // sMemoryManager. Easiest is to manually write to real pointer mapped to
      // that addr:
      auto realPtr = sMemoryManager->GetRealAddr(prevCountPtr);
      if (realPtr) {
        *reinterpret_cast<int *>(realPtr) = prev;
      }
    }
    return 1;
  }
  return 0;
}
uint32_t OSReleaseSemaphore(SystemServiceArguments *args) {
  return AutoBind<decltype(ReleaseSemaphore_impl)>::thunk<
      ReleaseSemaphore_impl>(args);
}

// OSCloseSemaphore(uint32_t id)
static uint32_t CloseSemaphore_impl(uint32_t id) {
  auto it = g_semaphores.find(id);
  if (it != g_semaphores.end()) {
    delete it->second;
    g_semaphores.erase(it);
    return 1;
  }
  return 0;
}
uint32_t OSCloseSemaphore(SystemServiceArguments *args) {
  return AutoBind<decltype(CloseSemaphore_impl)>::thunk<CloseSemaphore_impl>(
      args);
}
