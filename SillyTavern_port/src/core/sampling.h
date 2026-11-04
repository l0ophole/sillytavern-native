#pragma once

#include "core/json.h"
#include "core/settings.h"

namespace st {

struct Sampling {
    double temperature = 1.0;
    double top_p = 1.0;
    int max_tokens = 300;
    bool stream = true;
    int top_k = 0;
    double repetition_penalty = 1.0;
    double presence_penalty = 0.0;
    double frequency_penalty = 0.0;
    double dry_multiplier = 0.0;
    double mirostat_tau = 5.0;
};

Sampling clamp_sampling(Sampling value);
Sampling sampling_from_settings(const Settings& settings);
void apply_sampling(Settings& settings, const Sampling& sampling);
Json sampling_json(const Sampling& sampling);

}  // namespace st
