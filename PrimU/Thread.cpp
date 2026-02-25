#include "Thread.h"
#include "ThreadHandler.h"

int Thread::GenerateUniqueId() {
	static std::atomic<int> currentInt = -1;
	return ++currentInt;
}

Thread::Thread(VirtPtr start, uint32_t arg, uint8_t priority, size_t stackSize)
	: _id(GenerateUniqueId()), _startPc(start), _startArg(arg),
	_priority(priority) {

	if (stackSize != 0)
		_stackSize = stackSize;

	sMemoryManager->DyanmicAlloc(&_stackAddr, _stackSize);
	_startSp = sMemoryManager->GetAllocSize(_stackAddr) + _stackAddr;
	printf("Thread [%i] -> %08X stack starts at %08X and ends at %08X\n", _id,
		start, _startSp, _stackAddr);

	// Initialize the thread's own Unicorn engine instance
	uc_err err = uc_open(UC_ARCH_ARM, UC_MODE_THUMB, &_uc);
	if (err != UC_ERR_OK) {
		printf("Failed on uc_open with error returned: %u\n", err);
		// In a real app we'd throw or handle this gracefully
	}
}

Thread::~Thread() {
	sMemoryManager->DynamicFree(_stackAddr);
	if (_uc) {
		uc_close(_uc);
	}
}

bool Thread::MapSharedMemoryToLocalEngine() {
	// Since Unicorn Engine cannot truly share memory regions natively without
	// mapping host pointers, we retrieve all mapped memory regions from
	// MemoryManager and map them into this thread's local `_uc` instance using
	// `uc_mem_map_ptr`.

	// Note: Depending on your MemoryManager design, you need a way to get
	// all active memory blocks. Assuming there's a way to iterate them:
	for (auto* block : sMemoryManager->GetMemoryBlocks()) {
		uc_err err = uc_mem_map_ptr(_uc, block->GetVAddr(), block->GetSize(),
			UC_PROT_ALL, block->GetRAddr());
		if (err != UC_ERR_OK) {
			printf(
				"Failed to map memory block %08lX into Thread %d's uc_engine: %u\n",
				(uint32_t)block->GetVAddr(), _id, err);
			return false;
		}
	}
	return true;
}

void Thread::Start() {
	// Launch the native std::thread
	_nativeThread = std::thread(&Thread::ThreadProc, this);
}

void Thread::ThreadProc() {
	printf("Starting native thread for emulated Thread %d\n", _id);
	currentThread = this;
	if (!MapSharedMemoryToLocalEngine()) {
		printf("Thread %d shutting down due to memory mapping failure.\n", _id);
		return;
	}

	// Set up initial registers
	uc_reg_write(_uc, UC_ARM_REG_R0, &_startArg);
	uc_reg_write(_uc, UC_ARM_REG_SP, &_startSp);
	uc_reg_write(_uc, UC_ARM_REG_PC, &_startPc);

	sExecutor->RegisterHooksForEngine(_uc);

	// Main execution loop for this thread's engine
	// It will run until an error occurs or it explicitly exits (e.g. via an SVC
	// that stops it).
	auto pc = _startPc;
	while (1) {
		uc_err err = uc_emu_start(_uc, pc, 0, 0, 0);
		if (err == UC_ERR_INSN_INVALID) {
			uc_reg_read(_uc, UC_ARM_REG_PC, &pc);
			auto pp = __GET(uint32_t*, pc);
			if (!pc)
				break;
			if (*pp != 0xee17ff7e)
				break;
			uint32_t cpsr;
			uc_reg_read(_uc, UC_ARM_REG_CPSR, &cpsr);
			cpsr |= (1 << 30);
			uc_reg_write(_uc, UC_ARM_REG_CPSR, &cpsr);
			pc += 4;
			uc_reg_write(_uc, UC_ARM_REG_PC, &pc);
			continue;
		}

		if (err) {
			printf("Thread %d emu_start returned: %u (%s)\n", _id, err,
				uc_strerror(err));
			break;
		}
	}
	printf("Emulated Thread %d exited.\n", _id);
}

void Thread::Sleep(uint32_t time) { g_SyncFactory->SleepMillis(time); }

// ================================
// Event API 实现 (Wrappers)
// ================================

IEvent* Thread::CreateEvent(bool bManualReset, bool bInitialState) {
	return g_SyncFactory->CreateEventObject(bManualReset, bInitialState);
}

void Thread::SetEvent(IEvent* ev) {
	if (ev)
		ev->Set();
}

void Thread::ResetEvent(IEvent* ev) {
	if (ev)
		ev->Reset();
}

void Thread::WaitForEvent(IEvent* ev, int timeoutMillis) {
	if (ev)
		ev->Wait(timeoutMillis);
}

// ================================
// Semaphore API 实现 (Wrappers)
// ================================

ISemaphore* Thread::CreateSemaphore(int initialCount, int maxCount) {
	return g_SyncFactory->CreateSemaphoreObject(initialCount, maxCount);
}

void Thread::WaitForSemaphore(ISemaphore* sem, int timeoutMillis) {
	if (sem)
		sem->Wait(timeoutMillis);
}

void Thread::ReleaseSemaphore(ISemaphore* sem, int releaseCount,
	int* previousCount) {
	if (sem)
		sem->Release(releaseCount, previousCount);
}

void Thread::ExecuteCustomCode(VirtPtr pc, Arg a, Arg b, Arg c, Arg d)
{
	if (!inited) {
		if (!MapSharedMemoryToLocalEngine()) {
			printf("Thread %d shutting down due to memory mapping failure.\n", _id);
			return;
		}
		sExecutor->RegisterHooksForEngine(_uc);
		inited = true;
	}
	// 按照 ARM 调用约定设置参数寄存器 R0-R3
	if (a.has_value()) {
		uint32_t val = a.value();
		uc_reg_write(_uc, UC_ARM_REG_R0, &val);
	}
	if (b.has_value()) {
		uint32_t val = b.value();
		uc_reg_write(_uc, UC_ARM_REG_R1, &val);
	}
	if (c.has_value()) {
		uint32_t val = c.value();
		uc_reg_write(_uc, UC_ARM_REG_R2, &val);
	}
	if (d.has_value()) {
		uint32_t val = d.value();
		uc_reg_write(_uc, UC_ARM_REG_R3, &val);
	}

	// 设置 LR 为 0，当代码执行 BX LR 返回时会跳转到地址 0，用于检测返回
	uint32_t lr = 0xDEADC0DE;
	uc_reg_write(_uc, UC_ARM_REG_LR, &lr);
	uc_reg_write(_uc, UC_ARM_REG_PC, &pc);
	uc_reg_write(_uc, UC_ARM_REG_SP, &_startSp);

	// 执行循环
	VirtPtr current_pc = pc;
	while (1) {
		uc_err err = uc_emu_start(_uc, current_pc, 0, 0, 0);

		if (err) {
			uc_reg_read(_uc, UC_ARM_REG_PC, &current_pc);
			if (current_pc == 0xDEADC0DE)
				break;
			printf("Thread %d ExecuteCustomCode error: %u (%s) %p\n",
				_id, err, uc_strerror(err), current_pc);
			break;
		}

	}
}