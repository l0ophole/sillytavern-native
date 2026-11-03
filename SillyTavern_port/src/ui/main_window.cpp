#include "ui/main_window.h"

#include "core/log.h"
#include "ui/settings_dialog.h"

#include <string>

namespace st {
namespace ui {

MainWindow::MainWindow(st::Paths paths,
                       st::Settings& settings,
                       st::CharacterStore& characters,
                       st::ChatStore& chats)
    : paths_(std::move(paths)),
      settings_(settings),
      characters_(characters),
      chats_(chats) {
    set_title("SillyTavern Port");
    set_default_size(1100, 720);

    header_.set_title("SillyTavern Port");
    header_.set_show_close_button(true);
    settings_button_.set_label("Settings");
    header_.pack_end(settings_button_);
    set_titlebar(header_);

    chat_view_ = std::make_unique<ChatView>(
        chats_, settings_, [this](const std::string& text) {
            status_.set_text(st::redact_secrets(text, {settings_.api_key}));
        });
    character_list_ = std::make_unique<CharacterList>(
        *this,
        characters_,
        [this](const CharacterEntry& entry) {
            on_character_selected(entry);
        },
        [this](const CharacterEntry* entry) {
            if (entry == nullptr) {
                chat_view_->clear();
                status_.set_text("No characters");
            }
        });

    paned_.set_position(320);
    paned_.pack1(*character_list_, true, false);
    paned_.pack2(*chat_view_, true, false);

    status_.set_text("Ready");
    status_.set_halign(Gtk::ALIGN_START);
    status_.set_margin_top(4);
    status_.set_margin_bottom(4);
    status_.set_margin_left(8);
    status_.set_margin_right(8);

    root_.set_orientation(Gtk::ORIENTATION_VERTICAL);
    root_.pack_start(paned_, Gtk::PACK_EXPAND_WIDGET);
    root_.pack_end(status_, Gtk::PACK_SHRINK);

    add(root_);

    settings_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &MainWindow::on_settings));

    chat_view_->clear();
    status_.set_text("Loading characters...");
    character_list_->refresh_async();

    show_all_children();
}

void MainWindow::on_character_selected(const st::CharacterEntry& entry) {
    if (chat_view_->set_character_async(entry)) {
        status_.set_text("Selected: " + entry.card.name);
    }
}

void MainWindow::on_settings() {
    try {
        SettingsDialog dialog(*this, paths_, settings_, [this]() {
            st::log_set_redactions({settings_.api_key});
            chat_view_->refresh_settings();
            status_.set_text("Settings saved");
        });
        dialog.run();
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error, std::string("settings dialog failed: ") + error.what());
        status_.set_text(
            st::redact_secrets(std::string("Settings error: ") + error.what(), {settings_.api_key}));
    }
}

}  // namespace ui
}  // namespace st
