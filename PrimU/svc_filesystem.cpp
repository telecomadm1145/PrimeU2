// svc_filesystem.cpp — 文件 I/O、目录、查找、INI、路径 handler
#include "svc_common.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <vector>

#include "ThreadHandler.h"
#include "executor.h"

// ================================================================
//  文件 I/O handler — AutoBind thunk 封装
// ================================================================

static uint32_t afopen_impl(const char *vmname, const char *mode) {
  return VFileSystem::instance().open(vmname, mode);
}
uint32_t __afopen(SystemServiceArguments *args) {
  return AutoBind<decltype(afopen_impl)>::thunk<afopen_impl>(args);
}

static uint32_t wfopen_impl(const wchar_t *vmname, const wchar_t *mode) {
  return VFileSystem::instance().openW(vmname, mode);
}
uint32_t __wfopen(SystemServiceArguments *args) {
  return AutoBind<decltype(wfopen_impl)>::thunk<wfopen_impl>(args);
}

uint32_t _fclose(SystemServiceArguments *args) {
  return VFileSystem::instance().close(args->r0);
}

static uint32_t fread_impl(void *dest, size_t size, size_t count,
                           uint32_t handle) {
  return static_cast<uint32_t>(
      VFileSystem::instance().read(handle, dest, size, count));
}
uint32_t _fread(SystemServiceArguments *args) {
  return AutoBind<decltype(fread_impl)>::thunk<fread_impl>(args);
}

static uint32_t fwrite_impl(void *src, size_t size, size_t count,
                            uint32_t handle) {
  return static_cast<uint32_t>(
      VFileSystem::instance().write(handle, src, size, count));
}
uint32_t _fwrite(SystemServiceArguments *args) {
  return AutoBind<decltype(fwrite_impl)>::thunk<fwrite_impl>(args);
}

static uint32_t fseek_impl(uint32_t handle, size_t offset, int whence) {
  return VFileSystem::instance().seek(handle, offset, whence);
}
uint32_t __fseek(SystemServiceArguments *args) {
  return AutoBind<decltype(fseek_impl)>::thunk<fseek_impl>(args);
}

uint32_t __ftell(SystemServiceArguments *args) {
  return VFileSystem::instance().tell(args->r0);
}

static uint32_t filesize_impl(uint32_t handle) {
  return VFileSystem::instance().filesize(handle);
}
uint32_t _filesize(SystemServiceArguments *args) {
  return AutoBind<decltype(filesize_impl)>::thunk<filesize_impl>(args);
}

// ================================================================
//  目录操作 handler
// ================================================================

uint32_t _amkdir(SystemServiceArguments *args) {
  VMPath::init();
  const char *vmname = __GET(char *, args->r0);
  printf("    +amkdir VM name: %s\n", vmname ? vmname : "(null)");
  auto hostPath = VMPath::toHost(vmname);
  printf("    +Mapped host path: %s\n", hostPath.c_str());
  return VMPath::mkdir(hostPath) ? 1 : 0;
}

uint32_t _wmkdir(SystemServiceArguments *args) {
  VMPath::init();
  const wchar_t *vmname = __GET(wchar_t *, args->r0);
  wprintf(L"    +wmkdir VM name: %ls\n", vmname ? vmname : L"(null)");
  auto hostPath = VMPath::toHostW(vmname);
  wprintf(L"    +Mapped host path: %s\n", hostPath.c_str());
  return VMPath::mkdir(VMPath::wstr_to_utf8(hostPath)) ? 1 : 0;
}

uint32_t _wrmdir(SystemServiceArguments *args) {
  VMPath::init();
  const wchar_t *vmname = __GET(wchar_t *, args->r0);
  wprintf(L"    +wrmdir VM name: %ls\n", vmname ? vmname : L"(null)");
  auto hostPath = VMPath::toHostW(vmname);
  wprintf(L"    +Mapped host path: %s\n", hostPath.c_str());
  return VMPath::rmdir(VMPath::wstr_to_utf8(hostPath)) ? 1 : 0;
}

