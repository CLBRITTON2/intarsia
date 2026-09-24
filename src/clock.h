#pragma once

#include <windows.h>

#include <string>

inline constexpr wchar_t kClockIcon[] = L"\U000F1441";

// "Wed 24 Sep 2026, 14:05" in the user's locale, always 24-hour.
std::wstring FormatClock(const SYSTEMTIME& time);

UINT         MillisecondsUntilNextMinute(const SYSTEMTIME& time);
