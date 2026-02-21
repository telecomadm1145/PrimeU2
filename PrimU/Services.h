#pragma once
#include "InterruptHandler.h"
#include "interrupts.h"
#include <array>
#include <cstdint>

using SyscallFn = uint32_t (*)(SystemServiceArguments *args);
extern std::array<SyscallFn, SDKLIB_LastService - SDKLIB_FirstService + 1>
    service_table;