#include "ui/chat_view.h"

#include "core/identity.h"
#include "core/log.h"
#include "core/names.h"
#include "core/prompt.h"

#include <gdk/gdkkeysyms.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <exception>
#include <string>
#include <utility>

namespace st {
namespace ui {
namespace {

std::string trim_copy(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

}  // namespace

ChatView::ChatView(st::ChatStore& chats,
                   st::Settings& settings,
                   std::function<void(const std::string&)> on_status)
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6),
      chats_(chats),
      settings_(settings),
      on_status_(std::move(on_status)),
      composer_row_(Gtk::ORIENTATION_HORIZONTAL, 6),
      button_box_(Gtk::ORIENTATION_VERTICAL, 6) {
    set_margin_top(6);
    set_margin_bottom(6);
    set_margin_left(6);
    set_margin_right(6);

    top_bar_.set_spacing(6);
    chat_selector_.set_hexpand(true);
    chat_selector_.signal_changed().connect(
        sigc::mem_fun(*this, &ChatView::on_chat_selected));
    budget_label_.set_text("~0 / 0 tokens");
    new_button_.set_label("New");
    new_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &ChatView::on_new_chat));
    rename_button_.set_label("Rename");
    rename_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &ChatView::on_rename_chat));
    delete_button_.set_label("Delete");
    delete_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &ChatView::on_delete_chat));
    top_bar_.pack_start(chat_selector_, true, true, 0);
    top_bar_.pack_end(budget_label_, false, false, 0);
    top_bar_.pack_end(rename_button_, false, false, 0);
    top_bar_.pack_end(delete_button_, false, false, 0);
    top_bar_.pack_end(new_button_, false, false, 0);

    transcript_view_.set_editable(false);
    transcript_view_.set_cursor_visible(false);
    transcript_view_.set_monospace(true);
    transcript_view_.set_wrap_mode(Gtk::WRAP_WORD_CHAR);
    transcript_view_.set_margin_top(6);
    transcript_view_.set_margin_bottom(6);
    transcript_view_.set_margin_left(6);
    transcript_view_.set_margin_right(6);
    transcript_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    transcript_scroll_.add(transcript_view_);
    header_tag_ = transcript_view_.get_buffer()->create_tag("message-header");
    header_tag_->property_weight() = static_cast<int>(Pango::WEIGHT_BOLD);
    transcript_view_.signal_button_press_event().connect(
        sigc::mem_fun(*this, &ChatView::on_transcript_button_press), false);

    edit_item_.set_label("Edit");
    delete_item_.set_label("Delete");
    regenerate_item_.set_label("Regenerate");
    continue_item_.set_label("Continue");
    context_menu_.append(edit_item_);
    context_menu_.append(delete_item_);
    context_menu_.append(regenerate_item_);
    context_menu_.append(continue_item_);
    context_menu_.attach_to_widget(transcript_view_);
    edit_item_.signal_activate().connect(
        sigc::mem_fun(*this, &ChatView::on_edit_message));
    delete_item_.signal_activate().connect(
        sigc::mem_fun(*this, &ChatView::on_delete_message));
    regenerate_item_.signal_activate().connect(
        sigc::mem_fun(*this, &ChatView::on_regenerate));
    continue_item_.signal_activate().connect(
        sigc::mem_fun(*this, &ChatView::on_continue));
    context_menu_.show_all();

    composer_view_.set_wrap_mode(Gtk::WRAP_WORD_CHAR);
    composer_view_.set_margin_top(6);
    composer_view_.set_margin_bottom(6);
    composer_view_.set_margin_left(6);
    composer_view_.set_margin_right(6);
    composer_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    composer_scroll_.set_size_request(-1, 84);
    composer_scroll_.add(composer_view_);
    composer_view_.signal_key_press_event().connect(
        sigc::mem_fun(*this, &ChatView::on_composer_key_press), false);

    send_button_.set_label("Send");
    send_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &ChatView::on_send));
    cancel_button_.set_label("Cancel");
    cancel_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &ChatView::on_cancel));
    button_box_.pack_start(send_button_, false, false, 0);
    button_box_.pack_start(cancel_button_, false, false, 0);

    composer_row_.pack_start(composer_scroll_, true, true, 0);
    composer_row_.pack_end(button_box_, false, false, 0);

    pack_start(top_bar_, false, false, 0);
    pack_start(transcript_scroll_, true, true, 0);
    pack_start(composer_row_, false, false, 0);

    dispatcher_.connect(sigc::mem_fun(*this, &ChatView::on_dispatcher));

    update_controls();

    show_all_children();
}

