#pragma once

#include "stdafx.h"

class Memory;

// ── dbgout.cpp ──
uint32_t dbgMsg(SystemServiceArguments *args);

// ── svc_system.cpp ──
uint32_t LCDOn(SystemServiceArguments *args);
uint32_t GetActiveLCD(SystemServiceArguments *args);
uint32_t SetSystemVariable(SystemServiceArguments *args);
uint32_t lcalloc(SystemServiceArguments *args);
uint32_t lmalloc(SystemServiceArguments *args);
uint32_t lrealloc(SystemServiceArguments *args);
uint32_t _lfree(SystemServiceArguments *args);
uint32_t GetSysTime(SystemServiceArguments *args);
uint32_t SysPowerOff(SystemServiceArguments *args);
uint32_t BatteryLowCheck(SystemServiceArguments *args);
uint32_t InterruptInitialize(SystemServiceArguments *args);
uint32_t InterruptMask(SystemServiceArguments *args);
uint32_t InterruptDisable(SystemServiceArguments *args);
uint32_t InterruptDone(SystemServiceArguments *args);
uint32_t GetEvent(SystemServiceArguments *args);
uint32_t GetMasterIDInfo(SystemServiceArguments *args);
uint32_t _FindResourceW(SystemServiceArguments *args);
uint32_t _LoadLibraryA(SystemServiceArguments *args);
uint32_t _FreeLibrary(SystemServiceArguments *args);
uint32_t _GetModuleFileNameA(SystemServiceArguments *args);
uint32_t prgrmIsRunning(SystemServiceArguments *args);
uint32_t ProgramIsRunningW(SystemServiceArguments *args);
uint32_t GetCurrentExecutable(SystemServiceArguments *);
uint32_t CreateFile(SystemServiceArguments *args);
uint32_t DeviceIoControl(SystemServiceArguments *args);
uint32_t CloseHandle(SystemServiceArguments *args);
uint32_t _OpenFile(SystemServiceArguments *args);
uint32_t _FileSize(SystemServiceArguments *args);
uint32_t _OpenSubFile(SystemServiceArguments *args);
uint32_t _CloseFile(SystemServiceArguments *args);
uint32_t _FseekFile(SystemServiceArguments *args);
uint32_t _ReadFile(SystemServiceArguments *args);
uint32_t FSGetDiskRoomState(SystemServiceArguments *args);

// ── svc_thread.cpp ──
// Thread
uint32_t OSCreateThread(SystemServiceArguments *args);
uint32_t OSTerminateThread(SystemServiceArguments *args);
uint32_t OSSetThreadPriority(SystemServiceArguments *args);
uint32_t OSGetThreadPriority(SystemServiceArguments *args);
uint32_t OSSuspendThread(SystemServiceArguments *args);
uint32_t OSResumeThread(SystemServiceArguments *args);
uint32_t OSWakeUpThread(SystemServiceArguments *args);
uint32_t OSExitThread(SystemServiceArguments *args);
uint32_t OSSleep(SystemServiceArguments *args);

// Semaphore
uint32_t OSCreateSemaphore(SystemServiceArguments *args);
uint32_t OSWaitForSemaphore(SystemServiceArguments *args);
uint32_t OSReleaseSemaphore(SystemServiceArguments *args);
uint32_t OSCloseSemaphore(SystemServiceArguments *args);

// Event
uint32_t OSCreateEvent(SystemServiceArguments *args);
uint32_t OSWaitForEvent(SystemServiceArguments *args);
uint32_t OSSetEvent(SystemServiceArguments *args);
uint32_t OSResetEvent(SystemServiceArguments *args);
uint32_t OSCloseEvent(SystemServiceArguments *args);

// Critical Section
uint32_t OSInitCriticalSection(SystemServiceArguments *args);
uint32_t OSEnterCriticalSection(SystemServiceArguments *args);
uint32_t OSLeaveCriticalSection(SystemServiceArguments *args);
uint32_t OSDeleteCriticalSection(SystemServiceArguments *args);

// Message Queue
uint32_t OSCreateMsgQue(SystemServiceArguments *args);
uint32_t OSPostMsgQue(SystemServiceArguments *args);
uint32_t OSSendMsgQue(SystemServiceArguments *args);
uint32_t OSPeekMsgQue(SystemServiceArguments *args);
uint32_t OSGetMsgQue(SystemServiceArguments *args);
uint32_t OSCloseMsgQue(SystemServiceArguments *args);

// ── svc_filesystem.cpp ──
uint32_t __afopen(SystemServiceArguments *args);
uint32_t __wfopen(SystemServiceArguments *args);
uint32_t _fclose(SystemServiceArguments *args);
uint32_t _fread(SystemServiceArguments *args);
uint32_t _fwrite(SystemServiceArguments *args);
uint32_t __fseek(SystemServiceArguments *args);
uint32_t __ftell(SystemServiceArguments *args);
uint32_t _filesize(SystemServiceArguments *args);
uint32_t _amkdir(SystemServiceArguments *args);
uint32_t _wmkdir(SystemServiceArguments *args);
uint32_t _wrmdir(SystemServiceArguments *args);
uint32_t _achdir(SystemServiceArguments *args);
uint32_t _wchdir(SystemServiceArguments *args);
uint32_t _aremove(SystemServiceArguments *args);
uint32_t _wremove(SystemServiceArguments *args);
uint32_t _GetPrivateProfileString(SystemServiceArguments *args);
uint32_t _SetPrivateProfileString(SystemServiceArguments *args);
uint32_t _afindfirst(SystemServiceArguments *args);
#undef _wfindfirst
uint32_t _wfindfirst(SystemServiceArguments *args);
uint32_t _afindnext(SystemServiceArguments *args);
#undef _wfindnext
uint32_t _wfindnext(SystemServiceArguments *args);
uint32_t _findclose(SystemServiceArguments *args);
uint32_t _afnsplit(SystemServiceArguments *args);
uint32_t _afnmerge(SystemServiceArguments *args);

// ── svc_system.cpp — event input (called from UI layer) ──
void EnqueueEvent(struct UIMultipressEvent uime);
void EnqueueSpecial(int val);
void TouchUpdate(int x, int y, int finger_id, enum ui_event_type_e status);

// ── svc_system.cpp ──
void sys_init();