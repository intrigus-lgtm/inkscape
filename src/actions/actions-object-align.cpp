// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Gio::Actions for aligning and distributing objects without GUI.
 *
 * Copyright (C) 2020 Tavmjong Bah
 *
 * Some code and ideas from src/ui/dialogs/align-and-distribute.cpp
 *   Authors: Bryce Harrington
 *            Martin Owens
 *            John Smith
 *            Patrick Storz
 *            Jabier Arraiza
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 *
 */

#include "actions-helper.h"
#include "actions-object-align.h"

//#include <iostream>
#include <limits>

#include <giomm.h>  // Not <gtkmm.h>! To eventually allow a headless version!
#include <glibmm/i18n.h>

#include "document-undo.h"
#include "enums.h"                // Clones
#include "filter-chemistry.h"     // LPE bool
#include "inkscape-application.h"
#include "selection.h"
#include "text-editing.h"

#include "object/sp-text.h"
#include "object/sp-flowtext.h"

#include "object/algorithms/graphlayout.h"   // Graph layout objects.
#include "object/algorithms/removeoverlap.h" // Remove overlaps between objects.
#include "object/algorithms/unclump.h"       // Rearrange objects.
#include "object/algorithms/bboxsort.h"      // Sort based on bounding box.

#include "live_effects/effect-enum.h"
#include "live_effects/effect.h"

#include "object/sp-root.h"       // "Desktop Bounds"

#include "ui/icon-names.h"        // Icon macro used in undo.

enum class ObjectAlignTarget {
    LAST,
    FIRST,
    BIGGEST,
    SMALLEST,
    PAGE,
    DRAWING,
    SELECTION
};

void
object_align_on_canvas(InkscapeApplication *app)
{
    // Get Action
    auto *gapp = app->gio_app();
    auto action = gapp->lookup_action("object-align-on-canvas");
    if (!action) {
        show_output("object_align_on_canvas: action missing!");
        return;
    }

    auto saction = std::dynamic_pointer_cast<Gio::SimpleAction>(action);
    if (!saction) {
        show_output("object_align_on_canvas: action not SimpleAction!");
        return;
    }

    // Toggle state
    bool state = false;
    saction->get_state(state);
    state = !state;
    saction->change_state(state);

    // Toggle action
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setBool("/dialogs/align/oncanvas", state);
}

