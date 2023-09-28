// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Gio::Actions for transforming the canvas view. Tied to a particular InkscapeWindow.
 *
 * Copyright (C) 2020 Tavmjong Bah
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 *
 */

#include <iostream>

#include <giomm.h>  // Not <gtkmm.h>! To eventually allow a headless version!
#include <glibmm/i18n.h>

#include <2geom/angle.h>  // rad_from_deg

#include "actions-canvas-transform.h"
#include "actions-helper.h"
#include "inkscape-application.h"
#include "inkscape-window.h"
#include "desktop.h"

#include "object/sp-namedview.h"
#include "page-manager.h"

#include "ui/tools/freehand-base.h" // SP_DRAW_CONTEXT
#include "ui/tools/pen-tool.h"
#include "ui/tools/pencil-tool.h"

#include "ui/widget/canvas.h" // Canvas area

enum {
    INK_CANVAS_ZOOM_IN,
    INK_CANVAS_ZOOM_OUT,
    INK_CANVAS_ZOOM_1_1,
    INK_CANVAS_ZOOM_1_2,
    INK_CANVAS_ZOOM_2_1,
    INK_CANVAS_ZOOM_SELECTION,
    INK_CANVAS_ZOOM_DRAWING,
    INK_CANVAS_ZOOM_PAGE,
    INK_CANVAS_ZOOM_PAGE_WIDTH,
    INK_CANVAS_ZOOM_CENTER_PAGE,
    INK_CANVAS_ZOOM_PREV,
    INK_CANVAS_ZOOM_NEXT,

    INK_CANVAS_ROTATE_CW,
    INK_CANVAS_ROTATE_CCW,
    INK_CANVAS_ROTATE_RESET,
    INK_CANVAS_FLIP_HORIZONTAL,
    INK_CANVAS_FLIP_VERTICAL,
    INK_CANVAS_FLIP_RESET
};

static void
canvas_zoom_helper(SPDesktop* dt, const Geom::Point& midpoint, double zoom_factor)
{
    if (auto const * const tool = dt->getTool();
        tool && (dynamic_cast<Inkscape::UI::Tools::PencilTool const *>(tool) ||
                 dynamic_cast<Inkscape::UI::Tools::PenTool    const *>(tool)))
    {
        // Zoom around end of unfinished path.
        auto const &freehand_base = dynamic_cast<Inkscape::UI::Tools::FreehandBase const &>(*tool);
        if (auto const zoom_to = freehand_base.red_curve_get_last_point()) {
            dt->zoom_relative(*zoom_to, zoom_factor);
            return;
        }
    }

    dt->zoom_relative(midpoint, zoom_factor, false);
}

void
canvas_transform(InkscapeWindow *win, const int& option)
{
    SPDesktop* dt = win->get_desktop();
    SPDocument *doc = dt->getDocument();

    // The following might be better done elsewhere:

    // Get preference dependent parameters
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    double zoom_inc =
        prefs->getDoubleLimited("/options/zoomincrement/value", M_SQRT2, 1.01, 10);
    double rotate_inc =
        prefs->getDoubleLimited("/options/rotateincrement/value", 15, 1, 90, "°");
    rotate_inc *= M_PI/180.0;

    // Get document dependent parameters
    Geom::Rect const canvas = dt->getCanvas()->get_area_world();
    Geom::Point midpoint = dt->w2d(canvas.midpoint());        // Midpoint of drawing on canvas.

    switch (option) {
        case INK_CANVAS_ZOOM_IN:
            canvas_zoom_helper( dt, midpoint,       zoom_inc);
            break;

        case INK_CANVAS_ZOOM_OUT:
            canvas_zoom_helper( dt, midpoint, 1.0 / zoom_inc); // zoom_inc > 1
            break;

        case INK_CANVAS_ZOOM_1_1:
            dt->zoom_realworld( midpoint, 1.0 );
            break;

        case INK_CANVAS_ZOOM_1_2:
            dt->zoom_realworld( midpoint, 0.5 );
            break;

        case INK_CANVAS_ZOOM_2_1:
            dt->zoom_realworld( midpoint, 2.0 );
            break;

        case INK_CANVAS_ZOOM_SELECTION:
            dt->zoom_selection();
            break;

        case INK_CANVAS_ZOOM_DRAWING:
            dt->zoom_drawing();
            break;

        case INK_CANVAS_ZOOM_PAGE:
            doc->getPageManager().zoomToSelectedPage(dt, false);
            break;

        case INK_CANVAS_ZOOM_PAGE_WIDTH:
            doc->getPageManager().zoomToSelectedPage(dt, true);
            break;

        case INK_CANVAS_ZOOM_CENTER_PAGE:
            doc->getPageManager().centerToSelectedPage(dt);
            break;

        case INK_CANVAS_ZOOM_PREV:
            dt->prev_transform();
            break;

        case INK_CANVAS_ZOOM_NEXT:
            dt->next_transform(); // Is this only zoom? Yes!
            break;

        case INK_CANVAS_ROTATE_CW:
            dt->rotate_relative_center_point (midpoint, rotate_inc);
            break;

        case INK_CANVAS_ROTATE_CCW:
            dt->rotate_relative_center_point (midpoint, -rotate_inc);
            break;

        case INK_CANVAS_ROTATE_RESET:
            dt->rotate_absolute_center_point (midpoint, 0);
            break;

        case INK_CANVAS_FLIP_HORIZONTAL:
            dt->flip_relative_center_point (midpoint, SPDesktop::FLIP_HORIZONTAL);
            break;

        case INK_CANVAS_FLIP_VERTICAL:
            dt->flip_relative_center_point (midpoint, SPDesktop::FLIP_VERTICAL);
            break;

        case INK_CANVAS_FLIP_RESET:
            dt->flip_absolute_center_point (midpoint, SPDesktop::FLIP_NONE);
            break;

        default:
            show_output("canvas_zoom: unhandled action value!");
    }
}

