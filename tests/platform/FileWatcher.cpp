#include "platform/FileWatcher.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

    struct ChangeRecord {
        DWORD        action;
        std::wstring name;
    };

    // Lays records out the way ReadDirectoryChangesW does: DWORD-aligned, each pointing at the next.
    std::vector<std::byte> ChangeBuffer(const std::vector<ChangeRecord>& records) {
        const size_t           kHeaderBytes = offsetof(FILE_NOTIFY_INFORMATION, FileName);
        std::vector<std::byte> buffer;
        for (size_t index = 0; index < records.size(); ++index) {
            const size_t            nameBytes   = records[index].name.size() * sizeof(wchar_t);
            const size_t            recordBytes = (kHeaderBytes + nameBytes + sizeof(DWORD) - 1) / sizeof(DWORD) * sizeof(DWORD);
            const bool              last        = index + 1 == records.size();

            FILE_NOTIFY_INFORMATION header{};
            header.NextEntryOffset = last ? 0 : static_cast<DWORD>(recordBytes);
            header.Action          = records[index].action;
            header.FileNameLength  = static_cast<DWORD>(nameBytes);

            const size_t start = buffer.size();
            buffer.resize(start + recordBytes);
            std::memcpy(buffer.data() + start, &header, kHeaderBytes);
            std::memcpy(buffer.data() + start + kHeaderBytes, records[index].name.data(), nameBytes);
        }
        return buffer;
    }

} // namespace

TEST(FileWatcher, ChangedFileNamesReadsEveryRecordInOrder) {
    const std::vector<std::byte> buffer = ChangeBuffer({{.action = FILE_ACTION_ADDED, .name = L"config.lua~"}, {.action = FILE_ACTION_RENAMED_NEW_NAME, .name = L"config.lua"}});
    EXPECT_EQ(ChangedFileNames(buffer), (std::vector<std::wstring>{L"config.lua~", L"config.lua"}));
}

TEST(FileWatcher, ChangedFileNamesOfAnEmptyBufferIsEmpty) {
    EXPECT_TRUE(ChangedFileNames({}).empty());
}

TEST(FileWatcher, ChangedFileNamesRejectsANameRunningPastTheBuffer) {
    std::vector<std::byte> buffer = ChangeBuffer({{.action = FILE_ACTION_MODIFIED, .name = L"config.lua"}});
    buffer.resize(offsetof(FILE_NOTIFY_INFORMATION, FileName) + 4);
    EXPECT_THROW(ChangedFileNames(buffer), std::out_of_range);
}

TEST(FileWatcher, SameFileNameIgnoresCase) {
    EXPECT_TRUE(SameFileName(L"Config.LUA", L"config.lua"));
    EXPECT_FALSE(SameFileName(L"config.lua.bak", L"config.lua"));
}
