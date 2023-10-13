// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Eraser drawing mode
 *
 * Authors:
 *   Mitsuru Oka <oka326@parkcity.ne.jp>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   MenTaLguY <mental@rydia.net>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *   Rafael Siejakowski <rs@rs-math.net>
 *
 * The original dynadraw code:
 *   Paul Haeberli <paul@sgi.com>
 *
 * Copyright (C) 1998 The Free Software Foundation
 * Copyright (C) 1999-2005 authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 * Copyright (C) 2005-2007 bulia byak
 * Copyright (C) 2006 MenTaLguY
 * Copyright (C) 2008 Jon A. Cruz
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#define noERASER_VERBOSE

#include "eraser-tool.h"

#include <cmath>
#include <cstring>
#include <string>
#include <numeric>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glibmm/i18n.h>
#include <2geom/bezier-utils.h>
#include <2geom/pathvector.h>

#include "context-fns.h"
#include "desktop-events.h"
#include "desktop-style.h"
#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "layer-manager.h"
#include "message-context.h"
#include "message-stack.h"
#include "path-chemistry.h"
#include "preferences.h"
#include "rubberband.h"
#include "selection-chemistry.h"
#include "selection.h"
#include "style.h"
#include "display/curve.h"
#include "display/control/canvas-item-bpath.h"
#include "object/sp-clippath.h"
#include "object/sp-image.h"
#include "object/sp-item-group.h"
#include "object/sp-path.h"
#include "object/sp-rect.h"
#include "object/sp-shape.h"
#include "object/sp-use.h"
#include "ui/icon-names.h"
#include "ui/widget/events/canvas-event.h"
#include "svg/svg.h"

using Inkscape::DocumentUndo;

