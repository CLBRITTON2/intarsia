#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/IconText.hpp"

#include <chrono>
#include <cstdint>

// Cumulative CPU times across all cores, in 100 ns units, as GetSystemTimes reports them.
struct CpuTimes {
    std::uint64_t idle;
    std::uint64_t kernel; // includes idle time
    std::uint64_t user;
};

// Busy share of the time between two samples, 0 to 100.
int CpuUsagePercent(const CpuTimes& before, const CpuTimes& after);

// CPU busy time across all cores since the last sample, formatted from an integer percent: format = "{}%".
class Cpu final : public IModule {
  public:
    // Throws ConfigError when the format cannot render an integer.
    explicit Cpu(TextModuleConfig config);

    void                 Layout(const RenderContext& render) override;
    D2D1_SIZE_F          Size() const override;
    void                 Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    std::optional<UINT>  MillisecondsUntilChange() const override;
    void                 Click(D2D1_POINT_2F point) override;
    const ModuleActions& Actions() const override;

  private:
    TextModuleConfig                      config_;
    CpuTimes                              previous_;
    std::chrono::steady_clock::time_point sampledAt_;
    int                                   usage_;
    IconText                              text_;
};
