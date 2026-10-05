#include "modules/Ticks.hpp"

#include <algorithm>

std::chrono::milliseconds TimeUntilNextTick(std::chrono::system_clock::time_point now, std::chrono::seconds interval) {
    const auto sinceEpoch = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    const auto period     = std::chrono::duration_cast<std::chrono::milliseconds>(interval);
    return period - (sinceEpoch % period);
}

bool SampleDue(std::chrono::steady_clock::time_point sampledAt, std::chrono::steady_clock::time_point now, std::chrono::seconds interval) {
    return now - sampledAt >= interval - std::chrono::milliseconds(50);
}

SampledTexts::SampledTexts(std::chrono::steady_clock::time_point sampledAt) : _sampledAt(sampledAt) {}

void SampledTexts::SampleIfDue(std::chrono::steady_clock::time_point now, std::chrono::seconds interval, const std::function<void()>& sample) {
    if (!SampleDue(_sampledAt, now, interval)) {
        return;
    }
    sample();
    _sampledAt = now;
    _texts.clear();
}

const std::wstring& SampledTexts::Of(const std::string& spec, const std::function<std::wstring()>& format) {
    const auto found = std::ranges::find(_texts, spec, &std::pair<std::string, std::wstring>::first);
    if (found != _texts.end()) {
        return found->second;
    }
    return _texts.emplace_back(spec, format()).second;
}
