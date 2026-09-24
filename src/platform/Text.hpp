#pragma once

#include <string>
#include <string_view>

std::string  ToUtf8(std::wstring_view text);
std::wstring ToWide(std::string_view text);

// Untrusted text made safe for one quoted log field: quotes, backslashes and control bytes escaped, cut to at most
// maxBytes of input with a note of how much was dropped. The cut never splits a UTF-8 sequence.
std::string LogExcerpt(std::string_view text, size_t maxBytes);
