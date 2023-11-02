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

#include <gdkmm/monitor.h>
#include <gdkmm/surface.h>

#include "include/gtkmm_version.h"

namespace Inkscape::UI {

/** get monitor geometry of primary monitor */
Gdk::Rectangle get_monitor_geometry_primary() {
    Gdk::Rectangle monitor_geometry;
    auto const display = Gdk::Display::get_default();
    auto monitor = display->get_primary_monitor();

    // Fallback to monitor number 0 if the user hasn't configured a primary monitor
    if (!monitor) {
        monitor = display->get_monitor(0);
    }

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
    auto const display = Gdk::Display::get_default();
    auto const monitor = display->get_monitor_at_point(x ,y);
    monitor->get_geometry(monitor_geometry);
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