uint32_t _achdir(SystemServiceArguments *args) {
  const char *vmname = __GET(char *, args->r0);
  printf("    +achdir VM name: %s\n", vmname ? vmname : "(null)");
  if (!vmname || strlen(vmname) == 0)
    return 0;
  return VMPath::chdir(std::string(vmname)) ? 1 : 0;
}

uint32_t _wchdir(SystemServiceArguments *args) {
  const wchar_t *vmname = __GET(wchar_t *, args->r0);
  wprintf(L"    +wchdir VM name: %ls\n", vmname ? vmname : L"(null)");
  if (!vmname || wcslen(vmname) == 0)
    return 0;
  return VMPath::chdir(VMPath::wstr_to_utf8(std::wstring(vmname))) ? 1 : 0;
}

// ================================================================
//  文件删除 handler
// ================================================================

uint32_t _aremove(SystemServiceArguments *args) {
  const char *vmPath = __GET(char *, args->r0);
  if (!vmPath)
    return (uint32_t)-1;
  std::string hostPath = VMPath::toHost(vmPath);
  printf("    +_aremove VM path: '%s', Mapped host path: '%s'\n", vmPath,
         hostPath.c_str());
  try {
    std::error_code ec;
    fs::remove(hostPath, ec);
    if (ec) {
      printf("    +_aremove failed: %s\n", ec.message().c_str());
      return (uint32_t)-1;
    }
    return 0;
  } catch (const fs::filesystem_error &e) {
    printf("    +_aremove exception: %s\n", e.what());
    return (uint32_t)-1;
  }
}

uint32_t _wremove(SystemServiceArguments *args) {
  const wchar_t *wVmPath = __GET(wchar_t *, args->r0);
  if (!wVmPath)
    return (uint32_t)-1;
  std::wstring hostPath = VMPath::toHostW(wVmPath);
  wprintf(L"    +_wremove Mapped host path: '%s'\n", hostPath.c_str());
  try {
    std::error_code ec;
    fs::remove(hostPath, ec);
    if (ec) {
      printf("    +_wremove failed: %s\n", ec.message().c_str());
      return (uint32_t)-1;
    }
    return 0;
  } catch (const fs::filesystem_error &e) {
    printf("    +_wremove exception: %s\n", e.what());
    return (uint32_t)-1;
  }
}

// ================================================================
//  INI 操作 handler
// ================================================================

uint32_t _GetPrivateProfileString(SystemServiceArguments *args) {
  VMPath::init();

  const char *appName = __GET(char *, args->r0);
  const char *keyName = __GET(char *, args->r1);
  const char *def = __GET(char *, args->r2);
  int size = *__GET(int *, args->sp + 8);
  VirtPtr filenamePtr = *__GET(VirtPtr *, args->sp + 0xC);
  const char *filename = __GET(char *, filenamePtr);

  std::string hostPath = filename ? VMPath::toHost(filename) : std::string();

  printf("    +appname: %s\n    +keyName: %s\n    +default: %s\n    +size: "
         "%i\n    +VM filename: %s\n    +Mapped host path: %s\n",
         appName ? appName : "(null)", keyName ? keyName : "(null)",
         def ? def : "(null)", size, filename ? filename : "(null)",
         hostPath.c_str());

  if (hostPath.empty())
    return 0;

  try {
    FILE *f = fopen(hostPath.c_str(), "rb");
    if (!f)
      return 0;

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

    std::string section = appName ? std::string(appName) : std::string();
    std::string key = keyName ? std::string(keyName) : std::string();
    std::string foundValue;
    size_t pos = 0;
    bool inSection = section.empty();

    while (pos < ini.size()) {
      size_t eol = ini.find_first_of("\r\n", pos);
      std::string line = (eol == std::string::npos)
                             ? ini.substr(pos)
                             : ini.substr(pos, eol - pos);
      pos = (eol == std::string::npos) ? ini.size()
                                       : ini.find_first_not_of("\r\n", eol);

      auto l = line;
      while (!l.empty() && isspace((unsigned char)l.front()))
        l.erase(l.begin());
      while (!l.empty() && isspace((unsigned char)l.back()))
        l.pop_back();
      if (l.empty())
        continue;
      if (l.front() == ';' || l.front() == '#')
        continue;

      if (l.front() == '[' && l.back() == ']') {
        std::string name = l.substr(1, l.size() - 2);
        while (!name.empty() && isspace((unsigned char)name.front()))
          name.erase(name.begin());
        while (!name.empty() && isspace((unsigned char)name.back()))
          name.pop_back();
        inSection = (name == section);
        continue;
      }

      if (inSection) {
        auto eq = l.find('=');
        if (eq != std::string::npos) {
          std::string k = l.substr(0, eq);
          std::string v = l.substr(eq + 1);
          while (!k.empty() && isspace((unsigned char)k.front()))
            k.erase(k.begin());
          while (!k.empty() && isspace((unsigned char)k.back()))
            k.pop_back();
          while (!v.empty() && isspace((unsigned char)v.front()))
            v.erase(v.begin());
          while (!v.empty() && isspace((unsigned char)v.back()))
            v.pop_back();
          if (k == key) {
            foundValue = v;
            break;
          }
        }
      }
    }

    VirtPtr outBufPtr = args->r3;
    char *outBuf = nullptr;
    if (outBufPtr != 0)
      outBuf = __GET(char *, outBufPtr);

    if (!outBuf) {
      return (uint32_t)(foundValue.empty() ? (def ? strlen(def) : 0)
                                           : foundValue.size());
    }

    const char *toWrite =
        foundValue.empty() ? (def ? def : "") : foundValue.c_str();
    size_t writeLen = strlen(toWrite);
    if (size > 0) {
      size_t copyLen = (writeLen >= (size_t)size) ? (size_t)size - 1 : writeLen;
      memcpy(outBuf, toWrite, copyLen);
      outBuf[copyLen] = '\0';
      return (uint32_t)copyLen;
    }
    return 0;
  } catch (...) {
    return 0;
  }
}

