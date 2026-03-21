#include "ThreadHandler.h"
#include "Thread.h"

ThreadManager *ThreadManager::_instance = nullptr;

int ThreadManager::NewThread(VirtPtr start, uint32_t arg, uint8_t priority,
                             size_t stackSize) {
  std::lock_guard<std::mutex> lock(_threadMapMutex);

  Thread *newThread = new Thread(start, arg, priority, stackSize);
  int id = newThread->GetId();
  _threads[id] = newThread;

  // Start the thread natively
  newThread->Start();
  return id;
}

int ThreadManager::SetThreadPriority(int threadId, uint8_t priority) {
  std::lock_guard<std::mutex> lock(_threadMapMutex);
  auto it = _threads.find(threadId);
  if (it != _threads.end()) {
    it->second->SetPriority(priority);
    return 1;
  }
  return 0;
}

Thread *ThreadManager::GetThread(int threadId) {
  std::lock_guard<std::mutex> lock(_threadMapMutex);
  auto it = _threads.find(threadId);
  if (it != _threads.end()) {
    return it->second;
  }
  return nullptr;
}

Thread *ThreadManager::GetCurrentThread() { return currentThread; }

int ThreadManager::GetCurrentThreadId() { return currentThread->GetId(); }
