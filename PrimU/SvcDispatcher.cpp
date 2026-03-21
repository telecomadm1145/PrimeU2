#include "SvcDispatcher.h"

#include "InterruptHandler.h"
#include "Services.h"
#include "Thread.h"
#include "ThreadHandler.h"
#include "interrupts.h"
#include "log.h"
#include "svclogwin.h"

#include <cstdio>

RollingLogBuffer<SVCCallRecord> g_svcCallLog(1024);

static void svc_hook(uc_engine* uc, uint64_t address, uint32_t size,
	void* user_data) {

	uint32_t r0, r1, r2, r3, sp, pc, lr;
	void* args[6] = { &r0, &r1, &r2, &r3, &sp, &pc };
	int regs[6] = { UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
				   UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_PC };

	uc_reg_read_batch(uc, regs, args, sizeof(args) / sizeof(void*));

	uint32_t SVC;
	uc_mem_read(uc, pc - 4, &SVC, 4);
	uc_mem_read(uc, sp, &lr, 4);

	SVC &= 0xFFFFF;
	SystemServiceArguments args2(uc, lr - 4);
	auto cur_thread = sThreadHandler->GetCurrentThread();
	int currentThreadId = cur_thread ? cur_thread->GetId() : -1;
	g_svcCallLog.push(SVCCallRecord{ SVC, currentThreadId, args2,
									std::chrono::steady_clock::now() });
	if (SVC < SDKLIB_FirstService || SVC > SDKLIB_LastService) {
		printf("Unknown SVC: %x at PC: %08X\n", SVC, pc - 4);
		sp += 8;
		uint32_t empty = 0;
		uc_reg_write(uc, UC_ARM_REG_R0, &empty);
		uc_reg_write(uc, UC_ARM_REG_SP, &sp);
		uc_reg_write(uc, UC_ARM_REG_PC, &lr);
		return;
	}
	auto f = service_table.at(SVC - SDKLIB_FirstService);
	uint32_t return_value = f ? f(&args2) : 0;
	if (!f) {
		printf("Unimplemented SVC: %x(%s) at PC: %08X\n", SVC, SVCNameRegistry::Instance().Lookup(SVC), lr - 4);
	}
	sp += 8;

	uc_reg_write(uc, UC_ARM_REG_R0, &return_value);
	uc_reg_write(uc, UC_ARM_REG_SP, &sp);
	uc_reg_write(uc, UC_ARM_REG_PC, &lr);
}

void SvcDispatcher::RegisterHook(uc_engine* uc) {
	uc_hook h;
	uc_hook_add(uc, &h, UC_HOOK_INTR, (void*)svc_hook, nullptr, 0, 1);
}
