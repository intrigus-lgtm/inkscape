// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A Gtk::DrawingArea to preview color palette menu items by showing a small example of the colors
 */
/*
 * Authors:
 *   Michael Kowalski
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2021 Michael Kowalski
 * Copyright (C) 2023 Daniel Boles
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "color-palette-preview.h"

#include <utility>
#include <cairomm/context.h>
#include <sigc++/functors/mem_fun.h>

namespace Inkscape::UI::Widget {

static constexpr int height = 2;
static constexpr int dx = 1; // width per color

ColorPalettePreview::ColorPalettePreview(std::vector<rgb_t> colors)
    : Glib::ObjectBase{"ColorPalettePreview"}
    , Gtk::DrawingArea{}
    , _colors{std::move(colors)}
{
    set_size_request(-1, height);
    set_draw_func(sigc::mem_fun(*this, &ColorPalettePreview::draw_func));
}

void ColorPalettePreview::draw_func(Cairo::RefPtr<Cairo::Context> const &cr, int width, int height)
{
    if (_colors.empty()) return;
    if (width <= 0) return;

    for (int i = 0, px = 0; i < width && px < width; ++i, px += dx) {
        int const index = i * _colors.size() / width;
        auto &color = _colors.at(index);
        cr->set_source_rgb(color.r, color.g, color.b);
        cr->rectangle(px, 0, dx, height);
        cr->fill();
    }
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
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:
