#include "core/chat_store.h"

#include "core/atomic_file.h"
#include "core/names.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace st {
namespace {

std::string read_string(const Json& object, const char* key) {
    if (!object.contains(key) || !object[key].is_string()) {
        return {};
    }
    return object[key].get<std::string>();
}

bool read_bool(const Json& object, const char* key) {
    return object.contains(key) && object[key].is_boolean() && object[key].get<bool>();
}

void erase_known(Json& value, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        value.erase(key);
    }
}

std::string utc_now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc {};
    gmtime_r(&seconds, &utc);
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

void validate_chat_filename(const std::string& filename) {
    if (filename.empty()) {
        throw std::runtime_error("chat filename is empty");
    }
    const fs::path path(filename);
    if (path.is_absolute() || path.has_parent_path() || filename == "." || filename == "..") {
        throw std::runtime_error("unsafe chat filename: " + filename);
    }
    if (path.extension() != ".jsonl" || path.stem().empty()) {
        throw std::runtime_error("chat filename must end with .jsonl: " + filename);
    }
}

ChatMetadata metadata_from_json(const Json& value) {
    ChatMetadata metadata;
    metadata.user_name = read_string(value, "user_name");
    metadata.character_name = read_string(value, "character_name");
    metadata.create_date = read_string(value, "create_date");
    if (value.contains("chat_metadata") && value["chat_metadata"].is_object()) {
        metadata.chat_metadata = value["chat_metadata"];
    }
    metadata.extra = value;
    erase_known(metadata.extra, {"user_name", "character_name", "create_date", "chat_metadata"});
    return metadata;
}

ChatMessage message_from_json(const Json& value) {
    ChatMessage message;
    message.name = read_string(value, "name");
    message.is_user = read_bool(value, "is_user");
    message.is_system = read_bool(value, "is_system");
    message.send_date = read_string(value, "send_date");
    message.mes = read_string(value, "mes");
    message.extra = value;
    erase_known(message.extra, {"name", "is_user", "is_system", "send_date", "mes"});
    return message;
}

}  // namespace

ChatStore::ChatStore(fs::path chats_root) : chats_root_(std::move(chats_root)) {}

fs::path ChatStore::chat_dir(const std::string& character) const {
    return safe_join(chats_root_, sanitize_filename(character, "character"));
}

std::vector<std::string> ChatStore::list(const std::string& character) const {
    std::vector<std::string> names;
    std::error_code error;
    const fs::path directory = chat_dir(character);
    if (!fs::is_directory(directory, error)) {
        return names;
    }
    for (const auto& item : fs::directory_iterator(directory, error)) {
        if (error) {
            break;
        }
        if (!item.is_regular_file()) {
            continue;
        }
        if (item.path().extension() == ".jsonl") {
            names.push_back(item.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

Chat ChatStore::load(const std::string& character, const std::string& filename) const {
    validate_chat_filename(filename);
    const fs::path path = safe_join(chat_dir(character), filename);
    const std::string text = read_file(path);

    Chat chat;
    bool metadata_seen = false;
    std::size_t line_number = 0;
    std::size_t offset = 0;

    while (offset <= text.size()) {
        const std::size_t newline = text.find('\n', offset);
        std::string line =
            newline == std::string::npos ? text.substr(offset) : text.substr(offset, newline - offset);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        ++line_number;

        if (!line.empty()) {
            const std::string location = filename + ": line " + std::to_string(line_number);
            const Json value = parse_json(line, location);
            if (!value.is_object()) {
                throw std::runtime_error(location + ": expected a JSON object");
            }
            if (!metadata_seen) {
                chat.metadata = metadata_from_json(value);
                metadata_seen = true;
            } else {
                chat.messages.push_back(message_from_json(value));
            }
        }

        if (newline == std::string::npos) {
            break;
        }
        offset = newline + 1;
    }

    if (!metadata_seen) {
        throw std::runtime_error(filename + ": missing chat metadata line");
    }
    return chat;
}

void ChatStore::save(const std::string& character,
                     const std::string& filename,
                     const Chat& chat) const {
    validate_chat_filename(filename);

    Json metadata = chat.metadata.extra.is_object() ? chat.metadata.extra : Json::object();
    metadata["user_name"] = chat.metadata.user_name;
    metadata["character_name"] = chat.metadata.character_name;
    metadata["create_date"] = chat.metadata.create_date;
    metadata["chat_metadata"] =
        chat.metadata.chat_metadata.is_object() ? chat.metadata.chat_metadata : Json::object();

    std::string output = dump_json(metadata) + "\n";
    for (const auto& message : chat.messages) {
        Json value = message.extra.is_object() ? message.extra : Json::object();
        value["name"] = message.name;
        value["is_user"] = message.is_user;
        value["is_system"] = message.is_system;
        value["send_date"] = message.send_date;
        value["mes"] = message.mes;
        output += dump_json(value) + "\n";
    }

    write_file_atomic(safe_join(chat_dir(character), filename), output);
}

Chat ChatStore::create(const std::string& character,
                       const std::string& user_name,
                       const std::string& first_mes) const {
    Chat chat;
    chat.metadata.user_name = user_name;
    chat.metadata.character_name = character;
    chat.metadata.create_date = utc_now();
    chat.metadata.chat_metadata = Json::object();
    if (!first_mes.empty()) {
        ChatMessage greeting;
        greeting.name = character;
        greeting.send_date = utc_now();
        greeting.mes = first_mes;
        chat.messages.push_back(std::move(greeting));
    }
    return chat;
}

void ChatStore::rename(const std::string& character,
                       const std::string& from,
                       const std::string& to) const {
    validate_chat_filename(from);
    validate_chat_filename(to);
    const fs::path directory = chat_dir(character);
    const fs::path from_path = safe_join(directory, from);
    const fs::path to_path = safe_join(directory, to);

    if (!fs::exists(from_path)) {
        throw std::runtime_error("chat not found: " + from);
    }
    if (fs::exists(to_path)) {
        throw std::runtime_error("chat already exists: " + to);
    }

    std::error_code error;
    fs::rename(from_path, to_path, error);
    if (error) {
        throw std::runtime_error("cannot rename chat " + from + " to " + to + ": " + error.message());
    }
}

void ChatStore::remove(const std::string& character, const std::string& filename) const {
    validate_chat_filename(filename);
    std::error_code error;
    fs::remove(safe_join(chat_dir(character), filename), error);
    if (error) {
        throw std::runtime_error("cannot remove chat " + filename + ": " + error.message());
    }
}

}  // namespace st
