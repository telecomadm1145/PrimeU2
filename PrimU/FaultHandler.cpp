#include "FaultHandler.h"

#include "ThreadHandler.h"
#include "executor.h"

#include <capstone/capstone.h>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>

// ── Helpers ──

static bool TryRead32(uc_engine* uc, uint32_t addr, uint32_t* out) {
	return uc_mem_read(uc, addr, out, sizeof(uint32_t)) == UC_ERR_OK;
}

static void PrintOneFrame(uc_engine* uc, csh cs, uint32_t addr, int idx) {
	if (addr == 0) {
		printf("#%02d 0x%08X\n", idx, addr);
		return;
	}

	uint8_t bytes[8] = { 0 };
	if (uc_mem_read(uc, addr, bytes, sizeof(bytes)) == UC_ERR_OK) {
		cs_insn* insn = nullptr;
		size_t cnt = cs_disasm(cs, bytes, sizeof(bytes), addr, 1, &insn);
		if (cnt > 0 && insn) {
			printf("#%02d 0x%08X  %s %s\n", idx, addr, insn[0].mnemonic,
				insn[0].op_str);
			cs_free(insn, cnt);
			return;
		}
	}
	printf("#%02d 0x%08X\n", idx, addr);
}

void PrintStackTrace(uc_engine* uc) {
	uint32_t sp = 0, lr = 0, pc = 0, fp = 0, cpsr = 0;
	uc_reg_read(uc, UC_ARM_REG_SP, &sp);
	uc_reg_read(uc, UC_ARM_REG_LR, &lr);
	uc_reg_read(uc, UC_ARM_REG_PC, &pc);
	uc_reg_read(uc, UC_ARM_REG_R11, &fp);
	uc_reg_read(uc, UC_ARM_REG_CPSR, &cpsr);

	auto sanitize = [](uint32_t addr) { return addr & ~1u; };
	auto is_thumb_addr = [](uint32_t addr) { return (addr & 1u) != 0; };

	bool pc_thumb = is_thumb_addr(pc) || ((cpsr & (1u << 5)) != 0);
	uint32_t pc_sanitized = sanitize(pc);

	printf("\n--- Stack trace ---\n");

	csh handle = 0;
	cs_mode mode = pc_thumb ? CS_MODE_THUMB : CS_MODE_ARM;
	if (cs_open(CS_ARCH_ARM, mode, &handle) == CS_ERR_OK) {
		cs_option(handle, CS_OPT_DETAIL, CS_OPT_OFF);

		int depth = 0;
		PrintOneFrame(uc, handle, pc_sanitized, depth++);

		if (lr) {
			bool lr_thumb = is_thumb_addr(lr);
			uint32_t lr_sanitized = sanitize(lr);
			uint32_t call_site = lr_sanitized - (lr_thumb ? 2u : 4u);
			PrintOneFrame(uc, handle, call_site, depth++);
		}

		const int MAX_FRAMES = 64;
		uint32_t cur_fp = fp;
		uint32_t last_fp = 0;

		for (int i = 0; i < MAX_FRAMES; ++i) {
			if (cur_fp == 0 || cur_fp == last_fp)
				break;

			uint32_t prev_fp = 0, saved_lr = 0;
			if (!TryRead32(uc, cur_fp - 4, &prev_fp))
				break;
			if (!TryRead32(uc, cur_fp, &saved_lr))
				break;

			if (saved_lr != 0) {
				bool ret_thumb = is_thumb_addr(saved_lr);
				uint32_t ret_sanitized = sanitize(saved_lr);
				uint32_t call_site = ret_sanitized - (ret_thumb ? 2u : 4u);
				PrintOneFrame(uc, handle, call_site, depth++);
			}

			last_fp = cur_fp;
			cur_fp = prev_fp;
		}

		cs_close(&handle);
	}
	else {
		int depth = 0;
		printf("#%02d 0x%08X\n", depth++, pc_sanitized);
		if (lr) {
			bool lr_thumb = is_thumb_addr(lr);
			uint32_t lr_sanitized = sanitize(lr);
			uint32_t call_site = lr_sanitized - (lr_thumb ? 2u : 4u);
			printf("#%02d 0x%08X\n", depth++, call_site);
		}

		const int MAX_FRAMES = 64;
		uint32_t cur_fp = fp;
		uint32_t last_fp = 0;
		for (int i = 0; i < MAX_FRAMES; ++i) {
			if (cur_fp == 0 || cur_fp == last_fp)
				break;

			uint32_t prev_fp = 0, saved_lr = 0;
			if (!TryRead32(uc, cur_fp - 4, &prev_fp))
				break;
			if (!TryRead32(uc, cur_fp, &saved_lr))
				break;

			if (saved_lr) {
				bool ret_thumb = is_thumb_addr(saved_lr);
				uint32_t ret_sanitized = sanitize(saved_lr);
				uint32_t call_site = ret_sanitized - (ret_thumb ? 2u : 4u);
				printf("#%02d 0x%08X\n", depth++, call_site);
			}

			last_fp = cur_fp;
			cur_fp = prev_fp;
		}
	}

	printf("\n--- Stack dump (SP=0x%08X) ---\n", sp);
	const int WORDS = 32;
	uint32_t buf[WORDS] = { 0 };
	if (uc_mem_read(uc, sp, buf, sizeof(buf)) == UC_ERR_OK) {
		for (int i = 0; i < WORDS; i += 4) {
			printf("  0x%08X: %08X %08X %08X %08X\n", sp + i * 4, buf[i], buf[i + 1],
				buf[i + 2], buf[i + 3]);
		}
	}
}

