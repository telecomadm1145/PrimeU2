#include "HidPassthrough.h"
#include "InterruptController.h"
#include "MemoryManager.h"

#include <Windows.h>
#include <SetupAPI.h>
#include <algorithm>
#include <cfgmgr32.h>
#include <cstdio>
#include <hidusage.h>
#include <hidpi.h>
#include <hidsdi.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

HidPassthrough *HidPassthrough::_instance = nullptr;

// ── Enumeration ──

std::vector<HidDeviceInfo> HidPassthrough::EnumerateDevices() {
  std::vector<HidDeviceInfo> result;

  GUID hidGuid;
  HidD_GetHidGuid(&hidGuid);

  HDEVINFO devInfo = SetupDiGetClassDevsW(
      &hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

  if (devInfo == INVALID_HANDLE_VALUE)
    return result;

  SP_DEVICE_INTERFACE_DATA ifData = {};
  ifData.cbSize = sizeof(ifData);

  for (DWORD i = 0;
       SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &ifData);
       i++) {

    // Get required size
    DWORD reqSize = 0;
    SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &reqSize,
                                     nullptr);

    auto *detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *)malloc(reqSize);
    if (!detail)
      continue;
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

    if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, reqSize,
                                          nullptr, nullptr)) {
      free(detail);
      continue;
    }

    // Try to open the device to read attributes
    HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);

    HidDeviceInfo info;
    info.devicePath = detail->DevicePath;
    free(detail);

    if (h == INVALID_HANDLE_VALUE) {
      // Some HID devices (keyboards, mice) are exclusive to the OS.
      // We still list them but with limited info.
      // Try read-only
      h = CreateFileW(info.devicePath.c_str(), GENERIC_READ,
                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                      OPEN_EXISTING, 0, nullptr);
    }

    if (h != INVALID_HANDLE_VALUE) {
      HIDD_ATTRIBUTES attrs = {};
      attrs.Size = sizeof(attrs);
      if (HidD_GetAttributes(h, &attrs)) {
        info.vid = attrs.VendorID;
        info.pid = attrs.ProductID;
      }

      wchar_t buf[256] = {};
      if (HidD_GetManufacturerString(h, buf, sizeof(buf)))
        info.manufacturer = buf;
      if (HidD_GetProductString(h, buf, sizeof(buf)))
        info.product = buf;

      PHIDP_PREPARSED_DATA ppd = nullptr;
      if (HidD_GetPreparsedData(h, &ppd)) {
        HIDP_CAPS caps = {};
        if (HidP_GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS) {
          info.usagePage = caps.UsagePage;
          info.usage = caps.Usage;
          info.inputReportLen = caps.InputReportByteLength;
          info.outputReportLen = caps.OutputReportByteLength;
        }
        HidD_FreePreparsedData(ppd);
      }

      CloseHandle(h);
    }

    // Filter out system devices (keyboard, mouse) which are usage page 1, usage
    // 6 or 2 But still include them in the list - let user decide
    result.push_back(std::move(info));
  }

  SetupDiDestroyDeviceInfoList(devInfo);
  return result;
}

// ── Attach / Detach ──

bool HidPassthrough::AttachDevice(const std::wstring &devicePath) {
  std::lock_guard<std::mutex> lk(_mutex);

  // Check if already attached
  for (auto *dev : _attached) {
    if (dev->info.devicePath == devicePath)
      return true; // already attached
  }

  // Open the device
  HANDLE h = CreateFileW(devicePath.c_str(), GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);

  if (h == INVALID_HANDLE_VALUE) {
    printf("[HID] Failed to open device for attach: %ls (err=%lu)\n",
           devicePath.c_str(), GetLastError());
    return false;
  }

  auto *dev = new AttachedHidDevice;
  dev->handle = h;

  // Fill device info
  HIDD_ATTRIBUTES attrs = {};
  attrs.Size = sizeof(attrs);
  if (HidD_GetAttributes(h, &attrs)) {
    dev->info.vid = attrs.VendorID;
    dev->info.pid = attrs.ProductID;
  }
  dev->info.devicePath = devicePath;

  wchar_t buf[256] = {};
  if (HidD_GetProductString(h, buf, sizeof(buf)))
    dev->info.product = buf;

  PHIDP_PREPARSED_DATA ppd = nullptr;
  if (HidD_GetPreparsedData(h, &ppd)) {
    HIDP_CAPS caps = {};
    if (HidP_GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS) {
      dev->info.usagePage = caps.UsagePage;
      dev->info.usage = caps.Usage;
      dev->info.inputReportLen = caps.InputReportByteLength;
      dev->info.outputReportLen = caps.OutputReportByteLength;
    }
    HidD_FreePreparsedData(ppd);
  }

  _attached.push_back(dev);

  // Start read thread
  dev->reading.store(true);
  dev->readThread = std::thread(&HidPassthrough::ReadThreadFunc, this, dev);

  printf("[HID] Attached device: VID=%04X PID=%04X %ls\n", dev->info.vid,
         dev->info.pid, dev->info.product.c_str());
  return true;
}

