// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Gio::Actions for changing the canvas display mode. Tied to a particular InkscapeWindow.
 *
 * Copyright (C) 2020 Tavmjong Bah
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 *
 */

#include "actions-canvas-mode.h"

#include <iostream>

#include <giomm.h>  // Not <gtkmm.h>! To eventually allow a headless version!
#include <glibmm/i18n.h>

#include "ui/interface.h"

#include "actions-helper.h"

#include "desktop.h"
#include "inkscape-application.h"
#include "inkscape-window.h"

#include "display/rendermode.h"
#include "display/drawing.h"  // Setting gray scale parameters.
#include "display/control/canvas-item-drawing.h"

#include "ui/widget/canvas.h"

// TODO: Use action state rather than set variable in Canvas (via Desktop).
// TODO: Move functions from Desktop to Canvas.
// TODO: Canvas actions should belong to canvas (not window)!

/**
 * Helper function to set display mode.
 */
void
canvas_set_display_mode(Inkscape::RenderMode value, InkscapeWindow *win, Glib::RefPtr<Gio::SimpleAction> saction)
{
    g_assert(value != Inkscape::RenderMode::size);
    saction->change_state((int)value);

    // Save value as a preference
    Inkscape::Preferences *pref = Inkscape::Preferences::get();
    pref->setInt("/options/displaymode", (int)value);

    SPDesktop* dt = win->get_desktop();
    auto canvas = dt->getCanvas();
    canvas->set_render_mode(Inkscape::RenderMode(value));
}

/**
 * Set display mode.
 */
void
canvas_display_mode(int value, InkscapeWindow *win)
{
    if (value < 0 || value >= (int)Inkscape::RenderMode::size) {
        show_output(Glib::ustring("canvas_display_mode: value out of bound! : ") + Glib::ustring::format(value));
        return;
    }

    auto action = win->lookup_action("canvas-display-mode");
    if (!action) {
        show_output("canvas_display_mode: action 'canvas-display-mode' missing!");
        return;
    }

    auto saction = std::dynamic_pointer_cast<Gio::SimpleAction>(action);
    if (!saction) {
        show_output("canvas_display_mode: action 'canvas-display-mode' not SimpleAction!");
        return;
    }

    canvas_set_display_mode(Inkscape::RenderMode(value), win, saction);
}

/**
 * Cycle between values.
 */
void
canvas_display_mode_cycle(InkscapeWindow *win)
{
    auto action = win->lookup_action("canvas-display-mode");
    if (!action) {
        show_output("canvas_display_mode_cycle: action 'canvas-display-mode' missing!");
        return;
    }

    auto saction = std::dynamic_pointer_cast<Gio::SimpleAction>(action);
    if (!saction) {
        show_output("canvas_display_mode_cycle: action 'canvas-display-mode' not SimpleAction!");
        return;
    }

    int value = -1;
    saction->get_state(value);
    // TODO: match order of UI instead
    value++;
    value %= (int)Inkscape::RenderMode::size;

    saction->activate_variant(Glib::Variant<int>::create(value));
}


/**
 * Toggle between normal and last set other value.
 */
void
canvas_display_mode_toggle(InkscapeWindow *win)
{
    auto action = win->lookup_action("canvas-display-mode");
    if (!action) {
        show_output("canvas_display_mode_toggle: action 'canvas-display-mode' missing!");
        return;
    }

    auto saction = std::dynamic_pointer_cast<Gio::SimpleAction>(action);
    if (!saction) {
        show_output("canvas_display_mode_toogle: action 'canvas-display-mode' not SimpleAction!");
        return;
    }

    static Inkscape::RenderMode old_value = Inkscape::RenderMode::OUTLINE;

    int value = -1;
    saction->get_state(value);
    int new_value = 0;
    const int normal = static_cast<int>(Inkscape::RenderMode::NORMAL);

    if (value == normal) {
        new_value = static_cast<int>(old_value);
    } else {
        old_value = Inkscape::RenderMode(value);
        new_value = normal;
    }
    saction->activate_variant(Glib::Variant<int>::create(new_value));
}

