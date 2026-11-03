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

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
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

std::string make_smoke_png(const std::string& name = "Smoke Test") {
    st::Json card = st::Json::object();
    card["spec"] = "chara_card_v2";
    card["spec_version"] = "2.0";
    st::Json data = st::Json::object();
    data["name"] = name;
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

        {
            CharacterList async_list(
                window,
                characters,
                [](const st::CharacterEntry&) {},
                [](const st::CharacterEntry*) {});
            async_list.refresh_async();

            const gint64 scan_deadline = g_get_monotonic_time() + 5 * 1000 * 1000;
            while (async_list.is_scanning() && g_get_monotonic_time() < scan_deadline) {
                while (Glib::MainContext::get_default()->iteration(false)) {
                }
                g_usleep(1000);
            }
            if (async_list.is_scanning() || async_list.visible_entries().empty()) {
                std::cerr << "smoke: async library scan failed\n";
                return 1;
            }
            std::cout << "smoke: async library scan\n";
        }

        {
            const std::filesystem::path not_a_dir = root.path() / "not-a-directory";
            st::write_file_atomic(not_a_dir, "not a directory");
            st::CharacterStore bad_store(not_a_dir);
            CharacterList bad_list(
                window,
                bad_store,
                [](const st::CharacterEntry&) {},
                [](const st::CharacterEntry*) {});
            bad_list.refresh_async();

            const gint64 fail_deadline = g_get_monotonic_time() + 5 * 1000 * 1000;
            while (bad_list.is_scanning() && g_get_monotonic_time() < fail_deadline) {
                while (Glib::MainContext::get_default()->iteration(false)) {
                }
                g_usleep(1000);
            }
            if (bad_list.is_scanning() || !bad_list.scan_failed() ||
                bad_list.scan_error().empty()) {
                std::cerr << "smoke: scan failure not reported\n";
                return 1;
            }
            std::cout << "smoke: scan failure\n";
        }

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

        {
            std::string lifecycle_status;
            ChatView lifecycle_view(chats, settings, [&lifecycle_status](const std::string& text) {
                lifecycle_status = text;
            });
            lifecycle_view.show_all();
            if (!lifecycle_view.set_character(entry)) {
                std::cerr << "smoke: set_character failed\n";
                return 1;
            }
            if (!lifecycle_view.rename_current_chat("renamed-chat") ||
                lifecycle_view.current_filename() != "renamed-chat.jsonl") {
                std::cerr << "smoke: rename_current_chat failed: " << lifecycle_status << '\n';
                return 1;
            }
            const std::vector<std::string> after_rename = chats.list(entry.id);
            if (std::find(after_rename.begin(), after_rename.end(), "renamed-chat.jsonl") ==
                after_rename.end()) {
                std::cerr << "smoke: renamed chat missing on disk\n";
                return 1;
            }
            chats.save(entry.id,
                       "zzz-other.jsonl",
                       chats.create(entry.id,
                                    st::normalize_user_name(settings.user_name),
                                    entry.card.first_mes));
            if (!lifecycle_view.delete_current_chat()) {
                std::cerr << "smoke: delete_current_chat failed: " << lifecycle_status << '\n';
                return 1;
            }
            const gint64 delete_deadline = g_get_monotonic_time() + 5 * 1000 * 1000;
            while (lifecycle_view.is_loading() &&
                   g_get_monotonic_time() < delete_deadline) {
                while (Glib::MainContext::get_default()->iteration(false)) {
                }
                g_usleep(1000);
            }
            if (lifecycle_view.is_loading() ||
                lifecycle_view.current_filename() != "zzz-other.jsonl") {
                std::cerr << "smoke: delete_current_chat failed: " << lifecycle_status << '\n';
                return 1;
            }
            std::cout << "smoke: chat lifecycle\n";
        }

        {
            st::CharacterEntry async_first;
            async_first.id = "async-bot-a";
            async_first.card.name = "Async A";
            async_first.card.first_mes = "A.";
            st::CharacterEntry async_second;
            async_second.id = "async-bot-b";
            async_second.card.name = "Async B";
            async_second.card.first_mes = "B.";

            const std::string async_user = st::normalize_user_name(settings.user_name);
            chats.save("async-bot-a",
                       "async-a.jsonl",
                       chats.create("async-bot-a", async_user, async_first.card.first_mes));
            chats.save("async-bot-b",
                       "async-b.jsonl",
                       chats.create("async-bot-b", async_user, async_second.card.first_mes));

            st::ChatDraft async_draft;
            async_draft.text = "Recovered async reply";
            async_draft.send_date = "2026-11-01T00:00:03Z";
            async_draft.base_messages = 1;
            chats.save_draft("async-bot-b", "async-b.jsonl", async_draft);

            ChatView async_view(chats, settings, [](const std::string&) {});
            async_view.show_all();
            async_view.set_character_async(async_first);
            async_view.set_character_async(async_second);

            const gint64 load_deadline = g_get_monotonic_time() + 5 * 1000 * 1000;
            while (async_view.is_loading() && g_get_monotonic_time() < load_deadline) {
                while (Glib::MainContext::get_default()->iteration(false)) {
                }
                g_usleep(1000);
            }
            if (async_view.is_loading() ||
                async_view.current_filename() != "async-b.jsonl") {
                std::cerr << "smoke: async chat load failed: "
                          << async_view.current_filename() << '\n';
                return 1;
            }
            const st::Chat async_loaded = chats.load("async-bot-b", "async-b.jsonl");
            if (async_loaded.messages.size() != std::size_t(2) ||
                async_loaded.messages.back().mes != "Recovered async reply" ||
                chats.load_draft("async-bot-b", "async-b.jsonl").has_value()) {
                std::cerr << "smoke: async chat load failed: draft recovery\n";
                return 1;
            }
            std::cout << "smoke: async chat load\n";
        }

        {
            const st::CharacterEntry second = characters.import_file(source);
            bool list_changed = false;
            CharacterList list_widget(
                window,
                characters,
                [](const st::CharacterEntry&) {},
                [&list_changed](const st::CharacterEntry*) { list_changed = true; });
            list_widget.refresh();
            list_widget.select_id(second.id);
            if (!list_widget.delete_selected() || !list_changed ||
                characters.list().size() != std::size_t(1)) {
                std::cerr << "smoke: character delete failed\n";
                return 1;
            }

            const std::filesystem::path hidden_source = root.path() / "hidden-card.png";
            st::write_file_atomic(hidden_source, make_smoke_png("Hidden Bot"));
            const st::CharacterEntry hidden = characters.import_file(hidden_source);

            ChatView filtered_view(chats, settings, [](const std::string&) {});
            filtered_view.show_all();
            if (!filtered_view.set_character(hidden)) {
                std::cerr << "smoke: filtered view set_character failed\n";
                return 1;
            }

            bool filtered_changed = false;
            bool filtered_has_selection = true;
            CharacterList filtered_list(
                window,
                characters,
                [](const st::CharacterEntry&) {},
                [&filtered_changed, &filtered_has_selection, &filtered_view](
                    const st::CharacterEntry* selected) {
                    filtered_changed = true;
                    filtered_has_selection = selected != nullptr;
                    if (selected == nullptr) {
                        filtered_view.clear();
                    }
                });
            filtered_list.refresh();
            filtered_list.set_search_text("Hidden Bot");
            filtered_list.select_id(hidden.id);
            if (!filtered_list.delete_selected() || !filtered_changed ||
                filtered_has_selection || filtered_list.empty() ||
                !filtered_view.current_filename().empty()) {
                std::cerr << "smoke: filtered character delete failed\n";
                return 1;
            }
            std::cout << "smoke: character delete\n";
        }

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

            const std::vector<std::string> mock_chats = chats.list("mock");
            if (mock_chats.empty()) {
                std::cerr << "smoke: UI stream did not save a chat\n";
                return 1;
            }
            const st::Chat streamed = chats.load("mock", mock_chats.back());
            if (streamed.messages.size() != std::size_t(3) ||
                streamed.messages.back().name != "MockBot" ||
                streamed.messages.back().mes != "hello world") {
                std::cerr << "smoke: UI stream reply mismatch\n";
                return 1;
            }
            std::cout << "smoke: ui streaming\n";

            st::Settings slow_settings = mock_settings;
            slow_settings.api_base_url = std::string(mock_base) + "/slow-partial";

            st::CharacterEntry slow_entry;
            slow_entry.id = "slow-bot";
            slow_entry.card.name = "SlowBot";
            slow_entry.card.system_prompt = "Test.";
            slow_entry.card.first_mes = "Wait.";

            ChatView slow_view(chats, slow_settings, [](const std::string&) {});
            slow_view.show_all();
            slow_view.set_character(slow_entry);
            const std::string slow_filename = slow_view.current_filename();
            if (slow_filename.empty()) {
                std::cerr << "smoke: slow-bot chat has no filename\n";
                return 1;
            }
            slow_view.submit_message("Interrupt me");

            bool draft_seen = false;
            const gint64 draft_wait = g_get_monotonic_time() + 1000 * 1000;
            while (g_get_monotonic_time() < draft_wait) {
                while (Glib::MainContext::get_default()->iteration(false)) {
                }
                try {
                    const std::optional<st::ChatDraft> draft =
                        chats.load_draft("slow-bot", slow_filename);
                    if (draft.has_value() &&
                        draft->text.find("partial") != std::string::npos) {
                        draft_seen = true;
                        break;
                    }
                } catch (const std::exception&) {
                }
                g_usleep(5000);
            }
            if (!draft_seen) {
                std::cerr << "smoke: autosave journal missing\n";
                return 1;
            }
            std::cout << "smoke: autosave journal\n";

            slow_view.set_character(mock_entry);

            const std::vector<std::string> slow_chats = chats.list("slow-bot");
            if (slow_chats.empty()) {
                std::cerr << "smoke: slow-bot chat missing\n";
                return 1;
            }
            const st::Chat partial_chat = chats.load("slow-bot", slow_chats.back());
            if (partial_chat.messages.size() != std::size_t(3) ||
                partial_chat.messages.back().name != "SlowBot" ||
                partial_chat.messages.back().mes != "partial") {
                std::cerr << "smoke: partial on switch mismatch\n";
                return 1;
            }
            std::cout << "smoke: partial on switch\n";
        }

        {
            st::CharacterEntry recover_entry;
            recover_entry.id = "recover-bot";
            recover_entry.card.name = "RecoverBot";
            recover_entry.card.system_prompt = "Test.";
            recover_entry.card.first_mes = "Greeting.";

            st::Chat recover_chat = chats.create("recover-bot",
                                                 st::normalize_user_name(settings.user_name),
                                                 "Greeting.");
            st::ChatMessage recover_user;
            recover_user.name = st::normalize_user_name(settings.user_name);
            recover_user.is_user = true;
            recover_user.send_date = "2026-11-01T00:00:01Z";
            recover_user.mes = "Hi";
            recover_chat.messages.push_back(recover_user);
            chats.save("recover-bot", "recover.jsonl", recover_chat);

            st::ChatDraft recover_draft;
            recover_draft.text = "Recovered partial";
            recover_draft.continue_mode = false;
            recover_draft.send_date = "2026-11-01T00:00:02Z";
            recover_draft.base_messages = 2;
            chats.save_draft("recover-bot", "recover.jsonl", recover_draft);

            ChatView recover_view(chats, settings, [](const std::string&) {});
            recover_view.show_all();
            if (!recover_view.set_character(recover_entry)) {
                std::cerr << "smoke: crash recovery set_character failed\n";
                return 1;
            }

            const st::Chat recovered = chats.load("recover-bot", "recover.jsonl");
            if (recovered.messages.size() != std::size_t(3) ||
                recovered.messages.back().name != "RecoverBot" ||
                recovered.messages.back().mes != "Recovered partial" ||
                recovered.messages.back().send_date != "2026-11-01T00:00:02Z") {
                std::cerr << "smoke: crash recovery mismatch\n";
                return 1;
            }
            if (chats.load_draft("recover-bot", "recover.jsonl").has_value()) {
                std::cerr << "smoke: crash recovery left the draft behind\n";
                return 1;
            }
            std::cout << "smoke: crash recovery\n";
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
