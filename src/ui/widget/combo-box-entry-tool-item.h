// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A class derived from Gtk::ToolItem that wraps a GtkComboBoxEntry.
 * Features:
 *   Setting GtkEntryBox width in characters.
 *   Passing a function for formatting cells.
 *   Displaying a warning if entry text isn't in list.
 *   Check comma separated values in text against list. (Useful for font-family fallbacks.)
 *   Setting names for GtkComboBoxEntry and GtkEntry (actionName_combobox, actionName_entry)
 *     to allow setting resources.
 *
 * Author(s):
 *   Tavmjong Bah
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright (C) 2010 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_WIDGET_COMBOBOXENTRYTOOLITEM_H
#define INKSCAPE_UI_WIDGET_COMBOBOXENTRYTOOLITEM_H

#include <optional>
#include <glibmm/ustring.h>
#include <gtk/gtk.h>
#include <gtkmm/box.h>
#include <gtkmm/combobox.h>
#include <gtkmm/cellrenderertext.h>
#include <sigc++/signal.h>
#include "helper/auto-connection.h"

namespace Gtk {
class TreeModel;
} // namespace Gtk

namespace Inkscape::UI::Widget {

/**
 * Formerly a Gtk::ToolItem that wraps a Gtk::ComboBox object.
 * Now a Gtk::Box that wraps the same. To be replaced by a Gtk::DropDown in Gtk4.
 */
class ComboBoxEntryToolItem : public Gtk::Box
{
public:
    using InfoCallback = sigc::slot<void (Gtk::Entry const &)>;
    using CellDataFunc = sigc::slot<void (Gtk::CellRenderer &cell, Gtk::TreeModel::const_iterator const &iter, bool with_markup)>;
    using SeparatorFunc = sigc::slot<bool (Glib::RefPtr<Gtk::TreeModel> const &model, Gtk::TreeModel::const_iterator const &iter)>;

    ComboBoxEntryToolItem(Glib::ustring name,
                          Glib::ustring label,
                          Glib::ustring tooltip,
                          Glib::RefPtr<Gtk::TreeModel> model,
                          int           entry_width    = -1,
                          int           extra_width    = -1,
                          CellDataFunc  cell_data_func = {},
                          SeparatorFunc separator_func = {},
                          Gtk::Widget  *focusWidget    = nullptr);

    Glib::ustring const &get_active_text() const { return _text; }
    bool set_active_text(Glib::ustring text, int row = -1);

    void set_entry_width(int entry_width);
    void set_extra_width(int extra_width);

    void popup_enable();
    void popup_disable();
    void focus_on_click(bool focus_on_click);

    void set_info      (Glib::ustring info);
    void set_info_cb   (InfoCallback info_cb);
    void set_warning   (Glib::ustring warning_cb);
    void set_warning_cb(InfoCallback warning);
    void set_tooltip   (Glib::ustring const &tooltip);

    // Accessor methods
    int get_active() const { return _active; }
    sigc::connection connectChanged(sigc::slot<void ()> &&slot) { return _signal_changed.connect(std::move(slot)); }

    // This doesn't seem right... surely we should set the active row in the Combobox too?
    void set_active(int active) { _active = active; }

    void set_model(Glib::RefPtr<Gtk::TreeModel> model) { _model = std::move(model); }

private:
    Glib::ustring       _tooltip;
    Glib::ustring       _label;
    Glib::RefPtr<Gtk::TreeModel> _model;
    Gtk::ComboBox       _combobox;
    Gtk::Entry         *_entry;
    int                 _entry_width; // Width of GtkEntry in characters.
    int                 _extra_width; // Extra Width of GtkComboBox.. to widen drop-down list in list mode.
    CellDataFunc        _cell_data_func; // drop-down menu format
    bool                _popup = false; // Do we pop-up an entry-completion dialog?
    Glib::RefPtr<Gtk::EntryCompletion> _entry_completion;
    Gtk::Widget        *_focusWidget; ///< The widget to return focus to
    std::optional<Gtk::CellRendererText> _cell;

    int                 _active = -1; // Index of active menu item (-1 if not in list).
    Glib::ustring       _text; // Text of active menu item or entry box.
    Glib::ustring       _info;       // Text for tooltip info about entry.
    InfoCallback        _info_cb; // Callback for clicking info icon.
    auto_connection     _info_cb_id;
    bool                _info_cb_blocked = false;
    Glib::ustring       _warning; // Text for tooltip warning that entry isn't in list.
    InfoCallback        _warning_cb; // Callback for clicking warning icon.
    auto_connection     _warning_cb_id;
    bool                _warning_cb_blocked = false;

    auto_connection idle_conn;

    sigc::signal<void ()> _signal_changed;

    int get_active_row_from_text(Glib::ustring const &target_text, bool exclude = false, bool ignore_case = false) const;
    void defocus();

    void combo_box_changed_cb();
    bool combo_box_popup_cb();
    bool set_cell_markup();
    void entry_activate_cb();
    bool match_selected_cb(Gtk::TreeModel::iterator const &iter);
    bool keypress_cb(GtkEventControllerKey const *, unsigned keyval, unsigned, GdkModifierType);

    Glib::ustring check_comma_separated_text() const;
};

} // namespace Inkscape::UI::Widget

#endif // INKSCAPE_UI_WIDGET_COMBOBOXENTRYTOOLITEM_H

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
