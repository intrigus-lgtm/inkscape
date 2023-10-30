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
#include <gtkmm/drawingarea.h>

namespace Cairo {
class Context;
} // namespace Cairo

namespace Inkscape::UI::Widget {

/**
 * A simple color preview widget, mainly used within a picker button.
 */
class ColorPreview final : public Gtk::DrawingArea {
public:
    ColorPreview  (std::uint32_t rgba);
    void setRgba32(std::uint32_t rgba);

private:
    std::uint32_t _rgba;

    void draw_func(Cairo::RefPtr<Cairo::Context> const &cr, int width, int height);
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