namespace Inkscape::UI::Tools {

EraserTool::EraserTool(SPDesktop *desktop)
    : DynamicBase(desktop, "/tools/eraser", "eraser.svg")
    , _break_apart{"/tools/eraser/break_apart", false}
    , _mode_int{"/tools/eraser/mode", 1} // Cut mode is default
{
    currentshape = make_canvasitem<CanvasItemBpath>(desktop->getCanvasSketch());
    currentshape->set_stroke(0x0);
    currentshape->set_fill(trace_color_rgba, trace_wind_rule);

    /* fixme: Cannot we cascade it to root more clearly? */
    currentshape->connect_event(sigc::bind(sigc::ptr_fun(sp_desktop_root_handler), desktop));

    sp_event_context_read(this, "mass");
    sp_event_context_read(this, "wiggle");
    sp_event_context_read(this, "angle");
    sp_event_context_read(this, "width");
    sp_event_context_read(this, "thinning");
    sp_event_context_read(this, "tremor");
    sp_event_context_read(this, "flatness");
    sp_event_context_read(this, "tracebackground");
    sp_event_context_read(this, "usepressure");
    sp_event_context_read(this, "usetilt");
    sp_event_context_read(this, "abs_width");
    sp_event_context_read(this, "cap_rounding");

    is_drawing = false;
    //TODO not sure why get 0.01 if slider width == 0, maybe a double/int problem

    _mode_int.min = 0;
    _mode_int.max = 2;
    _updateMode();
    _mode_int.action = [this]() { _updateMode(); };

    enableSelectionCue();
}

EraserTool::~EraserTool() = default;

/**  Reads the current Eraser mode from Preferences and sets `mode` accordingly. */
void EraserTool::_updateMode()
{
    int const mode_idx = _mode_int;
    // Note: the integer indices must agree with those in EraserToolbar::_modeAsInt()
    if (mode_idx == 0) {
        mode = EraserToolMode::DELETE;
    } else if (mode_idx == 1) {
        mode = EraserToolMode::CUT;
    } else if (mode_idx == 2) {
        mode = EraserToolMode::CLIP;
    } else {
        g_printerr("Error: invalid mode setting \"%d\" for Eraser tool!", mode_idx);
        mode = DEFAULT_ERASER_MODE;
    }
}

inline double square(double const x)
{
    return x * x;
}

void EraserTool::_reset(Geom::Point p)
{
    last = cur = getNormalizedPoint(p);
    vel = Geom::Point(0, 0);
    vel_max = 0;
    acc = Geom::Point(0, 0);
    ang = Geom::Point(0, 0);
    del = Geom::Point(0, 0);
}

void EraserTool::_extinput(ExtendedInput const &ext)
{
    if (ext.pressure) {
        pressure = std::clamp(*ext.pressure, min_pressure, max_pressure);
    } else {
        pressure = default_pressure;
    }

    if (ext.xtilt) {
        xtilt = std::clamp(*ext.xtilt, min_tilt, max_tilt);
    } else {
        xtilt = default_tilt;
    }

    if (ext.ytilt) {
        ytilt = std::clamp(*ext.ytilt, min_tilt, max_tilt);
    } else {
        ytilt = default_tilt;
    }
}

bool EraserTool::_apply(Geom::Point const &p)
{
    /* Calculate force and acceleration */
    Geom::Point n = getNormalizedPoint(p);
    Geom::Point force = n - cur;

    // If force is below the absolute threshold `epsilon`,
    // or we haven't yet reached `vel_start` (i.e. at the beginning of stroke)
    // _and_ the force is below the (higher) `epsilon_start` threshold,
    // discard this move.
    // This prevents flips, blobs, and jerks caused by microscopic tremor of the tablet pen,
    // especially bothersome at the start of the stroke where we don't yet have the inertia to
    // smooth them out.
    if (Geom::L2(force) < epsilon || (vel_max < vel_start && Geom::L2(force) < epsilon_start)) {
        return false;
    }

    // Calculate mass
    double const m = std::lerp(1.0, 160.0, mass);
    acc = force / m;
    vel += acc; // Calculate new velocity
    double const speed = Geom::L2(vel);

    if (speed > vel_max) {
        vel_max = speed;
    } else if (speed < epsilon) {
        return false; // return early if movement is insignificant
    }

    /* Calculate angle of eraser tool */
    double angle_fixed{0.0};
    if (usetilt) {
        // 1a. calculate nib angle from input device tilt:
        Geom::Point normal{ytilt, xtilt};
        if (!Geom::is_zero(normal)) {
            angle_fixed = Geom::atan2(normal);
        }
    } else {
        // 1b. fixed angle (absolutely flat nib):
        angle_fixed = angle * M_PI / 180.0; // convert to radians
    }
    if (flatness < 0.0) {
        // flips direction. Useful when usetilt is true
        // allows simulating both pen/charcoal and broad-nibbed pen
        angle_fixed *= -1;
    }

    // 2. Angle perpendicular to vel (absolutely non-flat nib):
    double angle_dynamic = Geom::atan2(Geom::rot90(vel));
    // flip angle_dynamic to force it to be in the same half-circle as angle_fixed
    bool flipped = false;
    if (fabs(angle_dynamic - angle_fixed) > M_PI_2) {
        angle_dynamic += M_PI;
        flipped = true;
    }
    // normalize angle_dynamic
    if (angle_dynamic > M_PI) {
        angle_dynamic -= 2 * M_PI;
    }
    if (angle_dynamic < -M_PI) {
        angle_dynamic += 2 * M_PI;
    }

    // 3. Average them using flatness parameter:
    // find the flatness-weighted bisector angle, unflip if angle_dynamic was flipped
    // FIXME: when `vel` is oscillating around the fixed angle, the new_ang flips back and forth.
    // How to avoid this?
    double new_ang = std::lerp(angle_dynamic, angle_fixed, fabs(flatness)) - (flipped ? M_PI : 0);

    // Try to detect a sudden flip when the new angle differs too much from the previous for the
    // current velocity; in that case discard this move
    double angle_delta = Geom::L2(Geom::Point(cos(new_ang), sin(new_ang)) - ang);
    if (angle_delta / speed > 4000) {
        return false;
    }

    // convert to point
    ang = Geom::Point(cos(new_ang), sin(new_ang));

    /* Apply drag */
    double const d = std::lerp(0.0, 0.5, square(drag));
    vel *= 1.0 - d;

    /* Update position */
    last = cur;
    cur += vel;

    return true;
}

void EraserTool::_brush()
{
    g_assert(npoints >= 0 && npoints < SAMPLING_SIZE);

    // How much velocity thins strokestyle
    double const vel_thinning = std::lerp(0, 160, vel_thin);

    // Influence of pressure on thickness
    double const pressure_thick = (usepressure ? pressure : 1.0);

    // get the real brush point, not the same as pointer (affected by mass drag)
    Geom::Point brush = getViewPoint(cur);

    double const trace_thick = 1;
    double const speed = Geom::L2(vel);
    double effective_width = (pressure_thick * trace_thick - vel_thinning * speed) * width;

    double tremble_left = 0, tremble_right = 0;
    if (tremor > 0) {
        // obtain two normally distributed random variables, using polar Box-Muller transform
        double y1, y2;
        _generateNormalDist2(y1, y2);

        // deflect both left and right edges randomly and independently, so that:
        // (1) tremor=1 corresponds to sigma=1, decreasing tremor narrows the bell curve;
        // (2) deflection depends on width, but is upped for small widths for better visual uniformity across widths;
        // (3) deflection somewhat depends on speed, to prevent fast strokes looking
        //     comparatively smooth and slow ones excessively jittery
        double const width_coefficient = 0.15 + 0.8 * effective_width;
        double const speed_coefficient = 0.35 + 14 * speed;
        double const total_coefficient = tremor * width_coefficient * speed_coefficient;

        tremble_left  = y1 * total_coefficient;
        tremble_right = y2 * total_coefficient;
    }

    double const min_width = 0.02 * width;
    if (effective_width < min_width) {
        effective_width = min_width;
    }

    double dezoomify_factor = 0.05 * 1000;
    if (!abs_width) {
        dezoomify_factor /= _desktop->current_zoom();
    }

    Geom::Point del_left  = dezoomify_factor * (effective_width + tremble_left)  * ang;
    Geom::Point del_right = dezoomify_factor * (effective_width + tremble_right) * ang;

    point1[npoints] = brush + del_left;
    point2[npoints] = brush - del_right;

    if (nowidth) {
        point1[npoints] = Geom::middle_point(point1[npoints], point2[npoints]);
    }
    del = Geom::middle_point(del_left, del_right);

    npoints++;
}

void EraserTool::_generateNormalDist2(double &r1, double &r2)
{
    // obtain two normally distributed random variables, using polar Box-Muller transform
    double x1, x2, w;
    do {
        x1 = 2.0 * g_random_double_range(0, 1) - 1.0;
        x2 = 2.0 * g_random_double_range(0, 1) - 1.0;
        w = square(x1) + square(x2);
    } while (w >= 1.0);
    w = sqrt(-2.0 * log(w) / w);
    r1 = x1 * w;
    r2 = x2 * w;
}

void EraserTool::_cancel()
{
    dragging = false;
    is_drawing = false;
    ungrabCanvasEvents();

    segments.clear();

    /* reset accumulated curve */
    accumulated.reset();
    _clearCurrent();
    repr = nullptr;
}

bool EraserTool::root_handler(CanvasEvent const &event)
{
    bool ret = false;

    inspect_event(event,
        [&] (ButtonPressEvent const &event) {
            if (event.num_press == 1 && event.button == 1) {
                if (!have_viable_layer(_desktop, defaultMessageContext())) {
                    return;
                    ret = true;
                }

                auto const button_w = event.pos;
                auto const button_dt = _desktop->w2d(button_w);

                _reset(button_dt);
                _extinput(event.extinput);
                _apply(button_dt);
                accumulated.reset();

                repr = nullptr;

                if (mode == EraserToolMode::DELETE) {
                    auto rubberband = Inkscape::Rubberband::get(_desktop);
                    rubberband->start(_desktop, button_dt);
                    rubberband->setMode(RUBBERBAND_MODE_TOUCHPATH);
                }
                /* initialize first point */
                npoints = 0;

                grabCanvasEvents();
                is_drawing = true;
                ret = true;
            }
        },

        [&] (MotionEvent const &event) {
            auto const motion_w = event.pos;
            auto const motion_dt = _desktop->w2d(motion_w);
            _extinput(event.extinput);

            message_context->clear();

            if (is_drawing && (event.modifiers & GDK_BUTTON1_MASK)) {
                dragging = true;

                message_context->set(Inkscape::NORMAL_MESSAGE, _("<b>Drawing</b> an eraser stroke"));

                if (!_apply(motion_dt)) {
                    ret = true;
                    return;
                }

                if (cur != last) {
                    _brush();
                    g_assert(npoints > 0);
                    _fitAndSplit(false);
                }

                ret = true;
            }
            if (mode == EraserToolMode::DELETE) {
                accumulated.reset();
                Inkscape::Rubberband::get(_desktop)->move(motion_dt);
            }
        },

        [&] (ButtonReleaseEvent const &event) {
            if (event.button != 1) {
                return;
            }

            auto const motion_w = event.pos;
            auto const motion_dt = _desktop->w2d(motion_w);

            ungrabCanvasEvents();

            is_drawing = false;

            if (dragging) {
                dragging = false;

                _apply(motion_dt);
                segments.clear();

                // Create eraser stroke shape
                _fitAndSplit(true);
                _accumulate();

                // Perform the actual erase operation
                auto document = _desktop->getDocument();
                if (_doWork()) {
                    DocumentUndo::done(document, _("Draw eraser stroke"), INKSCAPE_ICON("draw-eraser"));
                } else {
                    DocumentUndo::cancel(document);
                }

                /* reset accumulated curve */
                accumulated.reset();

                _clearCurrent();
                repr = nullptr;

                message_context->clear();
                ret = true;
            }

            if (mode == EraserToolMode::DELETE) {
                auto r = Inkscape::Rubberband::get(_desktop);
                if (r->is_started()) {
                    r->stop();
                }
            }
        },

        [&] (KeyPressEvent const &event) {
            ret = _handleKeypress(event);
        },

        [&] (KeyReleaseEvent const &event) {
            switch (get_latin_keyval(event)) {
                case GDK_KEY_Control_L:
                case GDK_KEY_Control_R:
                    message_context->clear();
                    break;

                default:
                    break;
            }
        },

        [&] (CanvasEvent const &event) {}
    );

    return ret || DynamicBase::root_handler(event);
}

/** Analyses and handles a key press event, returns true if processed, false if not. */
bool EraserTool::_handleKeypress(KeyPressEvent const &key)
{
    bool ret = false;
    bool just_ctrl = (key.modifiers & GDK_CONTROL_MASK)                      // Ctrl key is down
                     && !(key.modifiers & (GDK_ALT_MASK | GDK_SHIFT_MASK)); // but not Alt or Shift

    bool just_alt = (key.modifiers & GDK_ALT_MASK)                            // Alt is down
                    && !(key.modifiers & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)); // but not Ctrl or Shift

