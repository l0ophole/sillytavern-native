#include "core/prompt.h"

#include "core/identity.h"

#include <cstddef>
#include <string>

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

std::vector<PromptMessage> build_prompt(const CharacterCard& card,
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

    std::vector<PromptMessage> messages;
    if (!system_text.empty()) {
        messages.push_back(PromptMessage{"system", std::move(system_text)});
    }

    for (const ChatMessage& message : history) {
        if (message.mes.empty()) {
            continue;
        }
        const char* role = message.is_system ? "system" : (message.is_user ? "user" : "assistant");
        messages.push_back(PromptMessage{role, expand_macros(message.mes, character, user)});
    }

    if (!card.post_history_instructions.empty()) {
        messages.push_back(PromptMessage{
            "system", expand_macros(card.post_history_instructions, character, user)});
    }
    return messages;
}

}  // namespace st
