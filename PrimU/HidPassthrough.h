#pragma once

#include "common.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>


/// Information about a discovered HID device on the host.
struct HidDeviceInfo {
	std::wstring devicePath;
	uint16_t vid = 0;
	uint16_t pid = 0;
	uint16_t usagePage = 0;
	uint16_t usage = 0;
	std::wstring manufacturer;
	std::wstring product;
	uint16_t inputReportLen = 0;
	uint16_t outputReportLen = 0;
};

/// Represents an opened, attached HID device.
struct AttachedHidDevice {
	HidDeviceInfo info;
	void* handle = (void*)-1;
	std::thread readThread;
	std::atomic<bool> reading{ false };
};

/// Manages HID device enumeration, attachment, and data transfer.
/// Provides the bridge between real host HID devices and the emulated
/// S3C2416 USB Host controller in the guest RTOS.
class HidPassthrough {
public:
	static HidPassthrough* GetInstance() {
		if (!_instance)
			_instance = new HidPassthrough;
		return _instance;
	}

	/// Enumerate all HID devices on the host.
	std::vector<HidDeviceInfo> EnumerateDevices();

	/// Attach a host HID device to the guest (by device path).
	bool AttachDevice(const std::wstring& devicePath);

	/// Detach a previously attached device.
	void DetachDevice(const std::wstring& devicePath);

	/// Detach all devices.
	void DetachAll();

	/// Is any device currently attached?
	bool HasAttachedDevice();

	/// Get the number of attached devices.
	size_t GetAttachedCount();

	/// Fill the guest's UsbHostDevInfo struct from the first attached device.
	/// Returns false if no device attached.
	bool GetDeviceInfo(void* outBuf, int outLen);

	/// Send an output report to the first attached device.
	bool SendData(const void* data, size_t len);

	/// Set the guest callback for incoming HID input reports.
	void SetReceiveCallback(uint32_t guestCbAddr);

	/// Called when a device change event is detected (hot-plug).
	void OnDeviceChange();

	/// Callback type for notifying UI of device list changes.
	using DeviceChangeCallback = std::function<void()>;
	void SetDeviceChangeCallback(DeviceChangeCallback cb);

private:
	HidPassthrough() = default;
	static HidPassthrough* _instance;

	void ReadThreadFunc(AttachedHidDevice* dev);

	std::mutex _mutex;
	std::vector<AttachedHidDevice*> _attached;
	uint32_t _guestReceiveCb = 0;
	DeviceChangeCallback _deviceChangeCb;
};

#define sHidPassthrough HidPassthrough::GetInstance()