    switch (get_latin_keyval(key)) {
        case GDK_KEY_Right:
        case GDK_KEY_KP_Right:
            if (!just_ctrl) {
                width += 0.01;
                if (width > 1.0) {
                    width = 1.0;
                }
                // Alt+X sets focus to this spinbutton as well
                _desktop->setToolboxAdjustmentValue("eraser-width", width * 100);
                ret = true;
            }
            break;

        case GDK_KEY_Left:
        case GDK_KEY_KP_Left:
            if (!just_ctrl) {
                width -= 0.01;
                if (width < 0.01) {
                    width = 0.01;
                }
                _desktop->setToolboxAdjustmentValue("eraser-width", width * 100);
                ret = true;
            }
            break;

        case GDK_KEY_Home:
        case GDK_KEY_KP_Home:
            width = 0.01;
            _desktop->setToolboxAdjustmentValue("eraser-width", width * 100);
            ret = true;
            break;

        case GDK_KEY_End:
        case GDK_KEY_KP_End:
            width = 1.0;
            _desktop->setToolboxAdjustmentValue("eraser-width", width * 100);
            ret = true;
            break;

        case GDK_KEY_x:
        case GDK_KEY_X:
            if (just_alt) {
                _desktop->setToolboxFocusTo("eraser-width");
                ret = true;
            }
            break;

        case GDK_KEY_Escape:
            if (mode == EraserToolMode::DELETE) {
                Inkscape::Rubberband::get(_desktop)->stop();
            }
            if (is_drawing) {
                // if drawing, cancel, otherwise pass it up for deselecting
                _cancel();
                ret = true;
            }
            break;

        case GDK_KEY_z:
        case GDK_KEY_Z:
            if (just_ctrl && is_drawing) { // Ctrl+Z pressed while drawing
                _cancel();
                ret = true;
            } // if not drawing, pass it up for undo
            break;

        default:
            break;
    }
    return ret;
}

/** Inserts the temporary red shape of the eraser stroke (the "acid") into the document.
 *  @return a pointer to the inserted item
 */
SPItem *EraserTool::_insertAcidIntoDocument(SPDocument *document)
{
    auto *top_layer = _desktop->layerManager().currentRoot();
    auto *eraser_item = cast<SPItem>(top_layer->appendChildRepr(repr));
    Inkscape::GC::release(repr);
    eraser_item->updateRepr();
    Geom::PathVector pathv = accumulated.get_pathvector() * _desktop->dt2doc();
    pathv *= eraser_item->i2doc_affine().inverse();
    repr->setAttribute("d", sp_svg_write_path(pathv));
    return cast<SPItem>(document->getObjectByRepr(repr));
}

