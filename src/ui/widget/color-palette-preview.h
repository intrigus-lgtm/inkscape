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

#ifndef SEEN_UI_WIDGET_COLOR_PALETTE_PREVIEW_H
#define SEEN_UI_WIDGET_COLOR_PALETTE_PREVIEW_H

#include <vector>
#include <gtkmm/drawingarea.h>

#include "ui/widget/palette_t.h"

namespace Inkscape::UI::Widget {

/// A Gtk::DrawingArea to preview color palette menu items by showing a small example of the colors
class ColorPalettePreview : public Gtk::DrawingArea
{
public:
    /// Construct with colors that we will preview.
    ColorPalettePreview(std::vector<rgb_t> colors);

private:
    std::vector<rgb_t> _colors;

    void draw_func(Cairo::RefPtr<Cairo::Context> const &cr, int, int);
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_UI_WIDGET_COLOR_PALETTE_PREVIEW_H

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
