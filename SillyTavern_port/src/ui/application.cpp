#include "ui/application.h"

#include "core/log.h"
#include "ui/main_window.h"

#include <exception>
#include <stdexcept>
#include <string>

namespace st {
namespace ui {

Application::Application() : Gtk::Application("org.sillytavern.port") {}

Glib::RefPtr<Application> Application::create() {
    return Glib::RefPtr<Application>(new Application());
}

int Application::run(int& argc, char**& argv) {
    const int code = Gtk::Application::run(argc, argv);
    st::log_shutdown();
    return startup_exit_code_ != 0 ? startup_exit_code_ : code;
}

void Application::on_activate() {
    try {
        if (!window_) {
            paths_ = st::Paths::resolve();
            paths_.ensure();
            instance_lock_ = st::FileLock::try_acquire(paths_.instance_lock_file);
            if (!instance_lock_.has_value()) {
                std::string message = "another instance is already running";
                if (const auto holder = st::FileLock::holder_pid(paths_.instance_lock_file);
                    holder.has_value()) {
                    message += " (pid " + std::to_string(*holder) + ")";
                }
                throw std::runtime_error(message);
            }
            st::log_init(paths_.logs_dir / "app.log", false);
            settings_ = st::Settings::load(paths_.settings_file);
            st::log_set_redactions({settings_.api_key});
            characters_ = std::make_unique<st::CharacterStore>(paths_.characters_dir);
            chats_ = std::make_unique<st::ChatStore>(paths_.chats_dir);
            window_ = std::make_unique<MainWindow>(paths_, settings_, *characters_, *chats_);
            add_window(*window_);
        }
        window_->show_all();
        window_->present();
    } catch (const std::exception& error) {
        startup_exit_code_ = 1;
        st::log_write(st::LogLevel::Error, std::string("startup failed: ") + error.what());
        Gtk::MessageDialog dialog(
            "Startup failed", false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        dialog.set_secondary_text(
            st::redact_secrets(error.what(), {settings_.api_key}));
        dialog.run();
        quit();
    }
}

}  // namespace ui
}  // namespace st
