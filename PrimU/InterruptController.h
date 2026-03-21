#pragma once

#include "common.h"
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

class Thread;

/// Emulates an ARM interrupt controller (modeled after S3C2440 INTC).
/// Owns a dedicated virtual thread with its own uc_engine to execute
/// guest ISR code. Any host thread may call RaiseIRQ() to enqueue an
/// interrupt; the ISR dispatch thread will dequeue and execute it.
class InterruptController {
public:
  static InterruptController *GetInstance() {
    if (!_instance)
      _instance = new InterruptController;
    return _instance;
  }

  // ── Lifecycle ──

  /// Create the ISR virtual thread and start the dispatch loop.
  /// Call after MemoryManager is fully initialized.
  void Start();

  /// Shut down the dispatch loop and destroy the ISR thread.
  void Stop();

  // ── SVC layer (called from SVC handlers) ──

  /// Register a guest ISR for a given IRQ number.
  /// Signature mirrors: InterruptInitialize(irq, unk, isrPtr)
  bool RegisterISR(uint32_t irq, VirtPtr isrPtr);

  /// Unregister (disable) an IRQ's ISR.
  bool UnregisterISR(uint32_t irq);

  /// Acknowledge an IRQ (called from InterruptDone SVC).
  void AcknowledgeIRQ(uint32_t irq);

  // ── Host-side interface (thread-safe, callable from ANY host thread) ──

  /// Raise a virtual hardware interrupt. The ISR will execute
  /// asynchronously on the dedicated ISR virtual thread.
  ///
  /// Example: When USB data arrives, call RaiseIRQ(25).
  void RaiseIRQ(uint32_t irq);

  // ── Query ──

  /// Get the registered ISR for a given IRQ, or 0 if none.
  VirtPtr GetISR(uint32_t irq) const;

  /// Check if any ISR is registered.
  bool HasRegisteredISR() const;

private:
  InterruptController() = default;
  static InterruptController *_instance;

  /// The main ISR dispatch loop (runs on _dispatchThread).
  void IsrDispatchLoop();

  // ISR registration table
  mutable std::mutex _isrMutex;
  std::unordered_map<uint32_t, VirtPtr> _isrMap;

  // Pending IRQ queue (producers: any host thread, consumer: dispatch loop)
  std::mutex _queueMutex;
  std::condition_variable _cv;
  std::queue<uint32_t> _pendingQueue;

  // The dedicated virtual thread that executes guest ISR code
  Thread *_isrThread = nullptr;
  std::thread _dispatchThread;
  bool _running = false;
};

#define sInterruptController InterruptController::GetInstance()
