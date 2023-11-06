// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
   A widget that allows entering a numerical value either by
   clicking/dragging on a custom Gtk::Scale or by using a
   Gtk::SpinButton. The custom Gtk::Scale differs from the stock
   Gtk::Scale in that it includes a label to save space and has a
   "slow-dragging" mode triggered by the Alt key.
*/
/*
 * Authors:
 *   Tavmjong Bah <tavmjong@free.fr>
 *
 * Copyright (C) 2017 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_INK_SPINSCALE_H
#define SEEN_INK_SPINSCALE_H

#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtk/gtk.h> // GtkEventControllerMotion
#include <gtkmm/box.h>
#include <gtkmm/gesture.h> // Gtk::EventSequenceState
#include <gtkmm/scale.h>

namespace Gtk {
class Adjustment;
class GestureClick;
class Snapshot;
class SpinButton;
} // namespace Gtk

class InkScale final : public Gtk::Scale
{
    using parent_type = Gtk::Scale;

public:
    InkScale(Glib::RefPtr<Gtk::Adjustment>, Gtk::SpinButton* spinbutton);

    void set_label(Glib::ustring label);

private:
    void snapshot_vfunc(Glib::RefPtr<Gtk::Snapshot> const &snapshot) final;

    Gtk::EventSequenceState on_click_pressed (Gtk::GestureClick const &click,
                                              int n_press, double x, double y);
    Gtk::EventSequenceState on_click_released(Gtk::GestureClick const &click,
                                              int n_press, double x, double y);
    void on_motion_enter (GtkEventControllerMotion const *motion, double x, double y);
    void on_motion_motion(GtkEventControllerMotion const *motion, double x, double y);
    void on_motion_leave (GtkEventControllerMotion const *motion);

    double get_fraction();
    void set_adjustment_value(double x, bool constrained = false);

    Gtk::SpinButton * _spinbutton; // Needed to get placement/text color.
    Glib::ustring _label;
    bool   _dragging;
    double _drag_start;
    double _drag_offset;
};

class InkSpinScale final : public Gtk::Box
{
public:
    // Create an InkSpinScale with a new adjustment.
    InkSpinScale(double value,
                 double lower,
                 double upper,
                 double step_increment = 1,
                 double page_increment = 10,
                 double page_size = 0);

    // Create an InkSpinScale with a preexisting adjustment.
    InkSpinScale(Glib::RefPtr<Gtk::Adjustment>);

    void set_label(Glib::ustring label);
    void set_digits(int digits);
    int  get_digits() const;
    void set_focus_widget(GtkWidget *focus_widget);
    Glib::RefPtr<Gtk::Adjustment      > get_adjustment()       { return _adjustment; };
    Glib::RefPtr<Gtk::Adjustment const> get_adjustment() const { return _adjustment; };

private:
    Glib::RefPtr<Gtk::Adjustment>  _adjustment;
    Gtk::SpinButton               *_spinbutton   = nullptr;
    InkScale                      *_scale        = nullptr;
    GtkWidget                     *_focus_widget = nullptr;

    bool on_key_released(GtkEventControllerKey const * controller,
                         unsigned keyval, unsigned keycode, GdkModifierType state);
};

#endif // SEEN_INK_SPINSCALE_H

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
