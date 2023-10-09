// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Event handlers for SPDesktop.
 */
/* Author:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Abhishek Sharma
 *
 * Copyright (C) 1999-2002 Lauris Kaplinski
 * Copyright (C) 1999-2010 Others
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "desktop-events.h"

#include <map>
#include <string>
#include <2geom/line.h>
#include <2geom/angle.h>
#include <glibmm/i18n.h>
#include <gdkmm/devicetool.h>
#include <gdkmm/display.h>
#include <gdkmm/seat.h>
#include <glibmm/i18n.h>
#include <glibmm/ustring_hash.h>
#include <gtk/gtk.h>

#include "desktop.h"
#include "document-undo.h"
#include "message-context.h"
#include "preferences.h"
#include "snap.h"
#include "actions/actions-tools.h"
#include "display/control/canvas-item-guideline.h"
#include "object/sp-guide.h"
#include "object/sp-namedview.h"
#include "ui/cursor-utils.h"
#include "ui/dialog/guides.h"
#include "ui/tools/tool-base.h"
#include "ui/tools/node-tool.h"
#include "ui/tools/select-tool.h"
#include "ui/widget/canvas.h"
#include "ui/widget/events/canvas-event.h"
#include "ui/widget/events/debug.h"

using Inkscape::DocumentUndo;
using Inkscape::EventType;

static void snoop_extended(Inkscape::CanvasEvent const &event, SPDesktop *desktop);
static void init_extended();

bool sp_desktop_root_handler(Inkscape::CanvasEvent const &event, SPDesktop *desktop)
{
    if constexpr (Inkscape::DEBUG_EVENTS) {
        Inkscape::dump_event(event, "sp_desktop_root_handler");
    }

    static bool watch = false;
    static bool first = true;

    if (first) {
        auto prefs = Inkscape::Preferences::get();
        if (prefs->getBool("/options/useextinput/value", true) &&
            prefs->getBool("/options/switchonextinput/value"))
        {
            watch = true;
            init_extended();
        }
        first = false;
    }

    if (watch) {
        snoop_extended(event, desktop);
    }

    if (auto const tool = desktop->getTool()) {
        return tool->start_root_handler(event);
    }

    return false;
}

static Geom::Point drag_origin;
static SPGuideDragType drag_type = SP_DRAG_NONE;
static bool guide_moved = false;

bool sp_dt_guide_event(Inkscape::CanvasEvent const &event, Inkscape::CanvasItemGuideLine *guide_item, SPGuide *guide)
{
    if constexpr (Inkscape::DEBUG_EVENTS) {
        Inkscape::dump_event(event, "sp_dt_guide_event");
    }

    bool ret = false;

    auto desktop = guide_item->get_canvas()->get_desktop();
    if (!desktop) {
        std::cerr << "sp_dt_guide_event: No desktop!" << std::endl;
        return false;
    }

    // Limit to select/node tools only.
    if (!dynamic_cast<Inkscape::UI::Tools::SelectTool *>(desktop->getTool()) &&
        !dynamic_cast<Inkscape::UI::Tools::NodeTool *>(desktop->getTool()))
    {
        return false;
    }

    auto apply_snap = [desktop, guide] (Geom::Point &event_dt, unsigned modifiers) {
        // This is for snapping while dragging existing guidelines. New guidelines,
        // which are dragged off the ruler, are being snapped in sp_dt_ruler_event
        auto &m = desktop->getNamedView()->snap_manager;
        m.setup(desktop, true, guide, nullptr);
        if (drag_type == SP_DRAG_MOVE_ORIGIN) {
            // If we snap in guideConstrainedSnap() below, then motion_dt will
            // be forced to be on the guide. If we don't snap however, then
            // the origin should still be constrained to the guide. So let's do
            // that explicitly first:
            auto const line = Geom::Line(guide->getPoint(), guide->angle());
            auto const t = line.nearestTime(event_dt);
            event_dt = line.pointAt(t);
            if (!(modifiers & GDK_SHIFT_MASK)) {
                m.guideConstrainedSnap(event_dt, *guide);
            }
        } else if (!(drag_type == SP_DRAG_ROTATE && modifiers & GDK_CONTROL_MASK)) {
            // Cannot use shift here to disable snapping, because we already use it for rotating the guide.
            Geom::Point tmp;
            if (drag_type == SP_DRAG_ROTATE) {
                tmp = guide->getPoint();
                m.guideFreeSnap(event_dt, tmp, true, false);
                guide->moveto(tmp, false);
            } else {
                tmp = guide->getNormal();
                m.guideFreeSnap(event_dt, tmp, false, true);
                guide->set_normal(tmp, false);
            }
        }
        m.unSetup();
    };

    auto move_guide = [guide] (Geom::Point const &event_dt, unsigned modifiers, bool flag) {
        switch (drag_type) {
            case SP_DRAG_TRANSLATE:
            case SP_DRAG_MOVE_ORIGIN:
                guide->moveto(event_dt, flag);
                break;
            case SP_DRAG_ROTATE: {
                auto angle = Geom::Angle(event_dt - guide->getPoint());
                if (modifiers & GDK_CONTROL_MASK) {
                    auto prefs = Inkscape::Preferences::get();
                    if (auto snaps = std::abs(prefs->getInt("/options/rotationsnapsperpi/value", 12))) {
                        if (prefs->getBool("/options/relativeguiderotationsnap/value", false)) {
                            auto orig_angle = Geom::Angle(guide->getNormal());
                            auto snap_angle = angle - orig_angle;
                            double sections = std::floor(snap_angle.radians0() * snaps / M_PI + 0.5);
                            angle = M_PI / snaps * sections + orig_angle.radians0();
                        } else {
                            double sections = std::floor(angle.radians0() * snaps / M_PI + 0.5);
                            angle = M_PI / snaps * sections;
                        }
                    }
                }
                guide->set_normal(Geom::Point::polar(angle).cw(), flag);
                break;
            }
            default:
                assert(false);
                break;
        }
    };

    inspect_event(event,
        [&] (Inkscape::ButtonPressEvent const &event) {
            if (event.num_press == 2) {
                if (event.button == 1) {
                    drag_type = SP_DRAG_NONE;
                    desktop->getTool()->discard_delayed_snap_event();
                    guide_item->ungrab();
                    Inkscape::UI::Dialog::GuidelinePropertiesDialog::showDialog(guide, desktop);
                    ret = true;
                }
            } else if (event.num_press == 1) {
                if (event.button == 1 && !guide->getLocked()) {
                    auto const event_dt = desktop->w2d(event.pos);

                    // Due to the tolerance allowed when grabbing a guide, event_dt will generally
                    // be close to the guide but not just exactly on it. The drag origin calculated
                    // here must be exactly on the guide line though, otherwise
                    // small errors will occur once we snap, see
                    // https://bugs.launchpad.net/inkscape/+bug/333762
                    drag_origin = Geom::projection(event_dt, Geom::Line(guide->getPoint(), guide->angle()));

                    if (event.modifiers & GDK_SHIFT_MASK) {
                        // with shift we rotate the guide
                        drag_type = SP_DRAG_ROTATE;
                    } else if (event.modifiers & GDK_CONTROL_MASK) {
                        drag_type = SP_DRAG_MOVE_ORIGIN;
                    } else {
                        drag_type = SP_DRAG_TRANSLATE;
                    }

                    if (drag_type == SP_DRAG_ROTATE || drag_type == SP_DRAG_TRANSLATE) {
                        guide_item->grab(EventType::BUTTON_RELEASE |
                                         EventType::BUTTON_PRESS   |
                                         EventType::MOTION);
                    }
                    ret = true;
                }
            }
        },

        [&] (Inkscape::MotionEvent const &event) {
            if (drag_type == SP_DRAG_NONE) {
                return;
            }

            desktop->getTool()->snap_delay_handler(guide_item, guide, event,
                                                           Inkscape::UI::Tools::DelayedSnapEvent::GUIDE_HANDLER);

            auto event_dt = desktop->w2d(event.pos);
            apply_snap(event_dt, event.modifiers);
            move_guide(event_dt, event.modifiers, false);

            guide_moved = true;
            desktop->set_coordinate_status(event_dt);
            desktop->getCanvas()->grab_focus();

            ret = true;
        },

        [&] (Inkscape::ButtonReleaseEvent const &event) {
            if (drag_type == SP_DRAG_NONE || event.button != 1) {
                return;
            }

            desktop->getTool()->discard_delayed_snap_event();

            if (guide_moved) {
                auto event_dt = desktop->w2d(event.pos);
                apply_snap(event_dt, event.modifiers);

                if (guide_item->get_canvas()->world_point_inside_canvas(event.pos)) {
                    move_guide(event_dt, event.modifiers, true);
                    DocumentUndo::done(desktop->getDocument(), _("Move guide"), "");
                } else {
                    // Undo movement of any attached shapes.
                    guide->moveto(guide->getPoint(), false);
                    guide->set_normal(guide->getNormal(), false);
                    guide->remove();
                    guide_item = nullptr;
                    desktop->getTool()->use_tool_cursor();

                    DocumentUndo::done(desktop->getDocument(), _("Delete guide"), "");
                }

                guide_moved = false;
                desktop->set_coordinate_status(event_dt);
            }

            drag_type = SP_DRAG_NONE;
            if (guide_item) {
                guide_item->ungrab();
            }

            ret = true;
        },

        [&] (Inkscape::EnterEvent const &event) {
            auto const canvas = desktop->getCanvas();
            g_assert(canvas);

            // This is a UX thing. Check if the canvas has focus, so the user knows they can
            // use hotkeys. See issue: https://gitlab.com/inkscape/inkscape/-/issues/2439
            if (!guide->getLocked() && canvas->has_focus()) {
                guide_item->set_stroke(guide->getHiColor());
            }

            // set move or rotate cursor
            if (guide->getLocked()) {
                Inkscape::set_svg_cursor(*canvas, "select.svg");
            } else if (event.modifiers & GDK_SHIFT_MASK && drag_type != SP_DRAG_MOVE_ORIGIN) {
                Inkscape::set_svg_cursor(*canvas, "rotate.svg");
            } else {
                canvas->set_cursor("grab");
            }

            auto guide_description = guide->description();
            desktop->guidesMessageContext()->setF(Inkscape::NORMAL_MESSAGE, _("<b>Guideline</b>: %s"), guide_description);
            g_free(guide_description);
        },

        [&] (Inkscape::LeaveEvent const &event) {
            guide_item->set_stroke(guide->getColor());

            // restore event context's cursor
            desktop->getTool()->use_tool_cursor();

            desktop->guidesMessageContext()->clear();
        },

        [&] (Inkscape::KeyPressEvent const &event) {
            switch (Inkscape::UI::Tools::get_latin_keyval(event)) {
                case GDK_KEY_Delete:
                case GDK_KEY_KP_Delete:
                case GDK_KEY_BackSpace:
                    if (!guide->getLocked()) {
                        auto doc = guide->document;
                        guide->remove();
                        guide_item = nullptr;
                        DocumentUndo::done(doc, _("Delete guide"), "");
                        ret = true;
                        desktop->getTool()->discard_delayed_snap_event();
                        desktop->getTool()->use_tool_cursor();
                    }
                    break;
                case GDK_KEY_Shift_L:
                case GDK_KEY_Shift_R:
                    if (drag_type != SP_DRAG_MOVE_ORIGIN) {
                        Inkscape::set_svg_cursor(*desktop->getCanvas(), "rotate.svg");
                        ret = true;
                        break;
                    }
                default:
                    break;
            }
        },

        [&] (Inkscape::KeyReleaseEvent const &event) {
            switch (Inkscape::UI::Tools::get_latin_keyval(event)) {
                case GDK_KEY_Shift_L:
                case GDK_KEY_Shift_R: {
                    desktop->getCanvas()->set_cursor("grab");
                    break;
                }
                default:
                    break;
            }
        },

        [&] (Inkscape::CanvasEvent const &event) {}
    );

    return ret;
}

static constexpr bool DEBUG_TOOL_SWITCHER = false;

static std::unordered_map<Glib::ustring, Glib::ustring> name_to_tool;
static Glib::ustring last_name;
static Gdk::InputSource last_source = Gdk::InputSource::MOUSE;

static void init_extended()
{
    auto display = Gdk::Display::get_default();
    auto seat = display->get_default_seat();
    auto const devices = seat->get_devices(Gdk::Seat::Capabilities::ALL);
    
    for (auto const &dev : devices) {
        auto const src = dev->get_source();
        if (src == Gdk::InputSource::MOUSE) continue;

        auto const &name = dev->get_name();
        if (name.empty() || name == "pad") continue;

        auto const tool = dev->property_tool().get_value();
        if (!tool) continue;

        // Set the initial tool for the device.
        switch (tool->get_tool_type()) {
            case Gdk::DeviceTool::Type::PEN:
                name_to_tool[name] = "Calligraphic";
                break;
            case Gdk::DeviceTool::Type::ERASER:
                name_to_tool[name] = "Eraser";
                break;
            case Gdk::DeviceTool::Type::MOUSE:
                name_to_tool[name] = "Select";
                break;
            default:
                break;
        }
    }
}

// Switch tool based on device that generated event.
// For example, switch to Calligraphy or Eraser tool when using a Wacom tablet pen.
// Enabled in "Input" section of preferences dialog.
void snoop_extended(Inkscape::CanvasEvent const &event, SPDesktop *desktop)
{
    // Restrict to events we're interested in.
    switch (event.type()) {
        case EventType::MOTION:
        case EventType::BUTTON_PRESS:
        case EventType::BUTTON_RELEASE:
        case EventType::SCROLL:
            break; // Good
        default:
            return;
    }

    // Extract information about the device of the event.
    auto device = event.device.get();
    if (!device) {
        // Not all event structures include a GdkDevice field but the above should!
        std::cerr << "snoop_extended: missing source device! " << (int)event.type() << std::endl;
        return;
    }

    auto source = device->get_source();
    auto name = device->get_name();

    if (name.empty()) {
        std::cerr << "snoop_extended: name empty!" << std::endl;
        return;
    } else if (source == last_source && name == last_name) {
        // Device has not changed.
        return;
    }

    if constexpr (DEBUG_TOOL_SWITCHER) {
        std::cout << "Changed device: " << last_name << " -> " << name << std::endl;
    }

    // Save the tool currently selected for next time the device shows up.
    if (auto it = name_to_tool.find(last_name); it != name_to_tool.end()) {
        it->second = get_active_tool(desktop);
    }

    // Select the tool that was selected last time the device was seen.
    if (auto it = name_to_tool.find(name); it != name_to_tool.end()) {
        set_active_tool(desktop, it->second);
    }

    last_name = name;
    last_source = source;
}

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