uint32_t _SetPrivateProfileString(SystemServiceArguments *args) {
  VMPath::init();

  const char *appName = __GET(char *, args->r0);
  const char *keyName = __GET(char *, args->r1);
  const char *stringToWrite = __GET(char *, args->r2);
  const char *filename = __GET(char *, args->r3);

  std::string hostPath = filename ? VMPath::toHost(filename) : std::string();

  printf("    +appname: %s\n    +keyName: %s\n    +string: %s\n    +VM "
         "filename: %s\n    +Mapped host path: %s\n",
         appName ? appName : "(null)", keyName ? keyName : "(null)",
         stringToWrite ? stringToWrite : "(null)",
         filename ? filename : "(null)", hostPath.c_str());

  if (hostPath.empty() || !appName)
    return 0;

  try {
    std::vector<std::string> lines;
    std::ifstream inFile(hostPath);
    if (inFile.is_open()) {
      std::string line;
      while (std::getline(inFile, line)) {
        if (!line.empty() && line.back() == '\r')
          line.pop_back();
        lines.push_back(line);
      }
      inFile.close();
    }

    int sectionStartLine = -1, keyLine = -1, sectionEndLine = -1;
    bool inSec = false;

    for (int i = 0; i < (int)lines.size(); ++i) {
      std::string trimmedLine = lines[i];
      if (trimmedLine.empty() || trimmedLine[0] == ';' || trimmedLine[0] == '#')
        continue;

      if (trimmedLine.front() == '[' && trimmedLine.back() == ']') {
        std::string currentSection =
            trimmedLine.substr(1, trimmedLine.length() - 2);
        if (inSec) {
          sectionEndLine = i;
          inSec = false;
        }
        if (currentSection == appName) {
          sectionStartLine = i;
          inSec = true;
        }
      } else if (inSec && keyName) {
        size_t equalsPos = trimmedLine.find('=');
        if (equalsPos != std::string::npos) {
          std::string currentKey = trimmedLine.substr(0, equalsPos);
          if (currentKey == keyName)
            keyLine = i;
        }
      }
    }
    if (inSec)
      sectionEndLine = (int)lines.size();

    if (!keyName && !stringToWrite) {
      if (sectionStartLine != -1)
        lines.erase(lines.begin() + sectionStartLine,
                    lines.begin() + sectionEndLine);
    } else if (keyName && !stringToWrite) {
      if (keyLine != -1)
        lines.erase(lines.begin() + keyLine);
    } else if (keyName && stringToWrite) {
      std::string newPair =
          std::string(keyName) + "=" + std::string(stringToWrite);
      if (keyLine != -1) {
        lines[keyLine] = newPair;
      } else if (sectionStartLine != -1) {
        lines.insert(lines.begin() + sectionEndLine, newPair);
      } else {
        if (!lines.empty() && !lines.back().empty())
          lines.push_back("");
        lines.push_back("[" + std::string(appName) + "]");
        lines.push_back(newPair);
      }
    } else {
      return 0;
    }

    std::ofstream outFile(hostPath, std::ios::binary | std::ios::trunc);
    if (!outFile.is_open())
      return 0;
    for (size_t i = 0; i < lines.size(); ++i)
      outFile << lines[i] << "\r\n";
    outFile.close();
    return 1;
  } catch (...) {
    return 0;
  }
}

