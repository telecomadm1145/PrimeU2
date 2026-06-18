# PrimU2

<p align="center">
  <b>HP Prime High-Level Emulator</b><br>
  <i>基于 Unicorn Engine 构建的 HP Prime 计算器 HLE 模拟器</i>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-Windows-blue?style=flat-square" alt="Platform">
  <img src="https://img.shields.io/badge/arch-x86__64-orange?style=flat-square" alt="Architecture">
  <img src="https://img.shields.io/badge/license-GPL--v2-green?style=flat-square" alt="License">
  <img src="https://img.shields.io/badge/firmware-20250915-red?style=flat-square" alt="Firmware">
</p>

<p align="center">
  <b>中文</b> | <a href="README.en.md">English</a>
</p>

---

## 简介

**PrimU2** 是一个面向 **HP Prime** 图形计算器（V1 / V2 / G1）的高级别模拟器（HLE, High-Level Emulator）。与全系统模拟不同，PrimU2 通过拦截固件的系统服务调用（SVC）并在主机上直接实现这些调用来运行计算器固件，从而避免了对底层硬件的完整模拟。

核心 CPU 仿真由 [Unicorn Engine](https://github.com/unicorn-engine/unicorn) 提供，PrimU2 在此基础上叠加了内存管理、文件系统虚拟化、多线程调度、LCD 显示和输入处理等较高层次的抽象。

> **注意：** PrimU2 目前仅针对 HP Prime 固件版本 **20250915** 进行测试，其他版本不受官方支持。

---

## 特性

### ✅ 已实现

| 模块 | 说明 |
|------|------|
| **ELF 加载器** | 解析并加载 ARM ELF 可执行文件（`armfir.elf`）到虚拟地址空间 |
| **PE 加载器** | 支持加载 PE（DLL）映像，处理节映射、导入/导出表解析及重定位 |
| **虚拟文件系统** | 将固件文件操作映射到主机 `.\prime_data` 目录，支持 ANSI / Unicode 路径 |
| **LCD 显示** | 模拟 320×240 RGB565 LCD，生成独立 Win32 窗口实时渲染帧缓冲区 |
| **多线程** | 每个 guest 线程拥有独立的 Unicorn 引擎实例，共享内存映射，由原生 `std::thread` 驱动 |
| **同步原语** | Event、Semaphore、Critical Section 的创建 / 等待 / 释放 |
| **内存管理** | 静态映射 + 32 MB 动态堆（带邻接空闲块合并的简单分配器） |
| **SVC 服务表** | 700+ 系统服务 ID 定义，数十个关键 handler 已实现 |
| **输入处理** | 键盘映射（VK → 设备键码）及触摸事件注入 |
| **电源管理** | 关机 / 电池检查的模拟 |
| **系统时间** | 将主机时间映射为 guest `SYSTEMTIME` 结构 |
| **INI 配置** | `GetPrivateProfileString` / `WritePrivateProfileString` |
| **文件查找** | `findfirst` / `findnext` / `findclose`，DOS 风格通配符匹配 |
| **PE DLL 加载** | `LoadLibrary` / `FreeLibrary` / `GetProcAddress` 等 |
| **PC 连接套件** | 通过 Named Pipe 桥接实现与 HP 连接套件的通信（`HpInterOp`） |
| **调试工具** | SVC 调用日志窗口、内存查看器、执行块追踪器（基于 ImGui） |

### ❌ 尚未实现

| 模块 | 说明 |
|------|------|
| **GDI 绘图 API** | `DrawLine`、`FillRect`、`DrawCircle` 等图形原语 |
| **中断子系统** | 定时器中断、硬件中断分发 |
| **电池 API** | 电池类型检测、精确电量模拟 |
| **外部调试器** | 远程 GDB/GDBStub 接口 |
| **跨平台支持** | 当前仅支持 Windows（依赖 Win32 API、MSVC） |
| **USB Device** | USB OTG 子系统（系统原生仅支持 HID） |

---

## 架构概览

```
┌──────────────────────────────────────────────────────────────┐
│                        PrimU2 Host                           │
│  ┌──────────┐  ┌──────────────────┐  ┌────────────────────┐ │
│  │ ELF/PE   │  │  Unicorn Engine  │  │   LCD Window       │ │
│  │ Loader   │──│  (ARM emulation) │──│   (Win32 + ImGui)  │ │
│  └──────────┘  └────────┬─────────┘  └────────────────────┘ │
│                         │ SVC trap                           │
│               ┌─────────▼──────────┐                        │
│               │   Interrupt Hook   │                        │
│               │  (SVC dispatcher)  │                        │
│               └─────────┬──────────┘                        │
│       ┌─────────────────┼─────────────────┐                 │
│       ▼                 ▼                 ▼                 │
│  ┌─────────┐    ┌──────────────┐   ┌───────────┐           │
│  │  Thread  │    │  Filesystem  │   │  System   │           │
│  │  Manager │    │  (VFS)       │   │  Services │           │
│  └─────────┘    └──────────────┘   └───────────┘           │
│       │                 │                 │                  │
│       ▼                 ▼                 ▼                  │
│  ┌─────────┐    ┌──────────────┐   ┌───────────┐           │
│  │  Sync   │    │  Host FS     │   │  Memory   │           │
│  │Primitive│    │ (prime_data) │   │  Manager  │           │
│  └─────────┘    └──────────────┘   └───────────┘           │
└──────────────────────────────────────────────────────────────┘
```

### 核心模块

| 文件 | 职责 |
|------|------|
| `PrimU.cpp` | 程序入口，加载 ELF、初始化 Executor 并启动执行 |
| `executor.cpp/h` | 核心执行器（Singleton），管理 Unicorn 实例、中断钩子和执行循环 |
| `executable.cpp/h` | ELF 可执行文件的加载和解析 |
| `PELoader.cpp/h` | PE 映像的加载、节映射和导入解析 |
| `MemoryManager.cpp/h` | 虚拟内存管理：静态映射 + 动态堆分配 |
| `Thread.cpp/h` | Guest 线程抽象，每线程一个 Unicorn 实例 |
| `ThreadHandler.cpp/h` | 线程生命周期管理（StateManager） |
| `SyncPrimitives.h` | 同步原语抽象接口（IEvent、ISemaphore、ICriticalSection） |
| `Win32SyncPrimitives.h` | 基于 Win32 API 的同步原语具体实现 |
| `Services.cpp` | SVC 服务注册表，连接 SVC ID 与 handler 函数 |
| `svc_filesystem.cpp` | 文件 I/O、目录操作、INI、文件查找 handler |
| `svc_system.cpp` | LCD、内存分配、系统时间、电源、事件输入、程序管理 handler |
| `svc_thread.cpp` | 线程创建 / 调度、同步原语操作 handler |
| `svc_common.cpp/h` | VMPath 路径映射、VFileSystem 虚拟文件系统公共基础设施 |
| `LCD.cpp/h` | LCD 帧缓冲模拟、Win32 窗口创建和渲染 |
| `HpInterOp.cpp` | PC 连接套件桥接，通过 Named Pipe 与 HP 连接套件通信 |
| `interrupts.h` | 700+ 系统服务 ID 枚举定义 |
| `ui.h` | 键码定义、事件结构体和输入系统 |
| `Marshal.h` | SVC 参数提取和类型安全的 AutoBind 模板 |

---

## 依赖项

| 库 | 用途 | 版本 |
|----|------|------|
| [Unicorn Engine](https://github.com/unicorn-engine/unicorn) | ARM CPU 仿真 | 已包含预编译库 |
| [Capstone](https://github.com/capstone-engine/capstone) | 反汇编引擎（用于调试追踪） | 已包含预编译库 |
| [ELFIO](https://github.com/serge1/ELFIO) | ELF 文件解析 | 已包含头文件 |
| [Dear ImGui](https://github.com/ocornut/imgui) | 调试 GUI（SVC 日志、内存查看器等） | 已包含源码 |
| [SDL2](https://www.libsdl.org/) | ImGui 后端渲染 | 已包含源码 |

> 所有依赖已包含在 `dependencies/`、`include/` 和 `lib/` 目录中，无需额外下载。

---

## 编译

### 环境要求

- **操作系统：** Windows 10 / 11（x64）
- **编译器：** Visual Studio 2022 或更高版本
- **平台：** x64（推荐）或 x86

### 步骤

1. 克隆仓库：
   ```bash
   git clone https://github.com/telecomadm1145/PrimeU2.git
   cd PrimeU2
   ```

2. 使用 Visual Studio 打开 `PrimU.sln`。

3. 选择 `Release | x64` 配置。

4. 生成解决方案（`Ctrl+Shift+B`）。

---

## 运行

### 1. 准备固件

你需要从 HP Prime 固件更新文件（版本 **20250915**）中提取「Disk A」。

> **Disk A** 位于固件更新文件内部：`APPDISK.DAT` 文件包含一个 FAT-16 文件系统，起始于 8 KB 偏移处。使用合适的工具（如 7z）挂载或提取该文件系统以获得「Disk A」的内容。
>
> 更多详情请参阅 HP Prime 固件 Wiki：
> [https://tiplanet.org/hpwiki/index.php?title=HP\_Prime/Firmware\_files](https://tiplanet.org/hpwiki/index.php?title=HP_Prime/Firmware_files)

### 2. 放置文件

将提取的 Disk A 内容放入 `.\prime_data\A` 目录：

```
PrimU2/
├── PrimU.exe
├── prime_data/
│   └── A/
│       └── programs/
│           └── misc/
│               └── armfir.elf    ← 固件主程序
│           └── ...
│       └── ...
└── ...
```

### 3. 启动

```bash
PrimU.exe
```

程序将自动加载 `prime_data\A\programs\misc\armfir.elf` 并开始执行。如果一切正常，将会弹出一个 LCD 窗口显示计算器的画面。

### 键盘映射

模拟器将 PC 键盘按键映射到 HP Prime 的物理按键：

| PC 按键 | HP Prime 功能 |
|---------|---------------|
| `Esc` | ESC |
| `↑ ↓ ← →` | 方向键 |
| `Enter` | ENTER |
| `Backspace` | DEL |
| `0-9` | 数字键 |
| `F1-F5` | 软键 F1-F5 |
| `M` | APPS |
| `B` | HOME |
| `K` | PLOT |
| `L` | NUM |
| `Z` | VIEW |
| `C` | CAS |
| `V` | ALPHA |

LCD 窗口还支持直接鼠标触摸交互和虚拟按键面板。

---

## 调试功能

PrimU2 内置了基于 ImGui 的调试工具集：

- **SVC 调用日志：** 实时记录所有系统服务调用，包括调用 ID、参数和返回值
- **内存查看器：** 使用 `imgui_memory_editor` 检查虚拟地址空间
- **执行块追踪器：** 记录基本块的执行历史
- **栈回溯：** 发生异常时使用 Capstone 反汇编引擎生成调用栈信息

---

## MCP 服务端与远程控制

模拟器现已原生集成 TCP 远程控制模块，并配备了标准 **Model Context Protocol (MCP)** 服务端，支持 AI 编码助手或外部程序对计算器模拟器进行自动化控制。

### 1. 模拟器 TCP 命令服务端
当模拟器启动时，后台线程会在本地 **`127.0.0.1:4321`** 开启 TCP 监听。支持以 `\n` 结尾的字符串命令：
* `key <keycode> <action>`: 注入物理按键（如 `key 0x59 press`，`action` 可为 `press`, `down`, `up`）。
* `touch <x> <y> <action>`: 模拟屏幕触摸（坐标 320x240，`action` 为 `down`, `move`, `up`）。
* `screenshot`: 捕获屏幕像素，返回 BGRX 格式的二进制流（带 4 字节大端大小前缀）。
* `state`: 返回模拟器当前状态。

### 2. Python MCP 桥接服务端
位于根目录下的 **[mcp_server.py](file:///c:/Users/Administrator/Downloads/PrimeU-master/mcp_server.py)** 实现了标准的 stdio JSON-RPC 协议，向 AI 助手提供了以下封装好的控制工具：
* **`press_button`**：发送物理按键命令。支持按键别名（不区分大小写，如 `Apps`, `Home`, `Enter`, `Esc`, 数字及运算符号等）。
* **`press_button_batch`** (批量按键)：传入按键序列（例如 `["1", "+", "2", "enter"]`），在每次按键间留有可选的延迟（`delay_ms`，默认 100ms），防止模拟器内置队列溢出而丢键。
* **`get_buttons`**：获取计算器上所有支持的按键名字与对应功能的中文/英文简要说明。
* **`touch_screen`**：点击或拖拽模拟器的 LCD 触摸屏。
* **`get_screen`**：捕获模拟器当前显示画面，返回 Base64 编码的 PNG，并自动保存一份副本到本地 `screenshot.png`。
* **`get_state`**：查询模拟器当前的连通状态。

### 3. 在 AI 客户端中注册与使用
你可以在 IDE 插件、Claude Desktop 或其他支持 MCP 协议的 AI 客户端的配置文件（如 `.gemini/settings.json` 或 `claude_desktop_config.json`）中注册该服务端：

```json
"mcpServers": {
  "primeu": {
    "command": "python",
    "args": [
      "C:\\path\\to\\PrimeU-master\\mcp_server.py"
    ]
  }
}
```

重新加载会话后，AI 即可直接调用上述工具与模拟器计算器进行交互（如清除历史、进行复杂算式输入并抓图确认等）。

---

## 项目结构

```
PrimeU-master/
├── PrimU.sln                    # Visual Studio 解决方案
├── PrimU/                       # 主项目目录
│   ├── PrimU.cpp                # 入口
│   ├── executor.cpp/h           # 核心执行器
│   ├── executable.cpp/h         # ELF 加载器
│   ├── PELoader.cpp/h           # PE 加载器
│   ├── MemoryManager.cpp/h      # 内存管理
│   ├── Thread.cpp/h             # 线程抽象
│   ├── ThreadHandler.cpp/h      # 线程管理器
│   ├── SyncPrimitives.h         # 同步原语接口
│   ├── Win32SyncPrimitives.h    # Win32 同步实现
│   ├── LCD.cpp/h                # LCD 显示
│   ├── Services.cpp/h           # 服务注册表
│   ├── svc_filesystem.cpp       # 文件系统 handler
│   ├── svc_system.cpp           # 系统服务 handler
│   ├── svc_thread.cpp           # 线程服务 handler
│   ├── svc_common.cpp/h         # 公共基础设施
│   ├── HpInterOp.cpp            # PC 连接套件桥接
│   ├── interrupts.h             # SVC ID 定义
│   ├── ui.h                     # 键码和事件定义
│   ├── Marshal.h                # 参数提取模板
│   ├── handlers.h               # Handler 声明汇总
│   └── syscalls_sdk.json        # SVC ID ↔ 名称映射（参考）
├── dependencies/                # 第三方依赖
│   ├── imgui/                   # Dear ImGui 源码
│   └── sdl/                     # SDL2 源码
├── include/                     # 头文件
│   ├── unicorn/                 # Unicorn Engine 头文件
│   ├── elfio/                   # ELFIO 头文件
│   └── capstone/                # Capstone 头文件
├── lib/                         # 预编译库
│   ├── unicorn.dll/lib/a
│   └── capstone.dll/lib
└── LICENSE                      # GPL v2
```

---

## 工作原理

1. **加载**：`Executable` 类解析 ARM ELF 文件（`armfir.elf`），将各段映射到 Unicorn 的虚拟地址空间。

2. **初始化**：`Executor` 创建主 Unicorn 实例，分配栈空间和动态堆，注册中断钩子。

3. **执行**：当 guest 代码执行 `SVC` 指令时，Unicorn 触发中断回调，`interrupt_hook` 根据 SVC 号查表派发到对应的 handler 函数。

4. **服务分发**：700+ 个 SVC 服务 ID（`0x10000` ~ `0x102E5`）定义了文件 I/O、内存分配、线程管理、LCD 控制等全部操作系统 API。

5. **多线程**：每个 guest 线程通过 `std::thread` 运行在独立的 Unicorn 实例上，通过共享 `MemoryManager` 中的物理内存映射实现内存共享。

6. **显示**：LCD handler 在 Win32 窗口中渲染 guest 帧缓冲区的内容，支持键盘和触摸输入回传到 guest 事件队列。

---

## 相关项目

- [PrimeU](https://github.com/opcod3/PrimeU) — 原始 PrimeU 项目
- [qemuPrime](https://github.com/Gigi1237/qemuPrime) — 基于 QEMU 的 HP Prime 模拟
- [ripem](https://github.com/boricj/ripem) — HP Prime 系统替换(停止维护)
- [Linux-For-HPPrime-V2](https://github.com/Repeerc/Linux-For-HPPrime-V2) — 在 HP Prime V2 上运行 Linux
- [prinux (G2)](https://github.com/zephray/prinux) — HP Prime G2 上的 Linux
- [Project-Muteki](https://github.com/Project-Muteki) — Besta/Muteki 平台逆向工程与开发

---

## 许可证

本项目基于 **GNU General Public License v2 (GPL-2.0)** 发布。

详见 [LICENSE](LICENSE) 文件。
