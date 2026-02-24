// svc_system.cpp — 系统级、事件输入、加载器文件、程序管理、设备 I/O handler
#include "svc_common.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <vector>

#include "LCD.h"
#include "PELoader.h"
#include "Thread.h"
#include "ThreadHandler.h"
#include "executor.h"
#include "ui.h"

// ================================================================
//  LCD / 系统变量
// ================================================================

uint32_t LCDOn(SystemServiceArguments* args) { return 0; }

uint32_t GetActiveLCD(SystemServiceArguments* args) {
	return sLCDHandler->GetActiveLCDPtr();
}

static uint32_t SetSystemVariable_impl(uint32_t var, uint32_t type,
	uint32_t value) {
	printf("    + sys.var %d (type %d) <- %d\n", var, type, value);
	if (var == 89)
		return (uint32_t)-1;
	if (var == 6 && type == 2)
		sLCDHandler->brightness_level = value;
	return (uint32_t)0;
}
uint32_t SetSystemVariable(SystemServiceArguments* args) {
	return AutoBind<decltype(SetSystemVariable_impl)>::thunk<
		SetSystemVariable_impl>(args);
}

// ================================================================
//  内存分配
// ================================================================

static uint32_t lcalloc_impl(uint32_t elem, uint32_t count) {
	VirtPtr addr;
	if (sMemoryManager->DyanmicAlloc(&addr, elem * count) == ERROR_OK)
		return addr;
	return VirtPtr{};
}
uint32_t lcalloc(SystemServiceArguments* args) {
	return AutoBind<decltype(lcalloc_impl)>::thunk<lcalloc_impl>(args);
}

static uint32_t lmalloc_impl(uint32_t size) {
	VirtPtr addr;
	if (sMemoryManager->DyanmicAlloc(&addr, size) == ERROR_OK)
		return addr;
	return VirtPtr{};
}
uint32_t lmalloc(SystemServiceArguments* args) {
	return AutoBind<decltype(lmalloc_impl)>::thunk<lmalloc_impl>(args);
}

static uint32_t lrealloc_impl(VirtPtr ptr, uint32_t new_size) {
	if (ptr == 0) {
		sMemoryManager->DyanmicAlloc(&ptr, new_size);
		return ptr;
	}
	sMemoryManager->DynamicRealloc(&ptr, static_cast<size_t>(new_size));
	return ptr;
}
uint32_t lrealloc(SystemServiceArguments* args) {
	return AutoBind<decltype(lrealloc_impl)>::thunk<lrealloc_impl>(args);
}

uint32_t _lfree(SystemServiceArguments* args) {
	ErrorCode err;
	if ((err = sMemoryManager->DynamicFree(args->r0)) != ERROR_OK)
		printf("    +error\n");
	return args->r0;
}

// ================================================================
//  系统时间
// ================================================================

struct SystemTime {
	uint16_t Year;
	uint16_t Month;
	uint16_t DayOfWeek;
	uint16_t Day;
	uint16_t Hour;
	uint16_t Minute;
	uint16_t Second;
	uint16_t Milliseconds;
};

uint32_t GetSysTime(SystemServiceArguments* args) {
	SystemTime* sysTime = __GET(SystemTime*, args->r0);
	auto now = std::chrono::system_clock::now();
	auto now_c = std::chrono::system_clock::to_time_t(now);
	tm* parts = std::localtime(&now_c);
	sysTime->Year = parts->tm_year + 1900;
	sysTime->Month = parts->tm_mon + 1;
	sysTime->DayOfWeek = parts->tm_wday;
	sysTime->Day = parts->tm_mday;
	sysTime->Hour = parts->tm_hour;
	sysTime->Minute = parts->tm_min;
	sysTime->Second = parts->tm_sec;
	auto totalMSec = std::chrono::duration_cast<std::chrono::milliseconds>(
		now.time_since_epoch())
		.count();
	sysTime->Milliseconds = static_cast<uint16_t>(totalMSec % 1000);
	return args->r0;
}

// ================================================================
//  电源 / 中断
// ================================================================

extern "C" uint32_t ExitProcess(uint32_t);

uint32_t SysPowerOff(SystemServiceArguments* args) {
	ExitProcess(0);
	return 0;
}

uint32_t BatteryLowCheck(SystemServiceArguments* args) { 
	// -1 battery low
	// -2 battery critical
	return 0; 
}

