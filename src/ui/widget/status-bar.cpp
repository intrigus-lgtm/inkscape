// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author:
 *   Tavmjong Bah
 *   Others
 *
 * Copyright (C) 2023 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "status-bar.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include <glibmm/i18n.h>
#include <gtkmm/applicationwindow.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/popover.h>
#include <2geom/point.h>
#include <2geom/angle.h>  // deg_from_rad

#include "desktop.h"
#include "ui/builder-utils.h"
#include "ui/menuize.h"
#include "ui/pack.h"
#include "ui/util.h"
#include "ui/widget/canvas.h"
#include "ui/widget/desktop-widget.h"
#include "ui/widget/layer-selector.h"
#include "ui/widget/page-selector.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/selected-style.h"

namespace Inkscape::UI::Widget {

StatusBar::StatusBar()
    : Gtk::Box(Gtk::Orientation::HORIZONTAL)
{
    auto builder = Inkscape::UI::create_builder("statusbar.ui");

    auto &statusbar = UI::get_widget<Gtk::Box>(builder, "statusbar");

    // selected_style = &UI::get_widget_derived<UI::Widget::SpinButton>(builder, "statusbar-selected-style");
    // layer_selector = &UI::get_widget_derived<UI::Widget::SpinButton>(builder, "statusbar-layer-selector");
    selection = &UI::get_widget<Gtk::Label>(builder, "statusbar-selection");

    // **** Coordinates  ****

    coordinates  = &UI::get_widget<Gtk::Grid> (builder, "statusbar-coordinates");
    coordinate_x = &UI::get_widget<Gtk::Label>(builder, "statusbar-coordinate-x");
    coordinate_y = &UI::get_widget<Gtk::Label>(builder, "statusbar-coordinate-y");

    // ******** Zoom ********

    zoom = &UI::get_widget<Gtk::Box>(builder, "statusbar-zoom");
    zoom_value = &UI::get_derived_widget<UI::Widget::SpinButton>(builder, "statusbar-zoom-value");

    // Can't seem to add actions with double parameters to .ui file, add here.
    const std::vector<std::pair<std::string, std::string>> zoom_entries =
    {
        { _(  "10%"), "win.canvas-zoom-absolute(0.1)" },
        { _(  "20%"), "win.canvas-zoom-absolute(0.2)" },
        { _(  "50%"), "win.canvas-zoom-absolute(0.5)" },
        { _( "100%"), "win.canvas-zoom-absolute(1.0)" }, // Must include decimal point!
        { _( "200%"), "win.canvas-zoom-absolute(2.0)" },
        { _( "500%"), "win.canvas-zoom-absolute(5.0)" },
        { _("1000%"), "win.canvas-zoom-absolute(10.0)"},
    };

    auto zoom_menu = UI::get_object<Gio::Menu>(builder, "statusbar-zoom-menu");
    for (auto entry : zoom_entries) {
        auto menu_item = Gio::MenuItem::create(entry.first, entry.second);
        zoom_menu->prepend_item(menu_item); // In reverse order.
    }

    zoom_popover = make_menuized_popover(zoom_menu, *zoom);

    zoom_value->signal_input().connect(sigc::mem_fun(*this, &StatusBar::zoom_input), true);
    zoom_value->signal_output().connect(sigc::mem_fun(*this, &StatusBar::zoom_output), true);
    zoom_value->signal_value_changed().connect(sigc::mem_fun(*this, &StatusBar::zoom_value_changed));
    on_popup_menu(*zoom_value, sigc::mem_fun(*this, &StatusBar::zoom_popup));

    auto zoom_adjustment = zoom_value->get_adjustment();
    zoom_adjustment->set_lower(log(SP_DESKTOP_ZOOM_MIN)/log(2));
    zoom_adjustment->set_upper(log(SP_DESKTOP_ZOOM_MAX)/log(2));

    // ******* Rotate *******

    rotate = &UI::get_widget<Gtk::Box>(builder, "statusbar-rotate");
    rotate_value = &UI::get_derived_widget<UI::Widget::SpinButton>(builder, "statusbar-rotate-value");
    rotate_value->set_dont_evaluate(true); // ExpressionEvaluator has trouble parsing degree symbol.

    // Can't seem to add actions with double parameters to .ui file, add here.
    const std::vector<std::pair<std::string, std::string>> rotate_entries =
    {
        {  _( "180°"), "win.canvas-rotate-absolute-degrees( 180.0)" }, // Must include decimal point!
        {  _( "135°"), "win.canvas-rotate-absolute-degrees( 135.0)" },
        {  _(  "90°"), "win.canvas-rotate-absolute-degrees(  90.0)" },
        {  _(  "45°"), "win.canvas-rotate-absolute-degrees(  45.0)" },
        {  _(   "0°"), "win.canvas-rotate-absolute-degrees(   0.0)" },
        {  _( "-45°"), "win.canvas-rotate-absolute-degrees( -45.0)" },
        {  _( "-90°"), "win.canvas-rotate-absolute-degrees( -90.0)" },
        {  _("-135°"), "win.canvas-rotate-absolute-degrees(-135.0)" },
    };

    auto rotate_menu = UI::get_object<Gio::Menu>(builder, "statusbar-rotate-menu");
    for (auto entry : rotate_entries) {
        auto menu_item = Gio::MenuItem::create(entry.first, entry.second);
        rotate_menu->prepend_item(menu_item); // In reverse order.
    }

    rotate_popover = make_menuized_popover(rotate_menu, *rotate);

    rotate_value->signal_output().connect(sigc::mem_fun(*this, &StatusBar::rotate_output), true);
    rotate_value->signal_value_changed().connect(sigc::mem_fun(*this, &StatusBar::rotate_value_changed));
    on_popup_menu(*rotate_value, sigc::mem_fun(*this, &StatusBar::rotate_popup));

    // Add rest by hand for now.

    // Selected Style
    selected_style = Gtk::make_managed<Inkscape::UI::Widget::SelectedStyle>();
    UI::pack_start(statusbar, *selected_style, false, false);
    statusbar.reorder_child_at_start(*selected_style);

    // Layer Selector
    layer_selector = Gtk::make_managed<Inkscape::UI::Widget::LayerSelector>(nullptr);
    UI::pack_start(statusbar, *layer_selector, false, false, 1);  // Expand Fill Padding
    statusbar.reorder_child_after(*layer_selector, *selected_style);

    // Selector status
    UI::pack_start(*this, statusbar);

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    preference_observer = prefs->createObserver("/statusbar/visibility", [this]{
        update_visibility();
    });
    update_visibility();
}

void
StatusBar::set_desktop(SPDesktop* desktop_in)
{
    if (!desktop_in) {
        std::cerr << "StatusBar::set_desktop: desktop is nullptr!" << std::endl;
        return;
    }

    desktop = desktop_in;

    selected_style->setDesktop(desktop);
    layer_selector->setDesktop(desktop);

    // A desktop is always "owned" by a desktop widget.
    desktop_widget = desktop->getDesktopWidget();
    zoom_value->set_defocus_widget(desktop_widget->get_canvas());
    rotate_value->set_defocus_widget(desktop_widget->get_canvas());

    // We add page widget here as it requires desktop for constructor.
    auto &box = dynamic_cast<Gtk::Box &>(*UI::get_children(*this).at(0));
    auto const page_selector = Gtk::make_managed<PageSelector>(desktop);
    UI::pack_start(box, *page_selector, false, false);
    box.reorder_child_after(*page_selector, *UI::get_children(box).at(4));
}

void
StatusBar::set_message(const Inkscape::MessageType type, const char* message)
{
#if PANGO_VERSION_CHECK(1,50,0)
    auto const msg = Glib::ustring::compose("<span line_height='0.9'>%1</span>", message ? message : "");
    selection->set_markup(msg.c_str());
#else
     selection->set_markup(message ? message : "");
#endif

    // Display important messages immediately!
    if (type == Inkscape::IMMEDIATE_MESSAGE && selection->is_drawable()) {
        selection->queue_draw();
    }

    // Allow user to view the entire message even if it doesn't fit into label (after removing markup).
    selection->set_tooltip_text(selection->get_text());
}

void
StatusBar::set_coordinate(const Geom::Point& p)
{
    char * const str_x = g_strdup_printf("%7.2f", p[Geom::X]);
    coordinate_x->set_markup(str_x);
    g_free(str_x);

    char * const str_y = g_strdup_printf("%7.2f", p[Geom::Y]);
    coordinate_y->set_markup(str_y);
    g_free(str_y);
}

void
StatusBar::rotate_grab_focus()
{
    rotate_value->grab_focus();
}

void
StatusBar::zoom_grab_focus()
{
    zoom_value->grab_focus();
}

// ******** Zoom ********

int
StatusBar::zoom_input(double &new_value)
{
    double value = g_strtod(zoom_value->get_text().c_str(), nullptr);
    new_value = log(value / 100.0) / log(2);
    return true;
}

bool
StatusBar::zoom_output()
{
    double value = floor (10 * (pow (2, zoom_value->get_value()) * 100.0 + 0.05)) / 10;

    char b[64];
    if (value < 10) {
        g_snprintf(b, 64, "%4.1f%%", value);
    } else {
        g_snprintf(b, 64, "%4.0f%%", value);
    }
    zoom_value->set_text(b);
       
    return true;
}

void
StatusBar::zoom_value_changed()
{
    double const zoom_factor = pow (2, zoom_value->get_value());

    if (auto const window = dynamic_cast<Gtk::ApplicationWindow *>(get_root())) {
        auto variant = Glib::Variant<double>::create(zoom_factor);
        window->activate_action("win.canvas-zoom-absolute", variant);
    } else {
        std::cerr << "StatusBar::zoom_value_changed(): failed to get window!" << std::endl;
    }
}

bool
StatusBar::zoom_popup(PopupMenuOptionalClick)
{
    popup_at_center(*zoom_popover, *zoom);
    return true;
}

void
StatusBar::update_zoom()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    double correction = 1.0;
    if (prefs->getDouble("/options/zoomcorrection/shown", true)) {
        correction = prefs->getDouble("/options/zoomcorrection/value", 1.0);
    }

