#pragma once

#include "stdafx.h"
#include "interrupts.h"
#include "executor.h"

struct SystemServiceArguments
{
	SystemServiceArguments(uint32_t caller_pc) :caller_pc(caller_pc)
	{
		uc_arm_reg _regs[5] =
		{
			UC_ARM_REG_R0,
			UC_ARM_REG_R1,
			UC_ARM_REG_R2,
			UC_ARM_REG_R3,
			// UC_ARM_REG_R4,
			UC_ARM_REG_SP
		};

		uint32_t* _args[5] =
		{
			&r0,
			&r1,
			&r2,
			&r3,
			// &r4,
			&sp
		};
		uc_reg_read_batch(sExecutor->GetUcInstance(), (int*)_regs, (void**)_args, 5);
	}
	uint32_t r0{};
	uint32_t r1{};
	uint32_t r2{};
	uint32_t r3{};
	// uint32_t r4;
	uint32_t sp{};
	uint32_t caller_pc;
};
