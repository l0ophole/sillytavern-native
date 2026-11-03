#include "ui/character_list.h"

#include "core/character_filter.h"
#include "core/log.h"
#include "ui/avatar.h"

#include <exception>
#include <string>
#include <utility>

namespace st {
namespace ui {

CharacterList::CharacterList(Gtk::Window& parent,
                             st::CharacterStore& store,
                             std::function<void(const CharacterEntry&)> on_selected,
                             std::function<void(const CharacterEntry*)> on_changed)
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL),
      parent_(parent),
      store_(store),
      on_selected_(std::move(on_selected)),
      on_changed_(std::move(on_changed)) {
    scan_dispatcher_.connect(sigc::mem_fun(*this, &CharacterList::on_scan_ready));

    header_.set_spacing(6);
    header_.set_margin_top(6);
    header_.set_margin_bottom(6);
    header_.set_margin_left(6);
    header_.set_margin_right(6);

    search_.set_placeholder_text("Search characters");
    search_.signal_changed().connect(
        sigc::mem_fun(*this, &CharacterList::apply_filter));
    import_button_.set_label("Import");
    import_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &CharacterList::on_import_clicked));
    delete_button_.set_label("Delete");
    delete_button_.set_sensitive(false);
    delete_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &CharacterList::on_delete_clicked));

    header_.pack_start(search_, Gtk::PACK_EXPAND_WIDGET);
    header_.pack_start(import_button_, Gtk::PACK_SHRINK);
    header_.pack_start(delete_button_, Gtk::PACK_SHRINK);

    list_.set_selection_mode(Gtk::SELECTION_SINGLE);
    list_.signal_row_selected().connect(
        sigc::mem_fun(*this, &CharacterList::on_row_selected));
    scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scroll_.add(list_);

    pack_start(header_, Gtk::PACK_SHRINK);
    pack_start(scroll_, Gtk::PACK_EXPAND_WIDGET);

    show_all_children();
}

CharacterList::~CharacterList() {
    ++scan_generation_;
    scanning_ = false;
    if (scan_worker_.joinable()) {
        scan_worker_.join();
    }
}

void CharacterList::refresh() {
    ++scan_generation_;
    if (scan_worker_.joinable()) {
        scan_worker_.join();
    }
    {
        std::lock_guard<std::mutex> lock(scan_mutex_);
        scan_ready_ = false;
        scan_entries_.clear();
        scan_error_.clear();
    }
    scanning_ = false;
    all_ = store_.list();
    apply_filter();
}

void CharacterList::refresh_async() {
    if (scan_worker_.joinable()) {
        // A scan is already in flight (or its dispatcher is queued); its result
        // is still current, so there is no need to start another one.
        return;
    }

    ++scan_generation_;
    const std::uint64_t generation = scan_generation_;
    {
        std::lock_guard<std::mutex> lock(scan_mutex_);
        scan_ready_ = false;
        scan_entries_.clear();
        scan_error_.clear();
    }
    scanning_ = true;

    try {
        scan_worker_ = std::thread([this, generation]() {
            scan_in_background(generation);
        });
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error,
                      std::string("character scan start failed: ") + error.what());
        scanning_ = false;
        refresh();
    }
}

void CharacterList::scan_in_background(std::uint64_t generation) {
    std::vector<CharacterEntry> entries;
    std::string error;
    try {
        entries = store_.list();
    } catch (const std::exception& failure) {
        error = failure.what();
    }

    {
        std::lock_guard<std::mutex> lock(scan_mutex_);
        scan_entries_ = std::move(entries);
        scan_error_ = std::move(error);
        scan_ready_generation_ = generation;
        scan_ready_ = true;
    }
    scan_dispatcher_.emit();
}

void CharacterList::on_scan_ready() {
    std::vector<CharacterEntry> entries;
    std::string error;
    {
        std::lock_guard<std::mutex> lock(scan_mutex_);
        if (!scan_ready_) {
            return;
        }
        if (scan_ready_generation_ != scan_generation_) {
            // A synchronous refresh superseded this scan; drop the stale result.
            scan_ready_ = false;
            scan_entries_.clear();
            scan_error_.clear();
            return;
        }
        entries = std::move(scan_entries_);
        scan_entries_.clear();
        error = std::move(scan_error_);
        scan_error_.clear();
        scan_ready_ = false;
    }

    if (scan_worker_.joinable()) {
        scan_worker_.join();
    }

    if (!error.empty()) {
        st::log_write(st::LogLevel::Error, std::string("character scan failed: ") + error);
    }

    all_ = std::move(entries);
    apply_filter();

    if (visible_.empty()) {
        if (on_changed_) {
            on_changed_(nullptr);
        }
    } else if (list_.get_selected_row() == nullptr) {
        select_first();
    }

    scanning_ = false;
}

void CharacterList::select_first() {
    if (list_.get_row_at_index(0) != nullptr) {
        list_.select_row(*list_.get_row_at_index(0));
    }
}

void CharacterList::select_id(const std::string& id) {
    for (std::size_t index = 0; index < visible_.size(); ++index) {
        if (visible_[index].id == id) {
            if (Gtk::ListBoxRow* row = list_.get_row_at_index(static_cast<int>(index))) {
                list_.select_row(*row);
            }
            return;
        }
    }
    select_first();
}

void CharacterList::set_search_text(const std::string& text) {
    search_.set_text(text);
}

void CharacterList::apply_filter() {
    visible_ = filter_characters(all_, search_.get_text());
    rebuild();
}

