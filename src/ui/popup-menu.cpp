// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Helpers to connect signals to events that popup a menu in both GTK3 and GTK4.
 * Plus miscellaneous helpers primarily useful with widgets used as popop menus.
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "popup-menu.h"

#include <utility>
#include <2geom/point.h>
#include <gtkmm/accelerator.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/gestureclick.h>
#include <gdkmm/rectangle.h>
#include <gtkmm/popover.h>
#include <gtkmm/widget.h>

#include "controller.h"
#include "ui/util.h"

namespace Inkscape::UI {

static bool on_key_pressed(unsigned const keyval, unsigned /*keycode*/, Gdk::ModifierType state,
                           PopupMenuSlot const &slot)
{
    if (keyval == GDK_KEY_Menu) {
        return slot(std::nullopt);
    }

    state &= Gtk::Accelerator::get_default_mod_mask();
    if (keyval == GDK_KEY_F10 && Controller::has_flag(state, Gdk::ModifierType::SHIFT_MASK)) {
        return slot(std::nullopt);
    }

    return false;
}

static Gtk::EventSequenceState on_click_pressed(Gtk::GestureClick const &click,
                                                int const n_press, double const x, double const y,
                                                PopupMenuSlot const &slot)
{
    auto const event = click.get_current_event();
    g_return_val_if_fail(event, Gtk::EventSequenceState::NONE);

    if (event->triggers_context_menu()) {
        auto const click = PopupMenuClick{n_press, x, y};
        if (slot(click)) return Gtk::EventSequenceState::CLAIMED;
    }

    return Gtk::EventSequenceState::NONE;
}

void on_popup_menu(Gtk::Widget &widget, PopupMenuSlot slot)
{
    auto key = Gtk::EventControllerKey::create();
    key->signal_key_pressed().connect(sigc::bind(&on_key_pressed, slot), true); // after
    widget.add_controller(std::move(key));

    Controller::add_click(widget, sigc::bind(&on_click_pressed, std::move(slot)), {},
                          Controller::Button::any, Gtk::PropagationPhase::TARGET); // ←beat Entry popup handler
}

sigc::connection on_hide_reset(std::shared_ptr<Gtk::Widget> widget)
{
    return widget->signal_hide().connect( [widget = std::move(widget)]() mutable { widget.reset(); });
}

static void popup_at(Gtk::Popover &popover, Gtk::Widget &widget,
                     double const x_offset, double const y_offset,
                     int width, int height)
{
    popover.set_visible(false);

    auto const parent = popover.get_parent();
    g_return_if_fail(parent);
    g_return_if_fail(&widget == parent || is_descendant_of(widget, *parent));

    auto const allocation = widget.get_allocation();
    if (!width ) width  = x_offset ? 1 : allocation.get_width ();
    if (!height) height = y_offset ? 1 : allocation.get_height();
    double x{}, y{};
    widget.translate_coordinates(*parent, 0, 0, x, y);
    x += x_offset;
    y += y_offset;
    auto const ix = static_cast<int>(x + 0.5), iy = static_cast<int>(y + 0.5);
    popover.set_pointing_to({ix, iy, width, height});

    popover.popup();
}

void popup_at(Gtk::Popover &popover, Gtk::Widget &widget,
              double const x_offset, double const y_offset)
{
    popup_at(popover, widget, x_offset, y_offset, 0, 0);
}

void popup_at(Gtk::Popover &popover, Gtk::Widget &widget,
              std::optional<Geom::Point> const &offset)
{
    auto const x_offset = offset ? offset->x() : 0;
    auto const y_offset = offset ? offset->y() : 0;
    popup_at(popover, widget, x_offset, y_offset);
}

void popup_at_center(Gtk::Popover &popover, Gtk::Widget &widget)
{
    auto const x_offset = widget.get_width () / 2;
    auto const y_offset = widget.get_height() / 2;
    popup_at(popover, widget, x_offset, y_offset);
}

void popup_at(Gtk::Popover &popover, Gtk::Widget &widget, Gdk::Rectangle const &rect)
{
    popup_at(popover, widget, rect.get_x(), rect.get_y(), rect.get_width(), rect.get_height());
}

} // namespace Inkscape::UI

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
