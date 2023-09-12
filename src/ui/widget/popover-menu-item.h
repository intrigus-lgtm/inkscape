// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A replacement for GTK3ʼs Gtk::MenuItem, as removed in GTK4.
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_UI_WIDGET_POPOVER_MENU_ITEM_H
#define SEEN_UI_WIDGET_POPOVER_MENU_ITEM_H

#include <glibmm/ustring.h>
#include <gtk/gtk.h> // GtkEventControllerMotion
#include <gtkmm/button.h>
#include <gtkmm/enums.h> // Gtk::IconSize
#include "ui/widget/css-name-class-init.h"

namespace Inkscape::UI::Widget {

class PopoverMenu;

/// A replacement for GTK3ʼs Gtk::MenuItem, as removed in GTK4.
/// Aim is to be a minimal but mostly “drop-in” replacement.
class PopoverMenuItem
    : public CssNameClassInit
    , public Gtk::Button
{
public:
    // Construct a flat Button with CSS name `menuitem` and class `.menuitem`.
    // If text & icon_name are present, we add a Box containing a Image & Label.
    // If only 1 of text or icon_name are present, we add only a Label or Image.
    // If neither text or icon_name are present, we add no child: you can do so
    [[nodiscard]] explicit PopoverMenuItem(Glib::ustring const &text = {},
                                           bool mnemonic = false,
                                           Glib::ustring const &icon_name = {},
                                           Gtk::IconSize icon_size = Gtk::IconSize::NORMAL,
                                           bool popdown_on_activate = true);

    /// A convenience, “drop-in” alias for signal_clicked().
    [[nodiscard]] Glib::SignalProxy<void ()> signal_activate();

private:
    [[nodiscard]] PopoverMenu *get_menu();

    void on_motion(GtkEventControllerMotion const *motion, double x, double y);
    void on_focus();
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_UI_WIDGET_POPOVER_MENU_ITEM_H

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
