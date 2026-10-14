#include "battery/PowerStatus.hpp"

#include "platform/Errors.hpp"
#include "platform/Log.hpp"

#include <windows.h>

#include <powrprof.h>
#include <setupapi.h>
// winioctl.h defines CTL_CODE, which batclass.h's IOCTL codes expand to.
#include <winioctl.h>

#include <batclass.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

    constexpr DWORD kRefreshMs = 10000;

    // GUID_DEVICE_BATTERY from poclass.h, which only DEFINE_GUID declares and no import library defines.
    constexpr GUID kBatteryInterface{0x72631e54, 0x78a4, 0x11d0, {0xbc, 0xf7, 0x00, 0xaa, 0x00, 0xb7, 0xb3, 0x2a}};

    // The device path of every battery interface present, such as a laptop's one or two packs.
    std::vector<std::wstring> BatteryPaths() {
        HDEVINFO raw = SetupDiGetClassDevsW(&kBatteryInterface, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (raw == INVALID_HANDLE_VALUE) {
            ThrowLastError("SetupDiGetClassDevsW(battery)");
        }
        const UniqueReleased<HDEVINFO, &SetupDiDestroyDeviceInfoList> devices(raw);
        std::vector<std::wstring>                                     paths;
        for (DWORD index = 0;; ++index) {
            SP_DEVICE_INTERFACE_DATA device{};
            device.cbSize = sizeof(device);
            if (!SetupDiEnumDeviceInterfaces(devices.get(), nullptr, &kBatteryInterface, index, &device)) {
                if (GetLastError() == ERROR_NO_MORE_ITEMS) {
                    return paths;
                }
                ThrowLastError("SetupDiEnumDeviceInterfaces(battery)");
            }
            DWORD required = 0;
            if (!SetupDiGetDeviceInterfaceDetailW(devices.get(), &device, nullptr, 0, &required, nullptr) && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
                ThrowLastError("SetupDiGetDeviceInterfaceDetailW(size)");
            }
            // DWORDs keep the detail struct, a DWORD then WCHARs, aligned.
            std::vector<DWORD> buffer((required + sizeof(DWORD) - 1) / sizeof(DWORD));
            auto*              detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
            detail->cbSize            = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (!SetupDiGetDeviceInterfaceDetailW(devices.get(), &device, detail, static_cast<DWORD>(buffer.size() * sizeof(DWORD)), nullptr, nullptr)) {
                ThrowLastError("SetupDiGetDeviceInterfaceDetailW");
            }
            paths.emplace_back(detail->DevicePath);
        }
    }

    // The battery's capacities, or nullopt for one that is not a system battery (a UPS) or reports only relative
    // capacities, which make no health.
    std::optional<BatteryCapacity> ReadCapacity(const std::wstring& path) {
        const UniqueHandle battery =
            OwnHandle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr), "CreateFileW(battery)");
        ULONG wait     = 0;
        ULONG tag      = BATTERY_TAG_INVALID;
        DWORD returned = 0;
        if (!DeviceIoControl(battery.get(), IOCTL_BATTERY_QUERY_TAG, &wait, sizeof(wait), &tag, sizeof(tag), &returned, nullptr)) {
            ThrowLastError("DeviceIoControl(IOCTL_BATTERY_QUERY_TAG)");
        }
        if (tag == BATTERY_TAG_INVALID) {
            return std::nullopt;
        }
        BATTERY_QUERY_INFORMATION query{};
        query.BatteryTag       = tag;
        query.InformationLevel = BatteryInformation;
        BATTERY_INFORMATION information{};
        if (!DeviceIoControl(battery.get(), IOCTL_BATTERY_QUERY_INFORMATION, &query, sizeof(query), &information, sizeof(information), &returned, nullptr)) {
            ThrowLastError("DeviceIoControl(IOCTL_BATTERY_QUERY_INFORMATION)");
        }
        if ((information.Capabilities & BATTERY_SYSTEM_BATTERY) == 0 || (information.Capabilities & BATTERY_CAPACITY_RELATIVE) != 0) {
            return std::nullopt;
        }
        return BatteryCapacity{.designed = information.DesignedCapacity, .fullCharged = information.FullChargedCapacity};
    }

    std::vector<BatteryCapacity> ReadCapacities() {
        std::vector<BatteryCapacity> capacities;
        for (const std::wstring& path : BatteryPaths()) {
            const std::optional<BatteryCapacity> capacity = ReadCapacity(path);
            if (capacity.has_value()) {
                capacities.push_back(*capacity);
            }
        }
        return capacities;
    }

    // Runs on a system thread, so it only wakes the worker.
    ULONG CALLBACK WakeOnPowerChange(PVOID context, ULONG, PVOID) {
        SetEvent(static_cast<HANDLE>(context));
        return ERROR_SUCCESS;
    }

    UniquePowerNotification RegisterWake(const GUID& setting, HANDLE wakeEvent, std::string_view call) {
        DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS parameters{.Callback = WakeOnPowerChange, .Context = wakeEvent};
        HPOWERNOTIFY                       notification = nullptr;
        ThrowIfWin32Failed(PowerSettingRegisterNotification(&setting, DEVICE_NOTIFY_CALLBACK, &parameters, &notification), call);
        return UniquePowerNotification(notification);
    }

} // namespace

