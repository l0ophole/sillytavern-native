#include "ui/smoke.h"

#include "core/atomic_file.h"
#include "core/base64.h"
#include "core/chat_session.h"
#include "core/identity.h"
#include "core/json.h"
#include "core/persona.h"
#include "core/prompt_manager.h"
#include "ui/avatar.h"
#include "ui/main_window.h"
#include "ui/settings_dialog.h"

#include <glib.h>
#include <glibmm/main.h>
#include <gtkmm/main.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

// Returns the plain text of the (non-editable) transcript TextView found
// under the given container, recursing into nested containers.
std::string transcript_text(Gtk::Container& container) {
    for (Gtk::Widget* child : container.get_children()) {
        if (auto* view = dynamic_cast<Gtk::TextView*>(child)) {
            if (!view->get_editable()) {
                return view->get_buffer()->get_text();
            }
        }
        if (auto* nested = dynamic_cast<Gtk::Container*>(child)) {
            std::string found = transcript_text(*nested);
            if (!found.empty()) {
                return found;
            }
        }
    }
    return {};
}

// Finds the first widget of the requested type under the given widget,
// recursing into nested containers.
template <typename T>
T* find_widget(Gtk::Widget& widget) {
    if (auto* found = dynamic_cast<T*>(&widget)) {
        return found;
    }
    if (auto* container = dynamic_cast<Gtk::Container*>(&widget)) {
        for (Gtk::Widget* child : container->get_children()) {
            if (T* nested = find_widget<T>(*child)) {
                return nested;
            }
        }
    }
    return nullptr;
}

