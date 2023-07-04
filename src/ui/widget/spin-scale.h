// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * Derived from and replaces SpinSlider
 */
/*
 * Author:
 *
 * Copyright (C) 2007 Nicholas Bishop <nicholasbishop@gmail.com>
 *               2017 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_WIDGET_SPIN_SCALE_H
#define INKSCAPE_UI_WIDGET_SPIN_SCALE_H

#include <glibmm/refptr.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/togglebutton.h>
#include <sigc++/signal.h>

#include "attr-widget.h"
#include "ink-spinscale.h"

namespace Inkscape::UI::Widget {

/**
 * Wrap the InkSpinScale class and attach an attribute.
 * A combo widget with label, scale slider, spinbutton, and adjustment;
 */
class SpinScale : public Gtk::Box, public AttrWidget
{
public:
    SpinScale(Glib::ustring label, double value,
              double lower, double upper,
              double step_increment, double page_increment, int digits,
              SPAttr a = SPAttr::INVALID, Glib::ustring const &tip_text = {});

    // Used by extensions
    SpinScale(Glib::ustring label,
              Glib::RefPtr<Gtk::Adjustment> adjustment, int digits,
              SPAttr a = SPAttr::INVALID, Glib::ustring const &tip_text = {});

    Glib::ustring get_as_attribute() const override;
    void set_from_attribute(SPObject*) override;

    // Shortcuts to _adjustment
    Glib::SignalProxy<void()> signal_value_changed();
    double get_value() const;
    void set_value(const double);
    void set_focuswidget(GtkWidget *widget);
    
private:
    Glib::RefPtr<Gtk::Adjustment> _adjustment;
    InkSpinScale _inkspinscale;

public:
    decltype(_adjustment) const &get_adjustment();
};

/**
 * Contains two SpinScales for controlling number-opt-number attributes.
 *
 * @see SpinScale
 */
class DualSpinScale : public Gtk::Box, public AttrWidget
{
public:
    DualSpinScale(const Glib::ustring label1, const Glib::ustring label2,
                  double value, double lower, double upper,
                  double step_increment, double page_increment, int digits,
                  SPAttr a,
                  Glib::ustring const &tip_text1, Glib::ustring const &tip_text2);

    Glib::ustring get_as_attribute() const override;
    void set_from_attribute(SPObject*) override;

    sigc::signal<void ()>& signal_value_changed();

    const SpinScale& get_SpinScale1() const;
    SpinScale& get_SpinScale1();

    const SpinScale& get_SpinScale2() const;
    SpinScale& get_SpinScale2();

    //void remove_scale();
private:
    void link_toggled();
    void update_linked();
    void set_link_active(bool link);
    sigc::signal<void ()> _signal_value_changed;
    SpinScale _s1, _s2;
    bool _linked = true;
    Gtk::Button _link;
};

} // namespace Inkscape::UI::Widget

#endif // INKSCAPE_UI_WIDGET_SPIN_SCALE_H

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
