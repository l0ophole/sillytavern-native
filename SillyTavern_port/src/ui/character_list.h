#pragma once

#include "core/character_store.h"

#include <gtkmm.h>
#include <glibmm/dispatcher.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace st {
namespace ui {

class CharacterList : public Gtk::Box {
public:
    CharacterList(Gtk::Window& parent,
                  st::CharacterStore& store,
                  std::function<void(const CharacterEntry&)> on_selected,
                  std::function<void(const CharacterEntry*)> on_changed = {});
    ~CharacterList() override;

    void refresh();
    void refresh_async();
    bool is_scanning() const { return scanning_.load(); }
    bool scan_failed() const { return scan_failed_; }
    const std::string& scan_error() const { return scan_error_text_; }
    void select_first();
    void select_id(const std::string& id);
    void set_search_text(const std::string& text);
    bool delete_selected();
    bool empty() const { return all_.empty(); }

    const std::vector<CharacterEntry>& visible_entries() const { return visible_; }

private:
    void scan_in_background(std::uint64_t generation);
    void on_scan_ready();
    void apply_filter();
    void rebuild();
    void on_import_clicked();
    void on_delete_clicked();
    void on_row_selected(Gtk::ListBoxRow* row);
    const CharacterEntry* selected_entry() const;

    Gtk::Window& parent_;
    st::CharacterStore& store_;
    std::function<void(const CharacterEntry&)> on_selected_;
    std::function<void(const CharacterEntry*)> on_changed_;

    std::vector<CharacterEntry> all_;
    std::vector<CharacterEntry> visible_;

    Glib::Dispatcher scan_dispatcher_;
    std::thread scan_worker_;
    std::mutex scan_mutex_;
    std::vector<CharacterEntry> scan_entries_;
    std::string scan_error_;
    bool scan_failed_ = false;
    std::string scan_error_text_;
    bool scan_ready_ = false;
    std::uint64_t scan_ready_generation_ = 0;
    std::atomic<bool> scanning_{false};
    std::uint64_t scan_generation_ = 0;

    Gtk::SearchEntry search_;
    Gtk::Button import_button_;
    Gtk::Button delete_button_;
    Gtk::ListBox list_;
    Gtk::ScrolledWindow scroll_;
    Gtk::Box header_;
};

}  // namespace ui
}  // namespace st