bool selector_has_file(Gtk::Container& container, const std::string& filename) {
    for (Gtk::Widget* child : container.get_children()) {
        if (auto* combo = dynamic_cast<Gtk::ComboBoxText*>(child)) {
            const auto model = combo->get_model();
            if (model) {
                for (const auto& row : model->children()) {
                    Glib::ustring value;
                    row.get_value(0, value);
                    if (value == filename) {
                        return true;
                    }
                }
            }
        }
        if (auto* nested = dynamic_cast<Gtk::Container*>(child)) {
            if (selector_has_file(*nested, filename)) {
                return true;
            }
        }
    }
    return false;
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
            st::CharacterEntry stale_first;
            stale_first.id = "stale-bot-a";
            stale_first.card.name = "Stale A";
            stale_first.card.first_mes = "Stale A.";
            st::CharacterEntry stale_second;
            stale_second.id = "stale-bot-b";
            stale_second.card.name = "Stale B";
            stale_second.card.first_mes = "Stale B.";

            const std::string stale_user = st::normalize_user_name(settings.user_name);
            chats.save("stale-bot-a",
                       "stale-a.jsonl",
                       chats.create("stale-bot-a", stale_user, stale_first.card.first_mes));
            chats.save("stale-bot-b",
                       "stale-b.jsonl",
                       chats.create("stale-bot-b", stale_user, stale_second.card.first_mes));

            ChatView stale_view(chats, settings, [](const std::string&) {});
            stale_view.show_all();
            stale_view.set_character_async(stale_first);
            stale_view.set_character_async(stale_second);
            stale_view.set_character_async(stale_second);

            const gint64 stale_deadline = g_get_monotonic_time() + 5 * 1000 * 1000;
            while (stale_view.is_loading() && g_get_monotonic_time() < stale_deadline) {
                while (Glib::MainContext::get_default()->iteration(false)) {
                }
                g_usleep(1000);
            }
            if (stale_view.is_loading() ||
                stale_view.current_filename() != "stale-b.jsonl") {
                std::cerr << "smoke: async stale load failed: "
                          << stale_view.current_filename() << '\n';
                return 1;
            }
            std::cout << "smoke: async stale load\n";
        }

        {
            st::CharacterEntry malformed_entry;
            malformed_entry.id = "malformed-bot";
            malformed_entry.card.name = "Malformed Bot";
            malformed_entry.card.first_mes = "Hi.";

            const std::string malformed_user = st::normalize_user_name(settings.user_name);
            chats.save("malformed-bot",
                       "malformed.jsonl",
                       chats.create("malformed-bot", malformed_user, "Hi."));
            st::write_file_atomic(chats.draft_path("malformed-bot", "malformed.jsonl"), "{");

            ChatView malformed_view(chats, settings, [](const std::string&) {});
            malformed_view.show_all();
            malformed_view.set_character_async(malformed_entry);

            const gint64 malformed_deadline = g_get_monotonic_time() + 5 * 1000 * 1000;
            while (malformed_view.is_loading() &&
                   g_get_monotonic_time() < malformed_deadline) {
                while (Glib::MainContext::get_default()->iteration(false)) {
                }
                g_usleep(1000);
            }
            if (malformed_view.is_loading() ||
                malformed_view.current_filename() != "malformed.jsonl" ||
                std::filesystem::exists(chats.draft_path("malformed-bot", "malformed.jsonl"))) {
                std::cerr << "smoke: malformed draft failed: "
                          << malformed_view.current_filename() << '\n';
                return 1;
            }
            const st::Chat malformed_loaded = chats.load("malformed-bot", "malformed.jsonl");
            if (malformed_loaded.messages.size() != std::size_t(1) ||
                malformed_loaded.messages.back().mes != "Hi.") {
                std::cerr << "smoke: malformed draft failed: chat mismatch\n";
                return 1;
            }
            std::cout << "smoke: malformed draft\n";
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

            {
                const std::string swipe_filename = mock_chats.back();
                const std::size_t last = chats.load("mock", swipe_filename).messages.size() - 1;
                st::ChatSession before_regen(mock_entry.card,
                                              mock_settings,
                                              chats,
                                              "mock",
                                              swipe_filename,
                                              chats.load("mock", swipe_filename));
                if (before_regen.swipe_count(last) != 1) {
                    std::cerr << "smoke: swipes initial count mismatch\n";
                    return 1;
                }

                view.on_regenerate();
                const gint64 regen_deadline = g_get_monotonic_time() + 5 * 1000 * 1000;
                while (g_get_monotonic_time() < regen_deadline) {
                    while (Glib::MainContext::get_default()->iteration(false)) {
                    }
                    if (!view.is_streaming() && ui_status != "Generating...") {
                        break;
                    }
                    g_usleep(5000);
                }
                if (view.is_streaming()) {
                    std::cerr << "smoke: regenerate timed out\n";
                    return 1;
                }

                st::ChatSession after_regen(mock_entry.card,
                                             mock_settings,
                                             chats,
                                             "mock",
                                             swipe_filename,
                                             chats.load("mock", swipe_filename));
                if (after_regen.swipe_count(last) != 2 ||
                    after_regen.swipe_index(last) != 1) {
                    std::cerr << "smoke: regenerate did not add a swipe\n";
                    return 1;
                }
                if (!view.swipe_current(-1)) {
                    std::cerr << "smoke: swipe_current failed\n";
                    return 1;
                }
                st::ChatSession after_swipe(mock_entry.card,
                                             mock_settings,
                                             chats,
                                             "mock",
                                             swipe_filename,
                                             chats.load("mock", swipe_filename));
                if (after_swipe.swipe_index(last) != 0 ||
                    after_swipe.chat().messages[last].mes != "hello world") {
                    std::cerr << "smoke: swipe did not restore original text\n";
                    return 1;
                }
                std::cout << "smoke: swipes\n";
            }

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

            // Cancelling (switching away) mid-regenerate must preserve the
            // popped original reply as a swipe instead of discarding it.
            st::CharacterEntry regen_entry;
            regen_entry.id = "regen-bot";
            regen_entry.card.name = "RegenBot";
            regen_entry.card.system_prompt = "Test.";
            regen_entry.card.first_mes = "Greeting.";

            st::Chat regen_chat = chats.create("regen-bot",
                                               st::normalize_user_name(settings.user_name),
                                               "Greeting.");
            st::ChatMessage regen_user;
            regen_user.name = st::normalize_user_name(settings.user_name);
            regen_user.is_user = true;
            regen_user.send_date = "2026-11-01T00:00:01Z";
            regen_user.mes = "Hi";
            regen_chat.messages.push_back(regen_user);
            st::ChatMessage regen_original;
            regen_original.name = "RegenBot";
            regen_original.send_date = "2026-11-01T00:00:02Z";
            regen_original.mes = "original";
            regen_chat.messages.push_back(regen_original);
            chats.save("regen-bot", "regen.jsonl", regen_chat);

            ChatView regen_view(chats, slow_settings, [](const std::string&) {});
            regen_view.show_all();
            if (!regen_view.set_character(regen_entry)) {
                std::cerr << "smoke: cancel-regenerate set_character failed\n";
                return 1;
            }
            regen_view.on_regenerate();
            if (!regen_view.is_streaming()) {
                std::cerr << "smoke: regenerate did not start streaming\n";
                return 1;
            }

            bool regen_draft_seen = false;
            const gint64 regen_wait = g_get_monotonic_time() + 1000 * 1000;
            while (g_get_monotonic_time() < regen_wait) {
                while (Glib::MainContext::get_default()->iteration(false)) {
                }
                try {
                    const std::optional<st::ChatDraft> draft =
                        chats.load_draft("regen-bot", "regen.jsonl");
                    if (draft.has_value() &&
                        draft->text.find("partial") != std::string::npos) {
                        regen_draft_seen = true;
                        break;
                    }
                } catch (const std::exception&) {
                }
                g_usleep(5000);
            }
            if (!regen_draft_seen) {
                std::cerr << "smoke: regenerate draft missing\n";
                return 1;
            }
            // Drop the draft so the reload does not re-recover it; the
            // assertion below targets the stop_streaming save alone.
            chats.clear_draft("regen-bot", "regen.jsonl");
            regen_view.set_character(regen_entry);

            const st::Chat regen_saved = chats.load("regen-bot", "regen.jsonl");
            if (regen_saved.messages.size() != std::size_t(3)) {
                std::cerr << "smoke: cancel during regenerate message count mismatch\n";
                return 1;
            }
            const std::size_t regen_last = regen_saved.messages.size() - 1;
            st::ChatSession regen_session(regen_entry.card,
                                          settings,
                                          chats,
                                          "regen-bot",
                                          "regen.jsonl",
                                          regen_saved);
            if (regen_session.swipe_count(regen_last) != 2 ||
                regen_session.swipe_index(regen_last) != 1 ||
                regen_session.chat().messages[regen_last].mes != "partial" ||
                !regen_session.select_swipe(regen_last, 0) ||
                regen_session.chat().messages[regen_last].mes != "original") {
                std::cerr << "smoke: cancel during regenerate lost the original reply\n";
                return 1;
            }
            std::cout << "smoke: cancel during regenerate\n";

            // Backup/Restore stay disabled while a generation streams, so
            // a restore cannot clobber the just-restored files.
            {
                st::Settings guard_settings = settings;
                guard_settings.api_base_url = std::string(mock_base) + "/slow-partial";
                guard_settings.model = "mock";
                guard_settings.stream = true;
                MainWindow guard_window(paths, guard_settings, characters, chats);
                guard_window.show_all();

                auto* guard_list = find_widget<CharacterList>(guard_window);
                auto* guard_view = find_widget<ChatView>(guard_window);
                if (guard_list == nullptr || guard_view == nullptr) {
                    std::cerr << "smoke: backup guard widgets missing\n";
                    return 1;
                }
                const gint64 guard_scan_deadline =
                    g_get_monotonic_time() + 5 * 1000 * 1000;
                while ((guard_list->is_scanning() || guard_list->visible_entries().empty()) &&
                       g_get_monotonic_time() < guard_scan_deadline) {
                    while (Glib::MainContext::get_default()->iteration(false)) {
                    }
                    g_usleep(1000);
                }
                guard_list->select_id(entry.id);
                const gint64 guard_load_deadline =
                    g_get_monotonic_time() + 5 * 1000 * 1000;
                while (guard_view->current_filename().empty() &&
                       g_get_monotonic_time() < guard_load_deadline) {
                    while (Glib::MainContext::get_default()->iteration(false)) {
                    }
                    g_usleep(1000);
                }
                if (guard_view->current_filename().empty()) {
                    std::cerr << "smoke: backup guard character load failed\n";
                    return 1;
                }
                if (!guard_view->submit_message("Hi")) {
                    std::cerr << "smoke: backup guard submit failed\n";
                    return 1;
                }
                if (!guard_view->is_streaming() ||
                    guard_window.backup_actions_enabled()) {
                    std::cerr << "smoke: backup actions not disabled while streaming\n";
                    return 1;
                }
                guard_view->clear();
                if (guard_view->is_streaming() ||
                    !guard_window.backup_actions_enabled()) {
                    std::cerr << "smoke: backup actions not re-enabled after streaming\n";
                    return 1;
                }
                std::cout << "smoke: backup guard while streaming\n";
            }
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

        {
            st::CharacterEntry branch_entry;
            branch_entry.id = "branch-bot";
            branch_entry.card.name = "BranchBot";
            branch_entry.card.system_prompt = "Test.";
            branch_entry.card.first_mes = "Greeting.";

            st::Chat branch_chat = chats.create(
                "branch-bot", st::normalize_user_name(settings.user_name), "Greeting.");
            st::ChatMessage branch_user;
            branch_user.name = st::normalize_user_name(settings.user_name);
            branch_user.is_user = true;
            branch_user.send_date = "2026-11-01T00:00:01Z";
            branch_user.mes = "Branch user";
            branch_chat.messages.push_back(branch_user);
            st::ChatMessage branch_assistant;
            branch_assistant.name = "BranchBot";
            branch_assistant.send_date = "2026-11-01T00:00:02Z";
            branch_assistant.mes = "Branch assistant";
            branch_chat.messages.push_back(branch_assistant);
            chats.save("branch-bot", "branch-source.jsonl", branch_chat);

            ChatView branch_view(chats, settings, [](const std::string&) {});
            branch_view.show_all();
            if (!branch_view.set_character(branch_entry)) {
                std::cerr << "smoke: branch set_character failed\n";
                return 1;
            }
            if (!branch_view.branch_from(1)) {
                std::cerr << "smoke: branch_from failed\n";
                return 1;
            }
            const gint64 branch_deadline = g_get_monotonic_time() + 5 * 1000 * 1000;
            while (branch_view.is_loading() &&
                   g_get_monotonic_time() < branch_deadline) {
                while (Glib::MainContext::get_default()->iteration(false)) {
                }
                g_usleep(1000);
            }
            if (branch_view.is_loading() ||
                branch_view.current_filename().find("branch") == std::string::npos) {
                std::cerr << "smoke: branch load failed: "
                          << branch_view.current_filename() << '\n';
                return 1;
            }
            const st::Chat branched = chats.load("branch-bot", branch_view.current_filename());
            const st::Chat source_chat = chats.load("branch-bot", "branch-source.jsonl");
            if (branched.messages.size() != std::size_t(2) ||
                source_chat.messages.size() != std::size_t(3)) {
                std::cerr << "smoke: branch message count mismatch\n";
                return 1;
            }
            std::cout << "smoke: branch\n";
        }

        {
            st::Settings dialog_settings = settings;
            bool saved = false;
            {
                SettingsDialog dialog(window, paths, dialog_settings, [&saved]() { saved = true; });
                dialog.show_all();
                dialog.authors_note_enabled_widget().set_active(true);
                dialog.authors_note_widget().get_buffer()->set_text("Stay in character.");
                dialog.authors_note_depth_widget().set_value(1);
                dialog.response(Gtk::RESPONSE_OK);
            }
            if (!saved || !dialog_settings.authors_note_enabled ||
                dialog_settings.authors_note != "Stay in character." ||
                dialog_settings.authors_note_depth != 1) {
                std::cerr << "smoke: authors note save failed\n";
                return 1;
            }
            {
                SettingsDialog reopened(window, paths, dialog_settings, []() {});
                if (!reopened.authors_note_enabled_widget().get_active() ||
                    reopened.authors_note_widget().get_buffer()->get_text() != "Stay in character." ||
                    reopened.authors_note_depth_widget().get_value_as_int() != 1) {
                    std::cerr << "smoke: authors note round-trip failed\n";
                    return 1;
                }
            }
            std::cout << "smoke: authors note\n";
        }

        {
            st::Settings persona_settings = settings;
            bool saved = false;
            {
                SettingsDialog dialog(window, paths, persona_settings, [&saved]() { saved = true; });
                dialog.show_all();
                dialog.persona_widget().set_active_text("Rook");
                // Typing a brand-new name into an editable ComboBoxText does
                // not add it to the list, so drive it like a selectable row:
                // ensure the name exists, then activate it.
                {
                    bool present = false;
                    if (dialog.persona_widget().get_model()) {
                        for (const auto& row : dialog.persona_widget().get_model()->children()) {
                            Glib::ustring value;
                            row.get_value(0, value);
                            if (value == "Rook") {
                                present = true;
                            }
                        }
                    }
                    if (!present) {
                        dialog.persona_widget().append("Rook");
                    }
                }
                dialog.persona_widget().set_active(1);
                dialog.persona_description_widget().get_buffer()->set_text("Calm guide.");
                dialog.instruct_enabled_widget().set_active(true);
                dialog.instruct_format_widget().set_active(1);
                dialog.context_template_widget().get_buffer()->set_text("{{char}} greets {{user}}");
                dialog.response(Gtk::RESPONSE_OK);
            }
            if (!saved || persona_settings.persona_name != "Rook" ||
                !persona_settings.instruct_mode ||
                persona_settings.instruct_format != "chatml" ||
                persona_settings.context_template != "{{char}} greets {{user}}") {
                std::cerr << "smoke: persona save failed\n";
                return 1;
            }
            // Persona description syncs into personas.json via PersonaStore.
            {
                bool found_description = false;
                try {
                    const st::PersonaStore store = st::PersonaStore::load(paths.data_dir / "personas.json");
                    if (store.active_name == "Rook" && store.active() != nullptr &&
                        store.active()->description == "Calm guide.") {
                        found_description = true;
                    }
                } catch (const std::exception&) {
                }
                if (!found_description) {
                    std::cerr << "smoke: persona store save failed\n";
                    return 1;
                }
            }
            {
                SettingsDialog reopened(window, paths, persona_settings, []() {});
                if (reopened.persona_widget().get_active_text() != "Rook" ||
                    reopened.persona_description_widget().get_buffer()->get_text() != "Calm guide." ||
                    !reopened.instruct_enabled_widget().get_active() ||
                    reopened.instruct_format_widget().get_active_text() != "chatml" ||
                    reopened.context_template_widget().get_buffer()->get_text() != "{{char}} greets {{user}}") {
                    std::cerr << "smoke: persona round-trip failed\n";
                    return 1;
                }
            }
            std::cout << "smoke: persona\n";
        }

        {
            // A corrupt personas.json must survive a manager save attempt:
            // the dialog reports the load error and save() refuses.
            const std::filesystem::path personas_file = paths.data_dir / "personas.json";
            st::write_file_atomic(personas_file, "{");
            st::Settings corrupt_settings = settings;
            corrupt_settings.persona_name = "Rook";
            bool refused = false;
            {
                PersonaManagerDialog manager(window, paths, corrupt_settings, []() {});
                if (manager.load_error().empty()) {
                    std::cerr << "smoke: persona corrupt load error missing\n";
                    return 1;
                }
                try {
                    manager.save();
                } catch (const std::exception&) {
                    refused = true;
                }
            }
            if (!refused) {
                std::cerr << "smoke: persona corrupt save not refused\n";
                return 1;
            }
            std::string preserved;
            {
                std::ifstream input(personas_file);
                preserved.assign(std::istreambuf_iterator<char>(input),
                                 std::istreambuf_iterator<char>());
            }
            if (preserved != "{" || corrupt_settings.persona_name != "Rook") {
                std::cerr << "smoke: persona corrupt file clobbered\n";
                return 1;
            }
            std::cout << "smoke: persona corrupt\n";
        }

        {
            st::Settings sampler_settings = settings;
            bool saved = false;
            {
                SettingsDialog dialog(window, paths, sampler_settings, [&saved]() { saved = true; });
                dialog.show_all();
                dialog.top_k_widget().set_value(40);
                dialog.repetition_penalty_widget().set_value(1.10);
                dialog.presence_penalty_widget().set_value(0.50);
                dialog.frequency_penalty_widget().set_value(-0.50);
                dialog.regex_rules_widget().get_buffer()->set_text(
                    "[{\"pattern\":\"foo\",\"replacement\":\"bar\"}]");
                dialog.variables_widget().get_buffer()->set_text("{\"mood\":\"calm\"}");
                dialog.response(Gtk::RESPONSE_OK);
            }
            if (!saved || sampler_settings.top_k != 40 ||
                sampler_settings.repetition_penalty != 1.10 ||
                sampler_settings.presence_penalty != 0.50 ||
                sampler_settings.frequency_penalty != -0.50 ||
                !sampler_settings.regex_rules.is_array() ||
                sampler_settings.regex_rules.size() != std::size_t(1) ||
                sampler_settings.regex_rules[0]["pattern"] != "foo" ||
                sampler_settings.regex_rules[0]["replacement"] != "bar" ||
                sampler_settings.variables != std::map<std::string, std::string>{{"mood", "calm"}}) {
                std::cerr << "smoke: sampler save failed\n";
                return 1;
            }
            {
                SettingsDialog reopened(window, paths, sampler_settings, []() {});
                const st::Json regex = st::parse_json(
                    reopened.regex_rules_widget().get_buffer()->get_text(), "regex");
                const st::Json variables = st::parse_json(
                    reopened.variables_widget().get_buffer()->get_text(), "variables");
                if (reopened.top_k_widget().get_value_as_int() != 40 ||
                    reopened.repetition_penalty_widget().get_value() != 1.10 ||
                    reopened.presence_penalty_widget().get_value() != 0.50 ||
                    reopened.frequency_penalty_widget().get_value() != -0.50 ||
                    !regex.is_array() || regex.size() != std::size_t(1) ||
                    regex[0]["pattern"] != "foo" || regex[0]["replacement"] != "bar" ||
                    !variables.is_object() || variables["mood"] != "calm") {
                    std::cerr << "smoke: sampler round-trip failed\n";
                    return 1;
                }
            }
            std::cout << "smoke: sampler\n";
        }

        {
            st::CharacterEntry md_entry;
            md_entry.id = "md-bot";
            md_entry.card.name = "MdBot";
            md_entry.card.first_mes = "Hi.";

            st::Chat md_chat = chats.create(
                "md-bot", st::normalize_user_name(settings.user_name), "Hi.");
            st::ChatMessage md_assistant;
            md_assistant.name = "MdBot";
            md_assistant.send_date = "2026-11-01T00:00:02Z";
            md_assistant.mes = "**bold** and <tag>";
            md_chat.messages.push_back(md_assistant);
            chats.save("md-bot", "md.jsonl", md_chat);

            ChatView md_view(chats, settings, [](const std::string&) {});
            md_view.show_all();
            if (!md_view.set_character(md_entry)) {
                std::cerr << "smoke: markdown render failed\n";
                return 1;
            }
            const std::string rendered = transcript_text(md_view);
            if (rendered.find("bold and <tag>") == std::string::npos ||
                rendered.find("**") != std::string::npos) {
                std::cerr << "smoke: markdown render mismatch\n";
                return 1;
            }
            std::cout << "smoke: markdown render\n";
        }

        {
            chats.save(entry.id,
                       "backup-target.jsonl",
                       chats.create(entry.id,
                                    st::normalize_user_name(settings.user_name),
                                    entry.card.first_mes));
            const std::string backup_id = window.create_backup();
            if (backup_id.empty()) {
                std::cerr << "smoke: create_backup failed\n";
                return 1;
            }
            chats.remove(entry.id, "backup-target.jsonl");

            ChatView check_view(chats, settings, [](const std::string&) {});
            check_view.show_all();
            if (!check_view.set_character(entry)) {
                std::cerr << "smoke: backup check set_character failed\n";
                return 1;
            }
            if (selector_has_file(check_view, "backup-target.jsonl")) {
                std::cerr << "smoke: backup target should be missing\n";
                return 1;
            }
            if (!window.restore_backup(backup_id)) {
                std::cerr << "smoke: restore_backup failed\n";
                return 1;
            }
            if (!check_view.set_character(entry)) {
                std::cerr << "smoke: backup check set_character failed\n";
                return 1;
            }
            const gint64 backup_deadline = g_get_monotonic_time() + 5 * 1000 * 1000;
            while (check_view.is_loading() && g_get_monotonic_time() < backup_deadline) {
                while (Glib::MainContext::get_default()->iteration(false)) {
                }
                g_usleep(1000);
            }
            if (check_view.is_loading() ||
                !selector_has_file(check_view, "backup-target.jsonl")) {
                std::cerr << "smoke: backup round trip failed\n";
                return 1;
            }
            std::cout << "smoke: backup round trip\n";
        }

        {
            st::Settings wi_settings = settings;
            wi_settings.world_info_enabled = true;
            wi_settings.world_info_book = "lorebook";
            {
                WorldInfoDialog dialog(window, paths, wi_settings, []() {});
                dialog.show_all();
                st::WorldInfoEntry dragon;
                dragon.key = "dragon";
                dragon.comment = "Dragon lore";
                dragon.content = "Dragons hoard gold and breathe fire.";
                dragon.enabled = true;
                dialog.add_entry(dragon);
                dialog.save();
            }
            {
                WorldInfoDialog reopened(window, paths, wi_settings, []() {});
                reopened.load();
                if (reopened.book().entries.size() != std::size_t(1) ||
                    reopened.book().entries[0].key != "dragon") {
                    std::cerr << "smoke: world info round-trip failed\n";
                    return 1;
                }
                st::Chat wi_chat = chats.create(entry.id,
                                                  st::normalize_user_name(settings.user_name),
                                                  entry.card.first_mes);
                st::ChatSession wi_session(entry.card,
                                           wi_settings,
                                           chats,
                                           entry.id,
                                           "smoke-world-info.jsonl",
                                           wi_chat);
                wi_session.set_world_info_book(reopened.book());
                wi_session.append_user("Tell me about the dragon.");
                bool found_lore = false;
                for (const st::PromptMessage& message : wi_session.prompt_messages()) {
                    if (message.content.find("Dragons hoard gold") != std::string::npos) {
                        found_lore = true;
                    }
                }
                if (!found_lore) {
                    std::cerr << "smoke: world info lore not in prompt\n";
                    return 1;
                }
            }
            std::cout << "smoke: world info\n";
        }

        {
            st::Settings preset_settings = settings;
            preset_settings.active_preset = "promo";
            bool saved = false;
            {
                PromptManagerDialog manager(window, paths, preset_settings,
                                            [&saved]() { saved = true; });
                manager.show_all();
                manager.set_preset_name("promo");
                st::PromptSection custom;
                custom.id = "custom";
                custom.title = "Custom";
                custom.content = "Be nice.";
                custom.enabled = true;
                custom.order = 0;
                manager.add_section(custom);
                manager.save();
            }
            if (!saved) {
                std::cerr << "smoke: prompt preset save failed\n";
                return 1;
            }
            {
                SettingsDialog reopened(window, paths, preset_settings, []() {});
                bool found = false;
                if (reopened.preset_widget().get_model()) {
                    for (const auto& row : reopened.preset_widget().get_model()->children()) {
                        Glib::ustring value;
                        row.get_value(0, value);
                        if (value == "promo") {
                            found = true;
                        }
                    }
                }
                if (!found) {
                    std::cerr << "smoke: prompt preset combo missing promo\n";
                    return 1;
                }
                const std::string text = st::read_file_limited(
                    paths.presets_dir / "promo.json", 16u * 1024u * 1024u, "preset");
                const st::PromptPreset loaded =
                    st::PromptPreset::from_json(st::parse_json(text, "preset"));
                if (loaded.sections.size() != std::size_t(1) ||
                    loaded.sections[0].id != "custom") {
                    std::cerr << "smoke: prompt preset section count mismatch\n";
                    return 1;
                }
            }
            std::cout << "smoke: prompt preset\n";
        }

        {
            // Live wiring: ChatView loads personas.json and the active
            // preset from disk; the resulting prompt carries the persona
            // description and preset system text, omits the disabled
            // description section, and the preset sampling override wins.
            st::PersonaStore live_personas;
            st::Persona mia;
            mia.name = "Mia";
            mia.description = "Brave explorer.";
            live_personas.personas.push_back(mia);
            live_personas.active_name = "Mia";
            live_personas.save(paths.data_dir / "personas.json");

            st::PromptPreset live_preset;
            live_preset.name = "smoke-live";
            st::PromptSection live_system;
            live_system.id = "system";
            live_system.title = "System";
            live_system.content = "Live preset system.";
            live_system.enabled = true;
            live_system.order = 0;
            st::PromptSection live_description;
            live_description.id = "description";
            live_description.title = "Description";
            live_description.content = "";
            live_description.enabled = false;
            live_description.order = 1;
            st::PromptSection live_persona;
            live_persona.id = "persona";
            live_persona.title = "Persona";
            live_persona.content = "";
            live_persona.enabled = true;
            live_persona.order = 2;
            live_preset.sections = {live_system, live_description, live_persona};
            live_preset.sampling_overrides = st::Json::object();
            live_preset.sampling_overrides["temperature"] = 0.25;
            st::write_file_atomic(paths.presets_dir / "smoke-live.json",
                                  st::dump_json(live_preset.to_json(), 2) + "\n");

            st::Settings live_settings = settings;
            live_settings.active_preset = "smoke-live";
            live_settings.persona_name = "Mia";
            std::string live_status;
            ChatView live_view(chats, live_settings,
                               [&live_status](const std::string& text) { live_status = text; },
                               &paths);
            live_view.show_all();
            if (!live_view.set_character(entry)) {
                std::cerr << "smoke: prompt live set_character failed: " << live_status << '\n';
                return 1;
            }
            const st::Persona* view_active = live_view.persona_store().active();
            if (view_active == nullptr || view_active->description != "Brave explorer.") {
                std::cerr << "smoke: prompt live persona not loaded into view\n";
                return 1;
            }
            bool view_has_system = false;
            bool view_has_description = false;
            for (const st::PromptSection& section :
                 live_view.prompt_manager().enabled_sections()) {
                if (section.id == "system" &&
                    section.content == "Live preset system.") {
                    view_has_system = true;
                }
                if (section.id == "description") {
                    view_has_description = true;
                }
            }
            if (!view_has_system || view_has_description) {
                std::cerr << "smoke: prompt live preset not loaded into view\n";
                return 1;
            }
            st::Chat live_chat = chats.create(entry.id,
                                              st::normalize_user_name(settings.user_name),
                                              entry.card.first_mes);
            st::ChatSession live_session(entry.card, live_settings, chats, entry.id,
                                         "smoke-live.jsonl", live_chat);
            live_session.load_persona_store(paths.data_dir);
            live_session.load_prompt_preset(paths.presets_dir, live_settings.active_preset);
            live_session.append_user("Hello.");
            bool found_persona = false;
            bool found_preset_system = false;
            for (const st::PromptMessage& message : live_session.prompt_messages()) {
                if (message.content.find("User persona (Mia): Brave explorer.") !=
                    std::string::npos) {
                    found_persona = true;
                }
                if (message.content.find("Live preset system.") != std::string::npos) {
                    found_preset_system = true;
                }
                if (message.content.find("Synthetic character card") != std::string::npos) {
                    std::cerr << "smoke: prompt live disabled section leaked\n";
                    return 1;
                }
            }
            if (!found_persona || !found_preset_system ||
                live_session.request().sampling.temperature != 0.25) {
                std::cerr << "smoke: prompt live prompt mismatch\n";
                return 1;
            }
            std::cout << "smoke: prompt live\n";
        }

        {
            // Corrupt world-info file must survive a dialog save attempt.
            const std::filesystem::path lore_file =
                paths.world_info_dir / "corrupt-lore.json";
            st::write_file_atomic(lore_file, "{");
            st::Settings corrupt_wi = settings;
            corrupt_wi.world_info_book = "corrupt-lore";
            bool wi_refused = false;
            {
                WorldInfoDialog dialog(window, paths, corrupt_wi, []() {});
                if (dialog.load_error().empty()) {
                    std::cerr << "smoke: world info corrupt load error missing\n";
                    return 1;
                }
                try {
                    dialog.save();
                } catch (const std::exception&) {
                    wi_refused = true;
                }
            }
            std::string preserved;
            {
                std::ifstream input(lore_file);
                preserved.assign(std::istreambuf_iterator<char>(input),
                                 std::istreambuf_iterator<char>());
            }
            if (!wi_refused || preserved != "{") {
                std::cerr << "smoke: world info corrupt file clobbered\n";
                return 1;
            }
            std::cout << "smoke: world info corrupt\n";
        }

        {
            // Corrupt preset file must survive a dialog save attempt.
            const std::filesystem::path preset_file =
                paths.presets_dir / "corrupt-preset.json";
            st::write_file_atomic(preset_file, "{");
            st::Settings corrupt_preset = settings;
            corrupt_preset.active_preset = "corrupt-preset";
            bool preset_refused = false;
            {
                PromptManagerDialog dialog(window, paths, corrupt_preset, []() {});
                if (dialog.load_error().empty()) {
                    std::cerr << "smoke: prompt preset corrupt load error missing\n";
                    return 1;
                }
                try {
                    dialog.save();
                } catch (const std::exception&) {
                    preset_refused = true;
                }
            }
            std::string preserved;
            {
                std::ifstream input(preset_file);
                preserved.assign(std::istreambuf_iterator<char>(input),
                                 std::istreambuf_iterator<char>());
            }
            if (!preset_refused || preserved != "{") {
                std::cerr << "smoke: prompt preset corrupt file clobbered\n";
                return 1;
            }
            std::cout << "smoke: prompt preset corrupt\n";
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
