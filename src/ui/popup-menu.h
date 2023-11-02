// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Helpers to connect signals to events that popup a menu in both GTK3 and GTK4.
 * Plus miscellaneous helpers primarily useful with widgets used as popop menus.
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_UI_POPUP_MENU_H
#define SEEN_UI_POPUP_MENU_H

#include <memory>
#include <optional>
#include <sigc++/connection.h>
#include <sigc++/slot.h>

namespace Geom {
class Point;
} // namespace Geom

namespace Gdk {
class Rectangle;
} // namespace Gdk

namespace Gtk {
class Popover;
class Widget;
} // namespace Gtk

namespace Inkscape {

namespace UI {

/// Information from a GestureClick if a popup menu was opened by click
struct PopupMenuClick final { int const n_press{}; double const x{}, y{}; };
/// Optional: not present if popup wasnʼt triggered by click.
using PopupMenuOptionalClick = std::optional<PopupMenuClick>;

/// Return whether a popup was activated.
/// Click param is nullopt if popup wasnʼt triggered by a click.
using PopupMenuSlot = sigc::slot<bool (PopupMenuOptionalClick)>;

/// Connect slot to a widgetʼs key and button events that traditionally trigger a popup menu, i.e.:
/// * The keys used by GTK3ʼs signal Widget::popup-menu: the Menu key, or the Shift+F10 combination
/// * The right mouse button or other platform convention, as per gtk_event_triggers_context_menu()
void on_popup_menu(Gtk::Widget &widget, PopupMenuSlot slot);

/// Connects ::hide of widget to reset() the shared_ptr i.e. to ‘self-destruct’.
/// @returns A connection that can be used to disconnect & prevent self-destruct
sigc::connection on_hide_reset(std::shared_ptr<Gtk::Widget> widget);

/// Replace Gtk::Menu::popup_at_pointer. If x or y
/// offsets != 0, :pointing-to is set to {x,y,1,1}
/// else it is set to the full allocation of @a widget, translated into the coordinate space of the
/// @a popoverʼs parent (:relative-to) widget. Hence, the @a widget must be the @a popoverʼs parent
/// (:relative-to) widget or a descendant thereof.
void popup_at(Gtk::Popover &popover, Gtk::Widget &widget,
              double x_offset = 0.0, double y_offset = 0.0);
/// @copydoc popup_at()
void popup_at(Gtk::Popover &popover, Gtk::Widget &widget,
              std::optional<Geom::Point> const &offset);

/// As popup_at() but point to center of widget
void popup_at_center(Gtk::Popover &popover, Gtk::Widget &widget);

/// As popup_at() but point to center of @a rect
/// @a rect must be valid within the coords of @widget, & @awidget must be a descendant of @parent.
void popup_at(Gtk::Popover &popover, Gtk::Widget &widget, Gdk::Rectangle const &rect);

} // namespace UI

} // namespace Inkscape

#endif // SEEN_UI_POPUP_MENU_H

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