void EraserTool::_clearCurrent()
{
    // reset bpath
    currentshape->set_bpath(nullptr);

    // reset curve
    currentcurve.reset();
    cal1.reset();
    cal2.reset();

    // reset points
    npoints = 0;
}

/**
 * @brief Performs the actual erase operation against the current document
 * @return whether actual erasing took place (and undo history should be updated).
 */
bool EraserTool::_doWork()
{
    if (accumulated.is_empty()) {
        if (repr) {
            sp_repr_unparent(repr);
            repr = nullptr;
        }
        return false;
    }

    SPDocument *document = _desktop->getDocument();
    if (!repr) {
        // Create eraser repr
        Inkscape::XML::Document *xml_doc = document->getReprDoc();
        Inkscape::XML::Node *eraser_repr = xml_doc->createElement("svg:path");

        sp_desktop_apply_style_tool(_desktop, eraser_repr, "/tools/eraser", false);
        repr = eraser_repr;
    }
    if (!repr) {
        return false;
    }

    Selection *selection = _desktop->getSelection();
    if (!selection) {
        return false;
    }
    bool was_selection = !selection->isEmpty();

    // Find items to work on as well as items that will be needed to restore the selection afterwards.
    _survivers.clear();
    _clearStatusBar();

    std::vector<EraseTarget> to_erase = _findItemsToErase();

    bool work_done = false;
    if (!to_erase.empty()) {
        selection->clear();
        work_done = _performEraseOperation(to_erase, true);
        if (was_selection && !_survivers.empty()) {
            selection->add(_survivers.begin(), _survivers.end());
        }
    }
    // Clean up the eraser stroke repr:
    sp_repr_unparent(repr);
    repr = nullptr;
    _acid = nullptr;
    return work_done;
}

/**
 * @brief Erases from a shape by cutting (boolean difference or cut operation).
 * @param target - the item to be erased
 * @param store_survivers - whether the surviving selected items and their remains should be stored.
 * @return whether the target was successfully processed.
 */
bool EraserTool::_cutErase(EraseTarget target, bool store_survivers)
{
    // If the item is a clone, we check if the original is cuttable before unlinking it
    if (auto use = cast<SPUse>(target.item)) {
        auto original = use->trueOriginal();
        if (_uncuttableItemType(original)) {
            if (store_survivers && target.was_selected) {
                _survivers.push_back(target.item);
            }
            return false;
        } else if (auto *group = cast<SPGroup>(original)) {
            return _probeUnlinkCutClonedGroup(target, use, group, store_survivers);
        }
        // A simple clone of a cuttable item: unlink and erase it.
        target.item = use->unlink();
        if (target.was_selected && store_survivers) { // Reselect the freshly unlinked item
            _survivers.push_back(target.item);
        }
    }
    return _booleanErase(target, store_survivers);
}

/**
 * @brief Analyses a cloned group and decides if the CUT mode should unlink the clone.
 *        The decision to unlink the clone is based on collision detection between the eraser stroke
 *        and any of the eraseable contents of the cloned group, in the clone's coordinates.
 *        Unlinking only happens if there's an overlap between the eraser stroke and something that
 *        can be erased in CUT mode (via boolean operations).
 *        If the decision is made to unlink the clone, a copy of the clone is inserted into the document,
 *        and the function then erases all elements of the newly inserted group.
 * @param original_target - the original erase target which turned out to be a clone.
 * @param clone - the pointer to the SPUse object representing the clone (assument non-null).
 * @param cloned_group - the original group that is cloned (at the origin of the USE chain).
 * @param store_survivers - whether the surviving selected items and their remains should be stored.
 * @return whether the clone was unlinked and something was erased from the resulting new group.
 */
bool EraserTool::_probeUnlinkCutClonedGroup(EraseTarget &original_target, SPUse *clone, SPGroup *cloned_group,
                                            bool store_survivers)
{
    std::vector<EraseTarget> children;
    children.reserve(cloned_group->getItemCount());

    for (auto *child : cloned_group->childList(false)) {
        children.emplace_back(cast<SPItem>(child), false);
    }
    auto const filtered_children = _filterCutEraseables(children, true);

    // We must now check if any of the eraseable items in the original group, after transforming
    // to the coordinates of the clone, actually intersect the eraser stroke.
    Geom::Affine parent_inverse_transform;
    if (auto *parent_item = cast<SPItem>(cloned_group->parent)) {
        parent_inverse_transform = parent_item->i2doc_affine().inverse();
    }
    auto const relative_transform = parent_inverse_transform * clone->i2doc_affine();
    auto const eraser_bounds = _acid->documentExactBounds();
    if (!eraser_bounds) {
        return false;
    }
    auto const eraser_in_group_coordinates = *eraser_bounds * relative_transform.inverse();
    bool found_collision = false;
    for (auto const &orig_child : filtered_children) {
        if (orig_child.item->collidesWith(eraser_in_group_coordinates)) {
            found_collision = true;
            break;
        }
    }
    if (found_collision) {
        auto *unlinked = cast<SPGroup>(clone->unlink());
        if (!unlinked) {
            return false;
        }
        std::vector<EraseTarget> unlinked_children;
        unlinked_children.reserve(filtered_children.size());

        for (auto *child : unlinked->childList(false)) {
            unlinked_children.emplace_back(cast<SPItem>(child), false);
        }
        auto overlapping = _filterCutEraseables(_filterByCollision(unlinked_children, _acid));

        // If the clone was selected, the newly unlinked group should stay selected
        if (original_target.was_selected && store_survivers) {
            _survivers.push_back(unlinked);
        }

        return _performEraseOperation(overlapping, false);
    } else {
        if (original_target.was_selected && store_survivers) {
            _survivers.push_back(original_target.item); // If the clone was selected, it should stay so
        }
        if (filtered_children.size() < children.size()) {
            auto non_eraseable_touched = [&](EraseTarget const &t) -> bool {
                if (!t.item || !_uncuttableItemType(t.item)) {
                    return false;
                }
                return t.item->collidesWith(eraser_in_group_coordinates);
            };
            if (std::any_of(children.begin(), children.end(), non_eraseable_touched)) {
                _setStatusBarMessage(_("Some objects could not be cut."));
            }
        }
        return false;
    }
}