void CharacterList::rebuild() {
    delete_button_.set_sensitive(false);
    for (Gtk::Widget* child : list_.get_children()) {
        list_.remove(*child);
    }

    for (const CharacterEntry& entry : visible_) {
        auto* row = Gtk::manage(new Gtk::ListBoxRow());
        auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
        box->set_margin_top(6);
        box->set_margin_bottom(6);
        box->set_margin_left(6);
        box->set_margin_right(6);

        auto* image = Gtk::manage(new Gtk::Image());
        const Glib::RefPtr<Gdk::Pixbuf> avatar = load_avatar(store_.avatar_path(entry), 48);
        if (avatar) {
            image->set(avatar);
        } else {
            image->set_from_icon_name("avatar-default", Gtk::ICON_SIZE_DIALOG);
        }
        image->set_size_request(48, 48);

        auto* text = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2));
        auto* name = Gtk::manage(new Gtk::Label());
        name->set_markup("<b>" + Glib::Markup::escape_text(entry.card.name) + "</b>");
        name->set_halign(Gtk::ALIGN_START);
        auto* description = Gtk::manage(new Gtk::Label(entry.card.description));
        description->set_halign(Gtk::ALIGN_START);
        description->set_ellipsize(Pango::ELLIPSIZE_END);
        description->set_max_width_chars(28);
        text->pack_start(*name, Gtk::PACK_SHRINK);
        text->pack_start(*description, Gtk::PACK_SHRINK);

        box->pack_start(*image, Gtk::PACK_SHRINK);
        box->pack_start(*text, Gtk::PACK_EXPAND_WIDGET);
        row->add(*box);
        list_.append(*row);
    }

    list_.show_all_children();
}

void CharacterList::on_import_clicked() {
    Gtk::FileChooserDialog dialog(parent_, "Import character", Gtk::FILE_CHOOSER_ACTION_OPEN);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Import", Gtk::RESPONSE_OK);

    Glib::RefPtr<Gtk::FileFilter> filter = Gtk::FileFilter::create();
    filter->set_name("Character cards");
    filter->add_pattern("*.png");
    filter->add_pattern("*.json");
    dialog.add_filter(filter);

    if (dialog.run() != Gtk::RESPONSE_OK) {
        return;
    }

    try {
        const CharacterEntry imported = store_.import_file(dialog.get_filename());
        refresh();
        select_id(imported.id);
        if (on_changed_) {
            on_changed_(selected_entry());
        }
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error, std::string("character import failed: ") + error.what());
        Gtk::MessageDialog message(parent_, "Import failed", false, Gtk::MESSAGE_ERROR,
                                   Gtk::BUTTONS_OK, true);
        message.set_secondary_text(error.what());
        message.run();
    }
}

void CharacterList::on_delete_clicked() {
    Gtk::ListBoxRow* row = list_.get_selected_row();
    if (row == nullptr) {
        return;
    }
    const int index = row->get_index();
    if (index < 0 || static_cast<std::size_t>(index) >= visible_.size()) {
        return;
    }
    const CharacterEntry entry = visible_[static_cast<std::size_t>(index)];

    Gtk::MessageDialog confirm(parent_,
                               "Delete character?",
                               false,
                               Gtk::MESSAGE_QUESTION,
                               Gtk::BUTTONS_OK_CANCEL,
                               true);
    confirm.set_secondary_text("Delete " + entry.card.name +
                               "? Existing chats will remain on disk.");
    if (confirm.run() != Gtk::RESPONSE_OK) {
        return;
    }
    delete_selected();
}

bool CharacterList::delete_selected() {
    Gtk::ListBoxRow* row = list_.get_selected_row();
    if (row == nullptr) {
        return false;
    }
    const int index = row->get_index();
    if (index < 0 || static_cast<std::size_t>(index) >= visible_.size()) {
        return false;
    }
    const std::string id = visible_[static_cast<std::size_t>(index)].id;

    try {
        store_.remove(id);
        refresh();
        if (!visible_.empty()) {
            select_first();
        }
        if (on_changed_) {
            on_changed_(selected_entry());
        }
        return true;
    } catch (const std::exception& error) {
        st::log_write(st::LogLevel::Error, std::string("character delete failed: ") + error.what());
        Gtk::MessageDialog message(parent_, "Delete failed", false, Gtk::MESSAGE_ERROR,
                                   Gtk::BUTTONS_OK, true);
        message.set_secondary_text(error.what());
        message.run();
        return false;
    }
}

void CharacterList::on_row_selected(Gtk::ListBoxRow* row) {
    if (row == nullptr) {
        delete_button_.set_sensitive(false);
        return;
    }
    const int index = row->get_index();
    if (index < 0 || static_cast<std::size_t>(index) >= visible_.size()) {
        delete_button_.set_sensitive(false);
        return;
    }
    delete_button_.set_sensitive(true);
    if (on_selected_) {
        on_selected_(visible_[static_cast<std::size_t>(index)]);
    }
}

const CharacterEntry* CharacterList::selected_entry() const {
    const Gtk::ListBoxRow* row = list_.get_selected_row();
    if (row == nullptr) {
        return nullptr;
    }
    const int index = row->get_index();
    if (index < 0 || static_cast<std::size_t>(index) >= visible_.size()) {
        return nullptr;
    }
    return &visible_[static_cast<std::size_t>(index)];
}

}  // namespace ui
}  // namespace st
