#include "platform/PeerProcess.hpp"

#include "Loopback.hpp"
#include "platform/Errors.hpp"
#include "platform/Handle.hpp"

#include <sddl.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace {

    constexpr wchar_t kTestImage[] = L"intarsia_gtests.exe";

    std::wstring      TestExecutable() {
        std::array<wchar_t, MAX_PATH> path{};
        const DWORD                   length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0 || length == path.size()) {
            ThrowLastError("GetModuleFileNameW");
        }
        return std::wstring(path.data(), length);
    }

    // A primary token like this process's, lowered to low integrity the way a sandbox runs.
    UniqueHandle LowIntegrityToken() {
        HANDLE own = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ASSIGN_PRIMARY, &own)) {
            ThrowLastError("OpenProcessToken");
        }
        const UniqueHandle ownToken(own);
        HANDLE             duplicate = nullptr;
        if (!DuplicateTokenEx(ownToken.get(), 0, nullptr, SecurityImpersonation, TokenPrimary, &duplicate)) {
            ThrowLastError("DuplicateTokenEx");
        }
        UniqueHandle low(duplicate);

        PSID         sid = nullptr;
        if (!ConvertStringSidToSidW(L"S-1-16-4096", &sid)) {
            ThrowLastError("ConvertStringSidToSidW");
        }
        const std::unique_ptr<void, decltype(&LocalFree)> ownedSid(sid, &LocalFree);
        TOKEN_MANDATORY_LABEL                             label{.Label = {.Sid = sid, .Attributes = SE_GROUP_INTEGRITY}};
        if (!SetTokenInformation(low.get(), TokenIntegrityLevel, &label, sizeof(label) + GetLengthSid(sid))) {
            ThrowLastError("SetTokenInformation(TokenIntegrityLevel)");
        }
        return low;
    }

} // namespace

TEST(PeerProcess, FindsTheProcessHoldingTheServerEndOfAConnection) {
    const loopback::Winsock winsock;
    const loopback::Socket  listener = loopback::Listen();
    const loopback::Socket  client   = loopback::Connect(listener.Port());
    const loopback::Socket  server   = loopback::Accept(listener);
    EXPECT_EQ(LoopbackPeerProcess(client.Port(), listener.Port()), GetCurrentProcessId());
}

TEST(PeerProcess, RejectsAConnectionThatIsNotOpen) {
    const loopback::Winsock winsock;
    const loopback::Socket  listener = loopback::Listen();
    // A listener with nobody connected: there is no server end to find.
    EXPECT_THROW(LoopbackPeerProcess(1, listener.Port()), PeerVerificationError);
}

TEST(PeerProcess, AcceptsAProcessOfThisUserWithTheExpectedImage) {
    EXPECT_NO_THROW(RequireSameUserProcess(GetCurrentProcessId(), kTestImage));
}

TEST(PeerProcess, RejectsAnotherImageName) {
    EXPECT_THROW(RequireSameUserProcess(GetCurrentProcessId(), L"glazewm.exe"), PeerVerificationError);
}

TEST(PeerProcess, RejectsALowIntegrityProcessOfThisUser) {
    const UniqueHandle token      = LowIntegrityToken();
    std::wstring       executable = TestExecutable();
    STARTUPINFOW       startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    // Suspended, so it never runs: only its token and image are read.
    if (!CreateProcessAsUserW(token.get(), executable.c_str(), nullptr, nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr, &startup, &child)) {
        ThrowLastError("CreateProcessAsUserW");
    }
    const UniqueHandle process(child.hProcess);
    const UniqueHandle thread(child.hThread);

    EXPECT_THAT([&child] { RequireSameUserProcess(child.dwProcessId, kTestImage); }, testing::ThrowsMessage<PeerVerificationError>(testing::HasSubstr("integrity level")));
    TerminateProcess(process.get(), 0);
}
