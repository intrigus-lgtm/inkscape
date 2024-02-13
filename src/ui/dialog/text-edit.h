// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Text-edit
 */
/* Authors:
 *   Lauris Kaplinski <lauris@ximian.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Johan Engelen <goejendaagh@zonnet.nl>
 *   John Smith
 *   Kris De Gussem <Kris.DeGussem@gmail.com>
 *   Tavmjong Bah
 *
 * Copyright (C) 1999-2013 Authors
 * Copyright (C) 2000-2001 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_DIALOG_TEXT_EDIT_H
#define INKSCAPE_UI_DIALOG_TEXT_EDIT_H

#include <glibmm/refptr.h>              // for RefPtr
#include <gtk/gtk.h>                    // for GtkEventControllerKey

#include "helper/auto-connection.h"     // for auto_connection
#include "ui/dialog/dialog-base.h"      // for DialogBase
#include "ui/widget/font-selector.h"    // for FontSelector
#include "ui/widget/font-variants.h"    // for FontVariants
#include "ui/widget/font-variations.h"  // for FontVariations
#include "util/action-accel.h"          // for ActionAccel

namespace Glib {
class ustring;
} // namespace Glib

namespace Gtk {
class Box;
class Builder;
class Button;
class Frame;
class Label;
class ListBox;
class MenuButton;
class Popover;
class SearchEntry2;
class TextBuffer;
class TextView;
class Widget;
} // namespace Gtk

class SPItem;
class SPCSSAttr;

namespace Inkscape::UI::Dialog {

/**
 * The TextEdit class defines the Text and font dialog.
 *
 * The Text and font dialog allows you to set the font family, style and size
 * and shows a preview of the result. The dialogs layout settings include
 * horizontal and vertical alignment and inter line distance.
 */
class TextEdit final : public DialogBase
{
public:
    TextEdit();
    ~TextEdit() final;

    void documentReplaced() final;
    void selectionChanged(Selection *selection) final;
    void selectionModified(Selection *selection, guint flags) final;

protected:
    /**
     * Callback for pressing the default button.
     */
    void onSetDefault ();

    /**
     * Callback for pressing the apply button.
     */
    void onApply ();

    /**
     * Function to list the font collections in the popover menu.
     */
    void display_font_collections();

    /**
     * Called whenever something 'changes' on canvas.
     *
     * onReadSelection gets the currently selected item from the canvas and sets all the controls in this dialog to the correct state.
     *
     * @param dostyle Indicates whether the modification of the user includes a style change.
     * @param content Indicates whether the modification of the user includes a style change. Actually refers to the question if we do want to show the content? (Parameter currently not used)
     */
    void onReadSelection (bool style, bool content);

    /**
     * This function would disable undo and redo if the text_view widget is in focus
     * It is to fix the issue: https://gitlab.com/inkscape/inkscape/-/issues/744
     */
    bool captureUndo(GtkEventControllerKey const *controller,
                     unsigned keyval, unsigned keycode, GdkModifierType state);

    /**
     * Callback invoked when the user modifies the text of the selected text object.
     *
     * onTextChange is responsible for initiating the commands after the user
     * modified the text in the selected object. The UI of the dialog is
     * updated. The subfunction setPreviewText updates the preview label.
     *
     * @param self pointer to the current instance of the dialog.
     */
    void onChange ();
    void onFontFeatures (Gtk::Widget * widgt, int pos);

    // Callback to handle changes in the search entry.
    void on_search_entry_changed();
    void on_reset_button_pressed();
    void change_font_count_label();
    void on_fcm_button_clicked();

    /**
     * Callback invoked when the user modifies the font through the dialog or the tools control bar.
     *
     * onFontChange updates the dialog UI. The subfunction setPreviewText updates the preview label.
     *
     * @param fontspec for the text to be previewed. (Parameter currently not used)
     */
    void onFontChange (Glib::ustring const &fontspec);

    /**
     * Get the selected text off the main canvas.
     *
     * @return SPItem pointer to the selected text object
     */
    SPItem *getSelectedTextItem ();

    /**
     * Count the number of text objects in the selection on the canvas.
     */
    unsigned getSelectedTextCount ();

    /**
     * Helper function to create markup from a fontspec and display in the preview label.
     *
     * @param fontspec for the text to be previewed.
     * @param font_features for text to be previewed (in CSS format).
     * @param phrase text to be shown.
     */
    void setPreviewText (Glib::ustring const &font_spec, Glib::ustring const &font_features,
                         Glib::ustring const &phrase);

    void updateObjectText ( SPItem *text );
    SPCSSAttr *fillTextStyle ();

private:
    Glib::RefPtr<Gtk::Builder> builder;

    // Tab 1: Font ---------------------- //
    Gtk::Box &settings_and_filters_box;
    Gtk::MenuButton &filter_menu_button;
    Gtk::Button &reset_button;
    Gtk::SearchEntry2 &search_entry;
    Gtk::Label &font_count_label;
    Gtk::Popover &filter_popover;
    Gtk::Box &popover_box;
    Gtk::Frame &frame;
    Gtk::Label &frame_label;
    Gtk::Button &collection_editor_button;
    Gtk::ListBox &collections_list;
    Gtk::Label &preview_label;  // Share with variants tab?

    Inkscape::UI::Widget::FontSelector font_selector;
    Inkscape::UI::Widget::FontVariations font_variations;

    // Tab 2: Text ---------------------- //
    Gtk::TextView &text_view;
    Glib::RefPtr<Gtk::TextBuffer> text_buffer;

    // Tab 3: Features  ----------------- //
    Inkscape::UI::Widget::FontVariants font_features;
    Gtk::Label &preview_label2; // Could reparent preview_label but having a second label is probably easier.

    // Shared ------- ------------------ //
    Gtk::Button &setasdefault_button;
    Gtk::Button &apply_button;

    // Signals
    auto_connection selectChangedConn;
    auto_connection subselChangedConn;
    auto_connection selectModifiedConn;
    auto_connection fontChangedConn;
    auto_connection fontFeaturesChangedConn;
    auto_connection fontCollectionsChangedSelection;
    auto_connection fontCollectionsUpdate;

    // Other
    double selected_fontsize = 12;
    bool blocked = false;

    // Track undo and redo keyboard shortcuts
    Util::ActionAccel _undo, _redo;
};

} // namespace Inkscape::UI::Dialog

#endif // INKSCAPE_UI_DIALOG_TEXT_EDIT_H

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
