// svc_thread.cpp — 线程、同步原语 handler
// 根据 muteki/threading.h API 定义实现
#include "svc_common.h"

#include "Thread.h"
#include "ThreadHandler.h"
#include "executor.h"

#include <cstring>
#include <map>
#include <mutex>
#include <queue>

// ================================================================
//  Guest structure layout constants (from threading.h)
// ================================================================

// Magic values for descriptor types
static constexpr uint32_t THREAD_MAGIC = 0x100;
static constexpr uint32_t SEMAPHORE_MAGIC = 0x200;
static constexpr uint32_t EVENT_MAGIC = 0x201;
static constexpr uint32_t CRITSEC_MAGIC = 0x202;
static constexpr uint32_t MSGQUE_MAGIC = 0x202; // same as critsec per spec

// wait_result_t values
static constexpr uint32_t WAIT_RESULT_TIMEOUT = 0x82;
static constexpr uint32_t WAIT_RESULT_RESOLVED = 0x83;
static constexpr uint32_t WAIT_RESULT_ERROR = 0x84;

// ================================================================
//  Helper: allocate guest memory for a descriptor struct
// ================================================================
static VirtPtr AllocGuestStruct(size_t size) {
  VirtPtr addr = 0;
  sMemoryManager->DyanmicAlloc(&addr, size);
  if (addr) {
    void *real = sMemoryManager->GetRealAddr(addr);
    if (real)
      memset(real, 0, size);
  }
  return addr;
}

// ================================================================
//  线程操作
// ================================================================

// OSCreateThread(thread_func_t func, void *user_data, size_t stack_size, bool
// defer_start) Returns: thread_t* (guest pointer to thread descriptor)
static uint32_t OSCreateThread_impl(VirtPtr func, uint32_t user_data,
                                    size_t stack_size, uint32_t defer_start) {
  // Allocate a guest-visible thread_t descriptor (0x54 bytes)
  VirtPtr desc = AllocGuestStruct(0x54);
  if (!desc)
    return 0;

  auto *real = reinterpret_cast<uint8_t *>(sMemoryManager->GetRealAddr(desc));
  // Write magic
  *reinterpret_cast<uint32_t *>(real + 0x00) = THREAD_MAGIC;
  // Write thread_func
  *reinterpret_cast<uint32_t *>(real + 0x18) = func;
  // Write exit_code = 0
  *reinterpret_cast<uint32_t *>(real + 0x0C) = 0;

  // Create the actual emulated thread
  uint8_t priority = THREAD_PRIORITY_NORMAL;
  if (stack_size == 0)
    stack_size = 0x2000;

  if (!defer_start) {
    int tid = sThreadHandler->NewThread(func, user_data, priority, stack_size);
    // Store thread ID in unk_0x14 for our internal tracking
    *reinterpret_cast<uint32_t *>(real + 0x14) = (uint32_t)tid;
  } else {
    // Deferred start: create but don't start
    // For now, still create and track, but mark as suspended
    int tid = sThreadHandler->NewThread(func, user_data, priority, stack_size);
    *reinterpret_cast<uint32_t *>(real + 0x14) = (uint32_t)tid;
    // TODO: Actually implement suspend-on-create
  }

  return desc;
}
uint32_t OSCreateThread(SystemServiceArguments *args) {
  return AutoBind<decltype(OSCreateThread_impl)>::thunk<OSCreateThread_impl>(
      args);
}

// OSTerminateThread(thread_t *thr, int exit_code)
// Returns: 0 on success
static uint32_t OSTerminateThread_impl(VirtPtr thr_desc, int exit_code) {
  if (!thr_desc)
    return 0;
  auto *real =
      reinterpret_cast<uint8_t *>(sMemoryManager->GetRealAddr(thr_desc));
  if (!real)
    return 0;
  // Write exit code
  *reinterpret_cast<int32_t *>(real + 0x0C) = exit_code;
  // TODO: actually terminate the thread
  printf("[SVC] OSTerminateThread(desc=0x%X, exit_code=%d) - stub\n", thr_desc,
         exit_code);
  return 0;
}
uint32_t OSTerminateThread(SystemServiceArguments *args) {
  return AutoBind<decltype(OSTerminateThread_impl)>::thunk<
      OSTerminateThread_impl>(args);
}

