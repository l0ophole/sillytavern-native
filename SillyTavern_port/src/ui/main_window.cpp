#include "ui/main_window.h"

#include "core/backup.h"
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
    backup_item_.set_label("Back up chats");
    restore_item_.set_label("Restore backup...");
    backup_menu_.append(backup_item_);
    backup_menu_.append(restore_item_);
    backup_menu_.show_all();
    menu_button_.set_label("Backup");
    menu_button_.set_popup(backup_menu_);
    header_.pack_end(menu_button_);
    set_titlebar(header_);

    chat_view_ = std::make_unique<ChatView>(
        chats_, settings_, [this](const std::string& text) {
            status_.set_text(st::redact_secrets(text, {settings_.api_key}));
            refresh_backup_menu();
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
                if (character_list_->scan_failed()) {
                    status_.set_text(st::redact_secrets(
                        "Character scan failed: " + character_list_->scan_error(),
                        {settings_.api_key}));
                } else {
                    status_.set_text("No characters");
                }
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
    backup_item_.signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_backup));
    restore_item_.signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_restore));

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

void MainWindow::refresh_backup_menu() {
    const bool enabled = !chat_view_->is_streaming();
    backup_item_.set_sensitive(enabled);
    restore_item_.set_sensitive(enabled);
}

std::string MainWindow::create_backup() {
    if (chat_view_->is_streaming()) {
        status_.set_text("Backup error: generation in progress");
        return {};
    }
    try {
        st::BackupManager manager(paths_.chats_dir);
        const std::string id = manager.create();
        status_.set_text(st::redact_secrets("Backup created: " + id, {settings_.api_key}));
        return id;
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error, std::string("backup failed: ") + error.what());
        status_.set_text(
            st::redact_secrets(std::string("Backup error: ") + error.what(), {settings_.api_key}));
        return {};
    }
}

bool MainWindow::backup_actions_enabled() const {
    return backup_item_.get_sensitive() && restore_item_.get_sensitive();
}

bool MainWindow::restore_backup(const std::string& id) {
    if (chat_view_->is_streaming()) {
        status_.set_text("Backup error: generation in progress");
        return false;
    }
    try {
        st::BackupManager manager(paths_.chats_dir);
        manager.restore(id);
        character_list_->refresh();
        chat_view_->clear();
        status_.set_text(st::redact_secrets("Backup restored: " + id, {settings_.api_key}));
        return true;
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error, std::string("backup restore failed: ") + error.what());
        status_.set_text(
            st::redact_secrets(std::string("Backup error: ") + error.what(), {settings_.api_key}));
        return false;
    }
}

void MainWindow::on_backup() {
    (void)create_backup();
}

void MainWindow::on_restore() {
    if (chat_view_->is_streaming()) {
        status_.set_text("Backup error: generation in progress");
        return;
    }
    std::vector<st::BackupEntry> entries;
    try {
        st::BackupManager manager(paths_.chats_dir);
        entries = manager.list();
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error,
                      std::string("backup list failed: ") + error.what());
        status_.set_text(
            st::redact_secrets(std::string("Backup error: ") + error.what(), {settings_.api_key}));
        return;
    }
    if (entries.empty()) {
        status_.set_text("Backup error: no backups");
        return;
    }

    Gtk::Dialog dialog("Restore backup", true);
    dialog.set_transient_for(*this);
    dialog.set_default_size(480, 320);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Restore", Gtk::RESPONSE_OK);
    dialog.set_default_response(Gtk::RESPONSE_OK);

    auto* list = Gtk::manage(new Gtk::ListBox());
    list->set_selection_mode(Gtk::SELECTION_SINGLE);
    for (const auto& entry : entries) {
        auto* row = Gtk::manage(new Gtk::ListBoxRow());
        auto* label = Gtk::manage(new Gtk::Label(
            entry.id + " (" + entry.created + ", " + std::to_string(entry.chats) + " chats)"));
        row->add(*label);
        list->append(*row);
    }
    list->select_row(*list->get_row_at_index(0));
    auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
    scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scroll->add(*list);
    dialog.get_content_area()->pack_start(*scroll, true, true, 6);
    dialog.show_all();

    if (dialog.run() != Gtk::RESPONSE_OK) {
        return;
    }
    Gtk::ListBoxRow* selected = list->get_selected_row();
    if (selected == nullptr) {
        return;
    }
    const int index = selected->get_index();
    if (index < 0 ||
        static_cast<std::size_t>(index) >= entries.size()) {
        return;
    }
    const std::string id = entries[static_cast<std::size_t>(index)].id;

    Gtk::MessageDialog confirm("Restore backup " + id + "?",
                               false,
                               Gtk::MESSAGE_QUESTION,
                               Gtk::BUTTONS_OK_CANCEL,
                               true);
    confirm.set_transient_for(*this);
    confirm.set_secondary_text(
        "Chats from backup " + id + " will replace the current chat files.");
    if (confirm.run() != Gtk::RESPONSE_OK) {
        return;
    }
    (void)restore_backup(id);
}

}  // namespace ui
}  // namespace st
