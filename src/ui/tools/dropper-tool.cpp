// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Tool for picking colors from drawing
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Abhishek Sharma
 *
 * Copyright (C) 1999-2005 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <glibmm/i18n.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>

#include <2geom/transforms.h>
#include <2geom/circle.h>

#include "desktop-style.h"
#include "desktop.h"
#include "document-undo.h"
#include "message-context.h"
#include "preferences.h"
#include "selection.h"
#include "style.h"
#include "page-manager.h"

#include "display/drawing.h"
#include "display/control/canvas-item-bpath.h"
#include "display/control/canvas-item-drawing.h"

#include "object/sp-namedview.h"

#include "svg/svg-color.h"

#include "ui/cursor-utils.h"
#include "ui/icon-names.h"
#include "ui/tools/dropper-tool.h"
#include "ui/widget/canvas.h"
#include "ui/widget/events/canvas-event.h"

using Inkscape::DocumentUndo;

namespace Inkscape::UI::Tools {

enum PickMode
{
    PICK_VISIBLE,
    PICK_ACTUAL
};

DropperTool::DropperTool(SPDesktop *desktop)
    : ToolBase(desktop, "/tools/dropper", "dropper-pick-fill.svg")
{
    area = make_canvasitem<CanvasItemBpath>(desktop->getCanvasControls());
    area->set_stroke(0x0000007f);
    area->set_fill(0x0, SP_WIND_RULE_EVENODD);
    area->set_visible(false);

    auto prefs = Preferences::get();
    
    if (prefs->getBool("/tools/dropper/selcue")) {
        enableSelectionCue();
    }

    if (prefs->getBool("/tools/dropper/gradientdrag")) {
        enableGrDrag();
    }
}

DropperTool::~DropperTool()
{
    enableGrDrag(false);
    ungrabCanvasEvents();
}

/**
 * Returns the current dropper context color.
 *
 * - If in dropping mode, returns color from selected objects.
 *   Ignored if non_dropping set to true.
 * - If in dragging mode, returns average color on canvas, depending on radius.
 * - If in pick mode, alpha is not premultiplied. Alpha is only set if in pick mode
 *   and setalpha is true. Both values are taken from preferences.
 *
 * @param invert If true, invert the rgb value
 * @param non_dropping If true, use color from canvas, even in dropping mode.
 */
uint32_t DropperTool::get_color(bool invert, bool non_dropping) const
{
    auto prefs = Preferences::get();

    int pick = prefs->getInt("/tools/dropper/pick", PICK_VISIBLE);
    bool setalpha = prefs->getBool("/tools/dropper/setalpha", true);

    // non_dropping ignores dropping mode and always uses color from canvas.
    // Used by the clipboard
    double r = non_dropping ? non_dropping_R : R;
    double g = non_dropping ? non_dropping_G : G;
    double b = non_dropping ? non_dropping_B : B;
    double a = non_dropping ? non_dropping_A : alpha;

    return SP_RGBA32_F_COMPOSE(
        std::abs(invert - r),
        std::abs(invert - g),
        std::abs(invert - b),
        (pick == PICK_ACTUAL && setalpha) ? a : 1.0);
}

bool DropperTool::root_handler(CanvasEvent const &event)
{
    auto prefs = Preferences::get();
    int pick = prefs->getInt("/tools/dropper/pick", PICK_VISIBLE);

    // Decide first what kind of 'mode' we're in.
    auto modifiers = event.modifiersAfter();
    stroke   = modifiers & GDK_SHIFT_MASK;
    dropping = modifiers & GDK_CONTROL_MASK; // Even on macOS.
    invert   = modifiers & GDK_ALT_MASK;

    // Get color from selected object
    // Only if dropping mode enabled and object's color is set.
    // Otherwise dropping mode disabled.
    if (dropping) {
        auto selection = _desktop->getSelection();
        g_assert(selection);

        std::optional<uint32_t> apply_color;
        for (auto const &obj: selection->objects()) {
            if (obj->style) {
                double opacity = 1.0;
                if (!stroke && obj->style->fill.set) {
                    if (obj->style->fill_opacity.set) {
                        opacity = SP_SCALE24_TO_FLOAT(obj->style->fill_opacity.value);
                    }
                    apply_color = obj->style->fill.value.color.toRGBA32(opacity);
                } else if (stroke && obj->style->stroke.set) {
                    if (obj->style->stroke_opacity.set) {
                        opacity = SP_SCALE24_TO_FLOAT(obj->style->stroke_opacity.value);
                    }
                    apply_color = obj->style->stroke.value.color.toRGBA32(opacity);
                }
            }
        }

        if (apply_color) {
            R = SP_RGBA32_R_F(*apply_color);
            G = SP_RGBA32_G_F(*apply_color);
            B = SP_RGBA32_B_F(*apply_color);
            alpha = SP_RGBA32_A_F(*apply_color);
        } else {
            // This means that having no selection or some other error
            // we will default back to normal dropper mode.
            dropping = false;
        }
    }

    bool ret = false;
    bool self_destroyed = false;

    inspect_event(event,
        [&] (ButtonPressEvent const &event) {
            if (event.num_press != 1) {
                return;
            }

            if (event.button == 1) {
                centre = event.pos;
                dragging = true;
                ret = true;
            }

            grabCanvasEvents(EventType::KEY_PRESS      |
                             EventType::KEY_RELEASE    |
                             EventType::BUTTON_RELEASE |
                             EventType::MOTION         |
                             EventType::BUTTON_PRESS);
        },

        [&] (MotionEvent const &event) {
            if (event.modifiers & (GDK_BUTTON2_MASK | GDK_BUTTON3_MASK)) {
                // pass on middle and right drag
                return;
            }

            // otherwise, constantly calculate color no matter if any button pressed or not
            Geom::IntRect pick_area;
            if (dragging) {
                // Calculate average

                // Radius
                double rw = std::min((event.pos - centre).length(), 400.0);
                if (rw == 0) { // happens sometimes, little idea why...
                    return;
                }
                radius = rw;

                auto const cd = _desktop->w2d(centre);
                auto const w2dt = _desktop->w2d();
                auto const scale = rw * w2dt.descrim();
                auto const sm = Geom::Scale(scale) * Geom::Translate(cd);

                // Show circle on canvas.
                auto path = Geom::Path(Geom::Circle(0, 0, 1)); // Unit circle centered at origin.
                path *= sm;
                area->set_bpath(std::move(path));
                area->set_visible(true);

                // Get buffer
                auto r = Geom::Rect(centre, centre);
                r.expandBy(rw);
                if (!r.hasZeroArea()) {
                    pick_area = r.roundOutwards();
                }
            } else {
                // Pick single pixel
                pick_area = Geom::IntRect::from_xywh(0, 0, 1, 1) + event.pos.floor();
            }

            auto canvas_item_drawing = _desktop->getCanvasDrawing();
            auto drawing = canvas_item_drawing->get_drawing();

            // Get average color
            double R2, G2, B2, A2;
            drawing->averageColor(pick_area, R2, G2, B2, A2);

            if (pick == PICK_VISIBLE) {
                // Compose with page color
                auto bg = _desktop->getDocument()->getPageManager().getDefaultBackgroundColor();
                R2 = R2 + bg[0] * (1 - A2);
                G2 = G2 + bg[1] * (1 - A2);
                B2 = B2 + bg[2] * (1 - A2);
                A2 = 1.0;
            } else {
                // Un-premultiply color channels
                if (A2 > 0) {
                    R2 /= A2;
                    G2 /= A2;
                    B2 /= A2;
                }
            }

            if (std::abs(A2) < 1e-4) {
                A2 = 0; // Suppress exponentials, CSS does not allow that.
            }

            // Remember color
            if (!dropping) {
                R = R2;
                G = G2;
                B = B2;
                alpha = A2;
            }

            // Remember color from canvas, even in dropping mode.
            // These values are used by the clipboard.
            non_dropping_R = R2;
            non_dropping_G = G2;
            non_dropping_B = B2;
            non_dropping_A = A2;

            ret = true;
        },

        [&] (ButtonReleaseEvent const &event) {
            if (event.button != 1) {
                return;
            }

            area->set_visible(false);
            dragging = false;

            ungrabCanvasEvents();

            auto selection = _desktop->getSelection();
            g_assert(selection);

            auto old_selection = std::vector<SPItem*>(selection->items().begin(), selection->items().end());

            if (dropping) {
                auto const button_w = event.pos;
                // Remember clicked item, disregarding groups, honoring Alt.
                item_to_select = sp_event_context_find_item(_desktop, button_w, event.modifiers & GDK_ALT_MASK, true);

                // Change selected object to object under cursor.
                if (item_to_select) {
                    selection->set(item_to_select);
                }
            }

            auto picked_color = ColorRGBA(get_color(invert));

            // One time pick has active signal, call them all and clear.
            if (!onetimepick_signal.empty()) {
                onetimepick_signal.emit(&picked_color);
                onetimepick_signal.clear();
                // Do this last as it destroys the picker tool.
                sp_toggle_dropper(_desktop);
                self_destroyed = true;
                return;
            }

            // do the actual color setting
            sp_desktop_set_color(_desktop, picked_color, false, !stroke);

            // REJON: set aux. toolbar input to hex color!
            if (!_desktop->getSelection()->isEmpty()) {
                DocumentUndo::done(_desktop->getDocument(), _("Set picked color"), INKSCAPE_ICON("color-picker"));
            }

            if (dropping) {
                selection->setList(old_selection);
            }

            ret = true;
        },

        [&] (KeyPressEvent const &event) {
            switch (get_latin_keyval(event)) {
                case GDK_KEY_Up:
                case GDK_KEY_Down:
                case GDK_KEY_KP_Up:
                case GDK_KEY_KP_Down:
                    // Prevent the zoom field from activating.
                    if (!mod_ctrl_only(event)) {
                        ret = true;
                    }
                    break;
                case GDK_KEY_Escape:
                    _desktop->getSelection()->clear();
                    break;
                default:
                    break;
            }
        },

        [&] (CanvasEvent const &event) {}
    );

    if (self_destroyed) {
        return true;
    }

    // set the status message to the right text.
    char c[64];
    sp_svg_write_color(c, sizeof(c), get_color(invert));

    // alpha of color under cursor, to show in the statusbar
    // locale-sensitive printf is OK, since this goes to the UI, not into SVG
    auto alphastr = g_strdup_printf(_(" alpha %.3g"), alpha);
    // where the color is picked, to show in the statusbar
    auto where = dragging ? g_strdup_printf(_(", averaged with radius %d"), (int)radius) : g_strdup_printf("%s", _(" under cursor"));
    // message, to show in the statusbar
    auto message = dragging ? _("<b>Release mouse</b> to set color.") : _("<b>Click</b> to set fill, <b>Shift+click</b> to set stroke; <b>drag</b> to average color in area; with <b>Alt</b> to pick inverse color; <b>Ctrl+C</b> to copy the color under mouse to clipboard");

    defaultMessageContext()->setF(
        Inkscape::NORMAL_MESSAGE,
        "<b>%s%s</b>%s. %s", c,
        pick == PICK_VISIBLE ? "" : alphastr, where, message);

    g_free(where);
    g_free(alphastr);

    // Set the right cursor for the mode and apply the special Fill color
    _cursor_filename = dropping ? (stroke ? "dropper-drop-stroke.svg" : "dropper-drop-fill.svg")
                                : (stroke ? "dropper-pick-stroke.svg" : "dropper-pick-fill.svg");

    // We do this ourselves to get color correct.
    set_svg_cursor(*_desktop->getCanvas(), _cursor_filename, get_color(invert));

    if (!ret) {
        ret = ToolBase::root_handler(event);
    }

    return ret;
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
