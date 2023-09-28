// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 *
 * Actions related to View mode
 *
 * Authors:
 *   Sushant A A <sushant.co19@gmail.com>
 *   Tavmjong Bah
 *
 * Copyright (C) 2021, 2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <giomm.h>
#include <glibmm/i18n.h>

#include "actions-view-mode.h"
#include "actions-helper.h"

#include "desktop.h"
#include "inkscape-application.h"
#include "inkscape-window.h"

#include "object/sp-namedview.h"

#include "ui/monitor.h"   // Monitor aspect ratio
#include "ui/widget/canvas.h"

#include "ui/widget/desktop-widget.h"

// Helper function to set state.
void
canvas_set_state(InkscapeWindow *win, Glib::ustring action_name, bool state)
{
    // Get Action
    auto action = win->lookup_action(action_name);
    if (!action) {
        show_output(Glib::ustring("canvas_set_state: ") + action_name.raw() + " action missing!");
        return;
    }

    auto saction = std::dynamic_pointer_cast<Gio::SimpleAction>(action);
    if (!saction) {
        show_output(Glib::ustring("canvas_set_state: ") + action_name.raw() + " not SimpleAction!");
        return;
    }

    // Set State
    saction->change_state(state);
}

// Helper function to toggle state.
bool
canvas_toggle_state(InkscapeWindow *win, Glib::ustring action_name)
{
    // Get Action
    auto action = win->lookup_action(action_name);
    if (!action) {
        show_output(Glib::ustring("canvas_toggle_state: ") + action_name.raw() + " action missing!");
        return false;
    }

    auto saction = std::dynamic_pointer_cast<Gio::SimpleAction>(action);
    if (!saction) {
        show_output(Glib::ustring("canvas_toggle_state: ") + action_name.raw() + " not SimpleAction!");
        return false;
    }

    // Toggle State
    bool state = false;
    saction->get_state(state);
    state = !state;
    saction->change_state(state);

    return state;
}

void
canvas_commands_bar_toggle(InkscapeWindow *win)
{
    // Toggle State
    canvas_toggle_state(win, "canvas-commands-bar");

    // Do Action
    SPDesktop* dt = win->get_desktop();
    dt->toggleToolbar("commands");
}

void
canvas_snap_controls_bar_toggle(InkscapeWindow *win)
{
    // Toggle State
    canvas_toggle_state(win, "canvas-snap-controls-bar");

    // Do Action
    SPDesktop* dt = win->get_desktop();
    dt->toggleToolbar("snaptoolbox");
}

void
canvas_tool_control_bar_toggle(InkscapeWindow *win)
{
    // Toggle State
    canvas_toggle_state(win, "canvas-tool-control-bar");

    // Do Action
    SPDesktop* dt = win->get_desktop();
    dt->toggleToolbar("toppanel");
}

void
canvas_toolbox_toggle(InkscapeWindow *win)
{
    // Toggle State
    canvas_toggle_state(win, "canvas-toolbox");

    // Do Action
    SPDesktop* dt = win->get_desktop();
    dt->toggleToolbar("toolbox");
}

void
canvas_rulers_toggle(InkscapeWindow *win)
{
    // Toggle State
    canvas_toggle_state(win, "canvas-rulers");

    // Do Action
    SPDesktop* dt = win->get_desktop();
    dt->toggleToolbar("rulers");
}

void
canvas_scroll_bars(InkscapeWindow *win)
{
    // Toggle State
    canvas_toggle_state(win, "canvas-scroll-bars");

    // Do Action
    SPDesktop* dt = win->get_desktop();
    dt->toggleToolbar("scrollbars");
}

void
canvas_palette_toggle(InkscapeWindow *win)
{
    // Toggle State
    canvas_toggle_state(win, "canvas-palette");

    // Do Action
    SPDesktop* dt = win->get_desktop();
    dt->toggleToolbar("panels");
}

void
canvas_statusbar_toggle(InkscapeWindow *win)
{
    // Toggle State
    canvas_toggle_state(win, "canvas-statusbar");

    // Do Action
    SPDesktop* dt = win->get_desktop();
    dt->toggleToolbar("statusbar");
}

void
canvas_interface_mode(InkscapeWindow *win)
{
    // Toggle State
    bool state = canvas_toggle_state(win, "canvas-interface-mode");

    // Save to preferences
    auto prefs = Inkscape::Preferences::get();
    Glib::ustring pref_root = "/window/";
    auto desktop = win->get_desktop();
    if (desktop && desktop->is_focusMode()) {
        pref_root = "/focus/";
    } else if (desktop && desktop->is_fullscreen()) {
        pref_root = "/fullscreen/";
    }
    prefs->setBool(pref_root + "interface_mode", state);

    // Update Interface
    auto desktop_widget = win->get_desktop_widget();
    desktop_widget->layoutWidgets();
}

