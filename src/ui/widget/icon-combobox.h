// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef ICON_COMBO_BOX_SEEN_
#define ICON_COMBO_BOX_SEEN_

#include <gtkmm/combobox.h>
#include <gtkmm/liststore.h>
#include <gtkmm/treemodelfilter.h>

namespace Inkscape {
namespace UI {
namespace Widget {

class IconComboBox : public Gtk::ComboBox
{
public:
    IconComboBox() {
        _model = Gtk::ListStore::create(_columns);

        pack_start(_renderer, false);
        _renderer.set_property("stock_size", Gtk::IconSize::NORMAL);
        _renderer.set_padding(2, 0);
        add_attribute(_renderer, "icon_name", _columns.icon_name);

        pack_start(_columns.label);

        _filter = Gtk::TreeModelFilter::create(_model);
        _filter->set_visible_column(_columns.is_visible);
        set_model(_filter);
    }

    void add_row(const Glib::ustring& icon_name, const Glib::ustring& label, int id) {
        Gtk::TreeModel::Row row = *_model->append();
        row[_columns.id] = id;
        row[_columns.icon_name] = icon_name;
        row[_columns.label] = ' ' + label;
        row[_columns.is_visible] = true;
    }

    void set_active_by_id(int id) {
        for (auto i = _filter->children().begin(); i != _filter->children().end(); ++i) {
            const int data = (*i)[_columns.id];
            if (data == id) {
                set_active(i);
                break;
            }
        }
    };

    void set_row_visible(int id, bool visible = true) {
        auto active_id = get_active_row_id();
        for (const auto & i : _model->children()) {
            const int data = i[_columns.id];
            if (data == id) {
                i[_columns.is_visible] = visible;
            }
        }
        _filter->refilter();

        // Reset the selected row if needed
        if (active_id == id) {
            for (const auto & i : _filter->children()) {
                const int data = i[_columns.id];
                set_active_by_id(data);
                break;
            }
        }
    }

    int get_active_row_id() const {
        if (auto it = get_active()) {
            return (*it)[_columns.id];
        }
        return -1;
    }

private:
    class Columns : public Gtk::TreeModel::ColumnRecord
    {
    public:
        Columns() {
            add(icon_name);
            add(label);
            add(id);
            add(is_visible);
        }

        Gtk::TreeModelColumn<Glib::ustring> icon_name;
        Gtk::TreeModelColumn<Glib::ustring> label;
        Gtk::TreeModelColumn<int> id;
        Gtk::TreeModelColumn<bool> is_visible;
    };

    Columns _columns;
    Glib::RefPtr<Gtk::ListStore> _model;
    Glib::RefPtr<Gtk::TreeModelFilter> _filter;
    Gtk::CellRendererPixbuf _renderer;
};

}}}

#endif