// Zoom to an arbitrary value
void
canvas_zoom_absolute(Glib::VariantBase const &value, InkscapeWindow *win)
{
    auto d = Glib::VariantBase::cast_dynamic<Glib::Variant<double> >(value);

    SPDesktop* dt = win->get_desktop();

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (prefs->getDouble("/options/zoomcorrection/shown", true)) {
        dt->zoom_realworld(dt->current_center(), d.get());
    } else {
        dt->zoom_absolute(dt->current_center(), d.get(), false);
    }
}

// Zoom a relative amount
void
canvas_zoom_relative(Glib::VariantBase const &value, InkscapeWindow *win)
{
    auto d = Glib::VariantBase::cast_dynamic<Glib::Variant<double> >(value);

    SPDesktop* dt = win->get_desktop();
    dt->zoom_relative(dt->current_center(), d.get());
}

// Rotate to an arbitrary value
void
canvas_rotate_absolute_radians(Glib::VariantBase const &value, InkscapeWindow *win)
{
    auto d = Glib::VariantBase::cast_dynamic<Glib::Variant<double> >(value);

    SPDesktop* dt = win->get_desktop();
    dt->rotate_absolute_center_point(dt->current_center(), d.get());
}

void
canvas_rotate_absolute_degrees(Glib::VariantBase const &value, InkscapeWindow *win)
{
    auto d = Glib::VariantBase::cast_dynamic<Glib::Variant<double> >(value);

    SPDesktop* dt = win->get_desktop();
    dt->rotate_absolute_center_point(dt->current_center(), Geom::rad_from_deg(d.get()));
}

// Rotate a relative amount
void
canvas_rotate_relative_radians(Glib::VariantBase const &value, InkscapeWindow *win)
{
    auto d = Glib::VariantBase::cast_dynamic<Glib::Variant<double> >(value);

    SPDesktop* dt = win->get_desktop();
    dt->rotate_relative_center_point(dt->current_center(), d.get());
}

void
canvas_rotate_relative_degrees(Glib::VariantBase const &value, InkscapeWindow *win)
{
    auto d = Glib::VariantBase::cast_dynamic<Glib::Variant<double> >(value);

    SPDesktop* dt = win->get_desktop();
    dt->rotate_relative_center_point(dt->current_center(), Geom::rad_from_deg(d.get()));
}

/**
 * Toggle rotate lock.
 */
void
canvas_rotate_lock(InkscapeWindow *win)
{
    auto action = win->lookup_action("canvas-rotate-lock");
    if (!action) {
        show_output("canvas_rotate_lock: action missing!");
        return;
    }

    auto saction = std::dynamic_pointer_cast<Gio::SimpleAction>(action);
    if (!saction) {
        show_output("canvas_rotate_lock: action not SimpleAction!");
        return;
    }

    bool state = false;
    saction->get_state(state);
    state = !state;
    saction->change_state(state);

    // Save value as a preference
    Inkscape::Preferences *pref = Inkscape::Preferences::get();
    pref->setBool("/options/rotationlock", state);

    SPDesktop* dt = win->get_desktop();
    dt->set_rotation_lock(state);
}

