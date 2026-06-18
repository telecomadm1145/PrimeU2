#define NOMINMAX
#include "GdbStub.h"
#include "MemoryManager.h"
#include "Thread.h"
#include "ThreadHandler.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>

#ifndef _WIN32
#error "Windows only"
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

GdbStub* GdbStub::_instance = nullptr;
GdbStub* GdbStub::GetInstance() {
	if (!_instance) _instance = new GdbStub;
	return _instance;
}
GdbStub::~GdbStub() { Stop(); }

static uint8_t HexNib(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return 10 + c - 'a';
	if (c >= 'A' && c <= 'F') return 10 + c - 'A';
	return 0;
}
static uint8_t HexByte(char h, char l) { return (HexNib(h) << 4) | HexNib(l); }
static char NibHex(uint8_t n) { return "0123456789abcdef"[n & 0xF]; }
static std::string ToHex(const uint8_t* d, size_t n) {
	std::string s; s.reserve(n * 2);
	for (size_t i = 0; i < n; ++i) { s += NibHex(d[i] >> 4); s += NibHex(d[i] & 0xF); }
	return s;
}
static std::vector<uint8_t> FromHex(const std::string& h) {
	std::vector<uint8_t> o; o.reserve(h.size() / 2);
	for (size_t i = 0; i + 1 < h.size(); i += 2) o.push_back(HexByte(h[i], h[i + 1]));
	return o;
}
static uint32_t HexU32(const std::string& h) {
	uint32_t v = 0; for (char c : h) v = (v << 4) | HexNib(c); return v;
}
static std::string LeHex32(uint32_t v) {
	uint8_t b[4] = { (uint8_t)(v),(uint8_t)(v >> 8),(uint8_t)(v >> 16),(uint8_t)(v >> 24) };
	return ToHex(b, 4);
}
static uint32_t FromLeHex32(const std::string& h) {
	if (h.size() < 8) return 0;
	auto b = FromHex(h.substr(0, 8));
	if (b.size() < 4) return 0;
	return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}
static uint8_t Checksum(const std::string& d) {
	uint8_t s = 0; for (char c : d) s += (uint8_t)c; return s;
}

// ARM reg map
static const int kUcReg[] = {
	UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,
	UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,
	UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,
	UC_ARM_REG_R12,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,
};

//  Thread helpers 
Thread* GdbStub::GetThreadById(int id) const {
	std::lock_guard<std::mutex> lk(_threadsMutex);
	auto it = _registeredThreads.find(id);
	return it != _registeredThreads.end() ? it->second : nullptr;
}
uc_engine* GdbStub::GetSelectedEngine() const {
	Thread* t = GetThreadById(_selectedTid);
	return t ? t->engine() : nullptr;
}
void GdbStub::StopAllEngines() {
	std::lock_guard<std::mutex> lk(_threadsMutex);
	for (auto& [id, t] : _registeredThreads)
		if (t && t->engine()) uc_emu_stop(t->engine());
}

//  Breakpoint hook per-address 
void GdbStub::BpHookCallback(uc_engine* uc, uint64_t addr, uint32_t, void*) {
	g_gdbBpHit = true;
	g_gdbBpAddr = (uint32_t)addr;
	uc_emu_stop(uc);
}

void GdbStub::AddHooksForBreakpoint(uint32_t addr) {
	addr &= ~1u; // strip Thumb bit
	BpEntry entry;
	std::lock_guard<std::mutex> lk(_threadsMutex);
	for (auto& [id, t] : _registeredThreads) {
		uc_hook h;
		uc_hook_add(t->engine(), &h, UC_HOOK_CODE,
			(void*)BpHookCallback, this, addr, addr);
		entry.hooks.push_back({ t->engine(), h });
	}
	std::lock_guard<std::mutex> lk2(_bpMutex);
	_breakpoints[addr] = std::move(entry);
}

void GdbStub::RemoveHooksForBreakpoint(uint32_t addr) {
	addr &= ~1u;
	std::lock_guard<std::mutex> lk(_bpMutex);
	auto it = _breakpoints.find(addr);
	if (it == _breakpoints.end()) return;
	for (auto& [uc, h] : it->second.hooks)
		uc_hook_del(uc, h);
	_breakpoints.erase(it);
}

