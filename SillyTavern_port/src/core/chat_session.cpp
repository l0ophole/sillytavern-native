#include "core/chat_session.h"

#include "core/atomic_file.h"
#include "core/context_budget.h"
#include "core/identity.h"
#include "core/names.h"

#include <cctype>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace st {
namespace {

std::string utc_now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc {};
    gmtime_r(&seconds, &utc);
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

ChatMessage make_message(const std::string& name, bool is_user, const std::string& text) {
    ChatMessage message;
    message.name = name;
    message.is_user = is_user;
    message.send_date = utc_now();
    message.mes = text;
    return message;
}

bool is_assistant(const ChatMessage& message) {
    return !message.is_user && !message.is_system;
}

// Defensive read of the swipe array: any missing or malformed `swipes`
// entry (not an array, empty, or containing a non-string) falls back to a
// single swipe equal to `mes`.
std::vector<std::string> read_swipes(const ChatMessage& message) {
    if (!message.extra.is_object()) {
        return {message.mes};
    }
    const auto it = message.extra.find("swipes");
    if (it == message.extra.end() || !it->is_array() || it->empty()) {
        return {message.mes};
    }
    std::vector<std::string> swipes;
    swipes.reserve(it->size());
    for (const Json& entry : *it) {
        if (!entry.is_string()) {
            return {message.mes};
        }
        swipes.push_back(entry.get<std::string>());
    }
    return swipes;
}

// 0-based `swipe_id` clamped into [0, count); missing or malformed values
// read as 0.
std::size_t read_swipe_id(const ChatMessage& message, std::size_t count) {
    if (!message.extra.is_object() || count == 0) {
        return 0;
    }
    const auto it = message.extra.find("swipe_id");
    if (it == message.extra.end()) {
        return 0;
    }
    if (it->is_number_integer()) {
        const long long id = it->get<long long>();
        if (id < 0 || static_cast<unsigned long long>(id) >= count) {
            return 0;
        }
        return static_cast<std::size_t>(id);
    }
    if (it->is_number_unsigned()) {
        const unsigned long long id = it->get<unsigned long long>();
        if (id >= count) {
            return 0;
        }
        return static_cast<std::size_t>(id);
    }
    return 0;
}

void write_swipes(ChatMessage& message, const std::vector<std::string>& swipes, std::size_t id) {
    Json array = Json::array();
    for (const std::string& text : swipes) {
        array.push_back(text);
    }
    message.extra["swipes"] = array;
    message.extra["swipe_id"] = static_cast<int>(id);
}

// Keeps the selected swipe entry aligned with `mes` when a swipe array is
// already present; a message without one is left untouched.
void sync_swipe(ChatMessage& message) {
    if (!is_assistant(message) || !message.extra.is_object()) {
        return;
    }
    const auto it = message.extra.find("swipes");
    if (it == message.extra.end() || !it->is_array() || it->empty()) {
        return;
    }
    std::vector<std::string> swipes;
    for (const Json& entry : *it) {
        if (!entry.is_string()) {
            return;  // array was malformed; leave it as-is
        }
        swipes.push_back(entry.get<std::string>());
    }
    const std::size_t id = read_swipe_id(message, swipes.size());
    swipes[id] = message.mes;
    write_swipes(message, swipes, id);
}

bool needs_separator(const std::string& tail, const std::string& text) {
    if (tail.empty() || text.empty()) {
        return false;
    }
    const unsigned char last = static_cast<unsigned char>(tail.back());
    const unsigned char first = static_cast<unsigned char>(text.front());
    if (std::isspace(last) != 0 || std::isspace(first) != 0) {
        return false;
    }
    return std::string(".,!?;:)]}\"'").find(static_cast<char>(first)) == std::string::npos;
}

std::size_t last_non_empty_index(const Chat& chat) {
    for (std::size_t index = chat.messages.size(); index > 0; --index) {
        if (!chat.messages[index - 1].mes.empty()) {
            return index - 1;
        }
    }
    return chat.messages.size();
}

}  // namespace

