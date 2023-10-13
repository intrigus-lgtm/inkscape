// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Utilities to more easily use Gtk::EventController & subclasses like Gesture.
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "controller.h"

#include <cassert>
#include <sigc++/adaptors/bind.h>
#include <gdk/gdk.h>
#include <gtkmm/gesturedrag.h>
#include <gtkmm/gesturemultipress.h>

#include "helper/auto-connection.h"

namespace Inkscape::UI::Controller {

template <typename T> static auto asserted(T * const t) { assert(t); return t; }

// TODO: GTK4: We will have gtkmm API for all controllers, so migrate from C API

// TODO: GTK4: We will have EventController.get_current_event_state(). And phew!
Gdk::ModifierType get_device_state(GtkEventController const * const controller)
{
    auto const widget = asserted(
        gtk_event_controller_get_widget(const_cast<GtkEventController *>(controller)));
    auto const surface = asserted(gtk_widget_get_window       (widget ));
    auto const display = asserted(gdk_window_get_display      (surface));
    auto const seat    = asserted(gdk_display_get_default_seat(display));
    auto const device  = asserted(gdk_seat_get_pointer        (seat   ));
    GdkModifierType state{};
    gdk_window_get_device_position(surface, device, NULL, NULL, &state);
    return static_cast<Gdk::ModifierType>(state);
}

GdkEvent const *get_last_event(Gtk::GestureSingle const &gesture)
{
    auto const sequence = gesture.get_current_sequence();
    return gesture.get_last_event(const_cast<GdkEventSequence *>(sequence));
}

// TODO: GTK4: We can replace w/ just EventController.get_current_event_state().
Gdk::ModifierType get_current_event_state(Gtk::GestureSingle const &gesture)
{
    auto state = GdkModifierType{};
    gdk_event_get_state(get_last_event(gesture), &state);
    return static_cast<Gdk::ModifierType>(state);
}

unsigned get_group(GtkEventControllerKey const * const controller)
{
    return gtk_event_controller_key_get_group(const_cast<GtkEventControllerKey *>(controller));
}

namespace {

/// Helper to create EventController or subclass, for & manage()d by the widget.
template <typename Controller>
[[nodiscard]] Controller &create(Gtk::Widget &widget, Gtk::PropagationPhase const phase)
{
    static_assert(std::is_base_of_v<Gtk::EventController, Controller>);
    auto &controller = Detail::managed(Controller::create(widget), widget);
    controller.set_propagation_phase(phase);
    return controller;
}

/// Helper to invoke getter on object, & connect a slot to the resulting signal.
template <typename Object, typename Getter, typename Slot>
void connect(Object &object, Getter const getter, Slot slot, When const when)
{
    auto signal = std::invoke(getter, object);
    signal.connect(sigc::bind<0>(std::move(slot), std::ref(object)),
                   when == When::after);
}

// We add the requirement that slots return an EventSequenceState, which if itʼs
// not NONE we set on the controller. This makes it easier & less error-prone to
// migrate code that returned a bool whether GdkEvent is handled, to Controllers
// & their way of claiming the sequence if handled – as then we only require end
// users to change their returned type/value – rather than need them to manually
// call controller.set_state(), which is easy to forget & unlike a return cannot
// be enforced by the compiler. So… this wraps a callerʼs slot that returns that
// state & uses it, with a void-returning wrapper as thatʼs what GTK/mm expects.
template <typename Slot>
[[nodiscard]] auto use_state(Slot &&slot)
{
    return [slot = std::move(slot)](auto &controller, auto &&...args)
    {
        if (!slot) return;
        Gtk::EventSequenceState const state = slot(
            controller, std::forward<decltype(args)>(args)...);
        if (state != Gtk::EventSequenceState::NONE) {
            controller.set_state(state);
        }
    };
}

} // unnamed namespace

Gtk::GestureClick &add_click(Gtk::Widget &widget,
                                  ClickSlot on_pressed,
                                  ClickSlot on_released,
                                  Button const button,
                                  Gtk::PropagationPhase const phase,
                                  When const when)
{
    auto &click = create<Gtk::GestureClick>(widget, phase);
    connect(click, &Gtk::GestureClick::signal_pressed , use_state(std::move(on_pressed )), when);
    connect(click, &Gtk::GestureClick::signal_released, use_state(std::move(on_released)), when);
    click.set_button(static_cast<int>(button));
    return click;
}

Gtk::GestureDrag &add_drag(Gtk::Widget &widget,
                           DragSlot on_drag_begin ,
                           DragSlot on_drag_update,
                           DragSlot on_drag_end   ,
                           Gtk::PropagationPhase const phase,
                           When const when)
{
    auto &drag = create<Gtk::GestureDrag>(widget, phase);
    connect(drag, &Gtk::GestureDrag::signal_drag_begin , use_state(std::move(on_drag_begin )), when);
    connect(drag, &Gtk::GestureDrag::signal_drag_update, use_state(std::move(on_drag_update)), when);
    connect(drag, &Gtk::GestureDrag::signal_drag_end   , use_state(std::move(on_drag_end   )), when);
    return drag;
}

// TODO: GTK4: EventControllerFocus.property_contains_focus() should make this slightly nicer?
void add_focus_on_window(Gtk::Widget &widget, WindowFocusSlot slot)
{
    static auto connections = std::unordered_map<Gtk::Widget *, std::vector<auto_connection>>{};
    widget.signal_map().connect([&widget, slot = std::move(slot)]
    {
        auto &window = dynamic_cast<Gtk::Window &>(*widget.get_toplevel());
        auto connection = auto_connection{window.signal_set_focus().connect(slot)};
        connections[&widget].push_back(std::move(connection));
    });
    widget.signal_unmap().connect([&widget]{ connections.erase(&widget); });
}

} // namespace Inkscape::UI::Controller

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