/**
 * Set split mode.
 */
void
canvas_split_mode(int value, InkscapeWindow *win)
{
    if (value < 0 || value >= (int)Inkscape::SplitMode::size) {
        show_output("canvas_split_mode: value out of bound! : " + Glib::ustring::format(value));
        return;
    }

    auto action = win->lookup_action("canvas-split-mode");
    if (!action) {
        show_output("canvas_split_mode: action 'canvas-split-mode' missing!");
        return;
    }

    auto saction = std::dynamic_pointer_cast<Gio::SimpleAction>(action);
    if (!saction) {
        show_output("canvas_split_mode: action 'canvas-split-mode' not SimpleAction!");
        return;
    }

    // If split mode is already set to the requested mode, turn it off.
    int old_value = -1;
    saction->get_state(old_value);
    if (value == old_value) {
        value = (int)Inkscape::SplitMode::NORMAL;
    }

    saction->change_state(value);

    SPDesktop* dt = win->get_desktop();
    auto canvas = dt->getCanvas();
    canvas->set_split_mode(Inkscape::SplitMode(value));
}

/**
 * Set gray scale for canvas.
 */
void
canvas_color_mode_gray(InkscapeWindow *win)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    gdouble r = prefs->getDoubleLimited("/options/rendering/grayscale/red-factor",   0.21,  0.0, 1.0);
    gdouble g = prefs->getDoubleLimited("/options/rendering/grayscale/green-factor", 0.72,  0.0, 1.0);
    gdouble b = prefs->getDoubleLimited("/options/rendering/grayscale/blue-factor",  0.072, 0.0, 1.0);
    gdouble grayscale_value_matrix[20] =
        { r, g, b, 0, 0,
          r, g, b, 0, 0,
          r, g, b, 0, 0,
          0, 0, 0, 1, 0 };
    SPDesktop* dt = win->get_desktop();
    dt->getCanvasDrawing()->get_drawing()->setGrayscaleMatrix(grayscale_value_matrix);
}

/**
 * Toggle Gray scale on/off.
 */
void
canvas_color_mode_toggle(InkscapeWindow *win)
{
    auto action = win->lookup_action("canvas-color-mode");
    if (!action) {
        show_output("canvas_color_mode_toggle: action missing!");
        return;
    }

    auto saction = std::dynamic_pointer_cast<Gio::SimpleAction>(action);
    if (!saction) {
        show_output("canvas_color_mode_toggle: action not SimpleAction!");
        return;
    }

    bool state = false;
    saction->get_state(state);
    state = !state;
    saction->change_state(state);

    if (state) {
        // Set gray scale parameters.
        canvas_color_mode_gray(win);
    }

    SPDesktop* dt = win->get_desktop();
    auto canvas = dt->getCanvas();
    canvas->set_color_mode(state ? Inkscape::ColorMode::GRAYSCALE : Inkscape::ColorMode::NORMAL);
}


/**
 * Toggle Color management on/off.
 */
void
canvas_color_manage_toggle(InkscapeWindow *win)
{
    auto action = win->lookup_action("canvas-color-manage");
    if (!action) {
        show_output("canvas_color_manage_toggle: action missing!");
        return;
    }

    auto saction = std::dynamic_pointer_cast<Gio::SimpleAction>(action);
    if (!saction) {
        show_output("canvas_color_manage_toggle: action not SimpleAction!");
        return;
    }

    bool state = false;
    saction->get_state(state);
    state = !state;
    saction->change_state(state);

    // Save value as a preference
    Inkscape::Preferences *pref = Inkscape::Preferences::get();
    pref->setBool("/options/displayprofile/enable", state);

    SPDesktop* dt = win->get_desktop();
    auto canvas = dt->getCanvas();
    canvas->set_cms_active(state);
    canvas->redraw_all();
}

