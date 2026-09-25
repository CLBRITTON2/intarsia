#pragma once

#include "platform/WorkQueue.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A program to start with its arguments, never through a shell: the arguments reach the program as they are written.
struct Command {
    std::filesystem::path     program; // absolute, resolved when the config loads
    std::vector<std::wstring> arguments;

    bool                      operator==(const Command&) const = default;
};

// The absolute path of a program: as given when it is already absolute, else the first match in the PATH directories
// (".exe" added when the name has no extension). Never the current directory, where a planted program would win.
// Nullopt when there is no such file.
std::optional<std::filesystem::path> ResolveProgram(std::wstring_view program);

// Arguments joined into one command line that CommandLineToArgvW and the C runtime split back into the same arguments.
std::wstring JoinArguments(const std::vector<std::wstring>& arguments);

// Limits DLL loading for the rest of the process to System32, so no DLL beside the executable or in the current
// directory can be planted into it. Call first thing at startup.
void RestrictDllSearchToSystem32();

// Starts commands on its own thread with ShellExecuteExW, so a UAC prompt (Task Manager needs one) never blocks the UI.
class Launcher {
  public:
    Launcher();

    // Queues the command. A full queue or a failed start is logged, never thrown: a click must not end the bar.
    void Launch(const Command& command);

  private:
    WorkQueue<Command> _queue;
};
