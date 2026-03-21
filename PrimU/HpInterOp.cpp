#include "Thread.h"
#include "MemoryManager.h"

#include <Windows.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <algorithm>

// 建议：放在某个 .cpp 做定义；头文件只 extern 声明
extern uint32_t usb_in_cb;                     // guest: void usb_in(uint8_t* buf, uint32_t len) 之类
extern void(*usb_out_cb)(void* dat, size_t sz);// host: guest->host OUT 时调用这个
void dump_hex(const void* data, size_t size)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    const size_t bytes_per_line = 16;

    for (size_t offset = 0; offset < size; offset += bytes_per_line)
    {
        // 1) 打印偏移地址
        printf("%08zx  ", offset);

        // 2) 打印十六进制字节（分两组，每组8字节）
        for (size_t i = 0; i < bytes_per_line; ++i)
        {
            if (i == 8) printf(" ");               // 中间多一个空格分隔

            if (offset + i < size)
                printf("%02x ", bytes[offset + i]);
            else
                printf("   ");                     // 不足的位置用空格填充
        }

        // 3) 打印 ASCII 可见字符
        printf(" |");
        for (size_t i = 0; i < bytes_per_line && (offset + i) < size; ++i)
        {
            uint8_t ch = bytes[offset + i];
            printf("%c", isprint(ch) ? ch : '.');
        }
        printf("|\n");
    }
}
#define TRANSPARENT

namespace {

    struct HpInterOp;
    static HpInterOp* g_hp = nullptr;

    struct HpInterOp
    {
        std::thread worker;
        HANDLE pipe = INVALID_HANDLE_VALUE;

        Thread* guestThread;
        VirtPtr usb_buf = 0; // guest内存地址(至少64字节)

        std::atomic_bool stop{ false };
        std::atomic_bool connected{ false };
        std::mutex io_mtx; // 保护 pipe 写 + 重连

        // guest->pipe 重组缓存（基于 seq 与 “最后一包<63” 结束）
        std::mutex assemble_mtx;
        std::vector<uint8_t> assemble;
        uint8_t lastSeq = 0xFF;

        HpInterOp()
        {
            g_hp = this;

            pipe = CreateNamedPipeA(
#ifdef TRANSPARENT
                "\\\\.\\pipe\\primu",
#else
                "\\\\.\\pipe\\hp89",
#endif 
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                1,
                0x4000, 0x4000,
                100, // ms
                nullptr
            );

            if (pipe == INVALID_HANDLE_VALUE) {
                std::cout << "CreateNamedPipe failed: " << GetLastError() << "\n";
            }

            // guest OUT -> host：让 guest 调 usb_out_cb 时，进到这里
            usb_out_cb = &HpInterOp::usb_in;

            worker = std::thread(&HpInterOp::handle, this);
        }

        ~HpInterOp()
        {
            stop = true;

            {
                std::lock_guard<std::mutex> lk(io_mtx);
                if (pipe != INVALID_HANDLE_VALUE) {
                    DisconnectNamedPipe(pipe);
                    CloseHandle(pipe);
                    pipe = INVALID_HANDLE_VALUE;
                }
            }

            if (worker.joinable()) worker.join();
            g_hp = nullptr;
        }

        // ========== guest -> pipe ==========
        // guest 调用 usb_out_cb(dat, sz) 时会进来
        static void usb_in(void* dat, size_t sz)
        {
            if (!g_hp) return;
            g_hp->fromGuestUsbPacket(dat, sz);
        }

        void fromGuestUsbPacket(void* dat, size_t sz)
        {
            printf("from Device\n");
            dump_hex(dat, sz);
            if (!dat || sz == 0) return;
#ifdef TRANSPARENT
            writePipe(dat,sz);
#else
            writePipe((char*)dat + 1, sz - 1);
#endif // TRANSPARENT
            //const uint8_t* p = reinterpret_cast<const uint8_t*>(dat);
            //uint8_t seq = p[0];
            //const uint8_t* payload = (sz > 1) ? (p + 1) : nullptr;
            //size_t payLen = (sz > 1) ? (sz - 1) : 0;

            //{
            //    std::lock_guard<std::mutex> lk(assemble_mtx);

            //    // seq==0 视为新消息开始（简单策略）
            //    if (seq == 0 || lastSeq == 0xFF || uint8_t(lastSeq + 1) != seq) {
            //        assemble.clear();
            //    }
            //    lastSeq = seq;

            //    if (payload && payLen) {
            //        assemble.insert(assemble.end(), payload, payload + payLen);
            //    }

            //    // 结束条件：最后一包 payload < 63
            //    if (payLen < 63) {
            //        // 一次性写到 pipe
            //        writePipe(assemble.data(), assemble.size());
            //        assemble.clear();
            //        lastSeq = 0xFF;
            //    }
            //}
        }

