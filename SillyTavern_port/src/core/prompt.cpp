#include "core/prompt.h"

#include "core/identity.h"

#include <cstddef>
#include <string>
#include <utility>

namespace st {
namespace {

struct Macro {
    const char* token;
    bool is_character;
};

constexpr Macro kMacros[] = {
    {"{{char}}", true},
    {"<bot>", true},
    {"{{user}}", false},
    {"<user>", false},
};

bool equals_ignore_case(const std::string& text, std::size_t offset, const char* token) {
    std::size_t index = 0;
    while (token[index] != '\0') {
        if (offset + index >= text.size()) {
            return false;
        }
        const unsigned char left = static_cast<unsigned char>(text[offset + index]);
        const unsigned char right = static_cast<unsigned char>(token[index]);
        const unsigned char lowered_left =
            (left >= 'A' && left <= 'Z') ? static_cast<unsigned char>(left - 'A' + 'a') : left;
        if (lowered_left != right) {
            return false;
        }
        ++index;
    }
    return true;
}

void append_section(std::string& target, const std::string& section) {
    if (section.empty()) {
        return;
    }
    if (!target.empty()) {
        target += "\n\n";
    }
    target += section;
}

}  // namespace

std::string expand_macros(const std::string& text,
                          const std::string& character,
                          const std::string& user) {
    std::string result;
    result.reserve(text.size());

    std::size_t offset = 0;
    while (offset < text.size()) {
        bool matched = false;
        for (const Macro& macro : kMacros) {
            if (!equals_ignore_case(text, offset, macro.token)) {
                continue;
            }
            result += macro.is_character ? character : user;
            offset += std::string(macro.token).size();
            matched = true;
            break;
        }
        if (!matched) {
            result += text[offset];
            ++offset;
        }
    }
    return result;
}

void insert_at_depth(std::vector<PromptMessage>& history,
                     const PromptMessage& note,
                     int depth) {
    const std::size_t effective_depth =
        depth <= 0 ? 0u : static_cast<std::size_t>(depth);
    const std::size_t follow = effective_depth < history.size() ? effective_depth : history.size();
    const std::size_t index = history.size() - follow;
    history.insert(history.begin() + static_cast<std::ptrdiff_t>(index), note);
}

PromptParts build_prompt_parts(const CharacterCard& card,
                               const Settings& settings,
                               const std::vector<ChatMessage>& history) {
    const std::string character = card.name.empty() ? "Character" : card.name;
    const std::string user = normalize_user_name(settings.user_name);

    std::string system_text;
    const std::string& base_prompt =
        card.system_prompt.empty() ? settings.system_prompt : card.system_prompt;
    append_section(system_text, expand_macros(base_prompt, character, user));
    append_section(system_text, expand_macros(card.description, character, user));
    if (!card.personality.empty()) {
        append_section(system_text,
                       expand_macros(character + "'s personality: " + card.personality,
                                     character, user));
    }
    if (!card.scenario.empty()) {
        append_section(system_text,
                       expand_macros("Scenario: " + card.scenario, character, user));
    }
    if (!card.mes_example.empty()) {
        append_section(system_text,
                       expand_macros("Example dialogues:\n" + card.mes_example,
                                     character, user));
    }

    PromptParts parts;
    if (!system_text.empty()) {
        parts.leading_system.push_back(PromptMessage{"system", std::move(system_text)});
    }

    for (const ChatMessage& message : history) {
        if (message.mes.empty()) {
            continue;
        }
        const char* role = message.is_system ? "system" : (message.is_user ? "user" : "assistant");
        parts.history.push_back(PromptMessage{role, expand_macros(message.mes, character, user)});
    }

    if (settings.authors_note_enabled) {
        const std::string expanded_note = expand_macros(settings.authors_note, character, user);
        if (!expanded_note.empty()) {
            insert_at_depth(parts.history, PromptMessage{"system", expanded_note},
                            settings.authors_note_depth);
        }
    }

    if (!card.post_history_instructions.empty()) {
        parts.trailing_system.push_back(PromptMessage{
            "system", expand_macros(card.post_history_instructions, character, user)});
    }
    return parts;
}

std::vector<PromptMessage> build_prompt(const CharacterCard& card,
                                        const Settings& settings,
                                        const std::vector<ChatMessage>& history) {
    PromptParts parts = build_prompt_parts(card, settings, history);

    std::vector<PromptMessage> messages;
    messages.reserve(parts.leading_system.size() + parts.history.size() +
                     parts.trailing_system.size());
    for (PromptMessage& message : parts.leading_system) {
        messages.push_back(std::move(message));
    }
    for (PromptMessage& message : parts.history) {
        messages.push_back(std::move(message));
    }
    for (PromptMessage& message : parts.trailing_system) {
        messages.push_back(std::move(message));
    }
    return messages;
}

}  // namespace st
