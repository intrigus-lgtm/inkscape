// SPDX-License-Identifier: GPL-2.0-or-later

#include "icon-combobox.h"

#include <gtkmm/liststore.h>
#include <gtkmm/treemodelfilter.h>

namespace Inkscape::UI::Widget {

IconComboBox::IconComboBox()
{
    _model = Gtk::ListStore::create(_columns);

    pack_start(_renderer, false);
    _renderer.property_icon_size().set_value(Gtk::IconSize::NORMAL);
    _renderer.set_padding(2, 0);
    add_attribute(_renderer, "icon_name", _columns.icon_name);

    pack_start(_columns.label);

    _filter = Gtk::TreeModelFilter::create(_model);
    _filter->set_visible_column(_columns.is_visible);
    set_model(_filter);
}

IconComboBox::~IconComboBox() = default;

void IconComboBox::add_row(Glib::ustring const &icon_name, Glib::ustring const &label, int const id)
{
    Gtk::TreeModel::Row row = *_model->append();
    row[_columns.id] = id;
    row[_columns.icon_name] = icon_name;
    row[_columns.label] = ' ' + label;
    row[_columns.is_visible] = true;
}

void IconComboBox::set_active_by_id(int const id)
{
    for (auto i = _filter->children().begin(); i != _filter->children().end(); ++i) {
        const int data = (*i)[_columns.id];
        if (data == id) {
            set_active(i);
            break;
        }
    }
};

void IconComboBox::set_row_visible(int const id, bool const visible)
{
    auto active_id = get_active_row_id();
    for (auto &i : _model->children()) {
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

int IconComboBox::get_active_row_id() const
{
    if (auto it = get_active()) {
        return (*it)[_columns.id];
    }
    return -1;
}

} // namespace Inkscape::UI::Widget

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
