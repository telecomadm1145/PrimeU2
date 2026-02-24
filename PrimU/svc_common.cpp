#include "svc_common.h"

#include <fstream>

// ================================================================
//  VMPath 实现
// ================================================================

static std::array<std::string, 26> g_cwds;
static char g_currentDrive = 'A';
static std::once_flag g_init_flag;

void VMPath::init() {
  std::call_once(g_init_flag, []() {
    for (int i = 0; i < 26; ++i) {
      std::string root = "prime_data\\";
      root.push_back(static_cast<char>('A' + i));
      root += "\\";
      g_cwds[i] = root;
      try {
        if (i == 0 || i == 2)
          fs::create_directories(root);
      } catch (...) {
      }
    }
  });
}

std::string VMPath::normalize_slashes(std::string s) {
  std::replace(s.begin(), s.end(), '/', '\\');
  std::string out;
  bool lastWasSlash = false;
  for (char c : s) {
    bool isSlash = (c == '\\');
    if (isSlash) {
      if (!lastWasSlash)
        out.push_back('\\');
    } else
      out.push_back(c);
    lastWasSlash = isSlash;
  }
  return out;
}

std::string VMPath::wstr_to_utf8(const std::wstring &w) {
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conv;
  return conv.to_bytes(w);
}

std::wstring VMPath::utf8_to_wstr(const std::string &s) {
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conv;
  return conv.from_bytes(s);
}

std::string VMPath::toHost(const char *vmPath) {
  init();

  if (!vmPath) {
    return normalize_slashes(g_cwds[g_currentDrive - 'A']);
  }

  std::string s(vmPath);
  // trim
  while (!s.empty() && isspace((unsigned char)s.front()))
    s.erase(s.begin());
  while (!s.empty() && isspace((unsigned char)s.back()))
    s.pop_back();
  if (!s.empty() && (s.front() == '"' || s.front() == '\''))
    s.erase(s.begin());
  if (!s.empty() && (s.back() == '"' || s.back() == '\''))
    s.pop_back();

  // "X:\..." or "X:/..."
  if (s.size() >= 2 && s[1] == ':') {
    char drive = static_cast<char>(std::toupper((unsigned char)s[0]));
    std::string rest = s.substr(2);
    if (!rest.empty() && (rest[0] == '\\' || rest[0] == '/'))
      rest.erase(rest.begin());
    return normalize_slashes(std::string("prime_data\\") + drive + "\\" + rest);
  }

  // absolute path starting with '\' or '/'
  if (!s.empty() && (s[0] == '\\' || s[0] == '/')) {
    std::string rest = s;
    while (!rest.empty() && (rest[0] == '\\' || rest[0] == '/'))
      rest.erase(rest.begin());
    return normalize_slashes(g_cwds[g_currentDrive - 'A'] + rest);
  }

  // relative path
  std::string base = g_cwds[g_currentDrive - 'A'];
  std::string out = base + s;
  try {
    fs::path p(out);
    p = p.lexically_normal();
    return normalize_slashes(p.string());
  } catch (...) {
    return normalize_slashes(out);
  }
}

std::wstring VMPath::toHostW(const wchar_t *vmPath) {
  if (!vmPath)
    return utf8_to_wstr(toHost(nullptr));
  return utf8_to_wstr(toHost(wstr_to_utf8(std::wstring(vmPath)).c_str()));
}

std::string VMPath::toVM(const char *hostPath) {
  init();

  if (!hostPath) {
    return std::string(1, g_currentDrive) + ":\\";
  }

  std::string s(hostPath);
  std::string lower = s;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });

  const std::string prefix = normalize_slashes("prime_data\\");
  if (lower.size() > prefix.size() &&
      lower.compare(0, prefix.size(), prefix) == 0) {
    char drive = (char)std::toupper((unsigned char)lower[prefix.size()]);
    size_t pos = prefix.size() + 1;
    if (pos < lower.size() && (lower[pos] == '\\' || lower[pos] == '/')) {
      pos++;
    }
    std::string rest = s.substr(pos);
    return std::string(1, drive) + ":\\" + rest;
  }

  for (int i = 0; i < 26; ++i) {
    std::string cwdPrefix = normalize_slashes(g_cwds[i]);
    std::string lowerPrefix = cwdPrefix;
    std::transform(lowerPrefix.begin(), lowerPrefix.end(), lowerPrefix.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });

    if (lower.size() >= lowerPrefix.size() &&
        lower.compare(0, lowerPrefix.size(), lowerPrefix) == 0) {
      std::string rest = s.substr(cwdPrefix.size());
      char drive = (char)('A' + i);
      return std::string(1, drive) + ":\\" + rest;
    }
  }

  return "";
}