/** Returns error flags for items that cannot be meaningfully erased in CUT mode */
EraserTool::Error EraserTool::_uncuttableItemType(SPItem *item)
{
    if (!item) {
        return NON_EXISTENT;
    } else if (is<SPImage>(item)) {
        return RASTER_IMAGE;
    } else if (_isStraightSegment(item)) {
        return NO_AREA_PATH;
    } else {
        return ALL_GOOD;
    }
}

/**
 * @brief Performs a boolean difference or cut operation which implements the CUT mode erasure.
 * @param target - the item to be erased.
 * @param store_survivers - whether the surviving selected items and their remains should be stored.
 * @return true on success, false on failure
 */
bool EraserTool::_booleanErase(EraseTarget target, bool store_survivers)
{
    if (!target.item) {
        return false;
    }
    XML::Document *xml_doc = _desktop->doc()->getReprDoc();
    XML::Node *duplicate_stroke = repr->duplicate(xml_doc);
    repr->parent()->appendChild(duplicate_stroke);
    Glib::ustring duplicate_id = duplicate_stroke->attribute("id");
    GC::release(duplicate_stroke); // parent takes over
    ObjectSet operands(_desktop);
    operands.set(duplicate_stroke);
    if (!nowidth) {
        operands.pathUnion(true, true);
    }
    operands.add(target.item);
    operands.removeLPESRecursive(true);

    _handleStrokeStyle(target.item);

    if (nowidth) {
        operands.pathCut(true, true);
    } else {
        operands.pathDiff(true, true);
    }
    if (auto *spill = _desktop->doc()->getObjectById(duplicate_id)) {
        operands.remove(spill);
        spill->deleteObject(false);
        return false;
    }
    if (!_break_apart) {
        operands.combine(true, true);
    } else if (!nowidth) {
        operands.breakApart(true, false, true);
    }
    if (store_survivers && target.was_selected) {
        _survivers.insert(_survivers.end(), operands.items().begin(), operands.items().end());
    }
    return true;
}

/**
 * @brief Performs the actual erasing on a collection of erase targets.
 *        In CUT mode, the optional survivers vector will be populated with leftover pieces of
 *        partially erased shapes that used to be selected.
 * @param items_to_erase - a non-empty vector of erase targets.
 * @param store_survivers - whether the surviving selected items and their remains should be stored.
 * @return whether something was actually erased.
 */
bool EraserTool::_performEraseOperation(std::vector<EraseTarget> const &items_to_erase, bool store_survivers)
{
    if (mode == EraserToolMode::CUT) {
        bool erased_something = false;
        for (auto const &target : items_to_erase) {
            erased_something = _cutErase(target, store_survivers) || erased_something;
        }
        return erased_something;
    } else if (mode == EraserToolMode::CLIP) {
        if (nowidth) {
            return false;
        }
        for (auto const &target : items_to_erase) {
            _clipErase(target.item);
        }
        return true;
    } else { // mode == EraserToolMode::DELETE
        for (auto const &target : items_to_erase) {
            if (target.item) {
                target.item->deleteObject(true);
            }
        }
        return true;
    }
}

/** Handles the "evenodd" stroke style */
void EraserTool::_handleStrokeStyle(SPItem *item) const
{
    auto *style = item->style;
    if (style && style->fill_rule.value == SP_WIND_RULE_EVENODD) {
        SPCSSAttr *css = sp_repr_css_attr_new();
        sp_repr_css_set_property(css, "fill-rule", "evenodd");
        sp_desktop_set_style(_desktop, css);
        sp_repr_css_attr_unref(css);
        css = nullptr;
    }
}

/** Sets an error message in the status bar */
void EraserTool::_setStatusBarMessage(char *message)
{
    MessageId id = _desktop->messageStack()->flash(WARNING_MESSAGE, message);
    _our_messages.push_back(id);
}

/** Clears all of messages sent by us to the status bar */
void EraserTool::_clearStatusBar()
{
    if (!_our_messages.empty()) {
        auto ms = _desktop->messageStack();
        for (MessageId id : _our_messages) {
            ms->cancel(id);
        }
        _our_messages.clear();
    }
}

