#pragma once

#include "platform/Handle.hpp"

#include <windows.h>

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

// Posts changedMessage to target once a burst of changes to one file settles. Watches the file's directory rather than
// the file, because editors often save by writing a temp file and renaming it over the original.
class FileWatcher {
  public:
    FileWatcher(const std::filesystem::path& file, HWND target, UINT changedMessage, UINT failedMessage, std::chrono::milliseconds settle);
    ~FileWatcher();
    FileWatcher(const FileWatcher&)            = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    // The error that stopped the watcher thread, set before failedMessage is posted.
    std::exception_ptr TakeError();

  private:
    void                      Watch();
    void                      WatchUntilStopped();

    std::wstring              _fileName;
    HWND                      _target;
    UINT                      _changedMessage;
    UINT                      _failedMessage;
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
