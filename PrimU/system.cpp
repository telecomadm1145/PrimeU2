

#include "stdafx.h"
#include <chrono>
#include <ctime>


#include "executor.h"
#include "LCD.h"
#include "InterruptHandler.h"
#include "ThreadHandler.h"
#include <string>
#include <algorithm>
#include <filesystem>
#include <locale>
#include <codecvt>
#include <cctype>
#include <unordered_map>
#include <mutex>
#include <array>
#include <vector>
#include "handlers.h"

namespace fs = std::filesystem;

// ====== 全局状态和工具函数 ======
static std::mutex g_vfile_mutex;

// 虚拟文件表结构
struct VFile {
	FILE* fp = nullptr;
	std::string hostPath;
	std::string mode;
};

static std::unordered_map<uint32_t, VFile> g_vfile_table;
static uint32_t g_next_handle = 1; // 0 保留为失败/无效

// 每个盘符的完整 CWD（以 host path 形式存储），索引 0 -> 'A'
static std::array<std::string, 26> g_cwds;
static char g_currentDrive = 'A';
static std::once_flag g_init_flag;

static void ensure_prime_drive_roots_initialized() {
	// 初始化每个 drive 的根为 "prime_data\<DriveLetter>\"
	for (int i = 0; i < 26; ++i) {
		std::string root = "prime_data\\";
		root.push_back(static_cast<char>('A' + i));
		root += "\\";
		g_cwds[i] = root;
		try {
			fs::create_directories(root);
		}
		catch (...) {}
	}
}

// 简单把重复的斜杠规范化为单个反斜杠
static std::string normalize_slashes(std::string s) {
	std::replace(s.begin(), s.end(), '/', '\\');
	std::string out;
	bool lastWasSlash = false;
	for (char c : s) {
		bool isSlash = (c == '\\');
		if (isSlash) {
			if (!lastWasSlash) out.push_back('\\');
		}
		else out.push_back(c);
		lastWasSlash = isSlash;
	}
	// remove leading ".\" if present (we will resolve relative separately)
	return out;
}

// 将 wide string 转 utf8（简易）
static std::string wstr_to_utf8(const std::wstring& w) {
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conv;
	return conv.to_bytes(w);
}

// 根据 vmPath（可能带驱动字母、以 '\' 开头、或相对路径）映射到宿主路径
// - vmPath == nullptr -> return current drive root
// - 自动使用 g_cwds[] 为相对路径做解析
static std::string MapVMPathToHost(const char* vmPath) {
	std::call_once(g_init_flag, ensure_prime_drive_roots_initialized);

	if (!vmPath) {
		std::string base = g_cwds[g_currentDrive - 'A'];
		return normalize_slashes(base);
	}

	std::string s(vmPath);
	// trim both ends
	while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
	while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
	if (!s.empty() && (s.front() == '"' || s.front() == '\'')) s.erase(s.begin());
	if (!s.empty() && (s.back() == '"' || s.back() == '\'')) s.pop_back();

	// Windows-like "X:\..." or "X:/..."
	if (s.size() >= 2 && s[1] == ':') {
		char drive = static_cast<char>(std::toupper((unsigned char)s[0]));
		std::string rest = s.substr(2);
		if (!rest.empty() && (rest[0] == '\\' || rest[0] == '/')) rest.erase(rest.begin());
		std::string out = std::string("prime_data\\") + drive + "\\" + rest;
		return normalize_slashes(out);
	}

	// absolute path starting with \ or /
	if (!s.empty() && (s[0] == '\\' || s[0] == '/')) {
		std::string rest = s;
		while (!rest.empty() && (rest[0] == '\\' || rest[0] == '/')) rest.erase(rest.begin());
		std::string out = g_cwds[g_currentDrive - 'A'] + rest;
		return normalize_slashes(out);
	}

	// relative path -> join with current drive CWD
	std::string base = g_cwds[g_currentDrive - 'A'];
	std::string out = base + s;
	// canonicalize lexically (handle "." and "..")
	try {
		fs::path p(out);
		p = p.lexically_normal();
		return normalize_slashes(p.string());
	}
	catch (...) {
		return normalize_slashes(out);
	}
}

static std::string MapVMPathToHostW(const wchar_t* vmPath) {
	if (!vmPath) return MapVMPathToHost(nullptr);
	std::wstring ws(vmPath);
	return MapVMPathToHost(wstr_to_utf8(ws).c_str());
}

// helper: 将 vm 指定的 filename 映射，确保父目录存在（当 write/create 时）
static bool ensure_parent_dirs_for_hostpath(const std::string& hostPath) {
	try {
		fs::path p(hostPath);
		fs::path parent = p.parent_path();
		if (!parent.empty() && !fs::exists(parent)) {
			fs::create_directories(parent);
		}
		return true;
	}
	catch (...) {
		return false;
	}
}

// ====== 虚拟文件表（open/read/write/close/seek） ======

// 返回非 0 的 handle 表示成功
uint32_t _OpenFile(Arguments* args)
{
	std::call_once(g_init_flag, ensure_prime_drive_roots_initialized);

	const char* vmname = __GET(char*, args->r0);
	const char* vmflags = __GET(char*, args->r1);
	if (!vmname) return 0;
	std::string hostPath = MapVMPathToHost(vmname);
	std::string mode = vmflags ? std::string(vmflags) : std::string("rb");

	printf("    +VM name: %s\n    +flags: %s\n    +Mapped host path: %s\n",
		vmname, vmflags ? vmflags : "(null)", hostPath.c_str());

	// 如果是写模式，确保目录存在
	if (mode.find('w') != std::string::npos || mode.find('a') != std::string::npos || mode.find('x') != std::string::npos) {
		ensure_parent_dirs_for_hostpath(hostPath);
	}

	FILE* f = fopen(hostPath.c_str(), mode.c_str());
	if (!f) {
		// 试着在文本模式/二进制模式之间切换（容错）
		if (mode.find('b') == std::string::npos) {
			std::string m2 = mode + "b";
			f = fopen(hostPath.c_str(), m2.c_str());
			if (f) mode = m2;
		}
	}

	if (!f) {
		printf("    _OpenFile: fopen failed for %s\n", hostPath.c_str());
		return 0;
	}

	std::lock_guard<std::mutex> lk(g_vfile_mutex);
	uint32_t handle = g_next_handle++;
	g_vfile_table[handle] = VFile{ f, hostPath, mode };
	return handle;
}

