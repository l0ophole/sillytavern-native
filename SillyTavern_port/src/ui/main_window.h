#pragma once

#include "core/character_store.h"
#include "core/chat_store.h"
#include "core/paths.h"
#include "core/persona.h"
#include "core/settings.h"
#include "core/world_info.h"
#include "ui/character_list.h"
#include "ui/chat_view.h"

#include <gtkmm.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace st {
namespace ui {

class WorldInfoDialog : public Gtk::Dialog {
public:
    WorldInfoDialog(Gtk::Window& parent,
                    const st::Paths& paths,
                    st::Settings& settings,
                    std::function<void()> on_saved);

    void add_entry(const st::WorldInfoEntry& entry);
    void save();
    void load();
    const st::WorldInfoBook& book() const { return book_; }
    const std::string& load_error() const { return load_error_; }
    std::filesystem::path book_path() const;

private:
    void refresh_list();
    void on_add_entry();
    void on_edit();
    void on_delete();
    void on_import();
    void on_export();
    void on_response(int response) override;

    const st::Paths& paths_;
    st::Settings& settings_;
    std::function<void()> on_saved_;
    st::WorldInfoBook book_;
    std::string load_error_;

    Gtk::ListBox list_;
    Gtk::Button add_button_;
    Gtk::Button edit_button_;
    Gtk::Button delete_button_;
    Gtk::Button import_button_;
    Gtk::Button export_button_;
};

class PersonaManagerDialog : public Gtk::Dialog {
public:
    PersonaManagerDialog(Gtk::Window& parent,
                         const st::Paths& paths,
                         st::Settings& settings,
                         std::function<void()> on_saved);

    void add_persona(const st::Persona& persona);
    void save();
    void load();
    const st::PersonaStore& store() const { return store_; }
    const std::string& load_error() const { return load_error_; }
    std::filesystem::path store_path() const;

private:
    void refresh_list();
    void on_add_persona();
    void on_delete();
    void on_response(int response) override;

    const st::Paths& paths_;
    st::Settings& settings_;
    std::function<void()> on_saved_;
    st::PersonaStore store_;
    std::string load_error_;

    Gtk::ListBox list_;
    Gtk::Button add_button_;
    Gtk::Button delete_button_;
};

class MainWindow : public Gtk::Window {
public:
    MainWindow(st::Paths paths,
               st::Settings& settings,
               st::CharacterStore& characters,
               st::ChatStore& chats);

    std::string create_backup();
    bool restore_backup(const std::string& id);
    bool backup_actions_enabled() const;

private:
    void refresh_backup_menu();
    void on_settings();
    void on_personas();
    void on_world_info();
    void on_character_selected(const st::CharacterEntry& entry);
    void on_backup();
    void on_restore();

    st::Paths paths_;
    st::Settings& settings_;
    st::CharacterStore& characters_;
    st::ChatStore& chats_;

    Gtk::HeaderBar header_;
    Gtk::Button settings_button_;
    Gtk::Button personas_button_;
    Gtk::Button world_info_button_;
    Gtk::MenuButton menu_button_;
    Gtk::Menu backup_menu_;
    Gtk::MenuItem backup_item_;
    Gtk::MenuItem restore_item_;
    Gtk::Paned paned_;
    std::unique_ptr<CharacterList> character_list_;
    std::unique_ptr<ChatView> chat_view_;
    Gtk::Label status_;
    Gtk::Box root_;
};

}  // namespace ui
}  // namespace st
