#include "ui/main_window.h"

#include "core/atomic_file.h"
#include "core/backup.h"
#include "core/json.h"
#include "core/log.h"
#include "core/names.h"
#include "ui/settings_dialog.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace st {
namespace ui {

MainWindow::MainWindow(st::Paths paths,
                       st::Settings& settings,
                       st::CharacterStore& characters,
                       st::ChatStore& chats)
    : paths_(std::move(paths)),
      settings_(settings),
      characters_(characters),
      chats_(chats) {
    set_title("SillyTavern Port");
    set_default_size(1100, 720);

    header_.set_title("SillyTavern Port");
    header_.set_show_close_button(true);
    settings_button_.set_label("Settings");
    header_.pack_end(settings_button_);
    personas_button_.set_label("Personas...");
    header_.pack_end(personas_button_);
    prompt_manager_button_.set_label("Prompt manager...");
    header_.pack_end(prompt_manager_button_);
    world_info_button_.set_label("World Info...");
    header_.pack_end(world_info_button_);
    backup_item_.set_label("Back up chats");
    restore_item_.set_label("Restore backup...");
    backup_menu_.append(backup_item_);
    backup_menu_.append(restore_item_);
    backup_menu_.show_all();
    menu_button_.set_label("Backup");
    menu_button_.set_popup(backup_menu_);
    header_.pack_end(menu_button_);
    set_titlebar(header_);

    chat_view_ = std::make_unique<ChatView>(
        chats_, settings_, [this](const std::string& text) {
            status_.set_text(st::redact_secrets(text, {settings_.api_key}));
            refresh_backup_menu();
        }, &paths_);
    character_list_ = std::make_unique<CharacterList>(
        *this,
        characters_,
        [this](const CharacterEntry& entry) {
            on_character_selected(entry);
        },
        [this](const CharacterEntry* entry) {
            if (entry == nullptr) {
                chat_view_->clear();
                if (character_list_->scan_failed()) {
                    status_.set_text(st::redact_secrets(
                        "Character scan failed: " + character_list_->scan_error(),
                        {settings_.api_key}));
                } else {
                    status_.set_text("No characters");
                }
            }
        });

    paned_.set_position(320);
    paned_.pack1(*character_list_, true, false);
    paned_.pack2(*chat_view_, true, false);

    status_.set_text("Ready");
    status_.set_halign(Gtk::ALIGN_START);
    status_.set_margin_top(4);
    status_.set_margin_bottom(4);
    status_.set_margin_left(8);
    status_.set_margin_right(8);

    root_.set_orientation(Gtk::ORIENTATION_VERTICAL);
    root_.pack_start(paned_, Gtk::PACK_EXPAND_WIDGET);
    root_.pack_end(status_, Gtk::PACK_SHRINK);

    add(root_);

    settings_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &MainWindow::on_settings));
    personas_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &MainWindow::on_personas));
    prompt_manager_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &MainWindow::on_prompt_manager));
    world_info_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &MainWindow::on_world_info));
    backup_item_.signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_backup));
    restore_item_.signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_restore));

    chat_view_->clear();
    status_.set_text("Loading characters...");
    character_list_->refresh_async();

    show_all_children();
}

void MainWindow::on_character_selected(const st::CharacterEntry& entry) {
    if (chat_view_->set_character_async(entry)) {
        status_.set_text("Selected: " + entry.card.name);
    }
}

void MainWindow::on_settings() {
    try {
        SettingsDialog dialog(*this, paths_, settings_, [this]() {
            st::log_set_redactions({settings_.api_key});
            chat_view_->refresh_settings();
            status_.set_text("Settings saved");
        });
        dialog.run();
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error, std::string("settings dialog failed: ") + error.what());
        status_.set_text(
            st::redact_secrets(std::string("Settings error: ") + error.what(), {settings_.api_key}));
    }
}

void MainWindow::on_personas() {
    try {
        PersonaManagerDialog dialog(*this, paths_, settings_, [this]() {
            // personas.json edits refresh via SettingsDialog on next open;
            // keep the active selection visible in the composer prompt.
            chat_view_->refresh_settings();
            status_.set_text("Personas saved");
        });
        if (!dialog.load_error().empty()) {
            status_.set_text(st::redact_secrets(
                std::string("Persona error: ") + dialog.load_error(), {settings_.api_key}));
        }
        dialog.run();
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error, std::string("persona dialog failed: ") + error.what());
        status_.set_text(
            st::redact_secrets(std::string("Persona error: ") + error.what(), {settings_.api_key}));
    }
}

