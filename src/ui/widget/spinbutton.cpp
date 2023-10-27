// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author:
 *   Johan B. C. Engelen
 *
 * Copyright (C) 2011 Author
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "spinbutton.h"

#include <cmath>
#include <gtkmm/enums.h>
#include <gtkmm/object.h>
#include <gtkmm/popovermenu.h>
#include <gtkmm/radiobutton.h>
#include <memory>
#include <sigc++/functors/mem_fun.h>

#include "scroll-utils.h"
#include "ui/controller.h"
#include "ui/menuize.h"
#include "ui/tools/tool-base.h"
#include "ui/widget/popover-menu-item.h"
#include "ui/widget/popover-menu.h"
#include "unit-menu.h"
#include "unit-tracker.h"
#include "util/expression-evaluator.h"

namespace Inkscape::UI::Widget {

MathSpinButton::MathSpinButton(BaseObjectType *cobject, const Glib::RefPtr<Gtk::Builder> &refGlade)
    : Gtk::SpinButton(cobject)
{
    signal_input().connect(sigc::mem_fun(*this, &MathSpinButton::on_input), true);
}

int MathSpinButton::on_input(double &newvalue)
{
    try {
        auto eval = Inkscape::Util::ExpressionEvaluator(get_text().c_str(), nullptr);
        auto result = eval.evaluate();
        newvalue = result.value;
    } catch (Inkscape::Util::EvaluatorException const &e) {
        g_message ("%s", e.what());
        return false;
    }
    return true;
}

void SpinButton::construct()
{
    Controller::add_key<&SpinButton::on_key_pressed>(*this, *this);

    property_has_focus().signal_changed().connect(
        sigc::mem_fun(*this, &SpinButton::on_has_focus_changed));
    UI::on_popup_menu(*this, sigc::mem_fun(*this, &SpinButton::on_popup_menu));

    signal_input().connect(sigc::mem_fun(*this, &SpinButton::on_input), true);
}

int SpinButton::on_input(double &newvalue)
{
    if (_dont_evaluate) return false;

    try {
        Inkscape::Util::EvaluatorQuantity result;
        if (_unit_menu || _unit_tracker) {
            Unit const *unit = nullptr;
            if (_unit_menu) {
                unit = _unit_menu->getUnit();
            } else {
                unit = _unit_tracker->getActiveUnit();
            }
            Inkscape::Util::ExpressionEvaluator eval = Inkscape::Util::ExpressionEvaluator(get_text().c_str(), unit);
            result = eval.evaluate();
            // check if output dimension corresponds to input unit
            if (result.dimension != (unit->isAbsolute() ? 1 : 0) ) {
                throw Inkscape::Util::EvaluatorException("Input dimensions do not match with parameter dimensions.","");
            }
        } else {
            Inkscape::Util::ExpressionEvaluator eval = Inkscape::Util::ExpressionEvaluator(get_text().c_str(), nullptr);
            result = eval.evaluate();
        }
        newvalue = result.value;
    } catch (Inkscape::Util::EvaluatorException const &e) {
        g_message ("%s", e.what());
        return false;
    }

    return true;
}

void SpinButton::on_has_focus_changed()
{
    if (has_focus()) {
        _on_focus_in_value = get_value();
    }
}

bool SpinButton::on_key_pressed(GtkEventControllerKey const * const controller,
                                unsigned const keyval, unsigned const keycode,
                                GdkModifierType const state)
{
    switch (Inkscape::UI::Tools::get_latin_keyval(controller, keyval, keycode, state)) {
        case GDK_KEY_Escape: // defocus
            undo();
            defocus();
            break;

        case GDK_KEY_Return: // defocus
        case GDK_KEY_KP_Enter:
            defocus();
            break;

        case GDK_KEY_Tab:
        case GDK_KEY_ISO_Left_Tab:
            // set the flag meaning "do not leave toolbar when changing value"
            _stay = true;
            break;

        case GDK_KEY_z:
        case GDK_KEY_Z:
            if (Controller::has_flag(state, GDK_CONTROL_MASK)) {
                _stay = true;
                undo();
                return true; // I consumed the event
            }
            break;

        default:
            break;
    }

    return false;
}

void SpinButton::on_numeric_menu_item_activate(double const value)
{
    auto adj = get_adjustment();
    adj->set_value(value);
}

bool SpinButton::on_popup_menu(PopupMenuOptionalClick)
{
    if (!_custom_popup) {
        return false;
    }
    auto popover_menu = get_popover_menu();
    popover_menu->popup_at_center(*this);
    return true;
}

std::shared_ptr<UI::Widget::PopoverMenu> SpinButton::get_popover_menu()
{
    auto adj = get_adjustment();
    auto adj_value = adj->get_value();
    auto lower = adj->get_lower();
    auto upper = adj->get_upper();
    auto page = adj->get_page_increment();

    auto values = NumericMenuData{};

    for (auto const &custom_data : _custom_menu_data) {
        if (custom_data.first >= lower && custom_data.first <= upper) {
            values.emplace(custom_data);
        }
    }

    values.emplace(adj_value, "");
    values.emplace(std::fmin(adj_value + page, upper), "");
    values.emplace(std::fmax(adj_value - page, lower), "");

    static auto popover_menu = std::make_shared<UI::Widget::PopoverMenu>(*this, Gtk::PositionType::BOTTOM);
    popover_menu->delete_all();
    Gtk::RadioButton::Group group;

    for (auto const &value : values) {
        bool const enable = adj_value == value.first;
        auto const item_label = !value.second.empty() ? Glib::ustring::compose("%1: %2", value.first, value.second)
                                                      : Glib::ustring::format(value.first);
        auto const radio_button = Gtk::make_managed<Gtk::RadioButton>(group, item_label);
        radio_button->set_active(enable);

        auto const item = Gtk::make_managed<UI::Widget::PopoverMenuItem>();
        item->add(*radio_button);
        item->signal_activate().connect(
            sigc::bind(sigc::mem_fun(*this, &SpinButton::on_numeric_menu_item_activate), value.first));
        popover_menu->append(*item);
    }

    return popover_menu;
}

void SpinButton::undo()
{
    set_value(_on_focus_in_value);
}

void SpinButton::defocus()
{
    // defocus spinbutton by moving focus to the canvas, unless "stay" is on
    if (_stay) {
        _stay = false;
    } else {
        Gtk::Widget *widget = _defocus_widget ? _defocus_widget : get_scrollable_ancestor(this);
        if (widget) {
            widget->grab_focus();
        }
    }
}

void SpinButton::set_custom_numeric_menu_data(NumericMenuData &&custom_menu_data)
{
    _custom_popup = true;
    _custom_menu_data = std::move(custom_menu_data);
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
