// SPDX-License-Identifier: GPL-2.0-or-later
#include "ctrl-handle-rendering.h"

#include <cairomm/enums.h>
#include <cmath>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <boost/container_hash/hash.hpp>
#include <cairomm/context.h>
#include <2geom/point.h>

#include "color.h"
#include "display/control/canvas-item-enums.h"

namespace Inkscape::Handles {
namespace {

void draw_darrow(Cairo::Context &cr, double size)
{
    // Find points, starting from tip of one arrowhead, working clockwise.
    /*   1        4
        ╱│        │╲
       ╱ └────────┘ ╲
     0╱  2        3  ╲5
      ╲  8        7  ╱
       ╲ ┌────────┐ ╱
        ╲│9      6│╱
    */

    // Length of arrowhead (not including stroke).
    double delta = (size - 1) / 4.0; // Use unscaled width.

    // Tip of arrow (0)
    double tip_x = 0.5;          // At edge, allow room for stroke.
    double tip_y = size / 2.0;   // Center

    // Outer corner (1)
    double out_x = tip_x + delta;
    double out_y = tip_y - delta;

    // Inner corner (2)
    double in_x = out_x;
    double in_y = out_y + (delta / 2.0);

    double x0 = tip_x;
    double y0 = tip_y;
    double x1 = out_x;
    double y1 = out_y;
    double x2 = in_x;
    double y2 = in_y;
    double x3 = size - in_x;
    double y3 = in_y;
    double x4 = size - out_x;
    double y4 = out_y;
    double x5 = size - tip_x;
    double y5 = tip_y;
    double x6 = size - out_x;
    double y6 = size - out_y;
    double x7 = size - in_x;
    double y7 = size - in_y;
    double x8 = in_x;
    double y8 = size - in_y;
    double x9 = out_x;
    double y9 = size - out_y;

    // Draw arrow
    cr.move_to(x0, y0);
    cr.line_to(x1, y1);
    cr.line_to(x2, y2);
    cr.line_to(x3, y3);
    cr.line_to(x4, y4);
    cr.line_to(x5, y5);
    cr.line_to(x6, y6);
    cr.line_to(x7, y7);
    cr.line_to(x8, y8);
    cr.line_to(x9, y9);
    cr.close_path();
}

void draw_carrow(Cairo::Context &cr, double size)
{
    // Length of arrowhead (not including stroke).
    double delta = (size - 3) / 4.0; // Use unscaled width.

    // Tip of arrow
    double tip_x =         1.5;  // Edge, allow room for stroke when rotated.
    double tip_y = delta + 1.5;

    // Outer corner (1)
    double out_x = tip_x + delta;
    double out_y = tip_y - delta;

    // Inner corner (2)
    double in_x = out_x;
    double in_y = out_y + (delta / 2.0);

    double x0 = tip_x;
    double y0 = tip_y;
    double x1 = out_x;
    double y1 = out_y;
    double x2 = in_x;
    double y2 = in_y;
    double x3 = size - in_y;        //double y3 = size - in_x;
    double x4 = size - out_y;
    double y4 = size - out_x;
    double x5 = size - tip_y;
    double y5 = size - tip_x;
    double x6 = x5 - delta;
    double y6 = y4;
    double x7 = x5 - delta / 2.0;
    double y7 = y4;
    double x8 = x1;                 //double y8 = y0 + delta/2.0;
    double x9 = x1;
    double y9 = y0 + delta;

    // Draw arrow
    cr.move_to(x0, y0);
    cr.line_to(x1, y1);
    cr.line_to(x2, y2);
    cr.arc(x1, y4, x3 - x2, 3.0 * M_PI / 2.0, 0);
    cr.line_to(x4, y4);
    cr.line_to(x5, y5);
    cr.line_to(x6, y6);
    cr.line_to(x7, y7);
    cr.arc_negative(x1, y4, x7 - x8, 0, 3.0 * M_PI / 2.0);
    cr.line_to(x9, y9);
    cr.close_path();
}

void draw_triangle(Cairo::Context &cr, double size)
{
    // Construct an arrowhead (triangle)
    double s = size / 2.0;
    double wcos = s * cos(M_PI / 6);
    double hsin = s * sin(M_PI / 6);
    // Construct a smaller arrow head for fill.
    Geom::Point p1f(1, s);
    Geom::Point p2f(s + wcos - 1, s + hsin);
    Geom::Point p3f(s + wcos - 1, s - hsin);
    // Draw arrow
    cr.move_to(p1f[0], p1f[1]);
    cr.line_to(p2f[0], p2f[1]);
    cr.line_to(p3f[0], p3f[1]);
    cr.close_path();
}

void draw_triangle_angled(Cairo::Context &cr, double size)
{
    // Construct an arrowhead (triangle) of half size.
    double s = size / 2.0;
    double wcos = s * cos(M_PI / 9);
    double hsin = s * sin(M_PI / 9);
    Geom::Point p1f(s + 1, s);
    Geom::Point p2f(s + wcos - 1, s + hsin - 1);
    Geom::Point p3f(s + wcos - 1, s - (hsin - 1));
    // Draw arrow
    cr.move_to(p1f[0], p1f[1]);
    cr.line_to(p2f[0], p2f[1]);
    cr.line_to(p3f[0], p3f[1]);
    cr.close_path();
}

void draw_pivot(Cairo::Context &cr, double size)
{
    double delta4 = (size - 5) / 4.0; // Keep away from edge or will clip when rotating.
    double delta8 = delta4 / 2;

    // Line start
    double center = size / 2.0;

    cr.move_to(center - delta8, center - 2 * delta4 - delta8);
    cr.rel_line_to(delta4,  0);
    cr.rel_line_to(0,       delta4);

    cr.rel_line_to(delta4,  delta4);

    cr.rel_line_to(delta4,  0);
    cr.rel_line_to(0,       delta4);
    cr.rel_line_to(-delta4,  0);

    cr.rel_line_to(-delta4,  delta4);

    cr.rel_line_to(0,       delta4);
    cr.rel_line_to(-delta4,  0);
    cr.rel_line_to(0,      -delta4);

    cr.rel_line_to(-delta4, -delta4);

    cr.rel_line_to(-delta4,  0);
    cr.rel_line_to(0,      -delta4);
    cr.rel_line_to(delta4,  0);

    cr.rel_line_to(delta4, -delta4);
    cr.close_path();

    cr.begin_new_sub_path();
    cr.arc_negative(center, center, delta4, 0, -2 * M_PI);
}

void draw_salign(Cairo::Context &cr, double size)
{
    // Triangle pointing at line.

    // Basic units.
    double delta4 = (size - 1) / 4.0; // Use unscaled width.
    double delta8 = delta4 / 2;
    if (delta8 < 2) {
        // Keep a minimum gap of at least one pixel (after stroking).
        delta8 = 2;
    }

    // Tip of triangle
    double tip_x = size / 2.0; // Center (also rotation point).
    double tip_y = size / 2.0;

    // Corner triangle position.
    double outer = size / 2.0 - delta4;

    // Outer line position
    double oline = size / 2.0 + (int)delta4;

    // Inner line position
    double iline = size / 2.0 + (int)delta8;

    // Draw triangle
    cr.move_to(tip_x,        tip_y);
    cr.line_to(outer,        outer);
    cr.line_to(size - outer, outer);
    cr.close_path();

    // Draw line
    cr.move_to(outer,        iline);
    cr.line_to(size - outer, iline);
    cr.line_to(size - outer, oline);
    cr.line_to(outer,        oline);
    cr.close_path();
}

void draw_calign(Cairo::Context &cr, double size)
{
    // Basic units.
    double delta4 = (size - 1) / 4.0; // Use unscaled width.
    double delta8 = delta4 / 2;
    if (delta8 < 2) {
        // Keep a minimum gap of at least one pixel (after stroking).
        delta8 = 2;
    }

    // Tip of triangle
    double tip_x = size / 2.0; // Center (also rotation point).
    double tip_y = size / 2.0;

    // Corner triangle position.
    double outer = size / 2.0 - delta8 - delta4;

    // End of line positin
    double eline = size / 2.0 - delta8;

    // Outer line position
    double oline = size / 2.0 + (int)delta4;

    // Inner line position
    double iline = size / 2.0 + (int)delta8;

    // Draw triangle
    cr.move_to(tip_x, tip_y);
    cr.line_to(outer, tip_y);
    cr.line_to(tip_x, outer);
    cr.close_path();

    // Draw line
    cr.move_to(iline, iline);
    cr.line_to(iline, eline);
    cr.line_to(oline, eline);
    cr.line_to(oline, oline);
    cr.line_to(eline, oline);
    cr.line_to(eline, iline);
    cr.close_path();
}

void draw_malign(Cairo::Context &cr, double size)
{
    // Basic units.
    double delta4 = (size - 1) / 4.0; // Use unscaled width.
    double delta8 = delta4 / 2;
    if (delta8 < 2) {
        // Keep a minimum gap of at least one pixel (after stroking).
        delta8 = 2;
    }

    // Tip of triangle
    double tip_0 = size / 2.0;
    double tip_1 = size / 2.0 - delta8;

    // Draw triangles
    cr.move_to(tip_0,           tip_1);
    cr.line_to(tip_0 - delta4,  tip_1 - delta4);
    cr.line_to(tip_0 + delta4,  tip_1 - delta4);
    cr.close_path();

    cr.move_to(size - tip_1,           tip_0);
    cr.line_to(size - tip_1 + delta4,  tip_0 - delta4);
    cr.line_to(size - tip_1 + delta4,  tip_0 + delta4);
    cr.close_path();

    cr.move_to(size - tip_0,           size - tip_1);
    cr.line_to(size - tip_0 + delta4,  size - tip_1 + delta4);
    cr.line_to(size - tip_0 - delta4,  size - tip_1 + delta4);
    cr.close_path();

    cr.move_to(tip_1,           tip_0);
    cr.line_to(tip_1 - delta4,  tip_0 + delta4);
    cr.line_to(tip_1 - delta4,  tip_0 - delta4);
    cr.close_path();
}

void draw_circle(Cairo::Context &cr, double size)
{
    cr.arc(size / 2.0, size / 2.0, size / 2.0, 0, 2 * M_PI);
}

void draw_square(Cairo::Context &cr, double size)
{
    cr.rectangle(0, 0, size, size);
}

void draw_diamond(Cairo::Context &cr, double size)
{
    cr.translate(size / 2.0, size / 2.0);
    cr.rotate(M_PI / 4);

    double const size2 = size / std::sqrt(2);
    cr.translate(-size2 / 2.0, -size2 / 2.0);
    cr.rectangle(0, 0, size2, size2);
}

void draw_cross(Cairo::Context &cr, double size, bool grid_fit)
{
    cr.move_to(0, 0);
    cr.line_to(size, size);

    cr.move_to(0, size);
    cr.line_to(size, 0);
}

void draw_plus(Cairo::Context &cr, double size, bool grid_fit)
{
    // draw shape and align fill to pixel grid, stroke will be grid-fitted later
    if (grid_fit) cr.translate(0.5, 0.5);
    double const half = size / 2;

    cr.move_to(half, 0);
    cr.line_to(half, size);

    cr.move_to(0, half);
    cr.line_to(size, half);
    if (grid_fit) cr.translate(-0.5, -0.5);
}

void draw_cairo_path(CanvasItemCtrlShape shape, Cairo::Context &cr, double size, bool grid_fit)
{
    switch (shape) {
        case CANVAS_ITEM_CTRL_SHAPE_DARROW:
        case CANVAS_ITEM_CTRL_SHAPE_SARROW:
            draw_darrow(cr, size);
            break;

        case CANVAS_ITEM_CTRL_SHAPE_TRIANGLE:
            draw_triangle(cr, size);
            break;

        case CANVAS_ITEM_CTRL_SHAPE_TRIANGLE_ANGLED:
            draw_triangle_angled(cr, size);
            break;

        case CANVAS_ITEM_CTRL_SHAPE_CARROW:
            draw_carrow(cr, size);
            break;

        case CANVAS_ITEM_CTRL_SHAPE_PIVOT:
            draw_pivot(cr, size);
            break;

        case CANVAS_ITEM_CTRL_SHAPE_SALIGN:
            draw_salign(cr, size);
            break;

        case CANVAS_ITEM_CTRL_SHAPE_CALIGN:
            draw_calign(cr, size);
            break;

        case CANVAS_ITEM_CTRL_SHAPE_MALIGN:
            draw_malign(cr, size);
            break;

        case CANVAS_ITEM_CTRL_SHAPE_CIRCLE:
            draw_circle(cr, size);
            break;

        case CANVAS_ITEM_CTRL_SHAPE_SQUARE:
            draw_square(cr, size);
            break;

        case CANVAS_ITEM_CTRL_SHAPE_DIAMOND:
            draw_diamond(cr, size);
            break;

        case CANVAS_ITEM_CTRL_SHAPE_CROSS:
            draw_cross(cr, size, grid_fit);
            break;

        case CANVAS_ITEM_CTRL_SHAPE_PLUS:
            draw_plus(cr, size, grid_fit);
            break;

        default:
            // Shouldn't happen
            std::cerr << "Missing drawing routine for shape " << shape << std::endl;
            break;
    }
}

std::unordered_map<RenderParams, std::shared_ptr<Cairo::ImageSurface const>> cache;
std::mutex mutex;

void set_source_rgba32(Cairo::Context &cr, uint32_t rgba)
{
    cr.set_source_rgba(SP_RGBA32_R_F(rgba),
                       SP_RGBA32_G_F(rgba),
                       SP_RGBA32_B_F(rgba),
                       SP_RGBA32_A_F(rgba));
}

std::shared_ptr<Cairo::ImageSurface const> draw_uncached(RenderParams const &p)
{
    // operate on a physical pixel scale, to make pixel grid aligning easier to understand
    auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, p.width, p.width);