ChatView::~ChatView() {
    stop_streaming();
}

bool ChatView::set_character(const st::CharacterEntry& entry) {
    stop_streaming();
    card_ = entry.card;
    character_key_ = entry.id;
    character_name_ = entry.card.name.empty() ? entry.id : entry.card.name;
    if (card_.name.empty()) {
        card_.name = character_name_;
    }
    session_.reset();
    filename_.clear();

    const std::vector<std::string> files = chats_.list(character_key_);
    if (files.empty()) {
        create_new_chat(true);
    } else {
        load_chat(files.back());
    }
    update_controls();
    return session_ != nullptr;
}

void ChatView::clear() {
    stop_streaming();
    session_.reset();
    filename_.clear();
    character_key_.clear();
    character_name_.clear();
    card_ = st::CharacterCard{};
    refresh_selector();
    refresh_transcript();
    update_controls();
    set_status("No characters");
}

void ChatView::refresh_settings() {
    if (!session_) {
        return;
    }
    session_->settings() = settings_;
    refresh_transcript();
}

void ChatView::on_send() {
    if (!session_ || streaming_) {
        return;
    }

    Glib::RefPtr<Gtk::TextBuffer> buffer = composer_view_.get_buffer();
    const std::string text = buffer->get_text().raw();
    if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
        return;
    }
    if (submit_message(text)) {
        buffer->set_text("");
    }
}

bool ChatView::submit_message(const std::string& text) {
    if (!session_ || streaming_) {
        return false;
    }
    if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
        return false;
    }

    const st::Chat before = session_->snapshot();
    try {
        session_->append_user(text);
        session_->save();
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error, std::string("chat save failed: ") + error.what());
        session_->restore(before);
        refresh_transcript();
        set_status(std::string("Save error: ") + error.what());
        return false;
    }

    refresh_transcript();
    start_generation(session_->request());
    return true;
}

void ChatView::on_cancel() {
    if (!streaming_) {
        return;
    }
    set_status("Cancelling...");
    if (client_) {
        client_->cancel();
    }
}

void ChatView::on_new_chat() {
    if (streaming_) {
        return;
    }
    create_new_chat(true);
}

void ChatView::on_chat_selected() {
    if (updating_selector_ || streaming_) {
        return;
    }
    const std::string selected = chat_selector_.get_active_text().raw();
    if (selected.empty() || selected == filename_) {
        return;
    }
    load_chat(selected);
}

void ChatView::on_rename_chat() {
    if (!session_ || streaming_ || filename_.empty()) {
        return;
    }

    Gtk::Dialog dialog("Rename chat", true);
    if (Gtk::Window* window = top_window()) {
        dialog.set_transient_for(*window);
    }
    dialog.set_default_size(420, 120);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Save", Gtk::RESPONSE_OK);

    auto* entry = Gtk::manage(new Gtk::Entry());
    std::string initial = filename_;
    if (initial.size() > 6 && initial.substr(initial.size() - 6) == ".jsonl") {
        initial.resize(initial.size() - 6);
    }
    entry->set_text(initial);
    entry->set_activates_default(true);
    dialog.set_default_response(Gtk::RESPONSE_OK);
    dialog.get_content_area()->pack_start(*entry, true, true, 6);
    dialog.show_all();

    if (dialog.run() != Gtk::RESPONSE_OK) {
        return;
    }
    rename_current_chat(entry->get_text().raw());
}

