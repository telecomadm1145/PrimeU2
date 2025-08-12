// LCD.cpp
// --- Include your project header ---
#include "LCD.h"
// --- Standard Library and Win32 Headers ---
#include <windows.h>
#include <thread>
#include <atomic>
#include <map>
#include <mutex>
#include <chrono>


// --- Globals for Window Management ---
// Since we cannot modify the LCD struct, we use a global map to associate
// an LCD instance with its window thread and handle. A mutex ensures thread safety.
struct WindowInfo {
    std::thread windowThread;
    HWND windowHandle = nullptr;
    std::atomic<bool> isExiting = false;
};

static std::map<LCD*, WindowInfo> g_LcdWindowMap;
static std::mutex g_LcdWindowMapMutex;

// Forward declarations for the window procedure and thread function
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void WindowThreadProc(LCD* lcd);


// --- LCDHandler Implementation (from your code) ---

LCDHandler* LCDHandler::_instance = nullptr;

LCDHandler::LCDHandler() {
    InitActiveLCD();
}

LCDHandler::~LCDHandler() {
    if (_activeLCD) {
        _activeLCD->~LCD();
    }
    DeleteActiveLCD();
    // In a real singleton, you wouldn't delete the instance this way,
    // but we'll stick to the original code's structure.
    delete _instance;
    _instance = nullptr;
}

void LCDHandler::InitActiveLCD() {
    ErrorCode err = ERROR_OK;
    VirtPtr lcd;

    if ((err = sMemoryManager->DyanmicAlloc(&lcd, sizeof(LCD))) != ERROR_OK)
        __debugbreak();

    _activeLCD = reinterpret_cast<LCD*>(sMemoryManager->GetRealAddr(lcd));
    new (_activeLCD) LCD(); // Placement new

    if ((err = sMemoryManager->DyanmicAlloc(&_activeLCDPtr, 0x4)) != ERROR_OK)
        __debugbreak();

    *__GET(uint32_t*, _activeLCDPtr) = reinterpret_cast<uint32_t>(_activeLCD->LCDMagicPtr);
}

void LCDHandler::DeleteActiveLCD() {
    if (_activeLCD) {
        sMemoryManager->DynamicFree(sMemoryManager->GetVirtualAddr(reinterpret_cast<RealPtr>(_activeLCD)));
        _activeLCD = nullptr;
    }
    if (_activeLCDPtr) {
        sMemoryManager->DynamicFree(_activeLCDPtr);
        _activeLCDPtr = 0;
    }
}

VirtPtr LCDHandler::GetActiveLCDPtr() const {
    return _activeLCDPtr;
}

// --- LCD Constructor & Destructor Implementation ---

// The constructor now launches the window thread.
LCD::LCD() {
    // Original initializations
    xRes = 320;
    yRes = 240;
    LcdMagic.SomeVal = 0x5850;
    LcdMagic.x_res = 320;
    LcdMagic.y_res = 240;
    LcdMagic.pixel_bits = 32; // Using 32-bit color for easier rendering with Win32
    LcdMagic.unk2_640 = 640;
    LcdMagic.unk0_2 = 2;
    LcdMagic.unk1_0 = 8;
    LcdMagic.window1_bufferstart = sMemoryManager->GetVirtualAddr(reinterpret_cast<RealPtr>(&buffer));

    LCDMagicPtr = reinterpret_cast<LCD_MAGIC*>(sMemoryManager->GetVirtualAddr(reinterpret_cast<RealPtr>(&LcdMagic)));
    itself = reinterpret_cast<LCD*>(sMemoryManager->GetVirtualAddr(reinterpret_cast<RealPtr>(this)));

    // Initialize buffer to black (ARGB format)
    for (int i = 0; i < 320 * 240; i++) {
        buffer[i] = 0xFF000000;
    }

    // --- Launch Window Thread ---
    {
        std::lock_guard<std::mutex> lock(g_LcdWindowMapMutex);
        // Create a new entry in the map and launch the thread
        g_LcdWindowMap[this].isExiting = false;
        g_LcdWindowMap[this].windowThread = std::thread(WindowThreadProc, this);
    }

    // Wait for the window to be created by the new thread
    HWND hwnd = nullptr;
    while (hwnd == nullptr) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::lock_guard<std::mutex> lock(g_LcdWindowMapMutex);
        auto it = g_LcdWindowMap.find(this);
        if (it != g_LcdWindowMap.end()) {
            hwnd = it->second.windowHandle;
        }
    }
}