/** Clips through an item */
void EraserTool::_clipErase(SPItem *item) const
{
    Inkscape::ObjectSet w_selection(_desktop);
    Geom::OptRect bbox = item->documentVisualBounds();
    Inkscape::XML::Document *xml_doc = _desktop->doc()->getReprDoc();
    Inkscape::XML::Node *dup = repr->duplicate(xml_doc);
    repr->parent()->appendChild(dup);
    Inkscape::GC::release(dup); // parent takes over
    w_selection.set(dup);
    w_selection.pathUnion(true);
    bool delete_old_clip_path = false;
    SPClipPath *clip_path = item->getClipObject();
    if (clip_path) {
        std::vector<SPItem *> selected;
        selected.push_back(cast<SPItem>(clip_path->firstChild()));
        std::vector<Inkscape::XML::Node *> to_select;
        std::vector<SPItem *> items(selected);
        sp_item_list_to_curves(items, selected, to_select);
        Inkscape::XML::Node *clip_data = cast<SPItem>(clip_path->firstChild())->getRepr();
        if (!clip_data && !to_select.empty()) {
            clip_data = *(to_select.begin());
        }
        if (clip_data) {
            Inkscape::XML::Node *dup_clip = clip_data->duplicate(xml_doc);
            if (dup_clip) {
                auto dup_clip_obj = cast<SPItem>(item->parent->appendChildRepr(dup_clip));
                Inkscape::GC::release(dup_clip);
                if (dup_clip_obj) {
                    dup_clip_obj->transform *= item->getRelativeTransform(cast<SPItem>(item->parent));
                    dup_clip_obj->updateRepr();
                    delete_old_clip_path = true;
                    w_selection.raiseToTop(true);
                    w_selection.add(dup_clip);
                    w_selection.pathDiff(true, true);
                }
            }
        }
    } else {
        Inkscape::XML::Node *rect_repr = xml_doc->createElement("svg:rect");
        sp_desktop_apply_style_tool(_desktop, rect_repr, "/tools/eraser", false);
        auto rect = cast<SPRect>(item->parent->appendChildRepr(rect_repr));
        Inkscape::GC::release(rect_repr);
        rect->setPosition(bbox->left(), bbox->top(), bbox->width(), bbox->height());
        rect->transform = cast<SPItem>(rect->parent)->i2doc_affine().inverse();

        rect->updateRepr();
        rect->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
        w_selection.raiseToTop(true);
        w_selection.add(rect);
        w_selection.pathDiff(true, true);
    }
    w_selection.raiseToTop(true);
    w_selection.add(item);
    w_selection.setMask(true, false, true);
    if (delete_old_clip_path) {
        clip_path->deleteObject(true);
    }
}

/** Detects whether the given path is a straight line segment which encloses no area
 or consists of several such segments */
bool EraserTool::_isStraightSegment(SPItem *path)
{
    auto as_path = cast<SPPath>(path);
    if (!as_path) {
        return false;
    }

    auto const &curve = as_path->curve();
    if (!curve) {
        return false;
    }
    auto const &pathvector = curve->get_pathvector();

    // Check if all segments are straight and collinear
    for (auto const &path : pathvector) {
        Geom::Point initial_tangent = path.front().unitTangentAt(0.0);
        for (auto const &segment : path) {
            if (!segment.isLineSegment()) {
                return false;
            } else {
                Geom::Point dir = segment.unitTangentAt(0.0);
                if (!Geom::are_near(dir, initial_tangent) && !Geom::are_near(-dir, initial_tangent)) {
                    return false;
                }
            }
        }
    }
    return true;
}

void EraserTool::_addCap(SPCurve &curve, Geom::Point const &pre, Geom::Point const &from, Geom::Point const &to,
                         Geom::Point const &post, double rounding)
{
    Geom::Point vel = rounding * Geom::rot90(to - from) / M_SQRT2;
    double mag = Geom::L2(vel);

    Geom::Point v_in = from - pre;
    double mag_in = Geom::L2(v_in);

    if (mag_in > epsilon) {
        v_in = mag * v_in / mag_in;
    } else {
        v_in = Geom::Point(0, 0);
    }

    Geom::Point v_out = to - post;
    double mag_out = Geom::L2(v_out);

    if (mag_out > epsilon) {
        v_out = mag * v_out / mag_out;
    } else {
        v_out = Geom::Point(0, 0);
    }

    if (Geom::L2(v_in) > epsilon || Geom::L2(v_out) > epsilon) {
        curve.curveto(from + v_in, to + v_out, to);
    }
}

void EraserTool::_accumulate()
{
    // construct a crude outline of the eraser's path.
    // this desperately needs to be rewritten to use the path outliner...
    if (!cal1.get_segment_count() || !cal2.get_segment_count()) {
        return;
    }

    auto rev_cal2 = cal2.reversed();

    g_assert(!cal1.first_path()->closed());
    g_assert(!rev_cal2.first_path()->closed());

    Geom::BezierCurve const *dc_cal1_firstseg  = dynamic_cast<Geom::BezierCurve const *>(cal1.first_segment());
    Geom::BezierCurve const *rev_cal2_firstseg = dynamic_cast<Geom::BezierCurve const *>(rev_cal2.first_segment());
    Geom::BezierCurve const *dc_cal1_lastseg   = dynamic_cast<Geom::BezierCurve const *>(cal1.last_segment());
    Geom::BezierCurve const *rev_cal2_lastseg  = dynamic_cast<Geom::BezierCurve const *>(rev_cal2.last_segment());

    g_assert(dc_cal1_firstseg);
    g_assert(rev_cal2_firstseg);
    g_assert(dc_cal1_lastseg);
    g_assert(rev_cal2_lastseg);

    accumulated.append(cal1);
    if (!nowidth) {
        _addCap(accumulated,
                dc_cal1_lastseg->finalPoint() - dc_cal1_lastseg->unitTangentAt(1),
                dc_cal1_lastseg->finalPoint(),
                rev_cal2_firstseg->initialPoint(),
                rev_cal2_firstseg->initialPoint() + rev_cal2_firstseg->unitTangentAt(0),
                cap_rounding);

        accumulated.append(rev_cal2, true);

        _addCap(accumulated,
                rev_cal2_lastseg->finalPoint() - rev_cal2_lastseg->unitTangentAt(1),
                rev_cal2_lastseg->finalPoint(),
                dc_cal1_firstseg->initialPoint(),
                dc_cal1_firstseg->initialPoint() + dc_cal1_firstseg->unitTangentAt(0),
                cap_rounding);

        accumulated.closepath();
    }
    cal1.reset();
    cal2.reset();
}