void
object_align(const Glib::VariantBase& value, InkscapeApplication *app)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::Variant<Glib::ustring> s = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring> >(value);
    std::vector<Glib::ustring> tokens = Glib::Regex::split_simple(" ", s.get());

    // Find out if we are using an anchor.
    bool anchor = std::find(tokens.begin(), tokens.end(), "anchor") != tokens.end();

    // Default values:
    auto target = ObjectAlignTarget::SELECTION;

    bool group = false;
    double mx0 = 0;
    double mx1 = 0;
    double my0 = 0;
    double my1 = 0;
    double sx0 = 0;
    double sx1 = 0;
    double sy0 = 0;
    double sy1 = 0;

    // Preference request allows alignment action to remember for key-presses
    if (std::find(tokens.begin(), tokens.end(), "pref") != tokens.end()) {
        group = prefs->getBool("/dialogs/align/sel-as-groups", false);
        tokens.push_back(prefs->getString("/dialogs/align/objects-align-to", "selection"));
    }
 
    // clang-format off
    for (auto const &token : tokens) {

        // Target
        if      (token == "last"     ) target = ObjectAlignTarget::LAST;
        else if (token == "first"    ) target = ObjectAlignTarget::FIRST;
        else if (token == "biggest"  ) target = ObjectAlignTarget::BIGGEST;
        else if (token == "smallest" ) target = ObjectAlignTarget::SMALLEST;
        else if (token == "page"     ) target = ObjectAlignTarget::PAGE;
        else if (token == "drawing"  ) target = ObjectAlignTarget::DRAWING;
        else if (token == "selection") target = ObjectAlignTarget::SELECTION;

        // Group
        else if (token == "group")     group = true;

        // Position
        if (!anchor) {
            if      (token == "left"    ) { mx0 = 1.0; mx1 = 0.0; sx0 = 1.0; sx1 = 0.0; }
            else if (token == "hcenter" ) { mx0 = 0.5; mx1 = 0.5; sx0 = 0.5; sx1 = 0.5; }
            else if (token == "right"   ) { mx0 = 0.0; mx1 = 1.0; sx0 = 0.0; sx1 = 1.0; }

            else if (token == "top"     ) { my0 = 1.0; my1 = 0.0; sy0 = 1.0; sy1 = 0.0; }
            else if (token == "vcenter" ) { my0 = 0.5; my1 = 0.5; sy0 = 0.5; sy1 = 0.5; }
            else if (token == "bottom"  ) { my0 = 0.0; my1 = 1.0; sy0 = 0.0; sy1 = 1.0; }
        } else {
            if      (token == "left"    ) { mx0 = 0.0; mx1 = 1.0; sx0 = 1.0; sx1 = 0.0; }
            else if (token == "hcenter" ) show_output("'anchor' cannot be used with 'hcenter'");
            else if (token == "right"   ) { mx0 = 1.0; mx1 = 0.0; sx0 = 0.0; sx1 = 1.0; }

            else if (token == "top"     ) { my0 = 0.0; my1 = 1.0; sy0 = 1.0; sy1 = 0.0; }
            else if (token == "vcenter" ) show_output("'anchor' cannot be used with 'vcenter'");
            else if (token == "bottom"  ) { my0 = 1.0; my1 = 0.0; sy0 = 0.0; sy1 = 1.0; }
        }
    }
    // clang-format on

    auto selection = app->get_active_selection();

    // We should not have to do this!
    auto document  = app->get_active_document();
    selection->setDocument(document);

    // We force unselect operand in bool LPE. TODO: See if we can use "selected" from below.
    auto list = selection->items();
    std::size_t total = std::distance(list.begin(), list.end());
    std::vector<SPItem *> selected;
    std::vector<Inkscape::LivePathEffect::Effect *> bools;
    for (auto item : list) {
        if (total == 2) {
            auto lpeitem = cast<SPLPEItem>(item);
            if (lpeitem) {
                for (auto lpe : lpeitem->getPathEffectsOfType(Inkscape::LivePathEffect::EffectType::BOOL_OP)) {
                    if (!g_strcmp0(lpe->getRepr()->attribute("is_visible"), "true")) {
                        lpe->getRepr()->setAttribute("is_visible", "false");
                        bools.emplace_back(lpe);
                        item->document->ensureUpToDate();
                    }
                }
            }
        }
        if (!(item && has_hidder_filter(item) && total > 2)) {
            selected.emplace_back(item);
        }
    }

    if (selected.empty()) return;

    // Find alignment rectangle. This can come from:
    // - The bounding box of an object
    // - The bounding box of a group of objects
    // - The bounding box of the page, drawing, or selection.
    SPItem *focus = nullptr;
    Geom::OptRect b = Geom::OptRect();
    Inkscape::Selection::CompareSize direction = (mx0 != 0.0 || mx1 != 0.0) ? Inkscape::Selection::VERTICAL : Inkscape::Selection::HORIZONTAL;

    switch (target) {
        case ObjectAlignTarget::LAST:
            focus = selected.back();
            break;
        case ObjectAlignTarget::FIRST:
            focus = selected.front();
            break;
        case ObjectAlignTarget::BIGGEST:
            focus = selection->largestItem(direction);
            break;
        case ObjectAlignTarget::SMALLEST:
            focus = selection->smallestItem(direction);
            break;
        case ObjectAlignTarget::PAGE:
            b = document->pageBounds();
            break;
        case ObjectAlignTarget::DRAWING:
            b = document->getRoot()->desktopPreferredBounds();
            break;
        case ObjectAlignTarget::SELECTION:
            b = selection->preferredBounds();
            break;
        default:
            g_assert_not_reached ();
            break;
    };

    if (focus) {
        b = focus->desktopPreferredBounds();
    }

    g_return_if_fail(b);

    if (auto desktop = selection->desktop(); desktop && !desktop->is_yaxisdown()) {
         std::swap(my0, my1);
         std::swap(sy0, sy1);
    }

    // Generate the move point from the selected bounding box
    Geom::Point mp = Geom::Point(mx0 * b->min()[Geom::X] + mx1 * b->max()[Geom::X],
                                 my0 * b->min()[Geom::Y] + my1 * b->max()[Geom::Y]);

    if (group) {
        if (focus) {
            // Use bounding box of all selected elements except the "focused" element.
            Inkscape::ObjectSet copy(document);
            copy.add(selection->objects().begin(), selection->objects().end());
            copy.remove(focus);
            b = copy.preferredBounds();
        } else {
            // Use bounding box of all selected elements.
            b = selection->preferredBounds();
        }
    }

    // Move each item in the selected list separately.
    bool changed = false;
    for (auto item : selected) {
    	document->ensureUpToDate();

        if (!group) {
            b = (item)->desktopPreferredBounds();
        }

        if (b && (!focus || (item) != focus)) {
            Geom::Point const sp(sx0 * b->min()[Geom::X] + sx1 * b->max()[Geom::X],
                                 sy0 * b->min()[Geom::Y] + sy1 * b->max()[Geom::Y]);
            Geom::Point const mp_rel( mp - sp );
            if (LInfty(mp_rel) > 1e-9) {
                item->move_rel(Geom::Translate(mp_rel));
                changed = true;
            }
        }
    }

    if (changed) {
        Inkscape::DocumentUndo::done(document, _("Align"), INKSCAPE_ICON("dialog-align-and-distribute"));
    }
}

