// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LPEToolContext: a context for a generic tool composed of subtools that are given by LPEs
 *
 * Authors:
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Abhishek Sharma
 *
 * Copyright (C) 1998 The Free Software Foundation
 * Copyright (C) 1999-2005 authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 * Copyright (C) 2008 Maximilian Albert
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <iomanip>

#include <glibmm/i18n.h>
#include <gtk/gtk.h>

#include <2geom/sbasis-geometric.h>

#include "desktop.h"
#include "document.h"
#include "message-stack.h"
#include "selection.h"

#include "display/curve.h"
#include "display/control/canvas-item-rect.h"
#include "display/control/canvas-item-text.h"

#include "object/sp-path.h"

#include "util/units.h"
#include "util-string/ustring-format.h"

#include "ui/toolbar/lpe-toolbar.h"
#include "ui/tools/lpe-tool.h"
#include "ui/shape-editor.h"
#include "ui/widget/events/canvas-event.h"

using Inkscape::Util::UnitTable;
using Inkscape::UI::Tools::PenTool;

int const num_subtools = 8;

SubtoolEntry const lpesubtools[] = {
    // this must be here to account for the "all inactive" action
    {Inkscape::LivePathEffect::INVALID_LPE, "draw-geometry-inactive"},
    {Inkscape::LivePathEffect::LINE_SEGMENT, "draw-geometry-line-segment"},
    {Inkscape::LivePathEffect::CIRCLE_3PTS, "draw-geometry-circle-from-three-points"},
    {Inkscape::LivePathEffect::CIRCLE_WITH_RADIUS, "draw-geometry-circle-from-radius"},
    {Inkscape::LivePathEffect::PARALLEL, "draw-geometry-line-parallel"},
    {Inkscape::LivePathEffect::PERP_BISECTOR, "draw-geometry-line-perpendicular"},
    {Inkscape::LivePathEffect::ANGLE_BISECTOR, "draw-geometry-angle-bisector"},
    {Inkscape::LivePathEffect::MIRROR_SYMMETRY, "draw-geometry-mirror"}
};

