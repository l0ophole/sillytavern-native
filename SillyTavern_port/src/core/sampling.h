#pragma once

#include "core/json.h"
#include "core/settings.h"

namespace st {

struct Sampling {
    double temperature = 1.0;
    double top_p = 1.0;
    int max_tokens = 300;
    bool stream = true;
};

Sampling clamp_sampling(Sampling value);
Sampling sampling_from_settings(const Settings& settings);
void apply_sampling(Settings& settings, const Sampling& sampling);
Json sampling_json(const Sampling& sampling);

}  // namespace st
