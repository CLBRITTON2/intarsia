#include "platform/KnownFolder.hpp"

#include "platform/Errors.hpp"
#include "platform/Handle.hpp"

#include <objbase.h>

#include <format>

std::filesystem::path KnownFolderPath(const KNOWNFOLDERID& folder, std::string_view name) {
    PWSTR raw = nullptr;
    ThrowIfFailed(SHGetKnownFolderPath(folder, 0, nullptr, &raw), std::format("SHGetKnownFolderPath({})", name));
    const UniqueReleased<PWSTR, &CoTaskMemFree> owned(raw);
    return {owned.get()};
}