void GdbStub::AddAllBreakpointHooksForEngine(uc_engine* uc) {
	std::lock_guard<std::mutex> lk(_bpMutex);
	for (auto& [addr, entry] : _breakpoints) {
		uc_hook h;
		uc_hook_add(uc, &h, UC_HOOK_CODE,
			(void*)BpHookCallback, this, addr, addr);
		entry.hooks.push_back({ uc, h });
	}
}

void GdbStub::RegisterThread(Thread* thread) {
	if (!_enabled.load() || !thread) return;
	int id = thread->GetId();
	{
		std::lock_guard<std::mutex> lk(_threadsMutex);
		if (_registeredThreads.count(id)) return;
		_registeredThreads[id] = thread;
	}
	AddAllBreakpointHooksForEngine(thread->engine());
	printf("[GDB] Registered Thread %d\n", id);
}

//  Halt / resume 
int GdbStub::WaitIfHalted(int threadId) {
	if (!_enabled.load()) return 0;
	if (_shutdownRequested.load()) return -1;
	if (!_halted.load()) return 0;

	std::unique_lock<std::mutex> lk(_haltMutex);
	_haltCv.wait(lk, [&] {
		return !_halted.load()
			|| _stepThreadId.load() == threadId
			|| _shutdownRequested.load();
		});
	if (_shutdownRequested.load()) return -1;
	if (_stepThreadId.load() == threadId) {
		_stepThreadId.store(-1);
		return 1; // step: count=1
	}
	return 0; // continue: count=0
}

void GdbStub::NotifyStop(uc_engine* uc, GdbStopReason reason, uint32_t addr) {
	if (!_clientConnected.load()) return;
	_halted.store(true);
	StopAllEngines();

	// Identify stopped thread
	{
		std::lock_guard<std::mutex> lk(_threadsMutex);
		for (auto& [id, t] : _registeredThreads) {
			if (t->engine() == uc) {
				_stoppedTid = id;
				_selectedTid = id;
				break;
			}
		}
	}
	_lastStopReason = reason;

	// Send stop reply (dedup: only one per stop)
	{
		std::lock_guard<std::mutex> lk(_replyMutex);
		if (!_stopReplySent) {
			SendPacket(BuildStopReply(reason));
			_stopReplySent = true;
		}
	}

	// Block until debugger resumes
	std::unique_lock<std::mutex> lk(_haltMutex);
	_haltCv.wait(lk, [this] {
		return !_halted.load() || _stepThreadId.load() >= 0 || _shutdownRequested.load();
		});
}

void GdbStub::WaitForDebugger() {
	if (!_enabled.load()) return;
	printf("[GDB] Waiting for debugger on port %u ...\n", _port);
	while (!_clientConnected.load() && !_shutdownRequested.load())
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	if (_shutdownRequested.load()) return;
	printf("[GDB] Debugger connected – halted at entry\n");
	_lastStopReason = GdbStopReason::Breakpoint;
	_halted.store(true);
	std::unique_lock<std::mutex> lk(_haltMutex);
	_haltCv.wait(lk, [this] {
		return !_halted.load() || _stepThreadId.load() >= 0 || _shutdownRequested.load();
		});
	printf("[GDB] Debugger released\n");
}

