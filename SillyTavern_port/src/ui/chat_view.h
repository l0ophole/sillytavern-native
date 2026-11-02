#pragma once

#include "core/chat_session.h"
#include "core/character_store.h"
#include "core/chat_store.h"
#include "core/settings.h"

#include <gtkmm.h>
#include <glibmm/dispatcher.h>

#include <functional>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace st {
namespace ui {

class ChatView : public Gtk::Box {
public:
    ChatView(st::ChatStore& chats,
             st::Settings& settings,
             std::function<void(const std::string&)> on_status);
    ~ChatView() override;

    void set_character(const st::CharacterEntry& entry);
    void refresh_settings();
    void submit_message(const std::string& text);
    bool is_streaming() const { return streaming_; }

private:
    void on_send();
    void on_cancel();
    void on_new_chat();
    void on_chat_selected();
    void on_dispatcher();
    bool on_composer_key_press(GdkEventKey* event);
    bool on_transcript_button_press(GdkEventButton* event);

    void on_edit_message();
    void on_delete_message();
    void on_regenerate();
    void on_continue();

    void start_generation(st::OpenAiRequest request, bool continue_mode = false);
    void finish_generation(const st::OpenAiResult& result);
    void stop_streaming();

    void create_new_chat(bool announce);
    void load_chat(const std::string& filename);
    std::string new_chat_filename() const;
    void refresh_selector();
    void refresh_transcript();
    void append_streaming_display();
    void scroll_to_end();
    void set_status(const std::string& text);
    std::size_t message_index_at_offset(std::size_t offset) const;
    Gtk::Window* top_window();

    st::ChatStore& chats_;
    st::Settings& settings_;
    std::function<void(const std::string&)> on_status_;

    st::CharacterCard card_;
    std::string character_;
    std::string filename_;
    std::unique_ptr<st::ChatSession> session_;

    Gtk::Box top_bar_;
    Gtk::ComboBoxText chat_selector_;
    Gtk::Button new_button_;
    Gtk::ScrolledWindow transcript_scroll_;
    Gtk::TextView transcript_view_;
    Glib::RefPtr<Gtk::TextTag> header_tag_;
    Gtk::Menu context_menu_;
    Gtk::MenuItem edit_item_;
    Gtk::MenuItem delete_item_;
    Gtk::MenuItem regenerate_item_;
    Gtk::MenuItem continue_item_;
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
};

}  // namespace ui
}  // namespace st
