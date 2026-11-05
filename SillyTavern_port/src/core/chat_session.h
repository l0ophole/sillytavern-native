#pragma once

#include "core/character_card.h"
#include "core/chat_store.h"
#include "core/chat_types.h"
#include "core/openai_client.h"
#include "core/persona.h"
#include "core/prompt.h"
#include "core/settings.h"
#include "core/world_info.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace st {

class ChatSession {
public:
    ChatSession(CharacterCard card,
                Settings settings,
                ChatStore store,
                std::string character,
                std::string filename,
                Chat chat);

    const Chat& chat() const { return chat_; }
    const CharacterCard& character() const { return card_; }
    const Settings& settings() const { return settings_; }
    Settings& settings() { return settings_; }

    std::vector<PromptMessage> prompt_messages() const;
    void set_world_info_book(const WorldInfoBook& book);
    const WorldInfoBook& world_info_book() const { return world_info_book_; }
    // Loads worldinfo/<book>.json (sanitized, 16 MiB cap) from dir.
    // Missing file leaves an empty book; parse/read errors throw.
    void load_world_info_book(const std::filesystem::path& dir, const std::string& book_name);
    void set_persona_store(const PersonaStore& store);
    const PersonaStore* persona_store() const;
    void load_persona_store(const std::filesystem::path& data_root);
    void save_persona_store(const std::filesystem::path& data_root) const;
    // When set, prompt_messages() builds through this manager (enabled
    // sections only) and request() merges sampling_overrides over the
    // settings sampling (preset wins, result clamped).
    void set_prompt_manager(const PromptManager& manager, const Json& sampling_overrides);
    const PromptManager* prompt_manager() const;
    const Json& sampling_overrides() const { return sampling_overrides_; }
    // Loads presets/<name>.json (sanitized, 16 MiB cap). Missing file
    // installs the default manager with no overrides; parse/read errors throw.
    void load_prompt_preset(const std::filesystem::path& presets_dir,
                            const std::string& preset_name);
    int estimated_tokens() const { return estimated_tokens_; }
    bool trimmed() const { return trimmed_; }

    OpenAiRequest request() const;

    void append_user(const std::string& text);
    void append_assistant(const std::string& text);
    std::size_t swipe_count(std::size_t index) const;
    std::size_t swipe_index(std::size_t index) const;
    bool select_swipe(std::size_t index, std::size_t swipe);
    bool add_swipe(std::size_t index, const std::string& text);
    Chat branch(std::size_t index) const;
    bool add_bookmark(std::size_t index, const std::string& label);
    bool remove_bookmark(std::size_t index);
    std::vector<std::pair<std::size_t, std::string>> bookmarks() const;
    bool recover_draft(const ChatDraft& draft);
    void save() const;

    Chat snapshot() const;
    void restore(Chat chat);

    bool edit_message(std::size_t index, const std::string& text);
    bool delete_message(std::size_t index);
    bool regenerate();
    bool can_regenerate_at(std::size_t index) const;
    bool can_continue() const;
    bool can_continue_at(std::size_t index) const;
    std::string continue_prefix() const;
    void append_to_last_assistant(const std::string& text);

    const std::string& filename() const { return filename_; }
    void set_filename(std::string filename);

private:
    CharacterCard card_;
    Settings settings_;
    ChatStore store_;
    std::string character_;
    std::string filename_;
    Chat chat_;
    WorldInfoBook world_info_book_;
    PersonaStore persona_store_;
    bool persona_store_set_ = false;
    PromptManager prompt_manager_;
    Json sampling_overrides_ = Json::object();
    bool prompt_manager_set_ = false;
    mutable int estimated_tokens_ = 0;
    mutable bool trimmed_ = false;
};

}  // namespace st
