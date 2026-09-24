#include "platform/FileWatcher.hpp"

#include "platform/Errors.hpp"
#include "platform/Text.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <format>
#include <stdexcept>
#include <utility>

namespace {

    constexpr DWORD kNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;

    // ReadDirectoryChangesW needs a DWORD-aligned buffer.
    constexpr size_t kBufferDwords = size_t{16} * 1024;

} // namespace

FileWatcher::FileWatcher(const std::filesystem::path& file, HWND target, UINT changedMessage, UINT failedMessage, std::chrono::milliseconds settle) :
    fileName_(file.filename().wstring()), target_(target), changedMessage_(changedMessage), failedMessage_(failedMessage), settle_(settle),
    directory_(OwnHandle(CreateFileW(file.parent_path().c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                     FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr),
                         "CreateFileW(config directory)")),
    stopEvent_(OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(stop)")), thread_([this] { Watch(); }) {}

FileWatcher::~FileWatcher() {
    SetEvent(stopEvent_.get());
    thread_.join();
}

std::exception_ptr FileWatcher::TakeError() {
    const std::scoped_lock lock(errorMutex_);
    return std::exchange(error_, nullptr);
}

void FileWatcher::Watch() {
    try {
        WatchUntilStopped();
    } catch (...) {
        {
            const std::scoped_lock lock(errorMutex_);
            error_ = std::current_exception();
        }
        PostMessageW(target_, failedMessage_, 0, 0);
    }
}

void FileWatcher::WatchUntilStopped() {
    const UniqueHandle          readDone = OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(read)");
    std::vector<DWORD>          buffer(kBufferDwords);
    const DWORD                 bufferBytes = static_cast<DWORD>(buffer.size() * sizeof(DWORD));
    OVERLAPPED                  overlapped{};
    const std::array<HANDLE, 2> waits{stopEvent_.get(), readDone.get()};
    bool                        pending = false;

    const auto                  startRead = [&] {
        overlapped        = OVERLAPPED{};
        overlapped.hEvent = readDone.get();
        ResetEvent(readDone.get());
        if (!ReadDirectoryChangesW(directory_.get(), buffer.data(), bufferBytes, FALSE, kNotifyFilter, nullptr, &overlapped, nullptr)) {
            ThrowLastError("ReadDirectoryChangesW");
        }
    };

    startRead();
    while (true) {
        const DWORD timeout = pending ? static_cast<DWORD>(settle_.count()) : INFINITE;
        const DWORD woke    = WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, timeout);
        if (woke == WAIT_OBJECT_0) {
            CancelIoEx(directory_.get(), &overlapped);
            DWORD ignored = 0;
            GetOverlappedResult(directory_.get(), &overlapped, &ignored, TRUE);
            return;
        }
        if (woke == WAIT_TIMEOUT) {
            pending = false;
            if (!PostMessageW(target_, changedMessage_, 0, 0)) {
                ThrowLastError("PostMessageW(config changed)");
            }
            continue;
        }
        if (woke != WAIT_OBJECT_0 + 1) {
            ThrowLastError("WaitForMultipleObjects");
        }

        DWORD bytes = 0;
        if (!GetOverlappedResult(directory_.get(), &overlapped, &bytes, FALSE)) {
            ThrowLastError("GetOverlappedResult(ReadDirectoryChangesW)");
        }
        // Zero bytes means the change list overflowed the buffer, so the file may have changed.
        const std::vector<std::wstring> names = ChangedFileNames(std::as_bytes(std::span<const DWORD>(buffer)).first(bytes));
        if (bytes == 0 || std::ranges::any_of(names, [this](const std::wstring& name) { return SameFileName(name, fileName_); })) {
            pending = true;
        }
        startRead();
    }
}

std::vector<std::wstring> ChangedFileNames(std::span<const std::byte> buffer) {
    // Not constexpr: the UCRT's offsetof uses reinterpret_cast, which clang rejects in a constant expression.
    const size_t              kHeaderBytes = offsetof(FILE_NOTIFY_INFORMATION, FileName);
    std::vector<std::wstring> names;
    size_t                    offset = 0;
    while (offset + kHeaderBytes <= buffer.size()) {
        FILE_NOTIFY_INFORMATION header{};
        std::memcpy(&header, buffer.data() + offset, kHeaderBytes);
        if (offset + kHeaderBytes + header.FileNameLength > buffer.size()) {
            throw std::out_of_range(std::format("change record at offset {} names {} bytes past the end of a {} byte buffer", offset, header.FileNameLength, buffer.size()));
        }
        std::wstring name(header.FileNameLength / sizeof(wchar_t), L'\0');
        std::memcpy(name.data(), buffer.data() + offset + kHeaderBytes, header.FileNameLength);
        names.push_back(std::move(name));
        if (header.NextEntryOffset == 0) {
            break;
        }
        offset += header.NextEntryOffset;
    }
    return names;
}

bool SameFileName(std::wstring_view left, std::wstring_view right) {
    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}
