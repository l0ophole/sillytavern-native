#include "test_util.h"

#include "core/sampling.h"
#include "core/settings.h"

#include <cmath>
#include <limits>

TEST(sampling_defaults_match_settings) {
    const st::Settings settings;
    const auto sampling = st::sampling_from_settings(settings);
    CHECK_EQ(sampling.temperature, 1.0);
    CHECK_EQ(sampling.top_p, 1.0);
    CHECK_EQ(sampling.max_tokens, 300);
    CHECK(sampling.stream);
}

TEST(sampling_clamps_values) {
    st::Sampling sampling;
    sampling.temperature = 9.0;
    sampling.top_p = -1.0;
    sampling.max_tokens = 0;
    const auto clamped = st::clamp_sampling(sampling);
    CHECK_EQ(clamped.temperature, 2.0);
    CHECK_EQ(clamped.top_p, 0.0);
    CHECK_EQ(clamped.max_tokens, 1);
}

TEST(sampling_clamps_lower_temperature_and_upper_max_tokens) {
    st::Sampling sampling;
    sampling.temperature = -3.0;
    sampling.top_p = 5.0;
    sampling.max_tokens = 5000000;
    const auto clamped = st::clamp_sampling(sampling);
    CHECK_EQ(clamped.temperature, 0.0);
    CHECK_EQ(clamped.top_p, 1.0);
    CHECK_EQ(clamped.max_tokens, 1000000);
}

TEST(sampling_replaces_non_finite_values) {
    st::Sampling sampling;
    sampling.temperature = std::numeric_limits<double>::quiet_NaN();
    sampling.top_p = std::numeric_limits<double>::infinity();
    const auto clamped = st::clamp_sampling(sampling);
    CHECK_EQ(clamped.temperature, 1.0);
    CHECK_EQ(clamped.top_p, 1.0);
}

TEST(sampling_applies_to_settings) {
    st::Settings settings;
    st::Sampling sampling;
    sampling.temperature = 0.5;
    sampling.top_p = 0.9;
    sampling.max_tokens = 128;
    sampling.stream = false;
    st::apply_sampling(settings, sampling);
    CHECK_EQ(settings.temperature, 0.5);
    CHECK_EQ(settings.top_p, 0.9);
    CHECK_EQ(settings.max_tokens, 128);
    CHECK(!settings.stream);
}

TEST(sampling_json_has_request_fields) {
    st::Sampling sampling;
    const auto value = st::sampling_json(sampling);
    CHECK_EQ(value["temperature"].get<double>(), 1.0);
    CHECK_EQ(value["top_p"].get<double>(), 1.0);
    CHECK_EQ(value["max_tokens"].get<int>(), 300);
    CHECK_EQ(value["stream"].get<bool>(), true);
}

TEST(sampling_json_reflects_custom_values) {
    st::Sampling sampling;
    sampling.temperature = 0.25;
    sampling.top_p = 0.75;
    sampling.max_tokens = 64;
    sampling.stream = false;
    const auto value = st::sampling_json(sampling);
    CHECK_EQ(value["temperature"].get<double>(), 0.25);
    CHECK_EQ(value["top_p"].get<double>(), 0.75);
    CHECK_EQ(value["max_tokens"].get<int>(), 64);
    CHECK_EQ(value["stream"].get<bool>(), false);
}
