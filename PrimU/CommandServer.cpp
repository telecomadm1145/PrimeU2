#define NOMINMAX
#include "stdafx.h"
#include "InterruptHandler.h"
#include "CommandServer.h"
#include "LCD.h"
#include "handlers.h"
#include "ui.h"

#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>

#include <WinSock2.h>
#include <WS2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

namespace CommandServer {
    static std::atomic<bool> g_running{false};
    static std::thread g_serverThread;
    static SOCKET g_listenSocket = INVALID_SOCKET;

    // Helper to send all data
    bool SendAll(SOCKET sock, const char* data, int len) {
        int totalSent = 0;
        while (totalSent < len) {
            int sent = send(sock, data + totalSent, len - totalSent, 0);
            if (sent == SOCKET_ERROR) return false;
            totalSent += sent;
        }
        return true;
    }

    void HandleClient(SOCKET clientSocket) {
        char recvBuf[1024];
        std::string inputBuffer;

        while (g_running.load()) {
            int bytesRecv = recv(clientSocket, recvBuf, sizeof(recvBuf) - 1, 0);
            if (bytesRecv <= 0) {
                break; // Connection closed or error
            }
            recvBuf[bytesRecv] = '\0';
            inputBuffer += recvBuf;

            // Process line-by-line
            size_t pos;
            while ((pos = inputBuffer.find('\n')) != std::string::npos) {
                std::string line = inputBuffer.substr(0, pos);
                inputBuffer.erase(0, pos + 1);

                // Trim carriage return if present
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                if (line.empty()) continue;

                std::stringstream ss(line);
                std::string cmd;
                ss >> cmd;

                if (cmd == "key") {
                    unsigned short keycode = 0;
                    std::string action;
                    if (ss >> keycode >> action) {
                        UIMultipressEvent uime{};
                        uime.key_code0 = keycode;

                        if (action == "down") {
                            uime.status = UI_EVENT_TYPE_KEY;
                            EnqueueEvent(uime);
                            SendAll(clientSocket, "OK\n", 3);
                        } else if (action == "up") {
                            uime.status = UI_EVENT_TYPE_KEY_UP;
                            EnqueueEvent(uime);
                            SendAll(clientSocket, "OK\n", 3);
                        } else if (action == "press") {
                            uime.status = UI_EVENT_TYPE_KEY;
                            EnqueueEvent(uime);
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                            uime.status = UI_EVENT_TYPE_KEY_UP;
                            EnqueueEvent(uime);
                            SendAll(clientSocket, "OK\n", 3);
                        } else {
                            SendAll(clientSocket, "ERROR: Invalid action\n", 22);
                        }
                    } else {
                        SendAll(clientSocket, "ERROR: Missing keycode or action\n", 33);
                    }
                } else if (cmd == "touch") {
                    int x = 0, y = 0;
                    std::string action;
                    if (ss >> x >> y >> action) {
                        ui_event_type_e status;
                        bool valid = false;
                        if (action == "down") {
                            status = UI_EVENT_TYPE_TOUCH_BEGIN;
                            valid = true;
                        } else if (action == "move") {
                            status = UI_EVENT_TYPE_TOUCH_MOVE;
                            valid = true;
                        } else if (action == "up") {
                            status = UI_EVENT_TYPE_TOUCH_END;
                            valid = true;
                        }

                        if (valid) {
                            TouchUpdate(x, y, 0, status);
                            SendAll(clientSocket, "OK\n", 3);
                        } else {
                            SendAll(clientSocket, "ERROR: Invalid touch action\n", 28);
                        }
                    } else {
                        SendAll(clientSocket, "ERROR: Missing coordinates or action\n", 37);
                    }
                } else if (cmd == "screenshot") {
                    LCD* lcd = sLCDHandler->GetActiveLCD();
                    if (lcd && lcd->buffer) {
                        int width = lcd->xRes;
                        int height = lcd->yRes;
                        int size = width * height * 4;

                        // Create BMP headers
                        #pragma pack(push, 1)
                        struct BMPHeader {
                            uint16_t bfType{ 0x4D42 };
                            uint32_t bfSize{ 0 };
                            uint16_t bfReserved1{ 0 };
                            uint16_t bfReserved2{ 0 };
                            uint32_t bfOffBits{ 54 };
                        } bmpHeader;

                        struct BMPInfoHeader {
                            uint32_t biSize{ 40 };
                            int32_t  biWidth{ 0 };
                            int32_t  biHeight{ 0 };
                            uint16_t biPlanes{ 1 };
                            uint16_t biBitCount{ 32 };
                            uint32_t biCompression{ 0 };
                            uint32_t biSizeImage{ 0 };
                            int32_t  biXPelsPerMeter{ 0 };
                            int32_t  biYPelsPerMeter{ 0 };
                            uint32_t biClrUsed{ 0 };
                            uint32_t biClrImportant{ 0 };
                        } bmpInfo;
                        #pragma pack(pop)

                        bmpHeader.bfSize = sizeof(BMPHeader) + sizeof(BMPInfoHeader) + size;
                        bmpInfo.biWidth = width;
                        bmpInfo.biHeight = -height; // Top-down BMP
                        bmpInfo.biSizeImage = size;

                        uint32_t totalSize = sizeof(BMPHeader) + sizeof(BMPInfoHeader) + size;

                        // Send total size of BMP first (4 bytes, network byte order/big-endian)
                        uint32_t sizeBE = htonl(totalSize);
                        SendAll(clientSocket, (char*)&sizeBE, 4);

                        // Send headers
                        SendAll(clientSocket, (char*)&bmpHeader, sizeof(bmpHeader));
                        SendAll(clientSocket, (char*)&bmpInfo, sizeof(bmpInfo));

                        // Send pixel data
                        SendAll(clientSocket, (char*)lcd->buffer, size);
                    } else {
                        // Send 0 length if LCD not initialized
                        uint32_t zero = 0;
                        SendAll(clientSocket, (char*)&zero, 4);
                    }
                } else if (cmd == "state") {
                    std::string stateStr = "Simulator running.\n";
                    SendAll(clientSocket, stateStr.c_str(), (int)stateStr.size());
                } else {
                    SendAll(clientSocket, "ERROR: Unknown command\n", 22);
                }
            }
        }
        closesocket(clientSocket);
    }

