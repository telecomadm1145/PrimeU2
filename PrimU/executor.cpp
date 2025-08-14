#include "executor.h"


#include "memory.h"
#include "executable.h"
#include "interrupts.h"
#include "InterruptHandler.h"
#include "SystemAPI.h"
#include "Thread.h"
#include "ThreadHandler.h"

#include <valarray>
#include <chrono>
#include <capstone/capstone.h>

Executor* Executor::m_instance = nullptr;

#define STACK_MIN 0x20000000

#define callAndcheckError(f) m_err = f; if (m_err != UC_ERR_OK) return false
#define DEFINE_INTERRUPT(id, s, n, c) m_interrupts.insert(std::pair<InterruptID, InterruptHandle*>(id, new InterruptHandle(id, s, c, n)))

static bool TryRead32(uc_engine * uc, uint32_t addr, uint32_t * out) {
	return uc_mem_read(uc, addr, out, sizeof(uint32_t)) == UC_ERR_OK;
}

static void PrintOneFrame(uc_engine * uc, csh cs, uint32_t addr, int idx) {
	if (addr == 0) {
		printf("#%02d 0x%08X\n", idx, addr);
		return;
	}

	// 尝试反汇编一条指令作为注释（失败就只打地址）
	uint8_t bytes[8] = { 0 };
	if (uc_mem_read(uc, addr, bytes, sizeof(bytes)) == UC_ERR_OK) {
		cs_insn* insn = nullptr;
		size_t cnt = cs_disasm(cs, bytes, sizeof(bytes), addr, 1, &insn);
		if (cnt > 0 && insn) {
			printf("#%02d 0x%08X  %s %s\n", idx, addr, insn[0].mnemonic, insn[0].op_str);
			cs_free(insn, cnt);
			return;
		}
	}
	printf("#%02d 0x%08X\n", idx, addr);
}

