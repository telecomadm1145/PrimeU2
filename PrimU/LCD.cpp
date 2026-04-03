// LCD.cpp
// --- Include your project header ---
#include "LCD.h"
#include "svclogwin.h"
#include "ui.h"

// --- Standard Library and Win32 Headers ---
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <windows.h>


#include "PrimeObj.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "imgui_internal.h"
#include "imgui_memory_editor.h"
#include <SDL.h>
#include <functional>
#include <array>
// --- Globals for Window Management ---
// Since we cannot modify the LCD struct, we use a global map to associate
// an LCD instance with its window thread and handle. A mutex ensures thread
// safety.
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
	{VK_ESCAPE, 0x01}, // Esc
	{VK_LEFT, 0x02},   // 左
	{VK_UP, 0x03},     // 上
	{VK_RIGHT, 0x04},  // 右
	{VK_DOWN, 0x05},   // 下
	{VK_BACK, 0x0C},   // 退格
	{VK_RETURN, 0x0D}, // 回车
	{VK_SPACE, 0x20},  // 空格
	{VK_SHIFT, 0x8B},  // Shift

	// === 数字与符号键 ===
	{'0', 0x30},           // 0
	{'1', 0x59},           // 1
	{'2', 0x5A},           // 2
	{'3', 0x33},           // 3
	{'4', 0x55},           // 4
	{'5', 0x56},           // 5
	{'6', 0x57},           // 6
	{'7', 0x51},           // 7
	{'8', 0x52},           // 8
	{'9', 0x53},           // 9
	{'X', 0x44},           // X
	{VK_OEM_COMMA, 0x4F},  // ,
	{VK_OEM_2, 0x54},      // /
	{VK_MULTIPLY, 0x58},   // *
	{VK_OEM_PERIOD, 0xB8}, // .
	{VK_OEM_MINUS, 0xB7},  // +
	{VK_OEM_PLUS, 0xB9},   // -

	// === 特殊功能键 (映射自 QWERTY 布局) ===
	{'Q', 0x41},   // 变量
	{'W', 0x42},   // 工具箱
	{'E', 0x43},   // 数学模板
	{'R', 0x45},   // a b/c
	{'T', 0x46},   // ^
	{'Y', 0x47},   // SIN
	{'U', 0x48},   // COS
	{'I', 0x49},   // TAN
	{'O', 0x4A},   // LN
	{'P', 0x4B},   // LOG
	{'A', 0x4C},   // ^2
	{'S', 0x4D},   // +/-
	{'D', 0x4E},   // ()
	{'F', 0x50},   // 1e
	{'G', 0x83},   // ON
	{'H', 0x91},   // 符号视图
	{'J', 0x93},   // 消息
	{'K', 0xB2},   // 绘图
	{'L', 0xB3},   // 数值
	{'Z', 0xB4},   // 视图
	{'C', 0xB5},   // CAS
	{'V', 0xB6},   // Alpha
	{'N', 0x95},   // 帮助
	{'M', 0xB1},   // 应用
	{'B', 0xe047}, // 主屏幕
};
struct KeyMapping
{
	const char* name;
	float x;
	float y;
	float width;
	float height;
	uint16_t deviceKeyCode;
};
constexpr KeyMapping mapping[] =
{
	{ "Apps", 2.5, 39.5, 14, 4.5, 0xB1 },
	{ "Home",  2.5,  46.0,  14,  4.5,  0xe047 },
	{ "Symb",  18.0,  39.5,  14,  4.5,  0x91 },
	{ "Plot",  18.0,  44.5,  14,  4.5,  0xB2 },
	{ "Num",  18.0,  49.5,  14,  4.5,  0xB3 },
	{ "Up",  46.0,  39.0,  8,  5.0,  0x03 },
	{ "Down",  46.0,  50.0,  8,  5.0,  0x05 },
	{ "Left",  37.0,  44.0,  8,  6.0,  0x02 },
	{ "Right",  55.0,  44.0,  8,  6.0,  0x04 },
	{ "Help",  68.0,  39.5,  14,  4.5,  0x95 },
	{ "View",  68.0,  44.5,  14,  4.5,  0xB4 },
	{ "Menu",  68.0,  49.5,  14,  4.5,  0x93 },
	{ "Esc",  84.0,  39.5,  14,  4.5,  0x01 },
	{ "CAS",  84.0,  46.0,  14,  4.5,  0xB5 },

	{ "Vars",  2.5,  54.5,  14,  5.5,  0x41 },
	{ "Toolbox",  18.5,  54.5,  14,  5.5,  0x42 },
	{ "Math",  34.5,  54.5,  14,  5.5,  0x43 },
	{ "xt_n",  50.5,  54.5,  14,  5.5,  0x44 },
	{ "a_b/c",  66.5,  54.5,  14,  5.5,  0x45 },
	{ "Del",  83.5,  54.5,  14,  5.5,  0x0C },

	{ "x^y",  2.5,  61.0,  14,  5.5,  0x46 },
	{ "SIN",  18.5,  61.0,  14,  5.5,  0x47 },
	{ "COS",  34.5,  61.0,  14,  5.5,  0x48 },
	{ "TAN",  50.5,  61.0,  14,  5.5,  0x49 },
	{ "LN",  66.5,  61.0,  14,  5.5,  0x4A },
	{ "LOG",  83.5,  61.0,  14,  5.5,  0x4B },

	{ "x^2",  2.5,  67.5,  14,  5.5,  0x4C },
	{ "+/-",  18.5,  67.5,  14,  5.5,  0x4D },
	{ "()",  34.5,  67.5,  14,  5.5,  0x4E },
	{ ",",  50.5,  67.5,  14,  5.5,  0x4F },
	{ "Enter",  66.5,  67.5,  31,  5.5,  0x0D },

	{ "EEX",  2.5,  74.0,  14,  5.5,  0x50 },
	{ "7",  18.5,  74.0,  20,  5.5,  0x51 },
	{ "8",  40.5,  74.0,  20,  5.5,  0x52 },
	{ "9",  62.5,  74.0,  20,  5.5,  0x53 },
	{ "/",  84.5,  74.0,  13,  5.5,  0x54 },

	{ "Alpha",  2.5,  80.5,  14,  5.5,  0xB6 },
	{ "4",  18.5,  80.5,  20,  5.5,  0x55 },
	{ "5",  40.5,  80.5,  20,  5.5,  0x56 },
	{ "6",  62.5,  80.5,  20,  5.5,  0x57 },
	{ "*",  84.5,  80.5,  13,  5.5,  0x58 },

	{ "Shift",  2.5,  87.0,  14,  5.5,  0x8B },
	{ "1",  18.5,  87.0,  20,  5.5,  0x59 },
	{ "2",  40.5,  87.0,  20,  5.5,  0x5A },
	{ "3",  62.5,  87.0,  20,  5.5,  0x33 },
	{ "-",  84.5,  87.0,  13,  5.5,  0xB9 },

	{ "On",  2.5,  93.5,  14,  5.5,  0x83 },
	{ "0",  18.5,  93.5,  20,  5.5,  0x30 },
	{ ".",  40.5,  93.5,  20,  5.5,  0xB8 },
	{ "Space",  62.5,  93.5,  20,  5.5,  0x20 },
	{ "+",  84.5,  93.5,  13,  5.5,  0xB7 }
};