//  Networking 
bool GdbStub::Start(uint16_t port) {
	if (_enabled.load()) return true;
	_port = port;
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
	_listenSock = (uintptr_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if ((SOCKET)_listenSock == INVALID_SOCKET) return false;
	int opt = 1;
	setsockopt((SOCKET)_listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
	sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons(_port);
	if (bind((SOCKET)_listenSock, (sockaddr*)&a, sizeof(a)) == SOCKET_ERROR) { closesocket((SOCKET)_listenSock); return false; }
	if (listen((SOCKET)_listenSock, 1) == SOCKET_ERROR) { closesocket((SOCKET)_listenSock); return false; }
	_enabled.store(true); _shutdownRequested.store(false);
	printf("[GDB] Listening on port %u\n", _port);
	_acceptThread = std::thread(&GdbStub::AcceptLoop, this);
	return true;
}
void GdbStub::Stop() {
	_shutdownRequested.store(true); _enabled.store(false);
	if ((SOCKET)_listenSock != INVALID_SOCKET) { closesocket((SOCKET)_listenSock); _listenSock = ~(uintptr_t)0; }
	if ((SOCKET)_clientSock != INVALID_SOCKET) { closesocket((SOCKET)_clientSock); _clientSock = ~(uintptr_t)0; }
	_halted.store(false); _haltCv.notify_all();
	if (_acceptThread.joinable()) _acceptThread.join();
	if (_clientThread.joinable()) _clientThread.join();
	_clientConnected.store(false);
}
void GdbStub::AcceptLoop() {
	while (!_shutdownRequested.load()) {
		SOCKET c = accept((SOCKET)_listenSock, nullptr, nullptr);
		if (c == INVALID_SOCKET) { if (_shutdownRequested.load()) break; continue; }
		int f = 1; setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char*)&f, sizeof(f));
		printf("[GDB] Client connected\n");
		_clientSock = (uintptr_t)c; _clientConnected.store(true);
		if (_clientThread.joinable()) _clientThread.join();
		_clientThread = std::thread(&GdbStub::ClientLoop, this);
	}
}
void GdbStub::ClientLoop() {
	while (!_shutdownRequested.load() && _clientConnected.load()) {
		std::string pkt;
		if (!RecvPacket(pkt)) break;
		HandlePacket(pkt);
	}
	_clientConnected.store(false);
	if ((SOCKET)_clientSock != INVALID_SOCKET) { closesocket((SOCKET)_clientSock); _clientSock = ~(uintptr_t)0; }
	_halted.store(false); _haltCv.notify_all();
}
bool GdbStub::SendRaw(const char* d, size_t n) {
	size_t s = 0; while (s < n) { int r = send((SOCKET)_clientSock, d + s, (int)(n - s), 0); if (r <= 0)return false; s += r; } return true;
}
bool GdbStub::SendPacket(const std::string& p) {
	char cs[4]; snprintf(cs, sizeof(cs), "%02x", Checksum(p));
	std::string w = "$" + p + "#" + cs; return SendRaw(w.c_str(), w.size());
}
bool GdbStub::RecvPacket(std::string& out) {
	out.clear(); char ch;
	while (true) {
		int r = recv((SOCKET)_clientSock, &ch, 1, 0); if (r <= 0)return false;
		if (ch == 0x03) { out = "\x03"; return true; } if (ch == '$')break;
	}
	std::string body; body.reserve(256);
	while (true) { int r = recv((SOCKET)_clientSock, &ch, 1, 0); if (r <= 0)return false; if (ch == '#')break; body += ch; }
	char cs[2]; if (recv((SOCKET)_clientSock, &cs[0], 1, 0) <= 0)return false;
	if (recv((SOCKET)_clientSock, &cs[1], 1, 0) <= 0)return false;
	SendRaw("+", 1); out = body; return true;
}

//  Dispatch 
void GdbStub::HandlePacket(const std::string& pkt) {
	if (pkt.empty()) return;
	if (pkt[0] == '\x03') {
		_halted.store(true); StopAllEngines();
		_lastStopReason = GdbStopReason::UserBreak;
		{ std::lock_guard<std::mutex> lk(_replyMutex); _stopReplySent = true; }
		SendPacket(BuildStopReply(GdbStopReason::UserBreak));
		return;
	}
	char cmd = pkt[0]; std::string args = pkt.substr(1);
	switch (cmd) {
	case '?': CmdHaltReason(); break;
	case 'g': CmdReadRegisters(); break;
	case 'G': CmdWriteRegisters(args); break;
	case 'p': CmdReadRegister(args); break;
	case 'P': CmdWriteRegister(args); break;
	case 'm': CmdReadMemory(args); break;
	case 'M': CmdWriteMemory(args); break;
	case 'X': CmdWriteMemoryBin(args); break;
	case 'c': CmdContinue(args); break;
	case 's': CmdStep(args); break;
	case 'Z': CmdAddBreakpoint(args); break;
	case 'z': CmdRemoveBreakpoint(args); break;
	case 'D': CmdDetach(); break;
	case 'k': CmdKill(); break;
	case 'q': HandleQuery(pkt); break;
	case 'Q': HandleQuery(pkt); break;
	case 'v': HandleVPacket(pkt); break;
	case 'H': HandleHCommand(args); break;
	default: SendPacket(""); break;
	}
}