ChatSession::ChatSession(CharacterCard card,
                         Settings settings,
                         ChatStore store,
                         std::string character,
                         std::string filename,
                         Chat chat)
    : card_(std::move(card)),
      settings_(std::move(settings)),
      store_(std::move(store)),
      character_(std::move(character)),
      filename_(std::move(filename)),
      chat_(std::move(chat)) {}

std::vector<PromptMessage> ChatSession::prompt_messages() const {
    const PersonaStore* persona = persona_store_set_ ? &persona_store_ : nullptr;
    const PromptParts parts = build_prompt_parts_with_book(card_, settings_, chat_.messages, world_info_book_, persona);

    std::vector<PromptMessage> reserved = parts.leading_system;
    reserved.insert(reserved.end(), parts.trailing_system.begin(), parts.trailing_system.end());

    const BudgetResult budget =
        fit_history(reserved, parts.history, settings_.context_size, settings_.max_tokens);

    std::vector<PromptMessage> messages;
    messages.reserve(parts.leading_system.size() + budget.history.size() +
                     parts.trailing_system.size());
    for (const PromptMessage& message : parts.leading_system) {
        messages.push_back(message);
    }
    for (const PromptMessage& message : budget.history) {
        messages.push_back(message);
    }
    for (const PromptMessage& message : parts.trailing_system) {
        messages.push_back(message);
    }

    estimated_tokens_ = budget.estimated_tokens;
    trimmed_ = budget.trimmed;
    return messages;
}

void ChatSession::set_world_info_book(const WorldInfoBook& book) {
    world_info_book_ = book;
}

void ChatSession::set_persona_store(const PersonaStore& store) {
    persona_store_ = store;
    persona_store_set_ = true;
}

const PersonaStore* ChatSession::persona_store() const {
    return persona_store_set_ ? &persona_store_ : nullptr;
}

void ChatSession::load_persona_store(const std::filesystem::path& data_root) {
    persona_store_ = PersonaStore::load(data_root / "personas.json");
    persona_store_set_ = true;
}

void ChatSession::save_persona_store(const std::filesystem::path& data_root) const {
    if (persona_store_set_) {
        persona_store_.save(data_root / "personas.json");
    }
}

void ChatSession::load_world_info_book(const std::filesystem::path& dir,
                                       const std::string& book_name) {
    world_info_book_ = WorldInfoBook{};
    const std::string sanitized = sanitize_filename(book_name, "lorebook");
    const std::filesystem::path path = safe_join(dir, sanitized + ".json");
    if (!file_exists(path)) {
        return;
    }
    const std::string text = read_file_limited(path, 16u * 1024u * 1024u, "world info");
    world_info_book_ = WorldInfoBook::from_json(parse_json(text, "world info"));
    world_info_book_.name = sanitized;
}

OpenAiRequest ChatSession::request() const {
    OpenAiRequest request;
    request.base_url = settings_.api_base_url;
    request.api_key = settings_.api_key;
    request.model = settings_.model;
    request.messages = prompt_messages();
    request.sampling = sampling_from_settings(settings_);
    request.timeout_seconds = settings_.request_timeout_seconds;
    return request;
}

void ChatSession::append_user(const std::string& text) {
    chat_.messages.push_back(
        make_message(normalize_user_name(settings_.user_name), true, text));
}

void ChatSession::append_assistant(const std::string& text) {
    ChatMessage message = make_message(card_.name, false, text);
    write_swipes(message, {text}, 0);
    chat_.messages.push_back(std::move(message));
}

std::size_t ChatSession::swipe_count(std::size_t index) const {
    if (index >= chat_.messages.size()) {
        return 0;
    }
    const ChatMessage& message = chat_.messages[index];
    if (!is_assistant(message)) {
        return 1;
    }
    return read_swipes(message).size();
}