// OSExitThread(int exit_code)
// Returns: 0 on success
static uint32_t OSExitThread_impl(int exit_code) {
  printf("[SVC] OSExitThread(exit_code=%d) - stub\n", exit_code);
  // TODO: terminate current thread
  return 0;
}
uint32_t OSExitThread(SystemServiceArguments *args) {
  return AutoBind<decltype(OSExitThread_impl)>::thunk<OSExitThread_impl>(args);
}

// OSGetThreadPriority(thread_t *thr)
// Returns: slot number (short)
static uint32_t OSGetThreadPriority_impl(VirtPtr thr_desc) {
  if (!thr_desc)
    return 0;
  auto *real =
      reinterpret_cast<uint8_t *>(sMemoryManager->GetRealAddr(thr_desc));
  if (!real)
    return 0;
  uint32_t tid = *reinterpret_cast<uint32_t *>(real + 0x14);
  Thread *t = sThreadHandler->GetThread(tid);
  if (t)
    return t->GetPriority();
  return 0;
}
uint32_t OSGetThreadPriority(SystemServiceArguments *args) {
  return AutoBind<decltype(OSGetThreadPriority_impl)>::thunk<
      OSGetThreadPriority_impl>(args);
}

// OSSetThreadPriority(thread_t *thr, short new_slot)
// Returns: true/false
static uint32_t OSSetThreadPriority_impl(VirtPtr thr_desc, uint32_t new_slot) {
  if (!thr_desc)
    return 0;
  auto *real =
      reinterpret_cast<uint8_t *>(sMemoryManager->GetRealAddr(thr_desc));
  if (!real)
    return 0;
  uint32_t tid = *reinterpret_cast<uint32_t *>(real + 0x14);
  // Update slot in guest descriptor
  *reinterpret_cast<uint16_t *>(real + 0x22) = (uint16_t)new_slot;
  sThreadHandler->SetThreadPriority(tid, (uint8_t)new_slot);
  return 1;
}
uint32_t OSSetThreadPriority(SystemServiceArguments *args) {
  return AutoBind<decltype(OSSetThreadPriority_impl)>::thunk<
      OSSetThreadPriority_impl>(args);
}

// OSSuspendThread(thread_t *thr)
// Returns: true/false
static uint32_t OSSuspendThread_impl(VirtPtr thr_desc) {
  printf("[SVC] OSSuspendThread(desc=0x%X) - stub\n", thr_desc);
  return 1;
}
uint32_t OSSuspendThread(SystemServiceArguments *args) {
  return AutoBind<decltype(OSSuspendThread_impl)>::thunk<OSSuspendThread_impl>(
      args);
}

// OSResumeThread(thread_t *thr)
// Returns: true/false
static uint32_t OSResumeThread_impl(VirtPtr thr_desc) {
  printf("[SVC] OSResumeThread(desc=0x%X) - stub\n", thr_desc);
  return 1;
}
uint32_t OSResumeThread(SystemServiceArguments *args) {
  return AutoBind<decltype(OSResumeThread_impl)>::thunk<OSResumeThread_impl>(
      args);
}

// OSWakeUpThread(thread_t *thr)
// Returns: true/false
static uint32_t OSWakeUpThread_impl(VirtPtr thr_desc) {
  printf("[SVC] OSWakeUpThread(desc=0x%X) - stub\n", thr_desc);
  return 1;
}
uint32_t OSWakeUpThread(SystemServiceArguments *args) {
  return AutoBind<decltype(OSWakeUpThread_impl)>::thunk<OSWakeUpThread_impl>(
      args);
}

// OSSleep(short millis)
// Void return
static void OSSleep_impl(uint32_t ms) { g_SyncFactory->SleepMillis(ms); }
uint32_t OSSleep(SystemServiceArguments *args) {
  return AutoBind<decltype(OSSleep_impl)>::thunk<OSSleep_impl>(args);
}

// ================================================================
//  Semaphore
// ================================================================

static std::map<uint32_t, ISemaphore *> g_semaphores;

