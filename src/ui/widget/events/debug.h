// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef INKSCAPE_UI_WIDGET_EVENTS_DEBUG_H
#define INKSCAPE_UI_WIDGET_EVENTS_DEBUG_H
/**
 * @file
 * Debug printing of event data.
 */
/*
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <iostream>
#include "ui/widget/events/canvas-event.h"

namespace Inkscape {

/**
 * Whether event debug printing is enabled.
 */
inline constexpr bool DEBUG_EVENTS = false;

/**
 * Print an event to stdout.
 *
 * @arg event The event to print.
 * @arg prefix A string to print before the event, to help identify its context.
 * @arg merge Whether to compress consecutive motion events into one.
 */
inline void dump_event(CanvasEvent const &event, char const *prefix, bool merge = true)
{
    static EventType old_type = EventType::NUM_EVENTS;
    static unsigned count = 0;

    // Doesn't usually help to dump a zillion motion notify events.
    ++count;
    if (merge && event.type() == old_type && event.type() == EventType::MOTION) {
        if (count == 1) {
            std::cout << prefix << "  ... ditto" << std::endl;
        }
        return;
    }

    count = 0;
    old_type = event.type();

    std::cout << prefix << ": ";

    inspect_event(event,
        [] (ButtonPressEvent const &event) {
            std::cout << "ButtonPressEvent: " << event.button;
            if (auto n = event.num_press; n != 1) {
                std::cout << " num_press: " << n;
            }
            std::cout << std::endl;
        },
        [] (ButtonReleaseEvent const &event) {
            std::cout << "ButtonReleaseEvent: " << event.button << std::endl;
        },

        [] (KeyPressEvent const &event) {
            std::cout << "KeyPressEvent: " << std::hex
                      << " keycode:  " << event.keycode
                      << " state: "    << event.modifiers
                      << " keyval: "   << event.keyval << std::endl;
        },
        [] (KeyReleaseEvent const &event) {
            std::cout << "KeyReleaseEvent: " << event.keycode << std::endl;
        },

        [] (MotionEvent const &event) {
            std::cout << "MotionEvent" << std::endl;
        },
        [] (EnterEvent const &event) {
            std::cout << "EnterEvent" << std::endl;
        },
        [] (LeaveEvent const &event) {
            std::cout << "LeaveEvent" << std::endl;
        },

        [] (ScrollEvent const &event) {
            std::cout << "ScrollEvent" << std::endl;
        }
    );
}

} // namespace Inkscape

#endif // INKSCAPE_UI_WIDGET_EVENTS_DEBUG_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
