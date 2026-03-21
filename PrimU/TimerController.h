#pragma once

#include "common.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

/// Emulates the S3C2416X PWM Timer block (5 timers, base 0x51000000).
/// Monitors guest writes to TCON, calculates timer periods from
/// prescaler/divider/count, and fires IRQs via InterruptController.
///
/// Register map (base 0x51000000):
///   +0x00 TCFG0   prescaler config
///   +0x04 TCFG1   MUX divider / DMA select
///   +0x08 TCON    control (start/stop/auto-reload/manual-update)
///   +0x0C TCNTB0  +0x10 TCMPB0  +0x14 TCNTO0
///   +0x18 TCNTB1  +0x1C TCMPB1  +0x20 TCNTO1
///   +0x24 TCNTB2  +0x28 TCMPB2  +0x2C TCNTO2
///   +0x30 TCNTB3  +0x34 TCMPB3  +0x38 TCNTO3
///   +0x3C TCNTB4                +0x40 TCNTO4
class TimerController {
public:
  static TimerController *GetInstance() {
    if (!_instance)
      _instance = new TimerController;
    return _instance;
  }

  /// Register MMIO write hook on a uc_engine.
  /// Called from Executor::RegisterHooksForEngine.
  void RegisterHook(uc_engine *uc);

  /// Shut down all timer threads.
  void Stop();

  // Timer base address
  static constexpr uint32_t BASE = 0x51000000;

  // Register offsets
  static constexpr uint32_t TCFG0 = 0x00;
  static constexpr uint32_t TCFG1 = 0x04;
  static constexpr uint32_t TCON = 0x08;

  // Per-timer offsets (timer 0-3 each have TCNTB, TCMPB, TCNTO; timer 4 has no
  // TCMPB)
  static constexpr uint32_t TCNTB0 = 0x0C;
  static constexpr uint32_t TCNTB1 = 0x18;
  static constexpr uint32_t TCNTB2 = 0x24;
  static constexpr uint32_t TCNTB3 = 0x30;
  static constexpr uint32_t TCNTB4 = 0x3C;
  static constexpr uint32_t TCNTO4 = 0x40;

  // TCON bit positions per timer
  struct TimerBits {
    int startBit;
    int manualUpdateBit;
    int autoReloadBit;
  };

  // IRQ numbers for each timer (S3C2416 interrupt table)
  static constexpr uint32_t IRQ_TIMER0 = 10;
  static constexpr uint32_t IRQ_TIMER1 = 11;
  static constexpr uint32_t IRQ_TIMER2 = 12;
  static constexpr uint32_t IRQ_TIMER3 = 13;
  static constexpr uint32_t IRQ_TIMER4 = 14;

  // Assumed PCLK frequency (Hz)
  static constexpr uint32_t PCLK = 38'000; // TODO: ???
  void OnTconWrite(uint32_t newTcon);
private:
  TimerController() = default;
  static TimerController *_instance;

  /// Called by MMIO write hook when TCON is written.


  /// Timer thread function for timer N.
  void TimerThreadFunc(int timerIndex);

  /// Calculate timer period in microseconds for a given timer index.
  uint64_t CalcPeriodUs(int timerIndex);

  /// Read a 32-bit value from the timer register region.
  uint32_t ReadReg(uint32_t offset);

  /// Write a 32-bit value to the timer register region.
  void WriteReg(uint32_t offset, uint32_t value);

  // Timer state
  struct TimerState {
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<bool> autoReload{false};
    uint32_t irq = 0;
  };

  TimerState _timers[5];
  uint32_t _lastTcon = 0;
  std::mutex _mutex;

  static constexpr TimerBits _bits[5] = {
      {0, 1, 3},    // Timer0
      {8, 9, 11},   // Timer1
      {12, 13, 15}, // Timer2
      {16, 17, 19}, // Timer3
      {20, 21, 22}, // Timer4
  };

  static constexpr uint32_t _tcntbOffsets[5] = {TCNTB0, TCNTB1, TCNTB2, TCNTB3,
                                                TCNTB4};

  static constexpr uint32_t _irqs[5] = {IRQ_TIMER0, IRQ_TIMER1, IRQ_TIMER2,
                                        IRQ_TIMER3, IRQ_TIMER4};
};

#define sTimerController TimerController::GetInstance()