/**
 *  @brief Filters out elements that can be erased in CUT mode (by boolean operations) from the given
 *         vector of potential erase targets. For items that cannot be erased in the CUT mode, a
 *         warning message can be flashed in the status bar.
 *  @param items - a vector containing EraseTarget structs
 *  @param silent - if set to true, the status bar messages will not be shown.
 *  @return a filtered vector whose elements can be erased in CUT mode
*/
std::vector<EraseTarget> EraserTool::_filterCutEraseables(std::vector<EraseTarget> const &items, bool silent)
{
    std::vector<EraseTarget> result;
    result.reserve(items.size());

    for (auto &target : items) {
        if (Error e = _uncuttableItemType(target.item)) {
            if (!silent) {
                if (e & RASTER_IMAGE) {
                    _setStatusBarMessage(_("Cannot cut out from a bitmap, use <b>Clip</b> mode "
                                           "instead."));
                } else if (e & NO_AREA_PATH) {
                    _setStatusBarMessage(_("Cannot cut out from a path with zero area, use "
                                           "<b>Clip</b> mode instead."));
                }
            }
        } else {
            result.push_back(target);
        }
    }
    return result;
}

/**
 * @brief Filters a list of potential erase targets by collision with a given item
 * @param items - a vector of EraseTarget elements to be filtered
 * @param with - a pointer to an SPItem to check collisions with
 * @return a new vector containing those elements of `items` that have a collision with `with`.
 */
std::vector<EraseTarget> EraserTool::_filterByCollision(std::vector<EraseTarget> const &items, SPItem *with) const
{
    std::vector<EraseTarget> result;
    if (!with) {
        return result;
    }
    result.reserve(items.size());

    if (auto const collision_shape = with->documentExactBounds()) {
        for (auto const &target : items) {
            if (target.item && target.item->collidesWith(*collision_shape)) {
                result.push_back(target);
            }
        }
    }
    return result;
}

/**
 * @brief Prepares a list of items in the current document containing the items which qualify
 *        for the erase operation (based on selection & collision detection).
 *        Additionally, the selected items which are going to survive the erase operation (and
 *        should be used to restore the selection afterwards) will be added to the _survivers member.
 *        If the user attempts to erase an illegal item, a warning message is shown in the status bar.
 * @return items that should undergo the erase operation
 */
std::vector<EraseTarget> EraserTool::_findItemsToErase()
{
    std::vector<EraseTarget> result;

    auto *document = _desktop->getDocument();
    auto *selection = _desktop->getSelection();
    if (!document || !selection) {
        return result;
    }

    if (mode == EraserToolMode::DELETE) {
        // In DELETE mode, the classification is based on having been touched by the mouse cursor:
        // * result     should contain touched items;
        // * _survivers should contain selected but untouched items.
        auto *r = Rubberband::get(_desktop);
        std::vector<SPItem *> touched = document->getItemsAtPoints(_desktop->dkey, r->getPoints());
        if (selection->isEmpty()) {
            for (auto *item : touched) {
                result.emplace_back(item, false);
            }
        } else {
            for (auto *item : selection->items()) {
                if (std::find(touched.begin(), touched.end(), item) == touched.end()) {
                    _survivers.push_back(item);
                } else {
                    result.emplace_back(item, true);
                }
            }
        }
    } else {
        // In the other modes, we start with a crude filtering step based on bounding boxes
        _acid = _insertAcidIntoDocument(document);
        if (!_acid) {
            return result;
        }
        Geom::OptRect eraser_bbox = _acid->documentVisualBounds();
        if (!eraser_bbox) {
            return result;
        }
        std::vector<SPItem *> candidates = document->getItemsPartiallyInBox(_desktop->dkey, *eraser_bbox,
                                                                            false, false, false, true);
        std::vector<EraseTarget> allowed; ///< Items we're allowed to erase based on selection
        allowed.reserve(candidates.size());

        // If selection is empty, we're allowed to erase all items except the eraser stroke itself.
        if (selection->isEmpty()) {
            for (auto *candidate : candidates) {
                if (candidate != _acid) {
                    allowed.emplace_back(candidate, false);
                }
            }
        } // How we handle non-empty selection further depends on the mode.

        if (mode == EraserToolMode::CUT) {
            // In CUT mode, we must unpack groups, since the boolean difference/cut operation
            // doesn't make sense for a group.
            for (auto *selected : selection->items()) {
                bool included_for_erase = false;
                for (auto *candidate : candidates) {
                    if (selected == candidate || selected->isAncestorOf(candidate)) {
                        allowed.emplace_back(candidate, selection->includes(candidate));
                        included_for_erase = (candidate == selected) || included_for_erase;
                    }
                }
                if (!included_for_erase) {
                    _survivers.push_back(selected);
                }
            }
            // The filtering is based on a precise collision detection procedure:
            // * result     will contain all eraseable items that overlap with the eraser stroke;
            // * _survivers will contain all selected items that were rejected during this filtering.
            auto overlapping = _filterByCollision(allowed, _acid);
            auto valid = _filterCutEraseables(overlapping); // Sets status bar messages

            for (auto const &element : allowed) {
                if (element.item && element.was_selected &&
                    std::find(valid.begin(), valid.end(), element) == valid.end())
                {
                    _survivers.push_back(element.item);
                }
            }
            result.insert(result.end(), valid.begin(), valid.end());

        } else if (mode == EraserToolMode::CLIP) {
            // In CLIP mode, we don't check descendants, because clip can be set to an entire group.
            auto const all_selected = selection->items();
            for (auto *item : all_selected) {
                allowed.emplace_back(item, true);
            }

            // The classification is also based on the precise collision detection:
            // * result     will contain all items that overlap with the eraser stroke;
            // * _survivers will contain all selected items, since CLIP mode is always non-destructive.
            auto overlapping = _filterByCollision(allowed, _acid);
            result.insert(result.end(), overlapping.begin(), overlapping.end());
            _survivers.insert(_survivers.end(), all_selected.begin(), all_selected.end());
        }
    }
    return result;
}