void MainWindow::on_prompt_manager() {
    try {
        PromptManagerDialog dialog(*this, paths_, settings_, [this]() {
            chat_view_->refresh_settings();
            status_.set_text("Prompt manager saved");
        });
        if (!dialog.load_error().empty()) {
            status_.set_text(st::redact_secrets(
                std::string("Preset error: ") + dialog.load_error(), {settings_.api_key}));
        }
        dialog.run();
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error, std::string("prompt manager failed: ") + error.what());
        status_.set_text(
            st::redact_secrets(std::string("Preset error: ") + error.what(), {settings_.api_key}));
    }
}

void MainWindow::on_world_info() {
    try {
        WorldInfoDialog dialog(*this, paths_, settings_, [this]() {
            chat_view_->refresh_settings();
            status_.set_text("World Info saved");
        });
        if (!dialog.load_error().empty()) {
            status_.set_text(st::redact_secrets(
                std::string("World Info error: ") + dialog.load_error(), {settings_.api_key}));
        }
        dialog.run();
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error, std::string("world info dialog failed: ") + error.what());
        status_.set_text(
            st::redact_secrets(std::string("World Info error: ") + error.what(), {settings_.api_key}));
    }
}

void MainWindow::refresh_backup_menu() {
    const bool enabled = !chat_view_->is_streaming();
    backup_item_.set_sensitive(enabled);
    restore_item_.set_sensitive(enabled);
}

std::string MainWindow::create_backup() {
    if (chat_view_->is_streaming()) {
        status_.set_text("Backup error: generation in progress");
        return {};
    }
    try {
        st::BackupManager manager(paths_.chats_dir);
        const std::string id = manager.create();
        status_.set_text(st::redact_secrets("Backup created: " + id, {settings_.api_key}));
        return id;
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error, std::string("backup failed: ") + error.what());
        status_.set_text(
            st::redact_secrets(std::string("Backup error: ") + error.what(), {settings_.api_key}));
        return {};
    }
}

bool MainWindow::backup_actions_enabled() const {
    return backup_item_.get_sensitive() && restore_item_.get_sensitive();
}

bool MainWindow::restore_backup(const std::string& id) {
    if (chat_view_->is_streaming()) {
        status_.set_text("Backup error: generation in progress");
        return false;
    }
    try {
        st::BackupManager manager(paths_.chats_dir);
        manager.restore(id);
        character_list_->refresh();
        chat_view_->clear();
        status_.set_text(st::redact_secrets("Backup restored: " + id, {settings_.api_key}));
        return true;
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error, std::string("backup restore failed: ") + error.what());
        status_.set_text(
            st::redact_secrets(std::string("Backup error: ") + error.what(), {settings_.api_key}));
        return false;
    }
}

void MainWindow::on_backup() {
    (void)create_backup();
}

void MainWindow::on_restore() {
    if (chat_view_->is_streaming()) {
        status_.set_text("Backup error: generation in progress");
        return;
    }
    std::vector<st::BackupEntry> entries;
    try {
        st::BackupManager manager(paths_.chats_dir);
        entries = manager.list();
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error,
                      std::string("backup list failed: ") + error.what());
        status_.set_text(
            st::redact_secrets(std::string("Backup error: ") + error.what(), {settings_.api_key}));
        return;
    }
    if (entries.empty()) {
        status_.set_text("Backup error: no backups");
        return;
    }

    Gtk::Dialog dialog("Restore backup", true);
    dialog.set_transient_for(*this);
    dialog.set_default_size(480, 320);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Restore", Gtk::RESPONSE_OK);
    dialog.set_default_response(Gtk::RESPONSE_OK);

    auto* list = Gtk::manage(new Gtk::ListBox());
    list->set_selection_mode(Gtk::SELECTION_SINGLE);
    for (const auto& entry : entries) {
        auto* row = Gtk::manage(new Gtk::ListBoxRow());
        auto* label = Gtk::manage(new Gtk::Label(
            entry.id + " (" + entry.created + ", " + std::to_string(entry.chats) + " chats)"));
        row->add(*label);
        list->append(*row);
    }
    list->select_row(*list->get_row_at_index(0));
    auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
    scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scroll->add(*list);
    dialog.get_content_area()->pack_start(*scroll, true, true, 6);
    dialog.show_all();

    if (dialog.run() != Gtk::RESPONSE_OK) {
        return;
    }
    Gtk::ListBoxRow* selected = list->get_selected_row();
    if (selected == nullptr) {
        return;
    }
    const int index = selected->get_index();
    if (index < 0 ||
        static_cast<std::size_t>(index) >= entries.size()) {
        return;
    }
    const std::string id = entries[static_cast<std::size_t>(index)].id;

    Gtk::MessageDialog confirm("Restore backup " + id + "?",
                               false,
                               Gtk::MESSAGE_QUESTION,
                               Gtk::BUTTONS_OK_CANCEL,
                               true);
    confirm.set_transient_for(*this);
    confirm.set_secondary_text(
        "Chats from backup " + id + " will replace the current chat files.");
    if (confirm.run() != Gtk::RESPONSE_OK) {
        return;
    }
    (void)restore_backup(id);
}

