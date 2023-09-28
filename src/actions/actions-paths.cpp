// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Gio::Actions for Path Actions
 *
 * Copyright (C) 2021 Sushant A.A.
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 *
 */

#include "actions-paths.h"

#include <iostream>

#include <giomm.h>  // Not <gtkmm.h>! To eventually allow a headless version!
#include <glibmm/i18n.h>

#include "actions-tools.h"
#include "desktop.h"
#include "document-undo.h"
#include "inkscape-application.h"
#include "inkscape-window.h"
#include "selection.h"            // Selection
#include "selection-chemistry.h"  // SelectionHelper
#include "path/path-offset.h"
#include "ui/icon-names.h"
#include "ui/tools/booleans-builder.h"

void
object_path_union(InkscapeApplication *app)
{
    auto selection = app->get_active_selection();
    selection->removeLPESRecursive(true);
    selection->unlinkRecursive(true);
    selection->pathUnion();
}

void
select_path_difference(InkscapeApplication *app)
{
    auto selection = app->get_active_selection();
    selection->removeLPESRecursive(true);
    selection->unlinkRecursive(true);
    selection->pathDiff();
}

void
select_path_intersection(InkscapeApplication *app)
{
    auto selection = app->get_active_selection();
    selection->removeLPESRecursive(true);
    selection->unlinkRecursive(true);
    selection->pathIntersect();
}

void
select_path_exclusion(InkscapeApplication *app)
{
    auto selection = app->get_active_selection();
    selection->removeLPESRecursive(true);
    selection->unlinkRecursive(true);
    selection->pathSymDiff();
}

void
select_path_division(InkscapeApplication *app)
{
    auto selection = app->get_active_selection();
    selection->removeLPESRecursive(true);
    selection->unlinkRecursive(true);
    selection->pathCut();
}

void
select_path_cut(InkscapeApplication *app)
{
    auto selection = app->get_active_selection();
    selection->removeLPESRecursive(true);
    selection->unlinkRecursive(true);
    selection->pathSlice();
}

void
select_path_combine(InkscapeApplication *app)
{
    auto selection = app->get_active_selection();
    selection->unlinkRecursive(true);
    selection->combine();
}

void
select_path_break_apart(InkscapeApplication *app)
{
    auto selection = app->get_active_selection();
    selection->breakApart();
}

void
select_path_split(InkscapeApplication *app)
{
    auto selection = app->get_active_selection();
    selection->breakApart(false, false);
}

void
select_path_fracture(InkscapeApplication *app)
{
    auto selection = app->get_active_selection();
    auto boolean_builder = Inkscape::BooleanBuilder(selection);
    selection->setList(boolean_builder.shape_commit(true, true));
    Inkscape::DocumentUndo::done(selection->document(), "Fracture", INKSCAPE_ICON("path-fracture"));
}

void select_path_flatten(InkscapeApplication *app)
{
    auto selection = app->get_active_selection();
    selection->strokesToPaths(false, true);
    auto boolean_builder = Inkscape::BooleanBuilder(selection, true);
    selection->setList(boolean_builder.shape_commit(true, true));
    Inkscape::DocumentUndo::done(selection->document(), "Flatten", INKSCAPE_ICON("path-flatten"));
}

void
fill_between_paths(InkscapeApplication *app)
{
    auto selection = app->get_active_selection();
    selection->fillBetweenMany();
}

void
select_path_simplify(InkscapeApplication *app)
{
    auto selection = app->get_active_selection();
    selection->simplifyPaths();
}

void
select_path_inset(InkscapeWindow* win)
{
    SPDesktop* dt = win->get_desktop();

    // Inset selected paths
    dt->getSelection()->removeLPESRecursive(true);
    dt->getSelection()->unlinkRecursive(true);
    sp_selected_path_inset(dt);
}

void
select_path_offset(InkscapeWindow* win)
{
    SPDesktop* dt = win->get_desktop();

    // Offset selected paths
    dt->getSelection()->removeLPESRecursive(true);
    dt->getSelection()->unlinkRecursive(true);
    sp_selected_path_offset(dt);
}

void
select_path_inset_screen(const Glib::VariantBase& value, InkscapeWindow *win)
{
    Glib::Variant<double> d = Glib::VariantBase::cast_dynamic<Glib::Variant<double> >(value);

    SPDesktop* dt = win->get_desktop();

    // Offset selected paths
    dt->getSelection()->removeLPESRecursive(true);
    dt->getSelection()->unlinkRecursive(true);
    sp_selected_path_inset_screen(dt, d.get());
}

