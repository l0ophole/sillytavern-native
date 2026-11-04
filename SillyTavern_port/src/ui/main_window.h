#pragma once

#include "core/character_store.h"
#include "core/chat_store.h"
#include "core/paths.h"
#include "core/settings.h"
#include "ui/character_list.h"
#include "ui/chat_view.h"

#include <gtkmm.h>

#include <memory>
#include <string>

namespace st {
namespace ui {

class MainWindow : public Gtk::Window {
public:
    MainWindow(st::Paths paths,
               st::Settings& settings,
               st::CharacterStore& characters,
               st::ChatStore& chats);

    std::string create_backup();
    bool restore_backup(const std::string& id);

private:
    void on_settings();
    void on_character_selected(const st::CharacterEntry& entry);
    void on_backup();
    void on_restore();

    st::Paths paths_;
    st::Settings& settings_;
    st::CharacterStore& characters_;
    st::ChatStore& chats_;

    Gtk::HeaderBar header_;
    Gtk::Button settings_button_;
    Gtk::MenuButton menu_button_;
    Gtk::Menu backup_menu_;
    Gtk::MenuItem backup_item_;
    Gtk::MenuItem restore_item_;
    Gtk::Paned paned_;
    std::unique_ptr<CharacterList> character_list_;
    std::unique_ptr<ChatView> chat_view_;
    Gtk::Label status_;
    Gtk::Box root_;
};

}  // namespace ui
}  // namespace st
