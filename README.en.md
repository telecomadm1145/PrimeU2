# PrimU2

<p align="center">
  <b>HP Prime High-Level Emulator</b><br>
  <i>An HLE emulator for the HP Prime calculator, built on top of the Unicorn Engine</i>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-Windows-blue?style=flat-square" alt="Platform">
  <img src="https://img.shields.io/badge/arch-x86__64-orange?style=flat-square" alt="Architecture">
  <img src="https://img.shields.io/badge/license-GPL--v2-green?style=flat-square" alt="License">
  <img src="https://img.shields.io/badge/firmware-20250915-red?style=flat-square" alt="Firmware">
</p>

<p align="center">
  <a href="README.md">中文</a> | <b>English</b>
</p>

---

## Introduction

**PrimU2** is a High-Level Emulator (HLE) for the **HP Prime** graphing calculator (V1 / V2 / G1). Unlike full-system emulation, PrimU2 intercepts the firmware's System Service Calls (SVCs) and implements them directly on the host, avoiding the need to emulate low-level hardware in its entirety.

Core CPU emulation is provided by [Unicorn Engine](https://github.com/unicorn-engine/unicorn). PrimU2 layers higher-level abstractions on top, including memory management, filesystem virtualization, multi-threading, LCD display, and input handling.

> **Note:** PrimU2 currently targets HP Prime firmware version **20250915** only. Other firmware versions are not officially supported.

---

## Features

### ✅ Implemented

| Module | Description |
|--------|-------------|
| **ELF Loader** | Parses and loads ARM ELF executables (`armfir.elf`) into virtual address space |
| **PE Loader** | Loads PE (DLL) images with section mapping, import/export table resolution, and relocations |
| **Virtual Filesystem** | Maps firmware file operations to the host `.\prime_data` directory; supports ANSI and Unicode paths |
| **LCD Display** | Emulates a 320×240 RGB565 LCD with a dedicated Win32 window for real-time framebuffer rendering |
| **Multi-threading** | Each guest thread runs on its own Unicorn engine instance with shared memory mapping, driven by native `std::thread` |
| **Sync Primitives** | Create / Wait / Release for Events, Semaphores, and Critical Sections |
| **Memory Management** | Static mapping + 32 MB dynamic heap with adjacent free-block coalescing |
| **SVC Service Table** | 700+ system service IDs defined; dozens of critical handlers implemented |
| **Input Handling** | Keyboard mapping (VK → device keycodes) and touch event injection |
| **Power Management** | Power-off and battery check emulation |
| **System Time** | Maps host time to guest `SYSTEMTIME` structure |
| **INI Configuration** | `GetPrivateProfileString` / `WritePrivateProfileString` |
| **File Search** | `findfirst` / `findnext` / `findclose` with DOS-style wildcard matching |
| **PE DLL Loading** | `LoadLibrary` / `FreeLibrary` / `GetProcAddress`, etc. |
| **PC Connectivity Kit** | Bridges communication with the HP Connectivity Kit via Named Pipe (`HpInterOp`) |
| **Debug Tools** | SVC call log window, memory viewer, execution block tracer (ImGui-based) |

### ❌ Not Yet Implemented

| Module | Description |
|--------|-------------|
| **GDI Drawing API** | Graphics primitives: `DrawLine`, `FillRect`, `DrawCircle`, etc. |
| **Interrupt Subsystem** | Timer interrupts, hardware interrupt dispatch |
| **Battery API** | Battery type detection, accurate charge level emulation |
| **External Debugger** | Remote GDB / GDBStub interface |
| **Cross-platform** | Currently Windows-only (depends on Win32 API, MSVC) |
| **USB Device** | USB OTG subsystem (system natively supports HID only) |

---

## Architecture Overview

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

### Core Modules

| File | Responsibility |
|------|----------------|
| `PrimU.cpp` | Entry point — loads ELF, initializes the Executor, and starts execution |
| `executor.cpp/h` | Core executor (singleton) managing the Unicorn instance, interrupt hooks, and the execution loop |
| `executable.cpp/h` | ELF executable loading and parsing |
| `PELoader.cpp/h` | PE image loading, section mapping, and import resolution |
| `MemoryManager.cpp/h` | Virtual memory management: static mappings + dynamic heap allocation |
| `Thread.cpp/h` | Guest thread abstraction — one Unicorn instance per thread |
| `ThreadHandler.cpp/h` | Thread lifecycle management (StateManager) |
| `SyncPrimitives.h` | Abstract sync primitive interfaces (IEvent, ISemaphore, ICriticalSection) |
| `Win32SyncPrimitives.h` | Concrete Win32-based sync primitive implementations |
| `Services.cpp` | SVC service registry mapping SVC IDs to handler functions |
| `svc_filesystem.cpp` | File I/O, directory operations, INI, and file search handlers |
| `svc_system.cpp` | LCD, memory allocation, system time, power, event input, program management handlers |
| `svc_thread.cpp` | Thread creation / scheduling and sync primitive handlers |
| `svc_common.cpp/h` | VMPath path mapping and VFileSystem shared infrastructure |
| `LCD.cpp/h` | LCD framebuffer emulation, Win32 window creation, and rendering |
| `HpInterOp.cpp` | PC Connectivity Kit bridge — communicates with the HP Connectivity Kit via Named Pipe |
| `interrupts.h` | 700+ system service ID enum definitions |
| `ui.h` | Keycode definitions, event structures, and the input system |
| `Marshal.h` | SVC argument extraction and type-safe AutoBind templates |

---

## Dependencies

| Library | Purpose | Included |
|---------|---------|----------|
| [Unicorn Engine](https://github.com/unicorn-engine/unicorn) | ARM CPU emulation | Pre-built binaries |
| [Capstone](https://github.com/capstone-engine/capstone) | Disassembly engine (for debug traces) | Pre-built binaries |
| [ELFIO](https://github.com/serge1/ELFIO) | ELF file parsing | Header-only |
| [Dear ImGui](https://github.com/ocornut/imgui) | Debug GUI (SVC log, memory viewer, etc.) | Full source |
| [SDL2](https://www.libsdl.org/) | ImGui backend rendering | Full source |

> All dependencies are bundled in the `dependencies/`, `include/`, and `lib/` directories. No additional downloads required.

---

## Building

### Requirements

- **OS:** Windows 10 / 11 (x64)
- **Compiler:** Visual Studio 2022 or later
- **Platform:** x64 (recommended) or x86

### Steps

1. Clone the repository:
   ```bash
   git clone https://github.com/telecomadm1145/PrimeU2.git
   cd PrimeU2
   ```

2. Open `PrimU.sln` in Visual Studio.

3. Select `Release | x64` configuration.

4. Build the solution (`Ctrl+Shift+B`).

---

## Running

### 1. Prepare the Firmware

You need to extract "Disk A" from the **20250915** firmware update for the HP Prime calculator.

> **Disk A** is contained inside the firmware update file: the `APPDISK.DAT` file contains a FAT-16 filesystem starting at an 8 KB offset. Mount the filesystem or extract it with a suitable tool (e.g. 7z) to obtain "Disk A".
>
> See the HP Prime firmware wiki for more details:
> [https://tiplanet.org/hpwiki/index.php?title=HP\_Prime/Firmware\_files](https://tiplanet.org/hpwiki/index.php?title=HP_Prime/Firmware_files)

### 2. Place the Files

Extract the Disk A contents into the `.\prime_data\A` directory:

```
PrimU2/
├── PrimU.exe
├── prime_data/
│   └── A/
│       └── programs/
│           └── misc/
│               └── armfir.elf    ← Main firmware binary
│           └── ...
│       └── ...
└── ...
```

### 3. Launch

```bash
PrimU.exe
```

The program will automatically load `prime_data\A\programs\misc\armfir.elf` and begin execution. If everything is set up correctly, an LCD window will appear showing the calculator display.

### Keyboard Mapping

The emulator maps PC keyboard keys to HP Prime physical buttons:

| PC Key | HP Prime Function |
|--------|-------------------|
| `Esc` | ESC |
| `↑ ↓ ← →` | Arrow keys |
| `Enter` | ENTER |
| `Backspace` | DEL |
| `0-9` | Numeric keys |
| `F1-F5` | Soft keys F1-F5 |
| `M` | APPS |
| `B` | HOME |
| `K` | PLOT |
| `L` | NUM |
| `Z` | VIEW |
| `C` | CAS |
| `V` | ALPHA |

The LCD window also supports direct mouse touch interaction and a virtual button panel.

---

## Debug Features

PrimU2 includes a built-in ImGui-based debugging toolkit:

- **SVC Call Log:** Real-time recording of all system service calls, including call ID, arguments, and return values
- **Memory Viewer:** Inspect virtual address space using `imgui_memory_editor`
- **Execution Block Tracer:** Records basic block execution history
- **Stack Trace:** Generates call stack information on exceptions using the Capstone disassembly engine

---

## MCP Server & Remote Control

The emulator now natively integrates a TCP remote control module and a standard **Model Context Protocol (MCP)** server, enabling automated control by AI coding assistants or external applications.

### 1. Emulator TCP Command Server
When the emulator starts, a background thread opens a TCP listener on **`127.0.0.1:4321`**. It processes string commands terminated with `\n`:
* `key <keycode> <action>`: Inject a physical key event (e.g. `key 0x59 press`, where `action` can be `press`, `down`, or `up`).
* `touch <x> <y> <action>`: Simulate a screen touch event (320x240 coordinates, where `action` can be `down`, `move`, or `up`).
* `screenshot`: Capture the LCD framebuffer, returning a BGRX binary stream prefixed with a 4-byte big-endian size header.
* `state`: Query the current simulator connection status.

### 2. Python MCP Bridge Server
Located at the root of the workspace, **[mcp_server.py](file:///c:/Users/Administrator/Downloads/PrimeU-master/mcp_server.py)** implements the standard stdio JSON-RPC protocol. It exposes the following tools to the AI assistant:
* **`press_button`**: Press a single physical key. Supports key name aliases (case-insensitive, e.g. `Apps`, `Home`, `Enter`, `Esc`, digits, math operators, etc.).
* **`press_button_batch`** (Batch Typing): Takes an array of keys (e.g., `["1", "+", "2", "enter"]`) and presses them in sequence with an optional delay (`delay_ms`, default 100ms) to prevent key queue overflow.
* **`get_buttons`**: Retrieve all valid physical calculator buttons and their descriptions.
* **`touch_screen`**: Tap or drag on the LCD touchscreen.
* **`get_screen`**: Grab the calculator display, returning it as a Base64-encoded PNG and saving a copy locally as `screenshot.png`.
* **`get_state`**: Query the emulator running state.

### 3. Registering in AI Clients
To use the MCP server in IDE plugins, Claude Desktop, or other AI agents, register it in the configuration file (e.g., `.gemini/settings.json` or `claude_desktop_config.json`):

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

Once reloaded, the AI can programmatically interact with the simulator (e.g., perform calculations, verify display output, etc.).

---

## Project Structure

```
PrimeU-master/
├── PrimU.sln                    # Visual Studio solution
├── PrimU/                       # Main project directory
│   ├── PrimU.cpp                # Entry point
│   ├── executor.cpp/h           # Core executor
│   ├── executable.cpp/h         # ELF loader
│   ├── PELoader.cpp/h           # PE loader
│   ├── MemoryManager.cpp/h      # Memory management
│   ├── Thread.cpp/h             # Thread abstraction
│   ├── ThreadHandler.cpp/h      # Thread manager
│   ├── SyncPrimitives.h         # Sync primitive interfaces
│   ├── Win32SyncPrimitives.h    # Win32 sync implementation
│   ├── LCD.cpp/h                # LCD display
│   ├── Services.cpp/h           # Service registry
│   ├── svc_filesystem.cpp       # Filesystem handlers
│   ├── svc_system.cpp           # System service handlers
│   ├── svc_thread.cpp           # Thread service handlers
│   ├── svc_common.cpp/h         # Shared infrastructure
│   ├── HpInterOp.cpp            # PC Connectivity Kit bridge
│   ├── interrupts.h             # SVC ID definitions
│   ├── ui.h                     # Keycodes and event definitions
│   ├── Marshal.h                # Argument extraction templates
│   ├── handlers.h               # Handler declarations
│   └── syscalls_sdk.json        # SVC ID ↔ name mapping (reference)
├── dependencies/                # Third-party dependencies
│   ├── imgui/                   # Dear ImGui source
│   └── sdl/                     # SDL2 source
├── include/                     # Headers
│   ├── unicorn/                 # Unicorn Engine headers
│   ├── elfio/                   # ELFIO headers
│   └── capstone/                # Capstone headers
├── lib/                         # Pre-built libraries
│   ├── unicorn.dll/lib/a
│   └── capstone.dll/lib
└── LICENSE                      # GPL v2
```

---

## How It Works

1. **Loading:** The `Executable` class parses the ARM ELF file (`armfir.elf`) and maps its segments into Unicorn's virtual address space.

2. **Initialization:** The `Executor` creates the main Unicorn instance, allocates stack and dynamic heap memory, and registers interrupt hooks.

3. **Execution:** When guest code executes an `SVC` instruction, Unicorn fires the interrupt callback. The `interrupt_hook` dispatches to the appropriate handler function based on the SVC number.

4. **Service Dispatch:** 700+ SVC service IDs (`0x10000` – `0x102E5`) define the full set of OS APIs covering file I/O, memory allocation, thread management, LCD control, and more.

5. **Multi-threading:** Each guest thread runs on a separate Unicorn engine instance via `std::thread`, sharing physical memory through the `MemoryManager`'s shared memory mappings.

6. **Display:** The LCD handler renders guest framebuffer contents in a Win32 window, with keyboard and touch input fed back into the guest event queue.

---

## Related Projects

- [PrimeU](https://github.com/opcod3/PrimeU) — Original PrimeU project
- [qemuPrime](https://github.com/Gigi1237/qemuPrime) — QEMU-based HP Prime emulation
- [ripem](https://github.com/boricj/ripem) — HP Prime system replacement(not maintained)
- [Linux-For-HPPrime-V2](https://github.com/Repeerc/Linux-For-HPPrime-V2) — Linux on HP Prime V2
- [prinux (G2)](https://github.com/zephray/prinux) — Linux on HP Prime G2
- [Project-Muteki](https://github.com/Project-Muteki) — Besta/Muteki platform reverse engineering and development

---

## License

This project is released under the **GNU General Public License v2 (GPL-2.0)**.

See the [LICENSE](LICENSE) file for details.
