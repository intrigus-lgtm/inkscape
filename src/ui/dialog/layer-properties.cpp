// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Dialog for renaming layers.
 */
/* Author:
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *   Andrius R. <knutux@gmail.com>
 *   Abhishek Sharma
 *
 * Copyright (C) 2004 Bryce Harrington
 * Copyright (C) 2006 Andrius R.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <gtkmm/eventcontroller.h>

#include "layer-properties.h"
#include "desktop.h"
#include "document.h"
#include "document-undo.h"
#include "layer-manager.h"
#include "message-stack.h"
#include "object/sp-root.h"
#include "preferences.h"
#include "selection.h"
#include "ui/controller.h"
#include "ui/icon-names.h"
#include "ui/tools/tool-base.h"
#include "ui/widget/imagetoggler.h"

namespace Inkscape::UI::Dialog {

LayerPropertiesDialog::LayerPropertiesDialog(LayerPropertiesDialogType type)
    : _type{type}
    , _mainbox(Gtk::Orientation::VERTICAL)
    , _close_button(_("_Cancel"), true)
{
    set_name("LayerPropertiesDialog");

    set_child(_mainbox);
    _mainbox.set_margin(2);
    _mainbox.set_spacing(4);

    _layout_table.set_row_spacing(8);
    _layout_table.set_column_spacing(4);

    // Layer name widgets
    _layer_name_entry.set_activates_default(true);
    _layer_name_label.set_label(_("Layer name:"));
    _layer_name_label.set_halign(Gtk::Align::START);
    _layer_name_label.set_valign(Gtk::Align::CENTER);

    _layout_table.attach(_layer_name_label, 0, 0, 1, 1);
    
    _layer_name_entry.set_halign(Gtk::Align::FILL);
    _layer_name_entry.set_valign(Gtk::Align::FILL);
    _layer_name_entry.set_hexpand();
    _layout_table.attach(_layer_name_entry, 1, 0, 1, 1);

    _layout_table.set_expand();
    _mainbox.append(_layout_table);

    // Buttons
    _close_button.set_receives_default(true);

    _apply_button.set_use_underline(true);
    _apply_button.set_receives_default();

    _close_button.signal_clicked().connect([this] { destroy(); });
    _apply_button.signal_clicked().connect([this] { _apply(); });

    _mainbox.append(_buttonbox);
    _buttonbox.set_halign(Gtk::Align::END);
    _buttonbox.set_homogeneous();
    _buttonbox.set_spacing(4);

    _buttonbox.append(_close_button);
    _buttonbox.append(_apply_button);

    set_default_widget(_apply_button);
}

LayerPropertiesDialog::~LayerPropertiesDialog()
{
    _setLayer(nullptr);
    _setDesktop(nullptr);
}

/** Static member function which displays a modal dialog of the given type */
void LayerPropertiesDialog::_showDialog(LayerPropertiesDialogType type, SPDesktop *desktop, SPObject *layer)
{
    auto dialog = Gtk::manage(new LayerPropertiesDialog(type));

    dialog->_setDesktop(desktop);
    dialog->_setLayer(layer);

    dialog->_setup();

    dialog->set_modal(true);
    desktop->setWindowTransient(dialog->gobj());
    dialog->property_destroy_with_parent() = true;

    dialog->present();
}

/** Performs an action depending on the type of the dialog */
void LayerPropertiesDialog::_apply()
{
    switch (_type) {
        case LayerPropertiesDialogType::CREATE:
            _doCreate();
            break;

        case LayerPropertiesDialogType::MOVE:
            _doMove();
            break;

        case LayerPropertiesDialogType::RENAME:
            _doRename();
            break;

        case LayerPropertiesDialogType::NONE:
        default:
            break;
    }
}

/** Creates a new layer based on the input entered in the dialog window */
void LayerPropertiesDialog::_doCreate()
{
    LayerRelativePosition position = LPOS_ABOVE;

    if (_position_visible) {
        int index = 0;
        if (_layer_position_radio[1].get_active()) {
            position = LPOS_CHILD;
            index = 1;
        } else if (_layer_position_radio[2].get_active()) {
            position = LPOS_BELOW;
            index = 2;
        }
        Preferences::get()->setInt("/dialogs/layerProp/addLayerPosition", index);
    }

    Glib::ustring name(_layer_name_entry.get_text());
    if (name.empty()) {
        return;
    }

    auto root = _desktop->getDocument()->getRoot();
    SPObject *new_layer = Inkscape::create_layer(root, _layer, position);

    if (!name.empty()) {
        _desktop->layerManager().renameLayer(new_layer, name.c_str(), true);
    }
    _desktop->getSelection()->clear();
    _desktop->layerManager().setCurrentLayer(new_layer);
    DocumentUndo::done(_desktop->getDocument(), _("Add layer"), INKSCAPE_ICON("layer-new"));
    _desktop->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("New layer created."));
}