std::vector<std::vector<Glib::ustring>> raw_data_canvas_mode =
{
    // clang-format off
    {"win.canvas-display-mode(0)",              N_("Display Mode: Normal"),             "Canvas Display",    N_("Use normal rendering mode")                     },
    {"win.canvas-display-mode(1)",              N_("Display Mode: Outline"),            "Canvas Display",    N_("Show only object outlines")                     },
    {"win.canvas-display-mode(2)",              N_("Display Mode: No Filters"),         "Canvas Display",    N_("Do not render filters (for speed)")             },
    {"win.canvas-display-mode(3)",              N_("Display Mode: Enhance Thin Lines"), "Canvas Display",    N_("Ensure all strokes are displayed on screen as at least 1 pixel wide")},
    {"win.canvas-display-mode(4)",              N_("Display Mode: Outline Overlay"),    "Canvas Display",    N_("Show objects as outlines, and the actual drawing below them with reduced opacity")},
    {"win.canvas-display-mode-cycle",           N_("Display Mode: Cycle"),              "Canvas Display",    N_("Cycle through display modes")                   },
    {"win.canvas-display-mode-toggle",          N_("Display Mode: Toggle"),             "Canvas Display",    N_("Toggle between normal and last non-normal mode")},
    {"win.canvas-display-mode-toggle-preview",  N_("Display Mode: Toggle Preview"),     "Canvas Display",    N_("Toggle between preview and previous mode")      },

    {"win.canvas-split-mode(0)",                N_("Split Mode: Normal"),            "Canvas Display",    N_("Do not split canvas")                              },
    {"win.canvas-split-mode(1)",                N_("Split Mode: Split"),             "Canvas Display",    N_("Render part of the canvas in outline mode")        },
    {"win.canvas-split-mode(2)",                N_("Split Mode: X-Ray"),             "Canvas Display",    N_("Render a circular area in outline mode")           },

    {"win.canvas-color-mode",                   N_("Color Mode"),                    "Canvas Display",    N_("Toggle between normal and grayscale modes")        },
    {"win.canvas-color-manage",                 N_("Color Managed Mode"),            "Canvas Display",    N_("Toggle between normal and color managed modes")    }
    // clang-format on
};

void
add_actions_canvas_mode(InkscapeWindow* win)
{
    // Sync action with desktop variables. TODO: Remove!
    auto prefs = Inkscape::Preferences::get();

    // Initial States of Actions
    int  display_mode       = prefs->getIntLimited("/options/displaymode", 0, 0, static_cast<int>(Inkscape::RenderMode::size) - 1);  // Default, minimum, maximum
    bool color_manage       = prefs->getBool("/options/displayprofile/enable");

    SPDesktop* dt = win->get_desktop();
    if (dt) {
        auto canvas = dt->getCanvas();
        canvas->set_render_mode(Inkscape::RenderMode(display_mode));
        canvas->set_cms_active(color_manage);
    } else {
        show_output("add_actions_canvas_mode: no desktop!");
    }

    // clang-format off
    win->add_action_radio_integer ("canvas-display-mode",                 sigc::bind(sigc::ptr_fun(&canvas_display_mode),                win), display_mode);
    win->add_action(               "canvas-display-mode-cycle",           sigc::bind(sigc::ptr_fun(&canvas_display_mode_cycle),          win));
    win->add_action(               "canvas-display-mode-toggle",          sigc::bind(sigc::ptr_fun(&canvas_display_mode_toggle),         win));
    win->add_action_radio_integer ("canvas-split-mode",                   sigc::bind(sigc::ptr_fun(&canvas_split_mode),                  win), (int)Inkscape::SplitMode::NORMAL);
    win->add_action_bool(          "canvas-color-mode",                   sigc::bind(sigc::ptr_fun(&canvas_color_mode_toggle),           win));
    win->add_action_bool(          "canvas-color-manage",                 sigc::bind(sigc::ptr_fun(&canvas_color_manage_toggle),         win), color_manage);
    // clang-format on

    auto app = InkscapeApplication::instance();
    if (!app) {
        show_output("add_actions_canvas_mode: no app!");
        return;
    }
    app->get_action_extra_data().add_data(raw_data_canvas_mode);
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
