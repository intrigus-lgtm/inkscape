// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Utility functions for generating export previews.
 */
/* Authors:
 *   Anshudhar Kumar Singh <anshudhar2001@gmail.com>
 *   Martin Owens <doctormo@gmail.com>
 *
 * Copyright (C) 2021 Anshudhar Kumar Singh
 *               2021 Martin Owens
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "preview.h"

#include "display/cairo-utils.h"
#include "display/drawing-context.h"

namespace Inkscape {
namespace UI {
namespace Preview {

Cairo::RefPtr<Cairo::ImageSurface>
render_preview(SPDocument *doc, std::shared_ptr<Inkscape::Drawing> drawing, uint32_t bg,
               Inkscape::DrawingItem *item, unsigned width_in, unsigned height_in, Geom::Rect const &dboxIn)
{
    if (!drawing->root())
        return {};

    // Calculate a scaling factor for the requested bounding box.
    double sf = 1.0;
    Geom::IntRect ibox = dboxIn.roundOutwards();
    if (ibox.width() != width_in || ibox.height() != height_in) {
        sf = std::min((double)width_in / dboxIn.width(),
                      (double)height_in / dboxIn.height());
        auto scaled_box = dboxIn * Geom::Scale(sf);
        ibox = scaled_box.roundOutwards();
    }

    auto pdim = Geom::IntPoint(width_in, height_in);
    // The unsigned width/height can wrap around when negative.
    int dx = ((int)width_in - ibox.width()) / 2;
    int dy = ((int)height_in - ibox.height()) / 2;
    auto area = Geom::IntRect::from_xywh(ibox.min() - Geom::IntPoint(dx, dy), pdim);

    /* Actual renderable area */
    Geom::IntRect ua = *Geom::intersect(ibox, area);
    auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, ua.width(), ua.height());

    {
        auto cr = Cairo::Context::create(surface);
        cr->rectangle(0, 0, ua.width(), ua.height());

        // We always use checkerboard to indicate transparency.
        if (SP_RGBA32_A_F(bg) < 1.0) {
            auto pattern = ink_cairo_pattern_create_checkerboard(bg, false);
            auto background = Cairo::RefPtr<Cairo::Pattern>(new Cairo::Pattern(pattern, true));
            cr->set_source(background);
            cr->fill();
        }

        // We always draw the background on top to indicate partial backgrounds.
        cr->set_source_rgba(SP_RGBA32_R_F(bg), SP_RGBA32_G_F(bg), SP_RGBA32_B_F(bg), SP_RGBA32_A_F(bg));
        cr->fill();
    }

    // Resize the contents to the available space with a scale factor.
    drawing->root()->setTransform(Geom::Scale(sf));
    drawing->update();

    auto dc = Inkscape::DrawingContext(surface->cobj(), ua.min());
    if (item) {
        // Render just one item
        item->render(dc, ua);
    } else {
        // Render drawing.
        drawing->render(dc, ua);
    }

    surface->flush();
    return surface;
}

} // namespace Preview
} // namespace UI
} // namespace Inkscape