std::vector<std::vector<Glib::ustring>> raw_data_canvas_transform =
{
    // clang-format off
    {"win.canvas-zoom-in",            N_("Zoom In"),             "Canvas Geometry",  N_("Zoom in")                                    },
    {"win.canvas-zoom-out",           N_("Zoom Out"),            "Canvas Geometry",  N_("Zoom out")                                   },
    {"win.canvas-zoom-1-1",           N_("Zoom 1:1"),            "Canvas Geometry",  N_("Zoom to 1:1")                                },
    {"win.canvas-zoom-1-2",           N_("Zoom 1:2"),            "Canvas Geometry",  N_("Zoom to 1:2")                                },
    {"win.canvas-zoom-2-1",           N_("Zoom 2:1"),            "Canvas Geometry",  N_("Zoom to 2:1")                                },
    {"win.canvas-zoom-selection",     N_("Zoom Selection"),      "Canvas Geometry",  N_("Zoom to fit selection in window")            },
    {"win.canvas-zoom-drawing",       N_("Zoom Drawing"),        "Canvas Geometry",  N_("Zoom to fit drawing in window")              },
    {"win.canvas-zoom-page",          N_("Zoom Page"),           "Canvas Geometry",  N_("Zoom to fit page in window")                 },
    {"win.canvas-zoom-page-width",    N_("Zoom Page Width"),     "Canvas Geometry",  N_("Zoom to fit page width in window")           },
    {"win.canvas-zoom-center-page",   N_("Zoom Center Page"),    "Canvas Geometry",  N_("Center page in window")                      },
    {"win.canvas-zoom-prev",          N_("Zoom Prev"),           "Canvas Geometry",  N_("Go back to previous zoom (from the history of zooms)")},
    {"win.canvas-zoom-next",          N_("Zoom Next"),           "Canvas Geometry",  N_("Go to next zoom (from the history of zooms)")},

    {"win.canvas-rotate-cw",          N_("Rotate Clockwise"),    "Canvas Geometry",  N_("Rotate canvas clockwise")                    },
    {"win.canvas-rotate-ccw",         N_("Rotate Counter-CW"),   "Canvas Geometry",  N_("Rotate canvas counter-clockwise")            },
    {"win.canvas-rotate-reset",       N_("Reset Rotation"),      "Canvas Geometry",  N_("Reset canvas rotation")                      },

    {"win.canvas-flip-horizontal",    N_("Flip Horizontal"),     "Canvas Geometry",  N_("Flip canvas horizontally")                   },
    {"win.canvas-flip-vertical",      N_("Flip Vertical"),       "Canvas Geometry",  N_("Flip canvas vertically")                     },
    {"win.canvas-flip-reset",         N_("Reset Flipping"),      "Canvas Geometry",  N_("Reset canvas flipping")                      },

    {"win.canvas-zoom-absolute",      N_("Zoom Absolute"),       "Canvas Geometry",  N_("Zoom to an absolute value")                  },
    {"win.canvas-zoom-relative",      N_("Zoom Relative"),       "Canvas Geometry",  N_("Zoom by a relative amount")               },

    {"win.canvas-rotate-absolute-radians", N_("Rotate Absolute (Radians)"), "Canvas Geometry",  N_("Rotate to an absolute value (radians)")    },
    {"win.canvas-rotate-relative-radians", N_("Rotate Relative (Radians)"), "Canvas Geometry",  N_("Rotate by a relative amount (radians)") },
    {"win.canvas-rotate-absolute-degrees", N_("Rotate Absolute (Degrees)"), "Canvas Geometry",  N_("Rotate to an absolute value (degrees)")    },
    {"win.canvas-rotate-relative-degrees", N_("Rotate Relative (Degrees)"), "Canvas Geometry",  N_("Rotate by a relative amount (degrees)") },

    {"win.canvas-rotate-lock",        N_("Lock Rotation"),       "Canvas Geometry",  N_("Lock canvas rotation")                       },
    // clang-format on
};

