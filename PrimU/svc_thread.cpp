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
  sThreadHandler->CurrentThreadSleep(ms);
  return ms;
}
uint32_t OSSleep(SystemServiceArguments *args) {
  return AutoBind<decltype(OSSleep_impl)>::thunk<OSSleep_impl>(args);
}

uint32_t OSSuspendThread(SystemServiceArguments *args) { return 0; }

uint32_t OSResumeThread(SystemServiceArguments *args) { return 0; }

// ================================================================
//  Critical Section
// ================================================================

static std::map<int, std::unique_ptr<CriticalSection>> g_cs;

uint32_t OSInitCriticalSection(SystemServiceArguments *args) {
  g_cs[args->r0] = std::make_unique<CriticalSection>();
  return args->r0;
}

uint32_t OSEnterCriticalSection(SystemServiceArguments *args) {
  sThreadHandler->CurrentThreadEnterCriticalSection(g_cs[args->r0].get());
  return args->r0;
}

uint32_t OSLeaveCriticalSection(SystemServiceArguments *args) {
  sThreadHandler->CurrentThreadExitCriticalSection(g_cs[args->r0].get());
  return args->r0;
}

// ================================================================
//  Event
// ================================================================

static std::map<uint32_t, std::unique_ptr<Event>> g_events;
static uint32_t g_next_event_id = 1;

static uint32_t CreateEvent_impl(uint32_t param0, uint32_t param1) {
  g_events[g_next_event_id] = std::unique_ptr<Event>(
      sThreadHandler->GetCurrentThread().CreateEvent(param0, param1));
  return g_next_event_id++;
}
uint32_t OSCreateEvent(SystemServiceArguments *args) {
  return AutoBind<decltype(CreateEvent_impl)>::thunk<CreateEvent_impl>(args);
}

// OSResetEvent
static uint32_t ResetEvent_impl(uint32_t id) {
  auto &event = *g_events[id];
  sThreadHandler->GetCurrentThread().ResetEvent(&event);
  return 0;
}
uint32_t OSResetEvent(SystemServiceArguments *args) {
  return AutoBind<decltype(ResetEvent_impl)>::thunk<ResetEvent_impl>(args);
}

// OSCloseEvent
static uint32_t CloseEvent_impl(uint32_t id) {
  auto it = g_events.find(id);
  if (it != g_events.end()) {
    g_events.erase(it);
    return 1; // Success
  }
  return 0; // Failure
}
uint32_t OSCloseEvent(SystemServiceArguments *args) {
  return AutoBind<decltype(CloseEvent_impl)>::thunk<CloseEvent_impl>(args);
}

// OSWaitForEvent (Updated to yield)
static uint32_t WaitForEvent_impl(uint32_t id, uint32_t timeout) {
  auto &event = *g_events[id];
  sThreadHandler->GetCurrentThread().WaitForEvent(&event, timeout);
  sThreadHandler
      ->CurrentThreadYield(); // Yield immediately after entering wait state
  return 0;
}
uint32_t OSWaitForEvent(SystemServiceArguments *args) {
  return AutoBind<decltype(WaitForEvent_impl)>::thunk<WaitForEvent_impl>(args);
}

static uint32_t SetEvent_impl(uint32_t id) {
  auto &event = *g_events[id];
  sThreadHandler->GetCurrentThread().SetEvent(&event);
  return 0;
}
uint32_t OSSetEvent(SystemServiceArguments *args) {
  return AutoBind<decltype(SetEvent_impl)>::thunk<SetEvent_impl>(args);
}

// ================================================================
//  Semaphore
// ================================================================

static std::map<uint32_t, std::unique_ptr<Semaphore>> g_semaphores;
static uint32_t g_next_sem_id = 1;

// OSCreateSemaphore(int initialCount, int maxCount)
static uint32_t CreateSemaphore_impl(int initialCount, int maxCount) {
  g_semaphores[g_next_sem_id] = std::unique_ptr<Semaphore>(
      sThreadHandler->GetCurrentThread().CreateSemaphore(initialCount,
                                                         maxCount));
  return g_next_sem_id++;
}
uint32_t OSCreateSemaphore(SystemServiceArguments *args) {
  return AutoBind<decltype(CreateSemaphore_impl)>::thunk<CreateSemaphore_impl>(
      args);
}

// OSWaitForSemaphore(uint32_t id, uint32_t timeout)
static uint32_t WaitForSemaphore_impl(uint32_t id, uint32_t timeout) {
  auto &sem = *g_semaphores[id];
  sThreadHandler->GetCurrentThread().WaitForSemaphore(&sem, timeout);
  sThreadHandler->CurrentThreadYield(); // Yield immediately
  return 0;
}
uint32_t OSWaitForSemaphore(SystemServiceArguments *args) {
  return AutoBind<decltype(WaitForSemaphore_impl)>::thunk<
      WaitForSemaphore_impl>(args);
}

// OSReleaseSemaphore(uint32_t id, int releaseCount, int* previousCount)
static uint32_t ReleaseSemaphore_impl(uint32_t id, int releaseCount,
                                      uint32_t prevCountPtr) {
  auto &sem = *g_semaphores[id];
  int prev = 0;
  sThreadHandler->GetCurrentThread().ReleaseSemaphore(&sem, releaseCount,
                                                      &prev);

  if (prevCountPtr != 0) {
    // Write back previous count to guest memory
    // We need to write 4 bytes to virtual address prevCountPtr
    // But here we are in handler, we can't easily write back unless we have
    // access to memory writing. AutoBind doesn't support pointers to guest
    // memory automatically as output. We'll use sMemoryManager or direct write.
    // Assuming we can write to it (user space).
    // Let's use uc_mem_write or sMemoryManager->Write
    // NOTE: AutoBind passes integer values for pointers.
    uc_mem_write(sExecutor->GetUcInstance(), prevCountPtr, &prev, sizeof(int));
  }
  return 1; // Non-zero for success (BOOL)
}
uint32_t OSReleaseSemaphore(SystemServiceArguments *args) {
  return AutoBind<decltype(ReleaseSemaphore_impl)>::thunk<
      ReleaseSemaphore_impl>(args);
}

// OSCloseSemaphore(uint32_t id)
static uint32_t CloseSemaphore_impl(uint32_t id) {
  auto it = g_semaphores.find(id);
  if (it != g_semaphores.end()) {
    g_semaphores.erase(it);
    return 1;
  }
  return 0;
}
uint32_t OSCloseSemaphore(SystemServiceArguments *args) {
  return AutoBind<decltype(CloseSemaphore_impl)>::thunk<CloseSemaphore_impl>(
      args);
}
