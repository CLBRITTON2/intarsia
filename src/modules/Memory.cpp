#include "modules/Memory.hpp"

#include "modules/Ticks.hpp"
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

Memory::Memory(TextModuleConfig config) : config_(std::move(config)), text_{.layout = nullptr, .size = {.width = 0.0f, .height = 0.0f}} {
    RequireFormat("memory.format", config_.format, 0.0);
}

void Memory::Layout(const RenderContext& render) {
    text_ = LayOutIconText(render, config_.icon, ToWide(FormatValue(config_.format, BytesToGibibytes(UsedPhysicalBytes()))));
}

D2D1_SIZE_F Memory::Size() const {
    return text_.size;
}

void Memory::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    render.context->DrawTextLayout(origin, text_.layout.Get(), render.foreground);
}

std::optional<UINT> Memory::MillisecondsUntilChange() const {
    return static_cast<UINT>(TimeUntilNextTick(std::chrono::system_clock::now(), config_.interval).count());
}

void                 Memory::Click(D2D1_POINT_2F) {}

const ModuleActions& Memory::Actions() const {
    return config_.actions;
}