bool VMPath::chdir(const std::string &vmDirSpec) {
  init();

  std::string s = vmDirSpec;
  // trim
  while (!s.empty() && isspace((unsigned char)s.front()))
    s.erase(s.begin());
  while (!s.empty() && isspace((unsigned char)s.back()))
    s.pop_back();
  if (s.empty())
    return false;

  // "X:" only — switch drive
  if (s.size() >= 1 && s.size() <= 2 && s[1] == ':') {
    g_currentDrive = static_cast<char>(std::toupper((unsigned char)s[0]));
    return true;
  }

  auto applyPath = [](const std::string &mapped, char drive) -> bool {
    try {
      fs::path p(mapped);
      p = p.lexically_normal();
      fs::create_directories(p);
      std::string pstr = p.string();
      g_cwds[drive - 'A'] =
          normalize_slashes(pstr) + ((pstr.back() == '\\') ? "" : "\\");
      g_currentDrive = drive;
      return true;
    } catch (...) {
      return false;
    }
  };

  // "X:\something" or "X:relative"
  if (s.size() >= 2 && s[1] == ':') {
    char drive = static_cast<char>(std::toupper((unsigned char)s[0]));
    std::string rest = s.substr(2);
    if (!rest.empty() && (rest[0] == '\\' || rest[0] == '/'))
      rest.erase(rest.begin());
    return applyPath(std::string("prime_data\\") + drive + "\\" + rest, drive);
  }

  // Absolute path starting with '\'
  if (!s.empty() && (s[0] == '\\' || s[0] == '/')) {
    std::string rest = s;
    while (!rest.empty() && (rest[0] == '\\' || rest[0] == '/'))
      rest.erase(rest.begin());
    return applyPath(g_cwds[g_currentDrive - 'A'] + rest, g_currentDrive);
  }

  // Relative path
  return applyPath(g_cwds[g_currentDrive - 'A'] + s, g_currentDrive);
}

bool VMPath::mkdir(const std::string &hostPath) {
  try {
    fs::path p(hostPath);
    if (!fs::exists(p))
      fs::create_directories(p);
    return true;
  } catch (...) {
    return false;
  }
}

bool VMPath::rmdir(const std::string &hostPath) {
  try {
    fs::remove(fs::path(hostPath));
    return true;
  } catch (...) {
    return false;
  }
}

bool VMPath::ensureParentDirs(const std::wstring &hostPath) {
  try {
    fs::path parent = fs::path(hostPath).parent_path();
    if (!parent.empty() && !fs::exists(parent))
      fs::create_directories(parent);
    return true;
  } catch (...) {
    return false;
  }
}

bool VMPath::ensureParentDirs(const std::string &hostPath) {
  return ensureParentDirs(utf8_to_wstr(hostPath));
}

// ================================================================
//  VFileSystem 实现
// ================================================================

VFileSystem &VFileSystem::instance() {
  static VFileSystem inst;
  return inst;
}

uint32_t VFileSystem::open(const char *vmPath, const char *mode) {
  VMPath::init();

  const char *vmname = vmPath;
  if (!vmname)
    return 0;

  std::wstring hostPath = VMPath::utf8_to_wstr(VMPath::toHost(vmname));
  std::wstring wMode = mode ? VMPath::utf8_to_wstr(mode) : std::wstring(L"rb");

  printf("    +VM name: %s\n    +flags: %s\n    +Mapped host path: %ls\n",
         vmname, mode ? mode : "(null)", hostPath.c_str());

  // 如果是写模式，确保目录存在
  if (wMode.find('w') != std::wstring::npos ||
      wMode.find('a') != std::wstring::npos ||
      wMode.find('x') != std::wstring::npos) {
    VMPath::ensureParentDirs(hostPath);
  }

  FILE *f = _wfopen(hostPath.c_str(), wMode.c_str());
  if (!f) {
    wprintf(L"    _OpenFile: fopen failed for %s\n", hostPath.c_str());
    return 0;
  }

  std::lock_guard lk(m_mtx);
  uint32_t handle = m_next++;
  m_files[handle] = VFile{f, hostPath, wMode};
  return handle;
}

