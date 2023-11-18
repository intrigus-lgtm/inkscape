// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * \brief helper functions for retrieving monitor geometry, etc.
 *//*
 * Authors:
 * see git history
 *   Patrick Storz <eduard.braun2@gmx.de>
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "monitor.h"

#include <limits>
#include <gdkmm/display.h>
#include <gdkmm/monitor.h>
#include <gdkmm/surface.h>
#include <2geom/point.h>
#include <2geom/rect.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#endif
#ifdef GDK_WINDOWING_WIN32
#include <gdk/win32/gdkwin32.h>
#endif

namespace Inkscape::UI {
namespace {

// Removed from Gtk in commit a46f9af1, so we have to reimplement it here.
Glib::RefPtr<Gdk::Monitor> get_primary_monitor(Glib::RefPtr<Gdk::Display> const &display)
{
    auto display_c = display->gobj();

    GdkMonitor *monitor = nullptr;
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY(display_c)) {
        monitor = gdk_x11_display_get_primary_monitor(display_c);
    }
#endif
#ifdef GDK_WINDOWING_WIN32
    if (GDK_IS_WIN32_DISPLAY(display_c)) {
        monitor = gdk_win32_display_get_primary_monitor(display_c);
    }
#endif

    if (monitor) {
        return Glib::wrap(monitor, true);
    }

    // Fallback to monitor number 0 if the user hasn't configured a primary monitor,
    // or if the backend doesn't support it.
    return display->get_monitors()->get_typed_object<Gdk::Monitor>(0);
}

} // namespace

/** get monitor geometry of primary monitor */
Gdk::Rectangle get_monitor_geometry_primary() {
    Gdk::Rectangle monitor_geometry;
    auto const display = Gdk::Display::get_default();
    auto monitor = get_primary_monitor(display);

    monitor->get_geometry(monitor_geometry);
    return monitor_geometry;
}

/** get monitor geometry of monitor containing largest part of surface */
Gdk::Rectangle get_monitor_geometry_at_surface(Glib::RefPtr<Gdk::Surface> const &surface) {
    Gdk::Rectangle monitor_geometry;
    auto const display = Gdk::Display::get_default();
    auto const monitor = display->get_monitor_at_surface(surface);
    monitor->get_geometry(monitor_geometry);
    return monitor_geometry;
}

/** get monitor geometry of monitor at (or closest to) point on combined screen area */
Gdk::Rectangle get_monitor_geometry_at_point(int x, int y) {
    Gdk::Rectangle monitor_geometry;
    double dist = std::numeric_limits<double>::max();
    auto const display = Gdk::Display::get_default();
    for (unsigned i = 0; i < display->get_monitors()->get_n_items(); ++i) {
        auto monitor = display->get_monitors()->get_typed_object<Gdk::Monitor>(i);
        Gdk::Rectangle tmp_monitor_geometry;
        monitor->get_geometry(tmp_monitor_geometry);
        double cdist = Geom::distance(Geom::Point(x, y),
            Geom::Rect(tmp_monitor_geometry.get_x(),
                       tmp_monitor_geometry.get_y(),
                       tmp_monitor_geometry.get_width(),
                       tmp_monitor_geometry.get_height()));
        if (cdist < dist) {
            monitor_geometry = tmp_monitor_geometry;
        }
    }
    return monitor_geometry;
}

} // namespace Inkscape::UI

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