bool ChatView::rename_current_chat(const std::string& name) {
    if (!session_ || streaming_ || filename_.empty() || character_key_.empty()) {
        return false;
    }

    std::string target = trim_copy(name);
    if (target.empty()) {
        set_status("Rename error: chat name cannot be empty");
        return false;
    }
    if (target.size() < 6 || target.substr(target.size() - 6) != ".jsonl") {
        target += ".jsonl";
    }
    if (target == filename_) {
        return true;
    }

    try {
        chats_.rename(character_key_, filename_, target);
        filename_ = target;
        session_->set_filename(filename_);
        refresh_selector();
        update_controls();
        set_status("Renamed to " + filename_);
        return true;
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error, std::string("chat rename failed: ") + error.what());
        set_status(std::string("Rename error: ") + error.what());
        return false;
    }
}

void ChatView::on_delete_chat() {
    if (!session_ || streaming_ || filename_.empty()) {
        return;
    }

    Gtk::MessageDialog dialog("Delete this chat?",
                              false,
                              Gtk::MESSAGE_QUESTION,
                              Gtk::BUTTONS_OK_CANCEL,
                              true);
    if (Gtk::Window* window = top_window()) {
        dialog.set_transient_for(*window);
    }
    dialog.set_secondary_text("Delete " + filename_ + "? This cannot be undone.");
    if (dialog.run() != Gtk::RESPONSE_OK) {
        return;
    }
    delete_current_chat();
}

bool ChatView::delete_current_chat() {
    if (!session_ || streaming_ || filename_.empty() || character_key_.empty()) {
        return false;
    }

    const std::string removed = filename_;
    try {
        chats_.remove(character_key_, removed);
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error, std::string("chat delete failed: ") + error.what());
        set_status(std::string("Delete error: ") + error.what());
        return false;
    }

    session_.reset();
    filename_.clear();
    const std::vector<std::string> remaining = chats_.list(character_key_);
    if (remaining.empty()) {
        create_new_chat(false);
    } else {
        load_chat(remaining.back());
    }
    update_controls();
    set_status("Deleted " + removed);
    return session_ != nullptr;
}

void ChatView::on_dispatcher() {
    std::string chunks;
    st::OpenAiResult result;
    bool finished = false;
    {
        std::lock_guard<std::mutex> lock(chunk_mutex_);
        chunks.swap(pending_chunks_);
        if (final_ready_) {
            finished = true;
            result = final_result_;
            final_ready_ = false;
        }
    }

    if (!chunks.empty() && streaming_) {
        streaming_text_ += chunks;
        refresh_transcript();
        append_streaming_display();
    }
    if (finished) {
        finish_generation(result);
    }
}

bool ChatView::on_composer_key_press(GdkEventKey* event) {
    if (event == nullptr) {
        return false;
    }
    const bool enter = event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter;
    if (enter && (event->state & GDK_SHIFT_MASK) == 0) {
        on_send();
        return true;
    }
    return false;
}

bool ChatView::on_transcript_button_press(GdkEventButton* event) {
    if (event == nullptr || event->button != 3 || streaming_ || !session_) {
        return false;
    }

    int buffer_x = 0;
    int buffer_y = 0;
    transcript_view_.window_to_buffer_coords(Gtk::TEXT_WINDOW_TEXT,
                                             static_cast<int>(event->x),
                                             static_cast<int>(event->y),
                                             buffer_x,
                                             buffer_y);
    Gtk::TextIter iter;
    transcript_view_.get_iter_at_location(iter, buffer_x, buffer_y);
    context_index_ = message_index_at_offset(static_cast<std::size_t>(iter.get_offset()));
    if (context_index_ >= message_ranges_.size()) {
        return false;
    }

    regenerate_item_.set_sensitive(session_->can_regenerate_at(context_index_));
    continue_item_.set_sensitive(session_->can_continue_at(context_index_));
    context_menu_.popup_at_pointer(reinterpret_cast<const GdkEvent*>(event));
    return true;
}

