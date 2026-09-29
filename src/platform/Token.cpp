#include "platform/Token.hpp"

#include "platform/Errors.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string_view>
#include <vector>

std::vector<std::byte> TokenBuffer(HANDLE token, TOKEN_INFORMATION_CLASS kind, std::string_view name) {
    DWORD size = 0;
    GetTokenInformation(token, kind, nullptr, 0, &size);
    std::vector<std::byte> buffer(size);
    if (!GetTokenInformation(token, kind, buffer.data(), size, &size)) {
        ThrowLastError(std::format("GetTokenInformation({})", name));
    }
    return buffer;
}

std::uint32_t IntegrityLevel(HANDLE token) {
    std::vector<std::byte> label = TokenBuffer(token, TokenIntegrityLevel, "TokenIntegrityLevel");
    PSID                   sid   = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(label.data())->Label.Sid;
    return *GetSidSubAuthority(sid, static_cast<DWORD>(*GetSidSubAuthorityCount(sid) - 1));
}
