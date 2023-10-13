// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
    A widget that allows entering a numerical value either by
    clicking/dragging on a custom Gtk::Scale or by using a
    Gtk::SpinButton. The custom Gtk::Scale differs from the stock
    Gtk::Scale in that it includes a label to save space and has a
    "slow dragging" mode triggered by the Alt key.
*/
/*
 * Authors:
 *   Tavmjong Bah <tavmjong@free.fr>
 *
 * Copyright (C) 2017 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <utility>
#include <gdk/gdk.h>
#include <sigc++/functors/mem_fun.h>
#include <gdkmm/general.h>
#include <gdkmm/cursor.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/gesturemultipress.h>
#include <gtkmm/spinbutton.h>

#include "ink-spinscale.h"
#include "ui/controller.h"
#include "ui/pack.h"
#include "ui/util.h"

namespace Controller = Inkscape::UI::Controller;

InkScale::InkScale(Glib::RefPtr<Gtk::Adjustment> adjustment, Gtk::SpinButton* spinbutton)
    : Glib::ObjectBase("InkScale")
    , parent_type(adjustment)
    , _spinbutton(spinbutton)
    , _dragging(false)
    , _drag_start(0)
    , _drag_offset(0)
{
    set_name("InkScale");

    Controller::add_click(*this, sigc::mem_fun(*this, &InkScale::on_click_pressed ),
                                 sigc::mem_fun(*this, &InkScale::on_click_released),
                          Controller::Button::any, Gtk::PropagationPhase::TARGET);
    Controller::add_motion<&InkScale::on_motion_enter ,
                           &InkScale::on_motion_motion,
                           &InkScale::on_motion_leave >
                          (*this, *this, Gtk::PropagationPhase::TARGET);
}

void
InkScale::set_label(Glib::ustring label) {
    _label = std::move(label);
}

bool
InkScale::on_draw(const::Cairo::RefPtr<::Cairo::Context>& cr) {
    parent_type::on_draw(cr);

    if (_label.empty()) {
        return true;
    }

    auto const alloc = get_allocation();

    // Get SpinButton style info...
    auto const text_color = get_foreground_color(_spinbutton->get_style_context());

    // Create Pango layout.
    auto layout_label = create_pango_layout(_label);
    layout_label->set_ellipsize( Pango::EllipsizeMode::END );
    layout_label->set_width(PANGO_SCALE * alloc.get_width());

    // Get y location of SpinButton text (to match vertical position of SpinButton text).
    int x, y;
    _spinbutton->get_layout_offsets(x, y);
    auto btn_alloc = _spinbutton->get_allocation();
    y += btn_alloc.get_y() - alloc.get_y();

    // Fill widget proportional to value.
    double fraction = get_fraction();

    // Get through rectangle and clipping point for text.
    Gdk::Rectangle slider_area = get_range_rect();
    // If we are not sensitive/editable, we render in normal color, no clipping
    auto const clip_text_x = !_spinbutton->is_sensitive() ? 0.0
                             : slider_area.get_x() + slider_area.get_width() * fraction;

    // Render text in normal text color.
    cr->save();
    cr->rectangle(clip_text_x, 0, alloc.get_width() - clip_text_x, alloc.get_height());
    cr->clip();
    Gdk::Cairo::set_source_rgba(cr, text_color);
    cr->move_to(5, y );
    layout_label->show_in_cairo_context(cr);
    cr->restore();

    if (clip_text_x == 0.0) {
        return true;
    }

    // Render text, clipped, in white over bar (TODO: use same color as SpinButton progress bar).
    cr->save();
    cr->rectangle(0, 0, clip_text_x, alloc.get_height());
    cr->clip();
    cr->set_source_rgba(1, 1, 1, 1);
    cr->move_to(5, y);
    layout_label->show_in_cairo_context(cr);
    cr->restore();

    return true;
}

static bool get_constrained(Gdk::ModifierType const state)
{
    return Controller::has_flag(state, Gdk::CONTROL_MASK);
}

Gtk::EventSequenceState InkScale::on_click_pressed(Gtk::GestureClick const &click,
                                                   int /*n_press*/, double const x, double /*y*/)
{
    auto const state = Controller::get_current_event_state(click);
    if (!Controller::has_flag(state, Gdk::ALT_MASK)) {
        auto const constrained = get_constrained(state);
        set_adjustment_value(x, constrained);
    }

    // Dragging must be initialized after any adjustment due to button press.
    _dragging = true;
    _drag_start  = x;
    _drag_offset = get_width() * get_fraction(); 
    return Gtk::EventSequenceState::CLAIMED;
}

Gtk::EventSequenceState InkScale::on_click_released(Gtk::GestureClick const & /*click*/,
                                                    int /*n_press*/, double /*x*/, double /*y*/)
{
    _dragging = false;
    return Gtk::EventSequenceState::CLAIMED;
}

