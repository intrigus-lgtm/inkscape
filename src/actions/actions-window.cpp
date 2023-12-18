// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Gio::Actions for window handling tied to the application and with GUI.
 *
 * Copyright (C) 2020 Tavmjong Bah
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 *
 */

#include "actions-window.h"

#include <giomm.h>  // Not <gtkmm.h>! To eventually allow a headless version!
#include <glibmm/i18n.h>

#include "actions-helper.h"
#include "desktop.h"
#include "document.h"
#include "inkscape-application.h"
#include "inkscape-window.h"
#include "util-string/ustring-format.h"

// Actions for window handling (should be integrated with file dialog).

// Open a window for current document
void
window_open(InkscapeApplication *app)
{
    SPDocument *document = app->get_active_document();
    if (document) {
        InkscapeWindow* window = app->get_active_window();
        if (window && window->get_document() && window->get_document()->getVirgin()) {
            // We have a window with an untouched template document, use this window.
            app->document_swap (window, document);
        } else {
            app->window_open(document);
        }
    } else {
        show_output("window_open(): failed to find document!");
    }
}

void
window_query_geometry(InkscapeApplication *app)
{
    auto window = app->get_active_window();
    if (window) {
        SPDesktop *desktop = window->get_desktop();
        if (desktop) {
            gint x, y, w, h = 0;
            desktop->getWindowGeometry(x, y, w, h);
            show_output(Glib::ustring("x:") + Inkscape::ustring::format_classic(x), false);
            show_output(Glib::ustring("y:") + Inkscape::ustring::format_classic(y), false);
            show_output(Glib::ustring("w:") + Inkscape::ustring::format_classic(w), false);
            show_output(Glib::ustring("h:") + Inkscape::ustring::format_classic(h), false);
        }
    } else {
        show_output("this action needs active window, probably you need to add --active-window / -q");
    }
}

void 
window_set_geometry(const Glib::VariantBase& value, InkscapeApplication *app)
{
    Glib::Variant<Glib::ustring> s = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring> >(value);

    std::vector<Glib::ustring> tokens = Glib::Regex::split_simple(",", s.get());
    if (tokens.size() != 4) {
        show_output("action:set geometry: requires 'x, y, width, height'");
        return;
    }
    auto window = app->get_active_window();
    if (window) {
        SPDesktop *desktop = window->get_desktop();
        if (desktop) {
            if (desktop->is_maximized()) {
                gtk_window_unmaximize(desktop->getToplevel()->gobj());
            }
            gint x = std::stoi(tokens[0]);
            gint y = std::stoi(tokens[1]);
            gint w = std::stoi(tokens[2]);
            gint h = std::stoi(tokens[3]);
            desktop->setWindowSize (w, h);
            desktop->setWindowPosition(Geom::Point(x,y));
        }
    } else {
        show_output("this action needs active window, probably you need to add --active-window / -q");
    }
}

void
window_close(InkscapeApplication *app)
{
    app->window_close_active();
}

std::vector<std::vector<Glib::ustring>> hint_data_window =
{
    // clang-format off
    {"app.window-set-geometry",         N_("Enter comma-separated string for x, y, width, height")  }
    // clang-format on
};

std::vector<std::vector<Glib::ustring>> raw_data_window =
{
    // clang-format off
    {"app.window-open",           N_("Window Open"),     "Window",     N_("Open a window for the active document; GUI only")       },
    {"app.window-close",           N_("Window Close"),           "Window",     N_("Close the active window, does not check for data loss") },
    {"app.window-query-geometry",  N_("Window Query Geometry"),  "Window",     N_("Query the active window's location and size") },
    {"app.window-set-geometry",    N_("Window Set Geometry"),    "Window",     N_("Set the active window's location and size (x, y, width, height)") },
    {"app.window-crash",           N_("Force Crash"),            "Window",     N_("Force Inkscape to crash, useful for testing.") },
    // clang-format on
};

void
add_actions_window(InkscapeApplication* app)
{
    auto *gapp = app->gio_app();
    Glib::VariantType String(Glib::VARIANT_TYPE_STRING);
    // clang-format off
    gapp->add_action(                "window-open",  sigc::bind(sigc::ptr_fun(&window_open),         app));
    gapp->add_action(                "window-close", sigc::bind(sigc::ptr_fun(&window_close),        app));
    gapp->add_action(                "window-query-geometry",  sigc::bind(sigc::ptr_fun(&window_query_geometry),       app));
    gapp->add_action_with_parameter( "window-set-geometry",    String, sigc::bind(sigc::ptr_fun(&window_set_geometry), app));
    gapp->add_action("window-crash", [=](){
        abort();
    });
    // clang-format on

    app->get_action_extra_data().add_data(raw_data_window);
    app->get_action_hint_data().add_data(hint_data_window);
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