void
select_path_offset_screen(const Glib::VariantBase& value, InkscapeWindow *win)
{
    Glib::Variant<double> d = Glib::VariantBase::cast_dynamic<Glib::Variant<double> >(value);

    SPDesktop* dt = win->get_desktop();

    // Offset selected paths
    dt->getSelection()->removeLPESRecursive(true);
    dt->getSelection()->unlinkRecursive(true);
    sp_selected_path_offset_screen(dt, d.get());
}

void
select_path_offset_dynamic(InkscapeWindow* win)
{
    SPDesktop* dt = win->get_desktop();

    // Dynamic Offset
    dt->getSelection()->removeLPESRecursive(true);
    dt->getSelection()->unlinkRecursive(true);
    sp_selected_path_create_offset_object_zero(dt);
    set_active_tool(dt, "Node");
}

void
select_path_offset_linked(InkscapeWindow* win)
{
    SPDesktop* dt = win->get_desktop();

    // Linked Offset
    dt->getSelection()->removeLPESRecursive(true);
    dt->getSelection()->unlinkRecursive(true);
    sp_selected_path_create_updating_offset_object_zero(dt);
    set_active_tool(dt, "Node");
}

void
select_path_reverse(InkscapeWindow* win)
{
    SPDesktop* dt = win->get_desktop();

    // Reverse
    Inkscape::SelectionHelper::reverse(dt);
}

void
shape_builder_mode(int value, InkscapeWindow* win)
{
    Inkscape::Preferences *pref = Inkscape::Preferences::get();

    auto action = win->lookup_action("shape-builder-mode");
    auto saction = std::dynamic_pointer_cast<Gio::SimpleAction>(action);
    saction->change_state((int)value);
    pref->setInt("/tools/booleans/mode", value);
}

void
shape_builder_replace(InkscapeWindow* win)
{
    Inkscape::Preferences *pref = Inkscape::Preferences::get();
    if (auto action = win->lookup_action("shape-builder-replace")) {
        bool active = false;
        action->get_state(active);
        active = !active; // toggle
        action->change_state(active);
        pref->setBool("/tools/booleans/replace", active);
    }

}


static const std::vector<std::vector<Glib::ustring>> raw_data_path =
{
    // clang-format offs
    {"app.path-union",               N_("Union"),                "Path",   N_("Create union of selected paths")},
    {"app.path-difference",          N_("Difference"),           "Path",   N_("Create difference of selected paths (bottom minus top)")},
    {"app.path-intersection",        N_("Intersection"),         "Path",   N_("Create intersection of selected paths")},
    {"app.path-exclusion",           N_("Exclusion"),            "Path",   N_("Create exclusive OR of selected paths (those parts that belong to only one path)")},
    {"app.path-division",            N_("Division"),             "Path",   N_("Cut the bottom path into pieces")},
    {"app.path-cut",                 N_("Cut Path"),             "Path",   N_("Cut the bottom path's stroke into pieces, removing fill")},
    {"app.path-combine",             N_("Combine"),              "Path",   N_("Combine several paths into one")},
    {"app.path-break-apart",         N_("Break Apart"),          "Path",   N_("Break selected paths into subpaths")},
    {"app.path-split",               N_("Split Apart"),          "Path",   N_("Split selected paths into non-overlapping sections")},
    {"app.path-fracture",            N_("Fracture"),             "Path",   N_("Fracture one or more overlapping objects into all possible segments")},
    {"app.path-flatten",             NC_("Path flatten", "Flatten"), "Path", N_("Flatten one or more overlapping objects into their visible parts")},
    {"app.path-fill-between-paths",  N_("Fill between paths"),   "Path",   N_("Create a fill object using the selected paths")},
    {"app.path-simplify",            N_("Simplify"),             "Path",   N_("Simplify selected paths (remove extra nodes)")},

    {"win.path-inset",               N_("Inset"),                "Path",   N_("Inset selected paths")},
    {"win.path-offset",              N_("Offset"),               "Path",   N_("Offset selected paths")},
    {"win.path-offset-dynamic",      N_("Dynamic Offset"),       "Path",   N_("Create a dynamic offset object")},
    {"win.path-offset-linked",       N_("Linked Offset"),        "Path",   N_("Create a dynamic offset object linked to the original path")},
    {"win.path-reverse",             N_("Reverse"),              "Path",   N_("Reverse the direction of selected paths (useful for flipping markers)")},
    {"win.path-inset-screen",        N_("Inset Screen"),         "Path",   N_("Inset selected paths by screen pixels")},
    {"win.path-offset-screen",       N_("Offset Screen"),        "Path",   N_("Offset selected paths by screen pixels")},

    {"win.shape-builder-mode(0)",    N_("Shape Builder: Add"),   "Path",   N_("Add shapes by clicking or clicking and dragging")},
    {"win.shape-builder-mode(1)",    N_("Shape Builder: Delete"),"Path",   N_("Remove shapes by clicking or clicking and dragging")},
    {"win.shape-builder-replace",    N_("Replace Objects"), "Path", N_("Remove selected objects when shape building is completed")},

    // clang-format on
};

