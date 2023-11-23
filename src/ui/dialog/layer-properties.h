// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief  Dialog for renaming layers
 */
/* Author:
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *
 * Copyright (C) 2004 Bryce Harrington
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_DIALOG_LAYER_PROPERTIES_H
#define INKSCAPE_DIALOG_LAYER_PROPERTIES_H

#include <gdk/gdk.h> // GdkModifierType
#include <gtk/gtk.h> // GtkEventControllerKey
#include <glibmm/refptr.h>
#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/combobox.h>
#include <gtkmm/entry.h>
#include <gtkmm/gesture.h> // Gtk::EventSequenceState
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/treestore.h>
#include <gtkmm/treeview.h>
#include <gtkmm/window.h>

#include "layer-manager.h"

namespace Gtk {
class GestureClick;
}

class SPDesktop;

namespace Inkscape::UI::Dialog {

/* FIXME: split the LayerPropertiesDialog class into three separate dialogs */
enum class LayerPropertiesDialogType
{
    NONE,
    CREATE,
    MOVE,
    RENAME
};

class LayerPropertiesDialog final : public Gtk::Window
{
public:
    static void showRename(SPDesktop *desktop, SPObject *layer) {
        _showDialog(LayerPropertiesDialogType::RENAME, desktop, layer);
    }
    static void showCreate(SPDesktop *desktop, SPObject *layer) {
        _showDialog(LayerPropertiesDialogType::CREATE, desktop, layer);
    }
    static void showMove(SPDesktop *desktop, SPObject *layer) {
        _showDialog(LayerPropertiesDialogType::MOVE, desktop, layer);
    }

private:
    explicit LayerPropertiesDialog(LayerPropertiesDialogType type);
    ~LayerPropertiesDialog() override;

    LayerPropertiesDialogType _type = LayerPropertiesDialogType::NONE;
    SPDesktop *_desktop = nullptr;
    SPObject *_layer = nullptr;

    struct PositionDropdownColumns : Gtk::TreeModel::ColumnRecord
    {
        Gtk::TreeModelColumn<LayerRelativePosition> position;
        Gtk::TreeModelColumn<Glib::ustring> name;

        PositionDropdownColumns()
        {
            add(position);
            add(name);
        }
    };

    Gtk::Label        _layer_name_label;
    Gtk::Entry        _layer_name_entry;
    Gtk::Label        _layer_position_label;
    Gtk::CheckButton  _layer_position_radio[3];
    Gtk::Grid         _layout_table;

    bool              _position_visible = false;

    struct ModelColumns : Gtk::TreeModel::ColumnRecord
    {
        Gtk::TreeModelColumn<SPObject *> object;
        Gtk::TreeModelColumn<Glib::ustring> label;
        Gtk::TreeModelColumn<bool> visible;
        Gtk::TreeModelColumn<bool> locked;

        ModelColumns()
        {
            add(object);
            add(visible);
            add(locked);
            add(label);
        }
    };

    Gtk::Box _mainbox;
    Gtk::Box _buttonbox;

    Gtk::TreeView _tree;
    ModelColumns _model;
    Glib::RefPtr<Gtk::TreeStore> _store;
    Gtk::ScrolledWindow _scroller;

    PositionDropdownColumns _dropdown_columns;
    Gtk::CellRendererText _label_renderer;

    Gtk::Button _close_button;
    Gtk::Button _apply_button;

    void _setDesktop(SPDesktop *desktop) { _desktop = desktop; };
    void _setLayer(SPObject *layer);

    static void _showDialog(LayerPropertiesDialogType type, SPDesktop *desktop, SPObject *layer);
    void _apply();

    void _setup_position_controls();
    void _setup_layers_controls();
    void _prepareLabelRenderer(Gtk::TreeModel::const_iterator const &iter);

    void _addLayer(SPObject* layer, Gtk::TreeModel::Row* parentRow, SPObject* target, int level);
    SPObject* _selectedLayer();

    bool on_key_pressed(GtkEventControllerKey const *controller,
                        unsigned keyval, unsigned keycode,
                        GdkModifierType state);
    Gtk::EventSequenceState on_click_pressed(Gtk::GestureClick const &click,
                                             int n_press, double x, double y);

    void _doCreate();
    void _doMove();
    void _doRename();
    void _setup();
};

} // namespace Inkscape::UI::Dialog

#endif // INKSCAPE_DIALOG_LAYER_PROPERTIES_H

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
