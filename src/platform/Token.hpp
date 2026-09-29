#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// The buffer GetTokenInformation fills for one class of a token opened with TOKEN_QUERY. Throws Win32Error naming name.
std::vector<std::byte> TokenBuffer(HANDLE token, TOKEN_INFORMATION_CLASS kind, std::string_view name);

// The mandatory level's RID, such as SECURITY_MANDATORY_MEDIUM_RID. Throws Win32Error.
std::uint32_t IntegrityLevel(HANDLE token);