void
add_actions_canvas_transform(InkscapeWindow* win)
{
    auto prefs = Inkscape::Preferences::get();

    bool rotate_lock = prefs->getBool("/options/rotationlock");

    SPDesktop* dt = win->get_desktop();
    if (dt) {
        dt->set_rotation_lock(rotate_lock);
    } else {
        show_output("add_actions_canvas_transform: no desktop!");
    }

    Glib::VariantType Double(Glib::VARIANT_TYPE_DOUBLE);

    // clang-format off
    win->add_action( "canvas-zoom-in",         sigc::bind(sigc::ptr_fun(&canvas_transform), win, INK_CANVAS_ZOOM_IN));
    win->add_action( "canvas-zoom-out",        sigc::bind(sigc::ptr_fun(&canvas_transform), win, INK_CANVAS_ZOOM_OUT));
    win->add_action( "canvas-zoom-1-1",        sigc::bind(sigc::ptr_fun(&canvas_transform), win, INK_CANVAS_ZOOM_1_1));
    win->add_action( "canvas-zoom-1-2",        sigc::bind(sigc::ptr_fun(&canvas_transform), win, INK_CANVAS_ZOOM_1_2));
    win->add_action( "canvas-zoom-2-1",        sigc::bind(sigc::ptr_fun(&canvas_transform), win, INK_CANVAS_ZOOM_2_1));
    win->add_action( "canvas-zoom-selection",  sigc::bind(sigc::ptr_fun(&canvas_transform), win, INK_CANVAS_ZOOM_SELECTION));
    win->add_action( "canvas-zoom-drawing",    sigc::bind(sigc::ptr_fun(&canvas_transform), win, INK_CANVAS_ZOOM_DRAWING));
    win->add_action( "canvas-zoom-page",       sigc::bind(sigc::ptr_fun(&canvas_transform), win, INK_CANVAS_ZOOM_PAGE));
    win->add_action( "canvas-zoom-page-width", sigc::bind(sigc::ptr_fun(&canvas_transform), win, INK_CANVAS_ZOOM_PAGE_WIDTH));
    win->add_action( "canvas-zoom-center-page",sigc::bind(sigc::ptr_fun(&canvas_transform), win, INK_CANVAS_ZOOM_CENTER_PAGE));
    win->add_action( "canvas-zoom-prev",       sigc::bind(sigc::ptr_fun(&canvas_transform), win, INK_CANVAS_ZOOM_PREV));
    win->add_action( "canvas-zoom-next",       sigc::bind(sigc::ptr_fun(&canvas_transform), win, INK_CANVAS_ZOOM_NEXT));

    win->add_action( "canvas-rotate-cw",       sigc::bind(sigc::ptr_fun(&canvas_transform), win, INK_CANVAS_ROTATE_CW));
    win->add_action( "canvas-rotate-ccw",      sigc::bind(sigc::ptr_fun(&canvas_transform), win, INK_CANVAS_ROTATE_CCW));
    win->add_action( "canvas-rotate-reset",    sigc::bind(sigc::ptr_fun(&canvas_transform), win, INK_CANVAS_ROTATE_RESET));

    win->add_action( "canvas-flip-horizontal", sigc::bind(sigc::ptr_fun(&canvas_transform), win, INK_CANVAS_FLIP_HORIZONTAL));
    win->add_action( "canvas-flip-vertical",   sigc::bind(sigc::ptr_fun(&canvas_transform), win, INK_CANVAS_FLIP_VERTICAL));
    win->add_action( "canvas-flip-reset",      sigc::bind(sigc::ptr_fun(&canvas_transform), win, INK_CANVAS_FLIP_RESET));

    win->add_action_with_parameter( "canvas-zoom-absolute",   Double, sigc::bind(sigc::ptr_fun(&canvas_zoom_absolute),   win));
    win->add_action_with_parameter( "canvas-zoom-relative",   Double, sigc::bind(sigc::ptr_fun(&canvas_zoom_relative),   win));

    win->add_action_with_parameter( "canvas-rotate-absolute-radians", Double, sigc::bind(sigc::ptr_fun(&canvas_rotate_absolute_radians), win));
    win->add_action_with_parameter( "canvas-rotate-relative-radians", Double, sigc::bind(sigc::ptr_fun(&canvas_rotate_relative_radians), win));
    win->add_action_with_parameter( "canvas-rotate-absolute-degrees", Double, sigc::bind(sigc::ptr_fun(&canvas_rotate_absolute_degrees), win));
    win->add_action_with_parameter( "canvas-rotate-relative-degrees", Double, sigc::bind(sigc::ptr_fun(&canvas_rotate_relative_degrees), win));

    win->add_action_bool( "canvas-rotate-lock",sigc::bind(sigc::ptr_fun(&canvas_rotate_lock),    win), rotate_lock);
    // clang-format on

    auto app = InkscapeApplication::instance();
    if (!app) {
        show_output("add_actions_canvas_transform: no app!");
        return;
    }
    app->get_action_extra_data().add_data(raw_data_canvas_transform);
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
