#include "test_util.h"

#include "core/character_card.h"
#include "core/chat_types.h"
#include "core/prompt.h"
#include "core/prompt_manager.h"
#include "core/settings.h"

#include <string>
#include <vector>

TEST(prompt_manager_default_order) {
    const st::PromptManager manager = st::PromptManager::default_manager();
    CHECK_EQ(manager.sections.size(), std::size_t(9));
    const std::vector<std::string> expected = {"system",        "description", "personality",
                                               "scenario",      "examples",    "world_info",
                                               "persona",       "authors_note", "post_history"};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK_EQ(manager.sections[i].id, expected[i]);
        CHECK_EQ(manager.sections[i].order, static_cast<int>(i));
        CHECK(manager.sections[i].enabled);
        CHECK(manager.sections[i].content.empty());
    }
    const auto enabled = manager.enabled_sections();
    CHECK_EQ(enabled.size(), std::size_t(9));
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK_EQ(enabled[i].id, expected[i]);
    }
    CHECK_EQ(manager.active_preset, "default");

    // Settings.active_preset defaults to "default" and round-trips.
    st::Settings settings;
    CHECK_EQ(settings.active_preset, "default");
    settings.active_preset = "heroic";
    const st::Settings restored = st::Settings::from_json(settings.to_json());
    CHECK_EQ(restored.active_preset, "heroic");
    // Missing field is tolerant: defaults back to "default".
    const st::Settings missing = st::Settings::from_json(st::parse_json("{}", "settings"));
    CHECK_EQ(missing.active_preset, "default");
    // validate() sanitizes to a filename-safe value.
    st::Settings dirty;
    dirty.active_preset = "a/b c";
    dirty.validate();
    CHECK(dirty.active_preset.find("/") == std::string::npos);
}

TEST(prompt_manager_disabled_section_skipped) {
    st::CharacterCard card;
    card.name = "Seraphina";
    card.scenario = "A cave";
    card.description = "A dragon.";
    st::Settings settings;
    settings.user_name = "Alice";
    settings.system_prompt = "Global prompt.";

    // Default manager: scenario contributes, identical to nullptr order content.
    st::PromptManager manager = st::PromptManager::default_manager();
    const auto with_manager = st::build_prompt_parts(card, settings, {}, nullptr, &manager);
    bool found_scenario = false;
    for (const auto& message : with_manager.leading_system) {
        if (message.content.find("Scenario: A cave") != std::string::npos) {
            found_scenario = true;
        }
    }
    CHECK(found_scenario);

    // Disable "scenario": no scenario text in any part.
    for (auto& section : manager.sections) {
        if (section.id == "scenario") {
            section.enabled = false;
        }
    }
    CHECK_EQ(manager.enabled_sections().size(), std::size_t(8));
    const auto without = st::build_prompt_parts(card, settings, {}, nullptr, &manager);
    for (const auto& message : without.leading_system) {
        CHECK(message.content.find("Scenario: A cave") == std::string::npos);
        CHECK(message.content.find("A cave") == std::string::npos);
    }
    for (const auto& message : without.history) {
        CHECK(message.content.find("A cave") == std::string::npos);
    }
    for (const auto& message : without.trailing_system) {
        CHECK(message.content.find("A cave") == std::string::npos);
    }
    // Other sections still contribute.
    bool found_description = false;
    for (const auto& message : without.leading_system) {
        if (message.content.find("A dragon.") != std::string::npos) {
            found_description = true;
        }
    }
    CHECK(found_description);
}

TEST(prompt_preset_round_trips) {
    st::PromptPreset preset;
    preset.name = "heroic";
    st::PromptSection first;
    first.id = "system";
    first.title = "System";
    first.content = "Custom system.";
    first.enabled = true;
    first.order = 1;
    st::PromptSection second;
    second.id = "scenario";
    second.title = "Scenario";
    second.content = "";
    second.enabled = false;
    second.order = 0;
    preset.sections = {first, second};
    preset.sampling_overrides = st::Json::object();
    preset.sampling_overrides["temperature"] = 0.5;

    const st::Json json = preset.to_json();
    CHECK_EQ(json["name"].get<std::string>(), "heroic");
    CHECK(json.contains("sections"));
    CHECK(json.contains("sampling"));

    const st::PromptPreset restored = st::PromptPreset::from_json(json);
    CHECK_EQ(restored.name, "heroic");
    CHECK_EQ(restored.sections.size(), std::size_t(2));
    // Sections are sorted by order on load.
    CHECK_EQ(restored.sections[0].id, "scenario");
    CHECK_EQ(restored.sections[0].order, 0);
    CHECK(!restored.sections[0].enabled);
    CHECK_EQ(restored.sections[1].id, "system");
    CHECK_EQ(restored.sections[1].content, "Custom system.");
    CHECK(restored.sampling_overrides.contains("temperature"));
    CHECK_EQ(restored.sampling_overrides["temperature"].get<double>(), 0.5);

    // Tolerance: missing id skips the section, missing content becomes "".
    const st::PromptPreset tolerant = st::PromptPreset::from_json(st::parse_json(
        R"({"name":"t","sections":[{"title":"no id"},{"id":"system"}]})", "preset"));
    CHECK_EQ(tolerant.sections.size(), std::size_t(1));
    CHECK_EQ(tolerant.sections[0].id, "system");
    CHECK_EQ(tolerant.sections[0].content, "");
}

TEST(prompt_manager_move_reorders) {
    st::PromptManager manager = st::PromptManager::default_manager();
    const auto before = manager.enabled_sections();
    CHECK_EQ(before[0].id, "system");
    CHECK_EQ(before[2].id, "personality");
    manager.move(0, 2);
    const auto after = manager.enabled_sections();
    CHECK_EQ(after.size(), std::size_t(9));
    CHECK(after[0].id != before[0].id);
    // system moved from position 0 to position 2.
    CHECK_EQ(after[2].id, "system");
    CHECK_EQ(after[0].id, "description");
    CHECK_EQ(after[1].id, "personality");
    // Underlying sections vector is reordered too, order fields follow positions.
    CHECK_EQ(manager.sections[2].id, "system");
    for (std::size_t i = 0; i < manager.sections.size(); ++i) {
        CHECK_EQ(manager.sections[i].order, static_cast<int>(i));
    }
}
