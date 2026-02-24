#include "ThreadHandler.h"
#include "Thread.h"

StateManager* StateManager::_instance = nullptr;

int StateManager::NewThread(VirtPtr start, uint32_t arg, uint8_t priority,
	size_t stackSize) {
	std::lock_guard<std::mutex> lock(_threadMapMutex);

	Thread* newThread = new Thread(start, arg, priority, stackSize);
	int id = newThread->GetId();
	_threads[id] = newThread;

	// Start the thread natively
	newThread->Start();
	return id;
}

int StateManager::SetThreadPriority(int threadId, uint8_t priority) {
	std::lock_guard<std::mutex> lock(_threadMapMutex);
	auto it = _threads.find(threadId);
	if (it != _threads.end()) {
		it->second->SetPriority(priority);
		return 1; // Or adjust native thread priority using OS APIs here
	}
	return 0; // NULL equivalent
}

Thread* StateManager::GetThread(int threadId) {
	std::lock_guard<std::mutex> lock(_threadMapMutex);
	auto it = _threads.find(threadId);
	if (it != _threads.end()) {
		return it->second;
	}
	return nullptr;
}

Thread* StateManager::GetCurrentThread() {
	return currentThread;
}

int StateManager::GetCurrentThreadId() {
	return currentThread->GetId();
}
