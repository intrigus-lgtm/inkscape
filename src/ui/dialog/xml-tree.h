// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * This is XML tree editor, which allows direct modifying of all elements
 *   of Inkscape document, including foreign ones.
 *//*
 * Authors: see git history
 * Lauris Kaplinski, 2000
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_DIALOG_XML_TREE_H
#define INKSCAPE_UI_DIALOG_XML_TREE_H

#include <memory>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/label.h>
#include <gtkmm/switch.h>
#include <gtkmm/widget.h>

#include "attrdialog.h"
#include "dialog-base.h"
#include "message.h"
#include "preferences.h"

#include "ui/syntax.h"
#include "ui/widget/bin.h"

namespace Gtk {
class Box;
class Builder;
class Button;
class Entry;
class Paned;
class TreeView;
} // namespace Gtk

class SPObject;
struct SPXMLViewAttrList;
struct SPXMLViewContent;
struct SPXMLViewTree;

namespace Inkscape {

class MessageStack;
class MessageContext;

namespace XML { class Node; }

namespace UI::Dialog {

/**
 * A dialog widget to view and edit the document xml
 */

class XmlTree final : public DialogBase
{
public:
    XmlTree();
    ~XmlTree() final;

    void setSyntaxStyle(Inkscape::UI::Syntax::XMLStyles const &new_style);

private:
    void unsetDocument();
    void documentReplaced() final;
    void selectionChanged(Selection *selection) final;
    void desktopReplaced() final;

    /**
     * Select a node in the xml tree
     */
    void set_tree_repr(Inkscape::XML::Node *repr);

    /**
     * Is the selected tree node editable
     */
    gboolean xml_tree_node_mutable(GtkTreeIter *node);

    /**
     * Select a node in the xml tree
     */
    void set_tree_select(Inkscape::XML::Node *repr, bool edit = false);

    /**
     * Set the attribute list to match the selected node in the tree
     */
    void propagate_tree_select(Inkscape::XML::Node *repr);

    /**
      * Find the current desktop selection
      */
    Inkscape::XML::Node *get_dt_select();

    /**
      * Select the current desktop selection
      */
    void set_dt_select(Inkscape::XML::Node *repr);

    /**
     * Callback for deferring the `on_tree_select_row` response in order to
     * skip invalid intermediate selection states. In particular,
     * `gtk_tree_store_remove` makes an undesired selection that we will
     * immediately revert and don't want to an early response for.
     */
    Inkscape::auto_connection _tree_select_idle;
    bool deferred_on_tree_select_row();

    /**
      * Enable widgets based on current selections
      */
    void on_tree_select_row_enable(GtkTreeIter *node);
    void on_tree_unselect_row_disable();
    void on_tree_unselect_row_hide();
    void on_attr_unselect_row_disable();

    void onNameChanged();
    void onCreateNameChanged();

    /**
      * Callbacks for changes in desktop selection and current document
      */
    static void _set_status_message(Inkscape::MessageType type, const gchar *message, GtkWidget *dialog);

    /**
      * Callbacks for toolbar buttons being pressed
      */
    void cmd_new_element_node();
    void cmd_new_text_node();
    void cmd_duplicate_node();
    void cmd_delete_node();
    void cmd_raise_node();
    void cmd_lower_node();
    void cmd_indent_node();
    void cmd_unindent_node();

    void _resized();
    bool in_dt_coordsys(SPObject const &item);

    void rebuildTree();
    void stopNodeEditing(bool ok, Glib::ustring const &path, Glib::ustring name);
    void startNodeEditing(Gtk::CellEditable *cell, Glib::ustring const &path);

    /**
     * Flag to ensure only one operation is performed at once
     */
    gint blocked = 0;

    /**
     * Signal handlers
     */
    Inkscape::XML::Node *selected_repr = nullptr;

    /* XmlTree Widgets */
    SPXMLViewTree *tree = nullptr;
    Gtk::TreeView* _treemm = nullptr;
    AttrDialog *attributes;
    Gtk::Box *_attrbox;

    /* XML Node Creation pop-up window */
    Glib::RefPtr<Gtk::Builder> _builder;
    UI::Widget::Bin _bin;
    Gtk::Entry *name_entry;
    Gtk::Button *create_button;
    Gtk::Paned& _paned;

    // Gtk::Box node_box;
    Gtk::Switch _attrswitch;
    Gtk::Label status;
    Gtk::Button& xml_element_new_button;
    Gtk::Button& xml_text_new_button;
    Gtk::Button& xml_node_delete_button;
    Gtk::Button& xml_node_duplicate_button;
    Gtk::Button& unindent_node_button;
    Gtk::Button& indent_node_button;
    Gtk::Button& raise_node_button;
    Gtk::Button& lower_node_button;

    // Keep these options in sync with menu/actions @ share/ui/dialog-xml.glade!
    enum DialogLayout: int { Auto = 0, Horizontal, Vertical };
    DialogLayout _layout = Auto;

    Pref<Glib::ustring> _syntax_theme;
    Pref<bool> _mono_font;
    Inkscape::XML::Node* _dummy = nullptr;
    Inkscape::XML::Node* _node_parent = nullptr;
};

} // namespace UI::Dialog

} // namespace Inkscape

#endif // INKSCAPE_UI_DIALOG_XML_TREE_H

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