PowerStatus::PowerStatus(ChangeSignal& changes) :
    _changes(changes), _wakeEvent(AutoResetEvent("battery wake")),
    _powerSourceNotification(RegisterWake(GUID_ACDC_POWER_SOURCE, _wakeEvent.get(), "PowerSettingRegisterNotification(GUID_ACDC_POWER_SOURCE)")),
    _percentNotification(RegisterWake(GUID_BATTERY_PERCENTAGE_REMAINING, _wakeEvent.get(), "PowerSettingRegisterNotification(GUID_BATTERY_PERCENTAGE_REMAINING)")),
    _thread("battery", [this](HANDLE stopEvent) { Run(stopEvent); }) {}

std::optional<BatteryState> PowerStatus::State() const {
    const std::scoped_lock lock(_mutex);
    return _state;
}

// Nothing here may throw out of the thread, so every failure is logged once and the next sample tries again.
void PowerStatus::Run(HANDLE stopEvent) {
    FailureLog                  sampleLog("battery");
    FailureLog                  healthLog("battery");
    const std::array<HANDLE, 2> events{stopEvent, _wakeEvent.get()};
    DWORD                       woke = WAIT_TIMEOUT;
    do {
        std::optional<BatteryState> state;
        try {
            SYSTEM_POWER_STATUS power{};
            if (!GetSystemPowerStatus(&power)) {
                ThrowLastError("GetSystemPowerStatus");
            }
            state = BatteryFromPowerStatus(power);
            sampleLog.Succeeded();
        } catch (const Win32Error& error) { sampleLog.Failed("sample_failed", error); }
        if (state.has_value()) {
            try {
                state->healthPercent = HealthPercent(ReadCapacities());
                healthLog.Succeeded();
            } catch (const Win32Error& error) {
                // The charge still shows, only its health is missing.
                healthLog.Failed("health_unavailable", error);
            }
        }
        bool changed = false;
        {
            const std::scoped_lock lock(_mutex);
            changed = state != _state;
            _state  = state;
        }
        if (changed) {
            _changes.Notify("battery");
        }
        woke = WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE, kRefreshMs);
    } while (woke == WAIT_TIMEOUT || woke == WAIT_OBJECT_0 + 1);
    if (woke != WAIT_OBJECT_0) {
        Log(LogLevel::Error, "battery", "wait_failed", {{.key = "error", .value = std::to_string(GetLastError())}});
    }
}

std::optional<BatteryState> BatteryFromPowerStatus(const SYSTEM_POWER_STATUS& power) {
    if (power.BatteryFlag == BATTERY_FLAG_UNKNOWN || (power.BatteryFlag & BATTERY_FLAG_NO_BATTERY) != 0 || power.BatteryLifePercent == BATTERY_PERCENTAGE_UNKNOWN) {
        return std::nullopt;
    }
    ChargeState charge = ChargeState::Discharging;
    if ((power.BatteryFlag & BATTERY_FLAG_CHARGING) != 0) {
        charge = ChargeState::Charging;
    } else if (power.ACLineStatus == AC_LINE_ONLINE) {
        charge = ChargeState::Plugged;
    }
    std::optional<std::int32_t> secondsLeft;
    if (charge == ChargeState::Discharging && power.BatteryLifeTime != BATTERY_LIFE_UNKNOWN) {
        secondsLeft = static_cast<std::int32_t>(power.BatteryLifeTime);
    }
    return BatteryState{
        .charge        = charge,
        .percent       = power.BatteryLifePercent,
        .secondsLeft   = secondsLeft,
        .healthPercent = std::nullopt,
    };
}

std::optional<std::int32_t> HealthPercent(const std::vector<BatteryCapacity>& batteries) {
    std::uint64_t designed    = 0;
    std::uint64_t fullCharged = 0;
    for (const BatteryCapacity& battery : batteries) {
        designed += battery.designed;
        fullCharged += battery.fullCharged;
    }
    if (designed == 0) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(std::llround(static_cast<double>(fullCharged) * 100.0 / static_cast<double>(designed)));
}
