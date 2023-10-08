// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cursor utilities
 *
 * Copyright (C) 2020 Tavmjong Bah
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 *
 */

#ifndef INK_CURSOR_UTILITIES_H
#define INK_CURSOR_UTILITIES_H

#include <cstdint>
#include <string>
#include <glibmm/refptr.h>

namespace Gdk {
class Cursor;
} // namespace Gdk

namespace Gtk {
class Widget;
} // namespace Gtk

namespace Inkscape {

Glib::RefPtr<Gdk::Cursor> load_svg_cursor(Gtk::Widget &widget,
                                          std::string const &file_name,
                                          std::uint32_t fill = 0xffffffff,
                                          std::uint32_t stroke = 0x000000ff,
                                          double fill_opacity = 1.0,
                                          double stroke_opacity = 1.0);

void set_svg_cursor(Gtk::Widget &widget,
                    std::string const &file_name,
                    std::uint32_t fill = 0xffffffff,
                    std::uint32_t stroke = 0x000000ff,
                    double fill_opacity = 1.0,
                    double stroke_opacity = 1.0);

} // namespace Inkscape

#endif // INK_CURSOR_UTILITIES_H

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