void
view_fullscreen(InkscapeWindow *win)
{
    SPDesktop* dt = win->get_desktop();
    dt->fullscreen();
}

void
view_full_screen_focus(InkscapeWindow *win)
{
    SPDesktop* dt = win->get_desktop();    
    dt->fullscreen();
    dt->focusMode(!dt->is_fullscreen());
}

void
view_focus_toggle(InkscapeWindow *win)
{
    SPDesktop* dt = win->get_desktop();
    dt->focusMode(!dt->is_focusMode());
}

void
canvas_command_palette(InkscapeWindow *win)
{
    SPDesktop* dt = win->get_desktop();
    dt->toggleCommandPalette();
}

/*
 * Sets state of actions (and thus toggle buttons).
 * Needed when changing window mode (fullscreen, normal, focus).
 */
void
view_set_gui(InkscapeWindow* win)
{
    auto prefs = Inkscape::Preferences::get();
    SPDesktop* desktop = win->get_desktop();

    if (!desktop) {
        show_output("canvas_set_gui: no desktop!");
        return;
    }

    Glib::ustring pref_root = "/window/";
    if (desktop && desktop->is_focusMode()) {
        pref_root = "/focus/";
    } else if (desktop && desktop->is_fullscreen()) {
        pref_root = "/fullscreen/";
    }

    // clang-format off

    // Current States of Actions
    bool commands_state    = prefs->getBool(pref_root + "commands/state", true);
    bool snaptoolbox_state = prefs->getBool(pref_root + "snaptoolbox/state", true);
    bool toppanel_state    = prefs->getBool(pref_root + "toppanel/state", true);
    bool toolbox_state     = prefs->getBool(pref_root + "toolbox/state", true);
    bool palette_state     = prefs->getBool(pref_root + "panels/state", true);
    bool statusbar_state   = prefs->getBool(pref_root + "statusbar/state", true);
    bool scrollbars_state  = prefs->getBool(pref_root + "scrollbars/state", true);
    bool rulers_state      = prefs->getBool(pref_root + "rulers/state", true);

    canvas_set_state(win, "canvas-commands-bar",      commands_state);
    canvas_set_state(win, "canvas-snap-controls-bar", snaptoolbox_state);
    canvas_set_state(win, "canvas-tool-control-bar",  toppanel_state);
    canvas_set_state(win, "canvas-toolbox",           toolbox_state);
    canvas_set_state(win, "canvas-rulers",            rulers_state);
    canvas_set_state(win, "canvas-scroll-bars",       scrollbars_state);
    canvas_set_state(win, "canvas-palette",           palette_state);
    canvas_set_state(win, "canvas-statusbar",         statusbar_state);
    // clang-format on
}

std::vector<std::vector<Glib::ustring>> raw_data_view_mode =
{
    // clang-format off
    {"win.canvas-commands-bar",             N_("Commands Bar"),             "Canvas Display",   N_("Show or hide the Commands bar (under the menu)")},
    {"win.canvas-snap-controls-bar",        N_("Snap Controls Bar"),        "Canvas Display",   N_("Show or hide the snapping controls")},
    {"win.canvas-tool-control-bar",         N_("Tool Controls Bar"),        "Canvas Display",   N_("Show or hide the Tool Controls bar")},
    {"win.canvas-toolbox",                  N_("Toolbox"),                  "Canvas Display",   N_("Show or hide the main toolbox (on the left)")},
    {"win.canvas-rulers",                   N_("Rulers"),                   "Canvas Display",   N_("Show or hide the canvas rulers")},
    {"win.canvas-scroll-bars",              N_("Scroll bars"),              "Canvas Display",   N_("Show or hide the canvas scrollbars")},
    {"win.canvas-palette",                  N_("Palette"),                  "Canvas Display",   N_("Show or hide the color palette")},
    {"win.canvas-statusbar",                N_("Statusbar"),                "Canvas Display",   N_("Show or hide the statusbar (at the bottom of the window)")},

    {"win.canvas-command-palette",          N_("Command Palette"),          "Canvas Display",   N_("Show or hide the on-canvas command palette")},
    {"win.view-fullscreen",                 N_("Fullscreen"),               "Canvas Display",   N_("Stretch this document window to full screen")},
    
    {"win.view-full-screen-focus",          N_("Fullscreen & Focus Mode"),  "Canvas Display",   N_("Stretch this document window to full screen")},
    {"win.view-focus-toggle",               N_("Focus Mode"),               "Canvas Display",   N_("Remove excess toolbars to focus on drawing")},

    {"win.canvas-interface-mode",           N_("Interface Mode"),           "Canvas Display",   N_("Toggle wide or narrow screen setup")},
    // clang-format on
};

