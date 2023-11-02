// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A dialog for XML attributes based on Gtk TreeView
 */
/* Authors:
 *   Martin Owens
 *
 * Copyright (C) Martin Owens 2018 <doctormo@gmail.com>
 *
 * Released under GNU GPLv2 or later, read the file 'COPYING' for more information
 */

#ifndef SEEN_UI_DIALOGS_ATTRDIALOG_H
#define SEEN_UI_DIALOGS_ATTRDIALOG_H

#include <memory>
#include <gtk/gtk.h> // GtkEventControllerKey
#include <gtkmm/treemodel.h>

#include "helper/auto-connection.h"
#include "ui/dialog/dialog-base.h"
#include "ui/syntax.h"
#include "xml/node-observer.h"

namespace Gtk {
class Box;
class Builder;
class CellEditable;
class CellRendererText;
class Entry;
class Label;
class ListStore;
class Popover;
class ScrolledWindow;
class TextView;
class TreeView;
class TreeViewColumn;
} // namespace Gtk

namespace Inkscape {

class MessageStack;
class MessageContext;

namespace UI::Dialog {

/**
 * @brief The AttrDialog class
 * This dialog allows to add, delete and modify XML attributes created in the
 * xml editor.
 */
class AttrDialog
    : public DialogBase
    , private XML::NodeObserver
{
public:
    AttrDialog();
    ~AttrDialog() override;

    void setRepr(Inkscape::XML::Node * repr);
    Gtk::ScrolledWindow& get_scrolled_window() { return _scrolled_window; }
    Gtk::Box& get_status_box() { return _status_box; }
    void adjust_popup_edit_size();
    void set_mono_font(bool mono);

private:
    // builder comes first, so it is initialized before other data members
    Glib::RefPtr<Gtk::Builder> _builder;

    // Data structure
    class AttrColumns : public Gtk::TreeModel::ColumnRecord
    {
    public:
        AttrColumns()
        {
            add(_attributeName);
            add(_attributeValue);
            add(_attributeValueRender);
        }
        Gtk::TreeModelColumn<Glib::ustring> _attributeName;
        Gtk::TreeModelColumn<Glib::ustring> _attributeValue;
        Gtk::TreeModelColumn<Glib::ustring> _attributeValueRender;
    };
    AttrColumns _attrColumns;

    // TreeView
    Gtk::TreeView& _treeView;
    Glib::RefPtr<Gtk::ListStore> _store;
    Gtk::CellRendererText *_nameRenderer;
    Gtk::CellRendererText *_valueRenderer;
    Gtk::TreeViewColumn *_nameCol;
    Gtk::TreeViewColumn *_valueCol;
    Gtk::Popover *_popover;
    Glib::ustring _value_path;
    Glib::ustring _value_editing;
    // Status bar
    std::shared_ptr<Inkscape::MessageStack> _message_stack;
    std::unique_ptr<Inkscape::MessageContext> _message_context;
    // Widgets
    Gtk::ScrolledWindow& _scrolled_window;
    Gtk::ScrolledWindow& _scrolled_text_view;
    // Variables - Inkscape
    Inkscape::XML::Node* _repr{nullptr};
    Gtk::Box& _status_box;
    Gtk::Label& _status;
    bool _updating = true;

    // Helper functions
    void setUndo(Glib::ustring const &event_description);
    void createAttribute();
    void deleteAttribute(Gtk::TreeRow &row);

    /**
     * Sets the XML status bar, depending on which attr is selected.
     */
    void attr_reset_context(gint attr);

    /**
     * Signal handlers
     */
    auto_connection _message_changed_connection;
    void onCreateClicked();
    void onAttrDelete(Glib::ustring const &path);
    bool onTreeViewKeyPressed (GtkEventControllerKey const *controller,
                               unsigned keyval, unsigned keycode, GdkModifierType state);
    bool onTreeViewKeyReleased(GtkEventControllerKey const *controller,
                               unsigned keyval, unsigned keycode, GdkModifierType state);
    void truncateDigits() const;
    void popClosed();
    void startNameEdit(Gtk::CellEditable *cell, const Glib::ustring &path);
    void startValueEdit(Gtk::CellEditable *cell, const Glib::ustring &path);
    void nameEdited(const Glib::ustring &path, const Glib::ustring &name);
    void valueEdited(const Glib::ustring &path, const Glib::ustring &value);
    void valueEditedPop();
    void storeMoveToNext(Gtk::TreeModel::Path modelpath);

    // Track current CellEditable Entry:
    Gtk::Entry *_editingEntry = nullptr;
    bool _embedNewline = false;
    void setEditingEntry(Gtk::Entry *entry, bool embedNewline);

    // Text/comment nodes
    Gtk::ScrolledWindow& _content_sw;
    std::unique_ptr<Syntax::TextEditView> _text_edit;  // text content editing (plain text)
    std::unique_ptr<Syntax::TextEditView> _style_edit; // embedded CSS style (with syntax coloring)

    // Attribute value editing
    std::unique_ptr<Syntax::TextEditView> _css_edit;    // in-line CSS style
    std::unique_ptr<Syntax::TextEditView> _svgd_edit;   // SVG path data
    std::unique_ptr<Syntax::TextEditView> _points_edit; // points in a <polygon> or <polyline>
    std::unique_ptr<Syntax::TextEditView> _attr_edit;   // all other attributes (plain text)
    Syntax::TextEditView* _current_text_edit = nullptr; // current text edit for attribute value editing
    auto_connection _adjust_size;
    auto_connection _close_popup;
    int _rounding_precision = 0;

    void size_allocate_vfunc(int width, int height, int baseline) final;
    bool onPopoverKeyPressed(GtkEventControllerKey const *controller,
                             unsigned keyval, unsigned keycode, GdkModifierType state);

    void setPrecision(int const n);

    void notifyAttributeChanged(XML::Node &repr, GQuark name, Util::ptr_shared old_value, Util::ptr_shared new_value) final;
    void notifyContentChanged(XML::Node &node, Util::ptr_shared old_content, Util::ptr_shared new_content) final;
    static Glib::ustring round_numbers(const Glib::ustring& text, int precision);
    Gtk::TextView &_activeTextView() const;
    void set_current_textedit(Syntax::TextEditView* edit);
    static std::unique_ptr<Syntax::TextEditView> init_text_view(AttrDialog* owner, Syntax::SyntaxMode coloring, bool map);
};

} // namespace UI::Dialog

} // namespace Inkscape

#endif // SEEN_UI_DIALOGS_ATTRDIALOG_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:
