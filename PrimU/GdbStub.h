#pragma once
#include <cstdint>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <condition_variable>
#include <unicorn/unicorn.h>

class Thread;

constexpr uint16_t GDB_DEFAULT_PORT = 2345;
constexpr size_t   GDB_PACKET_BUF   = 0x10000;

// Thread-local: set by breakpoint hook, read by ThreadProc
inline thread_local bool     g_gdbBpHit  = false;
inline thread_local uint32_t g_gdbBpAddr = 0;

enum class GdbStopReason : uint8_t {
    None, Breakpoint, Step, Fault, UserBreak,
};

class GdbStub {
public:
    static GdbStub* GetInstance();

    bool Start(uint16_t port = GDB_DEFAULT_PORT);
    void Stop();
    bool IsConnected() const { return _clientConnected.load(); }
    bool IsEnabled()   const { return _enabled.load(); }

    /// Register thread for debugging. Adds per-breakpoint hooks.
    void RegisterThread(Thread* thread);

    /// Called by ThreadProc BEFORE each uc_emu_start.
    /// Blocks if halted. Returns emu count: 0=free-run, 1=step, -1=exit.
    int WaitIfHalted(int threadId);

    /// Called by ThreadProc AFTER breakpoint hit or step complete.
    void NotifyStop(uc_engine* uc, GdbStopReason reason, uint32_t addr);

    /// Blocks thread 0 at entry until debugger connects.
    void WaitForDebugger();

private:
    GdbStub() = default;
    ~GdbStub();
    GdbStub(const GdbStub&) = delete;
    void operator=(const GdbStub&) = delete;
    static GdbStub* _instance;

    // ── Networking ──
    void AcceptLoop();
    void ClientLoop();
    bool SendRaw(const char* data, size_t len);
    bool SendPacket(const std::string& payload);
    bool RecvPacket(std::string& out);

    // ── Packet handlers ──
    void HandlePacket(const std::string& pkt);
    void HandleQuery(const std::string& pkt);
    void HandleVPacket(const std::string& pkt);
    void HandleHCommand(const std::string& args);
    void CmdReadRegisters();
    void CmdWriteRegisters(const std::string& data);
    void CmdReadRegister(const std::string& pkt);
    void CmdWriteRegister(const std::string& pkt);
    void CmdReadMemory(const std::string& pkt);
    void CmdWriteMemory(const std::string& pkt);
    void CmdWriteMemoryBin(const std::string& pkt);
    void CmdContinue(const std::string& pkt);
    void CmdStep(const std::string& pkt);
    void CmdAddBreakpoint(const std::string& pkt);
    void CmdRemoveBreakpoint(const std::string& pkt);
    void CmdHaltReason();
    void CmdDetach();
    void CmdKill();

    // ── Helpers ──
    uc_engine* GetSelectedEngine() const;
    Thread*    GetThreadById(int id) const;
    void       StopAllEngines();
    uint32_t   ReadReg(int ucReg) const;
    void       WriteReg(int ucReg, uint32_t val);
    std::string BuildStopReply(GdbStopReason reason) const;

    // ── Breakpoint hook management ──
    void AddHooksForBreakpoint(uint32_t addr);
    void RemoveHooksForBreakpoint(uint32_t addr);
    void AddAllBreakpointHooksForEngine(uc_engine* uc);
    static void BpHookCallback(uc_engine* uc, uint64_t addr,
                               uint32_t size, void* user);

    // ── State ──
    std::atomic<bool> _enabled{false};
    std::atomic<bool> _clientConnected{false};
    std::atomic<bool> _shutdownRequested{false};

    // Halt / resume (all-stop mode)
    std::atomic<bool> _halted{false};
    std::mutex _haltMutex;
    std::condition_variable _haltCv;
    std::atomic<int> _stepThreadId{-1}; // thread allowed to step, -1=none

    // Thread tracking: PrimeU thread-id → Thread*
    mutable std::mutex _threadsMutex;
    std::unordered_map<int, Thread*> _registeredThreads;

    // Currently selected thread for register ops (direct PrimeU id)
    int _selectedTid = 0;
    int _stoppedTid  = 0;

    // Per-breakpoint hooks: addr → { per-engine hook handles }
    struct BpEntry {
        std::vector<std::pair<uc_engine*, uc_hook>> hooks;
    };
    mutable std::mutex _bpMutex;
    std::unordered_map<uint32_t, BpEntry> _breakpoints;

    // Stop-reply dedup
    std::mutex _replyMutex;
    bool _stopReplySent = false;

    // Sockets
    uintptr_t _listenSock = ~(uintptr_t)0;
    uintptr_t _clientSock = ~(uintptr_t)0;
    std::thread _acceptThread;
    std::thread _clientThread;

    uint16_t _port = GDB_DEFAULT_PORT;
    GdbStopReason _lastStopReason = GdbStopReason::None;
};

#define sGdbStub GdbStub::GetInstance()
