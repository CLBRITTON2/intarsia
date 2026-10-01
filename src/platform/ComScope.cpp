#include "platform/ComScope.hpp"

#include "platform/Errors.hpp"

#include <windows.h>

#include <objbase.h>

#include <format>

ComScope::ComScope(ComThreading threading, std::string_view name) {
    const DWORD apartment = threading == ComThreading::Single ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED;
    ThrowIfFailed(CoInitializeEx(nullptr, apartment | COINIT_DISABLE_OLE1DDE), std::format("CoInitializeEx({})", name));
}

ComScope::~ComScope() {
    CoUninitialize();
}
