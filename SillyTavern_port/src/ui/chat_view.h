#pragma once

#include "core/chat_session.h"
#include "core/character_store.h"
#include "core/chat_store.h"
#include "core/draft_throttle.h"
#include "core/paths.h"
#include "core/settings.h"
#include "core/world_info.h"

#include <gtkmm.h>
#include <glibmm/dispatcher.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace st {
namespace ui {

class ChatView : public Gtk::Box {
public:
    ChatView(st::ChatStore& chats,
             st::Settings& settings,
             std::function<void(const std::string&)> on_status,
             const st::Paths* paths = nullptr);
    ~ChatView() override;

    bool set_character(const st::CharacterEntry& entry);
    bool set_character_async(const st::CharacterEntry& entry);
    bool load_chat_async(const std::string& filename);
    void clear();
    void refresh_settings();
    bool submit_message(const std::string& text);
    bool rename_current_chat(const std::string& name);
    bool delete_current_chat();
    const std::string& current_filename() const { return filename_; }
    bool is_streaming() const { return streaming_; }
    bool is_loading() const { return loading_; }
    void on_regenerate();
    bool swipe_current(int delta);
    bool branch_from(std::size_t index);
    void set_world_info_book(const st::WorldInfoBook& book);
    const st::WorldInfoBook& world_info_book() const { return world_info_book_; }

private:
    struct ChatLoadResult {
        st::Chat chat;
        std::string filename;
        std::uint64_t generation = 0;
        std::optional<st::ChatDraft> draft;
        bool recovered = false;
        std::string draft_error;
        std::string error;
    };

    void load_chat_in_background(std::string character_key,
                                 std::string filename,
                                 std::uint64_t generation);
    bool apply_load_result(ChatLoadResult& result);
    void on_load_dispatcher();
    void stop_loading();

    void on_send();
    void on_cancel();
    void on_new_chat();
    void on_chat_selected();
    void on_rename_chat();
    void on_delete_chat();
    void on_dispatcher();
    bool on_composer_key_press(GdkEventKey* event);
    bool on_transcript_button_press(GdkEventButton* event);

    void on_edit_message();
    void on_delete_message();
    void on_continue();
    void on_add_bookmark();
    void rebuild_bookmarks_menu();
    void jump_to_bookmark(std::size_t index);

    bool start_generation(st::OpenAiRequest request, bool continue_mode = false);
    void finish_generation(const st::OpenAiResult& result);
    void stop_streaming();
    std::string collect_streaming_text();
    void maybe_save_draft();

    bool create_new_chat(bool announce);
    bool load_chat(const std::string& filename);
    void load_world_info_book();
    void apply_world_info_book();
    std::string new_chat_filename() const;
    void refresh_selector();
    void refresh_transcript();
    void update_budget_label();
    void update_controls();
    void append_streaming_display();
    void scroll_to_end();
    void set_status(const std::string& text);
    std::size_t message_index_at_offset(std::size_t offset) const;
    Gtk::Window* top_window();

    st::ChatStore& chats_;
    st::Settings& settings_;
    std::function<void(const std::string&)> on_status_;
    const st::Paths* paths_ = nullptr;
    st::WorldInfoBook world_info_book_;

    st::CharacterCard card_;
    std::string character_key_;
    std::string character_name_;
    std::string filename_;
    std::unique_ptr<st::ChatSession> session_;
    std::optional<st::Chat> regenerate_backup_;

    Gtk::Box top_bar_;
    Gtk::ComboBoxText chat_selector_;
    Gtk::Label budget_label_;
    Gtk::Button new_button_;
    Gtk::Button rename_button_;
    Gtk::Button delete_button_;
    Gtk::ScrolledWindow transcript_scroll_;
    Gtk::TextView transcript_view_;
    Glib::RefPtr<Gtk::TextTag> header_tag_;
    Gtk::Menu context_menu_;
    Gtk::MenuItem edit_item_;
    Gtk::MenuItem delete_item_;
    Gtk::MenuItem regenerate_item_;
    Gtk::MenuItem continue_item_;
    Gtk::MenuItem swipe_prev_item_;
    Gtk::MenuItem swipe_next_item_;
    Gtk::MenuItem branch_item_;
    Gtk::MenuItem bookmarks_item_;
    Gtk::Menu bookmarks_menu_;
    struct MessageRange {
        std::size_t begin = 0;
        std::size_t end = 0;
    };
    std::vector<MessageRange> message_ranges_;
    std::size_t context_index_ = 0;
    Gtk::ScrolledWindow composer_scroll_;
    Gtk::TextView composer_view_;
    Gtk::Box composer_row_;
    Gtk::Box button_box_;
    Gtk::Button send_button_;
    Gtk::Button cancel_button_;

    Glib::Dispatcher dispatcher_;
    std::thread worker_;
    std::shared_ptr<st::OpenAiClient> client_;
    std::mutex chunk_mutex_;
    std::string pending_chunks_;
    bool final_ready_ = false;
    st::OpenAiResult final_result_;
    bool streaming_ = false;
    std::string streaming_text_;
    bool continue_mode_ = false;
    bool updating_selector_ = false;
    std::string draft_send_date_;
    std::size_t base_messages_ = 0;
    bool draft_replace_last_ = false;
    st::DraftThrottle draft_throttle_;

    Glib::Dispatcher load_dispatcher_;
    std::thread load_worker_;
    std::mutex load_mutex_;
    std::optional<ChatLoadResult> pending_load_;
    bool load_ready_ = false;
    bool loading_ = false;
    std::uint64_t load_generation_ = 0;
};

}  // namespace ui
}  // namespace st