void EraserTool::_fitAndSplit(bool releasing)
{
    double const tolerance_sq = square(_desktop->w2d().descrim() * tolerance);
    nowidth = (width == 0); // setting width is managed by the base class

#ifdef ERASER_VERBOSE
    g_print("[F&S:R=%c]", releasing ? 'T' : 'F');
#endif
    if (npoints >= SAMPLING_SIZE || npoints <= 0) {
        return; // just clicked
    }

    if (npoints == SAMPLING_SIZE - 1 || releasing) {
        _completeBezier(tolerance_sq, releasing);

#ifdef ERASER_VERBOSE
        g_print("[%d]Yup\n", npoints);
#endif
        if (!releasing) {
            _fitDrawLastPoint();
        }

        // Copy last point
        point1[0] = point1[npoints - 1];
        point2[0] = point2[npoints - 1];
        npoints = 1;
    } else {
        _drawTemporaryBox();
    }
}

void EraserTool::_completeBezier(double tolerance_sq, bool releasing)
{
    /* Current eraser */
    if (cal1.is_empty() || cal2.is_empty()) {
        /* dc->npoints > 0 */
        cal1.reset();
        cal2.reset();

        cal1.moveto(point1[0]);
        cal2.moveto(point2[0]);
    }
#ifdef ERASER_VERBOSE
    g_print("[F&S:#] npoints:%d, releasing:%s\n", npoints, releasing ? "TRUE" : "FALSE");
#endif

    unsigned const bezier_size = 4;
    unsigned const max_beziers = 8;
    size_t const bezier_max_length = bezier_size * max_beziers;

    Geom::Point b1[bezier_max_length];
    gint const nb1 = Geom::bezier_fit_cubic_r(b1, point1, npoints, tolerance_sq, max_beziers);
    g_assert(nb1 * bezier_size <= gint(G_N_ELEMENTS(b1)));

    Geom::Point b2[bezier_max_length];
    gint const nb2 = Geom::bezier_fit_cubic_r(b2, point2, npoints, tolerance_sq, max_beziers);
    g_assert(nb2 * bezier_size <= gint(G_N_ELEMENTS(b2)));

    if (nb1 == -1 || nb2 == -1) {
        _failedBezierFallback(); // TODO: do we ever need this?
        return;
    }

    /* Fit and draw and reset state */
#ifdef ERASER_VERBOSE
    g_print("nb1:%d nb2:%d\n", nb1, nb2);
#endif

    /* CanvasShape */
    if (!releasing) {
        currentcurve.reset();
        currentcurve.moveto(b1[0]);

        for (Geom::Point *bp1 = b1; bp1 < b1 + bezier_size * nb1; bp1 += bezier_size) {
            currentcurve.curveto(bp1[1], bp1[2], bp1[3]);
        }

        currentcurve.lineto(b2[bezier_size * (nb2 - 1) + 3]);

        for (Geom::Point *bp2 = b2 + bezier_size * (nb2 - 1); bp2 >= b2; bp2 -= bezier_size) {
            currentcurve.curveto(bp2[2], bp2[1], bp2[0]);
        }

        // FIXME: segments is always NULL at this point??
        if (segments.empty()) { // first segment
            _addCap(currentcurve, b2[1], b2[0], b1[0], b1[1], cap_rounding);
        }

        currentcurve.closepath();
        currentshape->set_bpath(&currentcurve, true);
    }

    /* Current eraser */
    for (Geom::Point *bp1 = b1; bp1 < b1 + bezier_size * nb1; bp1 += bezier_size) {
        cal1.curveto(bp1[1], bp1[2], bp1[3]);
    }

    for (Geom::Point *bp2 = b2; bp2 < b2 + bezier_size * nb2; bp2 += bezier_size) {
        cal2.curveto(bp2[1], bp2[2], bp2[3]);
    }
}

void EraserTool::_failedBezierFallback()
{
    /* fixme: ??? */
#ifdef ERASER_VERBOSE
    g_print("[_failedBezierFallback] - failed to fit cubic.\n");
#endif
    _drawTemporaryBox();

    for (gint i = 1; i < npoints; i++) {
        cal1.lineto(point1[i]);
    }

    for (gint i = 1; i < npoints; i++) {
        cal2.lineto(point2[i]);
    }
}

void EraserTool::_fitDrawLastPoint()
{
    g_assert(!currentcurve.is_empty());

    guint32 fillColor = sp_desktop_get_color_tool(_desktop, "/tools/eraser", true);
    double opacity = sp_desktop_get_master_opacity_tool(_desktop, "/tools/eraser");
    double fillOpacity = sp_desktop_get_opacity_tool(_desktop, "/tools/eraser", true);

    guint fill = (fillColor & 0xffffff00) | SP_COLOR_F_TO_U(opacity * fillOpacity);

    auto cbp = new Inkscape::CanvasItemBpath(_desktop->getCanvasSketch(), currentcurve.get_pathvector(), true);
    cbp->set_fill(fill, trace_wind_rule);
    cbp->set_stroke(0x0);

    /* fixme: Cannot we cascade it to root more clearly? */
    cbp->connect_event(sigc::bind(sigc::ptr_fun(sp_desktop_root_handler), _desktop));
    segments.emplace_back(cbp);

    if (mode == EraserToolMode::DELETE) {
        cbp->set_visible(false);
        currentshape->set_visible(false);
    }
}

void EraserTool::_drawTemporaryBox()
{
    currentcurve.reset();

    currentcurve.moveto(point1[npoints - 1]);

    for (gint i = npoints - 2; i >= 0; i--) {
        currentcurve.lineto(point1[i]);
    }

    for (gint i = 0; i < npoints; i++) {
        currentcurve.lineto(point2[i]);
    }

    if (npoints >= 2) {
        _addCap(currentcurve,
                point2[npoints - 2], point2[npoints - 1],
                point1[npoints - 1], point1[npoints - 2], cap_rounding);
    }

    currentcurve.closepath();
    currentshape->set_bpath(&currentcurve, true);
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