uint32_t __wfopen(Arguments* args)
{
	std::call_once(g_init_flag, ensure_prime_drive_roots_initialized);

	const wchar_t* wname = __GET(wchar_t*, args->r0);
	const wchar_t* wflags = __GET(wchar_t*, args->r1);

	std::string hostPath = wname ? MapVMPathToHostW(wname) : MapVMPathToHost(nullptr);
	std::string mode = wflags ? wstr_to_utf8(std::wstring(wflags)) : std::string("rb");

	printf("    +__wfopen VM name: %ls\n    +flags: %ls\n    +Mapped host path: %s\n",
		wname ? wname : L"(null)", wflags ? wflags : L"(null)", hostPath.c_str());

	if (mode.find('w') != std::string::npos || mode.find('a') != std::string::npos) {
		ensure_parent_dirs_for_hostpath(hostPath);
	}
	if (!fs::exists(hostPath)) {
		fclose(fopen(hostPath.c_str(), "w"));
	}
	FILE* f = fopen(hostPath.c_str(), mode.c_str());
	if (!f) {
		if (mode.find('b') == std::string::npos) {
			std::string m2 = mode + "b";
			f = fopen(hostPath.c_str(), m2.c_str());
			if (f) mode = m2;
		}
	}
	if (!f)
		return 0;

	std::lock_guard<std::mutex> lk(g_vfile_mutex);
	uint32_t handle = g_next_handle++;
	g_vfile_table[handle] = VFile{ f, hostPath, mode };
	return handle;
}

uint32_t _CloseFile(Arguments* args)
{
	uint32_t handle = args->r0;
	std::lock_guard<std::mutex> lk(g_vfile_mutex);
	auto it = g_vfile_table.find(handle);
	if (it == g_vfile_table.end()) return 0;
	if (it->second.fp) fclose(it->second.fp);
	// g_vfile_table.erase(it);
	return 1;
}

// _ReadFile(handle, destVirtPtr, size) -> returns bytes read
uint32_t _ReadFile(Arguments* args)
{
	uint32_t handle = args->r0;
	VirtPtr destVPtr = args->r1;
	uint32_t size = args->r2;

	std::lock_guard<std::mutex> lk(g_vfile_mutex);
	auto it = g_vfile_table.find(handle);
	if (it == g_vfile_table.end()) return 0;
	FILE* f = it->second.fp;
	if (!f) return 0;

	void* dest = __GET(void*, destVPtr);
	if (!dest) return 0;

	size_t read = fread(dest, 1, (size_t)size, f);
	return static_cast<uint32_t>(read);
}

// _WriteFile(handle, srcVirtPtr, size) -> returns bytes written
uint32_t _WriteFile(Arguments* args)
{
	uint32_t handle = args->r0;
	VirtPtr srcVPtr = args->r1;
	uint32_t size = args->r2;

	std::lock_guard<std::mutex> lk(g_vfile_mutex);
	auto it = g_vfile_table.find(handle);
	if (it == g_vfile_table.end()) return 0;
	FILE* f = it->second.fp;
	if (!f) return 0;

	void* src = __GET(void*, srcVPtr);
	if (!src) return 0;

	size_t wrote = fwrite(src, 1, (size_t)size, f);
	fflush(f);
	return static_cast<uint32_t>(wrote);
}

// _SeekFile(handle, offset, origin) -> returns 0 on success, non-zero on error
// offset: int32 (args->r1) origin: 0 SEEK_SET, 1 SEEK_CUR, 2 SEEK_END (args->r2)
uint32_t _SeekFile(Arguments* args)
{
	uint32_t handle = args->r0;
	int32_t offset = static_cast<int32_t>(args->r1);
	int32_t origin = static_cast<int32_t>(args->r2);

	std::lock_guard<std::mutex> lk(g_vfile_mutex);
	auto it = g_vfile_table.find(handle);
	if (it == g_vfile_table.end()) return 1; // error
	FILE* f = it->second.fp;
	if (!f) return 1;

	int whence = SEEK_SET;
	if (origin == 1) whence = SEEK_CUR;
	else if (origin == 2) whence = SEEK_END;

	if (fseek(f, offset, whence) != 0) return 1;
	return 0;
}

// ====== 目录 & CWD 操作 ======

// _amkdir(vmName)
uint32_t _amkdir(Arguments* args)
{
	std::call_once(g_init_flag, ensure_prime_drive_roots_initialized);

	const char* vmname = __GET(char*, args->r0);
	std::string hostPath = MapVMPathToHost(vmname);
	printf("    +amkdir VM name: %s\n    +Mapped host path: %s\n", vmname ? vmname : "(null)", hostPath.c_str());

	try {
		fs::path p(hostPath);
		if (!fs::exists(p)) fs::create_directories(p);
		return 1;
	}
	catch (const std::exception& e) {
		printf("    +amkdir exception: %s\n", e.what());
		return 0;
	}
}

// _achdir(vmName) -> 会更新对应 drive 的 g_cwds，并可能改变 g_currentDrive（如果指定了驱动字母）
uint32_t _achdir(Arguments* args)
{
	std::call_once(g_init_flag, ensure_prime_drive_roots_initialized);

	const char* vmname = __GET(char*, args->r0);
	printf("    +achdir VM name: %s\n", vmname ? vmname : "(null)");

	if (!vmname || strlen(vmname) == 0) return 0;

	std::string s(vmname);
	// trim
	while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
	while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();

	// 指定 drive letter (X:\... 或 X:)
	if (s.size() >= 1 && s.size() <= 2 && s[1] == ':') {
		// "X:" 形式 -> 切换到驱动，但保持原来的 cwd for that drive
		char drive = static_cast<char>(std::toupper((unsigned char)s[0]));
		g_currentDrive = drive;
		return 1;
	}

	if (s.size() >= 2 && s[1] == ':') {
		// X:\something 或 X:relative
		char drive = static_cast<char>(std::toupper((unsigned char)s[0]));
		std::string rest = s.substr(2);
		if (!rest.empty() && (rest[0] == '\\' || rest[0] == '/')) rest.erase(rest.begin());
		std::string mapped = std::string("prime_data\\") + drive + "\\" + rest;
		try {
			fs::path p(mapped);
			p = p.lexically_normal();
			fs::create_directories(p); // ensure exists
			g_cwds[drive - 'A'] = normalize_slashes(p.string()) + (p.string().back() == '\\' ? "" : "\\");
			g_currentDrive = drive;
			return 1;
		}
		catch (...) { return 0; }
	}

	// 以 '\' 开头的绝对 path (无 drive) -> 解释为当前 drive 下的绝对路径
	if (!s.empty() && (s[0] == '\\' || s[0] == '/')) {
		std::string rest = s;
		while (!rest.empty() && (rest[0] == '\\' || rest[0] == '/')) rest.erase(rest.begin());
		std::string mapped = g_cwds[g_currentDrive - 'A'] + rest;
		try {
			fs::path p(mapped);
			p = p.lexically_normal();
			fs::create_directories(p);
			g_cwds[g_currentDrive - 'A'] = normalize_slashes(p.string()) + (p.string().back() == '\\' ? "" : "\\");
			return 1;
		}
		catch (...) { return 0; }
	}

	// 相对路径 -> 相对于当前 drive 的 cwd
	{
		std::string mapped = g_cwds[g_currentDrive - 'A'] + s;
		try {
			fs::path p(mapped);
			p = p.lexically_normal();
			fs::create_directories(p);
			g_cwds[g_currentDrive - 'A'] = normalize_slashes(p.string()) + (p.string().back() == '\\' ? "" : "\\");
			return 1;
		}
		catch (...) { return 0; }
	}
}

