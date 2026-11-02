#include "ui/avatar.h"

#include <algorithm>
#include <exception>

namespace st {
namespace ui {

Glib::RefPtr<Gdk::Pixbuf> load_avatar(const std::filesystem::path& path, int size) {
    if (path.empty() || size <= 0) {
        return {};
    }

    try {
        Glib::RefPtr<Gdk::Pixbuf> pixbuf = Gdk::Pixbuf::create_from_file(path.string());
        if (!pixbuf) {
            return {};
        }
        const int width = pixbuf->get_width();
        const int height = pixbuf->get_height();
        if (width <= 0 || height <= 0) {
            return {};
        }
        const double scale =
            std::min(static_cast<double>(size) / width, static_cast<double>(size) / height);
        const int scaled_width = std::max(1, static_cast<int>(width * scale));
        const int scaled_height = std::max(1, static_cast<int>(height * scale));
        return pixbuf->scale_simple(scaled_width, scaled_height, Gdk::INTERP_BILINEAR);
    } catch (const Glib::Error&) {
        return {};
    } catch (const std::exception&) {
        return {};
    }
}

}  // namespace ui
}  // namespace st
