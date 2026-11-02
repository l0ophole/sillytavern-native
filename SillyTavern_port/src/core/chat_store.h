#pragma once

#include "core/chat_types.h"

#include <filesystem>
#include <string>
#include <vector>

namespace st {

class ChatStore {
public:
    explicit ChatStore(std::filesystem::path chats_root);

    std::filesystem::path chat_dir(const std::string& character) const;
    std::vector<std::string> list(const std::string& character) const;
    Chat load(const std::string& character, const std::string& filename) const;
    void save(const std::string& character,
              const std::string& filename,
              const Chat& chat) const;
    Chat create(const std::string& character,
                const std::string& user_name,
                const std::string& first_mes) const;
    void rename(const std::string& character,
                const std::string& from,
                const std::string& to) const;
    void remove(const std::string& character, const std::string& filename) const;

    const std::filesystem::path& root() const { return chats_root_; }

private:
    std::filesystem::path chats_root_;
};

}  // namespace st