// ================================================================
//  查找操作 (find)
// ================================================================

using UTF16 = char16_t;

#define AM_RDO 0x01
#define AM_HID 0x02
#define AM_SYS 0x04
#define AM_VOL 0x08
#define AM_DIR 0x10
#define AM_ARC 0x20

typedef struct {
  VirtPtr unk0;
  VirtPtr unk4;
  VirtPtr filename_lfn;
  VirtPtr filename;
  VirtPtr filename2_alt;
  uint32_t size;
  unsigned int mtime;
  unsigned int btime;
  unsigned int atime;
  unsigned char attrib_mask;
  unsigned char attrib;
} find_context_t;

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
static uintptr_t g_next_find_handle = 1;

static bool wildcard_match(const char *pattern, const char *text) {
  while (*pattern) {
    if (*pattern == '*') {
      while (*(pattern + 1) == '*')
        pattern++;
      if (!*(pattern + 1))
        return true;
      while (*text) {
        if (wildcard_match(pattern + 1, text))
          return true;
        text++;
      }
      return false;
    }
    if (!*text)
      return false;
    if (*pattern != '?' && std::tolower(*pattern) != std::tolower(*text))
      return false;
    pattern++;
    text++;
  }
  return !*text;
}

static std::string create_sfn(const std::string &lfn) {
  std::string name, ext;
  size_t dot_pos = lfn.find_last_of('.');
  if (dot_pos != std::string::npos && dot_pos > 0 &&
      dot_pos < lfn.length() - 1) {
    name = lfn.substr(0, dot_pos);
    ext = lfn.substr(dot_pos + 1);
  } else {
    name = lfn;
  }

  auto sanitize = [](std::string &s, size_t max_len) {
    s.erase(std::remove_if(s.begin(), s.end(),
                           [](char c) {
                             return !(isalnum((unsigned char)c) || c == '_');
                           }),
            s.end());
    if (s.length() > max_len)
      s.resize(max_len);
  };
  sanitize(name, 8);
  sanitize(ext, 3);

  std::string sfn_str = name;
  if (!ext.empty())
    sfn_str += "." + ext;
  std::transform(sfn_str.begin(), sfn_str.end(), sfn_str.begin(), ::toupper);
  return sfn_str;
}

static uint32_t pack_dos_datetime(const fs::file_time_type &ftime) {
  auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      ftime - fs::file_time_type::clock::now() +
      std::chrono::system_clock::now());
  std::time_t c_time = std::chrono::system_clock::to_time_t(sctp);
#ifdef _WIN32
  std::tm tm_buf;
  if (localtime_s(&tm_buf, &c_time) != 0)
    return 0;
  std::tm *tm = &tm_buf;
#else
  std::tm *tm = std::localtime(&c_time);
  if (!tm)
    return 0;
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

static uint8_t get_fat_attributes(const fs::directory_entry &entry) {
  uint8_t attribs = 0;
  std::error_code ec;
  if (fs::is_directory(entry.status(ec)))
    attribs |= AM_DIR;
  else
    attribs |= AM_ARC;
  auto perms = entry.status(ec).permissions();
  if ((perms & fs::perms::owner_write) == fs::perms::none &&
      (perms & fs::perms::group_write) == fs::perms::none &&
      (perms & fs::perms::others_write) == fs::perms::none)
    attribs |= AM_RDO;
  return attribs;
}

