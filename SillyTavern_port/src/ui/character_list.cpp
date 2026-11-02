#include "ui/character_list.h"

#include "core/character_filter.h"
#include "ui/avatar.h"

#include <exception>
#include <string>

namespace st {
namespace ui {

CharacterList::CharacterList(Gtk::Window& parent,
                             st::CharacterStore& store,
                             std::function<void(const CharacterEntry&)> on_selected)
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL),
      parent_(parent),
      store_(store),
      on_selected_(std::move(on_selected)) {
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

    header_.pack_start(search_, Gtk::PACK_EXPAND_WIDGET);
    header_.pack_start(import_button_, Gtk::PACK_SHRINK);

    list_.set_selection_mode(Gtk::SELECTION_SINGLE);
    list_.signal_row_selected().connect(
        sigc::mem_fun(*this, &CharacterList::on_row_selected));
    scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scroll_.add(list_);

    pack_start(header_, Gtk::PACK_SHRINK);
    pack_start(scroll_, Gtk::PACK_EXPAND_WIDGET);

    show_all_children();
}

void CharacterList::refresh() {
    all_ = store_.list();
    apply_filter();
}

void CharacterList::select_first() {
    if (list_.get_row_at_index(0) != nullptr) {
        list_.select_row(*list_.get_row_at_index(0));
    }
}

void CharacterList::apply_filter() {
    visible_ = filter_characters(all_, search_.get_text());
    rebuild();
}

void CharacterList::rebuild() {
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
        store_.import_file(dialog.get_filename());
        refresh();
        select_first();
    } catch (const std::exception& error) {
        Gtk::MessageDialog message(parent_, "Import failed", false, Gtk::MESSAGE_ERROR,
                                   Gtk::BUTTONS_OK, true);
        message.set_secondary_text(error.what());
        message.run();
    }
}

void CharacterList::on_row_selected(Gtk::ListBoxRow* row) {
    if (row == nullptr) {
        return;
    }
    const int index = row->get_index();
    if (index < 0 || static_cast<std::size_t>(index) >= visible_.size()) {
        return;
    }
    if (on_selected_) {
        on_selected_(visible_[static_cast<std::size_t>(index)]);
    }
}

}  // namespace ui
}  // namespace st