namespace {

Gtk::Label* wi_label(const char* text) {
    auto* label = Gtk::manage(new Gtk::Label(text));
    label->set_halign(Gtk::ALIGN_START);
    return label;
}

}  // namespace

WorldInfoDialog::WorldInfoDialog(Gtk::Window& parent,
                                 const st::Paths& paths,
                                 st::Settings& settings,
                                 std::function<void()> on_saved)
    : Gtk::Dialog("World Info", parent, true),
      paths_(paths),
      settings_(settings),
      on_saved_(std::move(on_saved)) {
    add_button("Cancel", Gtk::RESPONSE_CANCEL);
    add_button("Save", Gtk::RESPONSE_OK);
    set_default_response(Gtk::RESPONSE_OK);
    set_default_size(520, 420);

    list_.set_selection_mode(Gtk::SELECTION_SINGLE);
    auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
    scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scroll->add(list_);

    add_button_.set_label("Add");
    edit_button_.set_label("Edit");
    delete_button_.set_label("Delete");
    import_button_.set_label("Import");
    export_button_.set_label("Export");
    add_button_.signal_clicked().connect(sigc::mem_fun(*this, &WorldInfoDialog::on_add_entry));
    edit_button_.signal_clicked().connect(sigc::mem_fun(*this, &WorldInfoDialog::on_edit));
    delete_button_.signal_clicked().connect(sigc::mem_fun(*this, &WorldInfoDialog::on_delete));
    import_button_.signal_clicked().connect(sigc::mem_fun(*this, &WorldInfoDialog::on_import));
    export_button_.signal_clicked().connect(sigc::mem_fun(*this, &WorldInfoDialog::on_export));

    auto* buttons = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
    buttons->pack_start(add_button_, false, false, 0);
    buttons->pack_start(edit_button_, false, false, 0);
    buttons->pack_start(delete_button_, false, false, 0);
    buttons->pack_start(import_button_, false, false, 0);
    buttons->pack_start(export_button_, false, false, 0);

    auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
    box->pack_start(*scroll, true, true, 0);
    box->pack_start(*buttons, false, false, 0);
    box->set_margin_top(12);
    box->set_margin_bottom(12);
    box->set_margin_left(12);
    box->set_margin_right(12);
    get_content_area()->pack_start(*box, true, true, 0);

    try {
        load();
    } catch (const std::exception& error) {
        load_error_ = error.what();
        st::log_write(st::LogLevel::Error, std::string("world info load failed: ") + error.what());
    }
    refresh_list();
    show_all_children();
    signal_response().connect(sigc::mem_fun(*this, &WorldInfoDialog::on_response));
}

std::filesystem::path WorldInfoDialog::book_path() const {
    return safe_join(paths_.world_info_dir,
                     sanitize_filename(settings_.world_info_book, "lorebook") + ".json");
}

void WorldInfoDialog::load() {
    load_error_.clear();
    book_ = st::WorldInfoBook{};
    const std::filesystem::path path = book_path();
    if (!file_exists(path)) {
        refresh_list();
        return;
    }
    const std::string text = read_file_limited(path, 16u * 1024u * 1024u, "world info");
    book_ = st::WorldInfoBook::from_json(parse_json(text, "world info"));
    book_.name = sanitize_filename(settings_.world_info_book, "lorebook");
    refresh_list();
}

void WorldInfoDialog::save() {
    if (!load_error_.empty()) {
        // Refuse to save over a file that failed to load: book_ is
        // default-empty, so writing would clobber the corrupt file.
        throw std::runtime_error(load_error_);
    }
    book_.name = sanitize_filename(settings_.world_info_book, "lorebook");
    const std::filesystem::path path = book_path();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        throw std::runtime_error("cannot create directory " + path.parent_path().string() + ": " +
                                 error.message());
    }
    write_file_atomic(path, dump_json(book_.to_json(), 2) + "\n");
    if (on_saved_) {
        on_saved_();
    }
}

void WorldInfoDialog::add_entry(const st::WorldInfoEntry& entry) {
    book_.entries.push_back(entry);
    refresh_list();
}

void WorldInfoDialog::refresh_list() {
    for (Gtk::Widget* child : list_.get_children()) {
        list_.remove(*child);
    }
    for (const st::WorldInfoEntry& entry : book_.entries) {
        auto* row = Gtk::manage(new Gtk::ListBoxRow());
        const std::string text = entry.key + " \xe2\x80\x94 " + entry.comment;
        row->add(*Gtk::manage(new Gtk::Label(text)));
        list_.append(*row);
    }
    list_.show_all();
}