static short find_next_internal(VirtPtr ctx_vptr);
static int find_close_internal(VirtPtr ctx_vptr);

static short find_first_internal(const std::string &vm_path_pattern,
                                 VirtPtr ctx_vptr, int attrib_mask) {
  VMPath::init();

  // Use wide string path to avoid Windows code page issues with fs::path
  std::wstring host_path_w =
      VMPath::utf8_to_wstr(VMPath::toHost(vm_path_pattern.c_str()));
  fs::path host_path(host_path_w);
  std::error_code ec;

  fs::path dir_to_scan;
  std::string pattern;
  if (fs::is_directory(host_path, ec)) {
    dir_to_scan = host_path;
    pattern = "*";
  } else {
    dir_to_scan = host_path.parent_path();
    pattern = host_path.filename().string();
  }

  if (dir_to_scan.empty() || dir_to_scan.string() == ".")
    dir_to_scan = fs::path(VMPath::utf8_to_wstr(VMPath::toHost(nullptr)));

  printf("    +findfirst: searching dir '%s' for pattern '%s'\n",
         dir_to_scan.string().c_str(), pattern.c_str());

  InternalFindContext internal_ctx;
  internal_ctx.pattern = pattern;
  internal_ctx.attrib_mask = attrib_mask;
  internal_ctx.iterator = fs::directory_iterator(
      dir_to_scan, fs::directory_options::skip_permission_denied, ec);
  if (ec) {
    printf("    +findfirst: directory not found or error: %s\n",
           ec.message().c_str());
    return -1;
  }
  internal_ctx.end_iterator = fs::directory_iterator();

  uintptr_t handle;
  {
    std::lock_guard<std::mutex> lock(g_find_mutex);
    handle = g_next_find_handle++;
    g_find_contexts.emplace(handle, std::move(internal_ctx));
  }

  find_context_t *guest_ctx = __GET(find_context_t *, ctx_vptr);
  if (!guest_ctx) {
    std::lock_guard<std::mutex> lock(g_find_mutex);
    g_find_contexts.erase(handle);
    return -1;
  }
  guest_ctx->unk0 = (uint32_t)handle;
  guest_ctx->unk4 = (uint32_t)0xdeadbeef;
  return find_next_internal(ctx_vptr);
}

