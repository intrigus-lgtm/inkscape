// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A class that can be inherited to access the GTK4 Widget.css_changed vfunc, not wrapped in gtkmm4
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_UI_WIDGET_CSS_CHANGED_CLASS_INIT_H
#define SEEN_UI_WIDGET_CSS_CHANGED_CLASS_INIT_H

#include <concepts>
#include <typeinfo>
#include <glibmm/extraclassinit.h>
#include <gtk/gtk.h>

namespace Inkscape::UI::Widget {

/// A class that can be inherited to access the GTK4 Widget.css_changed vfunc not wrapped in gtkmm4
/// Use this if in gtkmm 3, you were connecting to ::style-updated or overriding on_style_updated()
/// See https://gitlab.gnome.org/GNOME/gtkmm/-/issues/147
/// The subclass must also inherit from Gtk::Widget or a subclass thereof.
class CssChangedClassInit : public Glib::ExtraClassInit {
protected:
    [[nodiscard]] CssChangedClassInit();
    ~CssChangedClassInit();

    /// Called after gtk_widget_css_changed(): when a CSS widget node is validated & style changed.
    virtual void css_changed(GtkCssStyleChange *change) = 0;

private:
    static void _css_changed(GtkWidget *widget, GtkCssStyleChange *change);
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_UI_WIDGET_CSS_CHANGED_CLASS_INIT_H

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