/** Moves selection to the chosen layer */
void LayerPropertiesDialog::_doMove()
{
    if (auto moveto = _selectedLayer()) {
        _desktop->getSelection()->toLayer(moveto);
        DocumentUndo::done(_desktop->getDocument(), _("Move selection to layer"), INKSCAPE_ICON("selection-move-to-layer"));
    }
}

/** Renames a layer based on the user input in the dialog window */
void LayerPropertiesDialog::_doRename()
{
    Glib::ustring name(_layer_name_entry.get_text());
    if (name.empty()) {
        return;
    }
    LayerManager &layman = _desktop->layerManager();
    layman.renameLayer(layman.currentLayer(), name.c_str(), false);

    DocumentUndo::done(_desktop->getDocument(), _("Rename layer"), INKSCAPE_ICON("layer-rename"));
    // TRANSLATORS: This means "The layer has been renamed"
    _desktop->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("Renamed layer"));
}

/** Sets up the dialog depending on its type */
void LayerPropertiesDialog::_setup()
{
    g_assert(_desktop);
    LayerManager &layman = _desktop->layerManager();

    switch (_type) {
        case LayerPropertiesDialogType::CREATE: {
            set_title(_("Add Layer"));
            Glib::ustring new_name = layman.getNextLayerName(nullptr, layman.currentLayer()->label());
            _layer_name_entry.set_text(new_name);
            _apply_button.set_label(_("_Add"));
            _setup_position_controls();
            break;
        }

        case LayerPropertiesDialogType::MOVE: {
            set_title(_("Move to Layer"));
            _layer_name_entry.set_text(_("Layer"));
            _apply_button.set_label(_("_Move"));
            _apply_button.set_sensitive(layman.getLayerCount());
            _setup_layers_controls();
            break;
        }

        case LayerPropertiesDialogType::RENAME: {
            set_title(_("Rename Layer"));
            gchar const *name = layman.currentLayer()->label();
            _layer_name_entry.set_text(name ? name : _("Layer"));
            _apply_button.set_label(_("_Rename"));
            break;
        }

        case LayerPropertiesDialogType::NONE:
        default:
            break;
    }
}

/** Sets up the combo box for choosing the relative position of the new layer */
void LayerPropertiesDialog::_setup_position_controls()
{
    if (!_layer || _desktop->getDocument()->getRoot() == _layer) {
        // no layers yet, so option above/below/sublayer is not applicable
        return;
    }

    _position_visible = true;

    _layer_position_label.set_label(_("Position:"));
    _layer_position_label.set_halign(Gtk::Align::START);
    _layer_position_label.set_valign(Gtk::Align::START);
    _layout_table.attach(_layer_position_label, 0, 1, 1, 1);

    int position = Preferences::get()->getIntLimited("/dialogs/layerProp/addLayerPosition", 0, 0, 2);

    _layer_position_radio[1].set_group(_layer_position_radio[0]);
    _layer_position_radio[2].set_group(_layer_position_radio[0]);
    _layer_position_radio[0].set_label(_("Above current"));
    _layer_position_radio[1].set_label(_("As sublayer of current"));
    _layer_position_radio[1].get_style_context()->add_class("indent");
    _layer_position_radio[2].set_label(_("Below current"));
    _layer_position_radio[0].set_active(position == LPOS_ABOVE);
    _layer_position_radio[1].set_active(position == LPOS_CHILD);
    _layer_position_radio[2].set_active(position == LPOS_BELOW);

    auto &vbox = *Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 3);
    vbox.append(_layer_position_radio[0]);
    vbox.append(_layer_position_radio[1]);
    vbox.append(_layer_position_radio[2]);

    _layout_table.attach(vbox, 1, 1, 1, 1);
}

