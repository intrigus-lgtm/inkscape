// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A dialog for CSS selectors
 */
/* Authors:
 *   Kamalpreet Kaur Grewal
 *   Tavmjong Bah
 *   Jabiertxof
 *
 * Copyright (C) Kamalpreet Kaur Grewal 2016 <grewalkamal005@gmail.com>
 * Copyright (C) Tavmjong Bah 2017 <tavmjong@free.fr>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_UI_DIALOG_STYLEDIALOG_H
#define SEEN_UI_DIALOG_STYLEDIALOG_H

#include <map>
#include <memory>
#include <vector>

#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtk/gtk.h> // GtkEventControllerKey
#include <gtkmm/box.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/treepath.h>

#include "ui/dialog/dialog-base.h"

namespace Gtk {
class Adjustment;
class CellEditable;
class Dialog;
class Entry;
class TreeStore;
class TreeView;
class TreeViewColumn;
} // namespace Gtk

struct SPStyleEnum;

class SPObject;

namespace Inkscape {

class Selection;

namespace XML {
class Node;
class NodeObserver;
} // namespace XML

namespace UI::Dialog {

// for selectorsdialog.cpp
XML::Node *get_first_style_text_node(XML::Node *root, bool create_if_missing);

/**
 * @brief The StyleDialog class
 * A list of CSS selectors will show up in this dialog. This dialog allows one to
 * add and delete selectors. Elements can be added to and removed from the selectors
 * in the dialog. Selection of any selector row selects the matching  objects in
 * the drawing and vice-versa. (Only simple selectors supported for now.)
 *
 * This class must keep two things in sync:
 *   1. The text node of the style element.
 *   2. The Gtk::TreeModel.
 */
class StyleDialog final : public DialogBase
{
public:
    StyleDialog();
    ~StyleDialog() final;

    void documentReplaced() final;
    void selectionChanged(Selection *selection) final;

    void setCurrentSelector(Glib::ustring current_selector);
    Gtk::TreeView *_current_css_tree;
    Gtk::TreeViewColumn *_current_value_col;
    Gtk::TreeModel::Path _current_path;
    bool _deletion{false};
    Glib::ustring fixCSSSelectors(Glib::ustring selector);
    void readStyleElement();

  private:
    using AttrProp = std::map<Glib::ustring, Glib::ustring>;

    // Monitor <style> element for changes.
    class NodeObserver;
    // Monitor all objects for addition/removal/attribute change
    class NodeWatcher;
    void _nodeAdded(Inkscape::XML::Node &repr);
    void _nodeRemoved(Inkscape::XML::Node &repr);
    void _nodeChanged(Inkscape::XML::Node &repr);
    void removeObservers();

    // Data structure
    class ModelColumns : public Gtk::TreeModel::ColumnRecord {
      public:
        ModelColumns()
        {
            add(_colActive);
            add(_colName);
            add(_colValue);
            add(_colStrike);
            add(_colSelector);
            add(_colSelectorPos);
            add(_colOwner);
            add(_colLinked);
            add(_colHref);
        }
        Gtk::TreeModelColumn<bool> _colActive;            // Active or inactive property
        Gtk::TreeModelColumn<Glib::ustring> _colName;     // Name of the property.
        Gtk::TreeModelColumn<Glib::ustring> _colValue;    // Value of the property.
        Gtk::TreeModelColumn<bool> _colStrike;            // Property not used, overloaded
        Gtk::TreeModelColumn<Glib::ustring> _colSelector; // Style or matching object id.
        Gtk::TreeModelColumn<int> _colSelectorPos;        // Position of the selector to handle dup selectors
        Gtk::TreeModelColumn<Glib::ustring> _colOwner;    // Store the owner of the property for popup
        Gtk::TreeModelColumn<bool> _colLinked;            // Other object linked
        Gtk::TreeModelColumn<SPObject *> _colHref;        // Is going to another object
    };
    ModelColumns _mColumns;

    class CSSData : public Gtk::TreeModel::ColumnRecord {
      public:
        CSSData() { add(_colCSSData); }
        Gtk::TreeModelColumn<Glib::ustring> _colCSSData; // Name of the property.
    };
    CSSData _mCSSData;

    unsigned _deleted_pos{0};

    // Widgets
    Gtk::ScrolledWindow _scrolledWindow;
    Glib::RefPtr<Gtk::Adjustment> _vadj;
    Gtk::Box _mainBox;
    Gtk::Box _styleBox;

    // Reading and writing the style element.

    Inkscape::XML::Node *_getStyleTextNode(bool create_if_missing = false);

    void _writeStyleElement(Glib::RefPtr<Gtk::TreeStore> const &store,
                            Glib::ustring selector, Glib::ustring const &new_selector = {});

    void _activeToggled(const Glib::ustring &path, Glib::RefPtr<Gtk::TreeStore> const &store);

    void _addRow(Glib::RefPtr<Gtk::TreeStore> const &store, Gtk::TreeView *css_tree,
                 Glib::ustring const &selector, int pos);

    void _onPropDelete(Glib::ustring const &path, Glib::RefPtr<Gtk::TreeStore> const &store);

    void _nameEdited(const Glib::ustring &path, const Glib::ustring &name, Glib::RefPtr<Gtk::TreeStore> store,
                     Gtk::TreeView *css_tree);

    Gtk::Entry *_editingEntry = nullptr;
    void _addTreeViewHandlers(Gtk::TreeView &treeview);
    void _setEditingEntry(Gtk::Entry *entry, Glib::ustring endChars);
    bool _onTreeViewKeyReleased(GtkEventControllerKey const *controller,
                                unsigned keyval, unsigned keycode, GdkModifierType state);
    void _onTreeViewFocusLeave();

    void _onLinkObj(Glib::ustring const &path, Glib::RefPtr<Gtk::TreeStore> const &store);

    void _valueEdited(const Glib::ustring &path, const Glib::ustring &value, Glib::RefPtr<Gtk::TreeStore> store);
    void _startNameEdit(Gtk::CellEditable *cell, const Glib::ustring &path);
    void _startValueEdit(Gtk::CellEditable *cell, const Glib::ustring &path, Glib::RefPtr<Gtk::TreeStore> store);

    void _setAutocompletion(Gtk::Entry *entry, SPStyleEnum const cssenum[]);
    void _setAutocompletion(Gtk::Entry *entry, Glib::ustring name);
    bool _on_foreach_iter(const Gtk::TreeModel::iterator &iter);
    void _reload();
    void _vscroll();

    bool _scrollock;
    double _scrollpos{0};
    Glib::ustring _current_selector;

    // Update watchers
    std::unique_ptr<XML::NodeObserver> const m_nodewatcher;
    std::unique_ptr<XML::NodeObserver> const m_styletextwatcher;

    // Manipulate Tree
    std::vector<SPObject *> _getObjVec(Glib::ustring selector);
    AttrProp parseStyle(Glib::ustring style_string);
    AttrProp _owner_style;
    void _addOwnerStyle(Glib::ustring name, Glib::ustring selector);

    // Variables
    Inkscape::XML::Node *m_root{nullptr};
    Inkscape::XML::Node *_textNode{nullptr}; // Track so we know when to add a NodeObserver.
    bool _updating{false};                   // Prevent cyclic actions: read <-> write, select via dialog <-> via desktop

    void _closeDialog(Gtk::Dialog *textDialogPtr);
};

} // namespace UI::Dialog

} // namespace Inkscape

#endif // SEEN_UI_DIALOG_STYLEDIALOG_H

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