uint32_t InterruptInitialize(SystemServiceArguments* args) {
	printf("   +InterruptInitialize -> %p\n", (void*)(uintptr_t)args->r2);
	return sThreadHandler->interruptPC = (args->r2);
}

uint32_t InterruptDone(SystemServiceArguments* args) {
	printf("A\n");
	if (sThreadHandler->interrupting) {
		printf("Stop emu!\n");
		sThreadHandler->interruptPC = 0xAAAAAAAA;
		sThreadHandler->SwitchThread();
	}
	return 0;
}

// ================================================================
//  事件输入系统
// ================================================================

static int _ui_thread_id = 0;
static std::mutex g_event_lock;
static std::vector<UIMultipressEvent> g_events_queue;
static constexpr size_t MAX_MULTIPRESS_EVENTS = 8;
static UIMultipressEvent touch_states[MAX_MULTIPRESS_EVENTS];
static std::atomic<int> special_event = 0;
static std::atomic<bool> touch = false;

void EnqueueEvent(UIMultipressEvent uime) {
	std::lock_guard lg(g_event_lock);
	g_events_queue.push_back(uime);
	sThreadHandler->WakeThread(_ui_thread_id);
}

void EnqueueSpecial(int val) { special_event.store(val); }

void TouchUpdate(int x, int y, int finger_id, ui_event_type_e status) {
	if (finger_id < 0 || finger_id >= (int)MAX_MULTIPRESS_EVENTS)
		return;
	touch_states[finger_id].finger_id = finger_id;
	touch_states[finger_id].touch_x = x;
	touch_states[finger_id].touch_y = y;
	touch_states[finger_id].status = status;
	touch.store(true);
	sThreadHandler->WakeThread(_ui_thread_id);
}

uint32_t GetEvent(SystemServiceArguments* args) {
	auto& event = *__GET(ui_event_prime_s*, args->r0);
	event = {};
	int k;
	if (k = special_event.load()) {
		special_event.store(0);
		event.event_type = (ui_event_type_e)k;
		return 0;
	}
	std::lock_guard lg(g_event_lock);
	event.event_type = UI_EVENT_TYPE_TOUCH;
	if (touch.load()) {
		for (size_t i = 0; i < MAX_MULTIPRESS_EVENTS; ++i) {
			if (touch_states[i].status != UI_EVENT_TYPE_INVALID)
				event.multipress_events[event.available_multipress_events++] =
				touch_states[i];
		}
		event.available_multipress_events = 8;
		touch.store(false);
		return 0;
	}
	if (!g_events_queue.empty()) {
		event.event_type = UI_EVENT_TYPE_KEY_BATCH;
		if (g_events_queue.size() > 8) {
			g_events_queue.clear();
			return 0;
		}
		event.available_multipress_events = (uint32_t)g_events_queue.size();
		std::copy(g_events_queue.begin(), g_events_queue.end(),
			event.multipress_events);
		g_events_queue.clear();
	}
	else {
		_ui_thread_id = sThreadHandler->GetCurrentThreadId();
		sThreadHandler->CurrentThreadSleep(
			50); // Sleep 50ms, relying on WakeThread to wake us early
		sThreadHandler->CurrentThreadYield();
		event.event_type = UI_EVENT_TYPE_INVALID;
	}
	return 0;
}

// ================================================================
//  程序管理
// ================================================================

static VirtPtr g_program_struc = 0;

uint32_t _FindResourceW(SystemServiceArguments* args) {
	printf("Warn: FindResourceW stub!!!\n");
	return 0;
}

uint32_t _LoadLibraryA(SystemServiceArguments* args) {
	auto libname = __GET(char*, args->r0);
	auto path = VMPath::toHost(libname);
	if (!fs::exists(path)) {
		char system32_path[260] = "A:\\WINDOW\\SYSTEM\\";
		strcat(system32_path, libname);
		path = VMPath::toHost(system32_path);
	}
	PEImage pei;
	if (LoadPEImage(path, pei, VMPath::toHost("A:\\WINDOW\\SYSTEM")) ==
		ERROR_OK) {
		return pei.actualImageBase;
	}
	else {
		printf("LoadLibraryA failed!!!\n");
	}
	return 0;
}