std::size_t ChatSession::swipe_index(std::size_t index) const {
    if (index >= chat_.messages.size()) {
        return 0;
    }
    const ChatMessage& message = chat_.messages[index];
    if (!is_assistant(message)) {
        return 0;
    }
    return read_swipe_id(message, read_swipes(message).size());
}

bool ChatSession::select_swipe(std::size_t index, std::size_t swipe) {
    if (index >= chat_.messages.size()) {
        return false;
    }
    ChatMessage& message = chat_.messages[index];
    if (!is_assistant(message)) {
        return false;
    }
    const std::vector<std::string> swipes = read_swipes(message);
    if (swipe >= swipes.size()) {
        return false;
    }
    write_swipes(message, swipes, swipe);
    message.mes = swipes[swipe];
    return true;
}

bool ChatSession::add_swipe(std::size_t index, const std::string& text) {
    if (index >= chat_.messages.size() || text.empty()) {
        return false;
    }
    ChatMessage& message = chat_.messages[index];
    if (!is_assistant(message)) {
        return false;
    }
    std::vector<std::string> swipes = read_swipes(message);
    swipes.push_back(text);
    write_swipes(message, swipes, swipes.size() - 1);
    message.mes = text;
    return true;
}

Chat ChatSession::branch(std::size_t index) const {
    if (index >= chat_.messages.size()) {
        return Chat{};
    }
    Chat copy = chat_;
    copy.messages.resize(index + 1);
    copy.metadata.create_date = utc_now();
    return copy;
}

bool ChatSession::add_bookmark(std::size_t index, const std::string& label) {
    if (index >= chat_.messages.size()) {
        return false;
    }
    Json& metadata = chat_.metadata.chat_metadata;
    if (!metadata.is_object()) {
        metadata = Json::object();
    }
    Json& entries = metadata["bookmarks"];
    if (!entries.is_array()) {
        entries = Json::array();
    }
    Json entry = Json::object();
    entry["index"] = static_cast<int>(index);
    entry["label"] = label;
    entry["create_date"] = utc_now();
    entries.push_back(entry);
    return true;
}

bool ChatSession::remove_bookmark(std::size_t index) {
    Json& metadata = chat_.metadata.chat_metadata;
    if (!metadata.is_object()) {
        return false;
    }
    const auto it = metadata.find("bookmarks");
    if (it == metadata.end() || !it->is_array()) {
        return false;
    }
    Json kept = Json::array();
    bool removed = false;
    for (const Json& entry : *it) {
        auto entry_index = [&entry]() -> std::optional<long long> {
            if (!entry.is_object()) {
                return std::nullopt;
            }
            const auto field = entry.find("index");
            if (field == entry.end()) {
                return std::nullopt;
            }
            if (field->is_number_integer()) {
                return field->get<long long>();
            }
            if (field->is_number_unsigned()) {
                return static_cast<long long>(field->get<unsigned long long>());
            }
            return std::nullopt;
        }();
        if (entry_index.has_value() &&
            *entry_index == static_cast<long long>(index)) {
            removed = true;
            continue;
        }
        kept.push_back(entry);
    }
    if (!removed) {
        return false;
    }
    metadata["bookmarks"] = kept;
    return true;
}

std::vector<std::pair<std::size_t, std::string>> ChatSession::bookmarks() const {
    std::vector<std::pair<std::size_t, std::string>> result;
    const Json& metadata = chat_.metadata.chat_metadata;
    if (!metadata.is_object()) {
        return result;
    }
    const auto it = metadata.find("bookmarks");
    if (it == metadata.end() || !it->is_array()) {
        return result;
    }
    for (const Json& entry : *it) {
        if (!entry.is_object()) {
            continue;
        }
        const auto index_it = entry.find("index");
        const auto label_it = entry.find("label");
        if (index_it == entry.end() || label_it == entry.end() ||
            !label_it->is_string()) {
            continue;
        }
        long long raw = -1;
        if (index_it->is_number_integer()) {
            raw = index_it->get<long long>();
        } else if (index_it->is_number_unsigned()) {
            const unsigned long long value = index_it->get<unsigned long long>();
            if (value > 9223372036854775807ULL) {
                continue;
            }
            raw = static_cast<long long>(value);
        } else {
            continue;
        }
        if (raw < 0 || chat_.messages.empty()) {
            continue;
        }
        std::size_t index = static_cast<std::size_t>(raw);
        if (index >= chat_.messages.size()) {
            index = chat_.messages.size() - 1;
        }
        result.emplace_back(index, label_it->get<std::string>());
    }
    return result;
}

