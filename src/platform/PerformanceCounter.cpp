#include "platform/PerformanceCounter.hpp"

#include "platform/Text.hpp"

#include <pdhmsg.h>

#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>

namespace {

    // PDH's messages live in pdh.dll, not the system's table.
    std::string PdhMessage(PDH_STATUS status) {
        wchar_t*    buffer = nullptr;
        const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                            GetModuleHandleW(L"pdh.dll"), static_cast<DWORD>(status), 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
        if (length == 0) {
            return "no message";
        }
        std::wstring message(buffer, length);
        LocalFree(buffer);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
            message.pop_back();
        }
        return ToUtf8(message);
    }

    // pdhmsg.h defines its codes as DWORD, while PDH calls return a signed PDH_STATUS.
    bool IsStatus(PDH_STATUS status, DWORD code) {
        return static_cast<DWORD>(status) == code;
    }

    // A rate before its second sample, a counter whose instances all just appeared, or a rate whose raw values ran
    // backwards between two samples (a processor's idle time outrunning the timebase), which the next sample recovers.
    bool SampleHasNoValue(PDH_STATUS status) {
        return IsStatus(status, PDH_INVALID_DATA) || IsStatus(status, PDH_CSTATUS_INVALID_DATA) || IsStatus(status, PDH_CALC_NEGATIVE_DENOMINATOR) ||
            IsStatus(status, PDH_CALC_NEGATIVE_TIMEBASE) || IsStatus(status, PDH_CALC_NEGATIVE_VALUE);
    }

} // namespace

PdhError::PdhError(std::string_view call, PDH_STATUS status) :
    std::runtime_error(std::format("{} failed with 0x{:08X}: {}", call, static_cast<std::uint32_t>(status), PdhMessage(status))) {}

PerformanceCounter::PerformanceCounter(const std::wstring& path) {
    PDH_HQUERY       query  = nullptr;
    const PDH_STATUS opened = PdhOpenQueryW(nullptr, 0, &query);
    if (opened != ERROR_SUCCESS) {
        throw PdhError("PdhOpenQueryW", opened);
    }
    _query.reset(query);
    const PDH_STATUS added = PdhAddEnglishCounterW(_query.get(), path.c_str(), 0, &_counter);
    if (added != ERROR_SUCCESS) {
        throw PdhError(std::format("PdhAddEnglishCounterW({})", ToUtf8(path)), added);
    }
}

// PDH_NO_DATA is a wildcard that matches no instance right now, which a later sample can.
void PerformanceCounter::Collect() {
    const PDH_STATUS collected = PdhCollectQueryData(_query.get());
    if (collected != ERROR_SUCCESS && !IsStatus(collected, PDH_NO_DATA)) {
        throw PdhError("PdhCollectQueryData", collected);
    }
}

std::optional<std::vector<double>> PerformanceCounter::Values() const {
    const std::optional<std::vector<CounterInstance>> instances = Instances();
    if (!instances.has_value()) {
        return std::nullopt;
    }
    std::vector<double> values;
    values.reserve(instances->size());
    for (const CounterInstance& instance : *instances) {
        values.push_back(instance.value);
    }
    return values;
}

std::optional<std::vector<CounterInstance>> PerformanceCounter::Instances() const {
    constexpr DWORD           kFormat = PDH_FMT_DOUBLE | PDH_FMT_NOCAP100; // a sum over GPU engines passes 100
    std::vector<std::uint8_t> buffer;
    DWORD                     bytes  = 0;
    DWORD                     count  = 0;
    PDH_STATUS                status = PdhGetFormattedCounterArrayW(_counter, kFormat, &bytes, &count, nullptr);
    // Instances can appear between asking the size and reading, so the read asks again until the buffer holds them.
    while (IsStatus(status, PDH_MORE_DATA)) {
        buffer.resize(bytes);
        status = PdhGetFormattedCounterArrayW(_counter, kFormat, &bytes, &count, reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data()));
    }
    if (SampleHasNoValue(status)) {
        return std::nullopt;
    }
    if (status != ERROR_SUCCESS) {
        throw PdhError("PdhGetFormattedCounterArrayW", status);
    }
    std::vector<CounterInstance> instances;
    instances.reserve(count);
    for (DWORD index = 0; index < count; ++index) {
        // The names point into the same buffer, after the items.
        PDH_FMT_COUNTERVALUE_ITEM_W item{};
        std::memcpy(&item, buffer.data() + (index * sizeof(item)), sizeof(item));
        if (item.FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || item.FmtValue.CStatus == PDH_CSTATUS_NEW_DATA) {
            instances.push_back(CounterInstance{.name = item.szName != nullptr ? std::wstring(item.szName) : std::wstring(), .value = item.FmtValue.doubleValue});
        }
    }
    return instances;
}
