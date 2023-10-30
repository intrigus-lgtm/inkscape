// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A slider with colored background - implementation.
 *//*
 * Authors:
 *   see git history
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_COLOR_SLIDER_H
#define SEEN_COLOR_SLIDER_H

#include <gtk/gtk.h> // GtkEventController*
#include <glibmm/refptr.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/gesture.h> // Gtk::EventSequenceState
#include <sigc++/signal.h>

namespace Gtk {
class Adjustment;
class GestureClick;
} // namespace Gtk

namespace Inkscape::UI::Widget {

/*
 * A slider with colored background
 */
class ColorSlider : public Gtk::DrawingArea {
public:
    ColorSlider(Glib::RefPtr<Gtk::Adjustment> adjustment);
    ~ColorSlider() override;

    void setAdjustment(Glib::RefPtr<Gtk::Adjustment> adjustment);
    void setColors(guint32 start, guint32 mid, guint32 end);
    void setMap(const guchar *map);
    void setBackground(guint dark, guint light, guint size);

    sigc::signal<void ()> signal_grabbed;
    sigc::signal<void ()> signal_dragged;
    sigc::signal<void ()> signal_released;
    sigc::signal<void ()> signal_value_changed;

private:
    void draw_func(Cairo::RefPtr<Cairo::Context> const &cr, int width, int height);

    Gtk::EventSequenceState on_click_pressed (Gtk::GestureClick const &click,
                                              int n_press, double x, double y);
    Gtk::EventSequenceState on_click_released(Gtk::GestureClick const &click,
                                              int n_press, double x, double y);
    void on_motion(GtkEventControllerMotion const *motion, double x, double y);

    void _onAdjustmentChanged();
    void _onAdjustmentValueChanged();

    bool _dragging;

    Glib::RefPtr<Gtk::Adjustment> _adjustment;
    sigc::connection _adjustment_changed_connection;
    sigc::connection _adjustment_value_changed_connection;

    gfloat _value;
    gfloat _oldvalue;
    guchar _c0[4], _cm[4], _c1[4];
    guchar _b0, _b1;
    guchar _bmask;
    guchar *_map;
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_COLOR_SLIDER_H

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