void
object_distribute(const Glib::VariantBase& value, InkscapeApplication *app)
{
    Glib::Variant<Glib::ustring> s = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring> >(value);
    auto token = s.get();

    auto selection = app->get_active_selection();

    // We should not have to do this!
    auto document  = app->get_active_document();
    selection->setDocument(document);

    std::vector<SPItem*> selected(selection->items().begin(), selection->items().end());
    if (selected.size() < 2) {
        return;
    }

    // clang-format off
    double a = 0.0;
    double b = 0.0;
    bool gap = false;
    auto orientation = Geom::X;
    if      (token == "hgap"    ) { gap = true;  orientation = Geom::X; a = 0.5, b = 0.5; }
    else if (token == "left"    ) { gap = false; orientation = Geom::X; a = 1.0, b = 0.0; }
    else if (token == "hcenter" ) { gap = false; orientation = Geom::X; a = 0.5, b = 0.5; }
    else if (token == "right"   ) { gap = false; orientation = Geom::X; a = 0.0, b = 1.0; }
    else if (token == "vgap"    ) { gap = true;  orientation = Geom::Y; a = 0.5, b = 0.5; }
    else if (token == "top"     ) { gap = false; orientation = Geom::Y; a = 1.0, b = 0.0; }
    else if (token == "vcenter" ) { gap = false; orientation = Geom::Y; a = 0.5, b = 0.5; }
    else if (token == "bottom"  ) { gap = false; orientation = Geom::Y; a = 0.0, b = 1.0; }
    // clang-format on


    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    int prefs_bbox = prefs->getBool("/tools/bounding_box");

    // Make a list of objects, sorted by anchors.
    std::vector<BBoxSort> sorted;
    for (auto item : selected) {
        Geom::OptRect bbox = !prefs_bbox ? (item)->desktopVisualBounds() : (item)->desktopGeometricBounds();
        if (bbox) {
            sorted.emplace_back(item, *bbox, orientation, a, b);
        }
    }
    std::stable_sort(sorted.begin(), sorted.end());

    // See comment in ActionAlign above (MISSING).
    int saved_compensation = prefs->getInt("/options/clonecompensation/value", SP_CLONE_COMPENSATION_UNMOVED);
    prefs->setInt("/options/clonecompensation/value", SP_CLONE_COMPENSATION_UNMOVED);

    bool changed = false;
    if (gap) {
        // Evenly spaced.

        // Overall bboxes span.
        double dist = (sorted.back().bbox.max()[orientation] - sorted.front().bbox.min()[orientation]);

        // Space eaten by bboxes.
        double span = 0.0;
        for (auto bbox : sorted) {
            span += bbox.bbox[orientation].extent();
        }

        // New distance between each bbox.
        double step = (dist - span) / (sorted.size() - 1);
        double pos = sorted.front().bbox.min()[orientation];
        for (auto bbox : sorted) {

            // Don't move if we are really close.
            if (!Geom::are_near(pos, bbox.bbox.min()[orientation], 1e-6)) {

                // Compute translation.
                Geom::Point t(0.0, 0.0);
                t[orientation] = pos - bbox.bbox.min()[orientation];

                // Translate
                bbox.item->move_rel(Geom::Translate(t));
                changed = true;
            }

            pos += bbox.bbox[orientation].extent();
            pos += step;
        }

    } else  {

        // Overall anchor span.
        double dist = sorted.back().anchor - sorted.front().anchor;

        // Distance between anchors.
        double step = dist / (sorted.size() - 1);

        for (unsigned int i = 0; i < sorted.size() ; i++) {
            BBoxSort & it(sorted[i]);

            // New anchor position.
            double pos = sorted.front().anchor + i * step;

            // Don't move if we are really close.
            if (!Geom::are_near(pos, it.anchor, 1e-6)) {

                // Compute translation.
                Geom::Point t(0.0, 0.0);
                t[orientation] = pos - it.anchor;

                // Translate
                it.item->move_rel(Geom::Translate(t));
                changed = true;
            }
        }
    }

    // Restore compensation setting.
    prefs->setInt("/options/clonecompensation/value", saved_compensation);

    if (changed) {
        Inkscape::DocumentUndo::done( document, _("Distribute"), INKSCAPE_ICON("dialog-align-and-distribute"));
    }
}

