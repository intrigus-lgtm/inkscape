// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This is the C++ glue between Inkscape and Potrace
 *
 * Authors:
 *   Bob Jamison <rjamison@titan.com>
 *   Stéphane Gimenez <dev@gim.name>
 *
 * Copyright (C) 2004-2006 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 *
 * Potrace, the wonderful tracer located at http://potrace.sourceforge.net,
 * is provided by the generosity of Peter Selinger, to whom we are grateful.
 *
 */
#include <iomanip>
#include <glibmm/i18n.h>
#include <potracelib.h>

#include "inkscape-potrace.h"
#include "bitmap.h"

#include "async/progress.h"
#include "trace/filterset.h"
#include "trace/quantize.h"
#include "trace/imagemap-gdk.h"
#include "util-string/ustring-format.h"

namespace {

struct potrace_state_deleter { void operator()(potrace_state_t *p) { potrace_state_free(p); }; };
using potrace_state_uniqptr = std::unique_ptr<potrace_state_t, potrace_state_deleter>;

struct potrace_bitmap_deleter { void operator()(potrace_bitmap_t *p) { bm_free(p); }; };
using potrace_bitmap_uniqptr = std::unique_ptr<potrace_bitmap_t, potrace_bitmap_deleter>;

Glib::ustring twohex(int value)
{
    return Inkscape::ustring::format_classic(std::hex, std::setfill(L'0'), std::setw(2), value);
}

} // namespace

