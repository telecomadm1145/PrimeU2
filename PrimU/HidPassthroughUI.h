#pragma once

/// Independent Win32 window for selecting which host HID devices
/// to pass through to the emulated guest USB Host controller.
///
/// Features:
///   - ListView with VID:PID, Product, UsagePage, Status columns
///   - Attach/Detach buttons
///   - Auto-refresh on WM_DEVICECHANGE (hot-plug)
///   - Runs in its own thread with its own message loop
namespace HidPassthroughUI {
/// Launch the UI window in a background thread.
/// Safe to call multiple times; only one window will be created.
void Launch();

/// Close the UI window and join the thread.
void Shutdown();
} // namespace HidPassthroughUI
