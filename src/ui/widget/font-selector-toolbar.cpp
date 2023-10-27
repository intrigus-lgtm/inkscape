// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * The routines here create and manage a font selector widget with two parts,
 * one each for font-family and font-style.
 *
 * This is essentially a toolbar version of the 'FontSelector' widget. Someday
 * this may be merged with it.
 *
 * The main functions are:
 *   Create the font-selector toolbar widget.
 *   Update the lists when a new text selection is made.
 *   Update the Style list when a new font-family is selected, highlighting the
 *     best match to the original font style (as not all fonts have the same style options).
 *   Update the on-screen text.
 *   Provide the currently selected values.
 */
/*
 * Author:
 *   Tavmjong Bah <tavmjong@free.fr>
 *
 * Copyright (C) 2018 Tavmong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "font-selector-toolbar.h"

#include <iostream>
#include <sigc++/functors/mem_fun.h>
#include <glibmm/i18n.h>
#include <glibmm/regex.h>
#include <gdkmm/display.h>
#include <libnrtype/font-instance.h>
#include <libnrtype/font-lister.h>

#include "inkscape.h"
#include "desktop.h"
#include "object/sp-text.h"
#include "ui/controller.h"
#include "ui/icon-names.h"
// TEMP TEMP TEMP
#include "ui/toolbar/text-toolbar.h"

/* To do:
 *   Fix altx.  The setToolboxFocusTo method now just searches for a named widget.
 *   We just need to do the following:
 *   * Set the name of the family_combo child widget
 *   * Change the setToolboxFocusTo() argument in tools/text-tool to point to that widget name
 */

static void family_cell_data_func(Gtk::TreeModel::const_iterator const iter,
                                  Gtk::CellRendererText* const cell)
{
    Inkscape::FontLister* font_lister = Inkscape::FontLister::get_instance();
    Glib::ustring markup = font_lister->get_font_family_markup(iter);
    cell->set_property ("markup", markup);
}

