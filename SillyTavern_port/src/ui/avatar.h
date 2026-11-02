#pragma once

#include <gdkmm/pixbuf.h>

#include <filesystem>

namespace st {
namespace ui {

Glib::RefPtr<Gdk::Pixbuf> load_avatar(const std::filesystem::path& path, int size);

}  // namespace ui
}  // namespace st