// ── Page fault handler ──

static void pf(uc_engine* uc, uc_mem_type type, uint64_t address, int size,
	int64_t value, void* user_data) {
	if (address == 0xdeadc0de)
		return;
	uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, sp, pc, lr;
	void* args[16] = { &r0, &r1, &r2,  &r3,  &r4,  &r5, &r6, &r7,
					  &r8, &r9, &r10, &r11, &r12, &sp, &lr, &pc };
	int regs[16] = { UC_ARM_REG_R0,  UC_ARM_REG_R1, UC_ARM_REG_R2,  UC_ARM_REG_R3,
					UC_ARM_REG_R4,  UC_ARM_REG_R5, UC_ARM_REG_R6,  UC_ARM_REG_R7,
					UC_ARM_REG_R8,  UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
					UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR,  UC_ARM_REG_PC };
	uc_reg_read_batch(uc, regs, args, 16);

	if (type == UC_MEM_FETCH_UNMAPPED || type == UC_MEM_FETCH_PROT) {
		if (pc < 0x2000) {
			uc_reg_write(uc, UC_ARM_REG_PC, &lr);
			return;
		}
	}
	const char* errorType = (type == UC_MEM_READ_UNMAPPED) ? "read"
		: (type == UC_MEM_WRITE_UNMAPPED) ? "written"
		: (type == UC_MEM_FETCH_UNMAPPED) ? "executed"
		: (type == UC_MEM_READ_PROT) ? "read"
		: (type == UC_MEM_WRITE_PROT) ? "written"
		: (type == UC_MEM_FETCH_PROT) ? "executed"
		: "???";
	printf("Memory at %p cannot be %s. \nThread: %i\nRegisters: \n",
		(void*)address, errorType, sThreadHandler->GetCurrentThreadId());
	printf("    r0: %08X|%i\n    r1: %08X|%i\n    r2: %08X|%i\n    r3: %08X|%i\n "
		"   r4: %08X|%i\n"
		"    r5: %08X|%i\n    r6: %08X|%i\n    r7: %08X|%i\n    r8: %08X|%i\n "
		"   r9: %08X|%i\n"
		"   r10: %08X|%i\n   r11: %08X|%i\n   r12: %08X|%i\n"
		"    sp: %08X\n    pc: %08X\n    lr: %08X\n",
		r0, r0, r1, r1, r2, r2, r3, r3, r4, r4, r5, r5, +r6, r6, r7, r7, r8,
		r8, r9, r9, r10, r10, r11, r11, r12, r12, sp, pc, lr);

	// Disassembly around PC
	printf("\n--- Disassembly around PC (0x%08X) ---\n", pc);

	csh handle = 0;
	cs_insn* insn = nullptr;
	size_t count = 0;

	uint32_t cpsr = 0;
	uc_reg_read(uc, UC_ARM_REG_CPSR, &cpsr);

	bool is_thumb = ((pc & 1) != 0) || ((cpsr & (1u << 5)) != 0);
	uint64_t pc_addr = (uint64_t)(pc & ~1u);

	cs_mode mode = is_thumb ? CS_MODE_THUMB : CS_MODE_ARM;

	if (cs_open(CS_ARCH_ARM, mode, &handle) == CS_ERR_OK) {
		const size_t CONTEXT_BYTES_BEFORE = 32;
		const size_t CONTEXT_BYTES_AFTER = 32;
		const size_t DISASM_SIZE = CONTEXT_BYTES_BEFORE + CONTEXT_BYTES_AFTER;

		uint64_t start_addr =
			(pc_addr > CONTEXT_BYTES_BEFORE) ? (pc_addr - CONTEXT_BYTES_BEFORE) : 0;

		uint8_t* code_buffer = (uint8_t*)malloc(DISASM_SIZE);
		if (!code_buffer) {
			printf("    Failed to allocate code buffer for disassembly.\n");
		}
		else {
			if (uc_mem_read(uc, start_addr, code_buffer, DISASM_SIZE) == UC_ERR_OK) {
				cs_option(handle, CS_OPT_DETAIL, CS_OPT_OFF);
				count =
					cs_disasm(handle, code_buffer, DISASM_SIZE, start_addr, 0, &insn);
				if (count > 0) {
					for (size_t i = 0; i < count; i++) {
						const char* marker = (insn[i].address == pc_addr) ? " -> " : "    ";
						printf("%s0x%08" PRIx64 ":\t", marker, insn[i].address);
						for (size_t b = 0; b < insn[i].size; b++)
							printf("%02x ", insn[i].bytes[b]);
						printf("\t%s\t%s\n", insn[i].mnemonic, insn[i].op_str);
					}
					cs_free(insn, count);
				}
				else {
					printf("    Failed to disassemble code at 0x%08" PRIx64
						" (count==0)\n",
						start_addr);
				}
			}
			else {
				printf("    Failed to read memory at 0x%08" PRIx64
					" for disassembly.\n",
					start_addr);
			}
			free(code_buffer);
		}
		cs_close(&handle);
	}
	else {
		printf("    Failed to initialize Capstone disassembler.\n");
	}
	PrintStackTrace(uc);
}

void FaultHandler::RegisterHook(uc_engine* uc) {
	uc_hook h;
	uc_hook_add(uc, &h, UC_HOOK_MEM_INVALID, (void*)pf, nullptr, 1, 0);
}
