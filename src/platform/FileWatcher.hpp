#pragma once

#include "platform/Handle.hpp"

#include <windows.h>

#include <cstdint>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Posts changedMessage to target once a burst of changes to any of the named files in directory settles. Watches the
// directory rather than the files, because editors often save by writing a temp file and renaming it over the original.
class FileWatcher {
  public:
    FileWatcher(const std::filesystem::path& directory, std::vector<std::wstring> fileNames, HWND target, std::uint32_t changedMessage, std::uint32_t failedMessage,
                std::chrono::milliseconds settle);
    ~FileWatcher();
    FileWatcher(const FileWatcher&)            = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    // The error that stopped the watcher thread, set before failedMessage is posted.
    std::exception_ptr TakeError();

  private:
    void                      Watch();
    void                      WatchUntilStopped();

    std::vector<std::wstring> _fileNames;
    HWND                      _target;
    std::uint32_t             _changedMessage;
    std::uint32_t             _failedMessage;
    std::chrono::milliseconds _settle;
    UniqueHandle              _directory;
    UniqueHandle              _stopEvent;
    std::mutex                _errorMutex;
    std::exception_ptr        _error;
    // Declared last so every member above exists before the thread starts.
    std::thread _thread;
};

// File names in a ReadDirectoryChangesW result buffer, in order.
std::vector<std::wstring> ChangedFileNames(std::span<const std::byte> buffer);

// Case-insensitive, as NTFS file names are.
bool SameFileName(std::wstring_view left, std::wstring_view right);
