#pragma once

#include "core/chat_types.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace st {

struct ChatDraft {
    std::string text;
    bool continue_mode = false;
    std::string send_date;
    std::size_t base_messages = 0;
    bool replace_last = false;
};

class ChatStore {
public:
    explicit ChatStore(std::filesystem::path chats_root);

    std::filesystem::path chat_dir(const std::string& character) const;
    std::filesystem::path lock_path(const std::string& character,
                                    const std::string& filename) const;
    std::filesystem::path draft_path(const std::string& character,
                                     const std::string& filename) const;
    void save_draft(const std::string& character,
                    const std::string& filename,
                    const ChatDraft& draft) const;
    std::optional<ChatDraft> load_draft(const std::string& character,
                                        const std::string& filename) const;
    void clear_draft(const std::string& character, const std::string& filename) const;
    std::vector<std::string> list(const std::string& character) const;
    Chat load(const std::string& character, const std::string& filename) const;
    void save(const std::string& character,
              const std::string& filename,
              const Chat& chat) const;
    Chat create(const std::string& character,
                const std::string& user_name,
                const std::string& first_mes,
                const std::string& display_name = {}) const;
    void rename(const std::string& character,
                const std::string& from,
                const std::string& to) const;
    void remove(const std::string& character, const std::string& filename) const;
    bool exists(const std::string& character, const std::string& filename) const;

    const std::filesystem::path& root() const { return chats_root_; }

private:
    std::filesystem::path chats_root_;
};

}  // namespace st
