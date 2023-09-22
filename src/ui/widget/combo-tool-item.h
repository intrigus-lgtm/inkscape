// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
    A combobox that can be displayed in a toolbar.
    To be replaced by a Gtk::DropDown in Gtk4 (and remove container).
*/
/*
 * Authors:
 *   Tavmjong Bah <tavmjong@free.fr>
 *
 * Copyright  (C) 2017 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_COMBO_TOOL_ITEM
#define SEEN_COMBO_TOOL_ITEM

#include <memory>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/box.h>
#include <gtkmm/enums.h>
#include <gtkmm/treemodel.h>
#include <sigc++/signal.h>

namespace Gdk {
class Pixbuf;
} // namespace Gdk

namespace Gtk {
class ComboBox;
class Label;
class ListStore;
} // namespace Gtk

namespace Inkscape::UI::Widget {

class ComboToolItemColumns final : public Gtk::TreeModel::ColumnRecord {
public:
    ComboToolItemColumns() {
        add (col_label);
        add (col_value);
        add (col_icon);
        add (col_pixbuf);
        add (col_data);  // Used to store a pointer
        add (col_tooltip);
        add (col_sensitive);
    }
    Gtk::TreeModelColumn<Glib::ustring> col_label;
    Gtk::TreeModelColumn<Glib::ustring> col_value;
    Gtk::TreeModelColumn<Glib::ustring> col_icon;
    Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf> >   col_pixbuf;
    Gtk::TreeModelColumn<void *>        col_data;
    Gtk::TreeModelColumn<Glib::ustring> col_tooltip;
    Gtk::TreeModelColumn<bool>          col_sensitive;
};

class ComboToolItem final : public Gtk::Box {
public:
    static ComboToolItem* create(const Glib::ustring &label,
                                 const Glib::ustring &tooltip,
                                 const Glib::ustring &stock_id,
                                 Glib::RefPtr<Gtk::ListStore> store,
                                 bool                 has_entry = false);

    /* Style of combobox */
    void use_label(  bool use_label  );
    void use_icon(   bool use_icon   );
    void focus_on_click( bool focus_on_click );
    void use_pixbuf( bool use_pixbuf );
    void use_group_label( bool use_group_label ); // Applies to tool item only
  
    int get_active() const { return _active; }
    Glib::ustring get_active_text();
    void set_active(int active);
    void set_icon_size( Gtk::IconSize size ) { _icon_size = size; }

    Glib::RefPtr<Gtk::ListStore> const &get_store() { return _store; }

    sigc::signal<void (int)> signal_changed() { return _changed; }
    sigc::signal<void (int)> signal_changed_after() { return _changed_after; }

protected:
    void populate_combobox();

    /* Signals */
    sigc::signal<void (int)> _changed;
    sigc::signal<void (int)> _changed_after;  // Needed for unit tracker which eats _changed.

private:
    Glib::ustring _group_label;
    Glib::ustring _tooltip;
    Glib::ustring _stock_id;
    Glib::RefPtr<Gtk::ListStore> _store;

    int _active; // Active menu item/button

    /* Style */
    bool _use_label;
    bool _use_icon;   // Applies to menu item only
    bool _use_pixbuf;
    Gtk::IconSize _icon_size;

    /* Combobox in tool */
    Gtk::ComboBox* _combobox;
    std::unique_ptr<Gtk::Label> _group_label_widget;
    Gtk::Box* _container;

    /* Internal Callbacks */
    void on_changed_combobox();

    ComboToolItem(Glib::ustring group_label,
                  Glib::ustring tooltip,
                  Glib::ustring stock_id,
                  Glib::RefPtr<Gtk::ListStore> store,
                  bool          has_entry = false);
    ~ComboToolItem() final;
};

} // namespace Inkscape::UI::Widget

#endif /* SEEN_COMBO_TOOL_ITEM */

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
