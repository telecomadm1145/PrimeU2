#include "TimerController.h"
#include "InterruptController.h"
#include "MemoryManager.h"
#include <chrono>
#include <cstdio>


TimerController* TimerController::_instance = nullptr;

// ── MMIO Hook ──

static void timer_mmio_write(uc_engine* uc, uc_mem_type type, uint64_t address,
	int size, int64_t value, void* user_data) {

	uint32_t offset = (uint32_t)(address - TimerController::BASE);

	// We only care about writes to TCON (the control register)
	if (offset == TimerController::TCON) {
		sTimerController->OnTconWrite((uint32_t)value);
	}
}

void TimerController::RegisterHook(uc_engine* uc) {
	uc_hook h;
	uc_hook_add(uc, &h, UC_HOOK_MEM_WRITE, (void*)timer_mmio_write, nullptr,
		BASE, BASE + 0x44);
}

// ── Register access helpers ──

uint32_t TimerController::ReadReg(uint32_t offset) {
	uint32_t addr = BASE + offset;
	auto* real = sMemoryManager->GetRealAddr(addr);
	if (!real)
		return 0;
	return *reinterpret_cast<uint32_t*>(real);
}

void TimerController::WriteReg(uint32_t offset, uint32_t value) {
	uint32_t addr = BASE + offset;
	auto* real = sMemoryManager->GetRealAddr(addr);
	if (!real)
		return;
	*reinterpret_cast<uint32_t*>(real) = value;
}

// ── Period calculation ──

uint64_t TimerController::CalcPeriodUs(int idx) {
	uint32_t tcfg0 = ReadReg(TCFG0);
	uint32_t tcfg1 = ReadReg(TCFG1);
	uint32_t tcntb = ReadReg(_tcntbOffsets[idx]) & 0xFFFF;

	if (tcntb == 0)
		tcntb = 1; // avoid zero period

	// Prescaler: bits [7:0] for Timer0-1, bits [15:8] for Timer2-4
	uint32_t prescaler;
	if (idx <= 1)
		prescaler = (tcfg0 & 0xFF) + 1;
	else
		prescaler = ((tcfg0 >> 8) & 0xFF) + 1;

	// MUX divider from TCFG1: 4 bits per timer
	uint32_t mux = (tcfg1 >> (idx * 4)) & 0xF;
	uint32_t divider;
	switch (mux) {
	case 0:
		divider = 2;
		break;
	case 1:
		divider = 4;
		break;
	case 2:
		divider = 8;
		break;
	case 3:
		divider = 16;
		break;
	default:
		divider = 2;
		break; // external TCLK not emulated, fallback
	}

	// Timer frequency = PCLK / prescaler / divider
	// Period = tcntb / timer_freq = tcntb * prescaler * divider / PCLK (seconds)
	// In microseconds:
	uint64_t period_us =
		(uint64_t)tcntb * prescaler * divider * 1000000ULL / PCLK;
	//printf("[TIMER] Timer%d period calculation: tcntb=%u, prescaler=%u, divider=%u, "
	//       "period=%llu us\n",
	   // idx, tcntb, prescaler, divider, period_us);
	// Clamp minimum to 1ms to avoid busy-spinning
	if (period_us < 1000)
		period_us = 1000;

	return period_us;
}

// ── TCON write handler ──

void TimerController::OnTconWrite(uint32_t newTcon) {
	std::lock_guard<std::mutex> lk(_mutex);

	for (int i = 0; i < 5; i++) {
		bool wasRunning = (_lastTcon >> _bits[i].startBit) & 1;
		bool nowRunning = (newTcon >> _bits[i].startBit) & 1;
		bool autoReload = (newTcon >> _bits[i].autoReloadBit) & 1;

		_timers[i].autoReload.store(autoReload);

		if (!wasRunning && nowRunning) {
			// Timer just started
			if (_timers[i].running.load()) {
				// Already running from a previous start, stop it first
				_timers[i].running.store(false);
				if (_timers[i].thread.joinable())
					_timers[i].thread.detach();
			}

			_timers[i].irq = _irqs[i];
			_timers[i].running.store(true);

			uint64_t period = CalcPeriodUs(i);
			printf("[TIMER] Timer%d started: period=%llu us, auto-reload=%d\n", i,
				period, autoReload ? 1 : 0);

			_timers[i].thread =
				std::thread(&TimerController::TimerThreadFunc, this, i);
			_timers[i].thread.detach();
		}
		else if (wasRunning && !nowRunning) {
			// Timer just stopped
			printf("[TIMER] Timer%d stopped\n", i);
			_timers[i].running.store(false);
			// Don't join here to avoid deadlock in MMIO hook context;
			// thread will exit on its own when it checks running flag
		}
	}

	_lastTcon = newTcon;
}

// ── Timer thread ──

void TimerController::TimerThreadFunc(int idx) {
	printf("[TIMER] Timer%d thread started (IRQ %u)\n", idx, _irqs[idx]);

	while (_timers[idx].running.load()) {
		uint64_t period_us = CalcPeriodUs(idx);

		// Sleep for the timer period
		std::this_thread::sleep_for(std::chrono::microseconds(period_us));

		if (!_timers[idx].running.load())
			break;

		// Update TCNTO (observation register) to 0 to indicate countdown complete
		// TCNTO offsets: Timer0=0x14, Timer1=0x20, Timer2=0x2C, Timer3=0x38,
		// Timer4=0x40
		static constexpr uint32_t tcntoOffsets[5] = { 0x14, 0x20, 0x2C, 0x38, 0x40 };
		WriteReg(tcntoOffsets[idx], 0);

		// Fire the interrupt
		sInterruptController->RaiseIRQ(_irqs[idx]);

		// If not auto-reload, stop after one shot
		if (!_timers[idx].autoReload.load()) {
			printf("[TIMER] Timer%d one-shot complete\n", idx);
			_timers[idx].running.store(false);
			break;
		}
	}

	printf("[TIMER] Timer%d thread exiting\n", idx);
}

// ── Shutdown ──

void TimerController::Stop() {
	for (int i = 0; i < 5; i++) {
		_timers[i].running.store(false);
	}
	for (int i = 0; i < 5; i++) {
		if (_timers[i].thread.joinable())
			_timers[i].thread.join();
	}
	printf("[TIMER] All timers stopped\n");
}