uint32_t VFileSystem::openW(const wchar_t *vmPath, const wchar_t *mode) {
  VMPath::init();

  if (!vmPath)
    return 0;

  std::wstring hostPath = VMPath::toHostW(vmPath);
  std::wstring wMode = mode ? std::wstring(mode) : std::wstring(L"rb");

  wprintf(L"    +__wfopen VM name: %ls\n    +flags: %ls\n    +Mapped host "
          L"path: %s\n",
          vmPath, mode ? mode : L"(null)", hostPath.c_str());

  if (wMode.find('w') != std::wstring::npos ||
      wMode.find('a') != std::wstring::npos) {
    VMPath::ensureParentDirs(hostPath);
  }

  FILE *f = _wfopen(hostPath.c_str(), wMode.c_str());
  if (!f)
    return 0;

  std::lock_guard lk(m_mtx);
  uint32_t handle = m_next++;
  m_files[handle] = VFile{f, hostPath, wMode};
  return handle;
}

uint32_t VFileSystem::close(uint32_t handle) {
  if (handle == 0)
    return 0;

  std::lock_guard lk(m_mtx);
  auto it = m_files.find(handle);
  if (it == m_files.end())
    return 0;

  if (it->second.fp) {
    fclose(it->second.fp);
    it->second.fp = nullptr;
  }
  return 1;
}

uint32_t VFileSystem::filesize(uint32_t handle) {
  if (handle == 0)
    return 0;

  std::lock_guard lk(m_mtx);
  auto it = m_files.find(handle);
  if (it == m_files.end() || !it->second.fp)
    return 0;

  FILE *f = it->second.fp;
#if defined(_WIN32)
  __int64 cur = _ftelli64(f);
  if (cur == -1)
    return 0;
  if (_fseeki64(f, 0, SEEK_END) != 0) {
    _fseeki64(f, cur, SEEK_SET);
    return 0;
  }
  __int64 size = _ftelli64(f);
  _fseeki64(f, cur, SEEK_SET);
  if (size == -1)
    return 0;
  if (size > UINT32_MAX)
    return UINT32_MAX;
  return static_cast<uint32_t>(size);
#else
  long cur = ftell(f);
  if (cur == -1L)
    return 0;
  if (fseek(f, 0, SEEK_END) != 0) {
    fseek(f, cur, SEEK_SET);
    return 0;
  }
  long size = ftell(f);
  fseek(f, cur, SEEK_SET);
  if (size == -1L)
    return 0;
  return static_cast<uint32_t>(size);
#endif
}

uint32_t VFileSystem::seek(uint32_t handle, size_t offset, int whence) {
  if (handle == 0)
    return (uint32_t)-1;

  std::lock_guard lk(m_mtx);
  auto it = m_files.find(handle);
  if (it == m_files.end() || !it->second.fp)
    return (uint32_t)-1;

  
  return _fseeki64(it->second.fp, (long long)offset, whence);
}

uint32_t VFileSystem::tell(uint32_t handle) {
  if (handle == 0)
    return (uint32_t)-1;

  std::lock_guard lk(m_mtx);
  auto it = m_files.find(handle);
  if (it == m_files.end() || !it->second.fp)
    return (uint32_t)-1;

  return (uint32_t)ftell(it->second.fp);
}

size_t VFileSystem::read(uint32_t handle, void *dest, size_t size,
                         size_t count) {
  if (handle == 0 || size == 0 || !dest)
    return 0;

  std::lock_guard lk(m_mtx);
  auto it = m_files.find(handle);
  if (it == m_files.end() || !it->second.fp)
    return 0;

  printf("    +_fread path: %ls, size: %zu, count: %zu\n",
         it->second.hostPath.c_str(), size, count);

  size_t nread = fread(dest, size, count, it->second.fp);
  if (nread == 0) {
    if (ferror(it->second.fp))
      clearerr(it->second.fp);
  }
  return nread;
}

size_t VFileSystem::write(uint32_t handle, const void *src, size_t size,
                          size_t count) {
  if (handle == 0 || size == 0 || !src)
    return 0;

  std::lock_guard lk(m_mtx);
  auto it = m_files.find(handle);
  if (it == m_files.end() || !it->second.fp)
    return 0;

  printf("    +_fwrite path: %ls, size: %zu, count: %zu\n",
         it->second.hostPath.c_str(), size, count);
  return fwrite(src, size, count, it->second.fp);
}
