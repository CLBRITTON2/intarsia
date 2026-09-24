#include "platform/PeerProcess.hpp"

#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "platform/Text.hpp"

#include <winsock2.h>
#include <iphlpapi.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <optional>
#include <vector>

namespace {

    // MIB_TCPROW_OWNER_PID stores address and port in network byte order.
    constexpr DWORD kLoopbackAddress = 0x0100007F; // 127.0.0.1
    constexpr DWORD kAnyAddress      = 0;

    USHORT          RowPort(DWORD networkPort) {
        return static_cast<USHORT>(((networkPort & 0xFFu) << 8) | ((networkPort >> 8) & 0xFFu));
    }

    std::vector<std::byte> ListenerTable() {
        DWORD size = 0;
        GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
        // The table can grow between the size query and the read, so retry a few times with the size it reports.
        for (int attempt = 0; attempt < 4; ++attempt) {
            std::vector<std::byte> table(size);
            const DWORD            result = GetExtendedTcpTable(table.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
            if (result == NO_ERROR) {
                return table;
            }
            if (result != ERROR_INSUFFICIENT_BUFFER) {
                throw Win32Error("GetExtendedTcpTable(TCP_TABLE_OWNER_PID_LISTENER)", HRESULT_FROM_WIN32(result));
            }
        }
        throw PeerVerificationError("GetExtendedTcpTable kept growing past its reported size");
    }

    std::vector<std::byte> TokenUserBuffer(HANDLE token) {
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::vector<std::byte> buffer(size);
        if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
            ThrowLastError("GetTokenInformation(TokenUser)");
        }
        return buffer;
    }

    PSID UserSid(std::vector<std::byte>& tokenUser) {
        return reinterpret_cast<TOKEN_USER*>(tokenUser.data())->User.Sid;
    }

} // namespace

DWORD LoopbackListenerProcess(USHORT port) {
    std::vector<std::byte> table = ListenerTable();
    MIB_TCPTABLE_OWNER_PID header{};
    std::memcpy(&header, table.data(), offsetof(MIB_TCPTABLE_OWNER_PID, table));

    std::optional<DWORD> loopback;
    std::optional<DWORD> any;
    for (DWORD index = 0; index < header.dwNumEntries; ++index) {
        MIB_TCPROW_OWNER_PID row{};
        std::memcpy(&row, table.data() + offsetof(MIB_TCPTABLE_OWNER_PID, table) + (index * sizeof(MIB_TCPROW_OWNER_PID)), sizeof(row));
        if (RowPort(row.dwLocalPort) != port) {
            continue;
        }
        if (row.dwLocalAddr == kLoopbackAddress) {
            loopback = row.dwOwningPid;
        } else if (row.dwLocalAddr == kAnyAddress) {
            any = row.dwOwningPid;
        }
    }
    // A connection to 127.0.0.1 reaches the loopback-specific binding before a wildcard one.
    if (loopback.has_value()) {
        return *loopback;
    }
    if (any.has_value()) {
        return *any;
    }
    throw PeerVerificationError(std::format("nothing is listening on 127.0.0.1:{}", port));
}

void RequireSameUserProcess(DWORD processId, std::wstring_view imageName) {
    const UniqueHandle         process = OwnHandle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId), "OpenProcess(listener)");

    std::array<wchar_t, 32768> path{};
    DWORD                      length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &length)) {
        ThrowLastError("QueryFullProcessImageNameW(listener)");
    }
    const std::filesystem::path image(std::wstring(path.data(), length));
    const std::wstring          fileName = image.filename().wstring();
    if (CompareStringOrdinal(fileName.c_str(), static_cast<int>(fileName.size()), imageName.data(), static_cast<int>(imageName.size()), TRUE) != CSTR_EQUAL) {
        throw PeerVerificationError(std::format("the listener is {} (pid {}), not {}", ToUtf8(image.wstring()), processId, ToUtf8(imageName)));
    }

    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(process.get(), TOKEN_QUERY, &rawToken)) {
        throw PeerVerificationError(std::format("cannot read the owner of {} (pid {}): OpenProcessToken failed with {}", ToUtf8(image.wstring()), processId, GetLastError()));
    }
    const UniqueHandle     token(rawToken);
    std::vector<std::byte> peerUser = TokenUserBuffer(token.get());
    std::vector<std::byte> ourUser  = TokenUserBuffer(GetCurrentProcessToken());
    if (!EqualSid(UserSid(peerUser), UserSid(ourUser))) {
        throw PeerVerificationError(std::format("{} (pid {}) runs as a different user", ToUtf8(image.wstring()), processId));
    }
}
