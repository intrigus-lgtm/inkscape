// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Helper functions to make children in GtkPopovers act like GtkMenuItem of GTK3
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "menuize.h"

#include <optional>
#include <utility>
#include <glibmm/main.h>
#include <giomm/menumodel.h>
#include <gdkmm/display.h>
#include <gdkmm/seat.h>
#include <gtkmm/eventcontroller.h>
#include <gtkmm/popovermenu.h>
#include <gtkmm/widget.h>
#include <gtkmm/window.h>

#include "ui/util.h"

namespace Inkscape::UI {

// Now that our PopoverMenu is scrollable, we want to distinguish between the pointer really moving
// into or within a menu item, versus the pointer staying still but the item being moved beneath it
// …so while I would welcome a nicer way to do that, this is the solution Iʼve come up with for now
// Without GTK supplying absolute coordinates or a ‘synthesised event’ flag I canʼt see another way
[[nodiscard]] static bool pointer_has_moved(Gtk::Widget const &widget)
{
    static std::optional<double> old_x, old_y;
    auto &window = dynamic_cast<Gtk::Window const &>(*widget.get_root());
    auto const surface = window.get_surface();
    auto const device = surface->get_display()->get_default_seat()->get_pointer();
    double new_x{}, new_y{}; Gdk::ModifierType state{};
    surface->get_device_position(device, new_x, new_y, state);
    return std::exchange(old_x, new_x) != new_x | std::exchange(old_y, new_y) != new_y; // NOT `||`
}

static Gtk::Widget &get_widget(GtkEventControllerMotion * const motion)
{
    auto const widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(motion));
    g_assert(widget != nullptr);
    return *Glib::wrap(widget);
}

static void unset_state(Gtk::Widget &widget)
{
    widget.unset_state_flags(Gtk::StateFlags::FOCUSED | Gtk::StateFlags::PRELIGHT);
}

static void on_motion_grab_focus(GtkEventControllerMotion * const motion, double /*x*/, double /*y*/,
                                 void * const user_data)
{
    auto &widget = get_widget(motion);

    // If pointer didnʼt move, we got here from a synthesised enter: un-hover item *after* GTK does
    // Sadly it also catches item that ends under pointer after scroll, but I donʼt know how to fix
    if (bool const is_enter = GPOINTER_TO_INT(user_data);
        is_enter && !pointer_has_moved(widget))
    {
        Glib::signal_idle().connect_once([&]{ unset_state(widget); });
        return;
    }

    if (widget.has_focus()) return;
    widget.grab_focus(); // Weʼll then run the below handler @ notify::has-focus
}

static void on_leave_unset_state(GtkEventControllerMotion * const motion, double /*x*/, double /*y*/,
                                 void * /*user_data*/)
{
    auto &widget = get_widget(motion);
    if (!pointer_has_moved(widget)) return;
    auto &parent = dynamic_cast<Gtk::Widget &>(*widget.get_parent());
    unset_state(widget); // This is somehow needed for GtkPopoverMenu, although not our PopoverMenu
    unset_state(parent); // Try to unset state on all other menu items, in case we left by keyboard
}

void menuize(Gtk::Widget &widget)
{
    // If hovered naturally or below, key-focus self & clear focus+hover on rest
    auto const motion = gtk_event_controller_motion_new();
    gtk_event_controller_set_propagation_phase(motion, GTK_PHASE_TARGET);
    g_signal_connect(motion, "enter" , G_CALLBACK(on_motion_grab_focus), GINT_TO_POINTER(TRUE ));
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion_grab_focus), GINT_TO_POINTER(FALSE));
    g_signal_connect(motion, "leave" , G_CALLBACK(on_leave_unset_state), NULL);
    gtk_widget_add_controller(widget.gobj(), motion);

    // If key-focused in/out, ‘fake’ correspondingly appearing as hovered or not
    widget.property_has_focus().signal_changed().connect([&]
    {
        if (widget.has_focus()) {
            widget.set_state_flags(Gtk::StateFlags::PRELIGHT, false);
        } else {
            widget.unset_state_flags(Gtk::StateFlags::PRELIGHT);
        }
    });
}

template <typename Type>
static void menuize_all(Gtk::Widget &parent)
{
    for_each_descendant(parent, [](Gtk::Widget &child)
    {
        if (dynamic_cast<Type *>(&child)) {
            menuize(child);
        }
        return ForEachResult::_continue;
    });
}

static void menuize_all(Gtk::Widget &parent, Glib::ustring const &css_name)
{
    for_each_descendant(parent, [&](Gtk::Widget &child)
    {
        if (auto const klass = GTK_WIDGET_GET_CLASS(child.gobj());
            gtk_widget_class_get_css_name(klass) == css_name)
        {
            menuize(child);
        }
        return ForEachResult::_continue;
    });
}

void autohide_tooltip(Gtk::Popover &popover)
{
    popover.signal_show().connect([&]
    {
        if (auto const parent = popover.get_parent()) {
            parent->set_has_tooltip(false);
        }
    });
    popover.signal_closed().connect([&]
    {
        if (auto const parent = popover.get_parent()) {
            parent->set_has_tooltip(true);
        }
    });
}

void menuize_popover(Gtk::Popover &popover)
{
    static Glib::ustring const css_class = "menuize";

    if (popover.has_css_class(css_class)) return;

    popover.add_css_class(css_class);
    menuize_all(popover, "modelbutton");
    autohide_tooltip(popover);

    if (auto const popover_menu = dynamic_cast<Gtk::PopoverMenu *>(&popover)) {
        popover_menu->set_flags(Gtk::PopoverMenu::Flags::NESTED);
    }
}

std::unique_ptr<Gtk::Popover>
make_menuized_popover(Glib::RefPtr<Gio::MenuModel> model, Gtk::Widget &parent)
{
    auto popover = std::make_unique<Gtk::PopoverMenu>(model, Gtk::PopoverMenu::Flags::NESTED);
    popover->set_parent(parent);
    menuize_popover(*popover);
    return popover;
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