bool ChatSession::recover_draft(const ChatDraft& draft) {
    if (draft.text.empty()) {
        return false;
    }
    if (draft.replace_last) {
        if (chat_.messages.empty() ||
            draft.base_messages != chat_.messages.size() - 1) {
            return false;
        }
        const ChatMessage& last = chat_.messages.back();
        if (last.is_user || last.is_system) {
            return false;
        }
        ChatMessage& target = chat_.messages.back();
        std::vector<std::string> swipes = read_swipes(target);
        swipes.push_back(draft.text);
        write_swipes(target, swipes, swipes.size() - 1);
        target.mes = draft.text;
        if (!draft.send_date.empty()) {
            target.send_date = draft.send_date;
        }
        return true;
    }
    if (draft.base_messages != chat_.messages.size()) {
        return false;
    }
    if (draft.continue_mode && can_continue()) {
        append_to_last_assistant(draft.text);
        return true;
    }
    ChatMessage message = make_message(card_.name, false, draft.text);
    write_swipes(message, {draft.text}, 0);
    if (!draft.send_date.empty()) {
        message.send_date = draft.send_date;
    }
    chat_.messages.push_back(std::move(message));
    return true;
}

void ChatSession::save() const {
    store_.save(character_, filename_, chat_);
}

Chat ChatSession::snapshot() const {
    return chat_;
}

void ChatSession::restore(Chat chat) {
    chat_ = std::move(chat);
}

bool ChatSession::edit_message(std::size_t index, const std::string& text) {
    if (index >= chat_.messages.size()) {
        return false;
    }
    chat_.messages[index].mes = text;
    sync_swipe(chat_.messages[index]);
    return true;
}

bool ChatSession::delete_message(std::size_t index) {
    if (index >= chat_.messages.size()) {
        return false;
    }
    chat_.messages.erase(chat_.messages.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool ChatSession::regenerate() {
    if (chat_.messages.empty()) {
        return false;
    }
    const ChatMessage& last = chat_.messages.back();
    if (last.is_user || last.is_system) {
        return false;
    }
    chat_.messages.pop_back();
    return true;
}

bool ChatSession::can_regenerate_at(std::size_t index) const {
    if (index + 1 != chat_.messages.size()) {
        return false;
    }
    const ChatMessage& message = chat_.messages[index];
    return !message.is_user && !message.is_system;
}

bool ChatSession::can_continue() const {
    const std::size_t index = last_non_empty_index(chat_);
    if (index >= chat_.messages.size()) {
        return false;
    }
    const ChatMessage& last = chat_.messages[index];
    return !last.is_user && !last.is_system;
}

bool ChatSession::can_continue_at(std::size_t index) const {
    if (index >= chat_.messages.size() || index != last_non_empty_index(chat_)) {
        return false;
    }
    const ChatMessage& message = chat_.messages[index];
    return !message.is_user && !message.is_system;
}

void ChatSession::set_filename(std::string filename) {
    filename_ = std::move(filename);
}

std::string ChatSession::continue_prefix() const {
    if (!can_continue()) {
        return {};
    }
    return chat_.messages[last_non_empty_index(chat_)].mes;
}

void ChatSession::append_to_last_assistant(const std::string& text) {
    if (!can_continue() || text.empty()) {
        return;
    }
    ChatMessage& target = chat_.messages[last_non_empty_index(chat_)];
    std::string& message = target.mes;
    if (needs_separator(message, text)) {
        message += ' ';
    }
    message += text;
    sync_swipe(target);
}

}  // namespace st
