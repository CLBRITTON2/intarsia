#pragma once

#include <gmock/gmock.h>

#include <string>
#include <string_view>

template <typename Error>
auto ThrowsWith(std::string_view text) {
    return testing::ThrowsMessage<Error>(testing::HasSubstr(std::string(text)));
}
