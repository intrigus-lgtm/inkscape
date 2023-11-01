// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
    A combobox that can be displayed in a toolbar.
*/
/*
 * Authors:
 *   Tavmjong Bah <tavmjong@free.fr>
 *
 * Copyright  (C) 2017 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "combo-tool-item.h"

#include <utility>
#include <vector>
#include <sigc++/functors/mem_fun.h>
#include <gdkmm/pixbuf.h>
#include <gtkmm/cellrendererpixbuf.h>
#include <gtkmm/box.h>
#include <gtkmm/combobox.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>

#include "preferences.h"
#include "ui/pack.h"

namespace Inkscape::UI::Widget {

static void strip_trailing(Glib::ustring &s, char const c)
{
    if (!s.empty() && s.raw().back() == c) {
        s.resize(s.size() - 1);
    }
}

ComboToolItem*
ComboToolItem::create(const Glib::ustring &group_label,
                      const Glib::ustring &tooltip,
                      const Glib::ustring &stock_id,
                      Glib::RefPtr<Gtk::ListStore> store,
                      bool                 has_entry)
{
    return new ComboToolItem(group_label, tooltip, stock_id, std::move(store), has_entry);
}

ComboToolItem::ComboToolItem(Glib::ustring group_label,
                             Glib::ustring tooltip,
                             Glib::ustring stock_id,
                             Glib::RefPtr<Gtk::ListStore> store,
                             bool          has_entry) :
    _active(-1),
    _group_label(std::move( group_label )),
    _tooltip(std::move( tooltip )),
    _stock_id(std::move( stock_id )),
    _store (std::move(store)),
    _use_label (true),
    _use_icon  (false),
    _use_pixbuf (true),
    _icon_size ( Gtk::IconSize::LARGE ),
    _combobox (nullptr),
    _container(Gtk::make_managed<Gtk::Box>())
{
    append(*_container);
    _container->set_spacing(3);

    // ": " is added to the group label later
    // If we have them already, we strip them
    strip_trailing(_group_label, ' ');
    strip_trailing(_group_label, ':');

    _combobox = Gtk::make_managed<Gtk::ComboBox>(has_entry);
    _combobox->set_model(_store);

    populate_combobox();

    _combobox->signal_changed().connect(
            sigc::mem_fun(*this, &ComboToolItem::on_changed_combobox));
    UI::pack_start(*_container, *_combobox);
}

ComboToolItem::~ComboToolItem() = default;

void
ComboToolItem::focus_on_click( bool focus_on_click )
{ 
    _combobox->set_focus_on_click(focus_on_click); 
}
    

void
ComboToolItem::use_label(bool use_label)
{
    _use_label = use_label;
    populate_combobox();
}

void
ComboToolItem::use_icon(bool use_icon)
{
    _use_icon = use_icon;
    populate_combobox();
}

void
ComboToolItem::use_pixbuf(bool use_pixbuf)
{
    _use_pixbuf = use_pixbuf;
    populate_combobox();
}

void
ComboToolItem::use_group_label(bool use_group_label)
{
    if (use_group_label == (_group_label_widget != nullptr)) {
        return;
    }

    if (use_group_label) {
        _combobox->reference();
        _container->remove(*_combobox);
        _group_label_widget = std::make_unique<Gtk::Label>(_group_label + ": ");
        UI::pack_start(*_container, *_group_label_widget);
        UI::pack_start(*_container, *_combobox);
        _combobox->unreference();
    } else {
        _container->remove(*_group_label_widget);
        _group_label_widget.reset();
    }
}

void
ComboToolItem::populate_combobox()
{
    _combobox->clear();

    ComboToolItemColumns columns;

    if (_use_icon) {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        if (prefs->getBool("/theme/symbolicIcons", false)) {
            auto children = _store->children();
            for (auto row : children) {
                auto const &icon = row.get_value(columns.col_icon);
                auto const pos = icon.find("-symbolic");
                if (pos == icon.npos) {
                    row.set_value(columns.col_icon, icon + "-symbolic");
                }
            }
        }

        auto const renderer = Gtk::make_managed<Gtk::CellRendererPixbuf>();
        renderer->set_property ("stock_size", Gtk::IconSize::LARGE);
        _combobox->pack_start (*renderer, false);
        _combobox->add_attribute (*renderer, "icon_name", columns.col_icon );
    } else if (_use_pixbuf) {
        auto const renderer = Gtk::make_managed<Gtk::CellRendererPixbuf>();
        _combobox->pack_start (*renderer, false);
        _combobox->add_attribute (*renderer, "pixbuf", columns.col_pixbuf   );
    }

    if (_use_label) {
        _combobox->pack_start(columns.col_label);
    }

    std::vector<Gtk::CellRenderer*> cells = _combobox->get_cells();
    for (auto & cell : cells) {
        _combobox->add_attribute (*cell, "sensitive", columns.col_sensitive);
    }

    set_tooltip_text(_tooltip);
    _combobox->set_tooltip_text(_tooltip);
    _combobox->set_active (_active);
}

void
ComboToolItem::set_active(int active) {
    if (_active == active) return;

    _active = active;

    if (_combobox) {
        _combobox->set_active (active);
    }
}

Glib::ustring
ComboToolItem::get_active_text () {
    Gtk::TreeModel::Row row = _store->children()[_active];
    ComboToolItemColumns columns;
    return row.get_value(columns.col_label);
}

void
ComboToolItem::on_changed_combobox() {
    int row = _combobox->get_active_row_number();
    set_active( row );
    _changed.emit (_active);
    _changed_after.emit (_active);
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
