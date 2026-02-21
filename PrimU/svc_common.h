#pragma once

#include "stdafx.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <codecvt>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <locale>
#include <mutex>
#include <string>
#include <unordered_map>


#include "InterruptHandler.h"
#include "Marshal.h"
#include "MemoryManager.h"
#include "Services.h"
#include "common.h"


namespace fs = std::filesystem;

// ================================================================
//  VMPath — 虚拟路径 <-> 宿主路径映射与目录管理
// ================================================================

namespace VMPath {

void init();

// 核心映射
std::string toHost(const char *vmPath);
std::wstring toHostW(const wchar_t *vmPath);
std::string toVM(const char *hostPath);

// 目录操作
bool chdir(const std::string &vmDirSpec);
bool mkdir(const std::string &hostPath);
bool rmdir(const std::string &hostPath);
bool ensureParentDirs(const std::wstring &hostPath);
bool ensureParentDirs(const std::string &hostPath);

// 字符串工具
std::string normalize_slashes(std::string s);
std::string wstr_to_utf8(const std::wstring &w);
std::wstring utf8_to_wstr(const std::string &s);

} // namespace VMPath

// ================================================================
//  VFileSystem — 虚拟文件系统（thread-safe 句柄管理）
// ================================================================

class VFileSystem {
public:
  static VFileSystem &instance();

  // 打开文件，返回句柄 (0 = 失败)
  uint32_t open(const char *vmPath, const char *mode);
  uint32_t openW(const wchar_t *vmPath, const wchar_t *mode);

  // 关闭句柄，返回 1 成功 / 0 失败
  uint32_t close(uint32_t handle);

  // 文件操作（自动锁定、校验句柄）
  uint32_t filesize(uint32_t handle);
  uint32_t seek(uint32_t handle, size_t offset, int whence);
  uint32_t tell(uint32_t handle);
  size_t read(uint32_t handle, void *dest, size_t size, size_t count);
  size_t write(uint32_t handle, const void *src, size_t size, size_t count);

  // 获取底层 FILE*（加锁条件下）— 用于需要直接操作 FILE* 的遗留代码
  template <typename Fn>
  auto with(uint32_t handle, Fn &&fn)
      -> decltype(fn(std::declval<FILE *>(), std::declval<std::wstring &>())) {
    std::lock_guard lk(m_mtx);
    auto it = m_files.find(handle);
    if (it == m_files.end() || !it->second.fp) {
      using R =
          decltype(fn(std::declval<FILE *>(), std::declval<std::wstring &>()));
      if constexpr (std::is_void_v<R>)
        return;
      else
        return R{};
    }
    return fn(it->second.fp, it->second.hostPath);
  }

private:
  VFileSystem() = default;

  struct VFile {
    FILE *fp = nullptr;
    std::wstring hostPath;
    std::wstring mode;
  };

  std::mutex m_mtx;
  std::unordered_map<uint32_t, VFile> m_files;
  uint32_t m_next = 0xDEAD'0001;
};