void ChatView::on_edit_message() {
    if (!session_ || streaming_ || context_index_ >= session_->chat().messages.size()) {
        return;
    }

    Gtk::Dialog dialog("Edit message", true);
    if (Gtk::Window* window = top_window()) {
        dialog.set_transient_for(*window);
    }
    dialog.set_default_size(520, 320);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Save", Gtk::RESPONSE_OK);

    auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
    scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    auto* view = Gtk::manage(new Gtk::TextView());
    view->set_wrap_mode(Gtk::WRAP_WORD_CHAR);
    view->get_buffer()->set_text(session_->chat().messages[context_index_].mes);
    scroll->add(*view);
    dialog.get_content_area()->pack_start(*scroll, true, true, 0);
    dialog.show_all();

    if (dialog.run() != Gtk::RESPONSE_OK) {
        return;
    }

    const std::string text = view->get_buffer()->get_text().raw();
    const st::Chat before = session_->snapshot();
    try {
        if (!session_->edit_message(context_index_, text)) {
            set_status("Edit failed: message not found");
            return;
        }
        session_->save();
        refresh_transcript();
        set_status("Message edited");
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error, std::string("message edit save failed: ") + error.what());
        session_->restore(before);
        refresh_transcript();
        set_status(std::string("Save error: ") + error.what());
    }
}

void ChatView::on_delete_message() {
    if (!session_ || streaming_ || context_index_ >= session_->chat().messages.size()) {
        return;
    }

    Gtk::MessageDialog dialog("Delete this message?",
                              false,
                              Gtk::MESSAGE_QUESTION,
                              Gtk::BUTTONS_OK_CANCEL,
                              true);
    if (Gtk::Window* window = top_window()) {
        dialog.set_transient_for(*window);
    }
    dialog.set_secondary_text("This cannot be undone.");
    if (dialog.run() != Gtk::RESPONSE_OK) {
        return;
    }

    const st::Chat before = session_->snapshot();
    try {
        if (!session_->delete_message(context_index_)) {
            set_status("Delete failed: message not found");
            return;
        }
        session_->save();
        refresh_transcript();
        set_status("Message deleted");
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error,
                      std::string("message delete save failed: ") + error.what());
        session_->restore(before);
        refresh_transcript();
        set_status(std::string("Save error: ") + error.what());
    }
}

void ChatView::on_regenerate() {
    if (!session_ || streaming_) {
        return;
    }

    const st::Chat before = session_->snapshot();
    if (!session_->regenerate()) {
        set_status("Nothing to regenerate");
        return;
    }
    regenerate_backup_ = before;
    refresh_transcript();
    if (!start_generation(session_->request())) {
        session_->restore(*regenerate_backup_);
        regenerate_backup_.reset();
        refresh_transcript();
    }
}

void ChatView::on_continue() {
    if (!session_ || streaming_) {
        return;
    }
    if (!session_->can_continue()) {
        set_status("Nothing to continue");
        return;
    }

    st::OpenAiRequest request = session_->request();
    request.messages.push_back(
        st::PromptMessage{"system", "Continue the last assistant message without repeating it."});
    start_generation(std::move(request), true);
}

bool ChatView::start_generation(st::OpenAiRequest request, bool continue_mode) {
    streaming_ = true;
    continue_mode_ = continue_mode;
    streaming_text_.clear();
    update_controls();
    set_status("Generating...");

    client_ = std::make_shared<st::OpenAiClient>();
    client_->reset_cancel();
    const std::shared_ptr<st::OpenAiClient> client = client_;

    try {
        worker_ = std::thread([this, client, request]() {
            st::OpenAiResult result = client->generate(
                request, [this](const std::string& chunk) {
                    {
                        std::lock_guard<std::mutex> lock(chunk_mutex_);
                        pending_chunks_ += chunk;
                    }
                    dispatcher_.emit();
                });
            {
                std::lock_guard<std::mutex> lock(chunk_mutex_);
                final_result_ = std::move(result);
                final_ready_ = true;
            }
            dispatcher_.emit();
        });
        return true;
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error,
                      std::string("generation start failed: ") + error.what());
        streaming_ = false;
        continue_mode_ = false;
        client_.reset();
        update_controls();
        set_status(std::string("Cannot start generation: ") + error.what());
        return false;
    }
}