uint32_t _FreeLibrary(SystemServiceArguments* args) {
	printf("Warn: FreeLibrary stub!!!\n");
	return 0;
}

uint32_t prgrmIsRunning(SystemServiceArguments* args) {
	printf("    program: %s\n", __GET(char*, args->r0));
	if (g_program_struc == 0)
		sMemoryManager->DyanmicAlloc(&g_program_struc, 0x250);
	return g_program_struc;
}

uint32_t ProgramIsRunningW(SystemServiceArguments* args) {
	printf("    program: %ls\n", __GET(wchar_t*, args->r0));
	if (g_program_struc == 0)
		sMemoryManager->DyanmicAlloc(&g_program_struc, 0x250);
	return g_program_struc;
}

uint32_t GetCurrentExecutable(SystemServiceArguments*) {
	auto app = VMPath::toVM(runningApplicationImagePath.c_str());
	VirtPtr vp;
	sMemoryManager->DyanmicAlloc(&vp, app.size() + 1);
	std::memcpy(__GET(void*, vp), app.c_str(), app.size() + 1);
	return vp;
}

uint32_t _GetModuleFileNameA(SystemServiceArguments* args) {
	auto ptr = __GET(char*, args->r1);
	auto sz = args->r2;
	auto img = GetPEImageByHandle(args->r0);
	auto vmpath = VMPath::toVM(img->path.c_str());
	if (vmpath.size() >= sz)
		return sz;
	strcpy(ptr, vmpath.c_str());
	return (uint32_t)vmpath.size();
}

#pragma pack(push, 1)
typedef struct _MASTER_ID_INFO {
	char a1[40];
	char a2[36];
	char master_id_suffix[18];
	char a3[78];
} MASTER_ID_INFO;
#pragma pack(pop)

uint32_t GetMasterIDInfo(SystemServiceArguments* args) {
	auto ptr = __GET(_MASTER_ID_INFO*, args->r0);
	memset(ptr, 0, sizeof(_MASTER_ID_INFO));
	strcpy(ptr->a2, "HPPRIMEEMU");
	strcpy(ptr->a3, "BESTARTOS114514");
	return 0;
}

// ================================================================
//  设备 I/O（CreateFile / DeviceIoControl / CloseHandle）
// ================================================================

static std::unordered_map<uint32_t, std::string> g_vdev_table;
static uint32_t g_next_dev_handle = 1;

uint32_t CreateFile(SystemServiceArguments* args) {
	std::cout << "    +CreateFile_stub name:" << __GET(char*, args->r0) << "\n";
	g_vdev_table[++g_next_dev_handle] = __GET(char*, args->r0);
	return g_next_dev_handle;
}

bool is_factory_mount = false;

// 持久化 FRP 数据存储
static std::vector<uint8_t> g_frp_data;
static const char* FRP_PERSIST_PATH = "frp_data.bin";

static void frp_load() {
	if (!g_frp_data.empty()) return;
	std::ifstream f(FRP_PERSIST_PATH, std::ios::binary | std::ios::ate);
	if (f.is_open()) {
		auto sz = f.tellg();
		f.seekg(0);
		g_frp_data.resize(sz);
		f.read((char*)g_frp_data.data(), sz);
		std::cout << "    +FRP: loaded " << sz << " bytes from disk\n";
	}
}

static void frp_save() {
	if (g_frp_data.empty()) return;
	std::ofstream f(FRP_PERSIST_PATH, std::ios::binary);
	f.write((char*)g_frp_data.data(), g_frp_data.size());
	std::cout << "    +FRP: saved " << g_frp_data.size() << " bytes to disk\n";
}