// OSCreateSemaphore(short init_ctr)
// Returns: semaphore_t* (guest pointer)
static uint32_t CreateSemaphore_impl(uint32_t init_ctr) {
  // Allocate guest-visible semaphore_s descriptor (0x14 bytes)
  VirtPtr desc = AllocGuestStruct(0x14);
  if (!desc)
    return 0;

  auto *real = reinterpret_cast<uint8_t *>(sMemoryManager->GetRealAddr(desc));
  // magic = 0x200
  *reinterpret_cast<uint32_t *>(real + 0x00) = SEMAPHORE_MAGIC;
  // ctr
  *reinterpret_cast<uint16_t *>(real + 0x08) = (uint16_t)init_ctr;

  ISemaphore *sem = g_SyncFactory->CreateSemaphoreObject(init_ctr, 0x7FFF);
  g_semaphores[desc] = sem;
  return desc;
}
uint32_t OSCreateSemaphore(SystemServiceArguments *args) {
  return AutoBind<decltype(CreateSemaphore_impl)>::thunk<CreateSemaphore_impl>(
      args);
}

// OSWaitForSemaphore(semaphore_t *semaphore, short timeout)
// Returns: wait_result_t
static uint32_t WaitForSemaphore_impl(VirtPtr desc, uint32_t timeout) {
  auto it = g_semaphores.find(desc);
  if (it != g_semaphores.end()) {
    bool ok = it->second->Wait(timeout);
    return ok ? WAIT_RESULT_RESOLVED : WAIT_RESULT_TIMEOUT;
  }
  return WAIT_RESULT_ERROR;
}
uint32_t OSWaitForSemaphore(SystemServiceArguments *args) {
  return AutoBind<decltype(WaitForSemaphore_impl)>::thunk<
      WaitForSemaphore_impl>(args);
}

// OSReleaseSemaphore(semaphore_t *semaphore)
// Returns: true/false
static uint32_t ReleaseSemaphore_impl(VirtPtr desc) {
  auto it = g_semaphores.find(desc);
  if (it != g_semaphores.end()) {
    it->second->Release(1, nullptr);
    return 1;
  }
  return 0;
}
uint32_t OSReleaseSemaphore(SystemServiceArguments *args) {
  return AutoBind<decltype(ReleaseSemaphore_impl)>::thunk<
      ReleaseSemaphore_impl>(args);
}

// OSCloseSemaphore(semaphore_t *semaphore)
// Returns: true/false
static uint32_t CloseSemaphore_impl(VirtPtr desc) {
  auto it = g_semaphores.find(desc);
  if (it != g_semaphores.end()) {
    delete it->second;
    g_semaphores.erase(it);
    // Free guest descriptor memory
    sMemoryManager->DynamicFree(desc);
    return 1;
  }
  return 0;
}
uint32_t OSCloseSemaphore(SystemServiceArguments *args) {
  return AutoBind<decltype(CloseSemaphore_impl)>::thunk<CloseSemaphore_impl>(
      args);
}

// ================================================================
//  Event
// ================================================================

static std::map<uint32_t, IEvent *> g_events;

// OSCreateEvent(short latch_on, int flag)
// Returns: event_t* (guest pointer)
static uint32_t CreateEvent_impl(uint32_t latch_on, uint32_t flag) {
  // Allocate guest-visible event_s descriptor (0x14 bytes)
  VirtPtr desc = AllocGuestStruct(0x14);
  if (!desc)
    return 0;

  auto *real = reinterpret_cast<uint8_t *>(sMemoryManager->GetRealAddr(desc));
  // magic = 0x201
  *reinterpret_cast<uint32_t *>(real + 0x00) = EVENT_MAGIC;
  // flag
  *reinterpret_cast<uint32_t *>(real + 0x04) = flag;
  // latch_on
  *reinterpret_cast<uint16_t *>(real + 0x08) = (uint16_t)latch_on;

  // latch_on != 0 means manual reset (inhibit auto-clear)
  IEvent *ev = g_SyncFactory->CreateEventObject(latch_on != 0, flag != 0);
  g_events[desc] = ev;
  return desc;
}
uint32_t OSCreateEvent(SystemServiceArguments *args) {
  return AutoBind<decltype(CreateEvent_impl)>::thunk<CreateEvent_impl>(args);
}

