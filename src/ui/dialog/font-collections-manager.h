// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Text-edit
 */
/* Authors:
 *   Vaihav Malik <vaibhavmalik2018@gmail.com>
 *
 * Copyright (C) 1999-2013 Authors
 * Copyright (C) 2000-2001 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_DIALOG_FONT_COLLECTIONS_MANAGER_H
#define INKSCAPE_UI_DIALOG_FONT_COLLECTIONS_MANAGER_H

#include "helper/auto-connection.h"
#include "ui/dialog/dialog-base.h"
#include "ui/widget/font-selector.h"
#include "ui/widget/font-collection-selector.h"

namespace Gtk {
class Box;
class Builder;
class Label;
class Paned;
class SearchEntry2;
} // namespace Gtk

namespace Inkscape::UI::Dialog {

/**
 * The font collections manager dialog allows the user to:
 * 1. Create
 * 2. Read
 * 3. Update
 * 4. Delete
 * the font collections and the fonts associated with each collection.
 *
 * User can add new fonts in font collections by dragging the fonts from the
 * font list and dropping them a user font collection.
 */
class FontCollectionsManager final : public DialogBase
{
public:
    enum SelectionStates {SYSTEM_COLLECTION = -1, USER_COLLECTION, USER_COLLECTION_FONT};

    FontCollectionsManager();

private:
    void on_search_entry_changed();
    void on_create_button_pressed();
    void on_edit_button_pressed();
    void on_delete_button_pressed();
    void on_reset_button_pressed();
    void change_font_count_label();
    void on_selection_changed(int state);

    /*
     * All the dialogs widgets
     */
    Glib::RefPtr<Gtk::Builder> builder;
    Gtk::Box &_contents;
    Gtk::Paned &_paned;
    Gtk::Box &_collections_box;
    Gtk::Box &_buttons_box;
    Gtk::Box &_font_list_box;
    Gtk::Label &_font_count_label;
    Gtk::Box &_font_list_filter_box;
    Gtk::SearchEntry2 &_search_entry;
    Gtk::Button &_reset_button;
    Gtk::Button &_create_button;
    Gtk::Button &_edit_button;
    Gtk::Button &_delete_button;
    Inkscape::UI::Widget::FontSelector _font_selector;
    Inkscape::UI::Widget::FontCollectionSelector _user_font_collections;

    // Signals
    auto_connection _font_count_changed_connection;
};

} // namespace Inkscape::UI::Dialog

#endif // INKSCAPE_UI_DIALOG_FONT_COLLECTIONS_MANAGER_H

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
