#include "platform/Monitors.hpp"

#include "platform/Edid.hpp"
#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "platform/Log.hpp"
#include "platform/Text.hpp"
#include "platform/Win32Cast.hpp"

#include <windows.h>

#include <setupapi.h>

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

    // QueryDisplayConfig answers ERROR_INSUFFICIENT_BUFFER when the paths change between sizing and reading.
    constexpr std::uint32_t kQueryAttempts = 8;

    struct ActivePath {
        std::wstring  sourceDevice; // equals MONITORINFOEXW::szDevice
        DisplayTarget target;
    };

    // Only collects handles: throwing out of an EnumDisplayMonitors callback would unwind through user32.
    BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
        IntToPointer<std::vector<HMONITOR>>(data)->push_back(monitor);
        return TRUE;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> QueryActivePaths() {
        for (std::uint32_t attempt = 0; attempt < kQueryAttempts; ++attempt) {
            UINT32 pathCount = 0;
            UINT32 modeCount = 0;
            ThrowIfWin32Failed(static_cast<DWORD>(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount)), "GetDisplayConfigBufferSizes");
            std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
            std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
            const LONG                           result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
            if (result == ERROR_INSUFFICIENT_BUFFER) {
                continue;
            }
            ThrowIfWin32Failed(static_cast<DWORD>(result), "QueryDisplayConfig");
            paths.resize(pathCount);
            return paths;
        }
        throw MonitorGoneError(std::format("QueryDisplayConfig kept finding more paths than it sized for, {} times", kQueryAttempts));
    }

    std::string ConnectionName(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY technology) {
        switch (technology) {
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15: return "VGA";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI: return "DVI";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI: return "HDMI";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL: return "DisplayPort";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_USB_TUNNEL: return "USB-C";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_MIRACAST: return "Miracast";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED: return "indirect";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_VIRTUAL: return "virtual";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL:
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED:
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED:
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS: return "internal";
            default: return "other";
        }
    }

    // The EDID Windows read from the monitor, kept in its device key. Absent for a target with no device path or no
    // EDID value, as virtual displays have.
    std::optional<std::vector<std::uint8_t>> ReadEdid(const std::wstring& devicePath) {
        if (devicePath.empty()) {
            return std::nullopt;
        }
        HDEVINFO raw = SetupDiCreateDeviceInfoList(nullptr, nullptr);
        if (raw == INVALID_HANDLE_VALUE) {
            ThrowLastError("SetupDiCreateDeviceInfoList(monitor)");
        }
        const UniqueReleased<HDEVINFO, &SetupDiDestroyDeviceInfoList> devices(raw);
        SP_DEVICE_INTERFACE_DATA                                      deviceInterface{};
        deviceInterface.cbSize = sizeof(deviceInterface);
        if (!SetupDiOpenDeviceInterfaceW(devices.get(), devicePath.c_str(), 0, &deviceInterface)) {
            ThrowLastError(std::format("SetupDiOpenDeviceInterfaceW({})", ToUtf8(devicePath)));
        }
        // Opening the interface added its device to the set as its only element.
        SP_DEVINFO_DATA device{};
        device.cbSize = sizeof(device);
        if (!SetupDiEnumDeviceInfo(devices.get(), 0, &device)) {
            ThrowLastError("SetupDiEnumDeviceInfo(monitor)");
        }
        HKEY rawKey = SetupDiOpenDevRegKey(devices.get(), &device, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (rawKey == INVALID_HANDLE_VALUE) {
            ThrowLastError("SetupDiOpenDevRegKey(monitor)");
        }
        const UniqueReleased<HKEY, &RegCloseKey> key(rawKey);
        DWORD                                    size  = 0;
        const LSTATUS                            sized = RegGetValueW(key.get(), nullptr, L"EDID", RRF_RT_REG_BINARY, nullptr, nullptr, &size);
        if (sized == ERROR_FILE_NOT_FOUND) {
            return std::nullopt;
        }
        ThrowIfWin32Failed(static_cast<DWORD>(sized), "RegGetValueW(EDID size)");
        std::vector<std::uint8_t> edid(size);
        ThrowIfWin32Failed(static_cast<DWORD>(RegGetValueW(key.get(), nullptr, L"EDID", RRF_RT_REG_BINARY, nullptr, edid.data(), &size)), "RegGetValueW(EDID)");
        edid.resize(size);
        return edid;
    }

    // A malformed EDID leaves the target without an id rather than ending the bar over a monitor's firmware, and is
    // logged so a user who expected an id sees why.
    std::optional<std::string> TargetId(const std::wstring& devicePath) {
        const std::optional<std::vector<std::uint8_t>> edid = ReadEdid(devicePath);
        if (!edid.has_value()) {
            return std::nullopt;
        }
        try {
            return MonitorId(ParseEdid(*edid));
        } catch (const EdidError& error) {
            Log(LogLevel::Warning, "monitors", "edid_invalid", {{.key = "device", .value = ToUtf8(devicePath)}, {.key = "reason", .value = error.what()}});
            return std::nullopt;
        }
    }

    // Nullopt when the target reports no friendly name.
    std::optional<std::string> FriendlyName(const DISPLAYCONFIG_TARGET_DEVICE_NAME& target) {
        std::string name = ToUtf8(target.monitorFriendlyDeviceName);
        if (name.empty()) {
            return std::nullopt;
        }
        return name;
    }

    ActivePath ReadPath(const DISPLAYCONFIG_PATH_INFO& path) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size      = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id        = path.sourceInfo.id;
        ThrowIfWin32Failed(static_cast<DWORD>(DisplayConfigGetDeviceInfo(&source.header)), "DisplayConfigGetDeviceInfo(source name)");

        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size      = sizeof(target);
        target.header.adapterId = path.targetInfo.adapterId;
        target.header.id        = path.targetInfo.id;
        ThrowIfWin32Failed(static_cast<DWORD>(DisplayConfigGetDeviceInfo(&target.header)), "DisplayConfigGetDeviceInfo(target name)");

        std::string connection = ConnectionName(target.outputTechnology);
        const bool  internal   = connection == "internal";
        return ActivePath{
            .sourceDevice = source.viewGdiDeviceName,
            .target =
                DisplayTarget{
                    .id         = TargetId(target.monitorDevicePath),
                    .name       = FriendlyName(target),
                    .connection = std::move(connection),
                    .internal   = internal,
                },
        };
    }

} // namespace