namespace {

bool edit_entry_dialog(Gtk::Window& parent, st::WorldInfoEntry& entry) {
    Gtk::Dialog dialog("World Info entry", parent, true);
    dialog.set_default_size(520, 380);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Save", Gtk::RESPONSE_OK);
    dialog.set_default_response(Gtk::RESPONSE_OK);

    auto* grid = Gtk::manage(new Gtk::Grid());
    grid->set_row_spacing(8);
    grid->set_column_spacing(12);
    grid->set_margin_top(12);
    grid->set_margin_bottom(12);
    grid->set_margin_left(12);
    grid->set_margin_right(12);

    auto* key = Gtk::manage(new Gtk::Entry());
    key->set_text(entry.key);
    auto* comment = Gtk::manage(new Gtk::Entry());
    comment->set_text(entry.comment);
    auto* content = Gtk::manage(new Gtk::TextView());
    content->set_wrap_mode(Gtk::WRAP_WORD_CHAR);
    content->get_buffer()->set_text(entry.content);
    auto* content_scroll = Gtk::manage(new Gtk::ScrolledWindow());
    content_scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    content_scroll->set_size_request(-1, 120);
    content_scroll->add(*content);
    auto* enabled = Gtk::manage(new Gtk::CheckButton("Enabled"));
    enabled->set_active(entry.enabled);
    auto* position = Gtk::manage(new Gtk::SpinButton());
    position->set_range(-100.0, 100.0);
    position->set_increments(1.0, 1.0);
    position->set_digits(0);
    position->set_value(entry.position);
    auto* depth = Gtk::manage(new Gtk::SpinButton());
    depth->set_range(0.0, 100.0);
    depth->set_increments(1.0, 1.0);
    depth->set_digits(0);
    depth->set_value(entry.depth);

    int row = 0;
    grid->attach(*wi_label("Key"), 0, row);
    grid->attach(*key, 1, row++);
    grid->attach(*wi_label("Comment"), 0, row);
    grid->attach(*comment, 1, row++);
    grid->attach(*wi_label("Content"), 0, row);
    grid->attach(*content_scroll, 1, row++);
    grid->attach(*wi_label("Enabled"), 0, row);
    grid->attach(*enabled, 1, row++);
    grid->attach(*wi_label("Position"), 0, row);
    grid->attach(*position, 1, row++);
    grid->attach(*wi_label("Depth"), 0, row);
    grid->attach(*depth, 1, row++);

    dialog.get_content_area()->pack_start(*grid, true, true, 0);
    dialog.show_all();
    if (dialog.run() != Gtk::RESPONSE_OK) {
        return false;
    }
    entry.key = key->get_text().raw();
    entry.comment = comment->get_text().raw();
    entry.content = content->get_buffer()->get_text().raw();
    entry.enabled = enabled->get_active();
    entry.position = position->get_value_as_int();
    entry.depth = depth->get_value_as_int();
    return true;
}

}  // namespace

void WorldInfoDialog::on_add_entry() {
    st::WorldInfoEntry entry;
    if (edit_entry_dialog(*this, entry) && !entry.key.empty() && !entry.content.empty()) {
        book_.entries.push_back(entry);
        refresh_list();
    }
}

void WorldInfoDialog::on_edit() {
    Gtk::ListBoxRow* selected = list_.get_selected_row();
    if (selected == nullptr) {
        return;
    }
    const int index = selected->get_index();
    if (index < 0 || static_cast<std::size_t>(index) >= book_.entries.size()) {
        return;
    }
    st::WorldInfoEntry entry = book_.entries[static_cast<std::size_t>(index)];
    if (edit_entry_dialog(*this, entry) && !entry.key.empty() && !entry.content.empty()) {
        book_.entries[static_cast<std::size_t>(index)] = entry;
        refresh_list();
    }
}

void WorldInfoDialog::on_delete() {
    Gtk::ListBoxRow* selected = list_.get_selected_row();
    if (selected == nullptr) {
        return;
    }
    const int index = selected->get_index();
    if (index < 0 || static_cast<std::size_t>(index) >= book_.entries.size()) {
        return;
    }
    book_.entries.erase(book_.entries.begin() + index);
    refresh_list();
}

void WorldInfoDialog::on_import() {
    Gtk::FileChooserDialog dialog(*this, "Import World Info", Gtk::FILE_CHOOSER_ACTION_OPEN);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Import", Gtk::RESPONSE_OK);
    dialog.set_default_response(Gtk::RESPONSE_OK);
    if (dialog.run() != Gtk::RESPONSE_OK) {
        return;
    }
    const std::filesystem::path path = dialog.get_file()->get_path();
    try {
        const std::string text = read_file_limited(path, 16u * 1024u * 1024u, "world info");
        st::WorldInfoBook imported =
            st::WorldInfoBook::from_json(parse_json(text, "world info"));
        book_ = imported;
        load_error_.clear();
        refresh_list();
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error,
                      std::string("world info import failed: ") + error.what());
        load_error_ = error.what();
        Gtk::MessageDialog message(*this, "World Info error", false,
                                   Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        message.set_secondary_text(error.what());
        message.run();
    }
}