/** Sets up the tree view of current layers */
void LayerPropertiesDialog::_setup_layers_controls()
{
    _store = Gtk::TreeStore::create(_model);
    _tree.set_model(_store);
    _tree.set_headers_visible(false);

    auto const eyeRenderer = Gtk::make_managed<UI::Widget::ImageToggler>(INKSCAPE_ICON("object-visible"),
                                                                         INKSCAPE_ICON("object-hidden"));
    int visibleColNum = _tree.append_column("vis", *eyeRenderer) - 1;
    Gtk::TreeViewColumn *col = _tree.get_column(visibleColNum);
    if (col) {
        col->add_attribute(eyeRenderer->property_active(), _model.visible);
    }

    auto const renderer = Gtk::make_managed<UI::Widget::ImageToggler>(INKSCAPE_ICON("object-locked"),
                                                                      INKSCAPE_ICON("object-unlocked"));
    int lockedColNum = _tree.append_column("lock", *renderer) - 1;
    col = _tree.get_column(lockedColNum);
    if (col) {
        col->add_attribute(renderer->property_active(), _model.locked);
    }

    auto const _text_renderer = Gtk::make_managed<Gtk::CellRendererText>();
    int nameColNum = _tree.append_column("Name", *_text_renderer) - 1;
    Gtk::TreeView::Column *_name_column = _tree.get_column(nameColNum);
    _name_column->add_attribute(_text_renderer->property_text(), _model.label);

    _tree.set_expander_column(*_tree.get_column(nameColNum));

    Controller::add_key<&LayerPropertiesDialog::on_key_pressed>(_tree, *this);
    Controller::add_click(_tree, sigc::mem_fun(*this, &LayerPropertiesDialog::on_click_pressed),
                          {}, Controller::Button::left);

    _scroller.set_child(_tree);
    _scroller.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    _scroller.set_has_frame(true);
    _scroller.set_size_request(220, 180);

    SPDocument* document = _desktop->doc();
    SPRoot* root = document->getRoot();
    if (root) {
        SPObject* target = _desktop->layerManager().currentLayer();
        _store->clear();
        _addLayer(root, nullptr, target, 0);
    }

    _layout_table.remove(_layer_name_entry);
    _layout_table.remove(_layer_name_label);

    _scroller.set_halign(Gtk::Align::FILL);
    _scroller.set_valign(Gtk::Align::FILL);
    _scroller.set_hexpand();
    _scroller.set_vexpand();
    _scroller.set_propagate_natural_width(true);
    _scroller.set_propagate_natural_height(true);
    _layout_table.attach(_scroller, 0, 1, 2, 1);
}

/** Inserts the new layer into the document */
void LayerPropertiesDialog::_addLayer(SPObject* layer, Gtk::TreeModel::Row* parentRow, SPObject* target,
                                      int level)
{
    constexpr int max_nest_depth = 20;
    if (!_desktop || !layer || level >= max_nest_depth) {
        g_warn_message("Inkscape", __FILE__, __LINE__, __func__, "Maximum layer nesting reached.");
        return;
    }
    LayerManager &layman = _desktop->layerManager();
    unsigned int counter = layman.childCount(layer);
    for (unsigned int i = 0; i < counter; i++) {
        SPObject *child = _desktop->layerManager().nthChildOf(layer, i);
        if (!child) {
            continue;
        }
#if DUMP_LAYERS
        g_message(" %3d    layer:%p  {%s}   [%s]", level, child, child->id, child->label() );
#endif // DUMP_LAYERS

        Gtk::TreeModel::iterator iter = parentRow ? _store->prepend(parentRow->children()) : _store->prepend();
        Gtk::TreeModel::Row row = *iter;
        row[_model.object] = child;
        row[_model.label] = child->label() ? child->label() : child->getId();
        row[_model.visible] = is<SPItem>(child) ? !cast_unsafe<SPItem>(child)->isHidden() : false;
        row[_model.locked] = is<SPItem>(child) ? cast_unsafe<SPItem>(child)->isLocked() : false;

        if (target && child == target) {
            _tree.expand_to_path(_store->get_path(iter));
            Glib::RefPtr<Gtk::TreeSelection> select = _tree.get_selection();
            select->select(iter);
        }

        _addLayer(child, &row, target, level + 1);
    }
}

SPObject *LayerPropertiesDialog::_selectedLayer()
{
    if (auto iter = _tree.get_selection()->get_selected()) {
        return (*iter)[_model.object];
    }

    return nullptr;
}

bool LayerPropertiesDialog::on_key_pressed(GtkEventControllerKey const * const controller,
                                           unsigned const keyval, unsigned const keycode,
                                           GdkModifierType const state)
{
    auto const latin_keyval = Inkscape::UI::Tools::get_latin_keyval(controller, keyval, keycode, state);
    switch (latin_keyval) {
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter: {
            _apply();
            return true;
        }
    }
    return false;
}

Gtk::EventSequenceState LayerPropertiesDialog::on_click_pressed(Gtk::GestureClick const & /*click*/,
                                                                int const n_press, double /*x*/, double /*y*/)
{
    if (n_press == 2) {
        _apply();
        return Gtk::EventSequenceState::CLAIMED;
    }
    return Gtk::EventSequenceState::NONE;
}

/** Formats the label for a given layer row
 */
void LayerPropertiesDialog::_prepareLabelRenderer(Gtk::TreeModel::const_iterator const &iter)
{
    _label_renderer.property_markup() = (*iter)[_dropdown_columns.name];
}

void LayerPropertiesDialog::_setLayer(SPObject *layer)
{
    if (layer == _layer) {
        return;
    }

    if (_layer) {
        sp_object_unref(_layer, nullptr);
    }

    _layer = layer;

    if (_layer) {
        sp_object_ref(_layer, nullptr);
    }
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