void HidPassthrough::DetachDevice(const std::wstring &devicePath) {
  std::lock_guard<std::mutex> lk(_mutex);

  auto it = std::find_if(
      _attached.begin(), _attached.end(),
      [&](AttachedHidDevice *d) { return d->info.devicePath == devicePath; });

  if (it == _attached.end())
    return;

  auto *dev = *it;
  dev->reading.store(false);

  // Cancel pending I/O and close handle to unblock ReadFile
  if (dev->handle != INVALID_HANDLE_VALUE) {
    CancelIoEx(dev->handle, nullptr);
    CloseHandle(dev->handle);
    dev->handle = INVALID_HANDLE_VALUE;
  }

  _attached.erase(it);

  // Join thread outside mutex would be ideal, but we hold it.
  // The thread should exit quickly since we closed the handle.
  if (dev->readThread.joinable())
    dev->readThread.detach();

  printf("[HID] Detached device: VID=%04X PID=%04X\n", dev->info.vid,
         dev->info.pid);
  // Note: dev is leaked intentionally since thread may still reference it
  // briefly. In a production system, use shared_ptr.
}

void HidPassthrough::DetachAll() {
  // Copy paths to avoid holding mutex while detaching
  std::vector<std::wstring> paths;
  {
    std::lock_guard<std::mutex> lk(_mutex);
    for (auto *d : _attached)
      paths.push_back(d->info.devicePath);
  }
  for (auto &p : paths)
    DetachDevice(p);
}

// ── Queries ──

bool HidPassthrough::HasAttachedDevice() {
  std::lock_guard<std::mutex> lk(_mutex);
  return !_attached.empty();
}

size_t HidPassthrough::GetAttachedCount() {
  std::lock_guard<std::mutex> lk(_mutex);
  return _attached.size();
}

bool HidPassthrough::GetDeviceInfo(void *outBuf, int outLen) {
  std::lock_guard<std::mutex> lk(_mutex);
  if (_attached.empty() || !outBuf || outLen < 0x10)
    return false;

  auto *dev = _attached[0];

#pragma pack(push, 1)
  struct UsbHostDevInfo {
    uint16_t idVendor;
    uint16_t idProduct;
    uint32_t bDeviceClass;
    uint16_t numEndpoints1;
    uint16_t numEndpoints2;
    uint32_t reserved;
  };
#pragma pack(pop)

  memset(outBuf, 0, outLen);
  auto *info = reinterpret_cast<UsbHostDevInfo *>(outBuf);
  info->idVendor = dev->info.vid;
  info->idProduct = dev->info.pid;
  info->bDeviceClass = 3; // HID class
  info->numEndpoints1 = 1;
  info->numEndpoints2 = (dev->info.outputReportLen > 0) ? 1 : 0;
  return true;
}

// ── Data transfer ──

bool HidPassthrough::SendData(const void *data, size_t len) {
  std::lock_guard<std::mutex> lk(_mutex);
  if (_attached.empty())
    return false;

  auto *dev = _attached[0];
  if (dev->handle == INVALID_HANDLE_VALUE)
    return false;

  DWORD written = 0;
  OVERLAPPED ov = {};
  ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

  BOOL ok = WriteFile(dev->handle, data, (DWORD)len, &written, &ov);
  if (!ok && GetLastError() == ERROR_IO_PENDING) {
    WaitForSingleObject(ov.hEvent, 1000);
    GetOverlappedResult(dev->handle, &ov, &written, FALSE);
  }

  CloseHandle(ov.hEvent);
  return written > 0;
}

void HidPassthrough::SetReceiveCallback(uint32_t guestCbAddr) {
  _guestReceiveCb = guestCbAddr;
}

// ── Read thread ──

extern uint32_t usb_in_cb;

void HidPassthrough::ReadThreadFunc(AttachedHidDevice *dev) {
  printf("[HID] Read thread started for VID=%04X PID=%04X (report len=%u)\n",
         dev->info.vid, dev->info.pid, dev->info.inputReportLen);

  uint16_t reportLen = dev->info.inputReportLen;
  if (reportLen == 0)
    reportLen = 64;

  auto *buf = new uint8_t[reportLen];

  while (dev->reading.load()) {
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD bytesRead = 0;

    memset(buf, 0, reportLen);
    BOOL ok = ReadFile(dev->handle, buf, reportLen, &bytesRead, &ov);

    if (!ok) {
      DWORD err = GetLastError();
      if (err == ERROR_IO_PENDING) {
        DWORD waitResult = WaitForSingleObject(ov.hEvent, 500);
        if (waitResult == WAIT_OBJECT_0) {
          GetOverlappedResult(dev->handle, &ov, &bytesRead, FALSE);
        } else {
          CancelIoEx(dev->handle, &ov);
          CloseHandle(ov.hEvent);
          continue;
        }
      } else if (err == ERROR_DEVICE_NOT_CONNECTED ||
                 err == ERROR_INVALID_HANDLE) {
        CloseHandle(ov.hEvent);
        printf("[HID] Device disconnected: VID=%04X PID=%04X\n", dev->info.vid,
               dev->info.pid);
        break;
      } else {
        CloseHandle(ov.hEvent);
        continue;
      }
    }

    CloseHandle(ov.hEvent);

    if (bytesRead > 0 && dev->reading.load()) {
      printf("[HID] Input report %u bytes from VID=%04X PID=%04X\n", bytesRead,
             dev->info.vid, dev->info.pid);

      // Fire USB Host interrupt (INT_USBH = IRQ 26)
      sInterruptController->RaiseIRQ(26);

      // TODO: Copy data to guest memory and invoke usb_in_cb
      // This requires the InterruptController's ISR thread
      // to execute the guest callback with the report data.
    }
  }

  delete[] buf;
  printf("[HID] Read thread exiting for VID=%04X PID=%04X\n", dev->info.vid,
         dev->info.pid);
}

// ── Hot-plug ──

void HidPassthrough::OnDeviceChange() {
  if (_deviceChangeCb)
    _deviceChangeCb();
}

void HidPassthrough::SetDeviceChangeCallback(DeviceChangeCallback cb) {
  _deviceChangeCb = cb;
}