// OSWaitForEvent(event_t *event, short timeout)
// Returns: wait_result_t
static uint32_t WaitForEvent_impl(VirtPtr desc, uint32_t timeout) {
  auto it = g_events.find(desc);
  if (it != g_events.end()) {
    bool ok = it->second->Wait(timeout);
    if (ok) {
      // Update guest flag if not latched (auto-reset)
      auto *real =
          reinterpret_cast<uint8_t *>(sMemoryManager->GetRealAddr(desc));
      if (real) {
        uint16_t latch = *reinterpret_cast<uint16_t *>(real + 0x08);
        if (latch == 0) {
          *reinterpret_cast<uint32_t *>(real + 0x04) = 0;
        }
      }
      return WAIT_RESULT_RESOLVED;
    }
    return WAIT_RESULT_TIMEOUT;
  }
  return WAIT_RESULT_ERROR;
}
uint32_t OSWaitForEvent(SystemServiceArguments *args) {
  return AutoBind<decltype(WaitForEvent_impl)>::thunk<WaitForEvent_impl>(args);
}

// OSSetEvent(event_t *event)
// Returns: true/false
static uint32_t SetEvent_impl(VirtPtr desc) {
  auto it = g_events.find(desc);
  if (it != g_events.end()) {
    it->second->Set();
    // Update guest flag
    auto *real = reinterpret_cast<uint8_t *>(sMemoryManager->GetRealAddr(desc));
    if (real) {
      *reinterpret_cast<uint32_t *>(real + 0x04) = 1;
    }
    return 1;
  }
  return 0;
}
uint32_t OSSetEvent(SystemServiceArguments *args) {
  return AutoBind<decltype(SetEvent_impl)>::thunk<SetEvent_impl>(args);
}

// OSResetEvent(event_t *event)
// Returns: true/false
static uint32_t ResetEvent_impl(VirtPtr desc) {
  auto it = g_events.find(desc);
  if (it != g_events.end()) {
    it->second->Reset();
    // Update guest flag
    auto *real = reinterpret_cast<uint8_t *>(sMemoryManager->GetRealAddr(desc));
    if (real) {
      *reinterpret_cast<uint32_t *>(real + 0x04) = 0;
    }
    return 1;
  }
  return 0;
}
uint32_t OSResetEvent(SystemServiceArguments *args) {
  return AutoBind<decltype(ResetEvent_impl)>::thunk<ResetEvent_impl>(args);
}

// OSCloseEvent(event_t *event)
// Returns: true/false
static uint32_t CloseEvent_impl(VirtPtr desc) {
  auto it = g_events.find(desc);
  if (it != g_events.end()) {
    delete it->second;
    g_events.erase(it);
    sMemoryManager->DynamicFree(desc);
    return 1;
  }
  return 0;
}
uint32_t OSCloseEvent(SystemServiceArguments *args) {
  return AutoBind<decltype(CloseEvent_impl)>::thunk<CloseEvent_impl>(args);
}

// ================================================================
//  Critical Section
// ================================================================

static std::map<uint32_t, ICriticalSection *> g_cs;

// OSInitCriticalSection(critical_section_t *cs)
// Void return. Writes magic into the guest-provided struct.
static void OSInitCriticalSection_impl(VirtPtr cs_ptr) {
  if (!cs_ptr)
    return;
  auto *real = reinterpret_cast<uint8_t *>(sMemoryManager->GetRealAddr(cs_ptr));
  if (!real)
    return;

  // Write magic = 0x202
  *reinterpret_cast<uint32_t *>(real + 0x00) = CRITSEC_MAGIC;
  // thr = NULL
  *reinterpret_cast<uint32_t *>(real + 0x04) = 0;
  // refcount = 0
  *reinterpret_cast<uint16_t *>(real + 0x08) = 0;
  // clear wait_state
  memset(real + 0x0A, 0, 9);

  auto cs = g_SyncFactory->CreateCriticalSectionObject();
  g_cs[cs_ptr] = cs;
}
uint32_t OSInitCriticalSection(SystemServiceArguments *args) {
  return AutoBind<decltype(OSInitCriticalSection_impl)>::thunk<
      OSInitCriticalSection_impl>(args);
}