void WorldInfoDialog::on_export() {
    Gtk::FileChooserDialog dialog(*this, "Export World Info", Gtk::FILE_CHOOSER_ACTION_SAVE);
    dialog.set_do_overwrite_confirmation(false);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Export", Gtk::RESPONSE_OK);
    dialog.set_default_response(Gtk::RESPONSE_OK);
    dialog.set_current_name(sanitize_filename(settings_.world_info_book, "lorebook") + ".json");
    if (dialog.run() != Gtk::RESPONSE_OK) {
        return;
    }
    const std::filesystem::path path = dialog.get_file()->get_path();
    if (file_exists(path)) {
        Gtk::MessageDialog confirm("Overwrite " + path.filename().string() + "?",
                                   false,
                                   Gtk::MESSAGE_QUESTION,
                                   Gtk::BUTTONS_OK_CANCEL,
                                   true);
        confirm.set_transient_for(*this);
        confirm.set_secondary_text("The existing file will be replaced.");
        if (confirm.run() != Gtk::RESPONSE_OK) {
            return;
        }
    }
    try {
        write_file_atomic(path, dump_json(book_.to_json(), 2) + "\n");
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error,
                      std::string("world info export failed: ") + error.what());
        Gtk::MessageDialog message(*this, "World Info error", false,
                                   Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        message.set_secondary_text(error.what());
        message.run();
    }
}

void WorldInfoDialog::on_response(int response) {
    if (response != Gtk::RESPONSE_OK) {
        hide();
        return;
    }
    try {
        save();
        hide();
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error,
                      std::string("world info save failed: ") + error.what());
        load_error_ = error.what();
        Gtk::MessageDialog message(*this, "World Info error", false,
                                   Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        message.set_secondary_text(error.what());
        message.run();
    }
}

PersonaManagerDialog::PersonaManagerDialog(Gtk::Window& parent,
                                           const st::Paths& paths,
                                           st::Settings& settings,
                                           std::function<void()> on_saved)
    : Gtk::Dialog("Personas", parent, true),
      paths_(paths),
      settings_(settings),
      on_saved_(std::move(on_saved)) {
    add_button("Cancel", Gtk::RESPONSE_CANCEL);
    add_button("Save", Gtk::RESPONSE_OK);
    set_default_response(Gtk::RESPONSE_OK);
    set_default_size(520, 420);

    list_.set_selection_mode(Gtk::SELECTION_SINGLE);
    auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
    scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scroll->add(list_);

    add_button_.set_label("Add");
    delete_button_.set_label("Delete");
    add_button_.signal_clicked().connect(sigc::mem_fun(*this, &PersonaManagerDialog::on_add_persona));
    delete_button_.signal_clicked().connect(sigc::mem_fun(*this, &PersonaManagerDialog::on_delete));

    auto* buttons = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
    buttons->pack_start(add_button_, false, false, 0);
    buttons->pack_start(delete_button_, false, false, 0);

    auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
    box->pack_start(*scroll, true, true, 0);
    box->pack_start(*buttons, false, false, 0);
    box->set_margin_top(12);
    box->set_margin_bottom(12);
    box->set_margin_left(12);
    box->set_margin_right(12);
    get_content_area()->pack_start(*box, true, true, 0);

    try {
        load();
    } catch (const std::exception& error) {
        load_error_ = error.what();
        st::log_write(st::LogLevel::Error, std::string("persona load failed: ") + error.what());
    }
    refresh_list();
    show_all_children();
    signal_response().connect(sigc::mem_fun(*this, &PersonaManagerDialog::on_response));
}

std::filesystem::path PersonaManagerDialog::store_path() const {
    return paths_.data_dir / "personas.json";
}

void PersonaManagerDialog::load() {
    load_error_.clear();
    store_ = st::PersonaStore::load(store_path());
    refresh_list();
}

void PersonaManagerDialog::save() {
    if (!load_error_.empty()) {
        // Refuse to save over a file that failed to load: store_ is
        // default-empty, so writing would clobber the corrupt file.
        throw std::runtime_error(load_error_);
    }
    // Keep the dual selection sources in sync: whichever persona is active
    // in the store becomes Settings::persona_name.
    settings_.persona_name = store_.active_name;
    settings_.validate();
    store_.save(store_path());
    settings_.save(paths_.settings_file);
    if (on_saved_) {
        on_saved_();
    }
}

void PersonaManagerDialog::add_persona(const st::Persona& persona) {
    if (persona.name.empty()) {
        return;
    }
    bool found = false;
    for (st::Persona& existing : store_.personas) {
        if (existing.name == persona.name) {
            existing = persona;
            found = true;
            break;
        }
    }
    if (!found) {
        store_.personas.push_back(persona);
    }
    store_.active_name = persona.name;
    refresh_list();
}