static void PrintStackTrace(uc_engine * uc) {
	uint32_t sp = 0, lr = 0, pc = 0, fp = 0, cpsr = 0;
	uc_reg_read(uc, UC_ARM_REG_SP, &sp);
	uc_reg_read(uc, UC_ARM_REG_LR, &lr);
	uc_reg_read(uc, UC_ARM_REG_PC, &pc);
	// R11 常作为帧指针（AAPCS），若你的编译器/ABI不保留帧指针，则只能靠启发式
	uc_reg_read(uc, UC_ARM_REG_R11, &fp);
	uc_reg_read(uc, UC_ARM_REG_CPSR, &cpsr); // 失败也没关系

	auto sanitize = [](uint32_t addr) { return addr & ~1u; };
	auto is_thumb_addr = [](uint32_t addr) { return (addr & 1u) != 0; };

	// 当前模式（用于 PC），后续每个返回地址单独判断 Thumb 位
	bool pc_thumb = is_thumb_addr(pc) || ((cpsr & (1u << 5)) != 0);
	uint32_t pc_sanitized = sanitize(pc);

	printf("\n--- Stack trace ---\n");

	csh handle = 0;
	cs_mode mode = pc_thumb ? CS_MODE_THUMB : CS_MODE_ARM;
	if (cs_open(CS_ARCH_ARM, mode, &handle) == CS_ERR_OK) {
		cs_option(handle, CS_OPT_DETAIL, CS_OPT_OFF);

		int depth = 0;

		// Frame 0: 当前 PC
		PrintOneFrame(uc, handle, pc_sanitized, depth++);

		// Frame 1: 来自 LR 的返回地址对应“调用点”
		if (lr) {
			// 每个地址单独判断是否 Thumb，再决定回退多少字节以指向 BL/BLX
			bool lr_thumb = is_thumb_addr(lr);
			uint32_t lr_sanitized = sanitize(lr);
			uint32_t call_site = lr_sanitized - (lr_thumb ? 2u : 4u);
			PrintOneFrame(uc, handle, call_site, depth++);
		}

		// 后续：用 R11 帧链展开（AAPCS 常见布局： [fp-4] = prev_fp, [fp] = saved_lr）
		const int MAX_FRAMES = 64;
		uint32_t cur_fp = fp;
		uint32_t last_fp = 0;

		for (int i = 0; i < MAX_FRAMES; ++i) {
			if (cur_fp == 0 || cur_fp == last_fp) break;

			uint32_t prev_fp = 0, saved_lr = 0;
			if (!TryRead32(uc, cur_fp - 4, &prev_fp)) break;
			if (!TryRead32(uc, cur_fp, &saved_lr)) break;

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
		// Capstone 初始化失败时，退化为仅打印地址
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
			if (cur_fp == 0 || cur_fp == last_fp) break;

			uint32_t prev_fp = 0, saved_lr = 0;
			if (!TryRead32(uc, cur_fp - 4, &prev_fp)) break;
			if (!TryRead32(uc, cur_fp, &saved_lr)) break;

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

	// 附加：栈 dump，便于肉眼搜返回地址
	printf("\n--- Stack dump (SP=0x%08X) ---\n", sp);
	const int WORDS = 32; // 打印 32 个 word
	uint32_t buf[WORDS] = { 0 };
	if (uc_mem_read(uc, sp, buf, sizeof(buf)) == UC_ERR_OK) {
		for (int i = 0; i < WORDS; i += 4) {
			printf("  0x%08X: %08X %08X %08X %08X\n",
				sp + i * 4, buf[i], buf[i + 1], buf[i + 2], buf[i + 3]);
		}
	}
	else {
		printf("  Failed to read stack memory.\n");
	}
}

void interrupt_hook(uc_engine* uc, uint64_t address, uint32_t size, void* user_data);
void code_hook(uc_engine* uc, uint64_t address, uint32_t size, void* user_data)
{
	static auto lastUpdate = std::chrono::high_resolution_clock::now();

	auto now = std::chrono::high_resolution_clock::now();
	std::chrono::milliseconds elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate);
	bool canrun = sThreadHandler->CanCurrentThreadRun();
	if (sThreadHandler->yielding) {
		uc_emu_stop(sExecutor->GetUcInstance());
		sThreadHandler->SaveCurrentThreadState();
		sThreadHandler->yielding = false;
		lastUpdate = std::chrono::high_resolution_clock::now();
		return;
	}
	if ((elapsed.count() < sThreadHandler->GetCurrentThreadQuantum() && canrun) ) {
		return;
	}
	uc_emu_stop(sExecutor->GetUcInstance());
	sThreadHandler->SaveCurrentThreadState();

	//printf(">>> Stopping at 0x%llX, instruction size = 0x%x\n", address, size);
	lastUpdate = std::chrono::high_resolution_clock::now();
}

bool Executor::Initialize(Executable* exec)
{
	if (!exec)
		return false;

	m_exec = exec;

	if (!m_uc)
	{
		m_err = uc_open(UC_ARCH_ARM, UC_MODE_ARM, &m_uc);
		if (m_err != UC_ERR_OK) return false;
	}

	__check(exec->Load(), ERROR_OK, false);

	__check(sMemoryManager->StaticAlloc(LCD_REGISTER, LCD_REGISTER_SIZE), ERROR_OK, false);
	__check(sMemoryManager->StaticAlloc(0x51000000, 0x44), ERROR_OK, false);

	__check(InitInterrupts(), true, false);

	return true;
}

uc_hook m_page_fault;
uc_hook m_page_fault2;
uc_hook m_page_fault3;
void pf(uc_engine* uc, uint64_t address, uint32_t size, void* user_data) {

	uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, sp, pc, lr;
	void* args[16] = { &r0, &r1, &r2, &r3, &r4, &r5, &r6, &r7, &r8, &r9, &r10, &r11, &r12, &sp, &lr, &pc };
	int regs[16] = { UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6,
		UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC };
	uc_reg_read_batch(sExecutor->GetUcInstance(), regs, args, 16);

	// __debugbreak();
	printf("Page fault triggered! \nThread: %i\nRegisters: \n", sThreadHandler->GetCurrentThreadId());
	printf("    r0: %08X|%i\n    r1: %08X|%i\n    r2: %08X|%i\n    r3: %08X|%i\n    r4: %08X|%i\n"
		"    r5: %08X|%i\n    r6: %08X|%i\n    r7: %08X|%i\n    r8: %08X|%i\n    r9: %08X|%i\n"
		"   r10: %08X|%i\n   r11: %08X|%i\n   r12: %08X|%i\n"
		"    sp: %08X\n    pc: %08X\n    lr: %08X\n",
		r0, r0, r1, r1, r2, r2, r3, r3, r4, r4, r5, r5, r6, r6, r7, r7, r8, r8,
		r9, r9, r10, r10, r11, r11, r12, r12, sp, pc, lr);

	// ==================== 新增的反汇编代码块 START ====================
	printf("\n--- Disassembly around PC (0x%08X) ---\n", pc);

	csh handle = 0;
	cs_insn* insn = nullptr;
	size_t count = 0;

	// 读取 CPSR（若支持）以检测 Thumb 模式
	uint32_t cpsr = 0;
	if (uc_reg_read(uc, UC_ARM_REG_CPSR, &cpsr) != UC_ERR_OK) {
		cpsr = 0; // 无法读取时保守处理
	}

	// 判断是否为 Thumb：PC 低位为 1 或 CPSR.T 位被置位
	bool is_thumb = ((pc & 1) != 0) || ((cpsr & (1u << 5)) != 0);

	// 用于从内存读取的对齐 PC（清除低位）
	uint64_t pc_addr = (uint64_t)(pc & ~1u);

	// Capstone 模式选择
	cs_mode mode = is_thumb ? CS_MODE_THUMB : CS_MODE_ARM;

	if (cs_open(CS_ARCH_ARM, mode, &handle) == CS_ERR_OK)
	{
		// 可配置的上下文字节数（前/后）
		const size_t CONTEXT_BYTES_BEFORE = 32;
		const size_t CONTEXT_BYTES_AFTER = 32;
		const size_t DISASM_SIZE = CONTEXT_BYTES_BEFORE + CONTEXT_BYTES_AFTER;

		uint64_t start_addr = (pc_addr > CONTEXT_BYTES_BEFORE) ? (pc_addr - CONTEXT_BYTES_BEFORE) : 0;

		// 分配缓冲区
		uint8_t* code_buffer = (uint8_t*)malloc(DISASM_SIZE);
		if (!code_buffer) {
			printf("    Failed to allocate code buffer for disassembly.\n");
		}
		else {
			// 从模拟器读取内存，注意若读取失败会返回错误（例如跨越未映射区域）
			if (uc_mem_read(uc, start_addr, code_buffer, DISASM_SIZE) == UC_ERR_OK)
			{
				// 可选：告诉 Capstone 显示详细信息
				cs_option(handle, CS_OPT_DETAIL, CS_OPT_OFF);

				// 反汇编（count 0 表示反汇编尽可能多的指令）
				count = cs_disasm(handle, code_buffer, DISASM_SIZE, start_addr, 0, &insn);
				if (count > 0)
				{
					for (size_t i = 0; i < count; i++)
					{
						// 如果是当前 PC 指向的指令，添加箭头标记
						const char* marker = (insn[i].address == pc_addr) ? " -> " : "    ";

						// 打印指令地址、机器码字节、助记符和操作数
						printf("%s0x%08" PRIx64 ":\t", marker, insn[i].address);
						for (size_t b = 0; b < insn[i].size; b++)
							printf("%02x ", insn[i].bytes[b]);
						// 对齐输出，使助记符不挤在字节后面（简单对齐）
						printf("\t%s\t%s\n", insn[i].mnemonic, insn[i].op_str);
					}
					cs_free(insn, count);
				}
				else
				{
					printf("    Failed to disassemble code at 0x%08" PRIx64 " (count==0)\n", start_addr);
				}
			}
			else
			{
				printf("    Failed to read memory at 0x%08" PRIx64 " for disassembly (maybe unmapped).\n", start_addr);
			}
			free(code_buffer);
		}

		cs_close(&handle);
	}
	else
	{
		printf("    Failed to initialize Capstone disassembler.\n");
	}
	PrintStackTrace(uc);
}
bool Executor::Cleanup()
{
	callAndcheckError(uc_hook_del(m_uc, m_interrupt_hook));
	callAndcheckError(uc_hook_del(m_uc, _codeHook));
	callAndcheckError(uc_hook_del(m_uc, m_page_fault));
	callAndcheckError(uc_hook_del(m_uc, m_page_fault2));
	callAndcheckError(uc_hook_del(m_uc, m_page_fault3));
	__check(sMemoryManager->StaticFree(LCD_REGISTER), ERROR_OK, false);
	callAndcheckError(uc_close(m_uc));
}


bool Executor::InitInterrupts()
{
	sMemoryManager->StaticAlloc(RTC_REGISTER, 0x100);
	callAndcheckError(uc_hook_add(m_uc, &m_interrupt_hook, UC_HOOK_INTR, interrupt_hook, this, 0, 1));
	callAndcheckError(uc_hook_add(m_uc, &_codeHook, UC_HOOK_BLOCK, code_hook, NULL, 1, 0));
	callAndcheckError(uc_hook_add(m_uc, &m_page_fault, UC_HOOK_MEM_READ_UNMAPPED, pf, 0, 1, 0));
	callAndcheckError(uc_hook_add(m_uc, &m_page_fault2, UC_HOOK_MEM_WRITE_UNMAPPED, pf, 0, 1, 0));
	callAndcheckError(uc_hook_add(m_uc, &m_page_fault3, UC_HOOK_MEM_FETCH_UNMAPPED, pf, 0, 1, 0));
	return true;
}
void Executor::Execute()
{
	sThreadHandler->NewThread(m_exec->get_entry(), 0, THREAD_PRIORITY_NORMAL, MEM_STACK_SIZE);
	sThreadHandler->LoadCurrentThreadState();

	m_err = UC_ERR_OK;
	printf("Starting execution at 0x%X\n\n", sThreadHandler->GetCurrentThreadPC());
	while (true)
	{
		m_err = uc_emu_start(m_uc, sThreadHandler->GetCurrentThreadPC(), 0, 0, 0);

		//if (sThreadHandler->interruptPC) {
		//	printf("interrupt begin PC: %08X\n", sThreadHandler->interruptPC);
		//	sThreadHandler->interrupting = true;
		//	m_err = uc_emu_start(m_uc, sThreadHandler->interruptPC, 0, 0, 0);
		//	uint32_t r0, r1, r2, r3, sp, pc, lr;
		//	void* args[6] = { &r0, &r1, &r2, &r3, &sp, &pc };
		//	int regs[6] = { UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_PC };


		//	uc_reg_read_batch(m_uc, regs, args, sizeof(args) / sizeof(void*));
		//	printf("interrupt end PC: %08X\n", pc);
		//	sThreadHandler->interrupting = false;
		//	sThreadHandler->interruptPC = 0;
		//}

		if (m_err != UC_ERR_OK) {
			uint32_t cpsr = 0;
			uint32_t pc = 0;
			if (uc_reg_read(m_uc, UC_ARM_REG_CPSR, &cpsr) != UC_ERR_OK) {
				cpsr = 0; // 无法读取时保守处理
			}
			uc_reg_read(m_uc, UC_ARM_REG_PC, &pc);
			// 判断是否为 Thumb：PC 低位为 1 或 CPSR.T 位被置位
			bool is_thumb = ((pc & 1) != 0) || ((cpsr & (1u << 5)) != 0);

			// 用于从内存读取的对齐 PC（清除低位）
			uint64_t pc_addr = (uint64_t)(pc & ~1u);
			if (is_thumb) {
				pc_addr += 2;
			}
			else {
				pc_addr += 4;
			}
			uc_reg_write(m_uc, UC_ARM_REG_PC, &pc_addr);
			sThreadHandler->SaveCurrentThreadState();
		}
			//break;
		sThreadHandler->SwitchThread();
	}

	if (m_err != UC_ERR_OK) {

		uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, sp, pc, lr;
		void* args[16] = { &r0, &r1, &r2, &r3, &r4, &r5, &r6, &r7, &r8, &r9, &r10, &r11, &r12, &sp, &lr, &pc };
		int regs[16] = { UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6,
			UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC };
		uc_reg_read_batch(sExecutor->GetUcInstance(), regs, args, 16);

		printf("Execution aborted on error: %s!\nThread: %i\nRegisters: \n", uc_strerror(m_err), sThreadHandler->GetCurrentThreadId());
		printf("    r0: %08X|%i\n    r1: %08X|%i\n    r2: %08X|%i\n    r3: %08X|%i\n    r4: %08X|%i\n"
			"    r5: %08X|%i\n    r6: %08X|%i\n    r7: %08X|%i\n    r8: %08X|%i\n    r9: %08X|%i\n"
			"   r10: %08X|%i\n   r11: %08X|%i\n   r12: %08X|%i\n"
			"    sp: %08X\n    pc: %08X\n    lr: %08X\n",
			r0, r0, r1, r1, r2, r2, r3, r3, r4, r4, r5, r5, r6, r6, r7, r7, r8, r8,
			r9, r9, r10, r10, r11, r11, r12, r12, sp, pc, lr);

		// ==================== 新增的反汇编代码块 START ====================
		printf("\n--- Disassembly around PC (0x%08X) ---\n", pc);

		csh handle = 0;
		cs_insn* insn = nullptr;
		size_t count = 0;

		// 读取 CPSR（若支持）以检测 Thumb 模式
		uint32_t cpsr = 0;
		if (uc_reg_read(m_uc, UC_ARM_REG_CPSR, &cpsr) != UC_ERR_OK) {
			cpsr = 0; // 无法读取时保守处理
		}

		// 判断是否为 Thumb：PC 低位为 1 或 CPSR.T 位被置位
		bool is_thumb = ((pc & 1) != 0) || ((cpsr & (1u << 5)) != 0);

		// 用于从内存读取的对齐 PC（清除低位）
		uint64_t pc_addr = (uint64_t)(pc & ~1u);

		// Capstone 模式选择
		cs_mode mode = is_thumb ? CS_MODE_THUMB : CS_MODE_ARM;

		if (cs_open(CS_ARCH_ARM, mode, &handle) == CS_ERR_OK)
		{
			// 可配置的上下文字节数（前/后）
			const size_t CONTEXT_BYTES_BEFORE = 32;
			const size_t CONTEXT_BYTES_AFTER = 64;
			const size_t DISASM_SIZE = CONTEXT_BYTES_BEFORE + CONTEXT_BYTES_AFTER;

			uint64_t start_addr = (pc_addr > CONTEXT_BYTES_BEFORE) ? (pc_addr - CONTEXT_BYTES_BEFORE) : 0;

			// 分配缓冲区
			uint8_t* code_buffer = (uint8_t*)malloc(DISASM_SIZE);
			if (!code_buffer) {
				printf("    Failed to allocate code buffer for disassembly.\n");
			}
			else {
				// 从模拟器读取内存，注意若读取失败会返回错误（例如跨越未映射区域）
				if (uc_mem_read(m_uc, start_addr, code_buffer, DISASM_SIZE) == UC_ERR_OK)
				{
					// 可选：告诉 Capstone 显示详细信息
					cs_option(handle, CS_OPT_DETAIL, CS_OPT_OFF);

					// 反汇编（count 0 表示反汇编尽可能多的指令）
					count = cs_disasm(handle, code_buffer, DISASM_SIZE, start_addr, 0, &insn);
					if (count > 0)
					{
						for (size_t i = 0; i < count; i++)
						{
							// 如果是当前 PC 指向的指令，添加箭头标记
							const char* marker = (insn[i].address == pc_addr) ? " -> " : "    ";

							// 打印指令地址、机器码字节、助记符和操作数
							printf("%s0x%08" PRIx64 ":\t", marker, insn[i].address);
							for (size_t b = 0; b < insn[i].size; b++)
								printf("%02x ", insn[i].bytes[b]);
							// 对齐输出，使助记符不挤在字节后面（简单对齐）
							printf("\t%s\t%s\n", insn[i].mnemonic, insn[i].op_str);
						}
						cs_free(insn, count);
					}
					else
					{
						printf("    Failed to disassemble code at 0x%08" PRIx64 " (count==0)\n", start_addr);
					}
				}
				else
				{
					printf("    Failed to read memory at 0x%08" PRIx64 " for disassembly (maybe unmapped).\n", start_addr);
				}
				free(code_buffer);
			}

			cs_close(&handle);
		}
		else
		{
			printf("    Failed to initialize Capstone disassembler.\n");
		}

		// ==================== 新增的反汇编代码块 END ======================
	}
}

void interrupt_hook(uc_engine* uc, uint64_t address, uint32_t size, void* user_data)
{

	uint32_t r0, r1, r2, r3, sp, pc, lr;
	void* args[6] = { &r0, &r1, &r2, &r3, &sp, &pc };
	int regs[6] = { UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_PC };


	uc_reg_read_batch(uc, regs, args, sizeof(args) / sizeof(void*));

	uint32_t SVC;
	uc_mem_read(uc, pc - 4, &SVC, 4);
	uc_mem_read(uc, sp, &lr, 4);


	SVC &= 0xFFFFF;

	uint32_t return_value = sSystemAPI->Call(static_cast<InterruptID>(SVC), SystemServiceArguments());
	// printf("    Caller: %08X\n    PC: %08X\n", lr - 4, pc);
	sp += 8;

	uc_reg_write(uc, UC_ARM_REG_R0, &return_value);
	uc_reg_write(uc, UC_ARM_REG_SP, &sp);
	uc_reg_write(uc, UC_ARM_REG_PC, &lr);
}