    const auto scale = p.device_scale;

    auto cr = Cairo::Context(cairo_create(surface->cobj()), true);

    // align stroke to pixel grid; even width stroke needs whole coordinates, odd width needs half a pixel shift
    auto offset_stroke = [&](float stroke) {
        auto size = static_cast<int>(std::round(stroke * scale));
        auto half = size / 2;
        auto half_pixel = size & 1;
        auto offset = half_pixel ? half + 0.5 : half;
        cr.translate(offset, offset);
    };

    cr.set_operator(Cairo::Context::Operator::SOURCE);
    cr.set_line_cap(Cairo::Context::LineCap::SQUARE);
    cr.set_line_join(Cairo::Context::LineJoin::MITER);
    // miter limit tweaked to produce sharp draw_darrow(), but blunt draw_triangle_angled() tip
    cr.set_miter_limit(2.9);

    // Rotate around center
    cr.translate(p.width / 2.0, p.width / 2.0);
    cr.rotate(p.angle);
    cr.translate(-p.width / 2.0, -p.width / 2.0);

    // offset the path to make space for outline and stroke; pixel grid-fit the stroke
    auto effective_outline = 2 * p.outline_width + p.stroke_width;
    offset_stroke(effective_outline);

    // ask drawing routines to align handle fill to pixel grid (avoid fractional coordinates) if device scale is odd
    auto grid_fit = !!(scale & 1);
    draw_cairo_path(p.shape, cr, p.size * scale, grid_fit);

    // Outline.
    set_source_rgba32(cr, p.outline);
    cr.set_line_width(effective_outline * scale);
    cr.stroke_preserve();

    // Fill.
    set_source_rgba32(cr, p.fill);;
    cr.fill_preserve();

    // Stroke.
    set_source_rgba32(cr, p.stroke);
    cr.set_line_width(p.stroke_width * scale);
    cr.stroke();

    cairo_surface_set_device_scale(surface->cobj(), p.device_scale, p.device_scale); // No C++ API!
    return surface;
}

} // namespace

std::shared_ptr<Cairo::ImageSurface const> draw(RenderParams const &params)
{
    auto lock = std::unique_lock{mutex};

    auto &surface = cache[params];

    if (!surface) {
        surface = draw_uncached(params);
    }

    return surface;
}

} // namespace Inkscape

size_t std::hash<Inkscape::Handles::RenderParams>::operator()(Inkscape::Handles::RenderParams const &params) const
{
    auto const [a, b, c, d, e, f, g, h, i, j] = params;
    auto const tuple = std::make_tuple(a, b, c, d, e, f, g, h, i, j);
    return boost::hash<decltype(tuple)>{}(tuple);
}

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