void PersonaManagerDialog::refresh_list() {
    for (Gtk::Widget* child : list_.get_children()) {
        list_.remove(*child);
    }
    for (const st::Persona& persona : store_.personas) {
        auto* row = Gtk::manage(new Gtk::ListBoxRow());
        std::string text = persona.name;
        if (!persona.description.empty()) {
            text += " \xe2\x80\x94 " + persona.description;
        }
        if (persona.name == store_.active_name) {
            text += " (active)";
        }
        row->add(*Gtk::manage(new Gtk::Label(text)));
        list_.append(*row);
    }
    list_.show_all();
}

namespace {

bool edit_persona_dialog(Gtk::Window& parent, st::Persona& persona) {
    Gtk::Dialog dialog("Persona", parent, true);
    dialog.set_default_size(520, 320);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Save", Gtk::RESPONSE_OK);
    dialog.set_default_response(Gtk::RESPONSE_OK);

    auto* grid = Gtk::manage(new Gtk::Grid());
    grid->set_row_spacing(8);
    grid->set_column_spacing(12);
    grid->set_margin_top(12);
    grid->set_margin_bottom(12);
    grid->set_margin_left(12);
    grid->set_margin_right(12);

    auto* name = Gtk::manage(new Gtk::Entry());
    name->set_text(persona.name);
    auto* description = Gtk::manage(new Gtk::TextView());
    description->set_wrap_mode(Gtk::WRAP_WORD_CHAR);
    description->get_buffer()->set_text(persona.description);
    auto* description_scroll = Gtk::manage(new Gtk::ScrolledWindow());
    description_scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    description_scroll->set_size_request(-1, 80);
    description_scroll->add(*description);

    int row = 0;
    grid->attach(*wi_label("Name"), 0, row);
    grid->attach(*name, 1, row++);
    grid->attach(*wi_label("Description"), 0, row);
    grid->attach(*description_scroll, 1, row++);

    dialog.get_content_area()->pack_start(*grid, true, true, 0);
    dialog.show_all();
    if (dialog.run() != Gtk::RESPONSE_OK) {
        return false;
    }
    persona.name = name->get_text().raw();
    persona.description = description->get_buffer()->get_text().raw();
    return true;
}

}  // namespace

void PersonaManagerDialog::on_add_persona() {
    st::Persona persona;
    if (edit_persona_dialog(*this, persona) && !persona.name.empty()) {
        add_persona(persona);
    }
}

void PersonaManagerDialog::on_delete() {
    Gtk::ListBoxRow* selected = list_.get_selected_row();
    if (selected == nullptr) {
        return;
    }
    const int index = selected->get_index();
    if (index < 0 || static_cast<std::size_t>(index) >= store_.personas.size()) {
        return;
    }
    const std::string removed = store_.personas[static_cast<std::size_t>(index)].name;
    store_.personas.erase(store_.personas.begin() + index);
    if (removed == store_.active_name) {
        store_.active_name = "";
    }
    refresh_list();
}

void PersonaManagerDialog::on_response(int response) {
    if (response != Gtk::RESPONSE_OK) {
        hide();
        return;
    }
    try {
        save();
        hide();
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error,
                      std::string("persona save failed: ") + error.what());
        load_error_ = error.what();
        Gtk::MessageDialog message(*this, "Persona error", false,
                                   Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        message.set_secondary_text(error.what());
        message.run();
    }
}

PromptManagerDialog::PromptManagerDialog(Gtk::Window& parent,
                                         const st::Paths& paths,
                                         st::Settings& settings,
                                         std::function<void()> on_saved)
    : Gtk::Dialog("Prompt manager", parent, true),
      paths_(paths),
      settings_(settings),
      on_saved_(std::move(on_saved)) {
    add_button("Cancel", Gtk::RESPONSE_CANCEL);
    add_button("Save", Gtk::RESPONSE_OK);
    set_default_response(Gtk::RESPONSE_OK);
    set_default_size(620, 480);

    preset_name_.clear();
    preset_name_entry_.set_text(sanitize_filename(settings_.active_preset, "default"));

    list_.set_selection_mode(Gtk::SELECTION_SINGLE);
    auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
    scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scroll->add(list_);

    import_button_.set_label("Import");
    export_button_.set_label("Export");
    up_button_.set_label("Up");
    down_button_.set_label("Down");
    import_button_.signal_clicked().connect(sigc::mem_fun(*this, &PromptManagerDialog::on_import));
    export_button_.signal_clicked().connect(sigc::mem_fun(*this, &PromptManagerDialog::on_export));
    up_button_.signal_clicked().connect([this]() {
        Gtk::ListBoxRow* selected = list_.get_selected_row();
        if (selected != nullptr) {
            on_move_up(static_cast<std::size_t>(selected->get_index()));
        }
    });
    down_button_.signal_clicked().connect([this]() {
        Gtk::ListBoxRow* selected = list_.get_selected_row();
        if (selected != nullptr) {
            on_move_down(static_cast<std::size_t>(selected->get_index()));
        }
    });

    auto* buttons = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
    buttons->pack_start(up_button_, false, false, 0);
    buttons->pack_start(down_button_, false, false, 0);
    buttons->pack_start(import_button_, false, false, 0);
    buttons->pack_start(export_button_, false, false, 0);

    auto* name_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
    auto* name_label = Gtk::manage(new Gtk::Label("Preset name"));
    name_label->set_halign(Gtk::ALIGN_START);
    name_row->pack_start(*name_label, false, false, 0);
    name_row->pack_start(preset_name_entry_, true, true, 0);

    auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
    box->pack_start(*name_row, false, false, 0);
    box->pack_start(*scroll, true, true, 0);
    box->pack_start(*buttons, false, false, 0);
    box->set_margin_top(12);
    box->set_margin_bottom(12);
    box->set_margin_left(12);
    box->set_margin_right(12);
    get_content_area()->pack_start(*box, true, true, 0);

    try {
        load();
    } catch (const std::exception& error) {
        load_error_ = error.what();
        st::log_write(st::LogLevel::Error, std::string("preset load failed: ") + error.what());
    }
    refresh_list();
    show_all_children();
    signal_response().connect(sigc::mem_fun(*this, &PromptManagerDialog::on_response));
}

