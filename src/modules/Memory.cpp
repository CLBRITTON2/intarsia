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
    RequireFormats("memory", Settings().format, Settings().tooltip, 0.0);
}

ModuleText Memory::Format(const std::string& spec) {
    return ModuleText{.text = ToWide(FormatValue(spec, BytesToGibibytes(UsedPhysicalBytes()))), .alert = false};
}
