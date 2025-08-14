

#include "ThreadHandler.h"
#include "Thread.h"

StateManager* StateManager::_instance = nullptr;

int StateManager::NewThread(VirtPtr start, uint32_t arg, uint8_t priority, size_t stackSize)
{
	Thread* newThread = new Thread(&_currentThread, start, arg, priority, stackSize);
	return newThread->GetId();
}

void StateManager::LoadCurrentThreadState()
{
	if (_currentThread)
		_currentThread->LoadState();
}

/*
* Do not call function outside of a callback!!
*   as it may save an errant PC value because
*   of a UC engine bug
*/
void StateManager::SaveCurrentThreadState()
{
	if (_currentThread)
		_currentThread->SaveState();
}

uint32_t StateManager::GetCurrentThreadPC()
{
	if (_currentThread)
		return _currentThread->GetCurrentPC();
	return 0;
}

void StateManager::SwitchThread()
{

	if (_currentThread != _currentThread->GetNextThread()) {
		_currentThread = _currentThread->GetNextThread();
		_currentThread->LoadState();
	}
}


int StateManager::SetThreadPriority(int threadId, uint8_t priority)
{
	for (Thread* thread = nullptr; thread != _currentThread; thread = thread->GetNextThread()) {
		if (thread == nullptr)
			thread = _currentThread;

		if (thread->GetId() == threadId) {
			thread->SetPriority(priority);
			return 1;
		}
	}
	return NULL;
}

void StateManager::WakeThread(int threadId)
{
	//_currentThread->SaveState();
	//for (Thread* thread = nullptr; thread != _currentThread; thread = thread->GetNextThread()) {
	//	if (thread == nullptr)
	//		thread = _currentThread;

	//	if (thread->GetId() == threadId) {
	//		_currentThread = thread;
	//		_currentThread->LoadState();
	//		return;
	//	}
	//}
}


int StateManager::GetCurrentThreadId() const
{
	return _currentThread->GetId();
}

uint32_t StateManager::GetCurrentThreadQuantum() const
{
	return _currentThread->GetTimeQuantum();
}

bool StateManager::CanCurrentThreadRun()
{
	return _currentThread->CanRun();
}

void StateManager::InitCriticalSection(CriticalSection* criticalSection)
{
	__debugbreak();
}


void StateManager::CurrentThreadEnterCriticalSection(CriticalSection* criticalSection)
{
	_currentThread->EnterCriticalSection(criticalSection);
}

void StateManager::CurrentThreadExitCriticalSection(CriticalSection* criticalSection)
{
	_currentThread->LeaveCriticalSection(criticalSection);
}

void StateManager::CurrentThreadSleep(uint32_t time)
{
	_currentThread->Sleep(time);
}

