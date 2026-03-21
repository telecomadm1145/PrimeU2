#include "InterruptController.h"
#include "Thread.h"
#include "executor.h"
#include <cstdio>

InterruptController *InterruptController::_instance = nullptr;

// ── Lifecycle ──

void InterruptController::Start() {
  if (_running)
    return;

  // Create a Thread that won't auto-start (entry = 0).
  // It only provides a uc_engine with shared memory mappings
  // so we can ExecuteCustomCode() for ISR dispatch.
  _isrThread = new Thread(0, 0, THREAD_PRIORITY_TIME_CRITICAL, 0x4000);
  // Do NOT call _isrThread->Start() — we drive it manually.

  _running = true;
  _dispatchThread = std::thread(&InterruptController::IsrDispatchLoop, this);
  printf("[INTC] ISR dispatch thread started.\n");
}

void InterruptController::Stop() {
  {
    std::lock_guard<std::mutex> lk(_queueMutex);
    _running = false;
  }
  _cv.notify_one();

  if (_dispatchThread.joinable())
    _dispatchThread.join();

  delete _isrThread;
  _isrThread = nullptr;
  printf("[INTC] ISR dispatch thread stopped.\n");
}

// ── SVC layer ──

bool InterruptController::RegisterISR(uint32_t irq, VirtPtr isrPtr) {
  std::lock_guard<std::mutex> lk(_isrMutex);
  _isrMap[irq] = isrPtr;
  printf("[INTC] Registered ISR for IRQ %u -> 0x%08X\n", irq, isrPtr);
  return true;
}

bool InterruptController::UnregisterISR(uint32_t irq) {
  std::lock_guard<std::mutex> lk(_isrMutex);
  auto it = _isrMap.find(irq);
  if (it != _isrMap.end()) {
    _isrMap.erase(it);
    printf("[INTC] Unregistered ISR for IRQ %u\n", irq);
    return true;
  }
  return false;
}

void InterruptController::AcknowledgeIRQ(uint32_t irq) {
  // In real hardware, this would clear pending bits in INTPND/SRCPND.
  // In our emulator, the guest ISR already writes to the MMIO registers
  // directly (0x4A000000, 0x4A000010). This is mostly a no-op but we
  // log it for debugging.
  // printf("[INTC] ACK IRQ %u\n", irq);
}

// ── Host-side interface ──

void InterruptController::RaiseIRQ(uint32_t irq) {
  {
    std::lock_guard<std::mutex> lk(_queueMutex);
    _pendingQueue.push(irq);
  }
  _cv.notify_one();
}

// ── Query ──

VirtPtr InterruptController::GetISR(uint32_t irq) const {
  std::lock_guard<std::mutex> lk(_isrMutex);
  auto it = _isrMap.find(irq);
  return (it != _isrMap.end()) ? it->second : 0;
}

bool InterruptController::HasRegisteredISR() const {
  std::lock_guard<std::mutex> lk(_isrMutex);
  return !_isrMap.empty();
}

// ── Dispatch loop ──

void InterruptController::IsrDispatchLoop() {
  printf("[INTC] ISR dispatch loop running.\n");

  while (true) {
    uint32_t irq;
    {
      std::unique_lock<std::mutex> lk(_queueMutex);
      _cv.wait(lk, [this] { return !_pendingQueue.empty() || !_running; });

      if (!_running && _pendingQueue.empty())
        break;

      irq = _pendingQueue.front();
      _pendingQueue.pop();
    }

    // Look up the ISR
    VirtPtr isrPtr;
    {
      std::lock_guard<std::mutex> lk(_isrMutex);
      auto it = _isrMap.find(irq);
      if (it == _isrMap.end()) {
        printf("[INTC] IRQ %u raised but no ISR registered, dropped.\n", irq);
        continue;
      }
      isrPtr = it->second;
    }

    // printf("[INTC] Dispatching IRQ %u -> ISR 0x%08X\n", irq, isrPtr);

    // Execute the guest ISR on the dedicated virtual thread's uc_engine.
    // The ISR receives the IRQ number in R0 (per ARM convention).
    _isrThread->ExecuteCustomCode(isrPtr, irq);

    // printf("[INTC] ISR 0x%08X for IRQ %u returned.\n", isrPtr, irq);
  }

  printf("[INTC] ISR dispatch loop exiting.\n");
}
