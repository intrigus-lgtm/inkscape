// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Gio::Actions tied to the application and independent of GUI.
 *
 * Copyright (C) 2018 Tavmjong Bah
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 *
 */

#include "actions-base.h"

#include <iostream>

#include <giomm.h>  // Not <gtkmm.h>! To eventually allow a headless version!
#include <glibmm/i18n.h>

#include "actions-helper.h"

#include "inkscape-application.h"

#include "document.h"             // SPDocument
#include "file.h"                 // dpi convert method
#include "inkscape-version-info.h"// Inkscape version
#include "inkscape.h"             // Inkscape::Application
#include "path-prefix.h"          // Extension directory
#include "selection.h"            // Selection

#include "actions/actions-extra-data.h"
#include "util-string/ustring-format.h"
#include "io/resource.h"
#include "object/sp-root.h"       // query_all()

void
print_inkscape_version()
{
    show_output(Inkscape::inkscape_version(), false);
}

void
active_window_start() {
    active_window_start_helper();
}

void
active_window_end() {
    active_window_end_helper();
}

void
print_debug_info()
{
    show_output(Inkscape::debug_info(), false);
}

void
print_system_data_directory()
{
    show_output(Glib::build_filename(get_inkscape_datadir(), "inkscape"), false);
}

void
print_user_data_directory()
{
    show_output(Inkscape::IO::Resource::profile_path(), false);
}

// Helper function for query_x(), query_y(), query_width(), and query_height().
void
query_dimension(InkscapeApplication* app, bool extent, Geom::Dim2 const axis)
{
    SPDocument* document = nullptr;
    Inkscape::Selection* selection = nullptr;
    if (!get_document_and_selection(app, &document, &selection)) {
        return;
    }

    if (selection->isEmpty()) {
        selection->add(document->getRoot());
    }

    bool first = true;
    auto items = selection->items();
    Glib::ustring out = "";
    for (auto item : items) {
        if (!first) {
            out += ",";
        }
        first = false;
        Geom::OptRect area = item->documentVisualBounds();
        if (area) {
            if (extent) {
                out += Inkscape::ustring::format_classic(area->dimensions()[axis]);
            } else {
                out += Inkscape::ustring::format_classic(area->min()[axis]);
            }
        } else {
            out += "0";
        }
    }
    show_output(out, false);
}

void
query_x(InkscapeApplication* app)
{
    query_dimension(app, false, Geom::X);
}

void
query_y(InkscapeApplication* app)
{
    query_dimension(app, false, Geom::Y);
}

void
query_width(InkscapeApplication* app)
{
    query_dimension(app, true, Geom::X);
}

void
query_height(InkscapeApplication* app)
{
    query_dimension(app, true, Geom::Y);
}

// Helper for query_all()
void
query_all_recurse (SPObject *o)
{
    auto item = cast<SPItem>(o);
    if (item && item->getId()) {
        Geom::OptRect area = item->documentVisualBounds();
        Glib::ustring out = "";
        if (area) {
            // clang-format off
            out += Glib::ustring(item->getId()) + ",";
            out += Inkscape::ustring::format_classic(area->min()[Geom::X]) + ",";
            out += Inkscape::ustring::format_classic(area->min()[Geom::Y]) + ",";
            out += Inkscape::ustring::format_classic(area->dimensions()[Geom::X]) + ",";
            out += Inkscape::ustring::format_classic(area->dimensions()[Geom::Y]);
            // clang-format on
        }
        show_output(out, false);
        for (auto& child: o->children) {
            query_all_recurse (&child);
        }
    }
}

void
query_all(InkscapeApplication* app)
{
    SPDocument* doc = app->get_active_document();
    if (!doc) {
        show_output("query_all: no document!");
        return;
    }

    SPObject *o = doc->getRoot();
    if (o) {
        query_all_recurse(o);
    }
}

void
pdf_page(int page)
{
    INKSCAPE.set_pages(std::to_string(page));
}

void
convert_dpi_method(Glib::ustring method)
{
    if (method == "none") {
        sp_file_convert_dpi_method_commandline = FILE_DPI_UNCHANGED;
    } else if (method == "scale-viewbox") {
        sp_file_convert_dpi_method_commandline = FILE_DPI_VIEWBOX_SCALED;
    } else if (method == "scale-document") {
        sp_file_convert_dpi_method_commandline = FILE_DPI_DOCUMENT_SCALED;
    } else {
        show_output("dpi_convert_method: invalid option");
    }
}

void
no_convert_baseline()
{
    sp_no_convert_text_baseline_spacing = true;
}