std::filesystem::path PromptManagerDialog::preset_path() const {
    return safe_join(paths_.presets_dir,
                     sanitize_filename(preset_name_, "default") + ".json");
}

void PromptManagerDialog::load() {
    load_error_.clear();
    preset_name_ = sanitize_filename(preset_name_entry_.get_text().raw(), "default");
    if (preset_name_.empty()) {
        preset_name_ = sanitize_filename(settings_.active_preset, "default");
    }
    preset_name_entry_.set_text(preset_name_);
    const std::filesystem::path path = preset_path();
    if (!file_exists(path)) {
        preset_ = st::PromptPreset{};
        preset_.name = preset_name_;
        if (preset_name_ == "default") {
            const st::PromptManager defaults = st::PromptManager::default_manager();
            preset_.sections = defaults.sections;
            preset_.sampling_overrides = st::Json::object();
        }
        refresh_list();
        return;
    }
    const std::string text = read_file_limited(path, 16u * 1024u * 1024u, "preset");
    preset_ = st::PromptPreset::from_json(parse_json(text, "preset"));
    preset_.name = preset_name_;
    refresh_list();
}

void PromptManagerDialog::save() {
    if (!load_error_.empty()) {
        // Refuse to save over a file that failed to load: preset_ is
        // default-empty, so writing would clobber the corrupt file.
        throw std::runtime_error(load_error_);
    }
    const std::string entered = preset_name_entry_.get_text().raw();
    if (!entered.empty()) {
        preset_name_ = sanitize_filename(entered, "default");
    }
    sync_checkboxes();
    for (std::size_t i = 0; i < preset_.sections.size(); ++i) {
        preset_.sections[i].order = static_cast<int>(i);
    }
    preset_.name = preset_name_;
    const std::string serialized = dump_json(preset_.to_json(), 2) + "\n";
    if (serialized.size() > 16u * 1024u * 1024u) {
        throw std::runtime_error("preset exceeds 16 MiB cap");
    }
    const std::filesystem::path path = preset_path();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        throw std::runtime_error("cannot create directory " + path.parent_path().string() + ": " +
                                 error.message());
    }
    // sampling_overrides round-trips verbatim through to_json()/from_json().
    write_file_atomic(path, serialized);
    settings_.active_preset = preset_name_;
    settings_.validate();
    settings_.save(paths_.settings_file);
    if (on_saved_) {
        on_saved_();
    }
}

void PromptManagerDialog::add_section(const st::PromptSection& section) {
    if (section.id.empty()) {
        return;
    }
    preset_.sections.push_back(section);
    for (std::size_t i = 0; i < preset_.sections.size(); ++i) {
        preset_.sections[i].order = static_cast<int>(i);
    }
    refresh_list();
}

void PromptManagerDialog::set_preset_name(const std::string& name) {
    preset_name_ = sanitize_filename(name, "default");
    preset_.name = preset_name_;
    preset_name_entry_.set_text(preset_name_);
}

void PromptManagerDialog::sync_checkboxes() {
    const std::size_t count = std::min(check_buttons_.size(), preset_.sections.size());
    for (std::size_t i = 0; i < count; ++i) {
        preset_.sections[i].enabled = check_buttons_[i]->get_active();
    }
}

