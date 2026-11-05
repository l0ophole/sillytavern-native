#include "core/prompt.h"

#include "core/identity.h"

#include <cstddef>
#include <regex>
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

std::vector<RegexRule> parse_regex_rules(const Json& value) {
    std::vector<RegexRule> rules;
    if (!value.is_array()) {
        return rules;
    }
    for (const Json& entry : value) {
        if (!entry.is_object()) {
            continue;
        }
        if (!entry.contains("pattern") || !entry["pattern"].is_string() ||
            !entry.contains("replacement") || !entry["replacement"].is_string()) {
            continue;
        }
        RegexRule rule;
        rule.pattern = entry["pattern"].get<std::string>();
        rule.replacement = entry["replacement"].get<std::string>();
        if (entry.contains("applies_to_user") && entry["applies_to_user"].is_boolean()) {
            rule.applies_to_user = entry["applies_to_user"].get<bool>();
        }
        if (entry.contains("applies_to_assistant") &&
            entry["applies_to_assistant"].is_boolean()) {
            rule.applies_to_assistant = entry["applies_to_assistant"].get<bool>();
        }
        rules.push_back(std::move(rule));
    }
    return rules;
}

std::string apply_regex_to_history(const std::string& text,
                                   const std::vector<RegexRule>& rules,
                                   bool is_user,
                                   bool is_system) {
    std::vector<RegexRule> filtered;
    for (const RegexRule& rule : rules) {
        const bool applies = is_system ? (rule.applies_to_user || rule.applies_to_assistant)
                                       : (is_user ? rule.applies_to_user : rule.applies_to_assistant);
        if (applies) {
            filtered.push_back(rule);
        }
    }
    return apply_regex(text, filtered);
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
                          const std::string& user,
                          const std::map<std::string, std::string>& variables) {
    constexpr const char* kVarPrefix = "{{var:";
    constexpr std::size_t kVarPrefixLength = 6;
    std::string result;
    result.reserve(text.size());

    std::size_t offset = 0;
    while (offset < text.size()) {
        // {{var:name}} (case-sensitive prefix and name) expands after
        // {{char}}/{{user}}; inserted values are not rescanned (single pass).
        if (text.compare(offset, kVarPrefixLength, kVarPrefix) == 0) {
            const std::size_t close = text.find("}}", offset + kVarPrefixLength);
            if (close != std::string::npos) {
                const std::string name =
                    text.substr(offset + kVarPrefixLength, close - (offset + kVarPrefixLength));
                bool valid = !name.empty() && name.size() <= 64;
                for (const char ch : name) {
                    const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                                    (ch >= '0' && ch <= '9') || ch == '_';
                    if (!ok) {
                        valid = false;
                        break;
                    }
                }
                if (valid) {
                    const auto found = variables.find(name);
                    if (found != variables.end()) {
                        result += found->second;
                    } else {
                        result += text.substr(offset, close + 2 - offset);
                    }
                    offset = close + 2;
                    continue;
                }
            }
        }
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

std::string expand_macros(const std::string& text,
                          const std::string& character,
                          const std::string& user) {
    return expand_macros(text, character, user, std::map<std::string, std::string>{});
}

std::string apply_regex(const std::string& text, const std::vector<RegexRule>& rules) {
    std::string result = text;
    const std::size_t count = rules.size() < 16 ? rules.size() : 16;
    for (std::size_t index = 0; index < count; ++index) {
        const RegexRule& rule = rules[index];
        if (rule.pattern.empty() || rule.pattern.size() > 512 || rule.replacement.size() > 4096) {
            continue;
        }
        try {
            const std::regex expression(rule.pattern);
            result = std::regex_replace(result, expression, rule.replacement);
        } catch (const std::regex_error&) {
            continue;
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
    const std::vector<RegexRule> regex_rules = parse_regex_rules(settings.regex_rules);

    // Manager precedence (pinned): when a PromptManager is present its
    // enabled sections drive which blocks are emitted, but the legacy
    // Settings flags still gate the *builtin* (empty-content) world_info
    // and authors_note text — so a manager cannot silently re-enable lore
    // or notes the user turned off. A section with non-empty custom
    // content is an explicit per-preset override and bypasses those flags.
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
            std::string content = expand_macros(message.mes, character, user, settings.variables);
            content = apply_regex_to_history(content, regex_rules, message.is_user, message.is_system);
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
                return expand_macros(section.content, character, user, settings.variables);
            }
            return builtin;
        };

        for (const PromptSection& section : ordered) {
            if (section.id == "system") {
                if (!section.content.empty()) {
                    append_section(buffer, expand_macros(section.content, character, user, settings.variables));
                } else if (settings.instruct_mode && !settings.context_template.empty()) {
                    append_section(buffer,
                                   expand_macros(settings.context_template, character, user, settings.variables));
                    if (!card.system_prompt.empty()) {
                        append_section(buffer,
                                       expand_macros(card.system_prompt, character, user, settings.variables));
                    }
                } else {
                    const std::string& base_prompt = card.system_prompt.empty()
                                                         ? settings.system_prompt
                                                         : card.system_prompt;
                    append_section(buffer, expand_macros(base_prompt, character, user, settings.variables));
                }
            } else if (section.id == "description") {
                append_section(buffer,
                               section_text(section, expand_macros(card.description, character, user, settings.variables)));
            } else if (section.id == "personality") {
                std::string builtin;
                if (!card.personality.empty()) {
                    builtin = expand_macros(character + "'s personality: " + card.personality,
                                            character, user, settings.variables);
                }
                append_section(buffer, section_text(section, std::move(builtin)));
            } else if (section.id == "scenario") {
                std::string builtin;
                if (!card.scenario.empty()) {
                    builtin = expand_macros("Scenario: " + card.scenario, character, user, settings.variables);
                }
                append_section(buffer, section_text(section, std::move(builtin)));
            } else if (section.id == "examples") {
                std::string builtin;
                if (!card.mes_example.empty()) {
                    builtin = expand_macros("Example dialogues:\n" + card.mes_example, character, user, settings.variables);
                }
                append_section(buffer, section_text(section, std::move(builtin)));
            } else if (section.id == "world_info") {
                flush_buffer();
                if (!section.content.empty()) {
                    parts.leading_system.push_back(PromptMessage{
                        "system", expand_macros(section.content, character, user, settings.variables)});
                } else if (settings.world_info_enabled && !book.entries.empty()) {
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
                        "system", expand_macros(section.content, character, user, settings.variables)});
                } else if (persona_store != nullptr) {
                    if (const Persona* active = persona_store->active()) {
                        if (!active->description.empty()) {
                            const std::string expanded =
                                expand_macros(active->description, character, user, settings.variables);
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
            // Legacy authors_note_enabled gates the builtin note; custom
            // section content overrides it (see manager precedence note).
            const std::string note =
                !authors->content.empty()
                    ? expand_macros(authors->content, character, user, settings.variables)
                    : (settings.authors_note_enabled
                           ? expand_macros(settings.authors_note, character, user, settings.variables)
                           : std::string{});
            if (!note.empty()) {
                insert_at_depth(parts.history, PromptMessage{"system", note},
                                settings.authors_note_depth);
            }
        }
        if (const PromptSection* post = find_section("post_history")) {
            std::string builtin;
            if (!card.post_history_instructions.empty()) {
                builtin = expand_macros(card.post_history_instructions, character, user, settings.variables);
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
                       expand_macros(settings.context_template, character, user, settings.variables));
        if (!card.system_prompt.empty()) {
            append_section(system_text, expand_macros(card.system_prompt, character, user, settings.variables));
        }
    } else {
        const std::string& base_prompt =
            card.system_prompt.empty() ? settings.system_prompt : card.system_prompt;
        append_section(system_text, expand_macros(base_prompt, character, user, settings.variables));
    }
    append_section(system_text, expand_macros(card.description, character, user, settings.variables));
    if (!card.personality.empty()) {
        append_section(system_text,
                       expand_macros(character + "'s personality: " + card.personality,
                                     character, user, settings.variables));
    }
    if (!card.scenario.empty()) {
        append_section(system_text,
                       expand_macros("Scenario: " + card.scenario, character, user, settings.variables));
    }
    if (!card.mes_example.empty()) {
        append_section(system_text,
                       expand_macros("Example dialogues:\n" + card.mes_example,
                                     character, user, settings.variables));
    }

    PromptParts parts;
    if (!system_text.empty()) {
        parts.leading_system.push_back(PromptMessage{"system", std::move(system_text)});
    }

    if (persona_store != nullptr) {
        if (const Persona* active = persona_store->active()) {
            if (!active->description.empty()) {
                const std::string expanded =
                    expand_macros(active->description, character, user, settings.variables);
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
        std::string content = expand_macros(message.mes, character, user, settings.variables);
        content = apply_regex_to_history(content, regex_rules, message.is_user, message.is_system);
        if (settings.instruct_mode) {
            content = format_instruct_message(role, content, settings.instruct_format);
        }
        parts.history.push_back(PromptMessage{role, std::move(content)});
    }

    if (settings.authors_note_enabled) {
        const std::string expanded_note = expand_macros(settings.authors_note, character, user, settings.variables);
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
            "system", expand_macros(card.post_history_instructions, character, user, settings.variables)});
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
