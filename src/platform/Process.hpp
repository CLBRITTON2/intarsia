#pragma once

#include "platform/Handle.hpp"

#include <windows.h>

#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// A program to start with its arguments, never through a shell: the arguments reach the program as they are written.
struct Command {
    std::filesystem::path     program; // absolute, resolved when the config loads
    std::vector<std::wstring> arguments;

    bool                      operator==(const Command&) const = default;
};

// The absolute path of a program: as given when it is already absolute, else found on PATH (".exe" added when the
// name has no extension). Nullopt when there is no such file.
std::optional<std::filesystem::path> ResolveProgram(std::wstring_view program);

// Arguments joined into one command line that CommandLineToArgvW and the C runtime split back into the same arguments.
std::wstring JoinArguments(const std::vector<std::wstring>& arguments);

// Starts commands on its own thread with ShellExecuteExW, so a UAC prompt (Task Manager needs one) never blocks the UI.
class Launcher {
  public:
    Launcher();
    ~Launcher();
    Launcher(const Launcher&)            = delete;
    Launcher& operator=(const Launcher&) = delete;

    // Queues the command. A full queue or a failed start is logged, never thrown: a click must not end the bar.
    void Launch(const Command& command);

  private:
    void                Run();

    UniqueHandle        stopEvent_;
    UniqueHandle        queuedEvent_;
    std::mutex          mutex_;
    std::deque<Command> queue_;
    // Declared last so every member above exists before the thread starts.
    std::thread thread_;
};