// The destructor now safely closes the window and joins the thread.
LCD::~LCD() {
    std::thread deadThread;
    HWND hwndToClose = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_LcdWindowMapMutex);
        auto it = g_LcdWindowMap.find(this);
        if (it != g_LcdWindowMap.end()) {
            // Signal the thread to exit
            it->second.isExiting = true;
            hwndToClose = it->second.windowHandle;

            // Move the thread handle out of the map so we can join it outside the lock
            deadThread = std::move(it->second.windowThread);

            g_LcdWindowMap.erase(it);
        }
    }

    // Post a message to the window to unblock GetMessage() and close it
    if (hwndToClose) {
        PostMessage(hwndToClose, WM_CLOSE, 0, 0);
    }

    // Wait for the thread to finish
    if (deadThread.joinable()) {
        deadThread.join();
    }
}

// --- Window Thread and Procedure Functions ---

// This function runs on its own thread to manage the window.
void WindowThreadProc(LCD* lcd) {
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"EmulatedLcdClass";
    if (!RegisterClassEx(&wc)) return;

    // Adjust window size to account for title bar and borders
    RECT wr = { 0, 0, (LONG)lcd->xRes, (LONG)lcd->yRes };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowEx(
        0,
        L"EmulatedLcdClass",
        L"LCD Display",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
        NULL, NULL, GetModuleHandle(NULL),
        lcd // Pass the LCD pointer to WM_CREATE
    );

    if (!hwnd) return;

    // Store the handle in the global map
    {
        std::lock_guard<std::mutex> lock(g_LcdWindowMapMutex);
        g_LcdWindowMap[lcd].windowHandle = hwnd;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Set a timer to refresh the display at ~60 FPS
    SetTimer(hwnd, 1, 1000 / 60, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    KillTimer(hwnd, 1);
}

// This function handles messages for the window.
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LCD* lcd = (LCD*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        // Retrieve the LCD pointer passed during CreateWindowEx and store it
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        lcd = (LCD*)pCreate->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)lcd);
        return 0;
    }

    case WM_TIMER: {
        // Trigger a repaint to update the screen
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_PAINT: {
        if (!lcd) break;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // 创建临时 32 位缓冲区用于转换
        uint32_t* tempBuffer = new uint32_t[lcd->xRes * lcd->yRes];

        // 将 RGB555 转换为 ARGB8888
        for (int i = 0; i < lcd->xRes * lcd->yRes; i++) {
            uint16_t rgb555 = lcd->buffer[i];
            // 提取 RGB 分量 (5-5-5 格式)
            uint8_t r = (rgb555 >> 10) & 0x1F;  // 高5位是红色
            uint8_t g = (rgb555 >> 5) & 0x1F;   // 中间5位是绿色
            uint8_t b = rgb555 & 0x1F;          // 低5位是蓝色

            // 将5位扩展到8位 (左移3位)
            r = (r << 3) | (r >> 2);
            g = (g << 3) | (g >> 2);
            b = (b << 3) | (b >> 2);

            // 组合成32位颜色 (0xAARRGGBB)
            tempBuffer[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }


        BITMAPINFO bi = { 0 };
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = lcd->xRes;
        bi.bmiHeader.biHeight = -lcd->yRes; // Negative for top-down DIB
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        // Blit the pixel buffer to the window
        StretchDIBits(hdc,
            0, 0, lcd->xRes, lcd->yRes,
            0, 0, lcd->xRes, lcd->yRes,
            tempBuffer, &bi, DIB_RGB_COLORS, SRCCOPY);

        delete[] tempBuffer;  // 释放临时缓冲区
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CLOSE: {
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_DESTROY: {
        // End the message loop
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}