class Baseline
{
public:
    Baseline(SPItem *item, Geom::Point base, Geom::Dim2 orientation)
        : _item (item)
        , _base (base)
        , _orientation (orientation)
    {}
    SPItem *_item = nullptr;
    Geom::Point _base;
    Geom::Dim2 _orientation;
};

static bool operator< (const Baseline &a, const Baseline &b)
{
    return (a._base[a._orientation] < b._base[b._orientation]);
}

void
object_distribute_text(const Glib::VariantBase& value, InkscapeApplication *app)
{
    Glib::Variant<Glib::ustring> s = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring> >(value);
    auto token = s.get();

    Geom::Dim2 orientation = Geom::Dim2::X;
    if (token.find("vertical") != Glib::ustring::npos) {
        orientation = Geom::Dim2::Y;
    }

    auto selection = app->get_active_selection();
    if (selection->size() < 2) {
        return;
    }

    // We should not have to do this!
    auto document  = app->get_active_document();
    selection->setDocument(document);

    std::vector<Baseline> baselines;
    Geom::Point b_min = Geom::Point ( HUGE_VAL,  HUGE_VAL);
    Geom::Point b_max = Geom::Point (-HUGE_VAL, -HUGE_VAL);

    for (auto item : selection->items()) {
        if (is<SPText>(item) || cast<SPFlowtext>(item)) {
            Inkscape::Text::Layout const *layout = te_get_layout(item);
            std::optional<Geom::Point> pt = layout->baselineAnchorPoint();
            if (pt) {
                Geom::Point base = *pt * item->i2dt_affine();
                if (base[Geom::X] < b_min[Geom::X]) b_min[Geom::X] = base[Geom::X];
                if (base[Geom::Y] < b_min[Geom::Y]) b_min[Geom::Y] = base[Geom::Y];
                if (base[Geom::X] > b_max[Geom::X]) b_max[Geom::X] = base[Geom::X];
                if (base[Geom::Y] > b_max[Geom::Y]) b_max[Geom::Y] = base[Geom::Y];
                baselines.emplace_back(item, base, orientation);
            }
        }
    }

    if (baselines.size() < 2) {
        return;
    }

    std::stable_sort(baselines.begin(), baselines.end());

    double step = (b_max[orientation] - b_min[orientation])/(baselines.size() - 1);
    int i = 0;
    for (auto& baseline : baselines) {
        Geom::Point t(0.0, 0.0);
        t[orientation] = b_min[orientation] + (step * i) - baseline._base[orientation];
        baseline._item->move_rel(Geom::Translate(t));
        ++i;
    }

    Inkscape::DocumentUndo::done( document, _("Distribute"), INKSCAPE_ICON("dialog-align-and-distribute"));
}

