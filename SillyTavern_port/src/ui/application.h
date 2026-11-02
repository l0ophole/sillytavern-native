#pragma once

#include "core/character_store.h"
#include "core/chat_store.h"
#include "core/paths.h"
#include "core/settings.h"

#include <gtkmm.h>

#include <memory>

namespace st {
namespace ui {

class MainWindow;

class Application : public Gtk::Application {
public:
    static Glib::RefPtr<Application> create();
    int run(int& argc, char**& argv);

protected:
    Application();
    void on_activate() override;

private:
    st::Paths paths_;
    st::Settings settings_;
    std::unique_ptr<st::CharacterStore> characters_;
    std::unique_ptr<st::ChatStore> chats_;
    std::unique_ptr<MainWindow> window_;
    int startup_exit_code_ = 0;
};

}  // namespace ui
}  // namespace st
