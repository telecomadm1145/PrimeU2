#pragma once

#include <cstdint>
#include <unicorn/unicorn.h>


/// Handles SVC (Supervisor Call) dispatching for emulated ARM code.
/// Registers a UC_HOOK_INTR hook on each uc_engine that intercepts
/// SVC instructions, decodes the SVC number, and calls into the
/// service_table defined in Services.cpp.
namespace SvcDispatcher {
/// Register the SVC interrupt hook on the given engine.
void RegisterHook(uc_engine *uc);
} // namespace SvcDispatcher