void
object_align_text(const Glib::VariantBase& value, InkscapeApplication *app)
{

    Glib::Variant<Glib::ustring> s = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring> >(value);
    std::vector<Glib::ustring> tokens = Glib::Regex::split_simple(" ", s.get());

    // Defaults
    auto target = ObjectAlignTarget::SELECTION;
    auto orientation = Geom::Dim2::X;
    auto direction = Inkscape::Selection::HORIZONTAL;

    // Preference request allows alignment action to remember for key-presses
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (std::find(tokens.begin(), tokens.end(), "pref") != tokens.end()) {
        tokens.push_back(prefs->getString("/dialogs/align/objects-align-to", "selection"));
    }
 
    for (auto const &token : tokens) {

        // Target
        if      (token == "last"     ) target = ObjectAlignTarget::LAST;
        else if (token == "first"    ) target = ObjectAlignTarget::FIRST;
        else if (token == "biggest"  ) target = ObjectAlignTarget::BIGGEST;
        else if (token == "smallest" ) target = ObjectAlignTarget::SMALLEST;
        else if (token == "page"     ) target = ObjectAlignTarget::PAGE;
        else if (token == "drawing"  ) target = ObjectAlignTarget::DRAWING;
        else if (token == "selection") target = ObjectAlignTarget::SELECTION;

        // Direction
        if      (token == "vertical" ) {
            orientation = Geom::Dim2::Y;
            direction = Inkscape::Selection::VERTICAL;
        }
    }

    auto selection = app->get_active_selection();

    // We should not have to do this!
    auto document  = app->get_active_document();
    selection->setDocument(document);

    // Find alignment rectangle. This can come from:
    // - The bounding box of an object
    // - The bounding box of a group of objects
    // - The bounding box of the page, drawing, or selection.
    SPItem *focus = nullptr;
    Geom::OptRect b = Geom::OptRect();

    switch (target) {
        case ObjectAlignTarget::LAST:
            focus = selection->items().back();
            break;
        case ObjectAlignTarget::FIRST:
            focus = selection->items().front();
            break;
        case ObjectAlignTarget::BIGGEST:
            focus = selection->largestItem(direction);
            break;
        case ObjectAlignTarget::SMALLEST:
            focus = selection->smallestItem(direction);
            break;
        case ObjectAlignTarget::PAGE:
            b = document->pageBounds();
            break;
        case ObjectAlignTarget::DRAWING:
            b = document->getRoot()->desktopPreferredBounds();
            break;
        case ObjectAlignTarget::SELECTION:
            b = selection->preferredBounds();
            break;
        default:
            g_assert_not_reached ();
            break;
    };

    Geom::Point ref_point;
    if (focus) {
        if (is<SPText>(focus) || cast<SPFlowtext>(focus)) {
            ref_point = *(te_get_layout(focus)->baselineAnchorPoint())*(focus->i2dt_affine());
        } else {
            ref_point = focus->desktopPreferredBounds()->min();
        }
    } else {
        ref_point = b->min();
    }

    for (auto item : selection->items()) {
        if (is<SPText>(item) || cast<SPFlowtext>(item)) {
            Inkscape::Text::Layout const *layout = te_get_layout(item);
            std::optional<Geom::Point> pt = layout->baselineAnchorPoint();
            if (pt) {
                Geom::Point base = *pt * (item)->i2dt_affine();
                Geom::Point t(0.0, 0.0);
                t[orientation] = ref_point[orientation] - base[orientation];
                item->move_rel(Geom::Translate(t));
            }
        }
    }

    Inkscape::DocumentUndo::done( document, _("Align"), INKSCAPE_ICON("dialog-align-and-distribute"));
}

/* --------------- Rearrange ----------------- */

class RotateCompare
{
public:
    RotateCompare(Geom::Point& center) : center(center) {}

    bool operator()(const SPItem* a, const SPItem* b) {
        Geom::Point point_a = a->getCenter() - (center);
        Geom::Point point_b = b->getCenter() - (center);

        // Sort according to angle.
        double angle_a = Geom::atan2(point_a);
        double angle_b = Geom::atan2(point_b);
        if (angle_a != angle_b) return (angle_a < angle_b);

        // Sort by distance
        return point_a.length() < point_b.length();
    }

private:
    Geom::Point center;
};

enum SortOrder {
    SelectionOrder,
    ZOrder,
    Rotate
};