// 返回 active LCD 已存在实现中使用
uint32_t GetActiveLCD(Arguments* args)
{
	return sLCDHandler->GetActiveLCDPtr();
}

// ====== 简单的 INI 读取（_GetPrivateProfileString） ======
// signature from your code: (r0=dest buffer ptr, r1=appName, r2=keyName, [sp+8]=size, [sp+0xC]=filenamePtr)
uint32_t _GetPrivateProfileString(Arguments* args)
{
	std::call_once(g_init_flag, ensure_prime_drive_roots_initialized);

	const char* appName = __GET(char*, args->r0); // careful: in your original code r0 seemed to be appName earlier - we will follow original print behaviour
	const char* keyName = __GET(char*, args->r1);
	const char* def = __GET(char*, args->r2);
	int size = *__GET(int*, args->sp + 8);
	VirtPtr filenamePtr = *__GET(VirtPtr*, args->sp + 0xC);
	const char* filename = __GET(char*, filenamePtr);

	// In your earlier snippet you printed like: __GET(char*, args->r0) was appname,
	// but typical GetPrivateProfileString signature is (section, key, def, outBuf, size, filename)
	// To be robust, if the caller follows classic signature, we need to reinterpret:
	// We'll detect: if appName looks like a path (contains ':' or '\\') -> assume the call passed output buffer differently.
	// For simplicity here: assume filename is at sp+0xC as earlier, and args->r0 is section name.
	std::string hostPath = filename ? MapVMPathToHost(filename) : std::string();

	printf("    +appname: %s\n    +keyName: %s\n    +default: %s\n    +size: %i\n    +VM filename: %s\n    +Mapped host path: %s\n",
		appName ? appName : "(null)",
		keyName ? keyName : "(null)",
		def ? def : "(null)",
		size,
		filename ? filename : "(null)",
		hostPath.c_str()
	);

	// Attempt to open ini and look for section/key
	if (hostPath.empty()) return 0;

	try {
		FILE* f = fopen(hostPath.c_str(), "rb");
		if (!f) {
			// return default into destination buffer if possible
			return 0;
		}
		std::vector<char> content;
		fseek(f, 0, SEEK_END);
		long flen = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (flen > 0) {
			content.resize((size_t)flen + 1);
			fread(content.data(), 1, (size_t)flen, f);
		}
		fclose(f);
		content.back() = '\0';
		std::string ini(content.data());

		// Simple parser: find [section], then lines key=value
		std::string section = appName ? std::string(appName) : std::string();
		std::string key = keyName ? std::string(keyName) : std::string();
		std::string foundValue;
		size_t pos = 0;
		bool inSection = section.empty(); // if no section provided, search whole file
		while (pos < ini.size()) {
			// read line
			size_t eol = ini.find_first_of("\r\n", pos);
			std::string line = (eol == std::string::npos) ? ini.substr(pos) : ini.substr(pos, eol - pos);
			pos = (eol == std::string::npos) ? ini.size() : ini.find_first_not_of("\r\n", eol);

			// trim
			auto l = line;
			while (!l.empty() && isspace((unsigned char)l.front())) l.erase(l.begin());
			while (!l.empty() && isspace((unsigned char)l.back())) l.pop_back();
			if (l.empty()) continue;
			if (l.front() == ';' || l.front() == '#') continue;

			if (l.front() == '[' && l.back() == ']') {
				std::string name = l.substr(1, l.size() - 2);
				// trim
				while (!name.empty() && isspace((unsigned char)name.front())) name.erase(name.begin());
				while (!name.empty() && isspace((unsigned char)name.back())) name.pop_back();
				inSection = (name == section);
				continue;
			}

			if (inSection) {
				auto eq = l.find('=');
				if (eq != std::string::npos) {
					std::string k = l.substr(0, eq);
					std::string v = l.substr(eq + 1);
					// trim
					while (!k.empty() && isspace((unsigned char)k.front())) k.erase(k.begin());
					while (!k.empty() && isspace((unsigned char)k.back())) k.pop_back();
					while (!v.empty() && isspace((unsigned char)v.front())) v.erase(v.begin());
					while (!v.empty() && isspace((unsigned char)v.back())) v.pop_back();
					if (k == key) {
						foundValue = v;
						break;
					}
				}
			}
		}

		// write foundValue or default into destination buffer
		// Note: we must find the destination buffer pointer - typical signature is (section, key, default, outbuf, size, filename)
		// In your earlier print you used args->r0 etc. To avoid confusion, try to read dest from args->r3 or stack:
		// We'll try to detect destination pointer in r3 first (commonly used)
		VirtPtr outBufPtr = args->r3;
		char* outBuf = nullptr;
		if (outBufPtr != 0) outBuf = __GET(char*, outBufPtr);

		if (!outBuf) {
			// fallback: allocate nothing - return length
			return (uint32_t)(foundValue.empty() ? (def ? strlen(def) : 0) : foundValue.size());
		}

		const char* toWrite = foundValue.empty() ? (def ? def : "") : foundValue.c_str();
		size_t writeLen = strlen(toWrite);
		if (size > 0) {
			size_t copyLen = (writeLen >= (size_t)size) ? (size_t)size - 1 : writeLen;
			memcpy(outBuf, toWrite, copyLen);
			outBuf[copyLen] = '\0';
			return (uint32_t)copyLen;
		}

		return 0;
	}
	catch (...) {
		return 0;
	}
}
// ====== 简单的 INI 写入 (_SetPrivateProfileString) ======
// signature follows Windows API: WritePrivateProfileString(lpAppName, lpKeyName, lpString, lpFileName)
// We map this to: (r0=appName, r1=keyName, r2=stringToWrite, r3=filenamePtr)
uint32_t _SetPrivateProfileString(Arguments* args)
{
	std::call_once(g_init_flag, ensure_prime_drive_roots_initialized);

	// 1. Parse arguments based on the standard WritePrivateProfileString signature
	const char* appName = __GET(char*, args->r0);
	const char* keyName = __GET(char*, args->r1);
	const char* stringToWrite = __GET(char*, args->r2);
	const char* filename = __GET(char*, args->r3);

	std::string hostPath = filename ? MapVMPathToHost(filename) : std::string();

	printf("    +appname: %s\n    +keyName: %s\n    +string: %s\n    +VM filename: %s\n    +Mapped host path: %s\n",
		appName ? appName : "(null)",
		keyName ? keyName : "(null)",
		stringToWrite ? stringToWrite : "(null)",
		filename ? filename : "(null)",
		hostPath.c_str()
	);

	// Essential parameters validation
	if (hostPath.empty() || !appName) {
		return 0; // Failure (FALSE)
	}

	try {
		std::vector<std::string> lines;
		std::ifstream inFile(hostPath);
		if (inFile.is_open()) {
			std::string line;
			while (std::getline(inFile, line)) {
				// Handle Windows-style \r\n line endings
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				lines.push_back(line);
			}
			inFile.close();
		}

		// Find the target section and key
		int sectionStartLine = -1;
		int keyLine = -1;
		int sectionEndLine = -1; // Line after the last key of the section
		bool inSection = false;

		for (int i = 0; i < lines.size(); ++i) {
			std::string trimmedLine = lines[i];
			//trim(trimmedLine);

			if (trimmedLine.empty() || trimmedLine[0] == ';' || trimmedLine[0] == '#') {
				continue;
			}

			if (trimmedLine.front() == '[' && trimmedLine.back() == ']') {
				std::string currentSection = trimmedLine.substr(1, trimmedLine.length() - 2);
				//trim(currentSection);
				if (inSection) { // We were in the target section, and now we've hit a new one
					sectionEndLine = i;
					inSection = false;
				}
				if (currentSection == appName) {
					sectionStartLine = i;
					inSection = true;
				}
			}
			else if (inSection && keyName) {
				size_t equalsPos = trimmedLine.find('=');
				if (equalsPos != std::string::npos) {
					std::string currentKey = trimmedLine.substr(0, equalsPos);
					//trim(currentKey);
					if (currentKey == keyName) {
						keyLine = i;
					}
				}
			}
		}
		if (inSection) { // If the file ends while in the target section
			sectionEndLine = lines.size();
		}


		// 2. Perform modification in memory
		// Case 1: Delete an entire section (keyName and stringToWrite are NULL)
		if (!keyName && !stringToWrite) {
			if (sectionStartLine != -1) {
				lines.erase(lines.begin() + sectionStartLine, lines.begin() + sectionEndLine);
			}
		}
		// Case 2: Delete a key (stringToWrite is NULL)
		else if (keyName && !stringToWrite) {
			if (keyLine != -1) {
				lines.erase(lines.begin() + keyLine);
			}
		}
		// Case 3: Update or Add a key/value pair
		else if (keyName && stringToWrite) {
			std::string newPair = std::string(keyName) + "=" + std::string(stringToWrite);
			if (keyLine != -1) {
				// Update existing key
				lines[keyLine] = newPair;
			}
			else if (sectionStartLine != -1) {
				// Add new key to existing section
				lines.insert(lines.begin() + sectionEndLine, newPair);
			}
			else {
				// Add new section and new key
				if (!lines.empty() && !lines.back().empty()) {
					lines.push_back(""); // Add a blank line for separation
				}
				lines.push_back("[" + std::string(appName) + "]");
				lines.push_back(newPair);
			}
		}
		else {
			// Invalid combination of parameters (e.g., keyName is NULL but stringToWrite is not)
			return 0; // Failure
		}

		// 3. Write the modified content back to the file
		std::ofstream outFile(hostPath, std::ios::binary | std::ios::trunc);
		if (!outFile.is_open()) {
			return 0; // Failure
		}

		for (size_t i = 0; i < lines.size(); ++i) {
			outFile << lines[i];
			// Write standard CRLF line endings for compatibility
			outFile << "\r\n";
		}
		outFile.close();

		return 1; // Success (TRUE)
	}
	catch (...) {
		return 0; // Failure
	}
}