namespace Inkscape {
namespace Trace {
namespace Potrace {

PotraceTracingEngine::PotraceTracingEngine() { common_init(); }

PotraceTracingEngine::PotraceTracingEngine(TraceType traceType, bool invert, int quantizationNrColors, double brightnessThreshold, double brightnessFloor, double cannyHighThreshold, int multiScanNrColors, bool multiScanStack, bool multiScanSmooth, bool multiScanRemoveBackground)
    : traceType(traceType)
    , invert(invert)
    , quantizationNrColors(quantizationNrColors)
    , brightnessThreshold(brightnessThreshold)
    , brightnessFloor(brightnessFloor)
    , cannyHighThreshold(cannyHighThreshold)
    , multiScanNrColors(multiScanNrColors)
    , multiScanStack(multiScanStack)
    , multiScanSmooth(multiScanSmooth)
    , multiScanRemoveBackground(multiScanRemoveBackground) { common_init(); }

void PotraceTracingEngine::common_init()
{
    potraceParams = potrace_param_default();
}

PotraceTracingEngine::~PotraceTracingEngine()
{
    potrace_param_free(potraceParams);
}

void PotraceTracingEngine::setOptiCurve(int opticurve)
{
    potraceParams->opticurve = opticurve;
}

void PotraceTracingEngine::setOptTolerance(double opttolerance)
{
    potraceParams->opttolerance = opttolerance;
}

void PotraceTracingEngine::setAlphaMax(double alphamax)
{
    potraceParams->alphamax = alphamax;
}

void PotraceTracingEngine::setTurdSize(int turdsize)
{
    potraceParams->turdsize = turdsize;
}

/**
 * Recursively descend the potrace_path_t node tree \a paths, writing paths to \a builder.
 * The \a points set is used to prevent redundant paths.
 */
void PotraceTracingEngine::writePaths(potrace_path_t *paths, Geom::PathBuilder &builder, std::unordered_set<Geom::Point> &points, Async::Progress<double> &progress) const
{
    auto to_geom = [] (potrace_dpoint_t const &c) {
        return Geom::Point(c.x, c.y);
    };

    for (auto path = paths; path; path = path->sibling) {
        progress.throw_if_cancelled();

        auto const &curve = path->curve;
        // g_message("node->fm:%d\n", node->fm);
        if (curve.n == 0) {
            continue;
        }

        auto seg = curve.c[curve.n - 1];
        auto const pt = to_geom(seg[2]);
        // Have we been here already?
        auto inserted = points.emplace(pt).second;
        if (!inserted) {
            // g_message("duplicate point: (%f,%f)\n", x2, y2);
            continue;
        }
        builder.moveTo(pt);

        for (int i = 0; i < curve.n; i++) {
            auto seg = curve.c[i];
            switch (curve.tag[i]) {
                case POTRACE_CORNER:
                    builder.lineTo(to_geom(seg[1]));
                    builder.lineTo(to_geom(seg[2]));
                    break;
                case POTRACE_CURVETO:
                    builder.curveTo(to_geom(seg[0]), to_geom(seg[1]), to_geom(seg[2]));
                    break;
                default:
                    break;
            }
        }
        builder.closePath();

        for (auto child = path->childlist; child; child = child->sibling) {
            writePaths(child, builder, points, progress);
        }
    }
}

std::optional<GrayMap> PotraceTracingEngine::filter(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf) const
{
    std::optional<GrayMap> map;

    if (traceType == TraceType::QUANT) {

        // Color quantization -- banding
        auto rgbmap = gdkPixbufToRgbMap(pixbuf);
        // rgbMap->writePPM(rgbMap, "rgb.ppm");
        map = quantizeBand(rgbmap, quantizationNrColors);

    } else if (traceType == TraceType::BRIGHTNESS || traceType == TraceType::BRIGHTNESS_MULTI) {

        // Brightness threshold
        auto gm = gdkPixbufToGrayMap(pixbuf);
        map = GrayMap(gm.width, gm.height);

        double floor = 3.0 * brightnessFloor * 256.0;
        double cutoff = 3.0 * brightnessThreshold * 256.0;
        for (int y = 0; y < gm.height; y++) {
            for (int x = 0; x < gm.width; x++) {
                double brightness = gm.getPixel(x, y);
                bool black = brightness >= floor && brightness < cutoff;
                map->setPixel(x, y, black ? GrayMap::BLACK : GrayMap::WHITE);
            }
        }

        // map->writePPM(map, "brightness.ppm");

    } else if (traceType == TraceType::CANNY) {

        // Canny edge detection
        auto gm = gdkPixbufToGrayMap(pixbuf);
        map = grayMapCanny(gm, 0.1, cannyHighThreshold);
        // map->writePPM(map, "canny.ppm");

    }

    // Invert the image if necessary.
    if (map && invert) {
        for (int y = 0; y < map->height; y++) {
            for (int x = 0; x < map->width; x++) {
                auto brightness = map->getPixel(x, y);
                brightness = GrayMap::WHITE - brightness;
                map->setPixel(x, y, brightness);
            }
        }
    }

    return map;
}

IndexedMap PotraceTracingEngine::filterIndexed(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf) const
{
    auto map = gdkPixbufToRgbMap(pixbuf);

    if (multiScanSmooth) {
        map = rgbMapGaussian(map);
    }

    auto imap = rgbMapQuantize(map, multiScanNrColors);

    auto tomono = [] (RGB c) -> RGB {
        unsigned char s = ((int)c.r + (int)c.g + (int)c.b) / 3;
        return { s, s, s };
    };

    if (traceType == TraceType::QUANT_MONO || traceType == TraceType::BRIGHTNESS_MULTI) {
        // Turn to grays
        for (auto &c : imap.clut) {
            c = tomono(c);
        }
    }

    return imap;
}

Glib::RefPtr<Gdk::Pixbuf> PotraceTracingEngine::preview(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf)
{
    if (traceType == TraceType::QUANT_COLOR ||
        traceType == TraceType::QUANT_MONO  ||
        traceType == TraceType::BRIGHTNESS_MULTI) // this is a lie: multipass doesn't use filterIndexed, but it's a better preview approx than filter()
    {
        auto gm = filterIndexed(pixbuf);

        return indexedMapToGdkPixbuf(gm);

    } else {

        auto gm = filter(pixbuf);
        if (!gm) {
            return {};
        }

        return grayMapToGdkPixbuf(*gm);
    }
}

/**
 * This is the actual wrapper of the call to Potrace.
 */
Geom::PathVector PotraceTracingEngine::grayMapToPath(GrayMap const &grayMap, Async::Progress<double> &progress)
{
    auto potraceBitmap = potrace_bitmap_uniqptr(bm_new(grayMap.width, grayMap.height));
    if (!potraceBitmap) {
        return {};
    }

    bm_clear(potraceBitmap.get(), 0);

    // Read the data out of the GrayMap
    for (int y = 0; y < grayMap.height; y++) {
        for (int x = 0; x < grayMap.width; x++) {
            BM_UPUT(potraceBitmap, x, y, grayMap.getPixel(x, y) ? 0 : 1);
        }
    }

    progress.throw_if_cancelled();

    //##Debug
    /*
    FILE *f = fopen("poimage.pbm", "wb");
    bm_writepbm(f, bm);
    fclose(f);
    */

    // Trace the bitmap.

    auto throttled = Async::ProgressStepThrottler(progress, 0.02);

    potraceParams->progress.data = &throttled;
    potraceParams->progress.callback = [] (double progress, void *data) { reinterpret_cast<decltype(throttled)*>(data)->report(progress); };
    auto potraceState = potrace_state_uniqptr(potrace_trace(potraceParams, potraceBitmap.get()));

    potraceBitmap.reset();

    progress.throw_if_cancelled();

    // Extract the paths into a pathvector and return it.
    Geom::PathBuilder builder;
    std::unordered_set<Geom::Point> points;
    writePaths(potraceState->plist, builder, points, progress);
    return builder.peek();
}

/**
 * This is called for a single scan.
 */
TraceResult PotraceTracingEngine::traceSingle(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf, Async::Progress<double> &progress)
{
    brightnessFloor = 0.0; // important to set this, since used by filter()

    auto grayMap = filter(pixbuf);
    if (!grayMap) {
        return {};
    }

    progress.report_or_throw(0.2);

    auto sub_gm = Async::SubProgress(progress, 0.2, 0.8);
    auto pv = grayMapToPath(*grayMap, sub_gm);

    TraceResult results;
    results.emplace_back("fill:#000000", std::move(pv));
    return results;
}

/**
 * This allows routines that already generate GrayMaps to skip image filtering, increasing performance.
 */
TraceResult PotraceTracingEngine::traceGrayMap(GrayMap const &grayMap, Async::Progress<double> &progress)
{
    auto pv = grayMapToPath(grayMap, progress);

    TraceResult results;
    results.emplace_back("fill:#000000", std::move(pv));
    return results;
}

/**
 * Called for multiple-scanning algorithms
 */
TraceResult PotraceTracingEngine::traceBrightnessMulti(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf, Async::Progress<double> &progress)
{
    double constexpr low   = 0.2; // bottom of range
    double constexpr high  = 0.9; // top of range
    double const     delta = (high - low) / multiScanNrColors;

    brightnessFloor = 0.0; // Set bottom to black

    TraceResult results;

    for (int i = 0; i < multiScanNrColors; i++) {
        auto subprogress = Async::SubProgress(progress, (double)i / multiScanNrColors, 1.0 / multiScanNrColors);

        brightnessThreshold = low + delta * i;

        auto grayMap = filter(pixbuf);
        if (!grayMap) {
            continue;
        }

        subprogress.report_or_throw(0.2);

        auto sub_gmtopath = Async::SubProgress(subprogress, 0.2, 0.8);
        auto pv = grayMapToPath(*grayMap, sub_gmtopath);
        if (pv.empty()) {
            continue;
        }

        // get style info
        int grayVal = 256.0 * brightnessThreshold;
        auto style = Glib::ustring::compose("fill-opacity:1.0;fill:#%1%2%3", twohex(grayVal), twohex(grayVal), twohex(grayVal));

        // g_message("### GOT '%s' \n", style.c_str());
        results.emplace_back(style.raw(), std::move(pv));

        if (!multiScanStack) {
            brightnessFloor = brightnessThreshold;
        }

        subprogress.report_or_throw(1.0);
    }

    // Remove the bottom-most scan, if requested.
    if (results.size() > 1 && multiScanRemoveBackground) {
        results.pop_back();
    }

    return results;
}

/**
 * Quantization
 */
TraceResult PotraceTracingEngine::traceQuant(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf, Async::Progress<double> &progress)
{
    auto imap = filterIndexed(pixbuf);

    // Create and clear a gray map
    auto gm = GrayMap(imap.width, imap.height);
    for (int row = 0; row < gm.height; row++) {
        for (int col = 0; col < gm.width; col++) {
            gm.setPixel(col, row, GrayMap::WHITE);
        }
    }

    TraceResult results;

    for (int colorIndex = 0; colorIndex < imap.nrColors; colorIndex++) {
        auto subprogress = Async::SubProgress(progress, (double)colorIndex / imap.nrColors, 1.0 / imap.nrColors);

        // Update the graymap for the current color index
        for (int row = 0; row < imap.height; row++) {
            for (int col = 0; col < imap.width; col++) {
                int index = imap.getPixel(col, row);
                if (index == colorIndex) {
                    gm.setPixel(col, row, GrayMap::BLACK);
                } else if (!multiScanStack) {
                    gm.setPixel(col, row, GrayMap::WHITE);
                }
            }
        }

        subprogress.report_or_throw(0.2);

        // Now we have a traceable graymap
        auto sub_gmtopath = Async::SubProgress(subprogress, 0.2, 0.8);
        auto pv = grayMapToPath(gm, sub_gmtopath);

        if (!pv.empty()) {
            // get style info
            auto rgb = imap.clut[colorIndex];
            auto style = Glib::ustring::compose("fill:#%1%2%3", twohex(rgb.r), twohex(rgb.g), twohex(rgb.b));
            results.emplace_back(style.raw(), std::move(pv));
        }

        subprogress.report_or_throw(1.0);
    }

    // Remove the bottom-most scan, if requested.
    if (results.size() > 1 && multiScanRemoveBackground) {
        results.pop_back();
    }

    return results;
}

TraceResult PotraceTracingEngine::trace(Glib::RefPtr<Gdk::Pixbuf> const &pixbuf, Async::Progress<double> &progress)
{
    if (traceType == TraceType::QUANT_COLOR || traceType == TraceType::QUANT_MONO) {
        return traceQuant(pixbuf, progress);
    } else if (traceType == TraceType::BRIGHTNESS_MULTI) {
        return traceBrightnessMulti(pixbuf, progress);
    } else {
        return traceSingle(pixbuf, progress);
    }
}

} // namespace Potrace
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
