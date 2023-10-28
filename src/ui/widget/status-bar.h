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

#ifndef INKSCAPE_UI_WIDGET_STATUSBAR_H
#define INKSCAPE_UI_WIDGET_STATUSBAR_H

#include <gtkmm/box.h>

#include "message.h"
#include "preferences.h" // observer
#include "ui/popup-menu.h"

namespace Gtk {
class Grid;
class Label;
class Popover;
} // namespace Gtk

namespace Geom {
class Point;
} // namespace Geom

class SPDesktop;
class SPDesktopWidget;

namespace Inkscape::UI::Widget {

class SelectedStyle;
class LayerSelector;
class SpinButton;

class StatusBar : public Gtk::Box {

public:
    StatusBar();
    ~StatusBar() override = default;

    void set_desktop(SPDesktop* desktop);
    void set_message(const Inkscape::MessageType type, const char* message);
    void set_coordinate(const Geom::Point& p);
    void update_visibility();

    void update_zoom();
    void update_rotate();

    void rotate_grab_focus();
    void zoom_grab_focus();

private:
    int zoom_input(double &new_value);
    bool zoom_output();
    void zoom_value_changed();
    void zoom_menu_handler();
    bool zoom_popup(PopupMenuOptionalClick);

    bool rotate_output();
    void rotate_value_changed();
    void rotate_menu_handler();
    bool rotate_popup(PopupMenuOptionalClick);

    // From left to right
    SelectedStyle* selected_style = nullptr;
    LayerSelector* layer_selector = nullptr;
    Gtk::Label*    selection = nullptr;
    Gtk::Label*    coordinate_x = nullptr;
    Gtk::Label*    coordinate_y = nullptr;
    Gtk::Grid*     coordinates = nullptr;
    Gtk::Box*      zoom = nullptr;
    Gtk::Box*      rotate = nullptr;
    UI::Widget::SpinButton* zoom_value = nullptr;
    UI::Widget::SpinButton* rotate_value = nullptr;

    SPDesktopWidget* desktop_widget = nullptr;
    std::unique_ptr<Gtk::Popover> zoom_popover;
    std::unique_ptr<Gtk::Popover> rotate_popover;

    SPDesktop* desktop = nullptr;

    Inkscape::PrefObserver preference_observer;
};

} // namespace Inkscape::UI::Widget

#endif // INKSCAPE_UI_WIDGET_STATUSBAR_H

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
