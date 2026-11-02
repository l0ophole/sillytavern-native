#pragma once

#include "core/character_card.h"
#include "core/chat_store.h"
#include "core/chat_types.h"
#include "core/openai_client.h"
#include "core/prompt.h"
#include "core/settings.h"

#include <cstddef>
#include <string>
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
    int estimated_tokens() const { return estimated_tokens_; }
    bool trimmed() const { return trimmed_; }

    OpenAiRequest request() const;

    void append_user(const std::string& text);
    void append_assistant(const std::string& text);
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
    mutable int estimated_tokens_ = 0;
    mutable bool trimmed_ = false;
};

}  // namespace st
