#pragma once

#include "core/character_card.h"
#include "core/chat_types.h"
#include "core/persona.h"
#include "core/prompt_manager.h"
#include "core/settings.h"
#include "core/world_info.h"

#include <string>
#include <vector>

namespace st {

struct PromptMessage {
    std::string role;
    std::string content;
};

struct PromptParts {
    std::vector<PromptMessage> leading_system;
    std::vector<PromptMessage> history;
    std::vector<PromptMessage> trailing_system;
};

std::string expand_macros(const std::string& text,
                          const std::string& character,
                          const std::string& user);

std::string format_instruct_message(const std::string& role,
                                    const std::string& content,
                                    const std::string& format);

void insert_at_depth(std::vector<PromptMessage>& history,
                     const PromptMessage& note,
                     int depth);

PromptParts build_prompt_parts(const CharacterCard& card,
                               const Settings& settings,
                               const std::vector<ChatMessage>& history);

PromptParts build_prompt_parts(const CharacterCard& card,
                               const Settings& settings,
                               const std::vector<ChatMessage>& history,
                               const PersonaStore* persona_store);

PromptParts build_prompt_parts(const CharacterCard& card,
                               const Settings& settings,
                               const std::vector<ChatMessage>& history,
                               const PersonaStore* persona_store,
                               const PromptManager* prompt_manager);

PromptParts build_prompt_parts_with_book(const CharacterCard& card,
                                         const Settings& settings,
                                         const std::vector<ChatMessage>& history,
                                         const WorldInfoBook& book);

PromptParts build_prompt_parts_with_book(const CharacterCard& card,
                                         const Settings& settings,
                                         const std::vector<ChatMessage>& history,
                                         const WorldInfoBook& book,
                                         const PersonaStore* persona_store);

PromptParts build_prompt_parts_with_book(const CharacterCard& card,
                                         const Settings& settings,
                                         const std::vector<ChatMessage>& history,
                                         const WorldInfoBook& book,
                                         const PersonaStore* persona_store,
                                         const PromptManager* prompt_manager);

std::vector<PromptMessage> build_prompt(const CharacterCard& card,
                                        const Settings& settings,
                                        const std::vector<ChatMessage>& history);

std::vector<PromptMessage> build_prompt(const CharacterCard& card,
                                        const Settings& settings,
                                        const std::vector<ChatMessage>& history,
                                        const PersonaStore* persona_store);

}  // namespace st
