#pragma once

#include "core/json.h"

#include <string>
#include <vector>

namespace st {

struct ChatMessage {
    std::string name;
    bool is_user = false;
    bool is_system = false;
    std::string send_date;
    std::string mes;
    Json extra = Json::object();
};

struct ChatMetadata {
    std::string user_name;
    std::string character_name;
    std::string create_date;
    Json chat_metadata = Json::object();
    Json extra = Json::object();
};

struct Chat {
    ChatMetadata metadata;
    std::vector<ChatMessage> messages;
};

}  // namespace st
