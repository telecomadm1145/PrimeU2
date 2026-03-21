#ifndef THREAD_HANDLER_H
#define THREAD_HANDLER_H

#include "common.h"
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

class Thread;

// Windows.h defines macros that collide with our method names
#undef GetCurrentThread
#undef GetCurrentThreadId
#undef SetThreadPriority

/// Manages all emulated guest threads.
/// Renamed from the original `StateManager` to reflect its actual
/// responsibility.
class ThreadManager {
public:
  static ThreadManager *GetInstance() {
    return !_instance ? _instance = new ThreadManager : _instance;
  }

  int NewThread(VirtPtr start, uint32_t arg = 0,
                uint8_t priority = THREAD_PRIORITY_NORMAL,
                size_t stackSize = 0x2000);

  int SetThreadPriority(int threadId, uint8_t priority);
  Thread *GetThread(int threadId);

  // Called from within an emulated thread to get its active Thread back-pointer
  Thread *GetCurrentThread();
  int GetCurrentThreadId();

private:
  ThreadManager() {}
  ~ThreadManager() {}
  ThreadManager(ThreadManager const &) = delete;
  void operator=(ThreadManager const &) = delete;
  static ThreadManager *_instance;

  std::mutex _threadMapMutex;
  std::unordered_map<int, Thread *> _threads;
  friend class Thread;
};

#define sThreadHandler ThreadManager::GetInstance()

#endif