    void ServerThreadProc(unsigned short port) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "[CommandServer] WSAStartup failed\n";
            return;
        }

        g_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (g_listenSocket == INVALID_SOCKET) {
            std::cerr << "[CommandServer] socket creation failed\n";
            WSACleanup();
            return;
        }

        // Allow reusing port
        char opt = 1;
        setsockopt(g_listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // Only listen locally for security
        serverAddr.sin_port = htons(port);

        if (bind(g_listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "[CommandServer] bind failed\n";
            closesocket(g_listenSocket);
            WSACleanup();
            return;
        }

        if (listen(g_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
            std::cerr << "[CommandServer] listen failed\n";
            closesocket(g_listenSocket);
            WSACleanup();
            return;
        }

        std::cout << "[CommandServer] Listening on 127.0.0.1:" << port << "\n";

        while (g_running.load()) {
            SOCKET clientSocket = accept(g_listenSocket, nullptr, nullptr);
            if (clientSocket == INVALID_SOCKET) {
                if (!g_running.load()) break;
                continue;
            }
            HandleClient(clientSocket);
        }

        closesocket(g_listenSocket);
        g_listenSocket = INVALID_SOCKET;
        WSACleanup();
    }

    bool Start(unsigned short port) {
        if (g_running.load()) return false;
        g_running.store(true);
        g_serverThread = std::thread(ServerThreadProc, port);
        return true;
    }

    void Stop() {
        if (!g_running.load()) return;
        g_running.store(false);
        if (g_listenSocket != INVALID_SOCKET) {
            closesocket(g_listenSocket);
        }
        if (g_serverThread.joinable()) {
            g_serverThread.join();
        }
    }
}
