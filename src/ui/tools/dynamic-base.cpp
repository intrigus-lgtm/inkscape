// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Common drawing mode. Base class of Eraser and Calligraphic tools.
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "ui/tools/dynamic-base.h"
#include "display/control/canvas-item-bpath.h"
#include "desktop.h"
#include "util/units.h"

using Inkscape::Util::Unit;
using Inkscape::Util::Quantity;

static constexpr double DRAG_MIN = 0.0;
static constexpr double DRAG_MAX = 1.0;

namespace Inkscape::UI::Tools {

DynamicBase::DynamicBase(SPDesktop *desktop, std::string &&prefs_path, std::string &&cursor_filename)
    : ToolBase(desktop, std::move(prefs_path), std::move(cursor_filename))
{
}

DynamicBase::~DynamicBase() = default;

void DynamicBase::set(Preferences::Entry const &value)
{
    auto const path = value.getEntryName();
    
    // ignore preset modifications
    auto const presets_path = getPrefsPath() + "/preset";
    auto const &full_path = value.getPath();

    auto prefs = Preferences::get();
    auto const unit = Util::UnitTable::get().getUnit(prefs->getString("/tools/calligraphic/unit"));

    if (full_path.compare(0, presets_path.size(), presets_path.c_str()) == 0) {
    	return;
    }

    if (path == "mass") {
        mass = 0.01 * std::clamp(value.getInt(10), 0, 100);
    } else if (path == "wiggle") {
        drag = std::clamp((1 - 0.01 * value.getInt()), DRAG_MIN, DRAG_MAX); // drag is inverse to wiggle
    } else if (path == "angle") {
        angle = std::clamp(value.getDouble(), -90.0, 90.0);
    } else if (path == "width") {
        width = 0.01 * std::clamp(value.getDouble(), Quantity::convert(0.001, unit, "px"), Quantity::convert(100, unit, "px"));
    } else if (path == "thinning") {
        vel_thin = 0.01 * std::clamp(value.getInt(10), -100, 100);
    } else if (path == "tremor") {
        tremor = 0.01 * std::clamp(value.getInt(), 0, 100);
    } else if (path == "flatness") {
        flatness = 0.01 * std::clamp(value.getInt(), -100, 100);
    } else if (path == "usepressure") {
        usepressure = value.getBool();
    } else if (path == "usetilt") {
        usetilt = value.getBool();
    } else if (path == "abs_width") {
        abs_width = value.getBool();
    } else if (path == "cap_rounding") {
        cap_rounding = value.getDouble();
    }
}

Geom::Point DynamicBase::getNormalizedPoint(Geom::Point const &v) const
{
    auto const drect = _desktop->get_display_area();
    double const max = drect.maxExtent();
    return (v - drect.bounds().min()) / max;
}

Geom::Point DynamicBase::getViewPoint(Geom::Point const &n) const
{
    auto const drect = _desktop->get_display_area();
    double const max = drect.maxExtent();
    return n * max + drect.bounds().min();
}

} // namespace Inkscape::UI::Tools

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