namespace Inkscape::UI::Tools {

LpeTool::LpeTool(SPDesktop *desktop)
    : PenTool(desktop, "/tools/lpetool", "geometric.svg")
{
    auto const selection = desktop->getSelection();
    auto const item = selection->singleItem();

    sel_changed_connection = selection->connectChanged(sigc::mem_fun(*this, &LpeTool::selection_changed));

    shape_editor = std::make_unique<ShapeEditor>(desktop);

    switch_mode(LivePathEffect::INVALID_LPE);
    reset_limiting_bbox();
    create_measuring_items();

// TODO temp force:
    enableSelectionCue();

    if (item) {
        shape_editor->set_item(item);
    }

    auto prefs = Preferences::get();
    if (prefs->getBool("/tools/lpetool/selcue")) {
        enableSelectionCue();
    }
}

LpeTool::~LpeTool() = default;

/**
 * Callback that processes the "changed" signal on the selection;
 * destroys old and creates new nodepath and reassigns listeners to the new selected item's repr.
 */
void LpeTool::selection_changed(Selection *selection)
{
    shape_editor->unset_item();
    shape_editor->set_item(selection->singleItem());
}

void LpeTool::set(Preferences::Entry const &val)
{
    if (val.getEntryName() == "mode") {
        Preferences::get()->setString("/tools/geometric/mode", "drag");
        PenTool::mode = PenTool::MODE_DRAG;
    }
}

bool LpeTool::item_handler(SPItem *item, CanvasEvent const &event)
{
    bool ret = false;

    inspect_event(event,
        [&] (ButtonPressEvent const &event) {
            if (event.num_press != 1 || event.button != 1) {
                return;
            }
            // select the clicked item but do nothing else
            auto const selection = _desktop->getSelection();
            selection->clear();
            selection->add(item);
            ret = true;
        },
        [&] (ButtonReleaseEvent const &event) {
            // TODO: do we need to catch this or can we pass it on to the parent handler?
            ret = true;
        },
        [&] (CanvasEvent const &event) {}
    );

    return ret || PenTool::item_handler(item, event);
}

bool LpeTool::root_handler(CanvasEvent const &event)
{
    if (hasWaitingLPE()) {
        // quit when we are waiting for a LPE to be applied
        return PenTool::root_handler(event);
    }

    auto const selection = _desktop->getSelection();

    bool ret = false;

    inspect_event(event,
        [&] (ButtonPressEvent const &event) {
            if (event.num_press == 1 && event.button == 1) {
                if (mode == LivePathEffect::INVALID_LPE) {
                    // don't do anything for now if we are inactive (except clearing the selection
                    // since this was a click into empty space)
                    selection->clear();
                    _desktop->messageStack()->flash(WARNING_MESSAGE, _("Choose a construction tool from the toolbar."));
                    ret = true;
                    return;
                }

                saveDragOrigin(event.pos);

                auto prefs = Preferences::get();
                int mode = prefs->getInt("/tools/lpetool/mode");
                auto type = lpesubtools[mode].type;

                waitForLPEMouseClicks(type, LivePathEffect::Effect::acceptsNumClicks(type));
            }
        },

        [&] (CanvasEvent const &event) {}
    );

    return ret || PenTool::root_handler(event);
}

/*
 * Finds the index in the list of geometric subtools corresponding to the given LPE type.
 * Returns -1 if no subtool is found.
 */
int lpetool_mode_to_index(LivePathEffect::EffectType const type)
{
    for (int i = 0; i < num_subtools; ++i) {
        if (lpesubtools[i].type == type) {
            return i;
        }
    }
    return -1;
}

/*
 * Checks whether an item has a construction applied as LPE and if so returns the index in
 * lpesubtools of this construction
 */
int lpetool_item_has_construction(SPItem *item)
{
    if (!is<SPLPEItem>(item)) {
        return -1;
    }

    auto lpe = cast<SPLPEItem>(item)->getCurrentLPE();
    if (!lpe) {
        return -1;
    }

    return lpetool_mode_to_index(lpe->effectType());
}

/*
 * Attempts to perform the construction of the given type (i.e., to apply the corresponding LPE) to
 * a single selected item. Returns whether we succeeded.
 */
bool lpetool_try_construction(SPDesktop *desktop, LivePathEffect::EffectType const type)
{
    auto const selection = desktop->getSelection();
    auto const item = selection->singleItem();

    // TODO: should we check whether type represents a valid geometric construction?
    if (item && is<SPLPEItem>(item) && LivePathEffect::Effect::acceptsNumClicks(type) == 0) {
        LivePathEffect::Effect::createAndApply(type, desktop->getDocument(), item);
        return true;
    }

    return false;
}

void LpeTool::switch_mode(LivePathEffect::EffectType const type)
{
    int index = lpetool_mode_to_index(type);
    if (index == -1) {
        g_warning("Invalid mode selected: %d", type);
        return;
    }

    mode = type;

    if (auto tb = dynamic_cast<UI::Toolbar::LPEToolbar*>(getDesktop()->get_toolbar_by_name("LPEToolToolbar"))) {
        tb->set_mode(index);
    } else {
        std::cerr << "Could not access LPE toolbar" << std::endl;
    }
}

std::pair<Geom::Point, Geom::Point> lpetool_get_limiting_bbox_corners(SPDocument const *document)
{
    auto const w = document->getWidth().value("px");
    auto const h = document->getHeight().value("px");

    auto prefs = Preferences::get();
    auto const ulx = prefs->getDouble("/tools/lpetool/bbox_upperleftx", 0);
    auto const uly = prefs->getDouble("/tools/lpetool/bbox_upperlefty", 0);
    auto const lrx = prefs->getDouble("/tools/lpetool/bbox_lowerrightx", w);
    auto const lry = prefs->getDouble("/tools/lpetool/bbox_lowerrighty", h);

    return {{ulx, uly}, {lrx, lry}};
}

/*
 * Reads the limiting bounding box from preferences and draws it on the screen
 */
// TODO: Note that currently the bbox is not user-settable; we simply use the page borders
void LpeTool::reset_limiting_bbox()
{
    canvas_bbox.reset();

    auto prefs = Preferences::get();
    if (!prefs->getBool("/tools/lpetool/show_bbox", true)) {
        return;
    }

    auto const document = _desktop->getDocument();

    auto [A, B] = lpetool_get_limiting_bbox_corners(document);
    auto const doc2dt = _desktop->doc2dt();
    A *= doc2dt;
    B *= doc2dt;

    canvas_bbox = make_canvasitem<CanvasItemRect>(_desktop->getCanvasControls(), Geom::Rect(A, B));
    canvas_bbox->set_stroke(0x0000ffff);
    canvas_bbox->set_dashed(true);
}

static void set_pos_and_anchor(CanvasItemText *canvas_text, Geom::Piecewise<Geom::D2<Geom::SBasis>> const &pwd2,
                               double t, double length)
{
    auto const pwd2_reparam = arc_length_parametrization(pwd2, 2, 0.1);
    auto const t_reparam = pwd2_reparam.cuts.back() * t;
    auto const pos = pwd2_reparam.valueAt(t_reparam);
    auto const dir = unit_vector(derivative(pwd2_reparam).valueAt(t_reparam));
    auto const n = -rot90(dir);
    auto const angle = Geom::angle_between(dir, Geom::Point(1, 0));

    canvas_text->set_coord(pos + n * length);
    canvas_text->set_anchor(Geom::Point(std::sin(angle), -std::cos(angle)));
}

void LpeTool::create_measuring_items(Selection *selection)
{
    if (!selection) {
        selection = _desktop->getSelection();
    }
    auto prefs = Preferences::get();
    bool show = prefs->getBool("/tools/lpetool/show_measuring_info",  true);

    auto tmpgrp = _desktop->getCanvasTemp();

    auto const &unit_table = Util::UnitTable::get();

    Util::Unit const *unit = nullptr;
    if (prefs->getString("/tools/lpetool/unit").compare("")) {
        unit = unit_table.getUnit(prefs->getString("/tools/lpetool/unit"));
    } else {
        unit = unit_table.getUnit("px");
    }

    for (auto item : selection->items()) {
        if (auto path = cast<SPPath>(item)) {
            SPCurve const *curve = path->curve();
            auto const pwd2 = paths_to_pw(curve->get_pathvector());

            double lengthval = Geom::length(pwd2);
            lengthval = Util::Quantity::convert(lengthval, "px", unit);

            auto arc_length = Inkscape::ustring::format_classic(std::setprecision(2), std::fixed, lengthval);
            arc_length += " ";
            arc_length += unit->abbr;

            auto canvas_text = make_canvasitem<CanvasItemText>(tmpgrp, Geom::Point(0,0), arc_length);
            set_pos_and_anchor(canvas_text.get(), pwd2, 0.5, 10);
            if (!show) {
                canvas_text->set_visible(false);
            }

            measuring_items[path] = std::move(canvas_text);
        }
    }
}

void LpeTool::delete_measuring_items()
{
    measuring_items.clear();
}

void LpeTool::update_measuring_items()
{
    auto prefs = Preferences::get();
    auto const &unit_table = Util::UnitTable::get();

    Util::Unit const *unit = nullptr;
    if (prefs->getString("/tools/lpetool/unit").compare("")) {
        unit = unit_table.getUnit(prefs->getString("/tools/lpetool/unit"));
    } else {
        unit = unit_table.getUnit("px");
    }

    for (auto &i : measuring_items) {
        SPPath *path = i.first;
        SPCurve const *curve = path->curve();
        auto const pwd2 = Geom::paths_to_pw(curve->get_pathvector());
        double lengthval = Geom::length(pwd2);
        lengthval = Util::Quantity::convert(lengthval, "px", unit);

        auto arc_length = Inkscape::ustring::format_classic(std::setprecision(2), std::fixed, lengthval);
        arc_length += " ";
        arc_length += unit->abbr;

        i.second->set_text(std::move(arc_length));
        set_pos_and_anchor(i.second.get(), pwd2, 0.5, 10);
    }
}

void LpeTool::show_measuring_info(bool show)
{
    for (auto &i : measuring_items) {
        i.second->set_visible(show);
    }
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
