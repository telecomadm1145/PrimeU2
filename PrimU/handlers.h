#pragma once

#include "stdafx.h"

class Memory;


uint32_t dbgMsg(Arguments* args);


uint32_t getCurrentDir(Arguments* args);
uint32_t prgrmIsRunning(Arguments* args);
uint32_t _FindResourceW(Arguments* args);
uint32_t _OpenFile(Arguments* args);
uint32_t _LoadLibraryA(Arguments* args);
uint32_t _FreeLibrary(Arguments* args); 
uint32_t GetSysTime(Arguments* args);

uint32_t _fwrite(Arguments* args);

uint32_t _fclose(Arguments* args);

uint32_t _filesize(Arguments* args);

uint32_t _fread(Arguments* args);

uint32_t OSSetEvent(Arguments* args);
uint32_t OSCreateEvent(Arguments* args);
uint32_t LCDOn(Arguments* args);
uint32_t GetActiveLCD(Arguments* args);

uint32_t lcalloc(Arguments* args);
uint32_t lmalloc(Arguments* args);
uint32_t _lfree(Arguments* args);
uint32_t lrealloc(Arguments* args);

uint32_t _amkdir(Arguments* args);
uint32_t _achdir(Arguments* args);
uint32_t __wfopen(Arguments* args);

uint32_t OSCreateThread(Arguments* args);
uint32_t OSSetThreadPriority(Arguments* args);
uint32_t OSInitCriticalSection(Arguments* args);
uint32_t OSEnterCriticalSection(Arguments* args);
uint32_t OSLeaveCriticalSection(Arguments* args);
uint32_t OSSleep(Arguments* args); 

uint32_t _GetPrivateProfileString(Arguments* args);

uint32_t _SetPrivateProfileString(Arguments* args);


uint32_t _afindfirst(Arguments* args);
#undef _wfindfirst
uint32_t _wfindfirst(Arguments* args);

uint32_t _afindnext(Arguments* args);
#undef _wfindnext
uint32_t _wfindnext(Arguments* args);

uint32_t _findclose(Arguments* args);

uint32_t GetEvent(Arguments* args);

uint32_t _aremove(Arguments* args);

uint32_t _wremove(Arguments* args);

uint32_t CreateFile(Arguments* args);

uint32_t DeviceIoControl(Arguments* args);

uint32_t CloseHandle(Arguments* args);
