#include "ui/smoke.h"

#include "core/atomic_file.h"
#include "core/base64.h"
#include "core/chat_session.h"
#include "core/identity.h"
#include "core/json.h"
#include "ui/avatar.h"
#include "ui/main_window.h"

#include <glib.h>
#include <glibmm/main.h>
#include <gtkmm/main.h>

#include <cstdint>
#include <cstdlib>
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

        st::Chat smoke_chat = chats.create(entry.card.name,
                                           st::normalize_user_name(settings.user_name),
                                           entry.card.first_mes);
        st::ChatSession session(entry.card,
                                settings,
                                chats,
                                entry.card.name,
                                "smoke-roundtrip.jsonl",
                                smoke_chat);
        session.append_user("Hello from the smoke test.");
        if (session.prompt_messages().empty()) {
            std::cerr << "smoke: empty prompt\n";
            return 1;
        }
        session.save();
        const st::Chat loaded = chats.load(entry.card.name, "smoke-roundtrip.jsonl");
        if (loaded.messages.size() != std::size_t(2)) {
            std::cerr << "smoke: chat round-trip failed\n";
            return 1;
        }
        std::cout << "smoke: chat round-trip\n";

        st::Chat action_chat = chats.create(entry.card.name,
                                            st::normalize_user_name(settings.user_name),
                                            entry.card.first_mes);
        st::ChatSession actions(entry.card,
                                settings,
                                chats,
                                entry.card.name,
                                "smoke-actions.jsonl",
                                action_chat);
        actions.append_user("Original user message");
        actions.append_assistant("Original assistant reply");
        if (!actions.edit_message(1, "Edited user message")) {
            std::cerr << "smoke: edit failed\n";
            return 1;
        }
        if (!actions.delete_message(2)) {
            std::cerr << "smoke: delete failed\n";
            return 1;
        }
        actions.append_assistant("Stale assistant reply");
        if (!actions.regenerate()) {
            std::cerr << "smoke: regenerate failed\n";
            return 1;
        }
        actions.append_assistant("Fresh assistant reply");
        actions.save();

        const st::Chat reloaded = chats.load(entry.card.name, "smoke-actions.jsonl");
        if (reloaded.messages.size() != std::size_t(3) ||
            reloaded.messages[1].mes != "Edited user message" ||
            reloaded.messages[2].mes != "Fresh assistant reply") {
            std::cerr << "smoke: message actions round-trip failed\n";
            return 1;
        }
        std::cout << "smoke: message actions\n";

        if (const char* mock_base = std::getenv("ST_MOCK_BASE_URL")) {
            st::Settings mock_settings = settings;
            mock_settings.api_base_url = mock_base;
            mock_settings.model = "mock";
            mock_settings.stream = true;

            st::CharacterEntry mock_entry;
            mock_entry.id = "mock";
            mock_entry.card.name = "MockBot";
            mock_entry.card.system_prompt = "Test.";
            mock_entry.card.first_mes = "Greeting.";

            std::string ui_status;
            ChatView view(chats, mock_settings, [&ui_status](const std::string& text) {
                ui_status = text;
            });
            view.show_all();
            view.set_character(mock_entry);
            view.submit_message("Hi");

            const gint64 stream_deadline = g_get_monotonic_time() + 5 * 1000 * 1000;
            while (g_get_monotonic_time() < stream_deadline) {
                while (Glib::MainContext::get_default()->iteration(false)) {
                }
                if (!view.is_streaming() && ui_status != "Generating...") {
                    break;
                }
                g_usleep(5000);
            }

            if (view.is_streaming()) {
                std::cerr << "smoke: UI stream timed out\n";
                return 1;
            }
            if (ui_status != "Ready") {
                std::cerr << "smoke: UI stream status: " << ui_status << '\n';
                return 1;
            }

            const std::vector<std::string> mock_chats = chats.list("MockBot");
            if (mock_chats.empty()) {
                std::cerr << "smoke: UI stream did not save a chat\n";
                return 1;
            }
            const st::Chat streamed = chats.load("MockBot", mock_chats.back());
            if (streamed.messages.size() != std::size_t(3) ||
                streamed.messages.back().name != "MockBot" ||
                streamed.messages.back().mes != "hello world") {
                std::cerr << "smoke: UI stream reply mismatch\n";
                return 1;
            }
            std::cout << "smoke: ui streaming\n";
        }

        window.hide();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "smoke: " << error.what() << '\n';
        return 1;
    }
}

}  // namespace ui
}  // namespace st