void GdbStub::HandleHCommand(const std::string& args) {
	if (args.size() < 2) { SendPacket("OK"); return; }
	char op = args[0];
	int tid = (int)HexU32(args.substr(1));
	if (op == 'g') {
		// tid 0 = any → use stopped thread; otherwise direct
		_selectedTid = (tid <= 0) ? _stoppedTid : tid;
		printf("[GDB] Hg -> selected tid %d\n", _selectedTid);
	}
	SendPacket("OK");
}

void GdbStub::HandleQuery(const std::string& pkt) {
	if (pkt.rfind("qSupported", 0) == 0) { SendPacket("PacketSize=4000;swbreak+;hwbreak+;qXfer:features:read+"); return; }
	if (pkt == "qAttached") { SendPacket("1"); return; }
	if (pkt == "qC") { char b[16]; snprintf(b, sizeof(b), "QC%x", _stoppedTid); SendPacket(b); return; }
	if (pkt.rfind("qfThreadInfo", 0) == 0) {
		std::string r = "m";
		{
			std::lock_guard<std::mutex> lk(_threadsMutex); bool f = true;
			for (auto& [id, t] : _registeredThreads) { if (!f)r += ","; char b[16]; snprintf(b, sizeof(b), "%x", id); r += b; f = false; }
		}
		if (r == "m")r = "m1"; SendPacket(r); return;
	}
	if (pkt.rfind("qsThreadInfo", 0) == 0) { SendPacket("l"); return; }
	if (pkt == "qTStatus") { SendPacket(""); return; }
	if (pkt.rfind("qXfer:features:read:target.xml:", 0) == 0) {
		std::string xml =
			"<?xml version=\"1.0\"?>\n<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
			"<target version=\"1.0\">\n<architecture>arm</architecture>\n"
			"<feature name=\"org.gnu.gdb.arm.core\">\n"
			"<reg name=\"r0\" bitsize=\"32\" type=\"uint32\"/>\n"
			"<reg name=\"r1\" bitsize=\"32\" type=\"uint32\"/>\n"
			"<reg name=\"r2\" bitsize=\"32\" type=\"uint32\"/>\n"
			"<reg name=\"r3\" bitsize=\"32\" type=\"uint32\"/>\n"
			"<reg name=\"r4\" bitsize=\"32\" type=\"uint32\"/>\n"
			"<reg name=\"r5\" bitsize=\"32\" type=\"uint32\"/>\n"
			"<reg name=\"r6\" bitsize=\"32\" type=\"uint32\"/>\n"
			"<reg name=\"r7\" bitsize=\"32\" type=\"uint32\"/>\n"
			"<reg name=\"r8\" bitsize=\"32\" type=\"uint32\"/>\n"
			"<reg name=\"r9\" bitsize=\"32\" type=\"uint32\"/>\n"
			"<reg name=\"r10\" bitsize=\"32\" type=\"uint32\"/>\n"
			"<reg name=\"r11\" bitsize=\"32\" type=\"uint32\"/>\n"
			"<reg name=\"r12\" bitsize=\"32\" type=\"uint32\"/>\n"
			"<reg name=\"sp\" bitsize=\"32\" type=\"data_ptr\"/>\n"
			"<reg name=\"lr\" bitsize=\"32\"/>\n"
			"<reg name=\"pc\" bitsize=\"32\" type=\"code_ptr\"/>\n"
			"<reg name=\"cpsr\" bitsize=\"32\" regnum=\"25\"/>\n"
			"</feature>\n</target>\n";
		auto cp = pkt.rfind(':'); auto ol = pkt.substr(cp + 1); auto cm = ol.find(',');
		uint32_t off = HexU32(ol.substr(0, cm)), len = HexU32(ol.substr(cm + 1));
		if (off >= xml.size()) { SendPacket("l"); }
		else { size_t rem = xml.size() - off; SendPacket((rem <= len ? "l" : "m") + xml.substr(off, len)); }
		return;
	}
	if (pkt.rfind("qXfer:features:read:", 0) == 0) { SendPacket(""); return; }
	if (pkt == "qOffsets") { SendPacket("Text=0;Data=0;Bss=0"); return; }
	if (pkt.rfind("qRcmd,", 0) == 0) {
		auto cb = FromHex(pkt.substr(6)); std::string cmd(cb.begin(), cb.end());
		printf("[GDB] Monitor: %s\n", cmd.c_str());
		std::string msg = "OK\n"; SendPacket("O" + ToHex((const uint8_t*)msg.c_str(), msg.size())); SendPacket("OK"); return;
	}
	if (pkt.rfind("qSymbol", 0) == 0) { SendPacket("OK"); return; }
	SendPacket("");
}