#define DUMPARGS printf("    r0: %08X|%i\n    r1: %08X|%i\n    r2: %08X|%i\n    r3: %08X|%i\n    r4: %08X|%i\n    sp: %08X\n", \
    args->r0, args->r0, args->r1, args->r1, args->r2,\
    args->r2, args->r3, args->r3, args->r4, args->r4, args->sp)

VirtPtr struc = 0;

uint32_t prgrmIsRunning(Arguments* args)
{
	printf("    program: %s\n", __GET(char*, args->r0));

	if (struc == 0)
		sMemoryManager->DyanmicAlloc(&struc, 0x250);

	return struc;
}
uint32_t getCurrentDir(Arguments*) {
	auto& cwd = g_cwds[g_currentDrive - 'A'];
	VirtPtr vp;
	sMemoryManager->DyanmicAlloc(&vp, cwd.size() + 1);
	std::memcpy(__GET(void*, vp), cwd.c_str(), cwd.size() + 1);
	printf("    +Current CWD: %s\n", cwd.c_str());
	return vp;
}

uint32_t _FindResourceW(Arguments* args)
{
	// TODO
	printf("Warn: FindResourceW stub!!!\n");
	return 0;
}

uint32_t _LoadLibraryA(Arguments* args)
{
	// TODO
	printf("Warn: LoadLibraryA stub!!!\n");
	return 0;
}

uint32_t _FreeLibrary(Arguments* args)
{
	// TODO
	printf("Warn: FreeLibrary stub!!!\n");
	return 0;
}

uint32_t OSInitCriticalSection(Arguments* args)
{
	sThreadHandler->InitCriticalSection(__GET(CriticalSection*, args->r0));
	return args->r0;
}

uint32_t OSEnterCriticalSection(Arguments* args)
{
	sThreadHandler->CurrentThreadEnterCriticalSection(__GET(CriticalSection*, args->r0));
	return args->r0;
}

uint32_t OSLeaveCriticalSection(Arguments* args)
{
	sThreadHandler->CurrentThreadExitCriticalSection(__GET(CriticalSection*, args->r0));
	return args->r0;
}

uint32_t OSSleep(Arguments* args)
{
	sThreadHandler->CurrentThreadSleep(args->r0);
	return args->r0;
}

struct EVENT
{
	uint32_t unk0 = 0x201;
	uint32_t unk1;
	uint16_t unk2;
	uint8_t unk3;
	uint8_t unk4;
	uint8_t unk5;
	uint8_t unk6;
	uint8_t unk7;
	uint8_t unk8;
	uint8_t unk9;
	uint8_t unk10;
	uint8_t unk11;
};

uint32_t OSCreateEvent(Arguments* args)
{
	VirtPtr allocAddr;
	sMemoryManager->DyanmicAlloc(&allocAddr, 0x14);
	EVENT* ptr = __GET(EVENT*, allocAddr);
	new (ptr) EVENT();

	ptr->unk1 = args->r1;
	ptr->unk2 = args->r0;

	return allocAddr;
}

uint32_t OSSetEvent(Arguments* args)
{
	DUMPARGS;
	return 0;
}

uint32_t LCDOn(Arguments* args)
{
	DUMPARGS;
	return 0;
}


