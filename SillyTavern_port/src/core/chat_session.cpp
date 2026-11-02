#include "core/chat_session.h"

#include "core/context_budget.h"
#include "core/identity.h"

#include <chrono>
#include <cstddef>
#include <ctime>
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
    const PromptParts parts = build_prompt_parts(card_, settings_, chat_.messages);

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
    chat_.messages.push_back(make_message(card_.name, false, text));
}

void ChatSession::save() const {
    store_.save(character_, filename_, chat_);
}

bool ChatSession::edit_message(std::size_t index, const std::string& text) {
    if (index >= chat_.messages.size()) {
        return false;
    }
    chat_.messages[index].mes = text;
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

bool ChatSession::can_continue() const {
    const std::size_t index = last_non_empty_index(chat_);
    if (index >= chat_.messages.size()) {
        return false;
    }
    const ChatMessage& last = chat_.messages[index];
    return !last.is_user && !last.is_system;
}

std::string ChatSession::continue_prefix() const {
    if (!can_continue()) {
        return {};
    }
    return chat_.messages[last_non_empty_index(chat_)].mes;
}

void ChatSession::append_to_last_assistant(const std::string& text) {
    if (!can_continue()) {
        return;
    }
    chat_.messages[last_non_empty_index(chat_)].mes += text;
}

}  // namespace st
