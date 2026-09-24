#include "clock.h"

#include "errors.h"

#include <array>

std::wstring FormatClock(const SYSTEMTIME& time) {
    std::array<wchar_t, 64> date{};
    if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &time, L"ddd d MMM yyyy", date.data(), static_cast<int>(date.size()), nullptr) == 0) {
        ThrowLastError("GetDateFormatEx");
    }

    std::array<wchar_t, 16> clock{};
    if (GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_FORCE24HOURFORMAT, &time, L"HH':'mm", clock.data(), static_cast<int>(clock.size())) == 0) {
        ThrowLastError("GetTimeFormatEx");
    }

    return std::wstring(date.data()) + L", " + clock.data();
}

UINT MillisecondsUntilNextMinute(const SYSTEMTIME& time) {
    return ((60u - time.wSecond) * 1000u) - time.wMilliseconds;
}
