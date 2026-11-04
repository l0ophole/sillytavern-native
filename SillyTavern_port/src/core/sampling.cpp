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

    value.top_k = std::clamp(value.top_k, 0, 1000);
    if (!std::isfinite(value.repetition_penalty)) {
        value.repetition_penalty = 1.0;
    }
    value.repetition_penalty = std::clamp(value.repetition_penalty, 0.5, 2.0);
    if (!std::isfinite(value.presence_penalty)) {
        value.presence_penalty = 0.0;
    }
    value.presence_penalty = std::clamp(value.presence_penalty, -2.0, 2.0);
    if (!std::isfinite(value.frequency_penalty)) {
        value.frequency_penalty = 0.0;
    }
    value.frequency_penalty = std::clamp(value.frequency_penalty, -2.0, 2.0);
    if (!std::isfinite(value.dry_multiplier)) {
        value.dry_multiplier = 0.0;
    }
    value.dry_multiplier = std::clamp(value.dry_multiplier, 0.0, 10.0);
    if (!std::isfinite(value.mirostat_tau)) {
        value.mirostat_tau = 5.0;
    }
    value.mirostat_tau = std::clamp(value.mirostat_tau, 0.0, 20.0);
    return value;
}

Sampling sampling_from_settings(const Settings& settings) {
    Sampling sampling;
    sampling.temperature = settings.temperature;
    sampling.top_p = settings.top_p;
    sampling.max_tokens = settings.max_tokens;
    sampling.stream = settings.stream;
    sampling.top_k = settings.top_k;
    sampling.repetition_penalty = settings.repetition_penalty;
    sampling.presence_penalty = settings.presence_penalty;
    sampling.frequency_penalty = settings.frequency_penalty;
    sampling.dry_multiplier = settings.dry_multiplier;
    sampling.mirostat_tau = settings.mirostat_tau;
    return sampling;
}

void apply_sampling(Settings& settings, const Sampling& sampling) {
    const Sampling clamped = clamp_sampling(sampling);
    settings.temperature = clamped.temperature;
    settings.top_p = clamped.top_p;
    settings.max_tokens = clamped.max_tokens;
    settings.stream = clamped.stream;
    settings.top_k = clamped.top_k;
    settings.repetition_penalty = clamped.repetition_penalty;
    settings.presence_penalty = clamped.presence_penalty;
    settings.frequency_penalty = clamped.frequency_penalty;
    settings.dry_multiplier = clamped.dry_multiplier;
    settings.mirostat_tau = clamped.mirostat_tau;
}

Json sampling_json(const Sampling& sampling) {
    const Sampling clamped = clamp_sampling(sampling);
    Json value = Json::object();
    value["temperature"] = clamped.temperature;
    value["top_p"] = clamped.top_p;
    value["max_tokens"] = clamped.max_tokens;
    value["stream"] = clamped.stream;
    if (clamped.top_k != 0) {
        value["top_k"] = clamped.top_k;
    }
    if (clamped.repetition_penalty != 1.0) {
        value["repetition_penalty"] = clamped.repetition_penalty;
    }
    if (clamped.presence_penalty != 0.0) {
        value["presence_penalty"] = clamped.presence_penalty;
    }
    if (clamped.frequency_penalty != 0.0) {
        value["frequency_penalty"] = clamped.frequency_penalty;
    }
    if (clamped.dry_multiplier != 0.0) {
        value["dry_multiplier"] = clamped.dry_multiplier;
    }
    if (clamped.mirostat_tau != 5.0) {
        value["mirostat_tau"] = clamped.mirostat_tau;
    }
    return value;
}

}  // namespace st
