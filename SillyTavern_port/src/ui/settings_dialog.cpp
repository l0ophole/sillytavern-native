#include "ui/settings_dialog.h"

#include "core/atomic_file.h"
#include "core/json.h"
#include "core/log.h"
#include "core/names.h"
#include "core/persona.h"
#include "core/prompt_manager.h"

#include <algorithm>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace st {
namespace ui {
namespace {

Gtk::Label* make_label(const char* text) {
    auto* label = Gtk::manage(new Gtk::Label(text));
    label->set_halign(Gtk::ALIGN_START);
    return label;
}

std::string trim_text(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
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

    authors_note_enabled_.set_label("Enable author's note");
    authors_note_depth_.set_range(0.0, 100.0);
    authors_note_depth_.set_increments(1.0, 1.0);
    authors_note_depth_.set_digits(0);

    top_k_.set_range(0.0, 1000.0);
    top_k_.set_increments(1.0, 10.0);
    top_k_.set_digits(0);
    repetition_penalty_.set_range(0.5, 2.0);
    repetition_penalty_.set_increments(0.05, 0.5);
    repetition_penalty_.set_digits(2);
    presence_penalty_.set_range(-2.0, 2.0);
    presence_penalty_.set_increments(0.05, 0.5);
    presence_penalty_.set_digits(2);
    frequency_penalty_.set_range(-2.0, 2.0);
    frequency_penalty_.set_increments(0.05, 0.5);
    frequency_penalty_.set_digits(2);

    persona_combo_.set_entry_text_column(0);
    persona_combo_.append("Built-in");
    try {
        const st::PersonaStore store =
            st::PersonaStore::load(paths.data_dir / "personas.json");
        for (const st::Persona& persona : store.personas) {
            persona_combo_.append(persona.name);
        }
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error,
                      std::string("persona list load failed: ") + error.what());
    }

    instruct_mode_.set_label("Enable instruct mode");
    instruct_format_.append("plain");
    instruct_format_.append("chatml");
    instruct_format_.append("llama");

    preset_import_.set_label("Import preset");
    preset_export_.set_label("Export preset");
    preset_import_.signal_clicked().connect(sigc::mem_fun(*this, &SettingsDialog::on_preset_import));
    preset_export_.signal_clicked().connect(sigc::mem_fun(*this, &SettingsDialog::on_preset_export));
    refresh_presets();

    prompt_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    prompt_scroll_.set_size_request(-1, 120);
    prompt_scroll_.add(system_prompt_);

    authors_note_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    authors_note_scroll_.set_size_request(-1, 80);
    authors_note_scroll_.add(authors_note_);

    persona_description_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    persona_description_scroll_.set_size_request(-1, 80);
    persona_description_scroll_.add(persona_description_);

    context_template_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    context_template_scroll_.set_size_request(-1, 80);
    context_template_scroll_.add(context_template_);

    regex_rules_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    regex_rules_scroll_.set_size_request(-1, 80);
    regex_rules_scroll_.add(regex_rules_);