void add_actions_path(InkscapeApplication *app)
{
    auto *gapp = app->gio_app();

    // clang-format off
    gapp->add_action(               "path-union",              sigc::bind(sigc::ptr_fun(&object_path_union),         app));
    gapp->add_action(               "path-difference",         sigc::bind(sigc::ptr_fun(&select_path_difference),    app));
    gapp->add_action(               "path-intersection",       sigc::bind(sigc::ptr_fun(&select_path_intersection),  app));
    gapp->add_action(               "path-exclusion",          sigc::bind(sigc::ptr_fun(&select_path_exclusion),     app));
    gapp->add_action(               "path-division",           sigc::bind(sigc::ptr_fun(&select_path_division),      app));
    gapp->add_action(               "path-cut",                sigc::bind(sigc::ptr_fun(&select_path_cut),           app));
    gapp->add_action(               "path-combine",            sigc::bind(sigc::ptr_fun(&select_path_combine),       app));
    gapp->add_action(               "path-break-apart",        sigc::bind(sigc::ptr_fun(&select_path_break_apart),   app));
    gapp->add_action(               "path-split",              sigc::bind(sigc::ptr_fun(&select_path_split),         app));
    gapp->add_action(               "path-fracture",           sigc::bind(sigc::ptr_fun(&select_path_fracture),      app));
    gapp->add_action(               "path-flatten",            sigc::bind(sigc::ptr_fun(&select_path_flatten) ,      app));
    gapp->add_action(               "path-fill-between-paths", sigc::bind(sigc::ptr_fun(&fill_between_paths),        app));
    gapp->add_action(               "path-simplify",           sigc::bind(sigc::ptr_fun(&select_path_simplify),      app));
    // clang-format on

    app->get_action_extra_data().add_data(raw_data_path);
}

// TODO: Remove desktop dependencies and convert to app actions.
void add_actions_path(InkscapeWindow *win)
{
    auto const Double = Glib::VariantType(Glib::VARIANT_TYPE_DOUBLE);

    auto prefs = Inkscape::Preferences::get();
    int current_mode = prefs->getInt("/tool/booleans/mode", 0);
    bool replace = prefs->getBool("/tool/booleans/replace", true);

    // clang-format off
    win->add_action(                "path-inset",                   sigc::bind(sigc::ptr_fun(&select_path_inset),          win));
    win->add_action(                "path-offset",                  sigc::bind(sigc::ptr_fun(&select_path_offset),         win));
    win->add_action_with_parameter( "path-inset-screen",    Double, sigc::bind(sigc::ptr_fun(&select_path_inset_screen),   win));
    win->add_action_with_parameter( "path-offset-screen",   Double, sigc::bind(sigc::ptr_fun(&select_path_offset_screen),  win));
    win->add_action(                "path-offset-dynamic",          sigc::bind(sigc::ptr_fun(&select_path_offset_dynamic), win));
    win->add_action(                "path-offset-linked",           sigc::bind(sigc::ptr_fun(&select_path_offset_linked),  win));
    win->add_action(                "path-reverse",                 sigc::bind(sigc::ptr_fun(&select_path_reverse),        win));
    win->add_action_radio_integer(  "shape-builder-mode",           sigc::bind(sigc::ptr_fun(&shape_builder_mode),         win), current_mode);
    win->add_action_bool(           "shape-builder-replace",        sigc::bind(sigc::ptr_fun(&shape_builder_replace),      win), replace);
    // clang-format on
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