uint32_t lcalloc(Arguments* args)
{
	//	printf("    +nElements: %i | size: %i\n", args->r0, args->r1);

	//uint32_t virt_addr = sExecutor->alloc_dynamic_mem(r0*r1);
	//auto addr = sExecutor->get_from_memory<void>(virt_addr);
	//memset(addr, 0, r0*r1);

	VirtPtr addr;
	if (sMemoryManager->DyanmicAlloc(&addr, args->r0 * args->r1) == ERROR_OK)
		return addr;

	return 0;
}

uint32_t lmalloc(Arguments* args)
{
	// printf("    +size: %i\n", args->r0);

	VirtPtr addr;
	if (sMemoryManager->DyanmicAlloc(&addr, args->r0) == ERROR_OK)
		return addr;

	return 0;
}

uint32_t lrealloc(Arguments* args)
{
	VirtPtr ptr = args->r0;
	uint32_t new_size = args->r1;

	if (ptr == 0) {
		sMemoryManager->DyanmicAlloc(&ptr, new_size);
		return ptr;
	}

	// printf("    +addr: %08X, size: %X\n", ptr, new_size);
	sMemoryManager->DynamicRealloc(&ptr, static_cast<size_t>(new_size));
	// printf("    +new_addr: %08X\n", ptr);
	return ptr;
}

uint32_t _lfree(Arguments* args)
{
	ErrorCode err;
	if ((err = sMemoryManager->DynamicFree(args->r0)) != ERROR_OK)
		printf("    +error\n");
	return args->r0;
}

uint32_t OSCreateThread(Arguments* args)
{
	DUMPARGS;
	return sThreadHandler->NewThread(args->r0, args->r4);
}

uint32_t OSSetThreadPriority(Arguments* args)
{
	DUMPARGS;
	sThreadHandler->SetThreadPriority(args->r0, args->r1);
	return 0;
}

struct SystemTime
{
	uint16_t Year;
	uint16_t Month;
	uint16_t DayOfWeek;
	uint16_t Day;
	uint16_t Hour;
	uint16_t Minute;
	uint16_t Second;
	uint16_t Milliseconds;
};

uint32_t GetSysTime(Arguments* args)
{
	SystemTime* sysTime = __GET(SystemTime*, args->r0);
	auto now = std::chrono::system_clock::now();
	auto now_c = std::chrono::system_clock::to_time_t(now);
	tm* parts = std::localtime(&now_c);

	sysTime->Year = parts->tm_yday;
	sysTime->Month = parts->tm_mon;
	sysTime->DayOfWeek = parts->tm_wday;
	sysTime->Day = parts->tm_mday;
	sysTime->Hour = parts->tm_hour;
	sysTime->Minute = parts->tm_min;
	sysTime->Second = parts->tm_sec;
	auto totalMSec = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
	sysTime->Milliseconds = static_cast<uint16_t>(totalMSec % 1000);

	return args->r0;
}

// append-write: _fwrite(handle, srcVirtPtr, size) -> bytes written (append to file end)
uint32_t _fwrite(Arguments* args)
{
	uint32_t handle = args->r3;
	VirtPtr srcVPtr = args->r0;
	uint32_t size = (size_t)args->r1 * (size_t)args->r2;

	if (handle == 0 || size == 0) return 0;

	std::lock_guard<std::mutex> lk(g_vfile_mutex);
	auto it = g_vfile_table.find(handle);
	if (it == g_vfile_table.end()) return 0;
	FILE* f = it->second.fp;
	if (!f) return 0;

	printf("    +_fwrite path: %s, size: %u\n", it->second.hostPath.c_str(), size);

	void* src = __GET(void*, srcVPtr);
	if (!src) return 0;

	//// seek to end for append semantics
	//if (fseek(f, 0, SEEK_END) != 0) {
	//	// seek failed
	//	return 0;
	//}

	size_t wrote = fwrite(src, 1, static_cast<size_t>(size), f);
	if (wrote > 0) fflush(f);

	return static_cast<uint32_t>(wrote);
}

// close: mirror of _CloseFile but named _fclose (returns 1 on success)
uint32_t _fclose(Arguments* args)
{
	uint32_t handle = args->r0;
	if (handle == 0) return 0;

	std::lock_guard<std::mutex> lk(g_vfile_mutex);
	auto it = g_vfile_table.find(handle);
	if (it == g_vfile_table.end()) return 0;

	if (it->second.fp) {
		fclose(it->second.fp);
		it->second.fp = nullptr;
	}
	// g_vfile_table.erase(it);
	return 1;
}

// --------- _filesize: return file size (clamped to uint32_t) ----------
static bool get_file_size_preserve_pos(FILE* f, uint64_t& out_size, uint64_t& out_orig_pos)
{
	if (!f) return false;

#if defined(_WIN32)
	__int64 cur = _ftelli64(f);
	if (cur == -1) return false;
	out_orig_pos = static_cast<uint64_t>(cur);
	if (_fseeki64(f, 0, SEEK_END) != 0) {
		_fseeki64(f, (long long)cur, SEEK_SET);
		return false;
	}
	__int64 size = _ftelli64(f);
	if (size == -1) {
		_fseeki64(f, (long long)cur, SEEK_SET);
		return false;
	}
	out_size = static_cast<uint64_t>(size);
	_fseeki64(f, (long long)cur, SEEK_SET);
	return true;
#elif defined(_POSIX_VERSION) || defined(__unix__) || defined(__APPLE__)
	off_t cur = ftello(f);
	if (cur == (off_t)-1) return false;
	out_orig_pos = static_cast<uint64_t>(cur);
	if (fseeko(f, 0, SEEK_END) != 0) {
		fseeko(f, cur, SEEK_SET);
		return false;
	}
	off_t size = ftello(f);
	if (size == (off_t)-1) {
		fseeko(f, cur, SEEK_SET);
		return false;
	}
	out_size = static_cast<uint64_t>(size);
	fseeko(f, cur, SEEK_SET);
	return true;
#else
	long cur = ftell(f);
	if (cur == -1L) return false;
	out_orig_pos = static_cast<uint64_t>(cur);
	if (fseek(f, 0, SEEK_END) != 0) {
		fseek(f, static_cast<long>(cur), SEEK_SET);
		return false;
	}
	long size = ftell(f);
	if (size == -1L) {
		fseek(f, static_cast<long>(cur), SEEK_SET);
		return false;
	}
	out_size = static_cast<uint64_t>(size);
	fseek(f, static_cast<long>(cur), SEEK_SET);
	return true;
#endif
}

uint32_t _filesize(Arguments* args)
{
	uint32_t handle = args->r0;
	if (handle == 0) return 0;

	std::lock_guard<std::mutex> lk(g_vfile_mutex);
	auto it = g_vfile_table.find(handle);
	if (it == g_vfile_table.end()) return 0;
	FILE* f = it->second.fp;
	if (!f) return 0;

	uint64_t size64 = 0, origPos = 0;
	if (!get_file_size_preserve_pos(f, size64, origPos)) return 0;

	if (size64 > UINT32_MAX) return UINT32_MAX;
	return static_cast<uint32_t>(size64);
}

