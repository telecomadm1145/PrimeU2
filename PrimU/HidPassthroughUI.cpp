#include "HidPassthroughUI.h"
#include "HidPassthrough.h"
#include <Windows.h>
#include <CommCtrl.h>
#include <Dbt.h>
#include <atomic>
#include <cstdio>
#include <hidsdi.h>
#include <string>
#include <thread>
#include <vector>


#pragma comment(lib, "comctl32.lib")
#pragma comment(                                                               \
    linker,                                                                    \
    "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

// ── Control IDs ──
constexpr int IDC_LISTVIEW = 1001;
constexpr int IDC_BTN_REFRESH = 1002;
constexpr int IDC_BTN_ATTACH = 1003;
constexpr int IDC_BTN_DETACH = 1004;

HWND g_hwnd = nullptr;
HWND g_listView = nullptr;
std::thread g_uiThread;
std::atomic<bool> g_running{false};
HDEVNOTIFY g_devNotify = nullptr;

// Cached device list (mirrors what's displayed)
std::vector<HidDeviceInfo> g_deviceList;

// ── Helpers ──

void RefreshDeviceList() {
  g_deviceList = sHidPassthrough->EnumerateDevices();
  ListView_DeleteAllItems(g_listView);

  for (int i = 0; i < (int)g_deviceList.size(); i++) {
    auto &dev = g_deviceList[i];

    wchar_t vidpid[32];
    swprintf_s(vidpid, L"%04X:%04X", dev.vid, dev.pid);

    wchar_t usage[32];
    swprintf_s(usage, L"%04X/%04X", dev.usagePage, dev.usage);

    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = i;

    // Column 0: VID:PID
    item.iSubItem = 0;
    item.pszText = vidpid;
    ListView_InsertItem(g_listView, &item);

    // Column 1: Product
    item.iSubItem = 1;
    item.pszText = (LPWSTR)dev.product.c_str();
    ListView_SetItem(g_listView, &item);

    // Column 2: Usage
    item.iSubItem = 2;
    item.pszText = usage;
    ListView_SetItem(g_listView, &item);

    // Column 3: Status
    item.iSubItem = 3;
    item.pszText = (LPWSTR)L"Detached";
    ListView_SetItem(g_listView, &item);
  }
}

int GetSelectedIndex() {
  return ListView_GetNextItem(g_listView, -1, LVNI_SELECTED);
}

void UpdateStatusColumn(int idx, const wchar_t *status) {
  LVITEMW item = {};
  item.mask = LVIF_TEXT;
  item.iItem = idx;
  item.iSubItem = 3;
  item.pszText = (LPWSTR)status;
  ListView_SetItem(g_listView, &item);
}

// ── Window Procedure ──

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_CREATE: {
    // Register for HID device notifications
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    DEV_BROADCAST_DEVICEINTERFACE filter = {};
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = hidGuid;
    g_devNotify =
        RegisterDeviceNotificationW(hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    return 0;
  }

  case WM_COMMAND: {
    int id = LOWORD(wParam);
    if (id == IDC_BTN_REFRESH) {
      RefreshDeviceList();
    } else if (id == IDC_BTN_ATTACH) {
      int sel = GetSelectedIndex();
      if (sel >= 0 && sel < (int)g_deviceList.size()) {
        if (sHidPassthrough->AttachDevice(g_deviceList[sel].devicePath)) {
          UpdateStatusColumn(sel, L"Attached");
        } else {
          UpdateStatusColumn(sel, L"FAILED");
        }
      }
    } else if (id == IDC_BTN_DETACH) {
      int sel = GetSelectedIndex();
      if (sel >= 0 && sel < (int)g_deviceList.size()) {
        sHidPassthrough->DetachDevice(g_deviceList[sel].devicePath);
        UpdateStatusColumn(sel, L"Detached");
      }
    }
    return 0;
  }

  case WM_DEVICECHANGE: {
    if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
      printf("[HID UI] Device change detected, refreshing...\n");
      sHidPassthrough->OnDeviceChange();
      RefreshDeviceList();
    }
    return 0;
  }

  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0; // Don't destroy, just hide

  case WM_DESTROY:
    if (g_devNotify) {
      UnregisterDeviceNotification(g_devNotify);
      g_devNotify = nullptr;
    }
    PostQuitMessage(0);
    return 0;
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ── UI Thread ──

void UIThreadFunc() {
  INITCOMMONCONTROLSEX icex = {};
  icex.dwSize = sizeof(icex);
  icex.dwICC = ICC_LISTVIEW_CLASSES;
  InitCommonControlsEx(&icex);

  const wchar_t *className = L"HidPassthroughUIClass";

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = className;
  RegisterClassExW(&wc);

  g_hwnd = CreateWindowExW(0, className, L"USB HID Device Passthrough",
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           700, 450, nullptr, nullptr, wc.hInstance, nullptr);

  if (!g_hwnd) {
    printf("[HID UI] Failed to create window: %lu\n", GetLastError());
    return;
  }

  // Create ListView
  g_listView = CreateWindowExW(0, WC_LISTVIEWW, L"",
                               WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT |
                                   LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                               10, 10, 660, 340, g_hwnd, (HMENU)IDC_LISTVIEW,
                               wc.hInstance, nullptr);

  ListView_SetExtendedListViewStyle(g_listView,
                                    LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

  // Add columns
  LVCOLUMNW col = {};
  col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

  col.iSubItem = 0;
  col.cx = 100;
  col.pszText = (LPWSTR)L"VID:PID";
  ListView_InsertColumn(g_listView, 0, &col);

  col.iSubItem = 1;
  col.cx = 280;
  col.pszText = (LPWSTR)L"Product";
  ListView_InsertColumn(g_listView, 1, &col);

  col.iSubItem = 2;
  col.cx = 100;
  col.pszText = (LPWSTR)L"Usage";
  ListView_InsertColumn(g_listView, 2, &col);

  col.iSubItem = 3;
  col.cx = 100;
  col.pszText = (LPWSTR)L"Status";
  ListView_InsertColumn(g_listView, 3, &col);

  // Create buttons
  CreateWindowExW(0, L"BUTTON", L"Refresh",
                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 360, 80, 30,
                  g_hwnd, (HMENU)IDC_BTN_REFRESH, wc.hInstance, nullptr);

  CreateWindowExW(0, L"BUTTON", L"Attach",
                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 100, 360, 80, 30,
                  g_hwnd, (HMENU)IDC_BTN_ATTACH, wc.hInstance, nullptr);

  CreateWindowExW(0, L"BUTTON", L"Detach",
                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 190, 360, 80, 30,
                  g_hwnd, (HMENU)IDC_BTN_DETACH, wc.hInstance, nullptr);

  // Initial device enumeration
  RefreshDeviceList();

  ShowWindow(g_hwnd, SW_SHOW);
  UpdateWindow(g_hwnd);

  // Message loop
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  g_hwnd = nullptr;
  g_listView = nullptr;
  g_running = false;
}

} // anonymous namespace

// ── Public API ──

void HidPassthroughUI::Launch() {
  if (g_running.exchange(true))
    return; // Already running

  g_uiThread = std::thread(UIThreadFunc);
}

void HidPassthroughUI::Shutdown() {
  if (g_hwnd) {
    PostMessageW(g_hwnd, WM_DESTROY, 0, 0);
  }
  if (g_uiThread.joinable()) {
    g_uiThread.join();
  }
  g_running = false;
}