static short find_next_internal(VirtPtr ctx_vptr) {
  find_context_t *guest_ctx = __GET(find_context_t *, ctx_vptr);
  if (!guest_ctx)
    return -1;
  uintptr_t handle = (uintptr_t)guest_ctx->unk0;

  std::lock_guard<std::mutex> lock(g_find_mutex);
  auto it = g_find_contexts.find(handle);
  if (it == g_find_contexts.end())
    return -1;
  InternalFindContext &internal_ctx = it->second;

  if (internal_ctx.current_sfn_vptr) {
    sMemoryManager->DynamicFree(internal_ctx.current_sfn_vptr);
    internal_ctx.current_sfn_vptr = 0;
  }
  if (internal_ctx.current_lfn_vptr) {
    sMemoryManager->DynamicFree(internal_ctx.current_lfn_vptr);
    internal_ctx.current_lfn_vptr = 0;
  }

  while (internal_ctx.iterator != internal_ctx.end_iterator) {
    const auto &entry = *internal_ctx.iterator;
    std::string filename_u8 =
        VMPath::wstr_to_utf8(entry.path().filename().wstring());

    if (!wildcard_match(internal_ctx.pattern.c_str(), filename_u8.c_str())) {
      internal_ctx.iterator++;
      continue;
    }

    uint8_t fat_attribs = get_fat_attributes(entry);
    if (internal_ctx.attrib_mask != 0 &&
        (fat_attribs & internal_ctx.attrib_mask) == 0) {
      internal_ctx.iterator++;
      continue;
    }

    printf("    +findnext: found '%s'\n", filename_u8.c_str());

    std::error_code ec;
    guest_ctx->size = fs::is_regular_file(entry.status(ec))
                          ? (uint32_t)fs::file_size(entry, ec)
                          : 0;
    guest_ctx->mtime = pack_dos_datetime(fs::last_write_time(entry, ec));
    guest_ctx->btime = fs::exists(entry.path(), ec) && !ec
                           ? pack_dos_datetime(fs::last_write_time(entry, ec))
                           : 0;
    guest_ctx->atime = guest_ctx->mtime;
    guest_ctx->attrib = fat_attribs;
    guest_ctx->attrib_mask = (unsigned char)internal_ctx.attrib_mask;

    std::string sfn = create_sfn(filename_u8);
    if (sMemoryManager->DyanmicAlloc(&internal_ctx.current_sfn_vptr,
                                     sfn.length() + 1) == ERROR_OK) {
      char *sfn_buf = __GET(char *, internal_ctx.current_sfn_vptr);
      strcpy(sfn_buf, sfn.c_str());
      guest_ctx->filename = internal_ctx.current_sfn_vptr;
      guest_ctx->filename2_alt = internal_ctx.current_sfn_vptr;
    }

    std::u16string lfn = entry.path().filename().u16string();
    if (sMemoryManager->DyanmicAlloc(&internal_ctx.current_lfn_vptr,
                                     (lfn.length() + 1) * sizeof(UTF16)) ==
        ERROR_OK) {
      UTF16 *lfn_buf = __GET(UTF16 *, internal_ctx.current_lfn_vptr);
      memcpy(lfn_buf, lfn.c_str(), (lfn.length() + 1) * sizeof(UTF16));
      guest_ctx->filename_lfn = internal_ctx.current_lfn_vptr;
    }

    internal_ctx.iterator++;
    return 0;
  }
  return -1;
}

static int find_close_internal(VirtPtr ctx_vptr) {
  find_context_t *guest_ctx = __GET(find_context_t *, ctx_vptr);
  if (!guest_ctx || guest_ctx->unk0 == 0)
    return 0;
  uintptr_t handle = (uintptr_t)guest_ctx->unk0;

  std::lock_guard<std::mutex> lock(g_find_mutex);
  auto it = g_find_contexts.find(handle);
  if (it == g_find_contexts.end())
    return 0;
  InternalFindContext &internal_ctx = it->second;

  if (internal_ctx.current_sfn_vptr)
    sMemoryManager->DynamicFree(internal_ctx.current_sfn_vptr);
  if (internal_ctx.current_lfn_vptr)
    sMemoryManager->DynamicFree(internal_ctx.current_lfn_vptr);

  g_find_contexts.erase(it);
  printf("    +_findclose: closed handle %zu\n", handle);
  guest_ctx->unk0 = 0;
  guest_ctx->filename = 0;
  guest_ctx->filename_lfn = 0;
  guest_ctx->filename2_alt = 0;
  return 0;
}

uint32_t _afindfirst(SystemServiceArguments *args) {
  const char *fnmatch = __GET(char *, args->r0);
  VirtPtr ctx_vptr = args->r1;
  int attrib_mask = args->r2;
  if (!fnmatch || !ctx_vptr)
    return (uint32_t)-1;
  return find_first_internal(std::string(fnmatch), ctx_vptr, attrib_mask);
}

#undef _wfindfirst
uint32_t _wfindfirst(SystemServiceArguments *args) {
  const wchar_t *w_fnmatch = __GET(wchar_t *, args->r0);
  VirtPtr ctx_vptr = args->r1;
  int attrib_mask = args->r2;
  if (!w_fnmatch || !ctx_vptr)
    return (uint32_t)-1;
  return find_first_internal(VMPath::wstr_to_utf8(w_fnmatch), ctx_vptr,
                             attrib_mask);
}

uint32_t _afindnext(SystemServiceArguments *args) {
  VirtPtr ctx_vptr = args->r0;
  if (!ctx_vptr)
    return (uint32_t)-1;
  return find_next_internal(ctx_vptr);
}

