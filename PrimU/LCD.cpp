// LCD.cpp
// --- Include your project header ---
#include "LCD.h"
#include "ui.h"
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
std::map<int, int> vk_to_device_keymap = {
	// === 标准功能键 ===
	{ VK_ESCAPE,       0x01 }, // Esc
	{ VK_LEFT,         0x02 }, // 左
	{ VK_UP,           0x03 }, // 上
	{ VK_RIGHT,        0x04 }, // 右
	{ VK_DOWN,         0x05 }, // 下
	{ VK_BACK,         0x0C }, // 退格
	{ VK_RETURN,       0x0D }, // 回车
	{ VK_SPACE,        0x20 }, // 空格
	{ VK_SHIFT,        0x8B }, // Shift

	// === 数字与符号键 ===
	{ '0',             0x30 }, // 0
	{ '1',             0x59 }, // 1
	{ '2',             0x5A }, // 2
	{ '3',             0x33 }, // 3
	{ '4',             0x55 }, // 4
	{ '5',             0x56 }, // 5
	{ '6',             0x57 }, // 6
	{ '7',             0x51 }, // 7
	{ '8',             0x52 }, // 8
	{ '9',             0x53 }, // 9
	{ 'X',             0x44 }, // X
	{ VK_OEM_COMMA,    0x4F }, // ,
	{ VK_OEM_2,        0x54 }, // /
	{ VK_MULTIPLY,     0x58 }, // *
	{ VK_OEM_PERIOD,   0xB8 }, // .
	{ VK_OEM_MINUS,	   0xB7 }, // +
	{ VK_OEM_PLUS,     0xB9 }, // -

	// === 特殊功能键 (映射自 QWERTY 布局) ===
	{ 'Q',             0x41 }, // 变量
	{ 'W',             0x42 }, // 工具箱
	{ 'E',             0x43 }, // 数学模板
	{ 'R',             0x45 }, // a b/c
	{ 'T',             0x46 }, // ^
	{ 'Y',             0x47 }, // SIN
	{ 'U',             0x48 }, // COS
	{ 'I',             0x49 }, // TAN
	{ 'O',             0x4A }, // LN
	{ 'P',             0x4B }, // LOG
	{ 'A',             0x4C }, // ^2
	{ 'S',             0x4D }, // +/-
	{ 'D',             0x4E }, // ()
	{ 'F',             0x50 }, // 1e
	{ 'G',             0x83 }, // ON
	{ 'H',             0x91 }, // 符号视图
	{ 'J',             0x93 }, // 消息
	{ 'K',             0xB2 }, // 绘图
	{ 'L',             0xB3 }, // 数值
	{ 'Z',             0xB4 }, // 视图
	{ 'C',             0xB5 }, // CAS
	{ 'V',             0xB6 }, // Alpha
	{ 'N',             0x95 }, // 帮助
	{ 'M',             0xB1 }, // 应用
};

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
std::wstring g_hexInputString;
// This function handles messages for the window.
// 此函数处理窗口消息。
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	LCD* lcd = (LCD*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

	static bool is_lbtn_down = false;

	switch (msg) {
	case WM_CREATE: {
		// Retrieve the LCD pointer passed during CreateWindowEx and store it
		// 检索在 CreateWindowEx 期间传递的 LCD 指针并存储它
		CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
		lcd = (LCD*)pCreate->lpCreateParams;
		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)lcd);
		return 0;
	}

	case WM_TIMER: {
		// Trigger a repaint to update the screen
		// 触发重绘以更新屏幕
		InvalidateRect(hwnd, NULL, FALSE);
		return 0;
	}

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);

		// Draw the LCD buffer first if it exists
		// 如果 LCD 缓冲区存在，首先绘制它
		if (lcd && lcd->buffer) {
			uint32_t* tempBuffer = new uint32_t[lcd->xRes * lcd->yRes];
			for (int i = 0; i < lcd->xRes * lcd->yRes; i++) {
				uint16_t rgb555 = lcd->buffer[i];
				uint8_t r = (rgb555 >> 10) & 0x1F;
				uint8_t g = (rgb555 >> 5) & 0x1F;
				uint8_t b = rgb555 & 0x1F;
				r = (r << 3) | (r >> 2);
				g = (g << 3) | (g >> 2);
				b = (b << 3) | (b >> 2);
				tempBuffer[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
			}
			BITMAPINFO bi = { 0 };
			bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bi.bmiHeader.biWidth = lcd->xRes;
			bi.bmiHeader.biHeight = -lcd->yRes;
			bi.bmiHeader.biPlanes = 1;
			bi.bmiHeader.biBitCount = 32;
			bi.bmiHeader.biCompression = BI_RGB;
			StretchDIBits(hdc, 0, 0, lcd->xRes, lcd->yRes, 0, 0, lcd->xRes, lcd->yRes, tempBuffer, &bi, DIB_RGB_COLORS, SRCCOPY);
			delete[] tempBuffer;
		}

		//// Display the hexadecimal input string
		//// 显示十六进制输入字符串
		//SetTextColor(hdc, RGB(255, 255, 0));
		//SetBkMode(hdc, TRANSPARENT);
		//std::wstring displayText = L"Hex Input: " + g_hexInputString;
		//RECT clientRect;
		//GetClientRect(hwnd, &clientRect);
		//clientRect.left += 10;
		//clientRect.top += 10;
		//DrawTextW(hdc, displayText.c_str(), -1, &clientRect, DT_TOP | DT_LEFT | DT_SINGLELINE);

		EndPaint(hwnd, &ps);
		return 0;
	}

				 //			 // Handles printable characters for hex input
				 //			 // 处理用于十六进制输入的可打印字符
				 //case WM_CHAR: {
				 //	wchar_t inputChar = (wchar_t)wParam;
				 //	// Only accept valid hexadecimal characters
				 //	// 只接受有效的十六进制字符
				 //	if (iswxdigit(inputChar)) {
				 //		g_hexInputString += towlower(inputChar);
				 //		InvalidateRect(hwnd, NULL, FALSE);
				 //	}
				 //	return 0;
				 //}

				 //			// --- NEW/MODIFIED: Handles non-printable keys like Enter and Backspace ---
				 //			// --- 新增/修改: 处理像回车和退格这样的非打印按键 ---
				 //case WM_KEYDOWN: {
				 //	switch (wParam) {
				 //	case VK_RETURN: { // Enter key pressed
				 //		if (!g_hexInputString.empty()) {
				 //			try {
				 //				// Convert hex string to an unsigned integer
				 //				// 将十六进制字符串转换为无符号整数
				 //				unsigned long value = std::stoul(g_hexInputString, nullptr, 16);

				 //				// Create an event with the parsed value and send it
				 //				// 创建带有解析值的事件并发送
				 //				UIMultipressEvent uime{};
				 //				uime.key_code0 = static_cast<uint32_t>(value); // Cast to the type of key_code0
				 //				uime.type = UI_EVENT_TYPE_KEY;
				 //				EnqueueEvent(uime);

				 //				// Clear the input string for the next entry
				 //				// 清空输入字符串以备下次输入
				 //				g_hexInputString.clear();
				 //			}
				 //			catch (const std::invalid_argument&) {
				 //				// Handle cases where the string is not a valid hex number (e.g., "0xgg")
				 //				// For simplicity, just clear the invalid input.
				 //				g_hexInputString.clear();
				 //			}
				 //			catch (const std::out_of_range&) {
				 //				// Handle cases where the number is too large to fit
				 //				// For simplicity, just clear the invalid input.
				 //				g_hexInputString.clear();
				 //			}

				 //			// Trigger a repaint to show the cleared input field
				 //			// 触发重绘以显示已清空的输入字段
				 //			InvalidateRect(hwnd, NULL, FALSE);
				 //		}
				 //		return 0; // We handled the message
				 //	}

				 //	case VK_BACK: { // Backspace key pressed
				 //		if (!g_hexInputString.empty()) {
				 //			g_hexInputString.pop_back();
				 //			InvalidateRect(hwnd, NULL, FALSE);
				 //		}
				 //		return 0; // We handled the message
				 //	}
				 //	}
				 //	break; // Let other keys be handled by DefWindowProc
				 //}
	case WM_KEYDOWN: {
		static std::map<int, int> numpad_mapping = ([]() {
			std::map<int, int> vk_to_device_keymap;
			// --- 数字键 ---
			vk_to_device_keymap[VK_NUMPAD0] = '0';
			vk_to_device_keymap[VK_NUMPAD1] = '1';
			vk_to_device_keymap[VK_NUMPAD2] = '2';
			vk_to_device_keymap[VK_NUMPAD3] = '3';
			vk_to_device_keymap[VK_NUMPAD4] = '4';
			vk_to_device_keymap[VK_NUMPAD5] = '5';
			vk_to_device_keymap[VK_NUMPAD6] = '6';
			vk_to_device_keymap[VK_NUMPAD7] = '7';
			vk_to_device_keymap[VK_NUMPAD8] = '8';
			vk_to_device_keymap[VK_NUMPAD9] = '9';
			// --- 运算符 ---
			//vk_to_device_keymap[VK_MULTIPLY] = '*';
			/*
				{ VK_OEM_COMMA,    0x4F }, // ,
	{ VK_OEM_2,        0x54 }, // /
	{ VK_MULTIPLY,     0x58 }, // *
	{ VK_OEM_PERIOD,   0xB8 }, // .
	{ VK_OEM_MINUS,	   0xB7 }, // +
	{ VK_OEM_PLUS,     0xB9 }, // -
			*/
			vk_to_device_keymap[107] = VK_OEM_PLUS;
			vk_to_device_keymap[109] = VK_OEM_MINUS;
			vk_to_device_keymap[111] = VK_OEM_2;
			vk_to_device_keymap[110] = VK_OEM_PERIOD;
			return vk_to_device_keymap;
			})();
		auto it0 = numpad_mapping.find(static_cast<int>(wParam));
		if (it0 != numpad_mapping.end()) {
			wParam = it0->second; // Use the mapped value for numpad keys
		}
		auto it = vk_to_device_keymap.find(static_cast<int>(wParam));
		if (it != vk_to_device_keymap.end()) {
			UIMultipressEvent uime{};
			uime.key_code0 = it->second; // Use the mapped value
			uime.type = UI_EVENT_TYPE_KEY;
			EnqueueEvent(uime);
			return 0; // We handled the message
		}
		else {
			printf("[KBD] Unhandled key: %d\n", wParam);
		}
		break; // Let other keys be handled by DefWindowProc
	}
	case WM_KEYUP: {
		static std::map<int, int> numpad_mapping = ([]() {
			std::map<int, int> vk_to_device_keymap;
			// --- 数字键 ---
			vk_to_device_keymap[VK_NUMPAD0] = '0';
			vk_to_device_keymap[VK_NUMPAD1] = '1';
			vk_to_device_keymap[VK_NUMPAD2] = '2';
			vk_to_device_keymap[VK_NUMPAD3] = '3';
			vk_to_device_keymap[VK_NUMPAD4] = '4';
			vk_to_device_keymap[VK_NUMPAD5] = '5';
			vk_to_device_keymap[VK_NUMPAD6] = '6';
			vk_to_device_keymap[VK_NUMPAD7] = '7';
			vk_to_device_keymap[VK_NUMPAD8] = '8';
			vk_to_device_keymap[VK_NUMPAD9] = '9';
			// --- 运算符 ---
			//vk_to_device_keymap[VK_MULTIPLY] = '*';
			/*
				{ VK_OEM_COMMA,    0x4F }, // ,
	{ VK_OEM_2,        0x54 }, // /
	{ VK_MULTIPLY,     0x58 }, // *
	{ VK_OEM_PERIOD,   0xB8 }, // .
	{ VK_OEM_MINUS,	   0xB7 }, // +
	{ VK_OEM_PLUS,     0xB9 }, // -
			*/
			vk_to_device_keymap[107] = VK_OEM_PLUS;
			vk_to_device_keymap[109] = VK_OEM_MINUS;
			vk_to_device_keymap[111] = VK_OEM_2;
			vk_to_device_keymap[110] = VK_OEM_PERIOD;
			return vk_to_device_keymap;
			})();
		auto it0 = numpad_mapping.find(static_cast<int>(wParam));
		if (it0 != numpad_mapping.end()) {
			wParam = it0->second; // Use the mapped value for numpad keys
		}
		auto it = vk_to_device_keymap.find(static_cast<int>(wParam));
		if (it != vk_to_device_keymap.end()) {
			UIMultipressEvent uime{};
			uime.key_code0 = it->second; // Use the mapped value
			uime.type = UI_EVENT_TYPE_KEY_UP;
			EnqueueEvent(uime);
			return 0; // We handled the message
		}
		else {
			printf("[KBD] Unhandled key: %d\n", wParam);
		}
		break; // Let other keys be handled by DefWindowProc
	}
	case WM_CLOSE: {
		DestroyWindow(hwnd);
		return 0;
	}
	case WM_LBUTTONDOWN: {
		{
			auto x = LOWORD(lParam);
			auto y = HIWORD(lParam);
			UIMultipressEvent uime{};
			uime.touch_x = static_cast<uint16_t>(x);
			uime.touch_y = static_cast<uint16_t>(y);
			uime.type = UI_EVENT_TYPE_TOUCH_BEGIN;
			EnqueueEvent(uime);
		}
		is_lbtn_down = true; break;
	}
	case WM_LBUTTONUP: {
		{
			auto x = LOWORD(lParam);
			auto y = HIWORD(lParam);
			UIMultipressEvent uime{};
			uime.touch_x = static_cast<uint16_t>(x);
			uime.touch_y = static_cast<uint16_t>(y);
			uime.type = UI_EVENT_TYPE_TOUCH_END;
			EnqueueEvent(uime);
		}
		is_lbtn_down = false; break;
	}
	case WM_MOUSEMOVE: {
		if (is_lbtn_down) {
			auto x = LOWORD(lParam);
			auto y = HIWORD(lParam);
			UIMultipressEvent uime{};
			uime.touch_x = static_cast<uint16_t>(x);
			uime.touch_y = static_cast<uint16_t>(y);
			uime.type = UI_EVENT_TYPE_TOUCH_MOVE;
			//EnqueueEvent(uime);
		}
		break;
	}
	case WM_DESTROY: {
		PostQuitMessage(0);
		return 0;
	}
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}