uint32_t DeviceIoControl(SystemServiceArguments* args) {
	uint32_t handle = args->r0;
	uint32_t request = args->r1;
	char* in = __GET(char*, args->r2);
	uint32_t size = args->r3;
	char* out = __GET(char*, *__GET(uint32_t*, args->sp + 8));
	int outlen = *__GET(int*, args->sp + 12);
	uint32_t* retlen = __GET(uint32_t*, *__GET(int*, args->sp + 16));
	void* overlapped = __GET(void*, *__GET(int*, args->sp + 20));

	if (g_vdev_table.find(handle) == g_vdev_table.end()) {
		std::cerr << "    +DeviceIoControl_stub: Invalid handle " << handle << "\n";
		return 0;
	}

	if (g_vdev_table[handle].ends_with("BAT")) {
		auto voltage = (uint16_t*)out;
		memset(voltage, 0, outlen);
		voltage[5] = 4;     // 0-4 bat level
		voltage[7] = 0;     // 0: not charging, 1: charging, 2: full
		voltage[3] = 1000; // a/d value, 10bit, v_ref = 5v, ~3.42v
		return 1;
	}

	if (g_vdev_table[handle].ends_with("ARCH")) {
		// req 258: reboot
		// req 259: reboot bootloader
		// req 260: get hardware type (VerA or VerC)
		auto voltage = (uint32_t*)out;
		voltage[0] = 0;
		return 1;
	}

	if (g_vdev_table[handle].ends_with("FACTORY")) {
		switch (request) {
		case 256:
			// Mount factory partition
			is_factory_mount = true;
			std::cout << "    +FRP: factory partition mounted\n";
			if (retlen) *retlen = 0;
			return 1;

		case 257:
			// Unmount factory partition
			is_factory_mount = false;
			std::cout << "    +FRP: factory partition unmounted\n";
			if (retlen) *retlen = 0;
			return 1;

		case 258: {
			// Read FRP data
			std::cout << "    +FRP data read! len:" << outlen << "\n";
			frp_load();

			memset(out, 0, outlen);
			if (!g_frp_data.empty()) {
				uint32_t copy_len = std::min((uint32_t)outlen, (uint32_t)g_frp_data.size());
				memcpy(out, g_frp_data.data(), copy_len);
				if (retlen) *retlen = copy_len;
			}
			else {
				// 未设置过 FRP，返回全 0（表示未锁定）
				if (retlen) *retlen = outlen;
			}
			return 1;
		}

		case 259: {
			// Write FRP data
			std::cout << "    +FRP data write! len:" << size << "\n";
			g_frp_data.resize(size);
			memcpy(g_frp_data.data(), in, size);
			frp_save();
			if (retlen) *retlen = size;
			return 1;
		}

		case 260: {
			// Erase/Clear FRP data
			std::cout << "    +FRP data erase!\n";
			g_frp_data.clear();
			std::remove(FRP_PERSIST_PATH);
			if (retlen) *retlen = 0;
			return 1;
		}

		default:
			std::cout << "    +FRP: unknown request " << request << "\n";
			if (out && outlen > 0) memset(out, 0, outlen);
			if (retlen) *retlen = 0;
			return 1;
		}
	}


	std::cout << "    +DeviceIoControl_stub file:" << g_vdev_table[handle]
		<< " request:" << request << " size:" << size << "\n";
	// 未知设备的默认处理
	if (out && outlen > 0)
		memset(out, 0xff, outlen);
	return 1;
}
uint32_t CloseHandle(SystemServiceArguments* args) {
	uint32_t handle = args->r0;
	if (handle == 0)
		return 0;
	std::cout << "    +CloseHandle_stub handle:" << handle << "\n";
	auto it = g_vdev_table.find(handle);
	if (it != g_vdev_table.end()) {
		g_vdev_table.erase(it);
		return 1;
	}
	return 0;
}

// ================================================================
//  加载器文件描述符
// ================================================================

typedef struct loader_file_descriptor_s {
	VirtPtr cart;
	VirtPtr parent_fd;
	uint32_t subfile_base;
	uint32_t size;
	uint32_t subfile_offset;
	short unk_0x14;
	short unk_0x16;
	unsigned int unk_0x18;
	unsigned int unk_0x1c;
} loader_file_descriptor_t;

uint32_t _OpenFile(SystemServiceArguments* args) {
	uint32_t cart = VFileSystem::instance().open(__GET(const char*, args->r0),
		__GET(const char*, args->r1));
	if (!cart)
		return 0;

	VirtPtr vp;
	sMemoryManager->DyanmicAlloc(&vp, sizeof(loader_file_descriptor_t));
	loader_file_descriptor_t* fd = __GET(loader_file_descriptor_t*, vp);
	if (!fd) {
		VFileSystem::instance().close(cart);
		errno = ENOMEM;
		return 0;
	}

	fd->cart = cart;
	fd->parent_fd = 0;
	fd->subfile_base = 0;
	fd->subfile_offset = 0;
	fd->unk_0x14 = 0;
	fd->unk_0x16 = 0;
	fd->unk_0x18 = 0;
	fd->unk_0x1c = 0;

	fd->size = VFileSystem::instance().filesize(cart);
	return (uint32_t)vp;
}