static bool PositionCompare(const SPItem* a, const SPItem* b) {
    return sp_item_repr_compare_position(a, b) < 0;
}

void exchange(Inkscape::Selection* selection, SortOrder order)
{
    std::vector<SPItem*> items(selection->items().begin(), selection->items().end());

    // Reorder items.
    switch (order) {
        case SelectionOrder:
            break;
        case ZOrder:
            std::sort(items.begin(), items.end(), PositionCompare);
            break;
        case Rotate:
            auto center = selection->center();
            if (center) {
                std::sort(items.begin(), items.end(), RotateCompare(*center));
            }
            break;
    }

    // Move items.
    Geom::Point p1 = items.back()->getCenter();
    for (SPItem *item : items) {
        Geom::Point p2 = item->getCenter();
        Geom::Point delta = p1 - p2;
        item->move_rel(Geom::Translate(delta));
        p1 = p2;
    }
}

/*
 *  The algorithm keeps the size of the bounding box of the centers of all items constant. This
 *  ensures there is no growth or shrinking or drift of the overall area of the items on sequential
 *  randomizations.
 */
void randomize(Inkscape::Selection* selection)
{
    std::vector<SPItem*> items(selection->items().begin(), selection->items().end());

    // Do 'x' and 'y' independently.
    for (int i = 0; i < 2; i++) {

        // First, find maximum and minimum centers.
        double min = std::numeric_limits<double>::max();
        double max = std::numeric_limits<double>::min();

        for (auto item : items) {
            double center = item->getCenter()[i];
            if (min > center) {
                min = center;
            }
            if (max < center) {
                max = center;
            }
        }


        // Second, assign minimum/maximum values to two different items randomly.
        int nitems = items.size();
        int imin = rand() % nitems;
        int imax = rand() % nitems;
        while (imin == imax) {
            imax = rand() % nitems;
        }


        // Third, find new positions of item centers.
        int index = 0;
        for (auto item : items) {
            double z = 0.0;
            if (index == imin) {
                z = min;
            } else  if (index == imax) {
                z = max;
            } else {
                z = g_random_double_range(min, max);
            }

            double delta = z - item->getCenter()[i];
            Geom::Point t;
            t[i] = delta;
            item->move_rel(Geom::Translate(t));

            ++index;
        }
    }
}


void
object_rearrange(const Glib::VariantBase& value, InkscapeApplication *app)
{
    Glib::Variant<Glib::ustring> s = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring> >(value);
    auto token = s.get();

    auto selection = app->get_active_selection();

    // We should not have to do this!
    auto document  = app->get_active_document();
    selection->setDocument(document);

    std::vector<SPItem*> items(selection->items().begin(), selection->items().end());
    if (items.size() < 2) {
        return;
    }

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    int saved_compensation = prefs->getInt("/options/clonecompensation/value", SP_CLONE_COMPENSATION_UNMOVED);
    prefs->setInt("/options/clonecompensation/value", SP_CLONE_COMPENSATION_UNMOVED);

    // clang-format off
    if      (token == "graph"     ) { graphlayout(items); }
    else if (token == "exchange"  ) { exchange(selection, SortOrder::SelectionOrder); }
    else if (token == "exchangez" ) { exchange(selection, SortOrder::ZOrder); }
    else if (token == "rotate"    ) { exchange(selection, SortOrder::Rotate); }
    else if (token == "randomize" ) { randomize(selection); }
    else if (token == "unclump"   ) { unclump(items); }
    else {
        show_output(Glib::ustring("object_rearrange: unhandled argument: ") + token.raw());
     }
    // clang-format on

    // Restore compensation setting.
    prefs->setInt("/options/clonecompensation/value", saved_compensation);

    Inkscape::DocumentUndo::done( document, _("Rearrange"), INKSCAPE_ICON("dialog-align-and-distribute"));
}


