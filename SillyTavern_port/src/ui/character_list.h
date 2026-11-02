#pragma once

#include "core/character_store.h"

#include <gtkmm.h>

#include <functional>
#include <vector>

namespace st {
namespace ui {

class CharacterList : public Gtk::Box {
public:
    CharacterList(Gtk::Window& parent,
                  st::CharacterStore& store,
                  std::function<void(const CharacterEntry&)> on_selected,
                  std::function<void()> on_changed = {});

    void refresh();
    void select_first();
    void select_id(const std::string& id);
    bool delete_selected();
    bool empty() const { return all_.empty(); }

    const std::vector<CharacterEntry>& visible_entries() const { return visible_; }

private:
    void apply_filter();
    void rebuild();
    void on_import_clicked();
    void on_delete_clicked();
    void on_row_selected(Gtk::ListBoxRow* row);

    Gtk::Window& parent_;
    st::CharacterStore& store_;
    std::function<void(const CharacterEntry&)> on_selected_;
    std::function<void()> on_changed_;

    std::vector<CharacterEntry> all_;
    std::vector<CharacterEntry> visible_;

    Gtk::SearchEntry search_;
    Gtk::Button import_button_;
    Gtk::Button delete_button_;
    Gtk::ListBox list_;
    Gtk::ScrolledWindow scroll_;
    Gtk::Box header_;
};

}  // namespace ui
}  // namespace st