uint32_t _FileSize(SystemServiceArguments* args) {
	loader_file_descriptor_t* stream =
		__GET(loader_file_descriptor_t*, args->r0);
	if (!stream) {
		errno = EINVAL;
		return (uint32_t)(size_t)-1;
	}
	return stream->size;
}

uint32_t _OpenSubFile(SystemServiceArguments* args) {
	loader_file_descriptor_t* parent =
		__GET(loader_file_descriptor_t*, args->r0);
	size_t base = args->r1;
	size_t max_size = args->r2;
	if (!parent) {
		errno = EINVAL;
		return 0;
	}
	if (base + max_size > parent->size) {
		errno = EINVAL;
		return 0;
	}

	VirtPtr vp;
	sMemoryManager->DyanmicAlloc(&vp, sizeof(loader_file_descriptor_t));
	loader_file_descriptor_t* fd = __GET(loader_file_descriptor_t*, vp);
	if (!fd) {
		errno = ENOMEM;
		return 0;
	}

	fd->cart = parent->cart;
	fd->parent_fd = args->r0;
	fd->subfile_base = parent->subfile_base + (uint32_t)base;
	fd->size = (uint32_t)max_size;
	fd->subfile_offset = 0;
	fd->unk_0x14 = 1;
	fd->unk_0x16 = 0;
	fd->unk_0x18 = 0;
	fd->unk_0x1c = 0;
	return vp;
}

uint32_t _CloseFile(SystemServiceArguments* args) {
	loader_file_descriptor_t* stream =
		__GET(loader_file_descriptor_t*, args->r0);
	if (!stream)
		return 0;
	if (!stream->parent_fd)
		VFileSystem::instance().close(stream->cart);
	sMemoryManager->DynamicFree(args->r0);
	return 0;
}

enum sys_seek_whence_e {
	_SYS_SEEK_SET = 0,
	_SYS_SEEK_CUR,
	_SYS_SEEK_END,
};

uint32_t _FseekFile(SystemServiceArguments* args) {
	loader_file_descriptor_t* stream =
		__GET(loader_file_descriptor_t*, args->r0);
	size_t offset = args->r1;
	int whence = args->r2;
	if (!stream) {
		errno = EINVAL;
		return (uint32_t)-1;
	}

	size_t new_offset;
	switch (whence) {
	case _SYS_SEEK_SET:
		new_offset = offset;
		break;
	case _SYS_SEEK_CUR:
		new_offset = stream->subfile_offset + offset;
		break;
	case _SYS_SEEK_END:
		new_offset = stream->size + offset;
		break;
	default:
		errno = EINVAL;
		return (uint32_t)-1;
	}

	if (new_offset > stream->size) {
		errno = EINVAL;
		return (uint32_t)-1;
	}
	stream->subfile_offset = (uint32_t)new_offset;
	return 0;
}

uint32_t _ReadFile(SystemServiceArguments* args) {
	loader_file_descriptor_t* stream =
		__GET(loader_file_descriptor_t*, args->r0);
	void* ptr = __GET(void*, args->r1);
	size_t size = args->r2;
	if (!stream || !ptr) {
		errno = EINVAL;
		return 0;
	}

	size_t bytes_remaining = stream->size - stream->subfile_offset;
	if (bytes_remaining == 0)
		return 0;
	size_t to_read = std::min(size, bytes_remaining);
	size_t absolute_offset = stream->subfile_base + stream->subfile_offset;

	size_t bytes_read = VFileSystem::instance().with(
		stream->cart, [&](FILE* fp, std::wstring&) -> size_t {
			if (fseek(fp, (long)absolute_offset, SEEK_SET) != 0)
				return 0;
			return fread(ptr, 1, to_read, fp);
		});

	stream->subfile_offset += (uint32_t)bytes_read;
	return (uint32_t)bytes_read;
}

uint32_t FSGetDiskRoomState(SystemServiceArguments* args) {
	auto dat = __GET(uint32_t*, args->r1 + 52);
	dat[0] = (uint32_t)-1;
	return 0;
}

// ================================================================
//  sys_init
// ================================================================

void sys_init() {
	// 保留，可用于未来初始化
}
