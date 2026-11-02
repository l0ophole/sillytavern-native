#include "core/sampling.h"

#include <algorithm>
#include <cmath>

namespace st {

Sampling clamp_sampling(Sampling value) {
    if (!std::isfinite(value.temperature)) {
        value.temperature = 1.0;
    }
    value.temperature = std::clamp(value.temperature, 0.0, 2.0);

    if (!std::isfinite(value.top_p)) {
        value.top_p = 1.0;
    }
    value.top_p = std::clamp(value.top_p, 0.0, 1.0);

    value.max_tokens = std::clamp(value.max_tokens, 1, 1000000);
    return value;
}

Sampling sampling_from_settings(const Settings& settings) {
    Sampling sampling;
    sampling.temperature = settings.temperature;
    sampling.top_p = settings.top_p;
    sampling.max_tokens = settings.max_tokens;
    sampling.stream = settings.stream;
    return sampling;
}

void apply_sampling(Settings& settings, const Sampling& sampling) {
    const Sampling clamped = clamp_sampling(sampling);
    settings.temperature = clamped.temperature;
    settings.top_p = clamped.top_p;
    settings.max_tokens = clamped.max_tokens;
    settings.stream = clamped.stream;
}

Json sampling_json(const Sampling& sampling) {
    const Sampling clamped = clamp_sampling(sampling);
    Json value = Json::object();
    value["temperature"] = clamped.temperature;
    value["top_p"] = clamped.top_p;
    value["max_tokens"] = clamped.max_tokens;
    value["stream"] = clamped.stream;
    return value;
}

}  // namespace st