void
object_remove_overlaps(const Glib::VariantBase& value, InkscapeApplication *app)
{
    auto selection = app->get_active_selection();

    // We should not have to do this!
    auto document  = app->get_active_document();
    selection->setDocument(document);

    std::vector<SPItem*> items(selection->items().begin(), selection->items().end());
    if (items.size() < 2) {
        return;
    }

    // We used tuple so as not to convert from double to string and back again (from Align and Distribute dialog).
    if (value.get_type_string() != "(dd)") {
        show_output(Glib::ustring("object_remove_overlaps:  wrong variant type: ") + Glib::ustring::format(value.get_type_string()) + " (should be '(dd)')");
    }

    auto tuple = Glib::VariantBase::cast_dynamic<Glib::Variant<std::tuple<double, double>>>(value);
    auto [hgap, vgap] = tuple.get();

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    int saved_compensation = prefs->getInt("/options/clonecompensation/value", SP_CLONE_COMPENSATION_UNMOVED);
    prefs->setInt("/options/clonecompensation/value", SP_CLONE_COMPENSATION_UNMOVED);

    removeoverlap(items, hgap, vgap);

    // Restore compensation setting.
    prefs->setInt("/options/clonecompensation/value", saved_compensation);

    Inkscape::DocumentUndo::done( document, _("Remove overlaps"), INKSCAPE_ICON("dialog-align-and-distribute"));
}


std::vector<std::vector<Glib::ustring>> raw_data_object_align =
{
    // clang-format off
    {"app.object-align-on-canvas",         N_("Enable on-canvas alignment"),  "Object", N_("Enable on-canvas alignment handles"                                                                                           )},

    {"app.object-align",                   N_("Align objects"),      "Object", N_("Align selected objects; usage: [[left|hcenter|right] || [top|vcenter|bottom]] [last|first|biggest|smallest|page|drawing|selection|pref]? group? anchor?")},

    {"app.object-align('left pref')",      N_("Align to left edge"),          "Object", N_("Align selection horizontally to left edge"                                                                                    )},
    {"app.object-align('hcenter pref')",   N_("Align to horizontal center"),  "Object", N_("Align selection horizontally to the center"                                                                                   )},
    {"app.object-align('right pref')",     N_("Align to right edge"),         "Object", N_("Align selection horizontally to right edge"                                                                                   )},
    {"app.object-align('top pref')",       N_("Align to top edge"),           "Object", N_("Align selection vertically to top edge"                                                                                       )},
    {"app.object-align('bottom pref')",    N_("Align to bottom edge"),        "Object", N_("Align selection vertically to bottom edge"                                                                                    )},
    {"app.object-align('vcenter pref')",   N_("Align to vertical center"),    "Object", N_("Align selection vertically to the center"                                                                                     )},
    {"app.object-align('hcenter vcenter pref')", N_("Align to center"),       "Object", N_("Align selection to the center"                                                                                                )},
    {"app.object-align-text",              N_("Align text objects"), "Object", N_("Align selected text anchors; usage: [[vertical | horizontal] [last|first|biggest|smallest|page|drawing|selection]?"            )},

    {"app.object-distribute",              N_("Distribute objects"),          "Object", N_("Distribute selected objects; usage: [hgap | left | hcenter | right | vgap | top | vcenter | bottom]"                           )},
    {"app.object-distribute('hgap')",      N_("Even horizontal gaps"),        "Object", N_("Distribute horizontally with even horizontal gaps"                                                                            )},
    {"app.object-distribute('left')",      N_("Even left edges"),             "Object", N_("Distribute horizontally with even spacing between left edges"                                                                 )},
    {"app.object-distribute('hcenter')",   N_("Even horizontal centers"),     "Object", N_("Distribute horizontally with even spacing between centers"                                                                    )},
    {"app.object-distribute('right')",     N_("Even right edges"),            "Object", N_("Distribute horizontally with even spacing between right edges"                                                                )},
    {"app.object-distribute('vgap')",      N_("Even vertical gaps"),          "Object", N_("Distribute vertically with even vertical gaps"                                                                                )},
    {"app.object-distribute('top')",       N_("Even top edges"),              "Object", N_("Distribute vertically with even spacing between top edges"                                                                    )},
    {"app.object-distribute('vcenter')",   N_("Even vertical centers"),       "Object", N_("Distribute vertically with even spacing between centers"                                                                      )},
    {"app.object-distribute('bottom')",    N_("Even bottom edges"),           "Object", N_("Distribute vertically with even spacing between bottom edges"                                                                 )},

    {"app.object-distribute-text",         N_("Distribute text objects"),     "Object", N_("Distribute text anchors; usage [vertical | horizontal]"                                                               )},
    {"app.object-distribute-text('horizontal')", N_("Distribute text objects"),     "Object", N_("Distribute text anchors horizontally"                                                                           )},
    {"app.object-distribute-text('vertical')",   N_("Distribute text objects"),     "Object", N_("Distribute text anchors vertically"                                                                             )},

    {"app.object-rearrange",               N_("Rearrange objects"),           "Object", N_("Rearrange selected objects; usage: [graph | exchange | exchangez | rotate | randomize | unclump]"                              )},
    {"app.object-rearrange('graph')",      N_("Rearrange as graph"),          "Object", N_("Nicely arrange selected connector network"                                                                                    )},
    {"app.object-rearrange('exchange')",   N_("Exchange in selection order"), "Object", N_("Exchange positions of selected objects - selection order"                                                                     )},
    {"app.object-rearrange('exchangez')",  N_("Exchange in z-order"),         "Object", N_("Exchange positions of selected objects - stacking order"                                                                      )},
    {"app.object-rearrange('rotate')",     N_("Exchange around center"),      "Object", N_("Exchange positions of selected objects - rotate around center point"                                                          )},
    {"app.object-rearrange('randomize')",  N_("Random exchange"),             "Object", N_("Randomize centers in both dimensions"                                                                                         )},
    {"app.object-rearrange('unclump')",    N_("Unclump"),                     "Object", N_("Unclump objects: try to equalize edge-to-edge distances"                                                                      )},

    {"app.object-remove-overlaps",         N_("Remove overlaps"),             "Object", N_("Remove overlaps between objects: requires two comma separated numbers (horizontal and vertical gaps)"                         )},
    // clang-format on
};

