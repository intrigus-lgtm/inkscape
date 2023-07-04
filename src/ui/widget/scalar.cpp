// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Carl Hetherington <inkscape@carlh.net>
 *   Derek P. Moore <derekm@hackunix.org>
 *   Bryce Harrington <bryce@bryceharrington.org>
 *   Johan Engelen <j.b.c.engelen@alumnus.utwente.nl>
 *
 * Copyright (C) 2004-2011 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "scalar.h"

#include <cmath>
#include <gtkmm/scale.h>

#include "spinbutton.h"
#include "ui/pack.h"

namespace Inkscape::UI::Widget {

Scalar::Scalar(Glib::ustring const &label, Glib::ustring const &tooltip,
               Glib::ustring const &icon,
               bool mnemonic)
    : Scalar{label, tooltip, {}, 0u, icon, mnemonic}
{
}

Scalar::Scalar(Glib::ustring const &label, Glib::ustring const &tooltip,
               unsigned digits,
               Glib::ustring const &icon,
               bool mnemonic)
    : Scalar{label, tooltip, {}, digits, icon, mnemonic}
{
}

Scalar::Scalar(Glib::ustring const &label, Glib::ustring const &tooltip,
               Glib::RefPtr<Gtk::Adjustment> const &adjust,
               unsigned digits,
               Glib::ustring const &icon,
               bool mnemonic)
    : Labelled(label, tooltip, new SpinButton(adjust, 0.0, digits), icon, mnemonic),
      setProgrammatically(false)
{
}

unsigned Scalar::getDigits() const
{
    return get_spin_button().get_digits();
}

double Scalar::getStep() const
{
    double step, page;
    get_spin_button().get_increments(step, page);
    return step;
}

double Scalar::getPage() const
{
    double step, page;
    get_spin_button().get_increments(step, page);
    return page;
}

double Scalar::getRangeMin() const
{
    double min, max;
    get_spin_button().get_range(min, max);
    return min;
}

double Scalar::getRangeMax() const
{
    double min, max;
    get_spin_button().get_range(min, max);
    return max;
}

double Scalar::getValue() const
{
    return get_spin_button().get_value();
}

int Scalar::getValueAsInt() const
{
    return get_spin_button().get_value_as_int();
}


void Scalar::setDigits(unsigned digits)
{
    return get_spin_button().set_digits(digits);
}

void Scalar::setNoLeadingZeros()
{
    if (getDigits()) {
        auto &spin_button = get_spin_button();
        spin_button.set_numeric(false);
        spin_button.set_update_policy(Gtk::UPDATE_ALWAYS);
        spin_button.signal_output().connect(sigc::mem_fun(*this, &Scalar::setNoLeadingZerosOutput));
    }
}

bool
Scalar::setNoLeadingZerosOutput()
{
    auto &spin_button = get_spin_button();
    double digits = std::pow(10.0, spin_button.get_digits());
    double val = std::round(spin_button.get_value() * digits) / digits;
    spin_button.set_text(Glib::ustring::format(val));
    return true;
}

void 
Scalar::setWidthChars(gint width_chars) {
    get_spin_button().property_width_chars() = width_chars;
}

void Scalar::setIncrements(double step, double /*page*/)
{
    get_spin_button().set_increments(step, 0);
}

void Scalar::setRange(double min, double max)
{
    get_spin_button().set_range(min, max);
}

void Scalar::setValue(double value, bool setProg)
{
    if (setProg) {
        setProgrammatically = true; // callback is supposed to reset back, if it cares
    }
    get_spin_button().set_value(value);
    setProgrammatically = false;
}

void Scalar::setWidthChars(unsigned chars)
{
    get_spin_button().set_width_chars(chars);
}

void Scalar::update()
{
    get_spin_button().update();
}

void Scalar::addSlider()
{
    auto const scale = Gtk::make_managed<Gtk::Scale>(get_spin_button().get_adjustment());
    scale->set_draw_value(false);
    UI::pack_start(*this, *scale);
}

Glib::SignalProxy<void()> Scalar::signal_value_changed()
{
    return get_spin_button().signal_value_changed();
}

void Scalar::hide_label() {
    if (auto const label = getLabel()) {
        label->set_visible(false);
        label->set_no_show_all();
        label->set_hexpand(true);
    }

    if (auto const widget = getWidget()) {
        remove(*widget);
        widget->set_hexpand();
        UI::pack_end(*this, *widget);
    }
}

SpinButton const &Scalar::get_spin_button() const
{
    auto const spinButton = dynamic_cast<SpinButton const *>(getWidget());
    g_assert(spinButton);
    return *spinButton;
}

SpinButton &Scalar::get_spin_button()
{
    return const_cast<SpinButton &>(const_cast<Scalar const &>(*this).get_spin_button());
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