namespace Inkscape::UI::Widget {

FontSelectorToolbar::FontSelectorToolbar ()
    : Gtk::Grid ()
    , family_combo (true)  // true => with text entry.
    , style_combo (true)
    , signal_block (false)
{
    Inkscape::FontLister* font_lister = Inkscape::FontLister::get_instance();

    // Font family
    family_combo.set_model (font_lister->get_font_list());
    family_combo.set_entry_text_column (0);
    family_combo.set_name ("FontSelectorToolBar: Family");
    family_combo.set_row_separator_func (&font_lister_separator_func);

    family_combo.clear(); // Clears all CellRenderer mappings.
    family_combo.set_cell_data_func (family_cell,
                                     sigc::bind(sigc::ptr_fun(family_cell_data_func), &family_cell));
    family_combo.pack_start (family_cell);

    Gtk::Entry* entry = family_combo.get_entry();
    entry->signal_icon_press().connect(sigc::mem_fun(*this, &FontSelectorToolbar::on_icon_pressed));
    Controller::add_key<&FontSelectorToolbar::on_key_pressed>(*entry, *this);

    Glib::RefPtr<Gtk::EntryCompletion> completion = Gtk::EntryCompletion::create();
    completion->set_model (font_lister->get_font_list());
    completion->set_text_column (0);
    completion->set_popup_completion ();
    completion->set_inline_completion (false);
    completion->set_inline_selection ();
    entry->set_completion (completion);

    // Style
    style_combo.set_model (font_lister->get_style_list());
    style_combo.set_name ("FontSelectorToolbar: Style");

    // Grid
    set_name ("FontSelectorToolbar: Grid");
    attach (family_combo,  0, 0, 1, 1);
    attach (style_combo,   1, 0, 1, 1);

    // Add signals
    family_combo.signal_changed().connect ([=](){ on_family_changed(); });
    style_combo.signal_changed().connect ([=](){ on_style_changed(); });

    // Initialize font family lists. (May already be done.) Should be done on document change.
    font_lister->update_font_list(SP_ACTIVE_DESKTOP->getDocument());

    // When FontLister is changed, update family and style shown in GUI.
    font_lister->connectUpdate([=](){ update_font(); });
}

// Update GUI based on font-selector values.
void
FontSelectorToolbar::update_font ()
{
    if (signal_block) return;

    signal_block = true;

    Inkscape::FontLister* font_lister = Inkscape::FontLister::get_instance();
    Gtk::TreeModel::Row row;

    // Set font family.
    try {
        row = font_lister->get_row_for_font();
        family_combo.set_active(row.get_iter());
    } catch (FontLister::Exception) {
        std::cerr << "FontSelectorToolbar::update_font: Couldn't find row for family: "
                  << font_lister->get_font_family().raw() << std::endl;
    }

    // Set style.
    try {
        row = font_lister->get_row_for_style();
        style_combo.set_active(row.get_iter());
    } catch (FontLister::Exception) {
        std::cerr << "FontSelectorToolbar::update_font: Couldn't find row for style: "
                  << font_lister->get_font_style().raw() << std::endl;
    }

    // Check for missing fonts.
    Glib::ustring missing_fonts = get_missing_fonts();

    // Add an icon to end of entry.
    Gtk::Entry* entry = family_combo.get_entry();
    if (missing_fonts.empty()) {
        // If no missing fonts, add icon for selecting all objects with this font-family.
        entry->set_icon_from_icon_name(INKSCAPE_ICON("edit-select-all"), Gtk::Entry::IconPosition::SECONDARY);
        entry->set_icon_tooltip_text(_("Select all text with this text family"), Gtk::Entry::IconPosition::SECONDARY);
    } else {
        // If missing fonts, add warning icon.
        Glib::ustring warning = _("Font not found on system: ") + missing_fonts; 
        entry->set_icon_from_icon_name(INKSCAPE_ICON("dialog-warning"), Gtk::Entry::IconPosition::SECONDARY);
        entry->set_icon_tooltip_text(warning, Gtk::Entry::IconPosition::SECONDARY);
    }

    signal_block = false;
}

// Get comma separated list of fonts in font-family that are not on system.
// To do, move to font-lister.
Glib::ustring
FontSelectorToolbar::get_missing_fonts ()
{
    // Get font list in text entry which may be a font stack (with fallbacks).
    Glib::ustring font_list = family_combo.get_entry_text();
    Glib::ustring missing_font_list;
    Inkscape::FontLister* font_lister = Inkscape::FontLister::get_instance();

    std::vector<Glib::ustring> tokens = Glib::Regex::split_simple("\\s*,\\s*", font_list);

    for (auto token: tokens) {
        bool found = false;
        Gtk::TreeModel::Children children = font_lister->get_font_list()->children();
        for (auto row2 : children) {
            Glib::ustring family2 = row2[font_lister->font_list.family];
            bool onSystem2        = row2[font_lister->font_list.onSystem];
            // CSS dictates that font family names are case insensitive.
            // This should really implement full Unicode case unfolding.
            if (onSystem2 && token.casefold().compare(family2.casefold()) == 0) {
                found = true;
                break;
            }
        }

        if (!found) {
            missing_font_list += token;
            missing_font_list += ", ";
        }
    }

    // Remove extra comma and space from end.
    if (missing_font_list.size() >= 2) {
        missing_font_list.resize(missing_font_list.size() - 2);
    }

    return missing_font_list;
}

// Callbacks

// Need to update style list
void
FontSelectorToolbar::on_family_changed() {

    if (signal_block) return;
    signal_block = true;

    Glib::ustring family = family_combo.get_entry_text();

    Inkscape::FontLister *fontlister = Inkscape::FontLister::get_instance();
    fontlister->set_font_family (family);

    signal_block = false;

    // Let world know
    changed_emit();
}

void
FontSelectorToolbar::on_style_changed() {

    if (signal_block) return;
    signal_block = true;

    Glib::ustring style = style_combo.get_entry_text();

    Inkscape::FontLister *fontlister = Inkscape::FontLister::get_instance();
    fontlister->set_font_style (style);

    signal_block = false;

    // Let world know
    changed_emit();
}

void FontSelectorToolbar::on_icon_pressed(Gtk::Entry::IconPosition icon_position)
{
    std::cerr << "FontSelectorToolbar::on_entry_icon_pressed" << std::endl;
    std::cerr << "    .... Should select all items with same font-family. FIXME" << std::endl;
    // Call equivalent of sp_text_toolbox_select_cb() in text-toolbar.cpp
    // Should be action!  (Maybe: select_all_fontfamily( Glib::ustring font_family );).
    // Check how Find dialog works.
}

// Return focus to canvas.
bool
FontSelectorToolbar::on_key_pressed(GtkEventControllerKey const * /*controller*/,
                                    unsigned /*keyval*/, unsigned const keycode,
                                    GdkModifierType const state)
{
    unsigned int key = 0;
    gdk_display_translate_key(gdk_display_get_default(),
                            keycode,
                            state,
                            0,
                            &key,
                            nullptr,
                            nullptr,
                            nullptr);
    switch ( key ) {
        case GDK_KEY_Escape:
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            // Defocus
            std::cerr << "FontSelectorToolbar::on_key_pressed: Defocus: FIXME" << std::endl;
            return true;
    }

    return false;
}

void
FontSelectorToolbar::changed_emit() {
    signal_block = true;
    changed_signal.emit ();
    signal_block = false;
}

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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8 :
