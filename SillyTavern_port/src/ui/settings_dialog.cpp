#include "ui/settings_dialog.h"

#include <exception>
#include <string>

namespace st {
namespace ui {
namespace {

Gtk::Label* make_label(const char* text) {
    auto* label = Gtk::manage(new Gtk::Label(text));
    label->set_halign(Gtk::ALIGN_START);
    return label;
}

}  // namespace

SettingsDialog::SettingsDialog(Gtk::Window& parent,
                               const st::Paths& paths,
                               st::Settings& settings,
                               std::function<void()> on_saved)
    : Gtk::Dialog("Settings", parent, true),
      paths_(paths),
      settings_(settings),
      on_saved_(std::move(on_saved)) {
    add_button("Cancel", Gtk::RESPONSE_CANCEL);
    add_button("Save", Gtk::RESPONSE_OK);
    set_default_response(Gtk::RESPONSE_OK);

    grid_.set_row_spacing(8);
    grid_.set_column_spacing(12);
    grid_.set_margin_top(12);
    grid_.set_margin_bottom(12);
    grid_.set_margin_left(12);
    grid_.set_margin_right(12);

    api_key_.set_visibility(false);

    temperature_.set_range(0.0, 2.0);
    temperature_.set_increments(0.05, 0.5);
    temperature_.set_digits(2);
    top_p_.set_range(0.0, 1.0);
    top_p_.set_increments(0.05, 0.5);
    top_p_.set_digits(2);
    max_tokens_.set_range(1.0, 1000000.0);
    max_tokens_.set_increments(16.0, 256.0);
    max_tokens_.set_digits(0);
    context_size_.set_range(256.0, 1000000.0);
    context_size_.set_increments(256.0, 4096.0);
    context_size_.set_digits(0);
    timeout_.set_range(5.0, 3600.0);
    timeout_.set_increments(5.0, 30.0);
    timeout_.set_digits(0);

    prompt_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    prompt_scroll_.set_size_request(-1, 120);
    prompt_scroll_.add(system_prompt_);

    int row = 0;
    grid_.attach(*make_label("API base URL"), 0, row);
    grid_.attach(base_url_, 1, row++);
    grid_.attach(*make_label("API key"), 0, row);
    grid_.attach(api_key_, 1, row++);
    grid_.attach(*make_label("Model"), 0, row);
    grid_.attach(model_, 1, row++);
    grid_.attach(*make_label("Temperature"), 0, row);
    grid_.attach(temperature_, 1, row++);
    grid_.attach(*make_label("Top P"), 0, row);
    grid_.attach(top_p_, 1, row++);
    grid_.attach(*make_label("Max tokens"), 0, row);
    grid_.attach(max_tokens_, 1, row++);
    grid_.attach(*make_label("Context size"), 0, row);
    grid_.attach(context_size_, 1, row++);
    grid_.attach(*make_label("User name"), 0, row);
    grid_.attach(user_name_, 1, row++);
    grid_.attach(*make_label("System prompt"), 0, row);
    grid_.attach(prompt_scroll_, 1, row++);
    grid_.attach(*make_label("Stream replies"), 0, row);
    grid_.attach(stream_, 1, row++);
    grid_.attach(*make_label("Request timeout (s)"), 0, row);
    grid_.attach(timeout_, 1, row++);

    get_content_area()->pack_start(grid_, true, true, 0);

    load();
    show_all_children();

    signal_response().connect(sigc::mem_fun(*this, &SettingsDialog::on_response));
}

void SettingsDialog::load() {
    base_url_.set_text(settings_.api_base_url);
    api_key_.set_text(settings_.api_key);
    model_.set_text(settings_.model);
    temperature_.set_value(settings_.temperature);
    top_p_.set_value(settings_.top_p);
    max_tokens_.set_value(settings_.max_tokens);
    context_size_.set_value(settings_.context_size);
    user_name_.set_text(settings_.user_name);
    system_prompt_.get_buffer()->set_text(settings_.system_prompt);
    stream_.set_active(settings_.stream);
    timeout_.set_value(settings_.request_timeout_seconds);
}

void SettingsDialog::store() {
    settings_.api_base_url = base_url_.get_text();
    settings_.api_key = api_key_.get_text();
    settings_.model = model_.get_text();
    settings_.temperature = temperature_.get_value();
    settings_.top_p = top_p_.get_value();
    settings_.max_tokens = max_tokens_.get_value_as_int();
    settings_.context_size = context_size_.get_value_as_int();
    settings_.user_name = user_name_.get_text();
    settings_.system_prompt = system_prompt_.get_buffer()->get_text();
    settings_.stream = stream_.get_active();
    settings_.request_timeout_seconds = timeout_.get_value_as_int();
    settings_.validate();
}

void SettingsDialog::on_response(int response) {
    if (response != Gtk::RESPONSE_OK) {
        hide();
        return;
    }

    store();
    try {
        settings_.save(paths_.settings_file);
        if (on_saved_) {
            on_saved_();
        }
        hide();
    } catch (const std::exception& error) {
        Gtk::MessageDialog message(*this, "Cannot save settings", false,
                                   Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        message.set_secondary_text(error.what());
        message.run();
    }
}

}  // namespace ui
}  // namespace st
