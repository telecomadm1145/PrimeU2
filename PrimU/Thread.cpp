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
  }
}

Thread::~Thread() {
  sMemoryManager->DynamicFree(_stackAddr);
  if (_uc) {
    uc_close(_uc);
  }
}

bool Thread::MapSharedMemoryToLocalEngine() {
  for (auto *block : sMemoryManager->GetMemoryBlocks()) {
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

void Thread::Start() { _nativeThread = std::thread(&Thread::ThreadProc, this); }

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
  auto pc = _startPc;
  while (1) {
    uc_err err = uc_emu_start(_uc, pc, 0, 0, 0);
    if (err == UC_ERR_INSN_INVALID) {
      uc_reg_read(_uc, UC_ARM_REG_PC, &pc);
      auto pp = __GET(uint32_t *, pc);
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

void Thread::ExecuteCustomCode(VirtPtr pc, Arg a, Arg b, Arg c, Arg d) {
  if (!inited) {
    if (!MapSharedMemoryToLocalEngine()) {
      printf("Thread %d shutting down due to memory mapping failure.\n", _id);
      return;
    }
    sExecutor->RegisterHooksForEngine(_uc);
    inited = true;
  }
  // Set up ARM calling convention registers R0-R3
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

  // Set LR to magic value; when guest code does BX LR, it jumps to
  // 0xDEADC0DE which triggers an address fault and we detect return.
  uint32_t lr = 0xDEADC0DE;
  uc_reg_write(_uc, UC_ARM_REG_LR, &lr);
  uc_reg_write(_uc, UC_ARM_REG_PC, &pc);
  uc_reg_write(_uc, UC_ARM_REG_SP, &_startSp);

  VirtPtr current_pc = pc;
  while (1) {
    uc_err err = uc_emu_start(_uc, current_pc, 0, 0, 0);
    if (err == UC_ERR_INSN_INVALID) {
        uc_reg_read(_uc, UC_ARM_REG_PC, &pc);
        {
            uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, sp, pc, lr;
            void* args[16] = { &r0, &r1, &r2,  &r3,  &r4,  &r5, &r6, &r7,
                              &r8, &r9, &r10, &r11, &r12, &sp, &lr, &pc };
            int regs[16] = { UC_ARM_REG_R0,  UC_ARM_REG_R1, UC_ARM_REG_R2,  UC_ARM_REG_R3,
                            UC_ARM_REG_R4,  UC_ARM_REG_R5, UC_ARM_REG_R6,  UC_ARM_REG_R7,
                            UC_ARM_REG_R8,  UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
                            UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR,  UC_ARM_REG_PC };
            uc_reg_read_batch(_uc, regs, args, 16);
            printf("    r0: %08X|%i\n    r1: %08X|%i\n    r2: %08X|%i\n    r3: %08X|%i\n "
                "   r4: %08X|%i\n"
                "    r5: %08X|%i\n    r6: %08X|%i\n    r7: %08X|%i\n    r8: %08X|%i\n "
                "   r9: %08X|%i\n"
                "   r10: %08X|%i\n   r11: %08X|%i\n   r12: %08X|%i\n"
                "    sp: %08X\n    pc: %08X\n    lr: %08X\n",
                r0, r0, r1, r1, r2, r2, r3, r3, r4, r4, r5, r5, +r6, r6, r7, r7, r8,
                r8, r9, r9, r10, r10, r11, r11, r12, r12, sp, pc, lr);
        }
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
      uc_reg_read(_uc, UC_ARM_REG_PC, &current_pc);
      if (current_pc == 0xDEADC0DE)
        break;
      printf("Thread %d ExecuteCustomCode error: %u (%s) %p\n", _id, err,
             uc_strerror(err), (void *)(uintptr_t)current_pc);
      break;
    }
  }
}