#include "platform/Monitors.hpp"

#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "platform/Log.hpp"
#include "platform/Text.hpp"
#include "platform/Win32Window.hpp"

#include <windows.h>

#include <setupapi.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    constexpr size_t kBaseBlockBytes = 128;
    constexpr std::array<std::uint8_t, 8> kHeader{0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    constexpr size_t kFirstDescriptorOffset = 54;
    constexpr size_t kDescriptorBytes = 18;
    constexpr size_t kDescriptorCount = 4;
    constexpr size_t kDescriptorTextOffset = 5;
    constexpr std::uint8_t kSerialDescriptor = 0xFF;
    constexpr std::uint8_t kTextEnd = 0x0A;
    constexpr size_t kModelLetters = 3;
    constexpr size_t kModelLength = 7;
    // The longest descriptor text, past the decimal digits of a 32-bit serial.
    constexpr size_t kMaxSerialLength = 13;

    // QueryDisplayConfig answers ERROR_INSUFFICIENT_BUFFER when the paths change between sizing and reading.
    constexpr std::uint32_t kQueryAttempts = 8;

    bool IsPrintable(char character) {
        return character >= 0x20 && character <= 0x7E;
    }

    // A descriptor's text runs to its line feed, padded with spaces. Text holding anything but printable ASCII counts as
    // none, so nothing a monitor sends reaches a log or a config unescaped.
    std::optional<std::string> DescriptorText(std::span<const std::uint8_t> descriptor) {
        std::string text;
        for (const std::uint8_t byte : descriptor.subspan(kDescriptorTextOffset)) {
            if (byte == kTextEnd) {
                break;
            }
            text.push_back(static_cast<char>(byte));
        }
        const size_t end = text.find_last_not_of(' ');
        const size_t start = text.find_first_not_of(' ');
        if (end == std::string::npos || !std::ranges::all_of(text, IsPrintable)) {
            return std::nullopt;
        }
        return text.substr(start, end - start + 1);
    }

    // Each of the four 18-byte descriptors that starts with three zero bytes is a display descriptor tagged by its fourth.
    std::optional<std::string> TaggedText(std::span<const std::uint8_t> edid, std::uint8_t tag) {
        for (size_t index = 0; index < kDescriptorCount; ++index) {
            const std::span<const std::uint8_t> descriptor = edid.subspan(kFirstDescriptorOffset + (index * kDescriptorBytes), kDescriptorBytes);
            if (descriptor[0] == 0 && descriptor[1] == 0 && descriptor[2] == 0 && descriptor[3] == tag) {
                return DescriptorText(descriptor);
            }
        }
        return std::nullopt;
    }

    // Three letters of five bits each, big-endian, 1 for A.
    std::string ManufacturerLetters(std::uint8_t high, std::uint8_t low) {
        const auto packed = static_cast<std::uint16_t>((high << 8) | low);
        std::string letters;
        for (const std::uint32_t shift : {10U, 5U, 0U}) {
            const std::uint32_t code = (packed >> shift) & 0x1FU;
            if (code < 1 || code > 26) {
                throw EdidError(std::format("EDID manufacturer id 0x{:04X} holds a letter code {} outside 1 to 26", packed, code));
            }
            letters.push_back(static_cast<char>('A' + code - 1));
        }
        return letters;
    }

    std::optional<std::string> Serial(std::span<const std::uint8_t> edid) {
        std::optional<std::string> text = TaggedText(edid, kSerialDescriptor);
        if (text.has_value()) {
            return text;
        }
        const std::uint32_t number = static_cast<std::uint32_t>(edid[12]) | (static_cast<std::uint32_t>(edid[13]) << 8) | (static_cast<std::uint32_t>(edid[14]) << 16) |
            (static_cast<std::uint32_t>(edid[15]) << 24);
        if (number == 0) {
            return std::nullopt;
        }
        return std::to_string(number);
    }

    bool IsUpperHex(char character) {
        return (character >= '0' && character <= '9') || (character >= 'A' && character <= 'F');
    }

    struct ActivePath {
        std::wstring sourceDevice; // equals MONITORINFOEXW::szDevice
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
            const LONG result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
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
        SP_DEVICE_INTERFACE_DATA deviceInterface{};
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
        DWORD size = 0;
        const LSTATUS sized = RegGetValueW(key.get(), nullptr, L"EDID", RRF_RT_REG_BINARY, nullptr, nullptr, &size);
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
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;
        ThrowIfWin32Failed(static_cast<DWORD>(DisplayConfigGetDeviceInfo(&source.header)), "DisplayConfigGetDeviceInfo(source name)");

        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = path.targetInfo.adapterId;
        target.header.id = path.targetInfo.id;
        ThrowIfWin32Failed(static_cast<DWORD>(DisplayConfigGetDeviceInfo(&target.header)), "DisplayConfigGetDeviceInfo(target name)");

        return ActivePath{
            .sourceDevice = source.viewGdiDeviceName,
            .target =
                DisplayTarget{
                    .id = TargetId(target.monitorDevicePath),
                    .name = FriendlyName(target),
                    .connection = ConnectionName(target.outputTechnology),
                },
        };
    }

} // namespace

EdidIdentity ParseEdid(std::span<const std::uint8_t> edid) {
    if (edid.size() < kBaseBlockBytes) {
        throw EdidError(std::format("EDID holds {} bytes, short of its {}-byte base block", edid.size(), kBaseBlockBytes));
    }
    if (!std::ranges::equal(edid.first(kHeader.size()), kHeader)) {
        throw EdidError("EDID does not start with its 00 FF FF FF FF FF FF 00 header");
    }
    const auto product = static_cast<std::uint16_t>(edid[10] | (edid[11] << 8));
    return EdidIdentity{.model = std::format("{}{:04X}", ManufacturerLetters(edid[8], edid[9]), product), .serial = Serial(edid)};
}

std::string MonitorId(const EdidIdentity& identity) {
    if (!identity.serial.has_value()) {
        return identity.model;
    }
    return std::format("{}-{}", identity.model, *identity.serial);
}

bool IsMonitorId(std::string_view text) {
    if (text.size() < kModelLength) {
        return false;
    }
    const std::string_view letters = text.substr(0, kModelLetters);
    const std::string_view product = text.substr(kModelLetters, kModelLength - kModelLetters);
    if (!std::ranges::all_of(letters, [](char character) { return character >= 'A' && character <= 'Z'; }) || !std::ranges::all_of(product, IsUpperHex)) {
        return false;
    }
    if (text.size() == kModelLength) {
        return true;
    }
    const std::string_view serial = text.substr(kModelLength + 1);
    return text[kModelLength] == '-' && !serial.empty() && serial.size() <= kMaxSerialLength && serial.front() != ' ' && serial.back() != ' ' &&
        std::ranges::all_of(serial, IsPrintable);
}

bool DisplayTarget::IsInternal() const {
    return connection == "internal";
}

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
        MonitorDescription description{
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
