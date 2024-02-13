// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A dialog to manage the font collections.
 */
/* Authors:
 *   Vaibhav Malik
 *
 * Released under GNU GPLv2 or later, read the file 'COPYING' for more information
 */

#include "font-collections-manager.h"

#include <gdk/gdkkeysyms.h>
#include <glibmm/i18n.h>
#include <gtkmm/box.h>
#include <gtkmm/builder.h>
#include <gtkmm/label.h>
#include <gtkmm/paned.h>
#include <gtkmm/searchentry2.h>

#include "desktop.h"
#include "libnrtype/font-lister.h"
#include "ui/builder-utils.h"
#include "ui/icon-names.h"
#include "ui/pack.h"
#include "util/font-collections.h"

namespace Inkscape::UI::Dialog {

FontCollectionsManager::FontCollectionsManager()
    : DialogBase("/dialogs/fontcollections", "FontCollections")
    , builder(UI::create_builder("dialog-font-collections.glade"))
    , _contents             (UI::get_widget<Gtk::Box>   (builder, "contents"))
    , _paned                (UI::get_widget<Gtk::Paned> (builder, "paned"))
    , _collections_box      (UI::get_widget<Gtk::Box>   (builder, "collections_box"))
    , _buttons_box          (UI::get_widget<Gtk::Box>   (builder, "buttons_box"))
    , _font_list_box        (UI::get_widget<Gtk::Box>   (builder, "font_list_box"))
    , _font_count_label     (UI::get_widget<Gtk::Label> (builder, "font_count_label"))
    , _font_list_filter_box (UI::get_widget<Gtk::Box>   (builder, "font_list_filter_box"))
    , _search_entry         (UI::get_widget<Gtk::SearchEntry2>(builder, "search_entry"))
    , _reset_button         (UI::get_widget<Gtk::Button>(builder, "reset_button"))
    , _create_button        (UI::get_widget<Gtk::Button>(builder, "create_button"))
    , _edit_button          (UI::get_widget<Gtk::Button>(builder, "edit_button"))
    , _delete_button        (UI::get_widget<Gtk::Button>(builder, "delete_button"))
{
    UI::pack_start(_font_list_box, _font_selector, true, true);
    _font_list_box.reorder_child_after(_font_selector, *_font_list_box.get_first_child());

    UI::pack_start(_collections_box, _user_font_collections, true, true);
    _collections_box.reorder_child_at_start(_user_font_collections);

    _user_font_collections.populate_system_collections();
    _user_font_collections.populate_user_collections();
    _user_font_collections.change_frame_name(_("Font Collections"));

    append(_contents);

    // Set the button images.
    _create_button.set_image_from_icon_name(INKSCAPE_ICON("list-add"));
    _edit_button.set_image_from_icon_name(INKSCAPE_ICON("document-edit"));
    _delete_button.set_image_from_icon_name(INKSCAPE_ICON("edit-delete"));

    // Paned settings.
    _paned.set_resize_start_child(false);
    _paned.set_resize_end_child(false);

    change_font_count_label();
    _font_selector.hide_others();

    // Setup the signals.
    _font_count_changed_connection = Inkscape::FontLister::get_instance()->connectUpdate(sigc::mem_fun(*this, &FontCollectionsManager::change_font_count_label));
    _search_entry.signal_search_changed().connect([=](){ on_search_entry_changed(); });
    _user_font_collections.connect_signal_changed([=](int s){ on_selection_changed(s); });
    _create_button.signal_clicked().connect([=](){ on_create_button_pressed(); });
    _edit_button.signal_clicked().connect([=](){ on_edit_button_pressed(); });
    _delete_button.signal_clicked().connect([=](){ on_delete_button_pressed(); });
    _reset_button.signal_clicked().connect([=](){ on_reset_button_pressed(); });

    // Edit and delete are initially insensitive because nothing is selected.
    _edit_button.set_sensitive(false);
    _delete_button.set_sensitive(false);
}

void FontCollectionsManager::on_search_entry_changed()
{
    auto search_txt = _search_entry.get_text();
    _font_selector.unset_model();
    Inkscape::FontLister *font_lister = Inkscape::FontLister::get_instance();
    font_lister->show_results(search_txt);
    _font_selector.set_model();
    change_font_count_label();
}

void FontCollectionsManager::on_create_button_pressed()
{
    _user_font_collections.on_create_collection();
}

void FontCollectionsManager::on_delete_button_pressed()
{
    _user_font_collections.on_delete_button_pressed();
}

void FontCollectionsManager::on_edit_button_pressed()
{
    _user_font_collections.on_edit_button_pressed();
}

void FontCollectionsManager::on_reset_button_pressed()
{
    _search_entry.set_text("");
    Inkscape::FontLister* font_lister = Inkscape::FontLister::get_instance();

    if(font_lister->get_font_families_size() == font_lister->get_font_list()->children().size()) {
        // _user_font_collections.populate_collections();
        return;
    }

    Inkscape::FontCollections::get()->clear_selected_collections();
    font_lister->init_font_families();
    font_lister->init_default_styles();
    SPDocument *document = getDesktop()->getDocument();
    font_lister->add_document_fonts_at_top(document);
}

void FontCollectionsManager::change_font_count_label()
{
    auto label = Inkscape::FontLister::get_instance()->get_font_count_label();
    _font_count_label.set_label(label);
}

// This function will set the sensitivity of the edit and delete buttons
// Whenever the selection changes.
void FontCollectionsManager::on_selection_changed(int state)
{
    bool edit = false, del = false;
    switch(state) {
        case SYSTEM_COLLECTION:
            break;
        case USER_COLLECTION:
            edit = true;
            del = true;
            break;
        case USER_COLLECTION_FONT:
            edit = false;
            del = true;
        default:
            break;
    }
    _edit_button.set_sensitive(edit);
    _delete_button.set_sensitive(del);
}

} // namespace Inkscape::UI::Dialog

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
