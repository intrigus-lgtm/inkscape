// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Icon Loader
 *//*
 * Authors:
 * see git history
 * Jabiertxo Arraiza <jabier.arraiza@marker.es>
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_INK_ICON_LOADER_H
#define SEEN_INK_ICON_LOADER_H

#include <cstdint>
#include <utility>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/enums.h>

namespace Gtk {
class Image;
} // namespace Gtk

// N.B. These return unmanaged widgets, so callers must manage() or delete them!
Gtk::Image *sp_get_icon_image(Glib::ustring const &icon_name, int size);
Gtk::Image *sp_get_icon_image(Glib::ustring const &icon_name, Gtk::IconSize icon_size);
GtkWidget  *sp_get_icon_image(Glib::ustring const &icon_name, GtkIconSize icon_size);

namespace Inkscape::UI {

/// A pair containing an icon name & a CSS class to set an RGBA color
struct GetShapeIconResult final { Glib::ustring icon_name; Glib::ustring color_class; };
// Get the icon name and CSS class you need to use to show a specific shape icon
[[nodiscard]] GetShapeIconResult get_shape_icon(Glib::ustring const &shape_type,
                                                std::uint32_t rgba_color);
// Get a managed Gtk::Image with icon_name & CSS class for a specific shape icon
[[nodiscard]] Gtk::Image *get_shape_image(Glib::ustring const &shape_type,
                                          std::uint32_t rgba_color,
                                          Gtk::IconSize icon_size = Gtk::IconSize::INHERIT);

} // namespace Inkscape::UI

#endif // SEEN_INK_ICON_LOADER_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