// --------- _fread: read from current file pointer into VM memory ----------
uint32_t _fread(Arguments* args)
{
	uint32_t handle = args->r3;
	VirtPtr destVPtr = args->r0;
	uint32_t size = (size_t)args->r1 * (size_t)args->r2;

	if (handle == 0 || size == 0) return 0;

	std::lock_guard<std::mutex> lk(g_vfile_mutex);
	auto it = g_vfile_table.find(handle);
	if (it == g_vfile_table.end()) return 0;
	FILE* f = it->second.fp;
	if (!f) return 0;

	void* dest = __GET(void*, destVPtr);
	if (!dest) return 0;

	size_t read = fread(dest, 1, static_cast<size_t>(size), f);
	if (read == 0) {
		if (feof(f)) {
			// EOF
		}
		else if (ferror(f)) {
			clearerr(f);
		}
	}

	return static_cast<uint32_t>(read);
}
// Make sure UTF16 is defined. If it's not in your headers, add this.
// Assuming it's a 16-bit character type for UTF-16.
using UTF16 = char16_t;

// FAT attribute constants, as used by DOS/Windows APIs
#define AM_RDO  0x01 // Read-only
#define AM_HID  0x02 // Hidden
#define AM_SYS  0x04 // System
#define AM_VOL  0x08 // Volume label
#define AM_DIR  0x10 // Directory
#define AM_ARC  0x20 // Archive

// The find_context_t struct as defined in the prompt
typedef struct {
	VirtPtr unk0; // Handle for our internal context
	VirtPtr unk4; // Unused
	VirtPtr filename_lfn;
	VirtPtr filename;
	VirtPtr filename2_alt;
	size_t size;
	unsigned int mtime;
	unsigned int btime;
	unsigned int atime;
	unsigned char attrib_mask;
	unsigned char attrib;
} find_context_t;


// ====== With other global state variables ======

// Internal state for file searching operations
struct InternalFindContext {
	fs::directory_iterator iterator;
	fs::directory_iterator end_iterator;
	std::string pattern;
	int attrib_mask;
	VirtPtr current_lfn_vptr = 0;
	VirtPtr current_sfn_vptr = 0;
};

static std::mutex g_find_mutex;
static std::unordered_map<uintptr_t, InternalFindContext> g_find_contexts;
static uintptr_t g_next_find_handle = 1; // 0 is reserved for invalid
// Case-insensitive wildcard match for patterns like "*.txt"
static bool wildcard_match(const char* pattern, const char* text) {
	while (*pattern) {
		if (*pattern == '*') {
			// Skip consecutive '*'
			while (*(pattern + 1) == '*') pattern++;
			// If '*' is the last character, it's a match
			if (!*(pattern + 1)) return true;
			// Recurse: try to match from every point in `text`
			while (*text) {
				if (wildcard_match(pattern + 1, text)) return true;
				text++;
			}
			return false;
		}
		// If text is exhausted but pattern is not, no match
		if (!*text) return false;
		// Match '?' with any char, or literal chars (case-insensitively)
		if (*pattern != '?' && std::tolower((unsigned char)*pattern) != std::tolower((unsigned char)*text)) {
			return false;
		}
		pattern++;
		text++;
	}
	// If pattern is exhausted, it's a match only if text is also exhausted
	return !*text;
}

// Generates a DOS-like 8.3 short filename (simplified)
static std::string create_sfn(const std::string& lfn) {
	std::string name, ext;
	size_t dot_pos = lfn.find_last_of('.');

	if (dot_pos != std::string::npos && dot_pos > 0 && dot_pos < lfn.length() - 1) {
		name = lfn.substr(0, dot_pos);
		ext = lfn.substr(dot_pos + 1);
	}
	else {
		name = lfn;
	}

	// Sanitize and shorten name and extension
	auto sanitize = [](std::string& s, size_t max_len) {
		s.erase(std::remove_if(s.begin(), s.end(), [](char c) {
			return !(isalnum((unsigned char)c) || c == '_');
			}), s.end());
		if (s.length() > max_len) {
			s.resize(max_len);
		}
		};

	sanitize(name, 8);
	sanitize(ext, 3);

	std::string sfn_str = name;
	if (!ext.empty()) {
		sfn_str += "." + ext;
	}

	std::transform(sfn_str.begin(), sfn_str.end(), sfn_str.begin(), ::toupper);
	return sfn_str;
}

// Converts a filesystem time point to a 32-bit packed DOS date/time value
static uint32_t pack_dos_datetime(const fs::file_time_type& ftime) {
	// This conversion to time_t is implementation-defined before C++20, but works on major platforms
	auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
		ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
	);
	std::time_t c_time = std::chrono::system_clock::to_time_t(sctp);

	// Use localtime_s on Windows for thread safety
#ifdef _WIN32
	std::tm tm_buf;
	if (localtime_s(&tm_buf, &c_time) != 0) return 0;
	std::tm* tm = &tm_buf;
#else
	std::tm* tm = std::localtime(&c_time);
	if (!tm) return 0;
#endif

	uint16_t year = (tm->tm_year >= 80) ? (tm->tm_year - 80) : 0;
	uint16_t month = tm->tm_mon + 1;
	uint16_t day = tm->tm_mday;
	uint16_t dos_date = (year << 9) | (month << 5) | day;

	uint16_t hour = tm->tm_hour;
	uint16_t minute = tm->tm_min;
	uint16_t second = tm->tm_sec / 2;
	uint16_t dos_time = (hour << 11) | (minute << 5) | second;

	return (static_cast<uint32_t>(dos_date) << 16) | dos_time;
}

// Gets FAT-like attributes from a directory entry
static uint8_t get_fat_attributes(const fs::directory_entry& entry) {
	uint8_t attribs = 0;
	std::error_code ec;

	if (fs::is_directory(entry.status(ec))) {
		attribs |= AM_DIR;
	}
	else {
		attribs |= AM_ARC; // Default for files
	}

	auto perms = entry.status(ec).permissions();
	if ((perms & fs::perms::owner_write) == fs::perms::none &&
		(perms & fs::perms::group_write) == fs::perms::none &&
		(perms & fs::perms::others_write) == fs::perms::none) {
		attribs |= AM_RDO;
	}

	// Emulate hidden files for names starting with a dot (common on Unix)
	if (!entry.path().filename().string().empty() && entry.path().filename().string()[0] == '.') {
		attribs |= AM_HID;
	}

	return attribs;
}

// Forward declarations for the internal implementation functions
short find_next_internal(VirtPtr ctx_ptr);
int find_close_internal(VirtPtr ctx_ptr);


