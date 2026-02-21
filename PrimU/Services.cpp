// Services.cpp — 服务注册表
// 所有 handler 实现已迁移到 svc_filesystem.cpp、svc_thread.cpp、svc_system.cpp
// 共用基础设施在 svc_common.h / svc_common.cpp（VFileSystem / VMPath）

#include "Services.h"
#include "handlers.h"
#include "stdafx.h"

#define REGISTER_HANDLER(id, s, n, h)                                          \
  service_table.at(id - SDKLIB_FirstService) = h

namespace {
struct Reg {
public:
  Reg() {
    // ── 线程 / 同步 ──
    // Semaphore
    REGISTER_HANDLER(SDKLIB_OSCreateSemaphore, HANDLE_IMPLEMENTED,
                     "OSCreateSemaphore", OSCreateSemaphore);
    REGISTER_HANDLER(SDKLIB_OSWaitForSemaphore, HANDLE_IMPLEMENTED,
                     "OSWaitForSemaphore", OSWaitForSemaphore);
    REGISTER_HANDLER(SDKLIB_OSReleaseSemaphore, HANDLE_IMPLEMENTED,
                     "OSReleaseSemaphore", OSReleaseSemaphore);
    REGISTER_HANDLER(SDKLIB_OSCloseSemaphore, HANDLE_IMPLEMENTED,
                     "OSCloseSemaphore", OSCloseSemaphore);

    REGISTER_HANDLER(SDKLIB_OSCreateThread, HANDLE_IMPLEMENTED,
                     "OSCreateThread", OSCreateThread);
    REGISTER_HANDLER(SDKLIB_OSSetThreadPriority, HANDLE_IMPLEMENTED,
                     "OSSetThreadPriority", OSSetThreadPriority);
    REGISTER_HANDLER(SDKLIB_OSSuspendThread, HANDLE_NAMEONLY, "OSSuspendThread",
                     OSSuspendThread);
    REGISTER_HANDLER(SDKLIB_OSResumeThread, HANDLE_NAMEONLY, "OSResumeThread",
                     OSResumeThread);
    REGISTER_HANDLER(SDKLIB_OSSleep, HANDLE_IMPLEMENTED, "OSSleep", OSSleep);
    REGISTER_HANDLER(SDKLIB_OSCreateEvent, HANDLE_IMPLEMENTED, "OSCreateEvent",
                     OSCreateEvent);
    REGISTER_HANDLER(SDKLIB_OSWaitForEvent, HANDLE_NAMEONLY, "OSWaitForEvent",
                     OSWaitForEvent);
    REGISTER_HANDLER(SDKLIB_OSSetEvent, HANDLE_IMPLEMENTED, "OSSetEvent",
                     OSSetEvent);
    REGISTER_HANDLER(SDKLIB_OSResetEvent, HANDLE_IMPLEMENTED, "OSResetEvent",
                     OSResetEvent);
    REGISTER_HANDLER(SDKLIB_OSCloseEvent, HANDLE_IMPLEMENTED, "OSCloseEvent",
                     OSCloseEvent);
    REGISTER_HANDLER(SDKLIB_OSInitCriticalSection, HANDLE_IMPLEMENTED,
                     "OSInitCriticalSection", OSInitCriticalSection);
    REGISTER_HANDLER(SDKLIB_OSEnterCriticalSection, HANDLE_IMPLEMENTED,
                     "OSEnterCriticalSection", OSEnterCriticalSection);
    REGISTER_HANDLER(SDKLIB_OSLeaveCriticalSection, HANDLE_IMPLEMENTED,
                     "OSLeaveCriticalSection", OSLeaveCriticalSection);

    // ── 中断 / 电源 ──
    REGISTER_HANDLER(SDKLIB_InterruptInitialize, HANDLE_NAMEONLY,
                     "InterruptInitialize", InterruptInitialize);
    REGISTER_HANDLER(SDKLIB_InterruptDone, HANDLE_NAMEONLY, "InterruptDone",
                     InterruptDone);
    REGISTER_HANDLER(SDKLIB_SysPowerOff, HANDLE_NAMEONLY, "SysPowerOff",
                     SysPowerOff);
    REGISTER_HANDLER(SDKLIB_BatteryLowCheck, HANDLE_NAMEONLY, "BatteryLowCheck",
                     BatteryLowCheck);

    // ── LCD / 显示 ──
    REGISTER_HANDLER(SDKLIB_LCDOn, HANDLE_IMPLEMENTED, "LCDOn", LCDOn);
    REGISTER_HANDLER(SDKLIB_GetActiveLCD, HANDLE_IMPLEMENTED, "GetActiveLCD",
                     GetActiveLCD);

    // ── 内存分配 ──
    REGISTER_HANDLER(SDKLIB_lmalloc, HANDLE_IMPLEMENTED, "lmalloc", lmalloc);
    REGISTER_HANDLER(SDKLIB_lcalloc, HANDLE_IMPLEMENTED, "lcalloc", lcalloc);
    REGISTER_HANDLER(SDKLIB_lrealloc, HANDLE_IMPLEMENTED, "lrealloc", lrealloc);
    REGISTER_HANDLER(SDKLIB__lfree, HANDLE_IMPLEMENTED, "_lfree", _lfree);

    // ── 事件输入 ──
    REGISTER_HANDLER(SDKLIB_GetEvent, HANDLE_NAMEONLY, "GetEvent", GetEvent);
    REGISTER_HANDLER(SDKLIB_SetSystemVariable, HANDLE_NAMEONLY,
                     "SetSystemVariable", SetSystemVariable);

    // ── 系统 / 时间 / ID ──
    REGISTER_HANDLER(SDKLIB_GetSysTime, HANDLE_IMPLEMENTED, "GetSysTime",
                     GetSysTime);
    REGISTER_HANDLER(SDKLIB_GetMasterIDInfo, HANDLE_NAMEONLY, "GetMasterIDInfo",
                     GetMasterIDInfo);

    // ── 标准文件 I/O ──
    REGISTER_HANDLER(SDKLIB__fclose, HANDLE_IMPLEMENTED, "_fclose", _fclose);
    REGISTER_HANDLER(SDKLIB__filesize, HANDLE_NAMEONLY, "_filesize", _filesize);
    REGISTER_HANDLER(SDKLIB___fseek, HANDLE_NAMEONLY, "__fseek", __fseek);
    REGISTER_HANDLER(SDKLIB__ftell, HANDLE_NAMEONLY, "_ftell", __ftell);
    REGISTER_HANDLER(SDKLIB__fread, HANDLE_NAMEONLY, "_fread", _fread);
    REGISTER_HANDLER(SDKLIB__fwrite, HANDLE_IMPLEMENTED, "_fwrite", _fwrite);

    // ── 文件查找 ──
    REGISTER_HANDLER(SDKLIB__afindfirst, HANDLE_NAMEONLY, "_afindfirst",
                     _afindfirst);
    REGISTER_HANDLER(SDKLIB__afindnext, HANDLE_NAMEONLY, "_afindnext",
                     _afindnext);
    REGISTER_HANDLER(SDKLIB__findclose, HANDLE_NAMEONLY, "_findclose",
                     _findclose);

    // ── 文件删除 / 目录 ──
    REGISTER_HANDLER(SDKLIB__aremove, HANDLE_NAMEONLY, "_aremove", _aremove);
    REGISTER_HANDLER(SDKLIB__amkdir, HANDLE_IMPLEMENTED, "_amkdir", _amkdir);
    REGISTER_HANDLER(SDKLIB__achdir, HANDLE_IMPLEMENTED, "_achdir", _achdir);

    // ── INI ──
    REGISTER_HANDLER(SDKLIB__GetPrivateProfileString, HANDLE_IMPLEMENTED,
                     "_GetPrivateProfileString", _GetPrivateProfileString);
    REGISTER_HANDLER(SDKLIB__WritePrivateProfileString, HANDLE_NAMEONLY,
                     "_WritePrivateProfileString", _SetPrivateProfileString);

    // ── 路径操作 ──
    REGISTER_HANDLER(SDKLIB__afnsplit, HANDLE_NAMEONLY, "_afnsplit", _afnsplit);
    REGISTER_HANDLER(SDKLIB__afnmerge, HANDLE_NAMEONLY, "_afnmerge", _afnmerge);

    // ── 加载器文件 API ──
    REGISTER_HANDLER(SDKLIB_FSGetDiskRoomState, HANDLE_NAMEONLY,
                     "FSGetDiskRoomState", FSGetDiskRoomState);
    REGISTER_HANDLER(SDKLIB__OpenFile, HANDLE_IMPLEMENTED, "_OpenFile",
                     _OpenFile);
    REGISTER_HANDLER(SDKLIB__CloseFile, HANDLE_NAMEONLY, "_CloseFile",
                     _CloseFile);
    REGISTER_HANDLER(SDKLIB__ReadFile, HANDLE_NAMEONLY, "_ReadFile", _ReadFile);
    REGISTER_HANDLER(SDKLIB__FseekFile, HANDLE_NAMEONLY, "_FseekFile",
                     _FseekFile);
    REGISTER_HANDLER(SDKLIB__FileSize, HANDLE_NAMEONLY, "_FileSize", _FileSize);
    REGISTER_HANDLER(SDKLIB__OpenSubFile, HANDLE_NAMEONLY, "_OpenSubFile",
                     _OpenSubFile);

    // ── 程序管理 ──
    REGISTER_HANDLER(SDKLIB_GetCurrentPathA, HANDLE_IMPLEMENTED,
                     "GetCurrentPathA", GetCurrentExecutable);
    REGISTER_HANDLER(SDKLIB_ProgramIsRunningA, HANDLE_IMPLEMENTED,
                     "ProgramIsRunningA", prgrmIsRunning);
    REGISTER_HANDLER(SDKLIB_ProgramIsRunningW, HANDLE_NAMEONLY,
                     "ProgramIsRunningW", ProgramIsRunningW);

    // ── 动态链接 ──
    REGISTER_HANDLER(SDKLIB__LoadLibraryA, HANDLE_IMPLEMENTED, "_LoadLibraryA",
                     _LoadLibraryA);
    REGISTER_HANDLER(SDKLIB__GetModuleFileNameA, HANDLE_NAMEONLY,
                     "_GetModuleFileNameA", _GetModuleFileNameA);
    REGISTER_HANDLER(SDKLIB__FindResourceW, HANDLE_IMPLEMENTED,
                     "_FindResourceW", _FindResourceW);
    REGISTER_HANDLER(SDKLIB__FreeLibrary, HANDLE_IMPLEMENTED, "_FreeLibrary",
                     _FreeLibrary);

    // ── 设备 I/O ──
    REGISTER_HANDLER(SDKLIB_CreateFile, HANDLE_NAMEONLY, "CreateFile",
                     CreateFile);
    REGISTER_HANDLER(SDKLIB_DeviceIoControl, HANDLE_NAMEONLY, "DeviceIoControl",
                     DeviceIoControl);
    REGISTER_HANDLER(SDKLIB_CloseHandle, HANDLE_NAMEONLY, "CloseHandle",
                     CloseHandle);

    // ── Wide 文件系统 ──
    REGISTER_HANDLER(SDKLIB___wfopen, HANDLE_IMPLEMENTED, "__wfopen", __wfopen);
    REGISTER_HANDLER(SDKLIB__wfindfirst, HANDLE_NAMEONLY, "_wfindfirst",
                     _wfindfirst);
    REGISTER_HANDLER(SDKLIB__wfindnext, HANDLE_NAMEONLY, "_wfindnext",
                     _wfindnext);
    REGISTER_HANDLER(SDKLIB___wremove, HANDLE_NAMEONLY, "__wremove", _wremove);
    REGISTER_HANDLER(SDKLIB__wmkdir, HANDLE_NAMEONLY, "_wmkdir", _wmkdir);
    REGISTER_HANDLER(SDKLIB__wrmdir, HANDLE_NAMEONLY, "_wrmdir", _wrmdir);
    REGISTER_HANDLER(SDKLIB__wchdir, HANDLE_NAMEONLY, "_wchdir", _wchdir);

    // ── 调试 ──
    REGISTER_HANDLER(SDKLIB_WriteComDebugMsg, HANDLE_IMPLEMENTED,
                     "WriteComDebugMsg", dbgMsg);
  }
} reg;
} // namespace

std::array<SyscallFn, SDKLIB_LastService - SDKLIB_FirstService + 1>
    service_table{};