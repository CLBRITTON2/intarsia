#pragma once

#include <string>
#include <string_view>

std::string  ToUtf8(std::wstring_view text);
std::wstring ToWide(std::string_view text);

// Text from another process with each unpaired surrogate replaced by U+FFFD, so ToUtf8 accepts it.
std::wstring ReplaceLoneSurrogates(std::wstring_view text);

// Controls (C0, DEL, C1), the line and paragraph separators (U+2028, U+2029) that break a drawn line, and the
// bidirectional embeddings, overrides and isolates, which can make drawn text read as something other than what it
// holds (Trojan Source, CVE-2021-42574). All are single UTF-16 units.
bool IsUnsafeCodePoint(char32_t code);
bool IsUnsafeCharacter(wchar_t unit);

// Whether bytes are well-formed UTF-8 (no overlong forms, encoded surrogates or code points past U+10FFFF).
bool IsValidUtf8(std::string_view bytes);

// Untrusted bytes that should be UTF-8, such as a Wi-Fi network's name, made safe to draw: each invalid sequence and
// each unsafe character becomes U+FFFD, so nothing can throw or reorder the drawn line.
std::wstring DrawableText(std::string_view bytes);

// Untrusted text made safe for one quoted log field: quotes, backslashes, bytes that are not UTF-8 and every unsafe
// character escaped, so the excerpt is always valid UTF-8 on one line that reads as written. Cut to at most maxBytes
// of input with a note of how much was dropped. The cut never splits a UTF-8 sequence.
std::string LogExcerpt(std::string_view text, size_t maxBytes);
