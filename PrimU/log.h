#pragma once
#include "InterruptHandler.h"
#include <chrono>
#include "logbuf.h"
class SVCCallRecord {
public:
	unsigned int svc;
	int threadid;
	SystemServiceArguments ssa;
	std::chrono::steady_clock::time_point timestamp;
};
extern RollingLogBuffer<SVCCallRecord> g_svcCallLog;