void ChatView::finish_generation(const st::OpenAiResult& result) {
    if (worker_.joinable()) {
        worker_.join();
    }

    const std::string text = !result.text.empty() ? result.text : streaming_text_;
    std::string save_error;
    if (session_) {
        if (!text.empty()) {
            const st::Chat before = session_->snapshot();
            try {
                if (continue_mode_) {
                    session_->append_to_last_assistant(text);
                } else {
                    session_->append_assistant(text);
                }
                session_->save();
            } catch (const std::exception& error) {
                st::log_write(st::LogLevel::Error,
                              std::string("assistant save failed: ") + error.what());
                session_->restore(regenerate_backup_.has_value() ? *regenerate_backup_ : before);
                save_error = std::string("Save error: ") + error.what();
            }
        } else if (regenerate_backup_.has_value()) {
            session_->restore(*regenerate_backup_);
        }
    }

    regenerate_backup_.reset();
    streaming_ = false;
    continue_mode_ = false;
    streaming_text_.clear();
    client_.reset();
    refresh_transcript();
    update_controls();

    if (!save_error.empty()) {
        set_status(save_error);
    } else if (result.cancelled) {
        set_status("Cancelled");
    } else if (!result.error.empty()) {
        st::log_write(st::LogLevel::Error, std::string("generation failed: ") + result.error);
        set_status(std::string("Error: ") + result.error);
    } else {
        set_status("Ready");
    }
}

std::string ChatView::collect_streaming_text() {
    std::lock_guard<std::mutex> lock(chunk_mutex_);
    if (!pending_chunks_.empty()) {
        streaming_text_ += pending_chunks_;
        pending_chunks_.clear();
    }
    std::string collected = streaming_text_;
    if (final_ready_ && !final_result_.text.empty()) {
        collected = final_result_.text;
    }
    final_result_ = st::OpenAiResult{};
    final_ready_ = false;
    return collected;
}

void ChatView::stop_streaming() {
    if (!streaming_) {
        return;
    }
    if (client_) {
        client_->cancel();
    }
    if (worker_.joinable()) {
        worker_.join();
    }

    const std::string partial = collect_streaming_text();
    if (session_) {
        if (!partial.empty()) {
            const st::Chat before = session_->snapshot();
            try {
                if (continue_mode_) {
                    session_->append_to_last_assistant(partial);
                } else {
                    session_->append_assistant(partial);
                }
                session_->save();
            } catch (const std::exception& error) {
                st::log_write(st::LogLevel::Error,
                              std::string("partial save failed: ") + error.what());
                session_->restore(regenerate_backup_.has_value() ? *regenerate_backup_ : before);
                set_status(std::string("Save error: ") + error.what());
            }
        } else if (regenerate_backup_.has_value()) {
            session_->restore(*regenerate_backup_);
        }
    }

    regenerate_backup_.reset();
    streaming_ = false;
    continue_mode_ = false;
    streaming_text_.clear();
    client_.reset();
    update_controls();
}

bool ChatView::create_new_chat(bool announce) {
    stop_streaming();
    if (character_key_.empty()) {
        return false;
    }

    try {
        st::Chat chat = chats_.create(character_key_,
                                      normalize_user_name(settings_.user_name),
                                      card_.first_mes,
                                      character_name_);
        filename_ = new_chat_filename();
        chats_.save(character_key_, filename_, chat);
        session_ = std::make_unique<st::ChatSession>(
            card_, settings_, chats_, character_key_, filename_, std::move(chat));
        refresh_selector();
        refresh_transcript();
        update_controls();
        if (announce) {
            set_status("New chat");
        }
        return true;
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error, std::string("chat create failed: ") + error.what());
        session_.reset();
        filename_.clear();
        refresh_selector();
        refresh_transcript();
        update_controls();
        set_status(std::string("Chat error: ") + error.what());
        return false;
    }
}

bool ChatView::load_chat(const std::string& filename) {
    stop_streaming();
    try {
        st::Chat chat = chats_.load(character_key_, filename);
        filename_ = filename;
        session_ = std::make_unique<st::ChatSession>(
            card_, settings_, chats_, character_key_, filename_, std::move(chat));
        refresh_selector();
        refresh_transcript();
        update_controls();
        set_status("Loaded " + filename);
        return true;
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error, std::string("chat load failed: ") + error.what());
        session_.reset();
        filename_.clear();
        refresh_selector();
        refresh_transcript();
        update_controls();
        set_status(std::string("Chat error: ") + error.what());
        return false;
    }
}