// --- LCDHandler Implementation (from your code) ---

LCDHandler* LCDHandler::_instance = nullptr;

LCDHandler::LCDHandler() { InitActiveLCD(); }

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

	*__GET(uint32_t*, _activeLCDPtr) =
		reinterpret_cast<uint32_t>(_activeLCD->LCDMagicPtr);
}

void LCDHandler::DeleteActiveLCD() {
	if (_activeLCD) {
		sMemoryManager->DynamicFree(
			sMemoryManager->GetVirtualAddr(reinterpret_cast<RealPtr>(_activeLCD)));
		_activeLCD = nullptr;
	}
	if (_activeLCDPtr) {
		sMemoryManager->DynamicFree(_activeLCDPtr);
		_activeLCDPtr = 0;
	}
}

VirtPtr LCDHandler::GetActiveLCDPtr() const { return _activeLCDPtr; }

// --- LCD Constructor & Destructor Implementation ---
struct BlockLog {
	uint32_t pc;
	int id;
};
extern RollingLogBuffer<BlockLog> block_log;
RollingLogBuffer<BlockLog> block_log;

inline void DrawBlockLogWindow(RollingLogBuffer<BlockLog>& logBuffer,
	bool* p_open = nullptr) {
	static bool autoScroll = true;
	static bool showDec = false;
	static char filterText[64] = "";
	static int displayMode = 0; // 0=Hex, 1=Dec, 2=Both

	ImGui::SetNextWindowSize(ImVec2(420, 500), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Block Log", p_open, ImGuiWindowFlags_MenuBar)) {
		ImGui::End();
		return;
	}

	// ---- 菜单栏 ----
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("Options")) {
			ImGui::MenuItem("Auto-scroll", nullptr, &autoScroll);
			ImGui::Combo("Format", &displayMode, "Hex\0Decimal\0Both\0");
			ImGui::Separator();
			if (ImGui::MenuItem("Clear"))
				logBuffer.clear();
			if (ImGui::MenuItem("Copy All")) {
				std::string clip;
				auto snap = logBuffer.snapshot();
				char buf[64];
				for (size_t i = 0; i < snap.size(); ++i) {
					snprintf(buf, sizeof(buf), "%zu: 0x%08X (%u)\n", i, snap[i], snap[i]);
					clip += buf;
				}
				ImGui::SetClipboardText(clip.c_str());
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	// ---- 工具栏 ----
	ImGui::SetNextItemWidth(180);
	ImGui::InputTextWithHint("##filter", "Filter 0x...", filterText,
		IM_ARRAYSIZE(filterText));
	ImGui::SameLine();
	if (ImGui::Button("X##clr"))
		filterText[0] = '\0';
	ImGui::SameLine();
	ImGui::Text("  %zu / %zu", logBuffer.size(), logBuffer.capacity());

	ImGui::Separator();

	// ---- 过滤预处理 ----
	bool hasFilter = (filterText[0] != '\0');
	uint32_t filterVal = 0;
	bool filterIsNum = false;
	if (hasFilter) {
		char* endp = nullptr;
		std::string fs(filterText);
		for (auto& c : fs)
			c = (char)tolower((unsigned char)c);
		unsigned long v;
		if (fs.size() > 2 && fs[0] == '0' && fs[1] == 'x')
			v = strtoul(fs.c_str(), &endp, 16);
		else
			v = strtoul(fs.c_str(), &endp, 10);
		if (endp != fs.c_str() && *endp == '\0') {
			filterIsNum = true;
			filterVal = static_cast<uint32_t>(v);
		}
	}

	// ---- 表格 ----
	ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
		ImGuiTableFlags_BordersOuter |
		ImGuiTableFlags_BordersInnerV |
		ImGuiTableFlags_SizingFixedFit;

	int cols = 3; // # | Value
	if (displayMode == 2)
		cols = 3; // # | Hex | Dec

	if (ImGui::BeginTable("##block_tbl", cols, flags)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 60.0f);
		if (displayMode == 2) {
			ImGui::TableSetupColumn("Hex", ImGuiTableColumnFlags_WidthFixed, 100.0f);
			ImGui::TableSetupColumn("Dec", ImGuiTableColumnFlags_WidthStretch);
		}
		else {
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed,
				200.0f);
			ImGui::TableSetupColumn("ThreadId", ImGuiTableColumnFlags_WidthStretch);
		}
		ImGui::TableHeadersRow();

		auto view = logBuffer.view();
		size_t total = view.size();

		if (!hasFilter) {
			// 无过滤 → clipper 高效渲染
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(total));
			while (clipper.Step()) {
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
					BlockLog val = view[static_cast<size_t>(i)];
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextDisabled("%d", i);
					if (displayMode == 2) {
						ImGui::TableNextColumn();
						ImGui::Text("0x%08X", val.pc);
						ImGui::TableNextColumn();
						ImGui::Text("%u", val.pc);
					}
					else {
						ImGui::TableNextColumn();
						if (displayMode == 0)
							ImGui::Text("0x%08X", val.pc);
						else
							ImGui::Text("%u", val.pc);
						ImGui::TableNextColumn();
						ImGui::Text("%u", val.id);
					}
				}
			}
			clipper.End();
		}

		if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f)
			ImGui::SetScrollHereY(1.0f);

		ImGui::EndTable();
	}

	ImGui::End();
}
bool timer_running = true;
// The constructor now launches the window thread.
LCD::LCD() {
	// Original initializations
	xRes = 320;
	yRes = 240;
	LcdMagic.SomeVal = 0x5850;
	LcdMagic.x_res = 320;
	LcdMagic.y_res = 240;
	LcdMagic.pixel_bits =
		32; // Using 32-bit color for easier rendering with Win32
	LcdMagic.unk2_640 = 640;
	LcdMagic.brightness_level = 2;
	LcdMagic.unk1_0 = 8;
	LcdMagic.window1_bufferstart =
		sMemoryManager->GetVirtualAddr(reinterpret_cast<RealPtr>(&buffer));

	LCDMagicPtr = reinterpret_cast<LCD_MAGIC*>(
		sMemoryManager->GetVirtualAddr(reinterpret_cast<RealPtr>(&LcdMagic)));
	itself = reinterpret_cast<LCD*>(
		sMemoryManager->GetVirtualAddr(reinterpret_cast<RealPtr>(this)));

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
		SVCNameRegistry::Instance().LoadFromFile("syscalls_sdk.json");
		std::thread([&]() {
			SDL_Init(0);
			bool busy = false;
			bool running = true;
			auto frame_event = SDL_RegisterEvents(1);
			auto win = SDL_CreateWindow("Hex editor", SDL_WINDOWPOS_UNDEFINED,
				SDL_WINDOWPOS_UNDEFINED, 1200, 900,
				SDL_WINDOW_RESIZABLE);
			auto renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
			auto srf = SDL_LoadBMP("Prime_compact.bmp");
			auto txt = SDL_CreateTextureFromSurface(renderer, srf);

			// ================= 新增：创建用于渲染 LCD 的纹理 =================
			auto lcd_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_STREAMING, 320, 240);
			// ===============================================================

			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.WantCaptureKeyboard = true;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			io.Fonts->AddFontDefault();
			io.Fonts->Build();
			ImGui_ImplSDL2_InitForSDLRenderer(win, renderer);
			ImGui_ImplSDLRenderer2_Init(renderer);
			std::thread t3([&]() {
				SDL_Event se{};
				se.type = frame_event;
				se.user.windowID = SDL_GetWindowID(win);
				while (running) {
					if (!busy)
						SDL_PushEvent(&se);
					SDL_Delay(1);
				}
				});
			t3.detach();
			SDL_ShowWindow(win);
			MemoryEditor me{};
			me.ReadFn = [](const ImU8* mem, size_t off, void* user_data) -> ImU8 {
				auto f = __GET(ImU8*, (size_t)mem + off);
				if (f)
					return *f;
				return 0;
				};
			me.WriteFn = [](ImU8* mem, size_t off, ImU8 d, void* user_data) {
				auto f = __GET(ImU8*, (size_t)mem + off);
				if (f)
					*f = d;
				};
			while (1) {
				SDL_Event event{};
				busy = false;
				if (!SDL_PollEvent(&event))
					continue;
				busy = true;
				if (event.type == frame_event) {
					// ================= 新增：每帧更新 LCD 纹理 =================
					void* pixels;
					int pitch;
					if (SDL_LockTexture(lcd_texture, NULL, &pixels, &pitch) == 0) {
						uint32_t* dst = (uint32_t*)pixels;
						struct RGB555 {
							uint16_t b : 8;
							uint16_t g : 8;
							uint16_t r : 8;
						};
						auto level = sLCDHandler->brightness_level / 4.0 + 0.25;
						for (int i = 0; i < 320 * 240; i++) {
							RGB555 pixel = *reinterpret_cast<RGB555*>(&((uint8_t*)this->buffer)[i * 4]);
							uint8_t r = static_cast<uint8_t>(pixel.r * level);
							uint8_t g = static_cast<uint8_t>(pixel.g * level);
							uint8_t b = static_cast<uint8_t>(pixel.b * level);
							// 转为 ARGB8888 格式
							dst[i] = (0xFF << 24) | (r << 16) | (g << 8) | b;
						}
						SDL_UnlockTexture(lcd_texture);
					}
					// =========================================================

					SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
					SDL_RenderClear(renderer);
					ImGui_ImplSDLRenderer2_NewFrame();
					ImGui_ImplSDL2_NewFrame();
					ImGui::NewFrame();
					ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
						ImGuiDockNodeFlags_PassthruCentralNode);
					ImGui::SetNextWindowDockID(
						ImGui::GetCurrentContext()->DockContext.Nodes.Data[0].key,
						ImGuiCond_FirstUseEver); // TODO: ????????
					me.DrawWindow("Editor", (void*)0x20'00'00'00, 0x20'00'00'00,
						0x20'00'00'00);
					ImGui::SetNextWindowDockID(
						ImGui::GetCurrentContext()->DockContext.Nodes.Data[0].key,
						ImGuiCond_FirstUseEver);
					DrawSVCLogWindow(g_svcCallLog);
					ImGui::SetNextWindowDockID(
						ImGui::GetCurrentContext()->DockContext.Nodes.Data[0].key,
						ImGuiCond_FirstUseEver);
					DrawBlockLogWindow(block_log);
					ImGui::SetNextWindowDockID(
						ImGui::GetCurrentContext()->DockContext.Nodes.Data[0].key,
						ImGuiCond_FirstUseEver);

					ImGui::Begin("Key");
					if (ImGui::Button("USB Gadget")) 
						EnqueueSpecial(UI_EVENT_TYPE_USB_GADGET_INSERTION);
					if (ImGui::Button("USB Device"))
						EnqueueSpecial(UI_EVENT_TYPE_USB_DEVICE_INSERTION);
					if (ImGui::Button("USB Ejection"))
						EnqueueSpecial(UI_EVENT_TYPE_USB_EJECTION);
					if (ImGui::Button("Toggle Timer"))
						timer_running = !timer_running;
					static bool keystat[256]{};
					static bool show_img = true;
					if (srf && txt) {
						auto w = srf->w, h = srf->h;
						auto bias = ImGui::GetCursorPos();
						if (show_img)
							ImGui::Image((void*)txt, ImVec2(w, h));
						auto bias2 = ImGui::GetCursorPos();
						int i = 0;
						for (auto b : mapping) {
							ImVec2 pos = ImVec2(b.x / 100.0f * w, b.y / 100.0f * h);
							ImVec2 sz = ImVec2(b.width / 100.0f * w, b.height / 100.0f * h);
							ImGui::SetCursorPos({ bias.x + pos.x,bias.y + pos.y });
							if (show_img)
								ImGui::InvisibleButton(b.name, sz);
							else
								ImGui::Button(b.name, sz);

							if (keystat[i] ^ ImGui::IsItemActive()) {
								keystat[i] = !keystat[i];
								UIMultipressEvent uimp;
								uimp.key_code0 = b.deviceKeyCode;
								uimp.status = keystat[i] ? UI_EVENT_TYPE_KEY : UI_EVENT_TYPE_KEY_UP;
								EnqueueEvent(uimp);
							}
							++i;
						}
						ImGui::SetCursorPos(bias);
						ImVec2 lcd_pos = ImGui::GetCursorScreenPos();
						ImGui::Image((void*)(intptr_t)lcd_texture, ImVec2(320, 240));

						bool is_hovered = ImGui::IsItemHovered();
						static bool is_dragging = false;

						// 处理触控事件，与 Win32 的 WM_LBUTTONDOWN/UP/MOVE 保持一致行为
						if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
							is_dragging = true;
							ImVec2 mouse_pos = ImGui::GetMousePos();
							TouchUpdate((uint16_t)(mouse_pos.x - lcd_pos.x), (uint16_t)(mouse_pos.y - lcd_pos.y), 0, UI_EVENT_TYPE_TOUCH_BEGIN);
						}
						if (is_dragging) {
							ImVec2 mouse_pos = ImGui::GetMousePos();
							int tx = (int)(mouse_pos.x - lcd_pos.x);
							int ty = (int)(mouse_pos.y - lcd_pos.y);
							// 限制坐标在 0~319, 0~239 范围内
							if (tx < 0) tx = 0; if (tx >= 320) tx = 319;
							if (ty < 0) ty = 0; if (ty >= 240) ty = 239;

							if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
								is_dragging = false;
								TouchUpdate((uint16_t)tx, (uint16_t)ty, 0, UI_EVENT_TYPE_TOUCH_END);
							}
							else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
								TouchUpdate((uint16_t)tx, (uint16_t)ty, 0, UI_EVENT_TYPE_TOUCH_MOVE);
							}
						}
						ImGui::SetCursorPos(bias2);
						ImGui::Checkbox("Show Image", &show_img);
					}
					ImGui::End();
					// ===== Desktop Component Explorer =====
					{
						// ── 持久状态 ──
						static CComponent* sel = nullptr;
						static char filter_buf[256] = "";
						static char search_buf[256] = "";
						static std::vector<CComponent*> search_results;
						static int hex_size = 0x80;
						static bool show_hex = false;
						static MemoryEditor comp_mem;
						static bool comp_mem_init = false;
						if (!comp_mem_init) {
							comp_mem_init = true;
							comp_mem.ReadFn = me.ReadFn;
							comp_mem.WriteFn = me.WriteFn;
						}

						// ── 工具函数 ──
						auto TypeName = [](CComponent* c) -> const char* {
							if (!c)
								return "<null>";
							auto* v = c->vtbl();
							if (!v)
								return "<?>";
							auto* r = v->rtti_info();
							if (!r)
								return "<?>";
							auto* n = r->name();
							return n ? n : "<?>";
							};

						auto VA = [](const void* p) -> uint32_t {
							return p ? (uint32_t)(uintptr_t)__ADDR((void*)p) : 0;
							};

						// ── 获取 desktop 根节点 ──
						auto* _dp = __GET(VirtPtr*, 0x30D58570);
						CComponent* desktop = _dp ? __GET(CComponent*, *_dp) : nullptr;

						// ╔══════════════════════════════════════════╗
						// ║          Component Tree 窗口             ║
						// ╚══════════════════════════════════════════╝
						std::function<void(CComponent*, int)> DrawNode;
						DrawNode = [&](CComponent* n, int depth) {
							if (!n || depth > 64)
								return;

							const char* tn = TypeName(n);
							uint32_t va = VA(n);
							bool leaf = n->empty();
							bool hit = filter_buf[0] && strstr(tn, filter_buf);

							ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
								ImGuiTreeNodeFlags_SpanAvailWidth;
							if (leaf)
								flags |= ImGuiTreeNodeFlags_Leaf |
								ImGuiTreeNodeFlags_NoTreePushOnOpen;
							if (sel == n)
								flags |= ImGuiTreeNodeFlags_Selected;

							ImGui::PushID((int)va);

							if (hit)
								ImGui::PushStyleColor(ImGuiCol_Text, { 1, 1, .2f, 1 });
							bool open = ImGui::TreeNodeEx("##n", flags, "%s  [%08X]  (%d)",
								tn, va, (int)n->child_count);
							if (hit)
								ImGui::PopStyleColor();

							if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
								sel = n;

							// ── 右键菜单 ──
							if (ImGui::BeginPopupContextItem()) {
								ImGui::TextDisabled("%s @ 0x%08X", tn, va);
								ImGui::Separator();
								if (ImGui::MenuItem("Select"))
									sel = n;
								if (ImGui::MenuItem("Copy Address")) {
									char b[16];
									snprintf(b, 16, "0x%08X", va);
									ImGui::SetClipboardText(b);
								}
								if (ImGui::MenuItem("Copy Type Name"))
									ImGui::SetClipboardText(tn);
								if (ImGui::MenuItem("Goto Hex Editor"))
									me.GotoAddrAndHighlight((ImU64)va, (ImU64)va + 0x26);
								ImGui::Separator();
								bool can_rm = (n != desktop) && n->get_parent();
								if (ImGui::MenuItem("Remove", nullptr, false, can_rm)) {
									auto* p = n->get_parent();
									for (auto it = p->begin(); it != p->end(); ++it)
										if (&*it == n) {
											p->erase(it);
											break;
										}
									if (sel == n)
										sel = nullptr;
								}
								ImGui::EndPopup();
							}

							if (open && !leaf) {
								int cnt = 0;
								for (auto& ch : *n) {
									if (++cnt > 500) {
										ImGui::TextDisabled("... (%d more)",
											(int)n->child_count - cnt + 1);
										break;
									}
									DrawNode(&ch, depth + 1);
								}
								ImGui::TreePop();
							}
							ImGui::PopID();
							};
						ImGui::SetNextWindowDockID(
							ImGui::GetCurrentContext()->DockContext.Nodes.Data[0].key,
							ImGuiCond_FirstUseEver);
						ImGui::Begin("Component Tree");
						{
							ImGui::SetNextItemWidth(-50);
							ImGui::InputTextWithHint("##flt", "Highlight...", filter_buf,
								sizeof(filter_buf));
							ImGui::SameLine();
							if (ImGui::SmallButton("X##f"))
								filter_buf[0] = 0;
							ImGui::Separator();

							if (desktop) {
								ImGui::BeginChild("##tree_scroll");
								DrawNode(desktop, 0);
								ImGui::EndChild();
							}
							else {
								ImGui::TextColored({ 1, .3f, .3f, 1 },
									"Desktop = NULL  (0x30D58570)");
							}
						}
						ImGui::End();
						ImGui::SetNextWindowDockID(
							ImGui::GetCurrentContext()->DockContext.Nodes.Data[0].key,
							ImGuiCond_FirstUseEver);
						// ╔══════════════════════════════════════════╗
						// ║        Component Properties 窗口         ║
						// ╚══════════════════════════════════════════╝
						ImGui::Begin("Component Properties");
						if (sel) {
							const char* tn = TypeName(sel);
							uint32_t va = VA(sel);

							ImGui::TextColored({ .4f, .85f, 1, 1 }, "%s", tn);
							ImGui::SameLine();
							ImGui::TextDisabled("@ 0x%08X", va);
							ImGui::Separator();

							// ── 属性表 ──
							if (ImGui::BeginTable("##props", 2,
								ImGuiTableFlags_Borders |
								ImGuiTableFlags_RowBg |
								ImGuiTableFlags_SizingStretchProp)) {
								ImGui::TableSetupColumn(
									"Field", ImGuiTableColumnFlags_WidthFixed, 110.f);
								ImGui::TableSetupColumn("Value");
								ImGui::TableHeadersRow();

								auto Row = [](const char* label) {
									ImGui::TableNextRow();
									ImGui::TableSetColumnIndex(0);
									ImGui::TextUnformatted(label);
									ImGui::TableSetColumnIndex(1);
									};

								Row("Address");
								ImGui::Text("0x%08X", va);
								Row("VTable");
								ImGui::Text("0x%08X", VA(sel->vtbl()));
								Row("Type");
								ImGui::TextUnformatted(tn);
								Row("Children");
								ImGui::Text("%d", (int)sel->child_count);

								// Parent
								auto* par = sel->get_parent();
								Row("Parent");
								if (par) {
									ImGui::Text("%s [%08X]", TypeName(par), VA(par));
									if (ImGui::IsItemClicked())
										sel = par;
								}
								else {
									ImGui::TextDisabled("<none>");
								}

								//// Prev Sibling
								// auto* ps = sel->prev_sibling
								//	? __GET(CComponent*, sel->prev_sibling) : nullptr;
								// Row("Prev Sibling");
								// if (ps) {
								//	ImGui::Text("%s [%08X]", TypeName(ps), VA(ps));
								//	if (ImGui::IsItemClicked()) sel = ps;
								// }
								// else {
								//	ImGui::TextDisabled("<none>");
								// }

								// Next Sibling
								auto* ns = sel->next_sibling
									? __GET(CComponent*, sel->next_sibling)
									: nullptr;
								Row("Next Sibling");
								if (ns) {
									ImGui::Text("%s [%08X]", TypeName(ns), VA(ns));
									if (ImGui::IsItemClicked())
										sel = ns;
								}
								else {
									ImGui::TextDisabled("<none>");
								}

								// First Child
								auto* fc = sel->first_child
									? __GET(CComponent*, sel->first_child)
									: nullptr;
								Row("First Child");
								if (fc) {
									ImGui::Text("%s [%08X]", TypeName(fc), VA(fc));
									if (ImGui::IsItemClicked())
										sel = fc;
								}
								else {
									ImGui::TextDisabled("<none>");
								}

								// RTTI 继承链
								if (auto* ri = sel->rtti()) {
									auto bases = ri->parents();
									for (size_t i = 0; i < bases.size(); i++) {
										char lbl[32];
										snprintf(lbl, sizeof(lbl), "Base[%zu]", i);
										Row(lbl);
										ImGui::Text("%s", (bases[i] && bases[i]->name())
											? bases[i]->name()
											: "?");
									}
								}

								ImGui::EndTable();
							}

							// ── 子节点列表 ──
							if (!sel->empty()) {
								ImGui::Separator();
								ImGui::Text("Children (%d):", (int)sel->child_count);
								ImGui::BeginChild("##ch_list", ImVec2(0, 150), true);
								int idx = 0;
								for (auto& ch : *sel) {
									if (idx > 500)
										break;
									uint32_t cva = VA(&ch);
									char lbl[256];
									snprintf(lbl, sizeof(lbl), "[%d] %s  [%08X]##c%d", idx,
										TypeName(&ch), cva, idx);
									if (ImGui::Selectable(lbl))
										sel = &ch;
									idx++;
								}
								ImGui::EndChild();
							}

							// ── 导航按钮 ──
							ImGui::Separator();
							{
								bool hp = sel->get_parent() != nullptr;
								bool hc = (bool)sel->first_child;
								bool hn = (bool)sel->next_sibling;
								// bool hv = (bool)sel->prev_sibling;

								if (!hp)
									ImGui::BeginDisabled();
								if (ImGui::Button("Parent"))
									sel = sel->get_parent();
								if (!hp)
									ImGui::EndDisabled();

								ImGui::SameLine();
								if (!hc)
									ImGui::BeginDisabled();
								if (ImGui::Button("Child"))
									sel = __GET(CComponent*, sel->first_child);
								if (!hc)
									ImGui::EndDisabled();

								// ImGui::SameLine();
								// if (!hv) ImGui::BeginDisabled();
								// if (ImGui::Button("<< Prev"))
								//	sel = __GET(CComponent*, sel->prev_sibling);
								// if (!hv) ImGui::EndDisabled();

								ImGui::SameLine();
								if (!hn)
									ImGui::BeginDisabled();
								if (ImGui::Button("Next >>"))
									sel = __GET(CComponent*, sel->next_sibling);
								if (!hn)
									ImGui::EndDisabled();

								ImGui::SameLine();
								if (ImGui::Button("Hex Editor"))
									me.GotoAddrAndHighlight((ImU64)va, (ImU64)va + 0x26);
							}

							// ── 操作按钮 ──
							ImGui::Separator();
							{
								bool can_rm = sel != desktop && sel->get_parent();
								if (!can_rm)
									ImGui::BeginDisabled();
								if (ImGui::Button("Remove from Parent")) {
									auto* p = sel->get_parent();
									for (auto it = p->begin(); it != p->end(); ++it)
										if (&*it == sel) {
											p->erase(it);
											break;
										}
									sel = p;
								}
								if (!can_rm)
									ImGui::EndDisabled();
							}

							// ── 内嵌 Hex 视图 ──
							ImGui::Separator();
							ImGui::Checkbox("Raw Memory", &show_hex);
							if (show_hex) {
								ImGui::SliderInt("Bytes", &hex_size, 0x10, 0x400);
								ImGui::BeginChild("##raw_hex", ImVec2(0, 300), true);
								comp_mem.DrawContents((void*)(uintptr_t)va, (size_t)hex_size,
									(size_t)va);
								ImGui::EndChild();
							}
						}
						else {
							ImGui::TextDisabled("No component selected.");
							ImGui::TextWrapped(
								"Click a node in the Component Tree window to inspect it.");
						}
						ImGui::End();
						ImGui::SetNextWindowDockID(
							ImGui::GetCurrentContext()->DockContext.Nodes.Data[0].key,
							ImGuiCond_FirstUseEver);
						// ╔══════════════════════════════════════════╗
						// ║        Component Search 窗口             ║
						// ╚══════════════════════════════════════════╝
						ImGui::Begin("Component Search");
						{
							ImGui::SetNextItemWidth(-120);
							ImGui::InputTextWithHint("##sb", "Type name...", search_buf,
								sizeof(search_buf));
							ImGui::SameLine();

							if (ImGui::Button("Search") && desktop && search_buf[0]) {
								search_results.clear();
								std::function<void(CComponent*, int)> Scan;
								Scan = [&](CComponent* n, int d) {
									if (!n || d > 64 || search_results.size() >= 5000)
										return;
									if (strstr(TypeName(n), search_buf))
										search_results.push_back(n);
									int cnt = 0;
									for (auto& ch : *n) {
										if (++cnt > 1000 || search_results.size() >= 5000)
											break;
										Scan(&ch, d + 1);
									}
									};
								Scan(desktop, 0);
							}
							ImGui::SameLine();
							if (ImGui::SmallButton("X##s")) {
								search_results.clear();
								search_buf[0] = 0;
							}

							ImGui::Text("%zu result(s)", search_results.size());
							ImGui::Separator();

							ImGui::BeginChild("##search_results");
							ImGuiListClipper clipper;
							clipper.Begin((int)search_results.size());
							while (clipper.Step()) {
								for (int i = clipper.DisplayStart; i < clipper.DisplayEnd;
									i++) {
									auto* c = search_results[i];
									uint32_t cva = VA(c);
									char lbl[256];
									snprintf(lbl, sizeof(lbl), "%s  [%08X]##r%d", TypeName(c),
										cva, i);

									if (ImGui::Selectable(lbl, c == sel))
										sel = c;

									// 右键可直接删除搜索结果中的组件
									if (ImGui::BeginPopupContextItem()) {
										if (ImGui::MenuItem("Select"))
											sel = c;
										if (ImGui::MenuItem("Copy Address")) {
											char b[16];
											snprintf(b, 16, "0x%08X", cva);
											ImGui::SetClipboardText(b);
										}
										if (ImGui::MenuItem("Remove from Parent")) {
											auto* p = c->get_parent();
											if (p) {
												for (auto it = p->begin(); it != p->end(); ++it)
													if (&*it == c) {
														p->erase(it);
														break;
													}
											}
											if (sel == c)
												sel = nullptr;
											search_results.erase(search_results.begin() + i);
										}
										ImGui::EndPopup();
									}
								}
							}
							ImGui::EndChild();
						}
						ImGui::End();
					}
					ImGui::EndFrame();
					ImGui::Render();
					ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
					SDL_RenderPresent(renderer);
				}
				else {
					ImGui_ImplSDL2_ProcessEvent(&event);
				}
			}
			}).detach();
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

			// Move the thread handle out of the map so we can join it outside the
			// lock
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
	if (!RegisterClassEx(&wc))
		return;

	// Adjust window size to account for title bar and borders
	RECT wr = { 0, 0, (LONG)lcd->xRes, (LONG)lcd->yRes };
	AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

	HWND hwnd = CreateWindowEx(0, L"EmulatedLcdClass", L"LCD Display",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		wr.right - wr.left, wr.bottom - wr.top, NULL, NULL,
		GetModuleHandle(NULL),
		lcd // Pass the LCD pointer to WM_CREATE
	);

	if (!hwnd)
		return;

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
		*__GET(uint32_t*, 0x51000040) = -1; // Reset the timer tick count
		return 0;
	}

	case WM_TIMER: {
		// Trigger a repaint to update the screen
		// 触发重绘以更新屏幕
		InvalidateRect(hwnd, NULL, FALSE);
		(*__GET(uint32_t*, 0x51000040)) -= 10000 / 60;
		static int i = 0, j = 0;
		if (timer_running) {
			if (i++ > 10)
				EnqueueSpecial(ui_event_type_e::UI_EVENT_TYPE_SYS_TIMER), i = 0;
			if (j++ > 60)
				EnqueueSpecial(ui_event_type_e::UI_EVENT_TYPE_DEVICE_TIMER), j = 0;
		}
		return 0;
	}

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);

		// Draw the LCD buffer first if it exists
		// 如果 LCD 缓冲区存在，首先绘制它
		if (lcd && lcd->buffer) {
			// ⚡ Bolt: Performance Improvement
			// Optimization: Avoid dynamic memory allocation on every WM_PAINT call
			// Impact: Reduces heap fragmentation and allocation overhead during frequent screen updates
			// We use thread_local to avoid thread-safety issues if multiple windows are opened across threads.
			thread_local std::vector<uint32_t> tempBufferVec;
			if (tempBufferVec.size() < lcd->xRes * lcd->yRes) {
				tempBufferVec.resize(lcd->xRes * lcd->yRes);
			}
			uint32_t* tempBuffer = tempBufferVec.data();
			struct RGB555 {
				uint16_t b : 8;
				uint16_t g : 8;
				uint16_t r : 8;
			};

			struct ARGB32 {
				uint8_t b;
				uint8_t g;
				uint8_t r;
				uint8_t a;
			};

			for (int i = 0; i < lcd->xRes * lcd->yRes; i++) {
				RGB555 pixel =
					*reinterpret_cast<RGB555*>(&((uint8_t*)lcd->buffer)[i * 4]);

				// 扩展 5 位到 8 位
				uint8_t r = pixel.r;
				uint8_t g = pixel.g;
				uint8_t b = pixel.b;

				auto level = sLCDHandler->brightness_level / 4.0 + 0.25; // 亮度等级
				r = static_cast<uint8_t>(r * level);
				g = static_cast<uint8_t>(g * level);
				b = static_cast<uint8_t>(b * level);

				ARGB32 argb = { b, g, r, 0xFF };
				tempBuffer[i] = *reinterpret_cast<uint32_t*>(&argb);
			}

			BITMAPINFO bi = { 0 };
			bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bi.bmiHeader.biWidth = lcd->xRes;
			bi.bmiHeader.biHeight = -lcd->yRes;
			bi.bmiHeader.biPlanes = 1;
			bi.bmiHeader.biBitCount = 32;
			bi.bmiHeader.biCompression = BI_RGB;
			StretchDIBits(hdc, 0, 0, lcd->xRes, lcd->yRes, 0, 0, lcd->xRes, lcd->yRes,
				tempBuffer, &bi, DIB_RGB_COLORS, SRCCOPY);
		}

		EndPaint(hwnd, &ps);
		return 0;
	}
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
			// vk_to_device_keymap[VK_MULTIPLY] = '*';
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
			uime.status = UI_EVENT_TYPE_KEY;
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
			// vk_to_device_keymap[VK_MULTIPLY] = '*';
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
			uime.status = UI_EVENT_TYPE_KEY_UP;
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
			// UIMultipressEvent uime{};
			// uime.touch_x = static_cast<uint16_t>(x);
			// uime.touch_y = static_cast<uint16_t>(y);
			// uime.status = UI_EVENT_TYPE_TOUCH_BEGIN;
			TouchUpdate(x, y, 0, UI_EVENT_TYPE_TOUCH_BEGIN);
		}
		is_lbtn_down = true;
		break;
	}
	case WM_LBUTTONUP: {
		{
			auto x = LOWORD(lParam);
			auto y = HIWORD(lParam);
			TouchUpdate(x, y, 0, UI_EVENT_TYPE_TOUCH_END);
		}
		is_lbtn_down = false;
		break;
	}
	case WM_MOUSEMOVE: {
		if (is_lbtn_down) {
			auto x = LOWORD(lParam);
			auto y = HIWORD(lParam);
			TouchUpdate(x, y, 0, UI_EVENT_TYPE_TOUCH_MOVE);
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