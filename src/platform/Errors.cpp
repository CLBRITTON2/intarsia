#include "platform/Errors.hpp"

#include <cstdint>
#include <format>
#include <string>
#include <system_error>

namespace {

    std::string DescribeFailure(std::string_view call, HRESULT result) {
        return std::format("{} failed with 0x{:08X}: {}", call, static_cast<std::uint32_t>(result), std::system_category().message(result));
    }

} // namespace

Win32Error::Win32Error(std::string_view call, HRESULT result) : std::runtime_error(DescribeFailure(call, result)) {}

void ThrowIfFailed(HRESULT result, std::string_view call) {
    if (FAILED(result)) {
        throw Win32Error(call, result);
    }
}

void ThrowIfWin32Failed(DWORD result, std::string_view call) {
    if (result != NO_ERROR) {
        throw Win32Error(call, HRESULT_FROM_WIN32(result));
    }
}

void ThrowLastError(std::string_view call) {
    throw Win32Error(call, HRESULT_FROM_WIN32(GetLastError()));
}
