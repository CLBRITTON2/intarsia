#include "platform/PeerProcess.hpp"

#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "platform/Text.hpp"
#include "platform/Token.hpp"

#include <winsock2.h>
#include <iphlpapi.h>

#include <cstdint>
#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <string_view>
#include <vector>

namespace {

    // MIB_TCPROW_OWNER_PID stores address and port in network byte order.
    constexpr std::uint32_t kLoopbackAddress = 0x0100007F; // 127.0.0.1

    std::uint16_t           RowPort(std::uint32_t networkPort) {
        return static_cast<std::uint16_t>(((networkPort & 0xFFu) << 8) | ((networkPort >> 8) & 0xFFu));
    }

    std::vector<std::byte> ConnectionTable() {
        DWORD size = 0;
        GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
        // The table can grow between the size query and the read, so retry a few times with the size it reports.
        for (std::int32_t attempt = 0; attempt < 4; ++attempt) {
            std::vector<std::byte> table(size);
            const DWORD            result = GetExtendedTcpTable(table.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
            if (result == NO_ERROR) {
                return table;
            }
            if (result != ERROR_INSUFFICIENT_BUFFER) {
                throw Win32Error("GetExtendedTcpTable(TCP_TABLE_OWNER_PID_CONNECTIONS)", HRESULT_FROM_WIN32(result));
            }
        }
        throw PeerVerificationError("GetExtendedTcpTable kept growing past its reported size");
    }

    PSID UserSid(std::vector<std::byte>& tokenUser) {
        return reinterpret_cast<TOKEN_USER*>(tokenUser.data())->User.Sid;
    }

} // namespace

std::uint32_t LoopbackPeerProcess(std::uint16_t clientPort, std::uint16_t serverPort) {
    std::vector<std::byte> table = ConnectionTable();
    MIB_TCPTABLE_OWNER_PID header{};
    std::memcpy(&header, table.data(), offsetof(MIB_TCPTABLE_OWNER_PID, table));

    for (std::uint32_t index = 0; index < header.dwNumEntries; ++index) {
        MIB_TCPROW_OWNER_PID row{};
        std::memcpy(&row, table.data() + offsetof(MIB_TCPTABLE_OWNER_PID, table) + (index * sizeof(MIB_TCPROW_OWNER_PID)), sizeof(row));
        // The server's end: local is the port it serves, remote is the client's port.
        if (row.dwLocalAddr == kLoopbackAddress && row.dwRemoteAddr == kLoopbackAddress && RowPort(row.dwLocalPort) == serverPort && RowPort(row.dwRemotePort) == clientPort) {
            return row.dwOwningPid;
        }
    }
    throw PeerVerificationError(std::format("no connection from 127.0.0.1:{} to 127.0.0.1:{} is open", clientPort, serverPort));
}

std::filesystem::path ProcessImage(HANDLE process) {
    std::array<wchar_t, 32768> path{};
    DWORD                      length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length)) {
        ThrowLastError("QueryFullProcessImageNameW");
    }
    return {std::wstring(path.data(), length)};
}

void RequireSameUserProcess(std::uint32_t processId, std::wstring_view imageName) {
    const UniqueHandle          process  = OwnHandle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId), "OpenProcess(listener)");
    const std::filesystem::path image    = ProcessImage(process.get());
    const std::wstring          fileName = image.filename().wstring();
    if (CompareStringOrdinal(fileName.c_str(), static_cast<std::int32_t>(fileName.size()), imageName.data(), static_cast<std::int32_t>(imageName.size()), TRUE) != CSTR_EQUAL) {
        throw PeerVerificationError(std::format("the listener is {} (pid {}), not {}", ToUtf8(image.wstring()), processId, ToUtf8(imageName)));
    }

    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(process.get(), TOKEN_QUERY, &rawToken)) {
        throw PeerVerificationError(std::format("cannot read the owner of {} (pid {}): OpenProcessToken failed with {}", ToUtf8(image.wstring()), processId, GetLastError()));
    }
    const UniqueHandle     token(rawToken);
    std::vector<std::byte> peerUser = TokenBuffer(token.get(), TokenUser, "TokenUser");
    std::vector<std::byte> ourUser  = TokenBuffer(GetCurrentProcessToken(), TokenUser, "TokenUser");
    if (!EqualSid(UserSid(peerUser), UserSid(ourUser))) {
        throw PeerVerificationError(std::format("{} (pid {}) runs as a different user", ToUtf8(image.wstring()), processId));
    }
    // A sandboxed process of the same user runs at low integrity (so does every AppContainer), and must not pass.
    const std::uint32_t peerLevel = IntegrityLevel(token.get());
    const std::uint32_t ourLevel  = IntegrityLevel(GetCurrentProcessToken());
    if (peerLevel < ourLevel) {
        throw PeerVerificationError(std::format("{} (pid {}) runs at integrity level 0x{:X}, below the bar's 0x{:X}", ToUtf8(image.wstring()), processId, peerLevel, ourLevel));
    }
}
