#pragma once

#include "core/character_store.h"
#include "core/chat_store.h"
#include "core/paths.h"
#include "core/settings.h"
#include "ui/character_list.h"

#include <gtkmm.h>

#include <memory>

namespace st {
namespace ui {

class MainWindow : public Gtk::Window {
public:
    MainWindow(st::Paths paths,
               st::Settings& settings,
               st::CharacterStore& characters,
               st::ChatStore& chats);

private:
    void on_settings();
    void on_character_selected(const st::CharacterEntry& entry);

    st::Paths paths_;
    st::Settings& settings_;
    st::CharacterStore& characters_;
    st::ChatStore& chats_;

    Gtk::HeaderBar header_;
    Gtk::Button settings_button_;
    Gtk::Paned paned_;
    std::unique_ptr<CharacterList> character_list_;
    Gtk::ScrolledWindow right_scroll_;
    Gtk::Label right_placeholder_;
    Gtk::Label status_;
    Gtk::Box root_;
};

}  // namespace ui
}  // namespace st
