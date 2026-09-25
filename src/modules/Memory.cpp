#include "modules/Memory.hpp"

#include "platform/Errors.hpp"
#include "platform/Text.hpp"

#include <windows.h>

#include <utility>

namespace {

    std::uint64_t UsedPhysicalBytes() {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (!GlobalMemoryStatusEx(&status)) {
            ThrowLastError("GlobalMemoryStatusEx");
        }
        return status.ullTotalPhys - status.ullAvailPhys;
    }

} // namespace

double BytesToGibibytes(std::uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
}

Memory::Memory(TextModuleConfig config) : TextModule(std::move(config)) {
    RequireFormat("memory format", Settings().format, 0.0);
}

ModuleText Memory::Text() {
    return ModuleText{.text = ToWide(FormatValue(Settings().format, BytesToGibibytes(UsedPhysicalBytes()))), .alert = false};
}