void GdbStub::HandleVPacket(const std::string& pkt) {
	if (pkt == "vMustReplyEmpty") { SendPacket(""); return; }
	if (pkt.rfind("vCont?", 0) == 0) { SendPacket("vCont;c;s;t"); return; }
	if (pkt.rfind("vCont;", 0) == 0) {
		auto a = pkt.substr(6);
		if (a.find('s') != std::string::npos) CmdStep("");
		else if (a.find('c') != std::string::npos) CmdContinue("");
		else SendPacket("OK");
		return;
	}
	SendPacket("");
}

//  Registers 
uint32_t GdbStub::ReadReg(int r) const {
	uint32_t v = 0; auto uc = GetSelectedEngine(); if (uc) uc_reg_read(uc, r, &v); return v;
}
void GdbStub::WriteReg(int r, uint32_t v) {
	auto uc = GetSelectedEngine(); if (uc) uc_reg_write(uc, r, &v);
}
void GdbStub::CmdReadRegisters() {
	std::string r; r.reserve(26 * 8 + 8 * 24);
	for (int i = 0; i < 16; ++i) r += LeHex32(ReadReg(kUcReg[i]));
	for (int i = 0; i < 8; ++i) r += "000000000000000000000000";
	r += "00000000";
	r += LeHex32(ReadReg(UC_ARM_REG_CPSR));
	SendPacket(r);
}
void GdbStub::CmdWriteRegisters(const std::string& d) {
	size_t p = 0;
	for (int i = 0; i < 16 && p + 8 <= d.size(); ++i, p += 8) WriteReg(kUcReg[i], FromLeHex32(d.substr(p, 8)));
	p += 8 * 24 + 8;
	if (p + 8 <= d.size()) WriteReg(UC_ARM_REG_CPSR, FromLeHex32(d.substr(p, 8)));
	SendPacket("OK");
}
void GdbStub::CmdReadRegister(const std::string& pkt) {
	int n = (int)HexU32(pkt);
	if (n < 16) SendPacket(LeHex32(ReadReg(kUcReg[n])));
	else if (n >= 16 && n < 24) SendPacket("000000000000000000000000");
	else if (n == 24) SendPacket("00000000");
	else if (n == 25) SendPacket(LeHex32(ReadReg(UC_ARM_REG_CPSR)));
	else SendPacket("00000000");
}
void GdbStub::CmdWriteRegister(const std::string& pkt) {
	auto eq = pkt.find('='); if (eq == std::string::npos) { SendPacket("E01"); return; }
	int n = (int)HexU32(pkt.substr(0, eq)); uint32_t v = FromLeHex32(pkt.substr(eq + 1));
	if (n < 16) WriteReg(kUcReg[n], v);
	else if (n == 25) WriteReg(UC_ARM_REG_CPSR, v);
	SendPacket("OK");
}