void PromptManagerDialog::refresh_list() {
    for (Gtk::Widget* child : list_.get_children()) {
        list_.remove(*child);
    }
    check_buttons_.clear();
    for (std::size_t i = 0; i < preset_.sections.size(); ++i) {
        const st::PromptSection& section = preset_.sections[i];
        auto* row = Gtk::manage(new Gtk::ListBoxRow());
        auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
        auto* check = Gtk::manage(new Gtk::CheckButton(
            section.title.empty() ? section.id : section.title));
        check->set_active(section.enabled);
        check_buttons_.push_back(check);
        auto* id_label = Gtk::manage(new Gtk::Label(section.id));
        id_label->set_halign(Gtk::ALIGN_START);
        auto* up = Gtk::manage(new Gtk::Button("Up"));
        auto* down = Gtk::manage(new Gtk::Button("Down"));
        up->signal_clicked().connect(
            [this, i]() { on_move_up(i); });
        down->signal_clicked().connect(
            [this, i]() { on_move_down(i); });
        box->pack_start(*check, false, false, 0);
        box->pack_start(*id_label, true, true, 0);
        box->pack_start(*up, false, false, 0);
        box->pack_start(*down, false, false, 0);
        row->add(*box);
        list_.append(*row);
    }
    list_.show_all();
}

void PromptManagerDialog::on_move_up(std::size_t index) {
    if (index == 0 || index >= preset_.sections.size()) {
        return;
    }
    sync_checkboxes();
    st::PromptManager manager;
    manager.sections = preset_.sections;
    manager.move(index, index - 1);
    preset_.sections = manager.sections;
    refresh_list();
}

void PromptManagerDialog::on_move_down(std::size_t index) {
    if (index + 1 >= preset_.sections.size()) {
        return;
    }
    sync_checkboxes();
    st::PromptManager manager;
    manager.sections = preset_.sections;
    manager.move(index, index + 1);
    preset_.sections = manager.sections;
    refresh_list();
}

void PromptManagerDialog::on_import() {
    Gtk::FileChooserDialog dialog(*this, "Import preset", Gtk::FILE_CHOOSER_ACTION_OPEN);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Import", Gtk::RESPONSE_OK);
    dialog.set_default_response(Gtk::RESPONSE_OK);
    if (dialog.run() != Gtk::RESPONSE_OK) {
        return;
    }
    const std::filesystem::path path = dialog.get_file()->get_path();
    try {
        const std::string text = read_file_limited(path, 16u * 1024u * 1024u, "preset");
        preset_ = st::PromptPreset::from_json(parse_json(text, "preset"));
        std::string name = preset_.name;
        if (name.empty()) {
            name = path.stem().string();
        }
        set_preset_name(name);
        load_error_.clear();
        refresh_list();
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error,
                      std::string("preset import failed: ") + error.what());
        load_error_ = error.what();
        Gtk::MessageDialog message(*this, "Preset error", false,
                                   Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        message.set_secondary_text(error.what());
        message.run();
    }
}

void PromptManagerDialog::on_export() {
    sync_checkboxes();
    preset_.name = preset_name_;
    Gtk::FileChooserDialog dialog(*this, "Export preset", Gtk::FILE_CHOOSER_ACTION_SAVE);
    dialog.set_do_overwrite_confirmation(false);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Export", Gtk::RESPONSE_OK);
    dialog.set_default_response(Gtk::RESPONSE_OK);
    dialog.set_current_name(sanitize_filename(preset_name_, "default") + ".json");
    if (dialog.run() != Gtk::RESPONSE_OK) {
        return;
    }
    const std::filesystem::path path = dialog.get_file()->get_path();
    if (file_exists(path)) {
        Gtk::MessageDialog confirm("Overwrite " + path.filename().string() + "?",
                                   false,
                                   Gtk::MESSAGE_QUESTION,
                                   Gtk::BUTTONS_OK_CANCEL,
                                   true);
        confirm.set_transient_for(*this);
        confirm.set_secondary_text("The existing file will be replaced.");
        if (confirm.run() != Gtk::RESPONSE_OK) {
            return;
        }
    }
    try {
        const std::string serialized = dump_json(preset_.to_json(), 2) + "\n";
        if (serialized.size() > 16u * 1024u * 1024u) {
            throw std::runtime_error("preset exceeds 16 MiB cap");
        }
        write_file_atomic(path, serialized);
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error,
                      std::string("preset export failed: ") + error.what());
        load_error_ = error.what();
        Gtk::MessageDialog message(*this, "Preset error", false,
                                   Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        message.set_secondary_text(error.what());
        message.run();
    }
}

void PromptManagerDialog::on_response(int response) {
    if (response != Gtk::RESPONSE_OK) {
        hide();
        return;
    }
    try {
        save();
        hide();
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error,
                      std::string("preset save failed: ") + error.what());
        load_error_ = error.what();
        Gtk::MessageDialog message(*this, "Preset error", false,
                                   Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        message.set_secondary_text(error.what());
        message.run();
    }
}

}  // namespace ui
}  // namespace st
