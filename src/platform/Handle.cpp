#include "platform/Handle.hpp"

#include "platform/Errors.hpp"

UniqueHandle OwnHandle(HANDLE handle, std::string_view call) {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        ThrowLastError(call);
    }
    return UniqueHandle(handle);
}
