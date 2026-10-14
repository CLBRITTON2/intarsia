#pragma once

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

// The absolute path of a program: as given when it is already absolute, else a bare file name's first match in the
// PATH directories (".exe" added when the name has no extension). Never the current directory, where a planted program
// would win, so a relative path with a directory part resolves to nothing. Nullopt when there is no such file.
std::optional<std::filesystem::path> ResolveProgram(std::wstring_view program);

// Arguments joined into one command line that CommandLineToArgvW and the C runtime split back into the same arguments.
std::wstring JoinArguments(const std::vector<std::wstring>& arguments);