std::vector<HMONITOR> ListMonitors() {
    std::vector<HMONITOR> monitors;
    if (!EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors))) {
        ThrowLastError("EnumDisplayMonitors");
    }
    return monitors;
}

MONITORINFOEXW MonitorInfo(HMONITOR monitor) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        throw MonitorGoneError("GetMonitorInfoW found no monitor for its handle");
    }
    return info;
}

std::vector<MonitorDescription> DescribeMonitors(const std::vector<HMONITOR>& monitors) {
    std::vector<ActivePath> paths;
    // Every call here can fail when a monitor is unplugged or the session's displays change mid-read, so none may end
    // the bar. The message keeps the call and its code for the recovery's log line.
    try {
        for (const DISPLAYCONFIG_PATH_INFO& path : QueryActivePaths()) {
            paths.push_back(ReadPath(path));
        }
    } catch (const Win32Error& error) { throw MonitorGoneError(error.what()); }

    std::vector<MonitorDescription> descriptions;
    descriptions.reserve(monitors.size());
    for (HMONITOR monitor : monitors) {
        const MONITORINFOEXW info = MonitorInfo(monitor);
        MonitorDescription   description{
              .monitor = monitor,
              .primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0,
              .targets = {},
        };
        for (const ActivePath& path : paths) {
            if (path.sourceDevice == info.szDevice) {
                description.targets.push_back(path.target);
            }
        }
        descriptions.push_back(std::move(description));
    }
    return descriptions;
}
