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
#include <gdkmm/rectangle.h>
#include <gtkmm/gesturemultipress.h>
#include <gtkmm/popover.h>
#include <gtkmm/widget.h>

#include "controller.h"
#include "manage.h"
#include "ui/util.h"

namespace Inkscape::UI {

static bool on_key_pressed(GtkEventControllerKey const * /*controller*/,
                           unsigned const keyval, unsigned /*keycode*/, GdkModifierType state,
                           PopupMenuSlot const * const slot)
{
    g_return_val_if_fail(slot != nullptr, false);

    if (keyval == GDK_KEY_Menu) {
        return (*slot)(std::nullopt);
    }

    state = static_cast<GdkModifierType>(state & gtk_accelerator_get_default_mod_mask());
    if (keyval == GDK_KEY_F10 && Controller::has_flag(state, GDK_SHIFT_MASK)) {
        return (*slot)(std::nullopt);
    }

    return false;
}

static Gtk::EventSequenceState on_click_pressed(Gtk::GestureClick const &click,
                                                int const n_press, double const x, double const y,
                                                PopupMenuSlot const * const slot)
{
    g_return_val_if_fail(slot != nullptr, Gtk::EventSequenceState::NONE);

    if (gdk_event_triggers_context_menu(Controller::get_last_event(click))) {
        auto const click = PopupMenuClick{n_press, x, y};
        if ((*slot)(click)) return Gtk::EventSequenceState::CLAIMED;
    }

    return Gtk::EventSequenceState::NONE;
}

sigc::connection on_popup_menu(Gtk::Widget &widget, PopupMenuSlot slot)
{
    auto &managed_slot = manage(std::move(slot), widget);
    auto const key = gtk_event_controller_key_new(widget.Gtk::Widget::gobj());
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_key_pressed), &managed_slot);
    Controller::add_click(widget, sigc::bind(&on_click_pressed, &managed_slot), {},
                          Controller::Button::any, Gtk::PropagationPhase::TARGET); // ←beat Entry popup handler
    return sigc::connection{managed_slot};
}

sigc::connection on_hide_reset(std::shared_ptr<Gtk::Widget> widget)
{
    return widget->signal_hide().connect( [widget = std::move(widget)]() mutable { widget.reset(); });
}

static void popup_at(Gtk::Popover &popover, Gtk::Widget &widget,
                     int const x_offset, int const y_offset,
                     int width, int height)
{
    popover.set_visible(false);

    auto const parent = popover.get_relative_to();
    g_return_if_fail(parent);
    g_return_if_fail(&widget == parent || is_descendant_of(widget, *parent));

    auto const allocation = widget.get_allocation();
    if (!width ) width  = x_offset ? 1 : allocation.get_width ();
    if (!height) height = y_offset ? 1 : allocation.get_height();
    int x{}, y{};
    widget.translate_coordinates(*parent, 0, 0, x, y);
    x += x_offset;
    y += y_offset;
    popover.set_pointing_to({x, y, width, height});

    popover.show_all_children();
    popover.popup();
}

void popup_at(Gtk::Popover &popover, Gtk::Widget &widget,
              int const x_offset, int const y_offset)
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
