// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Gio::Actions for use with <a> (for anchor or hyper link).
 *
 * Copyright (C) 2022 Tavmjong Bah
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 *
 */

#include "actions-element-a.h"
#include "actions-helper.h"

#include <iostream>

#include <giomm.h>  // Not <gtkmm.h>! To eventually allow a headless version!
#include <glibmm/i18n.h>

#include "inkscape-application.h"
#include "inkscape-window.h"
#include "preferences.h"

#include "selection.h"            // Selection
#include "object/sp-anchor.h"
#include "ui/dialog/dialog-container.h"

// void anchor_add(InkscapeApplication *app)
// {
//     auto selection = app->get_active_selection();
//     auto anchor = selection->group(1);
//     selection->set(anchor);

//     if (app->get_active_window()) {
//         app->get_active_window()->get_desktop()->getContainer()->new_dialog("ObjectAttributes");
//     }
// }

// XML not modified. Requires GUI.
void anchor_open_link(InkscapeApplication* app)
{
    // Todo: Remove no-longer necessary window check.
    auto window = app->get_active_window();
    if (window) {
        auto selection = app->get_active_selection();
        for (auto item : selection->items()) {
            auto anchor = cast<SPAnchor>(item);
            if (anchor) {
                const char* href = anchor->href;
                if (href) {
                    try {
                        Gio::AppInfo::launch_default_for_uri(href);
                    } catch (const Glib::Error &e) {
                        show_output(Glib::ustring("anchor_open_link: cannot open ") + href + " " + e.what());
                    }
                }
            }
        }
    }
}

std::vector<std::vector<Glib::ustring>> raw_data_element_a =
{
    // clang-format off
    {"app.element-a-open-link",          N_("Open link"),   "Anchor",    N_("Add an anchor to an object.") },
    // clang-format on
};

void
add_actions_element_a(InkscapeApplication* app)
{
    auto *gapp = app->gio_app();

    // clang-format off
    gapp->add_action(                "element-a-open-link",          sigc::bind(sigc::ptr_fun(&anchor_open_link),      app));
    // clang-format on

    app->get_action_extra_data().add_data(raw_data_element_a);
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
