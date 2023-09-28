// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 *
 *  Actions for Undo/Redo tied to document.
 *
 * Authors:
 *   Tavmjong Bah
 *
 * Copyright (C) 2021 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <giomm.h>
#include <glibmm/i18n.h>

#include "actions-undo-document.h"
#include "actions-helper.h"

#include "document.h"
#include "document-undo.h"
#include "inkscape-application.h"

// ifdef out for headless operation!
#include "desktop.h"
#include "inkscape-window.h"
#include "ui/tools/tool-base.h"
#include "ui/widget/canvas.h"

void
undo(SPDocument* document)
{
    auto app = InkscapeApplication::instance();
    auto win = app->get_active_window();

    // Undo can be used in headless mode.
    if (win) {
        auto desktop = win->get_desktop();
        auto const tool = desktop->getTool();

        // No undo while dragging, or if the tool handled this undo.
        if (desktop->getCanvas()->is_dragging() || (tool && tool->catch_undo())) {
            return;
        }
    }

    Inkscape::DocumentUndo::undo(document);
}

void
redo(SPDocument* document)
{
    auto app = InkscapeApplication::instance();
    auto win = app->get_active_window();

    // Redo can be used in headless mode.
    if (win) {
        auto desktop = win->get_desktop();
        auto const tool = desktop->getTool();

        // No redo while dragging, or if the tool handled this redo
        if (desktop->getCanvas()->is_dragging() || (tool && tool->catch_undo(true))) {
            return;
        }
    }

    Inkscape::DocumentUndo::redo(document);
}

void
enable_undo_actions(SPDocument* document, bool undo, bool redo)
{
    auto group = document->getActionGroup();
    if (!group)
        return;
    auto undo_action = group->lookup_action("undo");
    auto redo_action = group->lookup_action("redo");
    auto undo_saction = std::dynamic_pointer_cast<Gio::SimpleAction>(undo_action);
    auto redo_saction = std::dynamic_pointer_cast<Gio::SimpleAction>(redo_action);
    // GTK4
    // auto undo_saction = dynamic_cast<Gio::SimpleAction*>(undo_action);
    // auto redo_saction = dynamic_cast<Gio::SimpleAction*>(redo_action);
    if (!undo_saction || !redo_saction) {
        show_output("UndoActions: can't find undo or redo action!");
        return;
    }
    // Enable/disable menu items.
    undo_saction->set_enabled(undo);
    redo_saction->set_enabled(redo);
}

std::vector<std::vector<Glib::ustring>> raw_data_undo_document =
{
    // clang-format off
    {"doc.undo",                                N_("Undo"),                   "Edit Document",     N_("Undo last action")},
    {"doc.redo",                                N_("Redo"),                   "Edit Document",     N_("Do again the last undone action")},
    // clang-format on
};

void
add_actions_undo_document(SPDocument* document)
{
    auto group = document->getActionGroup();
    
    // clang-format off
    group->add_action( "undo",                            sigc::bind(sigc::ptr_fun(&undo), document));
    group->add_action( "redo",                            sigc::bind(sigc::ptr_fun(&redo), document));
    // clang-format on

    auto app = InkscapeApplication::instance();
    if (!app) { // i.e. Inkview
        return;
    }
    app->get_action_extra_data().add_data(raw_data_undo_document);
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