    variables_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    variables_scroll_.set_size_request(-1, 80);
    variables_scroll_.add(variables_);

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
    grid_.attach(*make_label("Author's note"), 0, row);
    grid_.attach(authors_note_enabled_, 1, row++);
    grid_.attach(*make_label("Note"), 0, row);
    grid_.attach(authors_note_scroll_, 1, row++);
    grid_.attach(*make_label("Note depth"), 0, row);
    grid_.attach(authors_note_depth_, 1, row++);
    grid_.attach(*make_label("Persona"), 0, row);
    grid_.attach(persona_combo_, 1, row++);
    grid_.attach(*make_label("Persona description"), 0, row);
    grid_.attach(persona_description_scroll_, 1, row++);
    grid_.attach(*make_label("Instruct mode"), 0, row);
    grid_.attach(instruct_mode_, 1, row++);
    grid_.attach(*make_label("Instruct format"), 0, row);
    grid_.attach(instruct_format_, 1, row++);
    grid_.attach(*make_label("Context template"), 0, row);
    grid_.attach(context_template_scroll_, 1, row++);
    grid_.attach(*make_label("Top K"), 0, row);
    grid_.attach(top_k_, 1, row++);
    grid_.attach(*make_label("Repetition penalty"), 0, row);
    grid_.attach(repetition_penalty_, 1, row++);
    grid_.attach(*make_label("Presence penalty"), 0, row);
    grid_.attach(presence_penalty_, 1, row++);
    grid_.attach(*make_label("Frequency penalty"), 0, row);
    grid_.attach(frequency_penalty_, 1, row++);
    grid_.attach(*make_label("Regex rules (JSON)"), 0, row);
    grid_.attach(regex_rules_scroll_, 1, row++);
    grid_.attach(*make_label("Variables (JSON)"), 0, row);
    grid_.attach(variables_scroll_, 1, row++);
    grid_.attach(*make_label("Preset"), 0, row);
    grid_.attach(preset_combo_, 1, row++);
    auto* preset_buttons = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
    preset_buttons->pack_start(preset_import_, false, false, 0);
    preset_buttons->pack_start(preset_export_, false, false, 0);
    grid_.attach(*make_label("Preset file"), 0, row);
    grid_.attach(*preset_buttons, 1, row++);

    get_content_area()->pack_start(grid_, true, true, 0);

    load();
    show_all_children();

    signal_response().connect(sigc::mem_fun(*this, &SettingsDialog::on_response));
}

void SettingsDialog::refresh_presets() {
    preset_combo_.remove_all();
    preset_combo_.append("default");
    std::vector<std::string> names;
    std::error_code error;
    if (std::filesystem::exists(paths_.presets_dir, error) && !error) {
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(paths_.presets_dir, error)) {
            if (error) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string stem = entry.path().stem().string();
            if (entry.path().extension() == ".json" && !stem.empty() && stem != "default") {
                names.push_back(stem);
            }
        }
    }
    if (error) {
        st::log_write(st::LogLevel::Error,
                      std::string("preset list load failed: ") + error.message());
    }
    std::sort(names.begin(), names.end());
    for (const std::string& name : names) {
        preset_combo_.append(name);
    }
}

