#include "executor.h"
#include "CommandServer.h"
#define ENABLE_GDB_STUB
#include "FaultHandler.h"
#include "GdbStub.h"

#include "InterruptController.h"
#include "SvcDispatcher.h"
#include "Thread.h"
#include "ThreadHandler.h"
#include "TimerController.h"
#include "executable.h"
#include "memory.h"

#include <chrono>
#include <cstdio>

Executor *Executor::m_instance = nullptr;

#define callAndcheckError(f)                                                   \
  m_err = f;                                                                   \
  if (m_err != UC_ERR_OK)                                                      \
  return false

bool Executor::Initialize(Executable *exec) {
  if (!exec)
    return false;

  m_exec = exec;

  if (!m_uc) {
    m_err = uc_open(UC_ARCH_ARM, UC_MODE_ARM, &m_uc);
    if (m_err != UC_ERR_OK)
      return false;
  }

  __check(exec->Load(), ERROR_OK, false);

  __check(sMemoryManager->StaticAlloc(LCD_REGISTER, LCD_REGISTER_SIZE),
          ERROR_OK, false);
  __check(sMemoryManager->StaticAlloc(0x51000000, 0x44), ERROR_OK, false);

  __check(InitMemory(), true, false);

  return true;
}

bool Executor::InitMemory() {
  sMemoryManager->StaticAlloc(RTC_REGISTER, 0x100);
  sMemoryManager->StaticAlloc(0, 0x10, nullptr, 3);
  sMemoryManager->StaticAlloc(0x31FFF000, 0x1000, nullptr, 3);
  return true;
}

void Executor::RegisterHooksForEngine(uc_engine *uc) {
  SvcDispatcher::RegisterHook(uc);
  FaultHandler::RegisterHook(uc);
  sTimerController->RegisterHook(uc);
  // GDB hooks are registered per-Thread in Thread::ThreadProc
  // via sGdbStub->RegisterThread(this)
}

void Executor::Execute() {
  // ── Start GDB stub (if enabled via compile-time or runtime config) ──
  // Set the environment variable PRIMU_GDB_PORT to a port number to enable,
  // or define ENABLE_GDB_STUB at compile time.  Default port: 2345.
  {
    uint16_t gdbPort = GDB_DEFAULT_PORT;
    const char *envPort = std::getenv("PRIMU_GDB_PORT");
    bool enableGdb = false;
#ifdef ENABLE_GDB_STUB
    enableGdb = true;
#endif
    if (envPort && envPort[0]) {
      gdbPort = (uint16_t)std::atoi(envPort);
      enableGdb = true;
    }
    if (enableGdb) {
      sGdbStub->Start(gdbPort);
    }
  }

  // Start the InterruptController's ISR dispatch thread
  sInterruptController->Start();


  // Create the main emulation thread
  sThreadHandler->NewThread(m_exec->get_entry(), 0, THREAD_PRIORITY_NORMAL,
                            MEM_STACK_SIZE);

  printf("Main emulator executor waiting...\n");

  // Start the TCP Command Server on port 4321
  CommandServer::Start(4321);

  // Keep the main process alive
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

bool Executor::Cleanup() {
  CommandServer::Stop();

  sTimerController->Stop();
  sInterruptController->Stop();
  __check(sMemoryManager->StaticFree(LCD_REGISTER), ERROR_OK, false);
  callAndcheckError(uc_close(m_uc));
}
