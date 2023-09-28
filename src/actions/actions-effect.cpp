// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 *
 *  Actions for Filters and Extension menu items
 *
 * Authors:
 *   Sushant A A <sushant.co19@gmail.com>
 *
 * Copyright (C) 2021 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "actions-effect.h"

#include <giomm.h>
#include <glibmm/i18n.h>

#include "document.h"
#include "actions-helper.h"
#include "inkscape-application.h"
#include "selection.h"
#include "extension/effect.h"
#include "extension/db.h"

void edit_remove_filter(InkscapeApplication *app)
{
    auto selection = app->get_active_selection();

    // Remove Filter
    selection->removeFilter();
}

void last_effect(InkscapeApplication *app)
{
    Inkscape::Extension::Effect *effect = Inkscape::Extension::Effect::get_last_effect();

    if (effect == nullptr) {
        return;
    }

    // Last Effect
    effect->effect(InkscapeApplication::instance()->get_active_desktop());
}

void last_effect_pref(InkscapeApplication *app)
{
    Inkscape::Extension::Effect *effect = Inkscape::Extension::Effect::get_last_effect();

    if (effect == nullptr) {
        return;
    }

    // Last Effect Pref
    effect->prefs(InkscapeApplication::instance()->get_active_desktop());
}

void enable_effect_actions(InkscapeApplication* app, bool enabled)
{
    auto gapp = app->gio_app();
    auto le_action = gapp->lookup_action("last-effect");
    auto lep_action = gapp->lookup_action("last-effect-pref");
    auto le_saction = std::dynamic_pointer_cast<Gio::SimpleAction>(le_action);
    auto lep_saction = std::dynamic_pointer_cast<Gio::SimpleAction>(lep_action);
    // GTK4
    // auto le_saction = dynamic_cast<Gio::SimpleAction*>(le_action);
    // auto lep_saction = dynamic_cast<Gio::SimpleAction*>(lep_action);
    if (!le_saction || !lep_saction) {
        g_warning("Unable to find Extension actions.");
        return;
    }
    // Enable/disable menu items.
    le_saction->set_enabled(enabled);
    lep_saction->set_enabled(enabled);
}

std::vector<std::vector<Glib::ustring>> raw_data_effect =
{
    // clang-format off
    {"app.edit-remove-filter",      N_("Remove Filters"),                   "Filters",          N_("Remove any filters from selected objects")},
    {"app.last-effect",             N_("Previous Extension"),               "Extensions",       N_("Repeat the last extension with the same settings")},
    {"app.last-effect-pref",        N_("Previous Extension Settings"),      "Extensions",       N_("Repeat the last extension with new settings")}
    // clang-format on
};

void add_actions_effect(InkscapeApplication* app)
{
    auto *gapp = app->gio_app();

    // clang-format off
    gapp->add_action( "edit-remove-filter",     sigc::bind(sigc::ptr_fun(&edit_remove_filter), app));
    gapp->add_action( "last-effect",            sigc::bind(sigc::ptr_fun(&last_effect), app));
    gapp->add_action( "last-effect-pref",       sigc::bind(sigc::ptr_fun(&last_effect_pref), app));
    // clang-format on

    if (!app) {
        show_output("add_actions_edit: no app!");
        return;
    }
    app->get_action_extra_data().add_data(raw_data_effect);
}

void add_document_actions_effect(SPDocument *doc)
{
    auto group = doc->getActionGroup();

    for (auto mod : Inkscape::Extension::db.get_effect_list()) {
        group->add_action( mod->get_sanitized_id(), [=]() { mod->effect(nullptr, doc); });
    }

    // No extra information required to be added to the app
}
