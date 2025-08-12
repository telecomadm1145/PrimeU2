#pragma once

#include "stdafx.h"

class Memory;


uint32_t dbgMsg(SystemServiceArguments* args);


uint32_t getCurrentDir(SystemServiceArguments* args);
uint32_t prgrmIsRunning(SystemServiceArguments* args);
uint32_t _FindResourceW(SystemServiceArguments* args);
uint32_t _OpenFile(SystemServiceArguments* args);
uint32_t _LoadLibraryA(SystemServiceArguments* args);
uint32_t _FreeLibrary(SystemServiceArguments* args); 
uint32_t GetSysTime(SystemServiceArguments* args);

uint32_t _fwrite(SystemServiceArguments* args);

uint32_t _fclose(SystemServiceArguments* args);

uint32_t _filesize(SystemServiceArguments* args);

uint32_t _fread(SystemServiceArguments* args);

uint32_t OSSetEvent(SystemServiceArguments* args);
uint32_t OSCreateEvent(SystemServiceArguments* args);
uint32_t LCDOn(SystemServiceArguments* args);
uint32_t GetActiveLCD(SystemServiceArguments* args);

uint32_t lcalloc(SystemServiceArguments* args);
uint32_t lmalloc(SystemServiceArguments* args);
uint32_t _lfree(SystemServiceArguments* args);
uint32_t lrealloc(SystemServiceArguments* args);

uint32_t _amkdir(SystemServiceArguments* args);
uint32_t _achdir(SystemServiceArguments* args);
uint32_t __wfopen(SystemServiceArguments* args);

uint32_t OSCreateThread(SystemServiceArguments* args);
uint32_t OSSetThreadPriority(SystemServiceArguments* args);
uint32_t OSInitCriticalSection(SystemServiceArguments* args);
uint32_t OSEnterCriticalSection(SystemServiceArguments* args);
uint32_t OSLeaveCriticalSection(SystemServiceArguments* args);
uint32_t OSSleep(SystemServiceArguments* args); 

uint32_t _GetPrivateProfileString(SystemServiceArguments* args);

uint32_t _SetPrivateProfileString(SystemServiceArguments* args);


uint32_t _afindfirst(SystemServiceArguments* args);
#undef _wfindfirst
uint32_t _wfindfirst(SystemServiceArguments* args);

uint32_t _afindnext(SystemServiceArguments* args);
#undef _wfindnext
uint32_t _wfindnext(SystemServiceArguments* args);

uint32_t _findclose(SystemServiceArguments* args);

uint32_t GetEvent(SystemServiceArguments* args);

uint32_t _aremove(SystemServiceArguments* args);

uint32_t _wremove(SystemServiceArguments* args);

uint32_t CreateFile(SystemServiceArguments* args);

uint32_t DeviceIoControl(SystemServiceArguments* args);

uint32_t CloseHandle(SystemServiceArguments* args);

uint32_t InterruptInitialize(SystemServiceArguments* args);

uint32_t InterruptDone(SystemServiceArguments* args);

uint32_t BatteryLowCheck(SystemServiceArguments* args);
