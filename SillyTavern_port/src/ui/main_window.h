#pragma once

#include "core/character_store.h"
#include "core/chat_store.h"
#include "core/paths.h"
#include "core/settings.h"

#include <gtkmm.h>

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

    st::Paths paths_;
    st::Settings& settings_;
    st::CharacterStore& characters_;
    st::ChatStore& chats_;

    Gtk::HeaderBar header_;
    Gtk::Button settings_button_;
    Gtk::Paned paned_;
    Gtk::ScrolledWindow left_scroll_;
    Gtk::Label left_placeholder_;
    Gtk::ScrolledWindow right_scroll_;
    Gtk::Label right_placeholder_;
    Gtk::Label status_;
    Gtk::Box root_;
};

}  // namespace ui
}  // namespace st