// OSEnterCriticalSection(critical_section_t *cs)
// Void return
static void OSEnterCriticalSection_impl(VirtPtr cs_ptr) {
  auto it = g_cs.find(cs_ptr);
  if (it != g_cs.end()) {
    it->second->Enter();
    // Update guest refcount
    auto *real =
        reinterpret_cast<uint8_t *>(sMemoryManager->GetRealAddr(cs_ptr));
    if (real) {
      uint16_t &rc = *reinterpret_cast<uint16_t *>(real + 0x08);
      rc++;
    }
  }
}
uint32_t OSEnterCriticalSection(SystemServiceArguments *args) {
  return AutoBind<decltype(OSEnterCriticalSection_impl)>::thunk<
      OSEnterCriticalSection_impl>(args);
}

// OSLeaveCriticalSection(critical_section_t *cs)
// Void return
static void OSLeaveCriticalSection_impl(VirtPtr cs_ptr) {
  auto it = g_cs.find(cs_ptr);
  if (it != g_cs.end()) {
    it->second->Leave();
    auto *real =
        reinterpret_cast<uint8_t *>(sMemoryManager->GetRealAddr(cs_ptr));
    if (real) {
      uint16_t &rc = *reinterpret_cast<uint16_t *>(real + 0x08);
      if (rc > 0)
        rc--;
    }
  }
}
uint32_t OSLeaveCriticalSection(SystemServiceArguments *args) {
  return AutoBind<decltype(OSLeaveCriticalSection_impl)>::thunk<
      OSLeaveCriticalSection_impl>(args);
}

// OSDeleteCriticalSection(critical_section_t *cs)
// Void return
static void OSDeleteCriticalSection_impl(VirtPtr cs_ptr) {
  auto it = g_cs.find(cs_ptr);
  if (it != g_cs.end()) {
    delete it->second;
    g_cs.erase(it);
  }
  // Zero out the guest struct
  if (cs_ptr) {
    auto *real =
        reinterpret_cast<uint8_t *>(sMemoryManager->GetRealAddr(cs_ptr));
    if (real)
      memset(real, 0, 0x14);
  }
}
uint32_t OSDeleteCriticalSection(SystemServiceArguments *args) {
  return AutoBind<decltype(OSDeleteCriticalSection_impl)>::thunk<
      OSDeleteCriticalSection_impl>(args);
}

// ================================================================
//  Message Queue
// ================================================================

// Host-side message queue implementation
struct HostMsgQueue {
  std::mutex mtx;
  std::deque<std::array<char, 16>> messages;
  unsigned short max_size;

  HostMsgQueue(unsigned short sz) : max_size(sz) {}

  bool push(const char *msg) {
    std::lock_guard lk(mtx);
    if (messages.size() >= max_size)
      return false;
    std::array<char, 16> m;
    memcpy(m.data(), msg, 16);
    messages.push_back(m);
    return true;
  }

  bool peek(char *out) {
    std::lock_guard lk(mtx);
    if (messages.empty())
      return false;
    memcpy(out, messages.front().data(), 16);
    return true;
  }

  bool pop(char *out) {
    std::lock_guard lk(mtx);
    if (messages.empty())
      return false;
    memcpy(out, messages.front().data(), 16);
    messages.pop_front();
    return true;
  }
};

static std::map<uint32_t, HostMsgQueue *> g_msgqueues;

// OSCreateMsgQue(unsigned short size)
// Returns: message_queue_t* (guest pointer)
static uint32_t OSCreateMsgQue_impl(uint32_t size) {
  // Allocate guest-visible message_queue_s descriptor (0x14 bytes)
  VirtPtr desc = AllocGuestStruct(0x14);
  if (!desc)
    return 0;

  auto *real = reinterpret_cast<uint8_t *>(sMemoryManager->GetRealAddr(desc));
  // magic = 0x202
  *reinterpret_cast<uint32_t *>(real + 0x00) = MSGQUE_MAGIC;

  // Also allocate the nonatomic storage struct and the message buffer in guest
  // For now, we just store a pointer to a host-side queue
  // storage pointer can be NULL for our implementation
  *reinterpret_cast<uint32_t *>(real + 0x04) = 0;

  auto *q = new HostMsgQueue((unsigned short)size);
  g_msgqueues[desc] = q;
  return desc;
}
uint32_t OSCreateMsgQue(SystemServiceArguments *args) {
  return AutoBind<decltype(OSCreateMsgQue_impl)>::thunk<OSCreateMsgQue_impl>(
      args);
}

