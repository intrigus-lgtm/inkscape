// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Provides a class that shows a temporary indicator on the canvas of where the snap was, and what kind of snap
 *
 * Authors:
 *   Johan Engelen
 *   Diederik van Lierop
 *
 * Copyright (C) Johan Engelen 2009 <j.b.c.engelen@utwente.nl>
 * Copyright (C) Diederik van Lierop 2010 - 2012 <mail@diedenrezi.nl>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <glibmm/i18n.h>
#include <string>
#include <iomanip>
#include <unordered_map>

#include "snap-indicator.h"

#include "desktop.h"
#include "enums.h"
#include "preferences.h"
#include "util/units.h"
#include "document.h"


#include "canvas-item-ctrl.h"
#include "canvas-item-rect.h"
#include "canvas-item-text.h"
#include "canvas-item-curve.h"

#include "ui/tools/measure-tool.h"
#include "util-string/ustring-format.h"

#define DISTANCE_BG_RADIUS 0.3

namespace Inkscape {
namespace Display {

static std::map<SnapSourceType, Glib::ustring> source2string = {
    {SNAPSOURCE_UNDEFINED, _("UNDEFINED")},
    {SNAPSOURCE_BBOX_CORNER, _("Bounding box corner")},
    {SNAPSOURCE_BBOX_MIDPOINT, _("Bounding box midpoint")},
    {SNAPSOURCE_BBOX_EDGE_MIDPOINT, _("Bounding box side midpoint")},
    {SNAPSOURCE_NODE_SMOOTH, _("Smooth node")},
    {SNAPSOURCE_NODE_CUSP, _("Cusp node")},
    {SNAPSOURCE_LINE_MIDPOINT, _("Line midpoint")},
    {SNAPSOURCE_PATH_INTERSECTION, _("Path intersection")},
    {SNAPSOURCE_RECT_CORNER, _("Corner")},
    {SNAPSOURCE_CONVEX_HULL_CORNER, _("Convex hull corner")},
    {SNAPSOURCE_ELLIPSE_QUADRANT_POINT, _("Quadrant point")},
    {SNAPSOURCE_NODE_HANDLE, _("Handle")},
    {SNAPSOURCE_GUIDE, _("Guide")},
    {SNAPSOURCE_GUIDE_ORIGIN, _("Guide origin")},
    {SNAPSOURCE_ROTATION_CENTER, _("Object rotation center")},
    {SNAPSOURCE_OBJECT_MIDPOINT, _("Object midpoint")},
    {SNAPSOURCE_IMG_CORNER, _("Corner")},
    {SNAPSOURCE_TEXT_ANCHOR, _("Text anchor")},
    {SNAPSOURCE_OTHER_HANDLE, _("Handle")},
    {SNAPSOURCE_GRID_PITCH, _("Multiple of grid spacing")},
    {SNAPSOURCE_PAGE_CORNER, _("Page corner")},
    {SNAPSOURCE_PAGE_CENTER, _("Page center")},
};

static std::map<SnapTargetType, Glib::ustring> target2string = {
    {SNAPTARGET_UNDEFINED, _("UNDEFINED")},
    {SNAPTARGET_BBOX_CORNER, _("bounding box corner")},
    {SNAPTARGET_BBOX_EDGE, _("bounding box side")},
    {SNAPTARGET_BBOX_EDGE_MIDPOINT, _("bounding box side midpoint")},
    {SNAPTARGET_BBOX_MIDPOINT, _("bounding box midpoint")},
    {SNAPTARGET_NODE_SMOOTH, _("smooth node")},
    {SNAPTARGET_NODE_CUSP, _("cusp node")},
    {SNAPTARGET_LINE_MIDPOINT, _("line midpoint")},
    {SNAPTARGET_PATH, _("path")},
    {SNAPTARGET_PATH_PERPENDICULAR, _("path (perpendicular)")},
    {SNAPTARGET_PATH_TANGENTIAL, _("path (tangential)")},
    {SNAPTARGET_PATH_INTERSECTION, _("path intersection")},
    {SNAPTARGET_PATH_GUIDE_INTERSECTION, _("guide-path intersection")},
    {SNAPTARGET_PATH_CLIP, _("clip-path")},
    {SNAPTARGET_PATH_MASK, _("mask-path")},
    {SNAPTARGET_ELLIPSE_QUADRANT_POINT, _("quadrant point")},
    {SNAPTARGET_RECT_CORNER, _("corner")},
    {SNAPTARGET_GRID_LINE, _("grid line")},
    {SNAPTARGET_GRID_INTERSECTION, _("grid intersection")},
    {SNAPTARGET_GRID_PERPENDICULAR, _("grid line (perpendicular)")},
    {SNAPTARGET_GUIDE, _("guide")},
    {SNAPTARGET_GUIDE_INTERSECTION, _("guide intersection")},
    {SNAPTARGET_GUIDE_ORIGIN, _("guide origin")},
    {SNAPTARGET_GUIDE_PERPENDICULAR, _("guide (perpendicular)")},
    {SNAPTARGET_GRID_GUIDE_INTERSECTION, _("grid-guide intersection")},
    {SNAPTARGET_PAGE_EDGE_BORDER, _("page border")},
    {SNAPTARGET_PAGE_EDGE_CORNER, _("page corner")},
    {SNAPTARGET_PAGE_EDGE_CENTER, _("page center")},
    {SNAPTARGET_PAGE_MARGIN_BORDER, _("page margin border")},
    {SNAPTARGET_PAGE_MARGIN_CORNER, _("page margin corner")},
    {SNAPTARGET_PAGE_MARGIN_CENTER, _("page margin center")},
    {SNAPTARGET_PAGE_BLEED_BORDER, _("page bleed border")},
    {SNAPTARGET_PAGE_BLEED_CORNER, _("page bleed corner")},
    {SNAPTARGET_OBJECT_MIDPOINT, _("object midpoint")},
    {SNAPTARGET_IMG_CORNER, _("corner")},
    {SNAPTARGET_ROTATION_CENTER, _("object rotation center")},
    {SNAPTARGET_TEXT_ANCHOR, _("text anchor")},
    {SNAPTARGET_TEXT_BASELINE, _("text baseline")},
    {SNAPTARGET_CONSTRAINED_ANGLE, _("constrained angle")},
    {SNAPTARGET_CONSTRAINT, _("constraint")},
};

SnapIndicator::SnapIndicator(SPDesktop * desktop)
    :   _snaptarget(nullptr),
        _snaptarget_tooltip(nullptr),
        _snaptarget_bbox(nullptr),
        _snapsource(nullptr),
        _snaptarget_is_presnap(false),
        _desktop(desktop)
{
}

SnapIndicator::~SnapIndicator()
{
    // remove item that might be present
    remove_snaptarget();
    remove_snapsource();
}

void
SnapIndicator::set_new_snaptarget(Inkscape::SnappedPoint const &p, bool pre_snap)
{
    remove_snaptarget(); //only display one snaptarget at a time

    g_assert(_desktop != nullptr);

    if (!p.getSnapped()) {
        return; // If we haven't snapped, then it is of no use to draw a snapindicator
    }

    if (p.getTarget() == SNAPTARGET_CONSTRAINT) {
        // This is not a real snap, although moving along the constraint did affect the mouse pointer's position.
        // Maybe we should only show a snap indicator when the user explicitly asked for a constraint by pressing ctrl?
        // We should not show a snap indicator when stretching a selection box, which is also constrained. That would be
        // too much information.
        return;
    }

    bool is_alignment = p.getAlignmentTarget().has_value();
    bool is_distribution = p.getTarget() & SNAPTARGET_DISTRIBUTION_CATEGORY;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    double scale = prefs->getDouble("/tools/measure/scale", 100.0) / 100.0;

    bool value = prefs->getBool("/options/snapindicator/value", true);

    if (value) {
        Glib::ustring target_name = _("UNDEFINED");
        Glib::ustring source_name = _("UNDEFINED");

        if (!is_alignment && !is_distribution) {
            if (target2string.find(p.getTarget()) == target2string.end())
                g_warning("Target type %i not present in target2string", p.getTarget());

            if (source2string.find(p.getSource()) == source2string.end())
                g_warning("Source type %i not present in target2string", p.getSource());

            target_name = _(target2string[p.getTarget()].c_str());
            source_name = _(source2string[p.getSource()].c_str());
        }
        //std::cout << "Snapped " << source_name << " to " << target_name << std::endl;

        remove_snapsource(); // Don't set both the source and target indicators, as these will overlap

        double timeout_val = prefs->getDouble("/options/snapindicatorpersistence/value", 2.0);
        if (timeout_val < 0.1) {
            timeout_val = 0.1; // a zero value would mean infinite persistence (i.e. until new snap occurs)
            // Besides, negatives values would ....?
        }

        // TODO: should this be a constant or a separate prefrence
        // we are using the preference of measure tool here.
        double fontsize = prefs->getDouble("/tools/measure/fontsize", 10.0);

        if (is_distribution) {
            make_distribution_indicators(p, fontsize, scale);
        }

        if (is_alignment) {
            auto color = pre_snap ? 0x7f7f7fff : get_guide_color(p.getAlignmentTargetType());
            make_alignment_indicator(p.getPoint(), *p.getAlignmentTarget(), color, fontsize, scale);
            if (p.getAlignmentTargetType() == SNAPTARGET_ALIGNMENT_INTERSECTION) {
                make_alignment_indicator(p.getPoint(), *p.getAlignmentTarget2(), color, fontsize, scale);
            }
        } 

        _snaptarget_is_presnap = pre_snap;
 
        // Display the snap indicator (i.e. the cross)
        Inkscape::CanvasItemCtrl *ctrl;

        if (!is_alignment && !is_distribution) {
            // Display snap indicator at snap target
            ctrl = new Inkscape::CanvasItemCtrl(_desktop->getCanvasTemp(), Inkscape::CANVAS_ITEM_CTRL_TYPE_MARKER, p.getPoint());
            ctrl->set_selected(!pre_snap);

            _snaptarget = _desktop->add_temporary_canvasitem(ctrl, timeout_val*1000.0);
            // The snap indicator will be deleted after some time-out, and sp_canvas_item_dispose
            // will be called. This will set canvas->current_item to NULL if the snap indicator was
            // the current item, after which any events will go to the root handler instead of any
            // item handler. Dragging an object which has just snapped might therefore not be possible
            // without selecting / repicking it again. To avoid this, we make sure here that the
            // snap indicator will never be picked, and will therefore never be the current item.
            // Reported bugs:
            //   - scrolling when hovering above a pre-snap indicator won't work (for example)
            //     (https://bugs.launchpad.net/inkscape/+bug/522335/comments/8)
            //   - dragging doesn't work without repicking
            //     (https://bugs.launchpad.net/inkscape/+bug/1420301/comments/15)
            ctrl->set_pickable(false);

            // Display the tooltip, which reveals the type of snap source and the type of snap target
            Glib::ustring tooltip_str;
            if ( (p.getSource() != SNAPSOURCE_GRID_PITCH) && (p.getTarget() != SNAPTARGET_UNDEFINED) ) {
                tooltip_str = source_name + _(" to ") + target_name;
            } else if (p.getSource() != SNAPSOURCE_UNDEFINED) {
                tooltip_str = source_name;
            }


            if (!tooltip_str.empty()) {
                Geom::Point tooltip_pos = p.getPoint();
                if (dynamic_cast<Inkscape::UI::Tools::MeasureTool *>(_desktop->getTool())) {
                    // Make sure that the snap tooltips do not overlap the ones from the measure tool
                    tooltip_pos += _desktop->w2d(Geom::Point(0, -3*fontsize));
                } else {
                    tooltip_pos += _desktop->w2d(Geom::Point(0, -2*fontsize));
                }

                auto canvas_tooltip = new Inkscape::CanvasItemText(_desktop->getCanvasTemp(), tooltip_pos, tooltip_str);
                canvas_tooltip->set_fontsize(fontsize);
                canvas_tooltip->set_fill(0xffffffff);
                canvas_tooltip->set_background(pre_snap ? 0x33337f40 : 0x33337f7f);

                _snaptarget_tooltip = _desktop->add_temporary_canvasitem(canvas_tooltip, timeout_val*1000.0);
            }

            // Display the bounding box, if we snapped to one
            Geom::OptRect const bbox = p.getTargetBBox();
            if (bbox) {
                auto box = new Inkscape::CanvasItemRect(_desktop->getCanvasTemp(), *bbox);
                box->set_stroke(pre_snap ? 0x7f7f7fff : 0xff0000ff);
                box->set_dashed(true);
                box->set_pickable(false); // Is false by default.
                box->lower_to_bottom();
                _snaptarget_bbox = _desktop->add_temporary_canvasitem(box, timeout_val*1000.0);
            }
        }
    }
}

void
SnapIndicator::remove_snaptarget(bool only_if_presnap)
{
    if (only_if_presnap && !_snaptarget_is_presnap) {
        return;
    }

    if (_snaptarget) {
        _desktop->remove_temporary_canvasitem(_snaptarget);
        _snaptarget = nullptr;
        _snaptarget_is_presnap = false;
    }

    if (_snaptarget_tooltip) {
        _desktop->remove_temporary_canvasitem(_snaptarget_tooltip);
        _snaptarget_tooltip = nullptr;
    }

    if (_snaptarget_bbox) {
        _desktop->remove_temporary_canvasitem(_snaptarget_bbox);
        _snaptarget_bbox = nullptr;
    }

    for (auto *item : _alignment_snap_indicators) {
        _desktop->remove_temporary_canvasitem(item);
    }
    _alignment_snap_indicators.clear();

    for (auto *item : _distribution_snap_indicators) {
        _desktop->remove_temporary_canvasitem(item);
    }
    _distribution_snap_indicators.clear();
}

void
SnapIndicator::set_new_snapsource(Inkscape::SnapCandidatePoint const &p)
{
    remove_snapsource();

    g_assert(_desktop != nullptr); // If this fails, then likely setup() has not been called on the snap manager (see snap.cpp -> setup())

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool value = prefs->getBool("/options/snapindicator/value", true);

    if (value) {
        auto ctrl = new Inkscape::CanvasItemCtrl(_desktop->getCanvasTemp(), Inkscape::CANVAS_ITEM_CTRL_TYPE_POINT);
        ctrl->set_position(p.getPoint());
        _snapsource = _desktop->add_temporary_canvasitem(ctrl, 1000);
    }
}

void
SnapIndicator::set_new_debugging_point(Geom::Point const &p)
{
    g_assert(_desktop != nullptr);
    auto ctrl = new Inkscape::CanvasItemCtrl(_desktop->getCanvasTemp(), Inkscape::CANVAS_ITEM_CTRL_TYPE_SHAPER, p);
    ctrl->set_size(Inkscape::HandleSize::LARGE);
    _debugging_points.push_back(_desktop->add_temporary_canvasitem(ctrl, 5000));
}

void
SnapIndicator::remove_snapsource()
{
    if (_snapsource) {
        _desktop->remove_temporary_canvasitem(_snapsource);
        _snapsource = nullptr;
    }
}

void
SnapIndicator::remove_debugging_points()
{
    for (std::list<TemporaryItem *>::const_iterator i = _debugging_points.begin(); i != _debugging_points.end(); ++i) {
        _desktop->remove_temporary_canvasitem(*i);
    }
    _debugging_points.clear();
}

guint32 SnapIndicator::get_guide_color(SnapTargetType t)
{
    switch(t) {
        case SNAPTARGET_ALIGNMENT_BBOX_CORNER:
        case SNAPTARGET_ALIGNMENT_BBOX_MIDPOINT:
        case SNAPTARGET_ALIGNMENT_BBOX_EDGE_MIDPOINT:
            return 0xff0000ff;
        case SNAPTARGET_ALIGNMENT_PAGE_EDGE_CENTER:
        case SNAPTARGET_ALIGNMENT_PAGE_EDGE_CORNER:
        case SNAPTARGET_ALIGNMENT_PAGE_MARGIN_CENTER:
        case SNAPTARGET_ALIGNMENT_PAGE_MARGIN_CORNER:
        case SNAPTARGET_ALIGNMENT_PAGE_BLEED_CORNER:
            return 0x00ff00ff;
        case SNAPTARGET_ALIGNMENT_HANDLE:
            return 0x0000ffff;
        case SNAPTARGET_ALIGNMENT_INTERSECTION:
            return 0xd13bd1ff;
        default:
            g_warning("Alignment guide color not handled %i", t);
            return 0x000000ff;
    }
}

std::pair<Geom::Coord, int> get_y_and_sign(Geom::Rect const &source, Geom::Rect const &target, double const offset)
{
    Geom::Coord y;
    int sign;

    // We add a margin of 5px here to make sure that very small movements of mouse
    // pointer do not cause the position of distribution indicator to change.
    if (source.midpoint().y() < target.midpoint().y() + 5) {
        y = source.max().y() + offset;
        sign = 1;
    } else {
        y = source.min().y() - offset;
        sign = -1;
    }

    return {y, sign};
}

std::pair<Geom::Coord, int> get_x_and_sign(Geom::Rect const &source, Geom::Rect const &target, double const offset)
{
    Geom::Coord x;
    int sign;

    // We add a margin of 5px here to make sure that very small movements of mouse
    // pointer do not cause the position of distribution indicator to change.
    if (source.midpoint().x() < target.midpoint().x() + 5) {
        x = source.max().x() + offset;
        sign = 1;
    } else {
        x = source.min().x() - offset;
        sign = -1;
    }

    return {x, sign};
}

void SnapIndicator::make_alignment_indicator(Geom::Point const &p1, Geom::Point const &p2, guint32 color, double fontsize, double scale)
{
    //make sure the line is straight
    g_assert(p1.x() == p2.x() || p1.y() == p2.y());

    Preferences *prefs = Preferences::get();
    bool show_distance = prefs->getBool("/options/snapindicatordistance/value", false);

    Inkscape::CanvasItemCurve *line;

    auto ctrl = new Inkscape::CanvasItemCtrl(_desktop->getCanvasTemp(), Inkscape::CANVAS_ITEM_CTRL_TYPE_POINT);
    ctrl->set_position(p1);
    ctrl->set_pickable(false);
    _alignment_snap_indicators.push_back(_desktop->add_temporary_canvasitem(ctrl, 0));

    ctrl = new Inkscape::CanvasItemCtrl(_desktop->getCanvasTemp(), Inkscape::CANVAS_ITEM_CTRL_TYPE_POINT);
    ctrl->set_position(p2);
    ctrl->set_pickable(false);
    _alignment_snap_indicators.push_back(_desktop->add_temporary_canvasitem(ctrl, 0));

    auto dist = Geom::L2(p2 - p1);
    double offset = (fontsize + 5) / _desktop->current_zoom();
    if (show_distance && dist > 2 * offset) {
        auto direction = Geom::unit_vector(p1 - p2);
        auto text_pos = (p1 + p2)/2;

        Glib::ustring unit_name = _desktop->doc()->getDisplayUnit()->abbr.c_str();
        if (!unit_name.compare("")) {
            unit_name = DEFAULT_UNIT_NAME;
        }

        dist = Inkscape::Util::Quantity::convert(dist, "px", unit_name);

        Glib::ustring distance = Inkscape::ustring::format_classic(std::fixed, std::setprecision(1), std::noshowpoint, scale*dist);
        
        auto text = new Inkscape::CanvasItemText(_desktop->getCanvasTemp(), text_pos, distance);
        text->set_fontsize(fontsize);
        text->set_fill(color);
        text->set_background(0xffffffc8);
        text->set_bg_radius(DISTANCE_BG_RADIUS);
        text->set_anchor({0.5, 0.5});
        _alignment_snap_indicators.push_back(_desktop->add_temporary_canvasitem(text, 0));

        auto temp_point = text_pos + offset*direction;
        line = new Inkscape::CanvasItemCurve(_desktop->getCanvasTemp(), p1, temp_point);
        line->set_stroke(color);
        line->set_bg_alpha(1.0f);
        _alignment_snap_indicators.push_back(_desktop->add_temporary_canvasitem(line, 0));

        temp_point = text_pos - offset*direction;
        line = new Inkscape::CanvasItemCurve(_desktop->getCanvasTemp(), temp_point, p2);
        line->set_stroke(color);
        line->set_bg_alpha(1.0f);
        _alignment_snap_indicators.push_back(_desktop->add_temporary_canvasitem(line, 0));
    } else {
        line = new Inkscape::CanvasItemCurve(_desktop->getCanvasTemp(), p1, p2);
        line->set_stroke(color);
        line->set_bg_alpha(1.0f);
        _alignment_snap_indicators.push_back(_desktop->add_temporary_canvasitem(line, 0));
    }
}

Inkscape::CanvasItemCurve* SnapIndicator::make_stub_line_v(Geom::Point const & p) 
{
    Geom::Coord length = 10/_desktop->current_zoom();
    auto line = new Inkscape::CanvasItemCurve(_desktop->getCanvasTemp(), p + Geom::Point(0, length/2), p - Geom::Point(0, length/2));
    line->set_stroke(0xff5f1fff);
    return line;
}

Inkscape::CanvasItemCurve* SnapIndicator::make_stub_line_h(Geom::Point const & p) 
{
    Geom::Coord length = 10/_desktop->current_zoom();
    auto line = new Inkscape::CanvasItemCurve(_desktop->getCanvasTemp(), p + Geom::Point(length/2, 0), p - Geom::Point(length/2, 0));
    line->set_stroke(0xff5f1fff);
    return line;
}

void SnapIndicator::make_distribution_indicators(SnappedPoint const &p,
                                                double fontsize,
                                                double scale)
{
    Preferences *prefs = Preferences::get();
    bool show_distance = prefs->getBool("/options/snapindicatordistance/value", false);

    guint32 color = 0xff5f1fff;
    guint32 text_fill = 0xffffffff;
    guint32 text_bg = 0xff5f1fff; //0x33337f7f
    Geom::Point text_pos;
    double text_offset = (fontsize * 2);
    // double line_offset = 5/_desktop->current_zoom();
    double line_offset = 0;

    Glib::ustring unit_name = _desktop->doc()->getDisplayUnit()->abbr.c_str();
    if (!unit_name.compare("")) {
        unit_name = DEFAULT_UNIT_NAME;
    }
    auto equal_dist = Inkscape::Util::Quantity::convert(p.getDistributionDistance(), "px", unit_name);
    Glib::ustring distance = Inkscape::ustring::format_classic(std::fixed, std::setprecision(1), std::noshowpoint, scale*equal_dist);

    switch (p.getTarget()) {
        case SNAPTARGET_DISTRIBUTION_Y:
        case SNAPTARGET_DISTRIBUTION_X:
        case SNAPTARGET_DISTRIBUTION_RIGHT:
        case SNAPTARGET_DISTRIBUTION_LEFT:
        case SNAPTARGET_DISTRIBUTION_UP:
        case SNAPTARGET_DISTRIBUTION_DOWN: {
            Geom::Point p1, p2;
            Inkscape::CanvasItemCurve *point1, *point2;

            for (auto it = p.getBBoxes().begin(); it + 1 != p.getBBoxes().end(); it++) {
                switch (p.getTarget()) {
                    case SNAPTARGET_DISTRIBUTION_RIGHT: 
                    case SNAPTARGET_DISTRIBUTION_LEFT:
                    case SNAPTARGET_DISTRIBUTION_X: {
                        auto [y, sign] = get_y_and_sign(*it, *std::next(it), 5/_desktop->current_zoom());
                        p1 = Geom::Point(it->max().x() + line_offset, y);
                        p2 = Geom::Point(std::next(it)->min().x() - line_offset, y);
                        text_pos = (p1 + p2)/2 + _desktop->w2d(Geom::Point(0, sign*text_offset));

                        point1 = make_stub_line_v(p1);
                        point2 = make_stub_line_v(p2);
                        break;
                    }

                    case SNAPTARGET_DISTRIBUTION_DOWN:
                    case SNAPTARGET_DISTRIBUTION_UP:
                    case SNAPTARGET_DISTRIBUTION_Y: {
                        auto [x, sign] = get_x_and_sign(*it, *std::next(it), 5/_desktop->current_zoom());
                        p1 = Geom::Point(x, it->max().y() + line_offset);
                        p2 = Geom::Point(x, std::next(it)->min().y() - line_offset);
                        text_pos = (p1 + p2)/2 + _desktop->w2d(Geom::Point(sign*text_offset, 0));

                        point1 = make_stub_line_h(p1);
                        point2 = make_stub_line_h(p2);
                        break;
                    }
                }

                _distribution_snap_indicators.push_back(_desktop->add_temporary_canvasitem(point1, 0));
                _distribution_snap_indicators.push_back(_desktop->add_temporary_canvasitem(point2, 0));

                auto line1 = new Inkscape::CanvasItemCurve(_desktop->getCanvasTemp(), p1, p2);
                line1->set_stroke(color);
                line1->set_width(2);
                _distribution_snap_indicators.push_back(_desktop->add_temporary_canvasitem(line1, 0));

                if (show_distance) {
                    auto text = new Inkscape::CanvasItemText(_desktop->getCanvasTemp(), text_pos, distance);
                    text->set_fontsize(fontsize);
                    text->set_fill(text_fill);
                    text->set_background(text_bg);
                    text->set_bg_radius(DISTANCE_BG_RADIUS);
                    text->set_anchor({0.5, 0.5});
                    _distribution_snap_indicators.push_back(_desktop->add_temporary_canvasitem(text, 0));
                }
            }
        break;
        }
        case SNAPTARGET_DISTRIBUTION_XY: {
            Geom::Point p1, p2;
            Inkscape::CanvasItemCurve *point1, *point2;

            auto equal_dist2 = Inkscape::Util::Quantity::convert(p.getDistributionDistance2(), "px", unit_name);
            Glib::ustring distance2 = Inkscape::ustring::format_classic(std::fixed, std::setprecision(1), std::noshowpoint, scale*equal_dist2);

            for (auto it = p.getBBoxes().begin(); it + 1 != p.getBBoxes().end(); it++) {
                auto [y, sign] = get_y_and_sign(*it, *std::next(it), 5/_desktop->current_zoom());
                p1 = Geom::Point(it->max().x() + line_offset, y);
                p2 = Geom::Point(std::next(it)->min().x() - line_offset, y);
                text_pos = (p1 + p2)/2 + _desktop->w2d(Geom::Point(0, sign*text_offset));

                point1 = make_stub_line_v(p1);
                point2 = make_stub_line_v(p2);

                _distribution_snap_indicators.push_back(_desktop->add_temporary_canvasitem(point1, 0));
                _distribution_snap_indicators.push_back(_desktop->add_temporary_canvasitem(point2, 0));

                auto line1 = new Inkscape::CanvasItemCurve(_desktop->getCanvasTemp(), p1, p2);
                line1->set_stroke(color);
                line1->set_width(2);
                _distribution_snap_indicators.push_back(_desktop->add_temporary_canvasitem(line1, 0));

                if (show_distance) {
                    auto text = new Inkscape::CanvasItemText(_desktop->getCanvasTemp(), text_pos, distance);
                    text->set_fontsize(fontsize);
                    text->set_fill(text_fill);
                    text->set_background(text_bg);
                    text->set_bg_radius(DISTANCE_BG_RADIUS);
                    text->set_anchor({0.5, 0.5});
                    _distribution_snap_indicators.push_back(_desktop->add_temporary_canvasitem(text, 0));
                }
            }

            for (auto it = p.getBBoxes2().begin(); it + 1 != p.getBBoxes2().end(); it++) {
                auto [x, sign] = get_x_and_sign(*it, *std::next(it), 5/_desktop->current_zoom());
                p1 = Geom::Point(x, it->max().y() + line_offset);
                p2 = Geom::Point(x, std::next(it)->min().y() - line_offset);
                text_pos = (p1 + p2)/2 + _desktop->w2d(Geom::Point(sign*text_offset, 0));

                point1 = make_stub_line_h(p1);
                point2 = make_stub_line_h(p2);

                _distribution_snap_indicators.push_back(_desktop->add_temporary_canvasitem(point1, 0));
                _distribution_snap_indicators.push_back(_desktop->add_temporary_canvasitem(point2, 0));

                auto line1 = new Inkscape::CanvasItemCurve(_desktop->getCanvasTemp(), p1, p2);
                line1->set_stroke(color);
                line1->set_width(2);
                _distribution_snap_indicators.push_back(_desktop->add_temporary_canvasitem(line1, 0));

                if (show_distance) {
                    auto text = new Inkscape::CanvasItemText(_desktop->getCanvasTemp(), text_pos, distance2);
                    text->set_fontsize(fontsize);
                    text->set_fill(text_fill);
                    text->set_background(text_bg);
                    text->set_bg_radius(DISTANCE_BG_RADIUS);
                    text->set_anchor({0.5, 0.5});
                    _distribution_snap_indicators.push_back(_desktop->add_temporary_canvasitem(text, 0));
                }
            }

            break;
        }
    }
}

} //namespace Display
} /* namespace Inkscape */


/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=4:softtabstop=4 :