#undef _wfindnext
uint32_t _wfindnext(SystemServiceArguments *args) {
  VirtPtr ctx_vptr = args->r0;
  if (!ctx_vptr)
    return (uint32_t)-1;
  return find_next_internal(ctx_vptr);
}

uint32_t _findclose(SystemServiceArguments *args) {
  VirtPtr ctx_vptr = args->r0;
  if (!ctx_vptr)
    return (uint32_t)-1;
  return find_close_internal(ctx_vptr);
}

// ================================================================
//  路径拆分/合并
// ================================================================

uint32_t _afnsplit(SystemServiceArguments *args) {
  const char *path = __GET(const char *, args->r0);
  char *drive = __GET(char *, args->r1);
  char *dir = __GET(char *, args->r2);
  char *name = __GET(char *, args->r3);
  char *ext = __GET(char *, args->sp + 8);

  if (!path) {
    if (drive)
      drive[0] = '\0';
    if (dir)
      dir[0] = '\0';
    if (name)
      name[0] = '\0';
    if (ext)
      ext[0] = '\0';
    return (uint32_t)-1;
  }

  std::string s(path);
  std::replace(s.begin(), s.end(), '/', '\\');

  if (drive)
    drive[0] = '\0';
  if (dir)
    dir[0] = '\0';
  if (name)
    name[0] = '\0';
  if (ext)
    ext[0] = '\0';

  size_t pos = 0;
  if (s.size() >= 2 && std::isalpha((unsigned char)s[0]) && s[1] == ':') {
    if (drive) {
      drive[0] = s[0];
      drive[1] = ':';
      drive[2] = '\0';
    }
    pos = 2;
  }

  size_t last_slash = s.find_last_of('\\');
  std::string dir_str, filename;
  if (last_slash == std::string::npos || last_slash < pos) {
    filename = s.substr(pos);
  } else {
    dir_str = s.substr(pos, last_slash - pos + 1);
    filename = s.substr(last_slash + 1);
  }

  if (pos == 0 && !s.empty() && s[0] == '\\' && dir_str.empty()) {
    size_t second_slash = s.find('\\', 1);
    if (second_slash != std::string::npos) {
      dir_str = s.substr(0, second_slash + 1);
      filename = s.substr(second_slash + 1);
    }
  }

  std::string name_str, ext_str;
  if (!filename.empty()) {
    size_t last_dot = filename.find_last_of('.');
    if (last_dot == std::string::npos || last_dot == 0) {
      name_str = filename;
    } else {
      name_str = filename.substr(0, last_dot);
      ext_str = filename.substr(last_dot);
    }
  }

  if (dir && !dir_str.empty())
    std::strcpy(dir, dir_str.c_str());
  if (name && !name_str.empty())
    std::strcpy(name, name_str.c_str());
  if (ext && !ext_str.empty())
    std::strcpy(ext, ext_str.c_str());
  return 0;
}

uint32_t _afnmerge(SystemServiceArguments *args) {
  char *outPath = __GET(char *, args->r0);
  const char *drive = __GET(const char *, args->r1);
  const char *dir = __GET(const char *, args->r2);
  const char *name = __GET(const char *, args->r3);
  const char *ext = __GET(char *, *__GET(VirtPtr *, args->sp + 0x8));

  if (!outPath)
    return (uint32_t)-1;

  std::string out;
  if (drive && drive[0]) {
    std::string drv(drive);
    if (drv.size() == 1 && std::isalpha((unsigned char)drv[0]))
      drv.push_back(':');
    out += drv;
  }
  if (dir && dir[0]) {
    std::string d(dir);
    std::replace(d.begin(), d.end(), '/', '\\');
    out += d;
  }
  bool need_sep = false;
  if ((dir && dir[0]) && (name && name[0])) {
    size_t len = std::string(dir).length();
    if (len == 0 || dir[len - 1] != '\\')
      need_sep = true;
  }
  if (need_sep)
    out.push_back('\\');
  if (name && name[0])
    out += name;
  if (ext && ext[0]) {
    std::string e(ext);
    if (e[0] != '.')
      out.push_back('.');
    out += e;
  }
  std::replace(out.begin(), out.end(), '/', '\\');
  std::strcpy(outPath, out.c_str());
  return 0;
}
