#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"

#include <wrl/client.h>

#include <chrono>
#include <string>

class Clock final : public IModule {
  public:
    // Throws ConfigError when the format is not a valid std::format chrono spec.
    explicit Clock(ClockConfig config);

    void                Layout(const RenderContext& render) override;
    D2D1_SIZE_F         Size() const override;
    void                Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    std::optional<UINT> MillisecondsUntilChange() const override;
    void                Click(D2D1_POINT_2F point) override;

  private:
    ClockConfig                               config_;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout_;
    D2D1_SIZE_F                               size_;
};

// Local time rendered with format, a std::format spec such as "{:%a %d %b %Y, %H:%M}".
std::string FormatLocalTime(const std::string& format, std::chrono::system_clock::time_point now);

// Time left until the next whole multiple of interval since the epoch, so a 60 s clock ticks on the minute.
std::chrono::milliseconds TimeUntilNextTick(std::chrono::system_clock::time_point now, std::chrono::seconds interval);
