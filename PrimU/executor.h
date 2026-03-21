#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "MemoryManager.h"
#include "executable.h"
#include "memory.h"
#include "stdafx.h"
#include <unicorn/unicorn.h>

/// The top-level emulator bootstrap.
/// Initializes memory, loads the executable, creates the main thread,
/// and starts the InterruptController. Hook registration is delegated
/// to SvcDispatcher and FaultHandler.
class Executor {
public:
  static Executor *get_instance() {
    return (!m_instance) ? m_instance = new Executor : m_instance;
  }
  ~Executor() { delete m_instance; }

  bool Initialize(Executable *exec);
  void Execute();
  bool Cleanup();

  /// Register SVC + fault hooks on a uc_engine.
  /// Called by Thread::ThreadProc and Thread::ExecuteCustomCode.
  void RegisterHooksForEngine(uc_engine *uc);

  uc_err GetLastError() { return m_err; }
  uc_engine *GetUcInstance() { return m_uc; }

private:
  Executor() : m_uc(nullptr) {}
  bool InitMemory();

  static Executor *m_instance;
  uc_engine *m_uc;
  uc_err m_err;

  Executable *m_exec;
  Memory *m_stack;
  uint32_t m_sp;

  Memory *m_dynamic;
  Memory *m_LCD;

public:
  Executor(Executor const &) = delete;
  void operator=(Executor const &) = delete;
};

#define sExecutor Executor::get_instance()

#endif
