#pragma once

#include "platform/Handle.hpp"
#include "platform/Threads.hpp"

#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

// Posts changedMessage to target once a burst of changes to any of the named files in directory settles. Watches the
// directory rather than the files, because editors often save by writing a temp file and renaming it over the original.
class FileWatcher {
  public:
    FileWatcher(const std::filesystem::path& directory, std::vector<std::wstring> fileNames, HWND target, std::uint32_t changedMessage, std::uint32_t failedMessage,
                std::chrono::milliseconds settle);
    FileWatcher(const FileWatcher&)            = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    // On failedMessage, rethrows the error that stopped the watcher thread.
    void RethrowFailure();

  private:
    void                      Watch(HANDLE stopEvent);
    void                      WatchUntilStopped(HANDLE stopEvent);

    std::vector<std::wstring> _fileNames;
    HWND                      _target;
    std::uint32_t             _changedMessage;
    std::chrono::milliseconds _settle;
    UniqueHandle              _directory;
    ThreadFailure             _failure;
    // Declared last so every member above exists before the thread starts.
    StoppableThread _thread;
};

// File names in a ReadDirectoryChangesW result buffer, in order.
std::vector<std::wstring> ChangedFileNames(std::span<const std::byte> buffer);
