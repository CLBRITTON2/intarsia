#pragma once

#include <string_view>

// Writes text to wherever the caller reads this process's output: stdout when it was redirected to a pipe or file, else
// the console of the process that started this one. The bar is a GUI program, so it has neither by default. Throws
// Win32Error when there is no such console, as for a bar started from Explorer.
void WriteToCaller(std::string_view utf8);