std::vector<std::vector<Glib::ustring>> raw_data_base =
{
    // clang-format off
    {"app.inkscape-version",          N_("Inkscape Version"),        "Base",       N_("Print Inkscape version and exit")                   },
    {"app.active-window-start",       N_("Active Window: Start Call"), "Base",     N_("Start execution in active window")                          },
    {"app.active-window-end",         N_("Active Window: End Call"), "Base",       N_("End execution in active window")                            },
    {"app.debug-info",                N_("Debug Info"),              "Base",       N_("Print debugging information and exit")              },
    {"app.system-data-directory",     N_("System Directory"),        "Base",       N_("Print system data directory and exit")              },
    {"app.user-data-directory",       N_("User Directory"),          "Base",       N_("Print user data directory and exit")                },
    {"app.action-list",               N_("List Actions"),            "Base",       N_("Print a list of actions and exit")                  },
    {"app.list-input-types",          N_("List Input File Extensions"), "Base",    N_("Print a list of input file extensions and exit")    },
    {"app.quit",                      N_("Quit"),                    "Base",       N_("Quit Inkscape, check for data loss")                },
    {"app.quit-immediate",            N_("Quit Immediately"),        "Base",       N_("Immediately quit Inkscape, no check for data loss") },

    {"app.open-page",                 N_("Import Page Number"),      "Import",     N_("Select PDF page number to import")                  },
    {"app.convert-dpi-method",        N_("Import DPI Method"),       "Import",     N_("Set DPI conversion method for legacy Inkscape files")},
    {"app.no-convert-baseline",       N_("No Import Baseline Conversion"), "Import", N_("Do not convert text baselines in legacy Inkscape files")},

    {"app.query-x",                   N_("Query X"),                 "Query",      N_("Query 'x' value(s) of selected objects")            },
    {"app.query-y",                   N_("Query Y"),                 "Query",      N_("Query 'y' value(s) of selected objects")            },
    {"app.query-width",               N_("Query Width"),             "Query",      N_("Query 'width' value(s) of object(s)")               },
    {"app.query-height",              N_("Query Height"),            "Query",      N_("Query 'height' value(s) of object(s)")              },
    {"app.query-all",                 N_("Query All"),               "Query",      N_("Query 'x', 'y', 'width', and 'height'")             }
    // clang-format on
};

void
add_actions_base(InkscapeApplication* app)
{
    auto *gapp = app->gio_app();
    // Note: "radio" actions are just an easy way to set type without using templating.
    // clang-format off
    gapp->add_action(               "inkscape-version",                                    sigc::ptr_fun(&print_inkscape_version)                 );
    gapp->add_action(               "active-window-start",                                 sigc::ptr_fun(&active_window_start)                    );
    gapp->add_action(               "active-window-end",                                   sigc::ptr_fun(&active_window_end)                      );
    gapp->add_action(               "debug-info",                                          sigc::ptr_fun(&print_debug_info)                       );
    gapp->add_action(               "system-data-directory",                               sigc::ptr_fun(&print_system_data_directory)            );
    gapp->add_action(               "user-data-directory",                                 sigc::ptr_fun(&print_user_data_directory)              );
    gapp->add_action(               "action-list",        sigc::mem_fun(*app, &InkscapeApplication::print_action_list)                            );
    gapp->add_action(               "list-input-types",   sigc::mem_fun(*app, &InkscapeApplication::print_input_type_list)                        );
    gapp->add_action(               "quit",               sigc::mem_fun(*app, &InkscapeApplication::on_quit)                                      );
    gapp->add_action(               "quit-immediate",     sigc::mem_fun(*app, &InkscapeApplication::on_quit_immediate)                            );

    gapp->add_action_radio_integer( "open-page",                                           sigc::ptr_fun(&pdf_page),                             0);
    gapp->add_action_radio_string(  "convert-dpi-method",                                  sigc::ptr_fun(&convert_dpi_method),              "none");
    gapp->add_action(               "no-convert-baseline",                                 sigc::ptr_fun(&no_convert_baseline)                    );


    gapp->add_action(               "query-x",            sigc::bind(sigc::ptr_fun(&query_x),                   app)        );
    gapp->add_action(               "query-y",            sigc::bind(sigc::ptr_fun(&query_y),                   app)        );
    gapp->add_action(               "query-width",        sigc::bind(sigc::ptr_fun(&query_width),               app)        );
    gapp->add_action(               "query-height",       sigc::bind(sigc::ptr_fun(&query_height),              app)        );
    gapp->add_action(               "query-all",          sigc::bind(sigc::ptr_fun(&query_all),                 app)        );
    // clang-format on

    // Revision string is going to be added to the actions interface so it can be queried for existance by GApplication
    gapp->add_action(Inkscape::inkscape_revision(), [=]() { g_warning("Don't call this action"); });

    app->get_action_extra_data().add_data(raw_data_base);
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