void SettingsDialog::on_preset_import() {
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
        st::PromptPreset preset = st::PromptPreset::from_json(parse_json(text, "preset"));
        std::string name = preset.name;
        if (name.empty()) {
            name = path.stem().string();
        }
        name = sanitize_filename(name, "default");
        preset.name = name;
        const std::filesystem::path target =
            safe_join(paths_.presets_dir, name + ".json");
        if (file_exists(target)) {
            Gtk::MessageDialog confirm("Overwrite " + target.filename().string() + "?",
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
        const std::string serialized = dump_json(preset.to_json(), 2) + "\n";
        if (serialized.size() > 16u * 1024u * 1024u) {
            throw std::runtime_error("preset exceeds 16 MiB cap");
        }
        std::error_code error;
        std::filesystem::create_directories(target.parent_path(), error);
        if (error) {
            throw std::runtime_error("cannot create directory " + target.parent_path().string() +
                                     ": " + error.message());
        }
        write_file_atomic(target, serialized);
        refresh_presets();
        preset_combo_.set_active_text(name);
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error,
                      std::string("preset import failed: ") + error.what());
        Gtk::MessageDialog message(*this, "Preset error", false,
                                   Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        message.set_secondary_text(error.what());
        message.run();
    }
}

void SettingsDialog::on_preset_export() {
    const std::string current =
        sanitize_filename(preset_combo_.get_active_text().raw(), "default");
    st::PromptPreset preset;
    preset.name = current;
    const std::filesystem::path source =
        safe_join(paths_.presets_dir, current + ".json");
    if (!file_exists(source)) {
        const st::PromptManager defaults = st::PromptManager::default_manager();
        preset.sections = defaults.sections;
    } else {
        try {
            const std::string text = read_file_limited(
                safe_join(paths_.presets_dir, current + ".json"), 16u * 1024u * 1024u, "preset");
            preset = st::PromptPreset::from_json(parse_json(text, "preset"));
            preset.name = current;
        } catch (const std::exception& error) {
            st::log_write(st::LogLevel::Error,
                          std::string("preset export failed: ") + error.what());
            Gtk::MessageDialog message(*this, "Preset error", false,
                                       Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
            message.set_secondary_text(error.what());
            message.run();
            return;
        }
    }
    Gtk::FileChooserDialog dialog(*this, "Export preset", Gtk::FILE_CHOOSER_ACTION_SAVE);
    dialog.set_do_overwrite_confirmation(false);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Export", Gtk::RESPONSE_OK);
    dialog.set_default_response(Gtk::RESPONSE_OK);
    dialog.set_current_name(current + ".json");
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
        const std::string serialized = dump_json(preset.to_json(), 2) + "\n";
        if (serialized.size() > 16u * 1024u * 1024u) {
            throw std::runtime_error("preset exceeds 16 MiB cap");
        }
        write_file_atomic(path, serialized);
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error,
                      std::string("preset export failed: ") + error.what());
        Gtk::MessageDialog message(*this, "Preset error", false,
                                   Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        message.set_secondary_text(error.what());
        message.run();
    }
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
    authors_note_enabled_.set_active(settings_.authors_note_enabled);
    authors_note_.get_buffer()->set_text(settings_.authors_note);
    authors_note_depth_.set_value(settings_.authors_note_depth);

    std::string description;
    const std::string persona_name = settings_.persona_name;
    if (!persona_name.empty()) {
        persona_combo_.set_active_text(persona_name);
        try {
            const st::PersonaStore store =
                st::PersonaStore::load(paths_.data_dir / "personas.json");
            for (const st::Persona& persona : store.personas) {
                if (persona.name == persona_name) {
                    description = persona.description;
                    break;
                }
            }
        } catch (const std::exception&) {
        }
    } else {
        persona_combo_.set_active_text("Built-in");
    }
    persona_description_.get_buffer()->set_text(description);
    instruct_mode_.set_active(settings_.instruct_mode);
    instruct_format_.set_active_text(settings_.instruct_format);
    context_template_.get_buffer()->set_text(settings_.context_template);
    top_k_.set_value(settings_.top_k);
    repetition_penalty_.set_value(settings_.repetition_penalty);
    presence_penalty_.set_value(settings_.presence_penalty);
    frequency_penalty_.set_value(settings_.frequency_penalty);
    regex_rules_.get_buffer()->set_text(
        dump_json(settings_.regex_rules.is_array() ? settings_.regex_rules : st::Json::array(), 2));
    {
        st::Json variables_json = st::Json::object();
        for (const auto& [key, entry] : settings_.variables) {
            variables_json[key] = entry;
        }
        variables_.get_buffer()->set_text(dump_json(variables_json, 2));
    }
    refresh_presets();
    preset_combo_.set_active_text(
        settings_.active_preset.empty() ? "default" : settings_.active_preset);
}

st::Settings SettingsDialog::build_settings() const {
    st::Settings candidate = settings_;
    candidate.api_base_url = base_url_.get_text();
    candidate.api_key = api_key_.get_text();
    candidate.model = model_.get_text();
    candidate.temperature = temperature_.get_value();
    candidate.top_p = top_p_.get_value();
    candidate.max_tokens = max_tokens_.get_value_as_int();
    candidate.context_size = context_size_.get_value_as_int();
    candidate.user_name = user_name_.get_text();
    candidate.system_prompt = system_prompt_.get_buffer()->get_text();
    candidate.stream = stream_.get_active();
    candidate.request_timeout_seconds = timeout_.get_value_as_int();
    candidate.authors_note_enabled = authors_note_enabled_.get_active();
    candidate.authors_note = authors_note_.get_buffer()->get_text();
    candidate.authors_note_depth = authors_note_depth_.get_value_as_int();
    candidate.top_k = top_k_.get_value_as_int();
    candidate.repetition_penalty = repetition_penalty_.get_value();
    candidate.presence_penalty = presence_penalty_.get_value();
    candidate.frequency_penalty = frequency_penalty_.get_value();

    // Parse the JSON fields before any side effects (e.g. the persona
    // store save below) so malformed input aborts the whole save and
    // leaves the live Settings untouched. Empty text means the empty
    // default, not an error.
    {
        const std::string regex_text = regex_rules_.get_buffer()->get_text();
        if (trim_text(regex_text).empty()) {
            candidate.regex_rules = st::Json::array();
        } else {
            st::Json parsed = st::parse_json(regex_text, "regex_rules");
            if (!parsed.is_array()) {
                throw std::runtime_error("regex_rules: root value must be an array");
            }
            candidate.regex_rules = std::move(parsed);
        }
    }
    {
        const std::string variables_text = variables_.get_buffer()->get_text();
        candidate.variables.clear();
        if (!trim_text(variables_text).empty()) {
            const st::Json parsed = st::parse_json(variables_text, "variables");
            if (!parsed.is_object()) {
                throw std::runtime_error("variables: root value must be an object");
            }
            for (const auto& [key, entry] : parsed.items()) {
                if (entry.is_string()) {
                    candidate.variables[key] = entry.get<std::string>();
                }
            }
        }
    }

    // Keep Settings::persona_name and PersonaStore::active_name in sync so
    // the two dual sources never leave divergent selections. A corrupt
    // personas.json is left untouched instead of being silently overwritten.
    const std::string persona_text = persona_combo_.get_active_text();
    st::PersonaStore store;
    bool store_loaded = true;
    try {
        store = st::PersonaStore::load(paths_.data_dir / "personas.json");
    } catch (const std::exception& error) {
        store_loaded = false;
        st::log_write(st::LogLevel::Error,
                      std::string("persona store load failed: ") + error.what());
    }
    if (store_loaded) {
        if (persona_text.empty() ||
            persona_text == "Built-in") {
            candidate.persona_name = "";
            store.active_name = "";
        } else {
            candidate.persona_name = persona_text;
            store.active_name = persona_text;
            bool found = false;
            for (st::Persona& persona : store.personas) {
                if (persona.name == persona_text) {
                    persona.description = persona_description_.get_buffer()->get_text();
                    found = true;
                    break;
                }
            }
            if (!found) {
                st::Persona fresh;
                fresh.name = persona_text;
                fresh.description = persona_description_.get_buffer()->get_text();
                store.personas.push_back(std::move(fresh));
            }
        }
        store.save(paths_.data_dir / "personas.json");
    } else {
        candidate.persona_name = persona_text == "Built-in" ? "" : persona_text;
    }

    candidate.instruct_mode = instruct_mode_.get_active();
    candidate.instruct_format = instruct_format_.get_active_text();
    if (candidate.instruct_format.empty()) {
        candidate.instruct_format = "plain";
    }
    candidate.context_template = context_template_.get_buffer()->get_text();
    const std::string preset_text = preset_combo_.get_active_text();
    candidate.active_preset = preset_text.empty() ? "default" : preset_text;
    candidate.validate();
    return candidate;
}

void SettingsDialog::on_response(int response) {
    if (response != Gtk::RESPONSE_OK) {
        hide();
        return;
    }

    try {
        st::Settings candidate = build_settings();
        candidate.save(paths_.settings_file);
        settings_ = std::move(candidate);
        st::log_set_redactions({settings_.api_key});
        if (on_saved_) {
            on_saved_();
        }
        hide();
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error, std::string("settings save failed: ") + error.what());
        Gtk::MessageDialog message(*this, "Settings error", false,
                                   Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        message.set_secondary_text(
            st::redact_secrets(error.what(), {settings_.api_key, api_key_.get_text()}));
        message.run();
    }
}

}  // namespace ui
}  // namespace st