// Internal implementation for _afindfirst and _wfindfirst
short find_first_internal(const std::string& vm_path_pattern, VirtPtr ctx_vptr, int attrib_mask) {
	std::call_once(g_init_flag, ensure_prime_drive_roots_initialized);

	// 1. Map the VM path pattern to a host path
	std::string host_path_str = MapVMPathToHost(vm_path_pattern.c_str());
	fs::path host_path(host_path_str);
	std::error_code ec;

	fs::path dir_to_scan;
	std::string pattern;

	// 2. Separate directory from file pattern
	if (fs::is_directory(host_path, ec)) {
		dir_to_scan = host_path;
		pattern = "*";
	}
	else {
		dir_to_scan = host_path.parent_path();
		pattern = host_path.filename().string();
	}

	if (dir_to_scan.empty() || dir_to_scan.string() == ".") {
		dir_to_scan = MapVMPathToHost(nullptr); // Use CWD
	}

	printf("    +findfirst: searching dir '%s' for pattern '%s'\n", dir_to_scan.string().c_str(), pattern.c_str());

	// 3. Create and store an internal context for the search
	InternalFindContext internal_ctx;
	internal_ctx.pattern = pattern;
	internal_ctx.attrib_mask = attrib_mask;
	internal_ctx.iterator = fs::directory_iterator(dir_to_scan, fs::directory_options::skip_permission_denied, ec);
	if (ec) {
		printf("    +findfirst: directory not found or error: %s\n", ec.message().c_str());
		return -1;
	}
	internal_ctx.end_iterator = fs::directory_iterator();

	uintptr_t handle;
	{
		std::lock_guard<std::mutex> lock(g_find_mutex);
		handle = g_next_find_handle++;
		g_find_contexts.emplace(handle, std::move(internal_ctx));
	}

	// 4. Link guest context to our internal context
	find_context_t* guest_ctx = __GET(find_context_t*, ctx_vptr);
	if (!guest_ctx) {
		std::lock_guard<std::mutex> lock(g_find_mutex);
		g_find_contexts.erase(handle);
		return -1;
	}
	guest_ctx->unk0 = (uint32_t)handle;
	guest_ctx->unk4 = (uint32_t)0xdeadbeef;

	// 5. Find the first match
	return find_next_internal(ctx_vptr);
}

// Internal implementation for _afindnext and _wfindnext
short find_next_internal(VirtPtr ctx_vptr) {
	find_context_t* guest_ctx = __GET(find_context_t*, ctx_vptr);
	if (!guest_ctx) return -1;
	uintptr_t handle = (uintptr_t)guest_ctx->unk0;

	std::lock_guard<std::mutex> lock(g_find_mutex);
	auto it = g_find_contexts.find(handle);
	if (it == g_find_contexts.end()) return -1;
	InternalFindContext& internal_ctx = it->second;

	// Free memory from previous find in the same context
	if (internal_ctx.current_sfn_vptr) {
		sMemoryManager->DynamicFree(internal_ctx.current_sfn_vptr);
		internal_ctx.current_sfn_vptr = 0;
	}
	if (internal_ctx.current_lfn_vptr) {
		sMemoryManager->DynamicFree(internal_ctx.current_lfn_vptr);
		internal_ctx.current_lfn_vptr = 0;
	}

	// Loop through directory to find a matching entry
	while (internal_ctx.iterator != internal_ctx.end_iterator) {
		const auto& entry = *internal_ctx.iterator;
		std::string filename_u8 = entry.path().filename().string();

		// Filter by filename pattern
		if (!wildcard_match(internal_ctx.pattern.c_str(), filename_u8.c_str())) {
			internal_ctx.iterator++;
			continue;
		}

		// Filter by attributes
		uint8_t fat_attribs = get_fat_attributes(entry);
		if (internal_ctx.attrib_mask != 0 && (fat_attribs & internal_ctx.attrib_mask) == 0) {
			internal_ctx.iterator++;
			continue;
		}

		// --- MATCH FOUND ---
		printf("    +findnext: found '%s'\n", filename_u8.c_str());

		// Populate guest context structure
		std::error_code ec;
		guest_ctx->size = fs::is_regular_file(entry.status(ec)) ? fs::file_size(entry, ec) : 0;
		guest_ctx->mtime = pack_dos_datetime(fs::last_write_time(entry, ec));
		guest_ctx->btime = fs::exists(entry.path(), ec) && !ec ? pack_dos_datetime(fs::last_write_time(entry, ec)) : 0; // Placeholder for btime
		guest_ctx->atime = guest_ctx->mtime; // Placeholder for atime

		guest_ctx->attrib = fat_attribs;
		guest_ctx->attrib_mask = (unsigned char)internal_ctx.attrib_mask;

		// Allocate VM memory for filenames and copy them
		std::string sfn = create_sfn(filename_u8);
		if (sMemoryManager->DyanmicAlloc(&internal_ctx.current_sfn_vptr, sfn.length() + 1) == ERROR_OK) {
			char* sfn_buf = __GET(char*, internal_ctx.current_sfn_vptr);
			strcpy(sfn_buf, sfn.c_str());
			guest_ctx->filename = internal_ctx.current_sfn_vptr;
			guest_ctx->filename2_alt = internal_ctx.current_sfn_vptr;
		}

		std::u16string lfn = entry.path().filename().u16string();
		if (sMemoryManager->DyanmicAlloc(&internal_ctx.current_lfn_vptr, (lfn.length() + 1) * sizeof(UTF16)) == ERROR_OK) {
			UTF16* lfn_buf = __GET(UTF16*, internal_ctx.current_lfn_vptr);
			memcpy(lfn_buf, lfn.c_str(), (lfn.length() + 1) * sizeof(UTF16));
			guest_ctx->filename_lfn = internal_ctx.current_lfn_vptr;
		}

		internal_ctx.iterator++; // Prepare for the next call
		return 0; // Success
	}

	return -1; // No more files
}

// Internal implementation for _findclose
int find_close_internal(VirtPtr ctx_vptr) {
	find_context_t* guest_ctx = __GET(find_context_t*, ctx_vptr);
	if (!guest_ctx || guest_ctx->unk0 == 0) return 0; // Already closed or invalid
	uintptr_t handle = (uintptr_t)guest_ctx->unk0;

	std::lock_guard<std::mutex> lock(g_find_mutex);
	auto it = g_find_contexts.find(handle);
	if (it == g_find_contexts.end()) return 0; // Not found

	InternalFindContext& internal_ctx = it->second;

	// Free any allocated VM memory
	if (internal_ctx.current_sfn_vptr) {
		sMemoryManager->DynamicFree(internal_ctx.current_sfn_vptr);
	}
	if (internal_ctx.current_lfn_vptr) {
		sMemoryManager->DynamicFree(internal_ctx.current_lfn_vptr);
	}

	g_find_contexts.erase(it);
	printf("    +_findclose: closed handle %zu\n", handle);

	// Invalidate the guest context
	guest_ctx->unk0 = 0;
	guest_ctx->filename = 0;
	guest_ctx->filename_lfn = 0;
	guest_ctx->filename2_alt = 0;

	return 0;
}

