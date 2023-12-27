// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Bryce Harrington <bryce@bryceharrington.org>
 *   Derek P. Moore <derekm@hackunix.org>
 *   buliabyak@gmail.com
 *
 * Copyright (C) 2004-2005 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "scalar-unit.h"

#include <utility>

#include "spinbutton.h"
#include "ui/pack.h"

namespace Inkscape::UI::Widget {

ScalarUnit::ScalarUnit(Glib::ustring const &label, Glib::ustring const &tooltip,
                       UnitType unit_type,
                       Glib::ustring const &icon,
                       UnitMenu *unit_menu,
                       bool mnemonic)
    : Scalar(label, tooltip, icon, mnemonic),
      _unit_menu(unit_menu),
      _hundred_percent(0),
      _absolute_is_increment(false),
      _percentage_is_increment(false)
{
    if (!_unit_menu) {
        _unit_menu = Gtk::make_managed<UnitMenu>();
        _unit_menu->setUnitType(unit_type);

        auto const widget = getWidget();
        g_assert(widget);
        widget->reference();
        remove(*widget);
        auto const widget_holder = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        UI::pack_start(*widget_holder, *getWidget(), UI::PackOptions::shrink);
        UI::pack_start(*widget_holder, *_unit_menu, UI::PackOptions::shrink);
        UI::pack_start(*this, *widget_holder, UI::PackOptions::shrink);
        widget->unreference();
    }

    g_assert(_unit_menu);

    _unit_menu->signal_changed()
            .connect(sigc::mem_fun(*this, &ScalarUnit::on_unit_changed));

    getSpinButton().setUnitMenu(_unit_menu);

    lastUnits = _unit_menu->getUnitAbbr();
}

ScalarUnit::ScalarUnit(Glib::ustring const &label, Glib::ustring const &tooltip,
                       ScalarUnit &take_unitmenu,
                       Glib::ustring const &icon,
                       bool mnemonic)
    : ScalarUnit{label, tooltip, UnitType{}, icon, take_unitmenu._unit_menu, mnemonic}
{
}

void ScalarUnit::initScalar(double min_value, double max_value)
{
    Scalar::setDigits(_unit_menu->getDefaultDigits());
    Scalar::setIncrements(_unit_menu->getDefaultStep(),
                          _unit_menu->getDefaultPage());
    Scalar::setRange(min_value, max_value);
}

bool ScalarUnit::setUnit(Glib::ustring const &unit)
{
    // First set the unit
    if (!_unit_menu->setUnit(unit)) {
        return false;
    }
    lastUnits = unit;
    return true;
}

void ScalarUnit::setUnitType(UnitType unit_type)
{
    _unit_menu->setUnitType(unit_type);
    lastUnits = _unit_menu->getUnitAbbr();
}

void ScalarUnit::resetUnitType(UnitType unit_type)
{
    _unit_menu->resetUnitType(unit_type);
    lastUnits = _unit_menu->getUnitAbbr();
}

Unit const * ScalarUnit::getUnit() const
{
    return _unit_menu->getUnit();
}

UnitType ScalarUnit::getUnitType() const
{
    return _unit_menu->getUnitType();
}

void ScalarUnit::setValue(double number, Glib::ustring const &units)
{
    _unit_menu->setUnit(units);
    Scalar::setValue(number);
}

void ScalarUnit::setValueKeepUnit(double number, Glib::ustring const &units)
{
    if (units.empty()) {
        // set the value in the default units
        Scalar::setValue(number);
    } else {
        double conversion = _unit_menu->getConversion(units);
        Scalar::setValue(number / conversion);
    }
}

void ScalarUnit::setValue(double number)
{
    Scalar::setValue(number);
}

double ScalarUnit::getValue(Glib::ustring const &unit_name) const
{
    if (unit_name.empty()) {
        // Return the value in the default units
        return Scalar::getValue();
    } else {
        double conversion = _unit_menu->getConversion(unit_name);
        return conversion * Scalar::getValue();
    }
}

void ScalarUnit::grabFocusAndSelectEntry()
{
    auto &spinButton = getSpinButton();
    spinButton.grab_focus();
    spinButton.select_region(0, 20);
}

void ScalarUnit::setAlignment(double xalign)
{
    xalign = std::clamp(xalign,0.0,1.0);
    getSpinButton().set_alignment(xalign);
}

void ScalarUnit::setHundredPercent(double number)
{
    _hundred_percent = number;
}

void ScalarUnit::setAbsoluteIsIncrement(bool value)
{
    _absolute_is_increment = value;
}

void ScalarUnit::setPercentageIsIncrement(bool value)
{
    _percentage_is_increment = value;
}

double ScalarUnit::PercentageToAbsolute(double value)
{
    // convert from percent to absolute
    double convertedVal = 0;
    double hundred_converted = _hundred_percent / _unit_menu->getConversion("px"); // _hundred_percent is in px
    if (_percentage_is_increment) 
        value += 100;
    convertedVal = 0.01 * hundred_converted * value;
    if (_absolute_is_increment) 
        convertedVal -= hundred_converted;

    return convertedVal;
}

double ScalarUnit::AbsoluteToPercentage(double value)
{
    double convertedVal = 0;
    // convert from absolute to percent
    if (_hundred_percent == 0) {
        if (_percentage_is_increment)
            convertedVal = 0;
        else 
            convertedVal = 100;
    } else {
        double hundred_converted = _hundred_percent / _unit_menu->getConversion("px", lastUnits); // _hundred_percent is in px
        if (_absolute_is_increment) 
            value += hundred_converted;
        convertedVal = 100 * value / hundred_converted;
        if (_percentage_is_increment) 
            convertedVal -= 100;
    }

    return convertedVal;
}

double ScalarUnit::getAsPercentage()
{
    double convertedVal = AbsoluteToPercentage(Scalar::getValue());
    return convertedVal;
}

void ScalarUnit::setFromPercentage(double value)
{
    double absolute = PercentageToAbsolute(value);
    Scalar::setValue(absolute);
}

void ScalarUnit::on_unit_changed()
{
    Glib::ustring abbr = _unit_menu->getUnitAbbr();
    auto const &unit_table = Util::UnitTable::get();
    Inkscape::Util::Unit const *new_unit = unit_table.getUnit(abbr);
    Inkscape::Util::Unit const *old_unit = unit_table.getUnit(lastUnits);

    double convertedVal = 0;
    if (old_unit->type == UNIT_TYPE_DIMENSIONLESS && new_unit->type == UNIT_TYPE_LINEAR) {
        convertedVal = PercentageToAbsolute(Scalar::getValue());
    } else if (old_unit->type == UNIT_TYPE_LINEAR && new_unit->type == UNIT_TYPE_DIMENSIONLESS) {
        convertedVal = AbsoluteToPercentage(Scalar::getValue());
    } else {
        double conversion = _unit_menu->getConversion(lastUnits);
        convertedVal = Scalar::getValue() / conversion;
    }
    Scalar::setValue(convertedVal);

    lastUnits = std::move(abbr);
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
