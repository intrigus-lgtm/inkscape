// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   buliabyak@gmail.com
 *
 * Copyright (C) 2007 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <sigc++/functors/mem_fun.h>
#include <gtkmm/gestureclick.h>
#include <2geom/point.h>

#include "rotateable.h"
#include "ui/controller.h"
#include "ui/tools/tool-base.h"

namespace Inkscape::UI::Widget {

Rotateable::Rotateable():
    axis(-M_PI/4),
    maxdecl(M_PI/4),
    dragging(false),
    working(false),
    scrolling(false),
    modifier(0),
    current_axis(axis)
{
    Controller::add_click(*this, sigc::mem_fun(*this, &Rotateable::on_click  ),
                                 sigc::mem_fun(*this, &Rotateable::on_release),
                          Controller::Button::left);
    Controller::add_motion<nullptr, &Rotateable::on_motion, nullptr>(*this, *this);
    Controller::add_scroll<nullptr, &Rotateable::on_scroll, nullptr>(*this, *this,
                                                                     GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
}

Gtk::EventSequenceState Rotateable::on_click(Gtk::GestureClick const &click,
                                             int /*n_press*/, double const x, double const y)
{
    drag_started_x = x;
    drag_started_y = y;
    auto const state = click.get_current_event_state();
    modifier = get_single_modifier(modifier, unsigned(state));
    dragging = true;
    working = false;
    current_axis = axis;
    return Gtk::EventSequenceState::NONE; // no claim, would stop release being fired
}

unsigned Rotateable::get_single_modifier(unsigned const old, unsigned const state)
{
    if (old == 0 || old == 3) {
        if (state & GDK_CONTROL_MASK)
            return 1; // ctrl
        if (state & GDK_SHIFT_MASK)
            return 2; // shift
        if (state & GDK_ALT_MASK)
            return 3; // alt
        return 0;
    }

    if (!(state & GDK_CONTROL_MASK) && !(state & GDK_SHIFT_MASK)) {
        if (state & GDK_ALT_MASK)
            return 3; // alt
        else
            return 0; // none
    }

    if (old == 1) {
        if (state & GDK_SHIFT_MASK && !(state & GDK_CONTROL_MASK))
            return 2; // shift
        if (state & GDK_ALT_MASK && !(state & GDK_CONTROL_MASK))
           return 3; // alt
        return 1;
    }

    if (old == 2) {
        if (state & GDK_CONTROL_MASK && !(state & GDK_SHIFT_MASK))
            return 1; // ctrl
        if (state & GDK_ALT_MASK && !(state & GDK_SHIFT_MASK))
           return 3; // alt
        return 2;
    }

    return old;
}

void Rotateable::on_motion(GtkEventControllerMotion const * const motion,
                           double const x, double const y)
{
    if (!dragging) {
        return;
    }

    double dist = Geom::L2(Geom::Point(x, y) - Geom::Point(drag_started_x, drag_started_y));
    if (dist > 20) {
        working = true;

        double angle = atan2(y - drag_started_y, x - drag_started_x);
        double force = CLAMP (-(angle - current_axis)/maxdecl, -1, 1);
        if (fabs(force) < 0.002)
            force = 0; // snap to zero

        auto const state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(motion));
        auto const new_modifier = get_single_modifier(modifier, state);
        if (modifier != new_modifier) {
            // user has switched modifiers in mid drag, close past drag and start a new
            // one, redefining axis temporarily
            do_release(force, modifier);
            current_axis = angle;
            modifier = new_modifier;
        } else {
            do_motion(force, modifier);
        }
    }

    Inkscape::UI::Tools::gobble_motion_events(GDK_BUTTON1_MASK);
}


Gtk::EventSequenceState Rotateable::on_release(Gtk::GestureClick const & /*click*/,
                                               int /*n_press*/, double const x, double const y)
{
    if (dragging && working) {
        double angle = atan2(y - drag_started_y, x - drag_started_x);
        double force = CLAMP(-(angle - current_axis) / maxdecl, -1, 1);
        if (fabs(force) < 0.002)
            force = 0; // snap to zero

        do_release(force, modifier);
        current_axis = axis;
        dragging = false;
        working = false;
        return Gtk::EventSequenceState::CLAIMED;
    }

    dragging = false;
    working = false;
    return Gtk::EventSequenceState::NONE;
}

bool Rotateable::on_scroll(GtkEventControllerScroll const * const scroll,
                           double /*dx*/, double const dy)
{
    double change = 0.0;
    double delta_y_clamped = CLAMP(dy, -1.0, 1.0); // values > 1 result in excessive changes
    change = 1.0 * -delta_y_clamped;

#if 0 // We can't (easily) get this from a scroll controller. Do we need it?
    drag_started_x = event->x;
    drag_started_y = event->y;
#endif

    auto const state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(scroll));
    modifier = get_single_modifier(modifier, state);
    dragging = false;
    working = false;
    scrolling = true;
    current_axis = axis;

    do_scroll(change, modifier);

    dragging = false;
    working = false;
    scrolling = false;

    return true;
}

Rotateable::~Rotateable() = default;

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
