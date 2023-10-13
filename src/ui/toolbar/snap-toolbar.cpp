// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Toolbar for Snapping options.
 */
/* Authors:
 *   Michael Kowalski
 *   Tavmjong Bah
 *
 * Copyright (C) 2023 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "snap-toolbar.h"

#include <iostream>
#include <glibmm/i18n.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/linkbutton.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/scrolledwindow.h>

#include "actions/actions-canvas-snapping.h"  // transition_to_xxx
#include "ui/builder-utils.h"
#include "ui/pack.h"


namespace Inkscape::UI::Toolbar {

SnapToolbar::SnapToolbar()
    : Gtk::Box()
    , builder(UI::create_builder("toolbar-snap.ui"))
    , snap_toolbar     (UI::get_widget<Gtk::Box>           (builder, "snap-toolbar"))
    , btn_simple       (UI::get_widget<Gtk::Button>        (builder, "btn-simple"))
    , btn_advanced     (UI::get_widget<Gtk::Button>        (builder, "btn-advanced"))
    , scroll_permanent (UI::get_widget<Gtk::ScrolledWindow>(builder, "scroll-permanent"))
    , box_permanent    (UI::get_widget<Gtk::Box>           (builder, "box-permanent"))
{
    set_name("SnapToolbar");

    Gtk::LinkButton &link_simple  (UI::get_widget<Gtk::LinkButton>(builder, "link-simple"));
    Gtk::LinkButton &link_advanced(UI::get_widget<Gtk::LinkButton>(builder, "link-advanced"));

    UI::pack_start(*this, snap_toolbar, false, false);

    static constexpr const char* snap_bar_simple_path = "/toolbox/simplesnap";

    // Watch snap bar preferences;
    Inkscape::Preferences* prefs = Inkscape::Preferences::get();
    _observer = prefs->createObserver(snap_bar_simple_path, [=](const Preferences::Entry& entry) {
        mode_update();
    });

    // switch to simple mode
    link_simple.signal_activate_link().connect([=](){
        g_timeout_add(250, &SnapToolbar::show_popover, &btn_simple);
        Inkscape::Preferences::get()->setInt(snap_bar_simple_path, SIMPLE);
        return true;
    }, false);

    // switch to advanced mode
    link_advanced.signal_activate_link().connect([=](){
        g_timeout_add(250, &SnapToolbar::show_popover, &btn_advanced);
        Inkscape::Preferences::get()->setInt(snap_bar_simple_path, ADVANCED);
        return true;
    }, false);

    // SnapToolbar::mode_update will be called at end of Desktop widget setup. Don't call now!
}

// Hide irrelevant buttons according to mode.
// This must be done after the desktop is built.
// Repositioning snap toolbar is handled in DesktopWidget.
void SnapToolbar::mode_update() {
    Inkscape::Preferences* prefs = Inkscape::Preferences::get();
    Mode mode = (Mode)prefs->getInt("/toolbox/simplesnap", 1);

    btn_simple.set_visible(false);
    btn_advanced.set_visible(false);
    scroll_permanent.set_visible(false);

    // Show/hide
    switch (mode) {
        case SIMPLE:
            btn_simple.set_visible(true);
            set_orientation(Gtk::Orientation::HORIZONTAL);
            snap_toolbar.set_orientation(Gtk::Orientation::HORIZONTAL);
            transition_to_simple_snapping();  // Defined in actions-canvas-snapping.cpp
            break;
        case ADVANCED:
            btn_advanced.set_visible(true);
            set_orientation(Gtk::Orientation::HORIZONTAL);
            snap_toolbar.set_orientation(Gtk::Orientation::HORIZONTAL);
            break;
        case PERMANENT:
            scroll_permanent.set_visible(true);
            box_permanent.set_orientation(Gtk::Orientation::VERTICAL);
            snap_toolbar.set_orientation(Gtk::Orientation::VERTICAL);
            break;
        default:
            std::cerr << "SnapToolbar::mode_update: unhandled case!" << std::endl;
    }
}

int SnapToolbar::show_popover(void* button) {
    auto btn = static_cast<Gtk::MenuButton*>(button);
    btn->get_popover()->set_visible(true);
    return false;
}

} // namespace Inkscape::UI::Toolbar

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