//  Memory — direct MemoryManager access 
void GdbStub::CmdReadMemory(const std::string& pkt) {
	auto cm = pkt.find(','); if (cm == std::string::npos) { SendPacket("E01"); return; }
	uint32_t addr = HexU32(pkt.substr(0, cm)), len = HexU32(pkt.substr(cm + 1));
	if (!len) { SendPacket(""); return; }
	if (len > GDB_PACKET_BUF / 2) len = GDB_PACKET_BUF / 2;
	std::string r; r.reserve(len * 2);
	for (uint32_t i = 0; i < len; ++i) {
		RealPtr rp = sMemoryManager->GetRealAddr(addr + i);
		if (!rp) { r += "00"; }
		else { r += NibHex(*rp >> 4); r += NibHex(*rp & 0xF); }
	}
	SendPacket(r);
}
void GdbStub::CmdWriteMemory(const std::string& pkt) {
	auto cm = pkt.find(','); auto cl = pkt.find(':');
	if (cm == std::string::npos || cl == std::string::npos) { SendPacket("E01"); return; }
	uint32_t addr = HexU32(pkt.substr(0, cm)), len = HexU32(pkt.substr(cm + 1, cl - cm - 1));
	auto bytes = FromHex(pkt.substr(cl + 1));
	for (uint32_t i = 0; i < len && i < bytes.size(); ++i) {
		RealPtr rp = sMemoryManager->GetRealAddr(addr + i);
		if (!rp) { SendPacket("E14"); return; } *rp = bytes[i];
	}
	SendPacket("OK");
}
void GdbStub::CmdWriteMemoryBin(const std::string& pkt) {
	auto cm = pkt.find(','); auto cl = pkt.find(':');
	if (cm == std::string::npos || cl == std::string::npos) { SendPacket("E01"); return; }
	uint32_t addr = HexU32(pkt.substr(0, cm)), len = HexU32(pkt.substr(cm + 1, cl - cm - 1));
	if (!len) { SendPacket("OK"); return; }
	std::string bd = pkt.substr(cl + 1); std::vector<uint8_t> dec; dec.reserve(len);
	for (size_t i = 0; i < bd.size(); ++i) {
		if ((uint8_t)bd[i] == 0x7d && i + 1 < bd.size()) dec.push_back((uint8_t)bd[++i] ^ 0x20);
		else dec.push_back((uint8_t)bd[i]);
	}
	uint32_t wl = std::min((uint32_t)dec.size(), len);
	for (uint32_t i = 0; i < wl; ++i) {
		RealPtr rp = sMemoryManager->GetRealAddr(addr + i);
		if (!rp) { SendPacket("E14"); return; } *rp = dec[i];
	}
	SendPacket("OK");
}

//  Execution control 
void GdbStub::CmdContinue(const std::string& pkt) {
	if (!pkt.empty()) WriteReg(UC_ARM_REG_PC, HexU32(pkt));
	{ std::lock_guard<std::mutex> lk(_replyMutex); _stopReplySent = false; }
	_stepThreadId.store(-1);
	_halted.store(false);
	_haltCv.notify_all();
}
void GdbStub::CmdStep(const std::string& pkt) {
	if (!pkt.empty()) WriteReg(UC_ARM_REG_PC, HexU32(pkt));
	{ std::lock_guard<std::mutex> lk(_replyMutex); _stopReplySent = false; }
	// Keep _halted=true so other threads stay blocked.
	// Only the selected thread will be woken with count=1.
	_stepThreadId.store(_selectedTid);
	_haltCv.notify_all();
}

void GdbStub::CmdAddBreakpoint(const std::string& pkt) {
	auto c1 = pkt.find(','); if (c1 == std::string::npos) { SendPacket("E01"); return; }
	auto c2 = pkt.find(',', c1 + 1);
	int type = (int)HexU32(pkt.substr(0, c1));
	uint32_t addr = HexU32(pkt.substr(c1 + 1, c2 != std::string::npos ? c2 - c1 - 1 : std::string::npos));
	if (type == 0 || type == 1) {
		AddHooksForBreakpoint(addr);
		printf("[GDB] BP set at 0x%08X\n", addr);
		SendPacket("OK");
	}
	else SendPacket("");
}
void GdbStub::CmdRemoveBreakpoint(const std::string& pkt) {
	auto c1 = pkt.find(','); if (c1 == std::string::npos) { SendPacket("E01"); return; }
	auto c2 = pkt.find(',', c1 + 1);
	int type = (int)HexU32(pkt.substr(0, c1));
	uint32_t addr = HexU32(pkt.substr(c1 + 1, c2 != std::string::npos ? c2 - c1 - 1 : std::string::npos));
	if (type == 0 || type == 1) {
		RemoveHooksForBreakpoint(addr);
		printf("[GDB] BP removed at 0x%08X\n", addr);
		SendPacket("OK");
	}
	else SendPacket("");
}

void GdbStub::CmdHaltReason() { SendPacket(BuildStopReply(_lastStopReason)); }
void GdbStub::CmdDetach() { SendPacket("OK"); _halted.store(false); _haltCv.notify_all(); _clientConnected.store(false); }
void GdbStub::CmdKill() { _halted.store(false); _haltCv.notify_all(); _clientConnected.store(false); }

std::string GdbStub::BuildStopReply(GdbStopReason reason) const {
	int sig = 5;
	if (reason == GdbStopReason::Fault)sig = 11;
	if (reason == GdbStopReason::UserBreak)sig = 2;
	char b[64]; snprintf(b, sizeof(b), "T%02xthread:%x;", sig, _stoppedTid);
	return std::string(b);
}
