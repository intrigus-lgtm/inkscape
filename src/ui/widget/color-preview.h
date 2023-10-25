// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Ralf Stephan <ralf@ark.in-berlin.de>
 *
 * Copyright (C) 2001-2005 Authors
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_COLOR_PREVIEW_H
#define SEEN_COLOR_PREVIEW_H

#include <cstdint>
#include <cairomm/refptr.h>
#include <gtkmm/box.h>

namespace Cairo {
class Context;
} // namespace Cairo

namespace Gtk {
class DrawingArea;
} // namespace Gtk

namespace Inkscape::UI::Widget {

/**
 * A simple color preview widget, mainly used within a picker button.
 */
// Box because GTK3 does not bother applying CSS bits like min-width|height on DrawingArea
// TODO: GTK4: Revisit whether that is still the case; hopefully it isnʼt, then just be DrawingArea
class ColorPreview final : public Gtk::Box {
public:
    ColorPreview  (std::uint32_t rgba);
    void setRgba32(std::uint32_t rgba);

private:
    Gtk::DrawingArea * const _drawing_area;
    std::uint32_t _rgba;

    void on_drawing_area_draw(Cairo::RefPtr<Cairo::Context> const &cr, int, int);
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_COLOR_PREVIEW_H

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
