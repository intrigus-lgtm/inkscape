// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * This is the C++ glue between Inkscape and Autotrace
 *//*
 *
 * Authors:
 *   Marc Jeanmougin
 *
 * Copyright (C) 2018 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 *
 */
#include <iomanip>
#include <2geom/path-sink.h>
#include <glibmm/i18n.h>

#include "inkscape-autotrace.h"
#include "async/progress.h"
#include "util/safe-printf.h"

extern "C" {
#include "3rdparty/autotrace/autotrace.h"
#include "3rdparty/autotrace/output.h"
#include "3rdparty/autotrace/spline.h"
}

namespace Inkscape {
namespace Trace {
namespace Autotrace {
namespace {

struct at_splines_deleter { void operator()(at_splines_type *p) { at_splines_free(p); }; };
using at_splines_uniqptr = std::unique_ptr<at_splines_type, at_splines_deleter>;

/**
 * Eliminate the alpha channel by overlaying on top of white, and ensure the result is in packed RGB8 format.
 * If nothing needs to be done, the original pixbuf is returned, otherwise a new pixbuf is returned.
 */
Glib::RefPtr<Gdk::Pixbuf> to_rgb8_packed(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf)
{
    int width     = pixbuf->get_width();
    int height    = pixbuf->get_height();
    int rowstride = pixbuf->get_rowstride();
    int nchannels = pixbuf->get_n_channels();
    auto data     = pixbuf->get_pixels();

    if (nchannels == 3 && rowstride == width * 3) {
        return pixbuf;
    }

    int imgsize = width * height;
    auto out = new unsigned char[3 * imgsize];
    auto q = out;

    for (int y = 0; y < height; y++) {
        auto p = data + rowstride * y;
        for (int x = 0; x < width; x++) {
            unsigned char alpha = nchannels == 3 ? 255 : p[3];
            unsigned char white = 255 - alpha;
            for (int c = 0; c < 3; c++) {
                *(q++) = (int)p[c] * alpha / 256 + white;
            }
            p += nchannels;
        }
    }

    return Gdk::Pixbuf::create_from_data(out, Gdk::Colorspace::RGB, false, 8, width, height, width * 3, [out] (auto) { delete [] out; });
}

} // namespace

AutotraceTracingEngine::AutotraceTracingEngine()
{
    // Create options struct, automatically filled with defaults.
    opts = at_fitting_opts_new();
    opts->background_color = at_color_new(255, 255, 255);
    autotrace_init();
}

AutotraceTracingEngine::~AutotraceTracingEngine()
{
    at_fitting_opts_free(opts);
}

Glib::RefPtr<Gdk::Pixbuf> AutotraceTracingEngine::preview(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf)
{
    // Todo: Actually generate a meaningful preview.
    return to_rgb8_packed(pixbuf);
}

TraceResult AutotraceTracingEngine::trace(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf, Async::Progress<double> &progress)
{
    auto pb = to_rgb8_packed(pixbuf);
    
    at_bitmap bitmap;
    bitmap.height = pb->get_height();
    bitmap.width  = pb->get_width();
    bitmap.bitmap = pb->get_pixels();
    bitmap.np     = 3;

    auto throttled = Async::ProgressStepThrottler(progress, 0.02);
    auto sub_trace = Async::SubProgress(throttled, 0.0, 0.8);
    
    auto splines = at_splines_uniqptr(at_splines_new_full(
        &bitmap, opts,
        nullptr, nullptr,
        [] (gfloat frac, gpointer data) { reinterpret_cast<decltype(sub_trace)*>(data)->report(frac); }, &sub_trace,
        [] (gpointer data) -> gboolean { return !reinterpret_cast<decltype(sub_trace)*>(data)->keepgoing(); }, &sub_trace
    ));
    // at_output_write_func wfunc = at_output_get_handler_by_suffix("svg");
    // at_spline_writer *wfunc = at_output_get_handler_by_suffix("svg");
    // at_splines_write(wfunc, stdout, "", NULL, splines, NULL, NULL);

    sub_trace.report_or_throw(1.0);
    auto sub_convert = Async::SubProgress(throttled, 0.8, 0.2);

    int height = splines->height;
    at_spline_list_type list;
    at_color last_color = { 0, 0, 0 };

    std::string style;
    Geom::PathBuilder pathbuilder;
    TraceResult res;

    auto get_style = [&] {
        char color[10];
        safeprintf(color, "#%02x%02x%02x;", list.color.r, list.color.g, list.color.b);

        std::stringstream ss;
        ss << (splines->centerline || list.open ? "stroke:" : "fill:") << color
           << (splines->centerline || list.open ? "fill:" : "stroke:") << "none";

        return ss.str();
    };

    auto to_geom = [=] (at_real_coord const &c) {
        return Geom::Point(c.x, height - c.y);
    };

    int const num_splines = SPLINE_LIST_ARRAY_LENGTH(*splines);
    for (int list_i = 0; list_i < num_splines; list_i++) {
        sub_convert.report_or_throw((double)list_i / num_splines);

        list = SPLINE_LIST_ARRAY_ELT(*splines, list_i);

        if (list_i == 0 || !at_color_equal(&list.color, &last_color)) {
            if (list_i > 0) {
                if (!(splines->centerline || list.open)) {
                    pathbuilder.closePath();
                } else {
                    pathbuilder.flush();
                }
                res.emplace_back(std::move(style), pathbuilder.peek());
                pathbuilder.clear();
            }

            style = get_style();
        }

        auto const first = SPLINE_LIST_ELT(list, 0);
        pathbuilder.moveTo(to_geom(START_POINT(first)));

        for (int spline_i = 0; spline_i < SPLINE_LIST_LENGTH(list); spline_i++) {
            auto const spline = SPLINE_LIST_ELT(list, spline_i);

            if (SPLINE_DEGREE(spline) == AT_LINEARTYPE) {
                pathbuilder.lineTo(to_geom(END_POINT(spline)));
            } else {
                pathbuilder.curveTo(to_geom(CONTROL1(spline)), to_geom(CONTROL2(spline)), to_geom(END_POINT(spline)));
            }

            last_color = list.color;
        }
    }

    if (SPLINE_LIST_ARRAY_LENGTH(*splines) > 0) {
        if (!(splines->centerline || list.open)) {
            pathbuilder.closePath();
        } else {
            pathbuilder.flush();
        }
        res.emplace_back(std::move(style), pathbuilder.peek());
    }

    return res;
}

void AutotraceTracingEngine::setColorCount(unsigned color_count)
{
    opts->color_count = color_count;
}

void AutotraceTracingEngine::setCenterLine(bool centerline)
{
    opts->centerline = centerline;
}

void AutotraceTracingEngine::setPreserveWidth(bool preserve_width)
{
    opts->preserve_width = preserve_width;
}

void AutotraceTracingEngine::setFilterIterations(unsigned filter_iterations)
{
    opts->filter_iterations = filter_iterations;
}

void AutotraceTracingEngine::setErrorThreshold(float error_threshold)
{
    opts->error_threshold = error_threshold;
}

} // namespace Autotrace
} // namespace Trace
} // namespace Inkscape

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