std::string ChatView::new_chat_filename() const {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc {};
    gmtime_r(&seconds, &utc);
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H-%M-%S", &utc);

    const std::string base(buffer);
    std::string candidate = base + ".jsonl";
    const std::vector<std::string> existing = chats_.list(character_key_);
    int suffix = 2;
    while (std::find(existing.begin(), existing.end(), candidate) != existing.end()) {
        candidate = base + "-" + std::to_string(suffix) + ".jsonl";
        ++suffix;
    }
    return candidate;
}

void ChatView::refresh_selector() {
    updating_selector_ = true;
    chat_selector_.remove_all();
    if (!character_key_.empty()) {
        for (const std::string& file : chats_.list(character_key_)) {
            chat_selector_.append(file);
        }
    }
    if (!filename_.empty()) {
        chat_selector_.set_active_text(filename_);
    }
    updating_selector_ = false;
}

void ChatView::refresh_transcript() {
    Glib::RefPtr<Gtk::TextBuffer> buffer = transcript_view_.get_buffer();
    buffer->set_text("");
    message_ranges_.clear();
    if (!session_) {
        update_budget_label();
        scroll_to_end();
        return;
    }

    const std::string user = normalize_user_name(settings_.user_name);
    for (const st::ChatMessage& message : session_->chat().messages) {
        const char* label = message.is_system ? "System:" : (message.is_user ? "User:" : "Character:");
        const std::size_t begin = static_cast<std::size_t>(buffer->get_char_count());
        buffer->insert_with_tag(buffer->end(), Glib::ustring(label) + "\n", header_tag_);
        buffer->insert(buffer->end(),
                       Glib::ustring(expand_macros(message.mes, character_name_, user)) + "\n\n");
        const std::size_t end = static_cast<std::size_t>(buffer->get_char_count());
        message_ranges_.push_back(MessageRange{begin, end});
    }
    update_budget_label();
    scroll_to_end();
}

void ChatView::update_budget_label() {
    if (!session_) {
        budget_label_.set_text("~0 / " + std::to_string(settings_.context_size) + " tokens");
        return;
    }
    (void)session_->prompt_messages();
    std::string label = "~" + std::to_string(session_->estimated_tokens()) + " / " +
                        std::to_string(settings_.context_size) + " tokens";
    if (session_->trimmed()) {
        label += " (trimmed)";
    }
    budget_label_.set_text(label);
}

void ChatView::update_controls() {
    const bool has_character = !character_key_.empty();
    const bool has_session = session_ != nullptr;
    send_button_.set_sensitive(has_session && !streaming_);
    cancel_button_.set_sensitive(streaming_);
    chat_selector_.set_sensitive(has_character && !streaming_);
    new_button_.set_sensitive(has_character && !streaming_);
    rename_button_.set_sensitive(has_session && !streaming_ && !filename_.empty());
    delete_button_.set_sensitive(has_session && !streaming_ && !filename_.empty());
}

void ChatView::append_streaming_display() {
    if (streaming_text_.empty() || !session_) {
        return;
    }
    Glib::RefPtr<Gtk::TextBuffer> buffer = transcript_view_.get_buffer();
    const std::string user = normalize_user_name(settings_.user_name);
    buffer->insert_with_tag(buffer->end(), "Character:\n", header_tag_);
    buffer->insert(buffer->end(),
                   Glib::ustring(expand_macros(streaming_text_, character_name_, user)) + "\n\n");
    scroll_to_end();
}

void ChatView::scroll_to_end() {
    Gtk::TextIter end = transcript_view_.get_buffer()->end();
    transcript_view_.scroll_to(end, 0.0);
}

void ChatView::set_status(const std::string& text) {
    if (on_status_) {
        on_status_(st::redact_secrets(text, {settings_.api_key}));
    }
}

std::size_t ChatView::message_index_at_offset(std::size_t offset) const {
    for (std::size_t index = 0; index < message_ranges_.size(); ++index) {
        if (offset >= message_ranges_[index].begin && offset < message_ranges_[index].end) {
            return index;
        }
    }
    return message_ranges_.size();
}

Gtk::Window* ChatView::top_window() {
    return dynamic_cast<Gtk::Window*>(get_toplevel());
}

}  // namespace ui
}  // namespace st
