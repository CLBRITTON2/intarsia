#include "modules/Clock.hpp"

#include "platform/Errors.hpp"
#include "platform/Text.hpp"

#include <format>
#include <limits>
#include <utility>

Clock::Clock(ClockConfig config) : config_(std::move(config)), size_{.width = 0.0f, .height = 0.0f} {
    try {
        FormatLocalTime(config_.format, std::chrono::system_clock::now());
    } catch (const std::format_error& error) { throw ConfigError(std::format("clock.format '{}' is not a valid std::format chrono spec: {}", config_.format, error.what())); }
}

void Clock::Layout(const RenderContext& render) {
    const std::wstring time = ToWide(FormatLocalTime(config_.format, std::chrono::system_clock::now()));
    const std::wstring text = config_.icon.empty() ? time : config_.icon + L"  " + time;

    layout_.Reset();
    ThrowIfFailed(render.dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), render.textFormat, std::numeric_limits<float>::max(),
                                                  std::numeric_limits<float>::max(), &layout_),
                  "IDWriteFactory::CreateTextLayout");
    ThrowIfFailed(layout_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP), "IDWriteTextLayout::SetWordWrapping");
    if (!config_.icon.empty()) {
        const DWRITE_TEXT_RANGE iconRange{.startPosition = 0, .length = static_cast<UINT32>(config_.icon.size())};
        ThrowIfFailed(layout_->SetFontSize(render.iconSizeDips, iconRange), "IDWriteTextLayout::SetFontSize");
        ThrowIfFailed(layout_->SetDrawingEffect(render.accent, iconRange), "IDWriteTextLayout::SetDrawingEffect");
    }

    DWRITE_TEXT_METRICS metrics{};
    ThrowIfFailed(layout_->GetMetrics(&metrics), "IDWriteTextLayout::GetMetrics");
    size_ = D2D1_SIZE_F{.width = metrics.widthIncludingTrailingWhitespace, .height = metrics.height};
}

D2D1_SIZE_F Clock::Size() const {
    return size_;
}

void Clock::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    render.context->DrawTextLayout(origin, layout_.Get(), render.foreground);
}

std::optional<UINT> Clock::MillisecondsUntilChange() const {
    return static_cast<UINT>(TimeUntilNextTick(std::chrono::system_clock::now(), config_.interval).count());
}

std::string FormatLocalTime(const std::string& format, std::chrono::system_clock::time_point now) {
    const std::chrono::zoned_time local(std::chrono::current_zone(), std::chrono::floor<std::chrono::seconds>(now));
    return std::vformat(format, std::make_format_args(local));
}

std::chrono::milliseconds TimeUntilNextTick(std::chrono::system_clock::time_point now, std::chrono::seconds interval) {
    const auto sinceEpoch = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    const auto period     = std::chrono::duration_cast<std::chrono::milliseconds>(interval);
    return period - (sinceEpoch % period);
}