// ====== Public API Implementations (add these to your handler map) ======

uint32_t _afindfirst(Arguments* args) {
	const char* fnmatch = __GET(char*, args->r0);
	VirtPtr ctx_vptr = args->r1;
	int attrib_mask = args->r2;

	if (!fnmatch || !ctx_vptr) return (uint32_t)-1;

	return find_first_internal(std::string(fnmatch), ctx_vptr, attrib_mask);
}
#undef _wfindfirst
uint32_t _wfindfirst(Arguments* args) {
	const wchar_t* w_fnmatch = __GET(wchar_t*, args->r0);
	VirtPtr ctx_vptr = args->r1;
	int attrib_mask = args->r2;

	if (!w_fnmatch || !ctx_vptr) return (uint32_t)-1;

	// Convert wide string pattern to UTF-8 for internal processing
	std::string u8_fnmatch = wstr_to_utf8(w_fnmatch);

	return find_first_internal(u8_fnmatch, ctx_vptr, attrib_mask);
}

uint32_t _afindnext(Arguments* args) {
	VirtPtr ctx_vptr = args->r0;
	if (!ctx_vptr) return (uint32_t)-1;
	return find_next_internal(ctx_vptr);
}
#undef _wfindnext
uint32_t _wfindnext(Arguments* args) {
	// This is interchangeable with _afindnext
	VirtPtr ctx_vptr = args->r0;
	if (!ctx_vptr) return (uint32_t)-1;
	return find_next_internal(ctx_vptr);
}

uint32_t _findclose(Arguments* args) {
	VirtPtr ctx_vptr = args->r0;
	if (!ctx_vptr) return (uint32_t)-1;
	return find_close_internal(ctx_vptr);
}

uint32_t GetEvent(Arguments* args)
{
	// TODO: This is a stub function, as the original code does not provide a full implementation.
	// printf("Warn: GetEvent stub!!!\n");
	return 0; // Idk
}

/**
 * @brief Deletes a file specified by an ASCII/UTF-8 path.
 * @param args r0 contains a virtual pointer to the null-terminated path string.
 * @return 0 on success, non-zero on failure.
 */
uint32_t _aremove(Arguments* args)
{
	// 1. Get the virtual path from arguments.
	const char* vmPath = __GET(char*, args->r0);
	if (!vmPath) {
		return -1; // Return non-zero for error (invalid argument).
	}

	// 2. Map the virtual path to a sandboxed host system path.
	std::string hostPath = MapVMPathToHost(vmPath);

	printf("    +_aremove VM path: '%s', Mapped host path: '%s'\n", vmPath, hostPath.c_str());

	try {
		std::error_code ec;
		// 3. Attempt to remove the file or empty directory.
		fs::remove(hostPath, ec);

		// 4. Check for errors. fs::remove sets ec on failure.
		if (ec) {
			printf("    +_aremove failed: %s\n", ec.message().c_str());
			return -1; // Failure.
		}

		return 0; // Success.
	}
	catch (const fs::filesystem_error& e) {
		// Catch potential exceptions from filesystem operations.
		printf("    +_aremove exception: %s\n", e.what());
		return -1; // Failure.
	}
}

/**
 * @brief Deletes a file specified by a wide-character (UTF-16) path.
 * @param args r0 contains a virtual pointer to the null-terminated wide-character path string.
 * @return 0 on success, non-zero on failure.
 */
uint32_t _wremove(Arguments* args)
{
	// 1. Get the virtual path from arguments.
	const wchar_t* wVmPath = __GET(wchar_t*, args->r0);
	if (!wVmPath) {
		return -1; // Return non-zero for error.
	}

	// 2. Map the wide-character virtual path to a sandboxed host system path.
	std::string hostPath = MapVMPathToHostW(wVmPath);

	// For logging purposes, convert the wide string to a printable UTF-8 string.
	printf("    +_wremove Mapped host path: '%s'\n", hostPath.c_str());

	try {
		std::error_code ec;
		// 3. Attempt to remove the file or empty directory.
		fs::remove(hostPath, ec);

		// 4. Check for errors.
		if (ec) {
			printf("    +_wremove failed: %s\n", ec.message().c_str());
			return -1; // Failure.
		}

		return 0; // Success.
	}
	catch (const fs::filesystem_error& e) {
		// Catch potential exceptions.
		printf("    +_wremove exception: %s\n", e.what());
		return -1; // Failure.
	}
}
static std::unordered_map<uint32_t, std::string> g_vdev_table;
static uint32_t g_next_dev_handle = 1; // 0 保留为失败/无效
uint32_t CreateFile(Arguments* args) {
	std::cout << "    +CreateFile_stub name:" << __GET(char*, args->r0) << "\n";
	g_vdev_table[++g_next_dev_handle] = __GET(char*, args->r0); // Store the device name in the map
	return g_next_dev_handle;
}
// 辅助函数：Hex dump
static void HexDump(const void* data, size_t size) {
	const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
	for (size_t i = 0; i < size; i += 16) {
		std::cout << std::setw(4) << std::setfill('0') << std::hex << i << "  ";
		for (size_t j = 0; j < 16 && i + j < size; ++j) {
			std::cout << std::setw(2) << static_cast<int>(p[i + j]) << " ";
		}
		std::cout << " ";
		for (size_t j = 0; j < 16 && i + j < size; ++j) {
			char c = static_cast<char>(p[i + j]);
			std::cout << (std::isprint(static_cast<unsigned char>(c)) ? c : '.');
		}
		std::cout << "\n";
	}
	std::cout << std::dec; // 恢复默认输出格式
}

uint32_t DeviceIoControl(Arguments* args) {
	uint32_t handle = args->r0;
	uint32_t request = args->r1;
	char* in = __GET(char*, args->r2);
	uint32_t size = args->r3;
	char* out = __GET(char*, *__GET(uint32_t*, args->sp + 8));
	int outlen = *__GET(int*, args->sp + 12);
	uint32_t* retlen = __GET(uint32_t*,*__GET(int*, args->sp + 16));
	void* overlapped = __GET(void*, *__GET(int*, args->sp + 20));

	std::cout << "    +DeviceIoControl_stub handle:" << handle
		<< " request:" << request
		<< " size:" << size << "\n";

	// 打印 ioctl 缓冲区内容
	std::cout << "    ioctl buffer dump:\n";
	HexDump(in, size);
	return 1; // Simulate success
}
uint32_t CloseHandle(Arguments* args) {
	uint32_t handle = args->r0;
	if (handle == 0) return 0; // Invalid handle
	std::cout << "    +CloseHandle_stub handle:" << handle << "\n";
	auto it = g_vdev_table.find(handle);
	if (it != g_vdev_table.end()) {
		g_vdev_table.erase(it); // Remove the device from the map
		return 1; // Success
	}
	return 0; // Failure, handle not found
}