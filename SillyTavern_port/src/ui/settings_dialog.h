#pragma once

#include "core/paths.h"
#include "core/settings.h"

#include <gtkmm.h>

#include <functional>

namespace st {
namespace ui {

class SettingsDialog : public Gtk::Dialog {
public:
    SettingsDialog(Gtk::Window& parent,
                   const st::Paths& paths,
                   st::Settings& settings,
                   std::function<void()> on_saved);

    Gtk::CheckButton& authors_note_enabled_widget() { return authors_note_enabled_; }
    Gtk::TextView& authors_note_widget() { return authors_note_; }
    Gtk::SpinButton& authors_note_depth_widget() { return authors_note_depth_; }
    Gtk::ComboBoxText& persona_widget() { return persona_combo_; }
    Gtk::TextView& persona_description_widget() { return persona_description_; }
    Gtk::CheckButton& instruct_enabled_widget() { return instruct_mode_; }
    Gtk::ComboBoxText& instruct_format_widget() { return instruct_format_; }
    Gtk::TextView& context_template_widget() { return context_template_; }

private:
    void load();
    st::Settings build_settings() const;
    void on_response(int response) override;

    const st::Paths& paths_;
    st::Settings& settings_;
    std::function<void()> on_saved_;

    Gtk::Grid grid_;
    Gtk::Entry base_url_;
    Gtk::Entry api_key_;
    Gtk::Entry model_;
    Gtk::SpinButton temperature_;
    Gtk::SpinButton top_p_;
    Gtk::SpinButton max_tokens_;
    Gtk::SpinButton context_size_;
    Gtk::Entry user_name_;
    Gtk::TextView system_prompt_;
    Gtk::ScrolledWindow prompt_scroll_;
    Gtk::CheckButton stream_;
    Gtk::SpinButton timeout_;
    Gtk::CheckButton authors_note_enabled_;
    Gtk::TextView authors_note_;
    Gtk::ScrolledWindow authors_note_scroll_;
    Gtk::SpinButton authors_note_depth_;
    Gtk::ComboBoxText persona_combo_{true};
    Gtk::TextView persona_description_;
    Gtk::ScrolledWindow persona_description_scroll_;
    Gtk::CheckButton instruct_mode_;
    Gtk::ComboBoxText instruct_format_;
    Gtk::TextView context_template_;
    Gtk::ScrolledWindow context_template_scroll_;
};

}  // namespace ui
}  // namespace st
