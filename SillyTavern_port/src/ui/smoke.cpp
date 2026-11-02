#include "ui/smoke.h"

#include "core/atomic_file.h"
#include "core/base64.h"
#include "core/json.h"
#include "ui/avatar.h"
#include "ui/main_window.h"

#include <glib.h>
#include <glibmm/main.h>
#include <gtkmm/main.h>

#include <cstdint>
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

std::uint32_t crc32_bytes(const std::string& type, const std::string& data) {
    std::uint32_t crc = 0xFFFFFFFFu;
    const auto feed = [&crc](unsigned char byte) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    };
    for (const char value : type) {
        feed(static_cast<unsigned char>(value));
    }
    for (const char value : data) {
        feed(static_cast<unsigned char>(value));
    }
    return crc ^ 0xFFFFFFFFu;
}

void append_u32(std::string& target, std::uint32_t value) {
    target += static_cast<char>((value >> 24) & 0xFFu);
    target += static_cast<char>((value >> 16) & 0xFFu);
    target += static_cast<char>((value >> 8) & 0xFFu);
    target += static_cast<char>(value & 0xFFu);
}

void append_chunk(std::string& png, const std::string& type, const std::string& data) {
    append_u32(png, static_cast<std::uint32_t>(data.size()));
    png += type;
    png += data;
    append_u32(png, crc32_bytes(type, data));
}

std::uint32_t adler32(const std::string& data) {
    std::uint32_t first = 1;
    std::uint32_t second = 0;
    for (const char value : data) {
        first = (first + static_cast<unsigned char>(value)) % 65521u;
        second = (second + first) % 65521u;
    }
    return (second << 16) | first;
}

std::string make_smoke_png() {
    st::Json card = st::Json::object();
    card["spec"] = "chara_card_v2";
    card["spec_version"] = "2.0";
    st::Json data = st::Json::object();
    data["name"] = "Smoke Test";
    data["description"] = "Synthetic character card for the GTK smoke test.";
    data["first_mes"] = "Hello there.";
    card["data"] = std::move(data);

    const std::string encoded = st::base64_encode(st::dump_json(card));

    std::string png("\x89PNG\r\n\x1a\n", 8);

    std::string header;
    append_u32(header, 1);
    append_u32(header, 1);
    header += '\x08';
    header += '\x06';
    header += '\x00';
    header += '\x00';
    header += '\x00';
    append_chunk(png, "IHDR", header);

    std::string text = "chara";
    text += '\0';
    text += encoded;
    append_chunk(png, "tEXt", text);

    std::string raw;
    raw += '\0';
    raw += '\xff';
    raw += '\x00';
    raw += '\x00';
    raw += '\xff';

    const std::uint16_t length = static_cast<std::uint16_t>(raw.size());
    std::string compressed;
    compressed += '\x78';
    compressed += '\x01';
    compressed += '\x01';
    compressed += static_cast<char>(length & 0xFFu);
    compressed += static_cast<char>((length >> 8) & 0xFFu);
    const std::uint16_t inverse = static_cast<std::uint16_t>(0xFFFFu - length);
    compressed += static_cast<char>(inverse & 0xFFu);
    compressed += static_cast<char>((inverse >> 8) & 0xFFu);
    compressed += raw;
    append_u32(compressed, adler32(raw));
    append_chunk(png, "IDAT", compressed);

    append_chunk(png, "IEND", std::string());
    return png;
}

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

        const std::filesystem::path source = root.path() / "smoke-card.png";
        st::write_file_atomic(source, make_smoke_png());
        const st::CharacterEntry entry = characters.import_file(source);

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

        const Glib::RefPtr<Gdk::Pixbuf> avatar =
            load_avatar(characters.avatar_path(entry), 48);
        if (!avatar) {
            std::cerr << "smoke: avatar not loaded\n";
            return 1;
        }
        std::cout << "smoke: character loaded\n";

        window.hide();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "smoke: " << error.what() << '\n';
        return 1;
    }
}

}  // namespace ui
}  // namespace st
