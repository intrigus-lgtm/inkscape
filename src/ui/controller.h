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

#ifndef SEEN_UI_CONTROLLER_H
#define SEEN_UI_CONTROLLER_H

#include <cstddef>
#include <functional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <sigc++/slot.h>
#include <glibmm/refptr.h>
#include <gdkmm/drag.h> // Gdk::DragCancelReason
#include <gdkmm/enums.h>
#include <gtk/gtk.h>
#include <gtkmm/gesture.h> // Gtk::EventSequenceState
#include <gtkmm/widget.h>
#include <gtkmm/window.h>

#include "util/callback-converter.h"

namespace Gtk {
class DragSource;
class DropTarget;
class EventController;
class GestureDrag;
class GestureClick;
class GestureSingle;
} // namespace Gtk

/// Utilities to more easily use Gtk::EventController & subclasses like Gesture.
namespace Inkscape::UI::Controller {

/// Helper to query if ModifierType state contains one or more of given flag(s).
// This will be needed in GTK4 as enums are scoped there, so bitwise is tougher.
[[nodiscard]] inline bool has_flag(Gdk::ModifierType const state,
                                   Gdk::ModifierType const flags)
    { return (state & flags) != Gdk::ModifierType{}; }

// migration aid for above, to later replace GdkModifierType w Gdk::ModifierType
[[nodiscard]] inline bool has_flag(GdkModifierType const state,
                                   GdkModifierType const flags)
    { return (state & flags) != GdkModifierType{}; }

/*
* * helpers to more easily add Controllers to Widgets
 */

/// Whether to connect a slot to a signal before or after the default handler.
enum class When {before, after};

// GestureClick

/// Type of slot connected to GestureClick::pressed & ::released signals.
/// The args are the gesture, n_press count, x coord & y coord (in widget space)
using ClickSlot = sigc::slot<Gtk::EventSequenceState (Gtk::GestureClick &, int, double, double)>;

/// helper to stop accidents on int vs gtkmm3's weak=typed enums, & looks nicer!
enum class Button {any = 0, left = 1, middle = 2, right = 3};

/// Create a click gesture for the given widget; by default claim sequence.
Gtk::GestureClick &add_click(Gtk::Widget &widget,
                             ClickSlot on_pressed,
                             ClickSlot on_released = {},
                             Button button = Button::any,
                             Gtk::PropagationPhase phase = Gtk::PropagationPhase::BUBBLE,
                             When when = When::after);

// GestureDrag

/// Type of slot connected to GestureDrag::drag-(begin|update|end) signals.
/// The arguments are the gesture, x coordinate & y coordinate (in widget space)
using DragSlot = sigc::slot<Gtk::EventSequenceState (Gtk::GestureDrag &, double, double)>;

/// Create a drag gesture for the given widget.
Gtk::GestureDrag &add_drag(Gtk::Widget &widget,
                           DragSlot on_drag_begin ,
                           DragSlot on_drag_update,
                           DragSlot on_drag_end   ,
                           Gtk::PropagationPhase phase = Gtk::PropagationPhase::BUBBLE,
                           When when = When::after);

// DragSource

/// Type of slot connected to the DragSource::prepare signal.
/// The arguments are the controller and the X and Y coordinates of the drag starting point.
using DragSourcePrepareSlot = sigc::slot<Glib::RefPtr<Gdk::ContentProvider> (Gtk::DragSource &, double, double)>;

/// Type of slot connected to the DragSource::drag-begin signal.
/// The arguments are the controller and the Gdk::Drag object.
using DragSourceDragBeginSlot = sigc::slot<void (Gtk::DragSource &, Glib::RefPtr<Gdk::Drag> const &)>;

/// Type of slot connected to the DragSource::drag-cancel signal.
/// The arguments are the controller, the Gdk::Drag object, and the reason/information on why the drag failed.
/// Return whether the failed drag operation has been handled.
using DragSourceDragCancelSlot = sigc::slot<bool (Gtk::DragSource &,
                                                  Glib::RefPtr<Gdk::Drag> const &,
                                                  Gdk::DragCancelReason)>;

/// Type of slot connected to the DragSource::drag-end signal.
/// The arguments are the controller, the Gdk::Drag object, and whether DragAction::MOVE ∴ data should be deleted.
using DragSourceDragEndSlot = sigc::slot<void (Gtk::DragSource &, Glib::RefPtr<Gdk::Drag> const &, bool)>;

/// Arguments for add_drag_source(), so calling w/ various combos is nicer via desigʼd initialisers
struct AddDragSourceArgs final {
    Button          button  = Button::left;
    Gdk::DragAction actions = Gdk::DragAction::COPY;
    Glib::RefPtr<Gdk::ContentProvider> content;
    DragSourcePrepareSlot    prepare;
    DragSourceDragBeginSlot  begin  ;
    DragSourceDragCancelSlot cancel ;
    DragSourceDragEndSlot    end    ;
};

/// Create a drag source for the given widget.
Gtk::DragSource &add_drag_source(Gtk::Widget &widget,
                                 AddDragSourceArgs &&args = {},
                                 Gtk::PropagationPhase phase = Gtk::PropagationPhase::BUBBLE,
                                 When when = When::after);

// DropTarget

/// Type of slot connected to the DropTarget::enter and DropTarget::motion signals.
/// The arguments are the controller and current pointer x/y coordinates.
/// Return the preferred action for this drag operation or 0 if dropping isnʼt supported at x,y pos
using DropTargetMotionSlot = sigc::slot<Gdk::DragAction (Gtk::DropTarget &, double, double)>;

/// Type of slot connected to the DropTarget::accept signal.
/// The arguments are the controller and the Gdk::Drop object.
/// Return true if @a drop is accepted.
using DropTargetAcceptSlot = sigc::slot<bool (Gtk::DropTarget &, Glib::RefPtr<Gdk::Drop> const &)>;

/// Type of slot connected to the DropTarget::drop signal.
/// The arguments are the controller, the value being dropped, and current pointer x/y coordinates.
/// Return whether the drop was accepted at the given pointer position.
using DropTargetDropSlot = sigc::slot<bool (Gtk::DropTarget &, Glib::ValueBase const &, double, double)>;

/// Type of slot connected to the DropTarget::leave signal.
/// The arguments are the controller and current pointer x/y coordinates.
/// Return the preferred action for this drag operation or 0 if dropping isnʼt supported at x,y pos
using DropTargetLeaveSlot = sigc::slot<void (Gtk::DropTarget &)>;

/// Arguments for add_drop_target(), so calling w/ various combos is nicer via desigʼd initialisers
struct AddDropTargetArgs final {
    Gdk::DragAction  actions = {};
    std::vector<GType> types;
    DropTargetMotionSlot enter ;
    DropTargetMotionSlot motion;
    DropTargetAcceptSlot accept;
    DropTargetDropSlot   drop  ;
    DropTargetLeaveSlot  leave ;
};

/// Create a drop target for the given widget, supporting the given type(s) and drag actions.
Gtk::DropTarget &add_drop_target(Gtk::Widget &widget,
                                 AddDropTargetArgs &&args,
                                 Gtk::PropagationPhase phase = Gtk::PropagationPhase::BUBBLE,
                                 When when = When::after);

/// internal stuff
namespace Detail {

/// Helper to create EventController or subclass, for the given the widget.
[[nodiscard]] Gtk::EventController &add(Gtk::Widget &widget,
                                        Glib::RefPtr<Gtk::EventController> const &controller,
                                        Gtk::PropagationPhase const phase);

/// Helper to connect member func of a C++ Listener object to a C GObject signal.
/// If method is not a nullptr, calls make_g_callback<method>, & connects result
/// with either g_signal_connect (when=before) or g_signal_connect_after (after)
template <auto method,
          typename Emitter, typename Listener>
void connect(Emitter * const emitter, char const * const detailed_signal,
             Listener &listener, When const when)
{
    // Special-case nullptr so we neednʼt make a no-op function to connect.
    if constexpr (std::is_same_v<decltype(method), std::nullptr_t>) return;
    else {
        auto const object = G_OBJECT(emitter);
        auto const c_handler = Util::make_g_callback<method>;

        switch (when) {
            case When::before:
                g_signal_connect(object, detailed_signal, c_handler, &listener);
                break;

            case When::after:
                g_signal_connect_after(object, detailed_signal, c_handler, &listener);
                break;

            default:
                g_assert_not_reached();
        }
    }
}

/// Whether Function can be invoked with Args... to return Result; OR it's a nullptr.
// FIXME: shouldnʼt allow functions that return non-void for signals declaring a void
//        result, as thatʼs misleading, but I didnʼt yet manage that with type_traits
// TODO: C++20: Use <concepts> instead…but not quite yet since Ubuntu 20.04 GCC lacks
template <typename Function, typename Result, typename ...Args>
auto constexpr callable_or_null = std::is_same_v       <Function, std::nullptr_t > ||
                                  std::is_invocable_r_v<Result, Function, Args...>;

} // namespace Detail

/// Whether Function is suitable to handle Gesture::begin|end.
/// The arguments are the gesture & triggering event sequence.
template <typename Function, typename Listener>
auto constexpr is_gesture_handler = Detail::callable_or_null<Function, void,
    Listener *, GtkGesture *, GdkEventSequence *>;

/// Whether Function is suitable to handle EventControllerKey::pressed|released.
/// The arguments are the controller, keyval, hardware keycode & modifier state.
template <typename Function, typename Listener>
auto constexpr is_key_handler = Detail::callable_or_null<Function, bool,
    Listener *, GtkEventControllerKey *, unsigned, unsigned, GdkModifierType>;

/// Whether Function is suitable to handle EventControllerKey::modifiers.
/// The arguments are the controller & modifier state.
/// Note that this signal seems buggy, i.e. gives wrong state, in GTK3. Beware!!
template <typename Function, typename Listener>
auto constexpr is_key_mod_handler = Detail::callable_or_null<Function, bool,
    Listener *, GtkEventControllerKey *, GdkModifierType>;

/// Whether Function is suitable to handle EventControllerMotion::enter|motion.
/// The arguments are the controller, x coordinate & y coord (in widget space).
template <typename Function, typename Listener>
auto constexpr is_motion_handler = Detail::callable_or_null<Function, void,
    Listener *, GtkEventControllerMotion *, double, double>;

/// Whether Function is suitable to handle EventControllerMotion::leave.
/// The argument is the controller. Coordinates arenʼt given on leaving.
template <typename Function, typename Listener>
auto constexpr is_leave_handler = Detail::callable_or_null<Function, void,
    Listener *, GtkEventControllerMotion *>;

/// Whether Function is suitable for EventControllerScroll::scroll-(begin|end).
/// The argument is the controller.
template <typename Function, typename Listener>
auto constexpr is_scroll_handler = Detail::callable_or_null<Function, void,
    Listener *, GtkEventControllerScroll *>;

/// Whether Function is suitable for EventControllerScroll::scroll|decelerate.
/// The arguments are controller & for scroll dx,dy; or decelerate: vel_x, vel_y
template <typename Function, typename Listener>
auto constexpr is_scroll_xy_handler = Detail::callable_or_null<Function, void,
    Listener *, GtkEventControllerScroll *, double, double>;

/// Whether Function is suitable for GestureZoom::scale-changed.
/// The arguments are gesture & scale delta (initial state as 1)
template <typename Function, typename Listener>
auto constexpr is_zoom_scale_handler = Detail::callable_or_null<Function, void,
    Listener *, GtkGestureZoom *, double>;

// TODO: GTK4: The below should be gtkmm-ified, but for now, I am only making min changes to build.

/// Create a key event controller for the given widget.
/// Note that ::modifiers seems buggy, i.e. gives wrong state, in GTK3. Beware!!
// As gtkmm 3 lacks EventControllerKey, this must go via C API, so to make it
// easier I reuse Util::make_g_callback(), which needs methods as template args.
// Once on gtkmm4, we can do as Click etc, & accept anything convertible to slot
template <auto on_pressed, auto on_released = nullptr, auto on_modifiers = nullptr,
          bool return_refptr = false, typename Listener>
decltype(auto) add_key(Gtk::Widget &widget, Listener &listener,
                       Gtk::PropagationPhase const phase = Gtk::PropagationPhase::BUBBLE,
                       When const when = When::after)
{
    // NB make_g_callback<> must type-erase methods, so we must check arg compat
    // TODO: C++20: Use <concepts> instead…but not quite yet since Ubuntu 20.04 GCC lacks
    static_assert(is_key_handler      <decltype(on_pressed  ), Listener>);
    static_assert(is_key_handler      <decltype(on_released ), Listener>);
    static_assert(is_key_mod_handler  <decltype(on_modifiers), Listener>);

    auto const gcontroller = gtk_event_controller_key_new();
    Detail::connect<on_pressed  >(gcontroller, "key-pressed" , listener, when);
    Detail::connect<on_released >(gcontroller, "key-released", listener, when);
    Detail::connect<on_modifiers>(gcontroller, "modifiers"   , listener, when);

    auto controller = Glib::wrap(gcontroller);
    if constexpr (return_refptr) {
        static_cast<void>(Detail::add(widget, controller, phase));
        return controller;
    } else {
        return Detail::add(widget, std::move(controller), phase);
    }
}

/// Create a motion event controller for the given widget.
// See comments for add_key().
template <auto on_enter, auto on_motion, auto on_leave,
          typename Listener>
Gtk::EventController &add_motion(Gtk::Widget &widget  ,
                                 Listener    &listener,
                                 Gtk::PropagationPhase const phase = Gtk::PropagationPhase::BUBBLE,
                                 When const when = When::after)
{
    // NB make_g_callback<> must type-erase methods, so we must check arg compat
    static_assert(is_motion_handler<decltype(on_enter ), Listener>);
    static_assert(is_motion_handler<decltype(on_motion), Listener>);
    static_assert( is_leave_handler<decltype(on_leave ), Listener>);

    auto const gcontroller = gtk_event_controller_motion_new();
    Detail::connect<on_enter >(gcontroller, "enter" , listener, when);
    Detail::connect<on_motion>(gcontroller, "motion", listener, when);
    Detail::connect<on_leave >(gcontroller, "leave" , listener, when);
    return Detail::add(widget, Glib::wrap(gcontroller), phase);
}

/// Create a scroll event controller for the given widget.
// See comments for add_key().
template <auto on_scroll_begin, auto on_scroll, auto on_scroll_end, auto on_decelerate = nullptr,
          typename Listener>
Gtk::EventController &add_scroll(Gtk::Widget &widget  ,
                                 Listener    &listener,
                                 GtkEventControllerScrollFlags const flags = GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES,
                                 Gtk::PropagationPhase const phase = Gtk::PropagationPhase::BUBBLE,
                                 When const when = When::after)
{
    // NB make_g_callback<> must type-erase methods, so we must check arg compat
    static_assert(is_scroll_handler   <decltype(on_scroll_begin), Listener>);
    static_assert(is_scroll_xy_handler<decltype(on_scroll      ), Listener>);
    static_assert(is_scroll_handler   <decltype(on_scroll_end  ), Listener>);
    static_assert(is_scroll_xy_handler<decltype(on_decelerate  ), Listener>);

    auto const gcontroller = gtk_event_controller_scroll_new(flags);
    Detail::connect<on_scroll_begin>(gcontroller, "scroll-begin", listener, when);
    Detail::connect<on_scroll      >(gcontroller, "scroll"      , listener, when);
    Detail::connect<on_scroll_end  >(gcontroller, "scroll-end"  , listener, when);
    Detail::connect<on_decelerate  >(gcontroller, "decelerate"  , listener, when);
    return Detail::add(widget, Glib::wrap(gcontroller), phase);
}

/// Create a zoom gesture for the given widget.
template <auto on_begin, auto on_scale_changed, auto on_end,
          typename Listener>
decltype(auto) add_zoom(Gtk::Widget &widget  ,
                        Listener    &listener,
                        Gtk::PropagationPhase const phase = Gtk::PropagationPhase::BUBBLE,
                        When const when = When::after)
{
    // NB make_g_callback<> must type-erase methods, so we must check arg compat
    static_assert(is_gesture_handler   <decltype(on_begin        ), Listener>);
    static_assert(is_zoom_scale_handler<decltype(on_scale_changed), Listener>);
    static_assert(is_gesture_handler   <decltype(on_end          ), Listener>);

    auto const gcontroller = GTK_EVENT_CONTROLLER(gtk_gesture_zoom_new());
    Detail::connect<on_begin        >(gcontroller, "begin"        , listener, when);
    Detail::connect<on_scale_changed>(gcontroller, "scale-changed", listener, when);
    Detail::connect<on_end          >(gcontroller, "end"          , listener, when);
    return Detail::add(widget, Glib::wrap(gcontroller), phase);
}

/*
* * helpers to track events on root/toplevel Windows
 */

namespace Detail {
inline auto controllers = std::unordered_map<Gtk::Widget *,
                                             std::vector<Glib::RefPtr<Gtk::EventController>>>{};
} // namespace Detail

/// Wait for widget to be mapped in a window, add a key controller to the window
/// & retain a reference to said controller until the widget is (next) unmapped.
// See comments for add_key().
// TODO: GTK4: may not be needed once our Windows donʼt intercept/forward/whatever w/ ::key-events?
template <auto on_pressed, auto on_released = nullptr, auto on_modifiers = nullptr,
          typename Listener>
void add_key_on_window(Gtk::Widget &widget, Listener &listener,
                       Gtk::PropagationPhase const phase = Gtk::PropagationPhase::BUBBLE,
                       When const when = When::after)
{
    widget.signal_map().connect([=, &widget, &listener]
    {
        auto &window = dynamic_cast<Gtk::Window &>(*widget.get_root());
        auto controller = add_key<on_pressed, on_released, on_modifiers,
                                  true, Listener>(window, listener, phase, when);
        Detail::controllers[&widget].push_back(std::move(controller));
    });
    widget.signal_unmap().connect([&widget]
    {
        auto const it = Detail::controllers.find(&widget);
        if (it == Detail::controllers.end()) return;

        for (auto const &controller: it->second) {
            auto &window = dynamic_cast<Gtk::Window &>(*controller->get_widget());
            window.remove_controller(controller);
        }
        Detail::controllers.erase(it);
    });
}

/// Type of slot connected to Gtk::Window::set-focus by add_focus_on_window().
/// The argument is the new focused widget of the window.
using WindowFocusSlot = sigc::slot<void (Gtk::Widget *)>;

/// Wait for widget to be mapped in a window, add a slot handling ::set-focus on
/// that window, & keep said slot connected until the widget is (next) unmapped.
void add_focus_on_window(Gtk::Widget &widget, WindowFocusSlot slot);

} // namespace Inkscape::UI::Controller

#endif // SEEN_UI_CONTROLLER_H

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
