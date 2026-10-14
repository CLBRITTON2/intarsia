#include "platform/ComScope.hpp"

#include "platform/Errors.hpp"

#include <windows.h>

#include <objbase.h>

#include <format>

namespace {

    DWORD Apartment(ComThreading threading) {
        if (threading == ComThreading::Single) {
            return COINIT_APARTMENTTHREADED;
        }
        return COINIT_MULTITHREADED;
    }

} // namespace

ComScope::ComScope(ComThreading threading, std::string_view name) {
    ThrowIfFailed(CoInitializeEx(nullptr, Apartment(threading) | COINIT_DISABLE_OLE1DDE), std::format("CoInitializeEx({})", name));
}

ComScope::~ComScope() {
    CoUninitialize();
}
