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
    _fileName(file.filename().wstring()), _target(target), _changedMessage(changedMessage), _failedMessage(failedMessage), _settle(settle),
    _directory(OwnHandle(CreateFileW(file.parent_path().c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                     FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr),
                         "CreateFileW(config directory)")),
    _stopEvent(OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(stop)")), _thread([this] { Watch(); }) {}

FileWatcher::~FileWatcher() {
    SetEvent(_stopEvent.get());
    _thread.join();
}

std::exception_ptr FileWatcher::TakeError() {
    const std::scoped_lock lock(_errorMutex);
    return std::exchange(_error, nullptr);
}

void FileWatcher::Watch() {
    try {
        WatchUntilStopped();
    } catch (...) {
        {
            const std::scoped_lock lock(_errorMutex);
            _error = std::current_exception();
        }
        PostMessageW(_target, _failedMessage, 0, 0);
    }
}

void FileWatcher::WatchUntilStopped() {
    const UniqueHandle          readDone = OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(read)");
    std::vector<DWORD>          buffer(kBufferDwords);
    const DWORD                 bufferBytes = static_cast<DWORD>(buffer.size() * sizeof(DWORD));
    OVERLAPPED                  overlapped{};
    const std::array<HANDLE, 2> waits{_stopEvent.get(), readDone.get()};
    bool                        pending = false;

    const auto                  startRead = [&] {
        overlapped        = OVERLAPPED{};
        overlapped.hEvent = readDone.get();
        ResetEvent(readDone.get());
        if (!ReadDirectoryChangesW(_directory.get(), buffer.data(), bufferBytes, FALSE, kNotifyFilter, nullptr, &overlapped, nullptr)) {
            ThrowLastError("ReadDirectoryChangesW");
        }
    };

    startRead();
    while (true) {
        const DWORD timeout = pending ? static_cast<DWORD>(_settle.count()) : INFINITE;
        const DWORD woke    = WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, timeout);
        if (woke == WAIT_OBJECT_0) {
            CancelIoEx(_directory.get(), &overlapped);
            DWORD ignored = 0;
            GetOverlappedResult(_directory.get(), &overlapped, &ignored, TRUE);
            return;
        }
        if (woke == WAIT_TIMEOUT) {
            pending = false;
            if (!PostMessageW(_target, _changedMessage, 0, 0)) {
                ThrowLastError("PostMessageW(config changed)");
            }
            continue;
        }
        if (woke != WAIT_OBJECT_0 + 1) {
            ThrowLastError("WaitForMultipleObjects");
        }

        DWORD bytes = 0;
        if (!GetOverlappedResult(_directory.get(), &overlapped, &bytes, FALSE)) {
            ThrowLastError("GetOverlappedResult(ReadDirectoryChangesW)");
        }
        // Zero bytes means the change list overflowed the buffer, so the file may have changed.
        const std::vector<std::wstring> names = ChangedFileNames(std::as_bytes(std::span<const DWORD>(buffer)).first(bytes));
        if (bytes == 0 || std::ranges::any_of(names, [this](const std::wstring& name) { return SameFileName(name, _fileName); })) {
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