void
add_actions_view_mode(InkscapeWindow* win)
{
    auto prefs = Inkscape::Preferences::get();
    SPDesktop* desktop = win->get_desktop();

    if (!desktop) {
        show_output("add_actions_view_mode: no desktop!");
    }

    Glib::ustring pref_root = "/window/";
    if (desktop && desktop->is_focusMode()) {
        pref_root = "/focus/";
    } else if (desktop && desktop->is_fullscreen()) {
        pref_root = "/fullscreen/";
    }

    // Initial States of Actions

    // If interface_mode unset, use screen aspect ratio.
    Gdk::Rectangle monitor_geometry = Inkscape::UI::get_monitor_geometry_primary();
    double const width  = monitor_geometry.get_width();
    double const height = monitor_geometry.get_height();
    bool widescreen = (height > 0 && width/height > 1.65);

    // clang-format off
    bool commands_toggle    = prefs->getBool(pref_root + "commands/state", true);
    bool snaptoolbox_toggle = prefs->getBool(pref_root + "snaptoolbox/state", true);
    bool toppanel_toggle    = prefs->getBool(pref_root + "toppanel/state", true);
    bool toolbox_toggle     = prefs->getBool(pref_root + "toolbox/state", true);
    bool rulers_toggle      = prefs->getBool(pref_root + "rulers/state", true);
    bool scrollbars_toggle  = prefs->getBool(pref_root + "scrollbars/state", true);
    bool palette_toggle     = prefs->getBool(pref_root + "panels/state", true);
    bool statusbar_toggle   = prefs->getBool(pref_root + "statusbar/state", true);

    bool interface_mode     = prefs->getBool(pref_root + "interface_mode", widescreen);

    win->add_action_bool(          "canvas-commands-bar",           sigc::bind(sigc::ptr_fun(&canvas_commands_bar_toggle),         win), commands_toggle);
    win->add_action_bool(          "canvas-snap-controls-bar",      sigc::bind(sigc::ptr_fun(&canvas_snap_controls_bar_toggle),    win), snaptoolbox_toggle);
    win->add_action_bool(          "canvas-tool-control-bar",       sigc::bind(sigc::ptr_fun(&canvas_tool_control_bar_toggle),     win), toppanel_toggle);
    win->add_action_bool(          "canvas-toolbox",                sigc::bind(sigc::ptr_fun(&canvas_toolbox_toggle),              win), toolbox_toggle);
    win->add_action_bool(          "canvas-rulers",                 sigc::bind(sigc::ptr_fun(&canvas_rulers_toggle),               win), rulers_toggle);
    win->add_action_bool(          "canvas-scroll-bars",            sigc::bind(sigc::ptr_fun(&canvas_scroll_bars),                 win), scrollbars_toggle);
    win->add_action_bool(          "canvas-palette",                sigc::bind(sigc::ptr_fun(&canvas_palette_toggle),              win), palette_toggle);
    win->add_action_bool(          "canvas-statusbar",              sigc::bind(sigc::ptr_fun(&canvas_statusbar_toggle),            win), statusbar_toggle);

    win->add_action_bool (         "canvas-interface-mode",         sigc::bind(sigc::ptr_fun(&canvas_interface_mode),              win), interface_mode);
    win->add_action(               "view-fullscreen",               sigc::bind(sigc::ptr_fun(&view_fullscreen),                    win));
    
    win->add_action(               "view-full-screen-focus",        sigc::bind(sigc::ptr_fun(&view_full_screen_focus),             win));
    win->add_action(               "view-focus-toggle",             sigc::bind(sigc::ptr_fun(&view_focus_toggle),                  win));

    win->add_action(               "canvas-command-palette",        sigc::bind(sigc::ptr_fun(&canvas_command_palette),             win));
    // clang-format on

    auto app = InkscapeApplication::instance();
    if (!app) {
        show_output("add_actions_view_mode: no app!");
        return;
    }
    app->get_action_extra_data().add_data(raw_data_view_mode);
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
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:
