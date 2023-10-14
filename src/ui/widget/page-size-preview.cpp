// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 *
 * Page size preview widget
 */
/*
 * Authors:
 *   Mike Kowalski
 *
 * Copyright (C) 2021 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "page-size-preview.h"
#include "display/cairo-utils.h"
#include "2geom/rect.h"

namespace Inkscape {
namespace UI {
namespace Widget {

PageSizePreview::PageSizePreview() {
    set_visible(true);
    set_draw_func(sigc::mem_fun(*this, &PageSizePreview::draw_func));
}

void rounded_rectangle(const Cairo::RefPtr<Cairo::Context>& cr, double x, double y, double w, double h, double r) {
    cr->begin_new_sub_path();
    cr->arc(x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cr->arc(x + w - r, y + r, r, 3 * M_PI / 2, 2 * M_PI);
    cr->arc(x + w - r, y + h - r, r, 0, M_PI / 2);
    cr->arc(x + r, y + h - r, r, M_PI / 2, M_PI);
    cr->close_path();
}

void set_source_rgba(const Cairo::RefPtr<Cairo::Context>& ctx, unsigned int rgba) {
    ctx->set_source_rgba(SP_RGBA32_R_F(rgba), SP_RGBA32_G_F(rgba), SP_RGBA32_B_F(rgba), SP_RGBA32_A_F(rgba));
}

void PageSizePreview::draw_func(Cairo::RefPtr<Cairo::Context> const &ctx, int width, int height) {
    // too small to fit anything?
    if (width <= 2 || height <= 2) return;

    double x = 0;//alloc.get_x();
    double y = 0;//alloc.get_y();

    if (_draw_checkerboard) {
        // auto device_scale = get_scale_factor();
        Cairo::RefPtr<Cairo::Pattern> pattern(new Cairo::Pattern(ink_cairo_pattern_create_checkerboard(_desk_color)));
        ctx->save();
        ctx->set_operator(Cairo::Context::Operator::SOURCE);
        ctx->set_source(pattern);
        rounded_rectangle(ctx, x, y, width, height, 2.0);
        ctx->fill();
        ctx->restore();
    }
    else {
        rounded_rectangle(ctx, x, y, width, height, 2.0);
        set_source_rgba(ctx, _desk_color);
        ctx->fill();
    }

    // use lesser dimension to prevent page from changing size when
    // switching from portrait to landscape or vice versa
    auto size = std::round(std::min(width, height) * 0.90); // 90% to leave margins
    double w, h;
    if (_width > _height) {
        w = size;
        h = std::round(size * _height / _width);
    }
    else {
        h = size;
        w = std::round(size * _width / _height);
    }
    if (w < 2) w = 2;
    if (h < 2) h = 2;

    // center page
    double ox = std::round(x + (width - w) / 2);
    double oy = std::round(y + (height - h) / 2);
    Geom::Rect rect(ox, oy, ox + w, oy + h);

    ctx->rectangle(rect.left(), rect.top(), rect.width(), rect.height());

    if (_draw_checkerboard) {
        Cairo::RefPtr<Cairo::Pattern> pattern(new Cairo::Pattern(ink_cairo_pattern_create_checkerboard(_page_color)));
        ctx->save();
        ctx->set_operator(Cairo::Context::Operator::SOURCE);
        ctx->set_source(pattern);
        ctx->rectangle(rect.left(), rect.top(), rect.width(), rect.height());
        ctx->fill();
        ctx->restore();
    }
    else {
        ctx->rectangle(rect.left(), rect.top(), rect.width(), rect.height());
        set_source_rgba(ctx, _page_color | 0xff);
        ctx->fill();
    }

    // draw cross
    /*
    {
        double gradient_size = 4;
        double cx = std::round(x + (width - gradient_size) / 2);
        double cy = std::round(y + (height - gradient_size) / 2);
        auto horz = Cairo::LinearGradient::create(x, cy, x, cy + gradient_size);
        auto vert = Cairo::LinearGradient::create(cx, y, cx + gradient_size, y);

        horz->add_color_stop_rgba(0.0, 0, 0, 0, 0.0);
        horz->add_color_stop_rgba(0.5, 0, 0, 0, 0.2);
        horz->add_color_stop_rgba(0.5, 1, 1, 1, 0.8);
        horz->add_color_stop_rgba(1.0, 1, 1, 1, 0.0);

        vert->add_color_stop_rgba(0.0, 0, 0, 0, 0.0);
        vert->add_color_stop_rgba(0.5, 0, 0, 0, 0.2);
        vert->add_color_stop_rgba(0.5, 1, 1, 1, 0.8);
        vert->add_color_stop_rgba(1.0, 1, 1, 1, 0.0);

        ctx->rectangle(x, cy, width, gradient_size);
        ctx->set_source(horz);
        ctx->fill();

        ctx->rectangle(cx, y, gradient_size, height);
        ctx->set_source(vert);
        ctx->fill();
    }

    ctx->rectangle(rect.left(), rect.top(), rect.width(), rect.height());
    set_source_rgba(ctx, _page_color);
    ctx->fill();
*/
    if (_draw_border) {
        // stoke; not pixel aligned, just like page on canvas
        ctx->rectangle(rect.left(), rect.top(), rect.width(), rect.height());
        set_source_rgba(ctx, _border_color);
        ctx->set_line_width(1);
        ctx->stroke();

        if (_draw_shadow) {
            const auto a = (exp(-3 * SP_RGBA32_A_F(_border_color)) - 1) / (exp(-3) - 1);
            ink_cairo_draw_drop_shadow(ctx, rect, 12, _border_color, a);
        }
    }
}

void PageSizePreview::draw_border(bool border) {
    _draw_border = border;
    queue_draw();
}

void PageSizePreview::set_desk_color(unsigned int rgba) {
    _desk_color = rgba | 0xff; // desk always opaque
    queue_draw();
}
void PageSizePreview::set_page_color(unsigned int rgba) {
    _page_color = rgba;
    queue_draw();
}
void PageSizePreview::set_border_color(unsigned int rgba) {
    _border_color = rgba;
    queue_draw();
}

void PageSizePreview::enable_drop_shadow(bool shadow) {
    _draw_shadow = shadow;
    queue_draw();
}

void PageSizePreview::enable_checkerboard(bool checkerboard) {
    _draw_checkerboard = checkerboard;
    queue_draw();
}

void PageSizePreview::set_page_size(double width, double height) {
    _width = width;
    _height = height;
    queue_draw();
}

} } } // namespace Inkscape/Widget/UI
