#include "platform/Handle.hpp"

#include "platform/Errors.hpp"

#include <format>

UniqueHandle OwnHandle(HANDLE handle, std::string_view call) {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        ThrowLastError(call);
    }
    return UniqueHandle(handle);
}

UniqueHandle AutoResetEvent(std::string_view name) {
    return OwnHandle(CreateEventW(nullptr, FALSE, FALSE, nullptr), std::format("CreateEventW({})", name));
}

UniqueHandle ManualResetEvent(std::string_view name) {
    return OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), std::format("CreateEventW({})", name));
}
