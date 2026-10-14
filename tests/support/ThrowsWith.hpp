#pragma once

#include <gmock/gmock.h>

#include <string>
#include <string_view>

// Matches a callable that throws Error with text somewhere in its message.
template <typename Error>
auto ThrowsWith(std::string_view text) {
    return testing::ThrowsMessage<Error>(testing::HasSubstr(std::string(text)));
}
