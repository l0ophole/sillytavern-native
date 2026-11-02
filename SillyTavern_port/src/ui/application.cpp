#include "ui/application.h"

#include "core/log.h"
#include "ui/main_window.h"

namespace st {
namespace ui {

Application::Application() : Gtk::Application("org.sillytavern.port") {}

Glib::RefPtr<Application> Application::create() {
    return Glib::RefPtr<Application>(new Application());
}

int Application::run(int& argc, char**& argv) {
    return Gtk::Application::run(argc, argv);
}

void Application::on_activate() {
    if (!window_) {
        paths_ = st::Paths::resolve();
        paths_.ensure();
        settings_ = st::Settings::load(paths_.settings_file);
        st::log_init(paths_.logs_dir / "app.log", false);
        st::log_set_redactions({settings_.api_key});
        characters_ = std::make_unique<st::CharacterStore>(paths_.characters_dir);
        chats_ = std::make_unique<st::ChatStore>(paths_.chats_dir);
        window_ = std::make_unique<MainWindow>(paths_, settings_, *characters_, *chats_);
        add_window(*window_);
    }
    window_->show_all();
    window_->present();
}

}  // namespace ui
}  // namespace st
