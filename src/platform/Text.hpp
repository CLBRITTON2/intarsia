#pragma once

#include <string>
#include <string_view>

std::string  ToUtf8(std::wstring_view text);
std::wstring ToWide(std::string_view text);

// Text from another process with each unpaired surrogate replaced by U+FFFD, so ToUtf8 accepts it.
std::wstring ReplaceLoneSurrogates(std::wstring_view text);

// Untrusted text made safe for one quoted log field: quotes, backslashes, control bytes and bytes that are not UTF-8
// escaped, so the excerpt is always valid UTF-8. Cut to at most maxBytes of input with a note of how much was dropped.
// The cut never splits a UTF-8 sequence.
std::string LogExcerpt(std::string_view text, size_t maxBytes);
