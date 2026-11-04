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

std::string trim_text(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// Resolved {{user}} value: the active persona name wins; otherwise a
// non-empty Settings::persona_name; otherwise normalized settings.user_name.
// An explicitly supplied (possibly empty) PersonaStore means "in use" only
// when it has an active persona.
std::string effective_user(const Settings& settings, const PersonaStore* persona_store) {
    if (persona_store != nullptr) {
        if (const Persona* active = persona_store->active()) {
            if (!active->name.empty()) {
                return active->name;
            }
        }
        // Store in use without an active persona: keep legacy behavior,
        // falling back to settings.persona_name below.
    }
    const std::string persona_name = trim_text(settings.persona_name);
    if (!persona_name.empty()) {
        return persona_name;
    }
    return normalize_user_name(settings.user_name);
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

std::string format_instruct_message(const std::string& role,
                                    const std::string& content,
                                    const std::string& format) {
    if (format == "chatml") {
        return "<|im_start|>" + role + "\n" + content + "<|im_end|>\n";
    }
    if (format == "llama") {
        if (role == "user") {
            return "[INST] " + content + " [/INST]\n";
        }
        return content + "\n";
    }
    // "plain" and any unknown format: content unchanged.
    return content;
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
    return build_prompt_parts_with_book(card, settings, history, WorldInfoBook{}, nullptr);
}

PromptParts build_prompt_parts(const CharacterCard& card,
                               const Settings& settings,
                               const std::vector<ChatMessage>& history,
                               const PersonaStore* persona_store) {
    return build_prompt_parts_with_book(card, settings, history, WorldInfoBook{},
                                        persona_store, nullptr);
}

PromptParts build_prompt_parts(const CharacterCard& card,
                               const Settings& settings,
                               const std::vector<ChatMessage>& history,
                               const PersonaStore* persona_store,
                               const PromptManager* prompt_manager) {
    return build_prompt_parts_with_book(card, settings, history, WorldInfoBook{},
                                        persona_store, prompt_manager);
}

PromptParts build_prompt_parts_with_book(const CharacterCard& card,
                                         const Settings& settings,
                                         const std::vector<ChatMessage>& history,
                                         const WorldInfoBook& book) {
    return build_prompt_parts_with_book(card, settings, history, book, nullptr, nullptr);
}

PromptParts build_prompt_parts_with_book(const CharacterCard& card,
                                         const Settings& settings,
                                         const std::vector<ChatMessage>& history,
                                         const WorldInfoBook& book,
                                         const PersonaStore* persona_store) {
    return build_prompt_parts_with_book(card, settings, history, book, persona_store, nullptr);
}

PromptParts build_prompt_parts_with_book(const CharacterCard& card,
                                         const Settings& settings,
                                         const std::vector<ChatMessage>& history,
                                         const WorldInfoBook& book,
                                         const PersonaStore* persona_store,
                                         const PromptManager* prompt_manager) {
    const std::string character = card.name.empty() ? "Character" : card.name;
    const std::string user = effective_user(settings, persona_store);

    if (prompt_manager != nullptr) {
        const std::vector<PromptSection> ordered = prompt_manager->enabled_sections();
        auto find_section = [&ordered](const char* id) -> const PromptSection* {
            for (const PromptSection& section : ordered) {
                if (section.id == id) {
                    return &section;
                }
            }
            return nullptr;
        };

        PromptParts parts;
        for (const ChatMessage& message : history) {
            if (message.mes.empty()) {
                continue;
            }
            const char* role =
                message.is_system ? "system" : (message.is_user ? "user" : "assistant");
            std::string content = expand_macros(message.mes, character, user);
            if (settings.instruct_mode) {
                content = format_instruct_message(role, content, settings.instruct_format);
            }
            parts.history.push_back(PromptMessage{role, std::move(content)});
        }

        std::string buffer;
        auto flush_buffer = [&parts, &buffer]() {
            if (!buffer.empty()) {
                parts.leading_system.push_back(PromptMessage{"system", buffer});
                buffer.clear();
            }
        };
        auto section_text = [&](const PromptSection& section, const std::string& builtin) {
            if (!section.content.empty()) {
                return expand_macros(section.content, character, user);
            }
            return builtin;
        };

        for (const PromptSection& section : ordered) {
            if (section.id == "system") {
                if (!section.content.empty()) {
                    append_section(buffer, expand_macros(section.content, character, user));
                } else if (settings.instruct_mode && !settings.context_template.empty()) {
                    append_section(buffer,
                                   expand_macros(settings.context_template, character, user));
                    if (!card.system_prompt.empty()) {
                        append_section(buffer,
                                       expand_macros(card.system_prompt, character, user));
                    }
                } else {
                    const std::string& base_prompt = card.system_prompt.empty()
                                                         ? settings.system_prompt
                                                         : card.system_prompt;
                    append_section(buffer, expand_macros(base_prompt, character, user));
                }
            } else if (section.id == "description") {
                append_section(buffer,
                               section_text(section, expand_macros(card.description, character,
                                                                   user)));
            } else if (section.id == "personality") {
                std::string builtin;
                if (!card.personality.empty()) {
                    builtin = expand_macros(character + "'s personality: " + card.personality,
                                            character, user);
                }
                append_section(buffer, section_text(section, std::move(builtin)));
            } else if (section.id == "scenario") {
                std::string builtin;
                if (!card.scenario.empty()) {
                    builtin = expand_macros("Scenario: " + card.scenario, character, user);
                }
                append_section(buffer, section_text(section, std::move(builtin)));
            } else if (section.id == "examples") {
                std::string builtin;
                if (!card.mes_example.empty()) {
                    builtin = expand_macros("Example dialogues:\n" + card.mes_example, character,
                                            user);
                }
                append_section(buffer, section_text(section, std::move(builtin)));
            } else if (section.id == "world_info") {
                flush_buffer();
                if (!section.content.empty()) {
                    parts.leading_system.push_back(PromptMessage{
                        "system", expand_macros(section.content, character, user)});
                } else if (!book.entries.empty()) {
                    std::vector<std::string> texts;
                    for (const ChatMessage& message : history) {
                        if (!message.mes.empty()) {
                            texts.push_back(message.mes);
                        }
                    }
                    const std::vector<WorldInfoEntry> hits = triggered_entries(book, texts);
                    if (!hits.empty()) {
                        std::string joined;
                        for (const WorldInfoEntry& entry : hits) {
                            if (!joined.empty()) {
                                joined += "\n\n";
                            }
                            joined += entry.content;
                        }
                        if (!joined.empty()) {
                            parts.leading_system.push_back(
                                PromptMessage{"system", "World Info:\n" + joined});
                        }
                    }
                }
            } else if (section.id == "persona") {
                flush_buffer();
                if (!section.content.empty()) {
                    parts.leading_system.push_back(PromptMessage{
                        "system", expand_macros(section.content, character, user)});
                } else if (persona_store != nullptr) {
                    if (const Persona* active = persona_store->active()) {
                        if (!active->description.empty()) {
                            const std::string expanded =
                                expand_macros(active->description, character, user);
                            if (!expanded.empty()) {
                                parts.leading_system.push_back(PromptMessage{
                                    "system", "User persona (" + active->name + "): " + expanded});
                            }
                        }
                    }
                }
            }
        }
        flush_buffer();

        if (const PromptSection* authors = find_section("authors_note")) {
            const std::string note = section_text(*authors,
                                                  expand_macros(settings.authors_note, character,
                                                                user));
            if (!note.empty()) {
                insert_at_depth(parts.history, PromptMessage{"system", note},
                                settings.authors_note_depth);
            }
        }
        if (const PromptSection* post = find_section("post_history")) {
            std::string builtin;
            if (!card.post_history_instructions.empty()) {
                builtin = expand_macros(card.post_history_instructions, character, user);
            }
            const std::string text = section_text(*post, std::move(builtin));
            if (!text.empty()) {
                parts.trailing_system.push_back(PromptMessage{"system", text});
            }
        }
        return parts;
    }

    std::string system_text;
    if (settings.instruct_mode && !settings.context_template.empty()) {
        // Template replaces the default leading system text; the character
        // system prompt, if set, is still appended after it (template first).
        append_section(system_text,
                       expand_macros(settings.context_template, character, user));
        if (!card.system_prompt.empty()) {
            append_section(system_text, expand_macros(card.system_prompt, character, user));
        }
    } else {
        const std::string& base_prompt =
            card.system_prompt.empty() ? settings.system_prompt : card.system_prompt;
        append_section(system_text, expand_macros(base_prompt, character, user));
    }
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

    if (persona_store != nullptr) {
        if (const Persona* active = persona_store->active()) {
            if (!active->description.empty()) {
                const std::string expanded =
                    expand_macros(active->description, character, user);
                if (!expanded.empty()) {
                    parts.leading_system.push_back(PromptMessage{
                        "system", "User persona (" + active->name + "): " + expanded});
                }
            }
        }
    }

    for (const ChatMessage& message : history) {
        if (message.mes.empty()) {
            continue;
        }
        const char* role = message.is_system ? "system" : (message.is_user ? "user" : "assistant");
        std::string content = expand_macros(message.mes, character, user);
        if (settings.instruct_mode) {
            content = format_instruct_message(role, content, settings.instruct_format);
        }
        parts.history.push_back(PromptMessage{role, std::move(content)});
    }

    if (settings.authors_note_enabled) {
        const std::string expanded_note = expand_macros(settings.authors_note, character, user);
        if (!expanded_note.empty()) {
            insert_at_depth(parts.history, PromptMessage{"system", expanded_note},
                            settings.authors_note_depth);
        }
    }

    if (settings.world_info_enabled && !book.entries.empty()) {
        std::vector<std::string> texts;
        for (const ChatMessage& message : history) {
            if (!message.mes.empty()) {
                texts.push_back(message.mes);
            }
        }
        // The last user message is already included above; scanning it once is
        // enough because triggered_entries deduplicates by key+content.
        const std::vector<WorldInfoEntry> hits = triggered_entries(book, texts);
        if (!hits.empty()) {
            std::string joined;
            for (const WorldInfoEntry& entry : hits) {
                if (!joined.empty()) {
                    joined += "\n\n";
                }
                joined += entry.content;
            }
            if (!joined.empty()) {
                parts.leading_system.push_back(
                    PromptMessage{"system", "World Info:\n" + joined});
            }
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
    return build_prompt(card, settings, history, nullptr);
}

std::vector<PromptMessage> build_prompt(const CharacterCard& card,
                                        const Settings& settings,
                                        const std::vector<ChatMessage>& history,
                                        const PersonaStore* persona_store) {
    PromptParts parts = build_prompt_parts(card, settings, history, persona_store);

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
