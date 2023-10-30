// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Ralf Stephan <ralf@ark.in-berlin.de>
 *
 * Copyright (C) 2001-2005 Authors
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "ui/widget/color-preview.h"

#include <cairomm/context.h>
#include <sigc++/functors/mem_fun.h>
#include <gtkmm/drawingarea.h>

#include "display/cairo-utils.h"

namespace Inkscape::UI::Widget {

ColorPreview::ColorPreview(std::uint32_t const rgba)
    : _rgba{rgba}
{
    set_name("ColorPreview");
    set_draw_func(sigc::mem_fun(*this, &ColorPreview::draw_func));
}

void
ColorPreview::setRgba32(std::uint32_t const rgba)
{
    if (_rgba == rgba) return;

    _rgba = rgba;
    queue_draw();
}

void ColorPreview::draw_func(Cairo::RefPtr<Cairo::Context> const &cr,
                             int const widget_width, int const widget_height)
{
    auto const width = widget_width / 2.0, height = widget_height - 1.0;
    auto x = 0.0;
    auto y = 0.0;
    double radius = height / 7.5;
    double degrees = M_PI / 180.0;

    cairo_new_sub_path (cr->cobj());
    cairo_line_to(cr->cobj(), width, 0);
    cairo_line_to(cr->cobj(), width, height);
    cairo_arc (cr->cobj(), x + radius, y + height - radius, radius, 90 * degrees, 180 * degrees);
    cairo_arc (cr->cobj(), x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
    cairo_close_path (cr->cobj());

    /* Transparent area */
    cairo_pattern_t *checkers = ink_cairo_pattern_create_checkerboard();
    cairo_set_source(cr->cobj(), checkers);
    cr->fill_preserve();
    ink_cairo_set_source_rgba32(cr->cobj(), _rgba);
    cr->fill();
    cairo_pattern_destroy(checkers);

    /* Solid area */
    x = width;
    cairo_new_sub_path (cr->cobj());
    cairo_arc (cr->cobj(), x + width - radius, y + radius, radius, -90 * degrees, 0 * degrees);
    cairo_arc (cr->cobj(), x + width - radius, y + height - radius, radius, 0 * degrees, 90 * degrees);
    cairo_line_to(cr->cobj(), x, height);
    cairo_line_to(cr->cobj(), x, y);
    cairo_close_path (cr->cobj());
    ink_cairo_set_source_rgba32(cr->cobj(), _rgba | 0xff);
    cr->fill();
}

} // namespace Inkscape::UI::Widget

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
