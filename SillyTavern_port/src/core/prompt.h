#pragma once

#include "core/character_card.h"
#include "core/chat_types.h"
#include "core/settings.h"

#include <string>
#include <vector>

namespace st {

struct PromptMessage {
    std::string role;
    std::string content;
};

std::string expand_macros(const std::string& text,
                          const std::string& character,
                          const std::string& user);

std::vector<PromptMessage> build_prompt(const CharacterCard& card,
                                        const Settings& settings,
                                        const std::vector<ChatMessage>& history);

}  // namespace st