// TODO: GTK4: Just use Widget.set_cursor().
void
InkScale::on_motion_enter(GtkEventControllerMotion const * /*motion*/, double /*x*/, double /*y*/)
{
    auto const display = get_display();
    auto const cursor = Gdk::Cursor::create(display, Gdk::SB_UP_ARROW);
    get_window()->set_cursor(cursor);
}

void
InkScale::on_motion_motion(GtkEventControllerMotion const * const motion, double const x, double /*y*/)
{
    if (!_dragging) {
        return;
    }

    auto const state = Controller::get_device_state(GTK_EVENT_CONTROLLER(motion));
    if (!Controller::has_flag(state, Gdk::ALT_MASK)) {
        // Absolute change
        auto const constrained = get_constrained(state);
        set_adjustment_value(x, constrained);
    } else {
        // Relative change
        double xx = (_drag_offset + (x - _drag_start) * 0.1);
        set_adjustment_value(xx);
    }
}

void
InkScale::on_motion_leave(GtkEventControllerMotion const * /*motion*/)
{
    get_window()->set_cursor({});
}

double
InkScale::get_fraction() {
    Glib::RefPtr<Gtk::Adjustment> adjustment = get_adjustment();
    double upper = adjustment->get_upper();
    double lower = adjustment->get_lower();
    double value = adjustment->get_value();
    double fraction = (value - lower)/(upper - lower);
    return fraction;
}

void
InkScale::set_adjustment_value(double x, bool constrained) {
    Glib::RefPtr<Gtk::Adjustment> adjustment = get_adjustment();
    double upper = adjustment->get_upper();
    double lower = adjustment->get_lower();
    double range = upper-lower;

    Gdk::Rectangle slider_area = get_range_rect();
    double fraction = (x - slider_area.get_x()) / (double)slider_area.get_width();
    double value = fraction * range + lower;
    
    if (constrained) {
        // TODO: do we want preferences for (any of) these?
        if (fmod(range + 1, 16) == 0) {
                value = round(value/16) * 16;
        } else if (range >= 1000 && fmod(upper, 100) == 0) {
                value = round(value/100) * 100;
        } else if (range >= 100 && fmod(upper, 10) == 0) {
                value = round(value/10) * 10;
        } else if (range > 20 && fmod(upper, 5) == 0) {
                value = round(value / 5) * 5;
        } else if (range > 2) {
                value = round(value);
        } else if (range <= 2) {
                value = round(value * 10) / 10;
        }
    }

    adjustment->set_value( value );
}

/*******************************************************************/

InkSpinScale::InkSpinScale(double value, double lower,
                           double upper, double step_increment,
                           double page_increment, double page_size)
    : InkSpinScale{Gtk::Adjustment::create(value,
                                           lower,
                                           upper,
                                           step_increment,
                                           page_increment,
                                           page_size)}
{
    // TODO: Why does the ctor from doubles do this stuff but the other doesnʼt?
    _spinbutton->set_valign(Gtk::Align::CENTER);
    _spinbutton->signal_key_release_event().connect(sigc::mem_fun(*this,&InkSpinScale::on_key_release_event),false);
}

InkSpinScale::InkSpinScale(Glib::RefPtr<Gtk::Adjustment> adjustment)
    : _adjustment(std::move(adjustment))
    , _spinbutton{Gtk::make_managed<Inkscape::UI::Widget::ScrollProtected<Gtk::SpinButton>>(_adjustment)}
    , _scale{Gtk::make_managed<InkScale>(_adjustment, _spinbutton)}
{
    g_assert (_adjustment->get_upper() - _adjustment->get_lower() > 0);

    set_name("InkSpinScale");

    _spinbutton->set_numeric();

    // TODO: GTK4: :draw-value defaults to FALSE, so remove this when migrating.
    _scale->set_draw_value(false);

    Inkscape::UI::pack_end(*this, *_spinbutton, Inkscape::UI::PackOptions::shrink       );
    Inkscape::UI::pack_end(*this, *_scale,      Inkscape::UI::PackOptions::expand_widget);
}

void
InkSpinScale::set_label(Glib::ustring label) {
    _scale->set_label(std::move(label));
}

void
InkSpinScale::set_digits(int digits) {
    _spinbutton->set_digits(digits);
}

int
InkSpinScale::get_digits() const {
    return _spinbutton->get_digits();
}

void
InkSpinScale::set_focus_widget(GtkWidget * focus_widget) {
    _focus_widget = focus_widget;
}

// Return focus to canvas.
bool
InkSpinScale::on_key_release_event(GdkEventKey* key_event) {
    switch (key_event->keyval) {
        case GDK_KEY_Escape:
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if (_focus_widget) {
                gtk_widget_grab_focus( _focus_widget );
            }
            break;
    }

    return false;
}

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
