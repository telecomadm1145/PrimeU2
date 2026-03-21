#include "executor.h"

#include "FaultHandler.h"
#include "HidPassthroughUI.h"
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
}

void Executor::Execute() {
  // Start the InterruptController's ISR dispatch thread
  sInterruptController->Start();

  // Launch the USB HID passthrough selection window
  HidPassthroughUI::Launch();

  // Create the main emulation thread
  sThreadHandler->NewThread(m_exec->get_entry(), 0, THREAD_PRIORITY_NORMAL,
                            MEM_STACK_SIZE);

  printf("Main emulator executor waiting...\n");

  // Keep the main process alive
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

bool Executor::Cleanup() {
  HidPassthroughUI::Shutdown();
  sTimerController->Stop();
  sInterruptController->Stop();
  __check(sMemoryManager->StaticFree(LCD_REGISTER), ERROR_OK, false);
  callAndcheckError(uc_close(m_uc));
}
