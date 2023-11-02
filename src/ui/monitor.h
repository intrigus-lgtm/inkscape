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

#ifndef SEEN_MONITOR_H
#define SEEN_MONITOR_H

#include <glibmm/refptr.h>
#include <gdkmm/rectangle.h>

namespace Gdk {
class Surface;
} // namespace Gdk

namespace Inkscape::UI {

Gdk::Rectangle get_monitor_geometry_primary();
Gdk::Rectangle get_monitor_geometry_at_surface(Glib::RefPtr<Gdk::Surface> const &surface);
Gdk::Rectangle get_monitor_geometry_at_point(int x, int y);

} // namespace Inkscape::UI

#endif // SEEN_MONITOR_H

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
