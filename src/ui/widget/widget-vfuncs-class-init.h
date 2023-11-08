// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A class that can be inherited to access GTK4ʼs Widget.css_changed & .focus vfuncs, not in gtkmm4
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_UI_WIDGET_WIDGET_VFUNCS_CLASS_INIT_H
#define SEEN_UI_WIDGET_WIDGET_VFUNCS_CLASS_INIT_H

#include <optional>
#include <glibmm/extraclassinit.h>
#include <gtk/gtk.h>
#include <gtkmm/enums.h>

namespace Inkscape::UI::Widget {

/// A class you can inherit to access GTK4ʼs Widget.css_changed & .focus vfuncs, missing in gtkmm4.
/// Use this if in gtkmm 3, you were connecting to ::style-updated or overriding on_style_updated()
/// – or overriding on_focus() in a way that needs to change the result from that of the base vfunc
/// See https://gitlab.gnome.org/GNOME/gtkmm/-/issues/147
/// The subclass must also inherit from Gtk::Widget or a subclass thereof.
class WidgetVfuncsClassInit : public Glib::ExtraClassInit {
public:
    /// Holds pointers to C vfunc implementations
    struct CFuncs final {
        using CssChanged = void     (GtkWidget *, GtkCssStyleChange *);
        using Focus      = gboolean (GtkWidget *, GtkDirectionType   );

        CssChanged *css_changed = nullptr;
        Focus      *focus       = nullptr;
    };

protected:
    [[nodiscard]] WidgetVfuncsClassInit();
    ~WidgetVfuncsClassInit() override;

    /// Called after gtk_widget_css_changed(): when a CSS widget node is validated & style changed.
    virtual void css_changed(GtkCssStyleChange *change) {}

    /// Called before gtk_widget_focus(): return true if moving in direction keeps focus w/in self,
    /// false if moving left focus outside widget, or nullopt for no decision & to call base vfunc.
    virtual std::optional<bool> focus(Gtk::DirectionType direction) { return std::nullopt; }

private:
    // The following ‘thunk’ vfunc impls...
    static CFuncs::CssChanged _css_changed;
    static CFuncs::Focus      _focus      ;
    // ...are used by every instance of us.
    static constexpr CFuncs cfuncs = {
        &_css_changed,
        &_focus      ,
    };
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_UI_WIDGET_WIDGET_VFUNCS_CLASS_INIT_H

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
