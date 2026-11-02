#include "ui/smoke.h"

#include "ui/main_window.h"

#include <glib.h>
#include <glibmm/main.h>
#include <gtkmm/main.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <unistd.h>

namespace st {
namespace ui {
namespace {

class TempRoot {
public:
    TempRoot() {
        const std::filesystem::path base = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 1000; ++attempt) {
            const std::filesystem::path candidate =
                base / ("st-smoke-" + std::to_string(::getpid()) + "-" +
                        std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                path_ = candidate;
                return;
            }
        }
        throw std::runtime_error("cannot create smoke-test directory");
    }

    ~TempRoot() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace

int run_smoke_test() {
    Gtk::Main kit(false);

    try {
        TempRoot root;
        st::Paths paths = st::Paths::resolve(root.path() / "config", root.path() / "data");
        paths.ensure();

        st::Settings settings;
        settings.validate();
        st::CharacterStore characters(paths.characters_dir);
        st::ChatStore chats(paths.chats_dir);

        MainWindow window(paths, settings, characters, chats);
        window.show_all();

        const gint64 deadline = g_get_monotonic_time() + 300 * 1000;
        while (g_get_monotonic_time() < deadline) {
            while (Glib::MainContext::get_default()->iteration(false)) {
            }
            g_usleep(5000);
        }

        if (!window.get_realized()) {
            std::cerr << "smoke: window not realized\n";
            return 1;
        }
        std::cout << "smoke: window realized\n";
        window.hide();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "smoke: " << error.what() << '\n';
        return 1;
    }
}

}  // namespace ui
}  // namespace st
