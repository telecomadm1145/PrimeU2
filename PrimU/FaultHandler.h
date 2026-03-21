#pragma once

#include <cstdint>
#include <unicorn/unicorn.h>

void PrintStackTrace(uc_engine* uc);
/// Handles memory access faults in the emulated CPU, providing
/// register dumps, disassembly context, and stack traces.
namespace FaultHandler {
/// Register the memory-invalid hook on the given engine.
void RegisterHook(uc_engine *uc);
} // namespace FaultHandler