        bool writePipe(const void* data, size_t len)
        {
            if (!data || len == 0) return true;

            std::lock_guard<std::mutex> lk(io_mtx);

            if (pipe == INVALID_HANDLE_VALUE || !connected.load()) {
                // 未连接时丢弃/或你也可以改成缓存等待连接
                return false;
            }

            DWORD written = 0;
            BOOL ok = WriteFile(pipe, data, (DWORD)len, &written, nullptr);
            if (!ok) {
                DWORD e = GetLastError();
                if (e == ERROR_BROKEN_PIPE || e == ERROR_NO_DATA) {
                    connected = false;
                    DisconnectNamedPipe(pipe);
                }
                return false;
            }
            return written == len;
        }

        // ========== pipe -> guest ==========
        void usb_out(void* dat, size_t sz)
        {
            if (!dat || sz == 0) return;
            if (!usb_in_cb) return;

            if (!usb_buf) {
                // 分配 64 字节 guest buffer
                sMemoryManager->DyanmicAlloc(&usb_buf, 64);
            }
#ifdef TRANSPARENT
            memcpy(__GET(char*, usb_buf), dat, 64);
            printf("from Host\n");
            dump_hex(dat,64);
            // 调 guest 回调：参数按 (bufPtr, len) 传入
            // ExecuteCustomCode 的真实签名你项目里是什么就换成什么
            if (!guestThread)
                guestThread = new Thread(0, 0, 0, 0x1000);
            guestThread->ExecuteCustomCode(usb_in_cb, usb_buf, 64);
            return;
#endif // TRANSPARENT


            const uint8_t* src = reinterpret_cast<const uint8_t*>(dat);
            uint8_t seq = 0;

            size_t off = 0;
            while (off < sz) {
                size_t chunk = std::min<size_t>(63, sz - off);

                uint8_t pkt[64]; // todo
                pkt[0] = seq++;
                std::memcpy(pkt + 1, src + off, chunk);

                // 可选：尾部清零，避免 guest 读到脏数据
                if (chunk < 63) {
                    std::memset(pkt + 1 + chunk, 0, 63 - chunk);
                }

                size_t pktLen = 1 + chunk;

                // 写入 guest 内存：用你项目里实际的“写 guest 内存”接口替换这里
                // 例：sMemoryManager->WriteGuestMemory(usb_buf, pkt, pktLen);
                memcpy(__GET(char*,usb_buf), pkt, pktLen); // <- 如果没有这个函数，请替换成你已有的
                printf("from Host\n");
                dump_hex(pkt, pktLen);
                // 调 guest 回调：参数按 (bufPtr, len) 传入
                // ExecuteCustomCode 的真实签名你项目里是什么就换成什么
                if (!guestThread)
                    guestThread = new Thread(0, 0, 0, 0x1000);
                guestThread->ExecuteCustomCode(usb_in_cb, usb_buf, pktLen);

                off += chunk;
            }
        }

        void handle()
        {
            if (pipe == INVALID_HANDLE_VALUE) return;

            std::vector<uint8_t> buf(0x1000);

            while (!stop.load()) {

                // 连接阶段（阻塞式等待客户端）
                if (!connected.load()) {
                    BOOL ok = ConnectNamedPipe(pipe, nullptr);
                    if (ok) {
                        connected = true;
                    }
                    else {
                        DWORD e = GetLastError();
                        if (e == ERROR_PIPE_CONNECTED) {
                            connected = true;
                        }
                        else {
                            // 其他错误：稍等重试
                            // std::cout << "ConnectNamedPipe error: " << e << "\n";
                            Sleep(10);
                            continue;
                        }
                    }
                }

                // 有连接后读取数据
                DWORD bytesAvail = 0;
                if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &bytesAvail, nullptr)) {
                    DWORD e = GetLastError();
                    if (e == ERROR_BROKEN_PIPE) {
                        connected = false;
                        DisconnectNamedPipe(pipe);
                    }
                    Sleep(1);
                    continue;
                }

                if (bytesAvail == 0) {
                    Sleep(1);
                    continue;
                }

                if (bytesAvail > buf.size()) buf.resize(bytesAvail);

                DWORD bytesRead = 0;
                if (!ReadFile(pipe, buf.data(), (DWORD)buf.size(), &bytesRead, nullptr)) {
                    DWORD e = GetLastError();
                    if (e == ERROR_BROKEN_PIPE || e == ERROR_NO_DATA) {
                        connected = false;
                        DisconnectNamedPipe(pipe);
                    }
                    Sleep(1);
                    continue;
                }

                if (bytesRead > 0) {
                    usb_out(buf.data(), bytesRead);
                }
            }
        }
    };

    // 全局实例
    HpInterOp _g;

} // namespace