    zoom_value->set_value(log(desktop->current_zoom() / correction) / log(2));
    zoom_value->queue_draw();

}

// ******* Rotate *******

bool
StatusBar::rotate_output()
{
    auto val = rotate_value->get_value();
    if (val < -180) val += 360;
    if (val >  180) val -= 360;

    char b[64];
    g_snprintf(b, 64, "%7.2f°", val);
    rotate_value->set_text(b);
       
    return true;
}

void
StatusBar::rotate_value_changed()
{
    if (auto const window = dynamic_cast<Gtk::ApplicationWindow *>(get_root())) {
        auto variant = Glib::Variant<double>::create(rotate_value->get_value());
        window->activate_action("win.canvas-rotate-absolute-degrees", variant);
    } else {
        std::cerr << "StatusBar::rotate_value_changed(): failed to get window!" << std::endl;
    }
}

bool
StatusBar::rotate_popup(PopupMenuOptionalClick)
{
    popup_at_center(*rotate_popover, *rotate);
    return true;
}

void
StatusBar::update_rotate()
{
    rotate_value->set_value(Geom::deg_from_rad(desktop->current_rotation()));
    rotate_value->queue_draw();
}

void
StatusBar::update_visibility()
{
    auto prefs = Inkscape::Preferences::get();
    Glib::ustring path("/statusbar/visibility/");

    layer_selector->set_visible(prefs->getBool(path + "layer",       true));
    selected_style->set_visible(prefs->getBool(path + "style",       true));
    coordinates->set_visible(   prefs->getBool(path + "coordinates", true));
    rotate->set_visible(        prefs->getBool(path + "rotation",    true));
}

} // namespace Inkscape::UI::Widget

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