// OSPostMsgQue(message_queue_t *queue, const message_queue_message_t *message)
// Returns: true/false
static uint32_t OSPostMsgQue_impl(VirtPtr queue_desc, VirtPtr msg_ptr) {
  auto it = g_msgqueues.find(queue_desc);
  if (it == g_msgqueues.end())
    return 0;
  if (!msg_ptr)
    return 0;

  auto *msg_real =
      reinterpret_cast<const char *>(sMemoryManager->GetRealAddr(msg_ptr));
  if (!msg_real)
    return 0;

  return it->second->push(msg_real) ? 1 : 0;
}
uint32_t OSPostMsgQue(SystemServiceArguments *args) {
  return AutoBind<decltype(OSPostMsgQue_impl)>::thunk<OSPostMsgQue_impl>(args);
}

// OSSendMsgQue(message_queue_t *queue, const message_queue_message_t *message)
// Same as Post but reschedules immediately. For our emulator, equivalent to
// Post. Returns: true/false
static uint32_t OSSendMsgQue_impl(VirtPtr queue_desc, VirtPtr msg_ptr) {
  auto it = g_msgqueues.find(queue_desc);
  if (it == g_msgqueues.end())
    return 0;
  if (!msg_ptr)
    return 0;

  auto *msg_real =
      reinterpret_cast<const char *>(sMemoryManager->GetRealAddr(msg_ptr));
  if (!msg_real)
    return 0;

  return it->second->push(msg_real) ? 1 : 0;
}
uint32_t OSSendMsgQue(SystemServiceArguments *args) {
  return AutoBind<decltype(OSSendMsgQue_impl)>::thunk<OSSendMsgQue_impl>(args);
}

// OSPeekMsgQue(message_queue_t *queue, message_queue_message_t *message)
// Returns: true/false
static uint32_t OSPeekMsgQue_impl(VirtPtr queue_desc, VirtPtr msg_ptr) {
  auto it = g_msgqueues.find(queue_desc);
  if (it == g_msgqueues.end())
    return 0;
  if (!msg_ptr)
    return 0;

  auto *msg_real =
      reinterpret_cast<char *>(sMemoryManager->GetRealAddr(msg_ptr));
  if (!msg_real)
    return 0;

  return it->second->peek(msg_real) ? 1 : 0;
}
uint32_t OSPeekMsgQue(SystemServiceArguments *args) {
  return AutoBind<decltype(OSPeekMsgQue_impl)>::thunk<OSPeekMsgQue_impl>(args);
}

// OSGetMsgQue(message_queue_t *queue, message_queue_message_t *message)
// Returns: true/false
static uint32_t OSGetMsgQue_impl(VirtPtr queue_desc, VirtPtr msg_ptr) {
  auto it = g_msgqueues.find(queue_desc);
  if (it == g_msgqueues.end())
    return 0;
  if (!msg_ptr)
    return 0;

  auto *msg_real =
      reinterpret_cast<char *>(sMemoryManager->GetRealAddr(msg_ptr));
  if (!msg_real)
    return 0;

  return it->second->pop(msg_real) ? 1 : 0;
}
uint32_t OSGetMsgQue(SystemServiceArguments *args) {
  return AutoBind<decltype(OSGetMsgQue_impl)>::thunk<OSGetMsgQue_impl>(args);
}

// OSCloseMsgQue(message_queue_t *queue)
// Returns: true/false
static uint32_t OSCloseMsgQue_impl(VirtPtr queue_desc) {
  auto it = g_msgqueues.find(queue_desc);
  if (it != g_msgqueues.end()) {
    delete it->second;
    g_msgqueues.erase(it);
    sMemoryManager->DynamicFree(queue_desc);
    return 1;
  }
  return 0;
}
uint32_t OSCloseMsgQue(SystemServiceArguments *args) {
  return AutoBind<decltype(OSCloseMsgQue_impl)>::thunk<OSCloseMsgQue_impl>(
      args);
}
