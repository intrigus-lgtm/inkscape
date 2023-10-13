// SPDX-License-Identifier: GPL-2.0-or-later

#include "graphics.h"

#include <2geom/parallelogram.h>
#include <cairomm/context.h>
#include <cairomm/surface.h>

#include "helper/geom.h"
#include "ui/util.h"
#include "util.h"

namespace Inkscape::UI::Widget {

namespace {

// Convert an rgba into a pattern, turning transparency into checkerboard-ness.
Cairo::RefPtr<Cairo::Pattern> rgba_to_pattern(std::uint32_t const rgba)
{
    if (SP_RGBA32_A_U(rgba) == 255) {
        return Cairo::SolidPattern::create_rgb(SP_RGBA32_R_F(rgba), SP_RGBA32_G_F(rgba), SP_RGBA32_B_F(rgba));
    }

    int constexpr w = 6;
    int constexpr h = 6;

    auto dark = checkerboard_darken(rgba);

    auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, 2 * w, 2 * h);

    auto cr = Cairo::Context::create(surface);
    cr->set_operator(Cairo::Context::Operator::SOURCE);
    cr->set_source_rgb(SP_RGBA32_R_F(rgba), SP_RGBA32_G_F(rgba), SP_RGBA32_B_F(rgba));
    cr->paint();
    cr->set_source_rgb(dark[0], dark[1], dark[2]);
    cr->rectangle(0, 0, w, h);
    cr->rectangle(w, h, w, h);
    cr->fill();

    auto pattern = Cairo::SurfacePattern::create(surface);
    pattern->set_extend(Cairo::Pattern::Extend::REPEAT);
    pattern->set_filter(Cairo::SurfacePattern::Filter::NEAREST);

    return pattern;
}

} // namespace

// Paint the background and pages using Cairo into the given fragment.
void Graphics::paint_background(Fragment const &fragment, PageInfo const &pi,
                                std::uint32_t const page, std::uint32_t const desk,
                                Cairo::RefPtr<Cairo::Context> const &cr)
{
    cr->save();
    cr->set_operator(Cairo::Context::Operator::SOURCE);
    cr->rectangle(0, 0, fragment.rect.width(), fragment.rect.height());
    cr->clip();

    if (desk == page || check_single_page(fragment, pi)) {
        // Desk and page are the same, or a single page fills the whole screen; just clear the fragment to page.
        cr->set_source(rgba_to_pattern(page));
        cr->paint();
    } else {
        // Paint the background to the complement of the pages. (Slightly overpaints when pages overlap.)
        cr->save();
        cr->set_source(rgba_to_pattern(desk));
        cr->set_fill_rule(Cairo::Context::FillRule::EVEN_ODD);
        cr->rectangle(0, 0, fragment.rect.width(), fragment.rect.height());
        cr->translate(-fragment.rect.left(), -fragment.rect.top());
        cr->transform(geom_to_cairo(fragment.affine));
        for (auto &rect : pi.pages) {
            cr->rectangle(rect.left(), rect.top(), rect.width(), rect.height());
        }
        cr->fill();
        cr->restore();

        // Paint the pages.
        cr->save();
        cr->set_source(rgba_to_pattern(page));
        cr->translate(-fragment.rect.left(), -fragment.rect.top());
        cr->transform(geom_to_cairo(fragment.affine));
        for (auto &rect : pi.pages) {
            cr->rectangle(rect.left(), rect.top(), rect.width(), rect.height());
        }
        cr->fill();
        cr->restore();
    }

    cr->restore();
}

std::pair<Geom::IntRect, Geom::IntRect> Graphics::calc_splitview_cliprects(Geom::IntPoint const &size, Geom::Point const &split_frac, SplitDirection split_direction)
{
    auto window = Geom::IntRect({0, 0}, size);

    auto content = window;
    auto outline = window;
    auto split = [&] (Geom::Dim2 dim, Geom::IntRect &lo, Geom::IntRect &hi) {
        int s = std::round(split_frac[dim] * size[dim]);
        lo[dim].setMax(s);
        hi[dim].setMin(s);
    };

    switch (split_direction) {
        case Inkscape::SplitDirection::NORTH: split(Geom::Y, content, outline); break;
        case Inkscape::SplitDirection::EAST:  split(Geom::X, outline, content); break;
        case Inkscape::SplitDirection::SOUTH: split(Geom::Y, outline, content); break;
        case Inkscape::SplitDirection::WEST:  split(Geom::X, content, outline); break;
        default: assert(false); break;
    }

    return std::make_pair(content, outline);
}

void Graphics::paint_splitview_controller(Geom::IntPoint const &size, Geom::Point const &split_frac, SplitDirection split_direction, SplitDirection hover_direction, Cairo::RefPtr<Cairo::Context> const &cr)
{
    auto split_position = (split_frac * size).round();

    // Add dividing line.
    cr->set_source_rgb(0.0, 0.0, 0.0);
    cr->set_line_width(1.0);
    if (split_direction == Inkscape::SplitDirection::EAST ||
        split_direction == Inkscape::SplitDirection::WEST) {
        cr->move_to(split_position.x() + 0.5, 0.0     );
        cr->line_to(split_position.x() + 0.5, size.y());
        cr->stroke();
    } else {
        cr->move_to(0.0     , split_position.y() + 0.5);
        cr->line_to(size.x(), split_position.y() + 0.5);
        cr->stroke();
    }

    // Add controller image.
    double a = hover_direction == Inkscape::SplitDirection::NONE ? 0.5 : 1.0;
    cr->set_source_rgba(0.2, 0.2, 0.2, a);
    cr->arc(split_position.x(), split_position.y(), 20, 0, 2 * M_PI);
    cr->fill();

    for (int i = 0; i < 4; i++) {
        // The four direction triangles.
        cr->save();

        // Position triangle.
        cr->translate(split_position.x(), split_position.y());
        cr->rotate((i + 2) * M_PI / 2);

        // Draw triangle.
        cr->move_to(-5,  8);
        cr->line_to( 0, 18);
        cr->line_to( 5,  8);
        cr->close_path();

        double b = (int)hover_direction == (i + 1) ? 0.9 : 0.7;
        cr->set_source_rgba(b, b, b, a);
        cr->fill();

        cr->restore();
    }
}

bool Graphics::check_single_page(Fragment const &view, PageInfo const &pi)
{
    auto pl = Geom::Parallelogram(view.rect) * view.affine.inverse();
    return std::any_of(pi.pages.begin(), pi.pages.end(), [&] (auto &rect) {
        return Geom::Parallelogram(rect).contains(pl);
    });
}

} // namespace Inkscape::UI::Widget