std::vector<std::vector<Glib::ustring>> hint_data_object_align =
{
    // clang-format off
    {"app.object-align",           N_("Enter anchor<space>alignment<space>optional second alignment. Possible anchors: last, first, biggest, smallest, page, drawing, selection, pref; possible alignments: left, hcenter, right, top, vcenter, bottom.")},
    {"app.object-distribute",      N_("Enter distribution type. Possible values: left, hcenter, right, top, vcenter, bottom, hgap, vgap.")  },
    {"app.object-rearrange",       N_("Enter arrange method. Possible values: graph, exchange, exchangez, rotate, randomize, unclump.")       },
    {"app.object-remove-overlaps", N_("Enter two comma-separated numbers: horizontal,vertical")                                                              },
    // clang-format on
};

void
add_actions_object_align(InkscapeApplication* app)
{
    Glib::VariantType String(Glib::VARIANT_TYPE_STRING);
    std::vector<Glib::VariantType> dd = {Glib::VARIANT_TYPE_DOUBLE, Glib::VARIANT_TYPE_DOUBLE};
    Glib::VariantType Tuple_DD = Glib::VariantType::create_tuple(dd);

    auto *gapp = app->gio_app();

    auto prefs = Inkscape::Preferences::get();
    bool on_canvas = prefs->getBool("/dialogs/align/oncanvas");

    // clang-format off
    gapp->add_action_bool(           "object-align-on-canvas",             sigc::bind(sigc::ptr_fun(&object_align_on_canvas),  app), on_canvas);
    gapp->add_action_with_parameter( "object-align",             String,   sigc::bind(sigc::ptr_fun(&object_align),            app));
    gapp->add_action_with_parameter( "object-align-text",        String,   sigc::bind(sigc::ptr_fun(&object_align_text),       app));
    gapp->add_action_with_parameter( "object-distribute",        String,   sigc::bind(sigc::ptr_fun(&object_distribute),       app));
    gapp->add_action_with_parameter( "object-distribute-text",   String,   sigc::bind(sigc::ptr_fun(&object_distribute_text),  app));
    gapp->add_action_with_parameter( "object-rearrange",         String,   sigc::bind(sigc::ptr_fun(&object_rearrange),        app));
    gapp->add_action_with_parameter( "object-remove-overlaps",   Tuple_DD, sigc::bind(sigc::ptr_fun(&object_remove_overlaps),  app));
    // clang-format on

    app->get_action_extra_data().add_data(raw_data_object_align);
    app->get_action_hint_data().add_data(hint_data_object_align);
}

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
