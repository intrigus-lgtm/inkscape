// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Extensions gallery
 */
/* Authors:
 *   Mike Kowalski
 *
 * Copyright (C) 2023 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_DIALOG_EXTENSIONS_H
#define INKSCAPE_UI_DIALOG_EXTENSIONS_H

#include <gdkmm/texture.h>
#include <giomm/liststore.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/boolfilter.h>
#include <gtkmm/cellrendererpixbuf.h>
#include <gtkmm/filterlistmodel.h>
#include <gtkmm/gridview.h>
#include <gtkmm/iconview.h>
#include <gtkmm/label.h>
#include <gtkmm/multiselection.h>
#include <gtkmm/signallistitemfactory.h>
#include <gtkmm/singleselection.h>
#include <gtkmm/treemodel.h>
#include <boost/compute/detail/lru_cache.hpp>
#include <memory>

#include "helper/auto-connection.h"
#include "ui/dialog/dialog-base.h"
#include "ui/iconview-item-factory.h"

namespace Gtk {
class Builder;
class Button;
class IconView;
class ListStore;
class SearchEntry2;
class TreeSelection;
class TreeView;
} // namespace Gtk

namespace Inkscape::Extension {
class Effect;
}

namespace Inkscape::UI::Dialog {

class ExtensionsGallery : public DialogBase
{
public:
    enum Type { Filters, Effects };
    ExtensionsGallery(Type type);

private:
    Glib::RefPtr<Gtk::Builder> _builder;
    Gtk::GridView& _gridview;
    Gtk::SearchEntry2& _search;
    Gtk::TreeView& _selector;
    Gtk::Button& _run;
    Gtk::Label& _run_btn_label;
    Glib::ustring _run_label;
    Glib::RefPtr<Gtk::BoolFilter> _filter;
    Glib::RefPtr<Gtk::ListStore> _categories;
    auto_connection _selection_change;
    Glib::RefPtr<Gtk::TreeSelection> _page_selection;
    Glib::ustring _current_category;
    int _thumb_size_index = 0;
    Type _type;
    boost::compute::detail::lru_cache<std::string, Glib::RefPtr<Gdk::Texture>> _image_cache;
    Cairo::RefPtr<Cairo::ImageSurface> _blank_image;
    Glib::RefPtr<Gtk::FilterListModel> _filtered_model;
    Glib::RefPtr<Gtk::SingleSelection> _selection_model;
    std::unique_ptr<IconViewItemFactory> _factory;

    Glib::RefPtr<Gdk::Texture> get_image(const std::string& key, const std::string& icon, Extension::Effect* effect);
    bool is_item_visible(const Glib::RefPtr<Glib::ObjectBase>& item) const;
    Gtk::TreeRow selected_item();
    void update_name();
    void show_category(const Glib::ustring& id);
    void refilter();
    void rebuild();
};

} // namespace Inkscape::UI::Dialog

#endif // INKSCAPE_UI_DIALOG_EXTENSIONS_H
