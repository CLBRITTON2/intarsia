#pragma once

#include "platform/Process.hpp"
#include "platform/Threads.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

// Starts commands on its own thread with ShellExecuteExW, so a UAC prompt (Task Manager needs one) never blocks the UI.
class Launcher {
  public:
    Launcher();

    // Queues the command. A full queue or a failed start is logged, never thrown: a click must not end the bar.
    void Launch(const Command& command);

  private:
    WorkQueue<Command> _queue;
};

// The desktop's shell (Explorer) has no window to find, or its window's process cannot be read.
class ShellLaunchError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// The integrity level of the process behind the desktop's shell window, waiting up to timeout for the shell to start,
// as it may not have at logon. Throws ShellLaunchError when no shell window appears, Win32Error when its process cannot
// be read.
std::uint32_t ShellIntegrityLevel(std::chrono::milliseconds timeout);

// Starts program with arguments from the shell's own process, so it runs with the shell's token rather than this
// process's: an elevated process starts it unelevated. Throws Win32Error.
void StartThroughShell(const std::filesystem::path& program, const std::vector<std::wstring>& arguments);
