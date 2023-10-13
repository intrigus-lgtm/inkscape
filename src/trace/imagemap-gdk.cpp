// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * TODO: insert short description here
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#include <cassert>
#include "imagemap-gdk.h"

namespace Inkscape {
namespace Trace {

GrayMap gdkPixbufToGrayMap(Glib::RefPtr<Gdk::Pixbuf> const &buf)
{
    int width     = buf->get_width();
    int height    = buf->get_height();
    int rowstride = buf->get_rowstride();
    int nchannels = buf->get_n_channels();
    auto data     = buf->get_pixels();

    auto map = GrayMap(width, height);

    for (int y = 0; y < height; y++) {
        auto p = data + rowstride * y;
        for (int x = 0; x < width; x++) {
            int alpha = nchannels == 3 ? 255 : p[3];
            int white = 3 * (255 - alpha);
            unsigned long sample = (int)p[0] + (int)p[1] + (int)p[2];
            unsigned long bright = sample * alpha / 256 + white;
            map.setPixel(x, y, bright);
            p += nchannels;
        }
    }

    return map;
}

Glib::RefPtr<Gdk::Pixbuf> grayMapToGdkPixbuf(GrayMap const &map)
{
    auto buf = Gdk::Pixbuf::create(Gdk::Colorspace::RGB, false, 8, map.width, map.height);

    int rowstride = buf->get_rowstride();
    int nchannels = buf->get_n_channels();
    auto data     = buf->get_pixels();

    for (int y = 0; y < map.height; y++) {
        auto p = data + rowstride * y;
        for (int x = 0; x < map.width; x++) {
            unsigned long pix = map.getPixel(x, y) / 3;
            p[0] = p[1] = p[2] = pix & 0xff;
            p += nchannels;
        }
    }

    return buf;
}

RgbMap gdkPixbufToRgbMap(Glib::RefPtr<Gdk::Pixbuf> const &buf)
{
    int width     = buf->get_width();
    int height    = buf->get_height();
    int rowstride = buf->get_rowstride();
    int nchannels = buf->get_n_channels();
    auto data     = buf->get_pixels();

    auto map = RgbMap(width, height);

    for (int y = 0; y < height; y++) {
        auto p = data + rowstride * y;
        for (int x = 0; x < width; x++) {
            int alpha = nchannels == 3 ? 255 : p[3];
            int white = 255 - alpha;
            unsigned char r = (int)p[0] * alpha / 256 + white;
            unsigned char g = (int)p[1] * alpha / 256 + white;
            unsigned char b = (int)p[2] * alpha / 256 + white;
            map.setPixel(x, y, {r, g, b});
            p += nchannels;
        }
    }

    return map;
}

Glib::RefPtr<Gdk::Pixbuf> indexedMapToGdkPixbuf(IndexedMap const &map)
{
    auto buf = Gdk::Pixbuf::create(Gdk::Colorspace::RGB, false, 8, map.width, map.height);

    auto data     = buf->get_pixels();
    int rowstride = buf->get_rowstride();
    int nchannels = buf->get_n_channels();

    for (int y = 0; y < map.height; y++) {
        auto p = data + rowstride * y;
        for (int x = 0; x < map.width; x++) {
            auto rgb = map.getPixelValue(x, y);
            p[0] = rgb.r & 0xff;
            p[1] = rgb.g & 0xff;
            p[2] = rgb.b & 0xff;
            p += nchannels;
        }
    }

    return buf;
}

} // namespace Trace
} // namespace Inkscape
