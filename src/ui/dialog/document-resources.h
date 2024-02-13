// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A simple dialog for previewing document resources
 *
 * Copyright (C) 2023 Michael Kowalski
 */

#ifndef SEEN_DOC_RESOURCES_H
#define SEEN_DOC_RESOURCES_H

#include <cstddef>
#include <giomm/liststore.h>
#include <gtkmm/boolfilter.h>
#include <gtkmm/columnview.h>
#include <gtkmm/gridview.h>
#include <gtkmm/listview.h>
#include <gtkmm/singleselection.h>
#include <memory>
#include <string>
#include <boost/ptr_container/ptr_vector.hpp>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/cellrendererpixbuf.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/treeview.h>

#include "helper/auto-connection.h"
#include "ui/dialog/dialog-base.h"
#include "ui/iconview-item-factory.h"
#include "ui/text_filter.h"
#include "ui/widget/entity-entry.h"
#include "ui/widget/registry.h"

namespace Glib {
class ustring;
} // namespace Glib

namespace Gtk {
class Builder;
class Button;
class CellEditable;
class CellRendererText;
class ColumnView;
class IconView;
class ListStore;
class SearchEntry2;
class TreeSelection;
class TreeView;
} // namespace Gtk

namespace Inkscape::UI::Dialog {

namespace details {

struct Statistics {
    std::size_t nodes = 0;
    std::size_t groups = 0;
    std::size_t layers = 0;
    std::size_t paths = 0;
    std::size_t images = 0;
    std::size_t patterns = 0;
    std::size_t symbols = 0;
    std::size_t markers = 0;
    std::size_t fonts = 0;
    std::size_t filters = 0;
    std::size_t svg_fonts = 0;
    std::size_t colors = 0;
    std::size_t gradients = 0;
    std::size_t swatches = 0;
    std::size_t metadata = 0;
    std::size_t styles = 0;
    std::size_t meshgradients = 0;
    std::size_t colorprofiles = 0;
    std::size_t external_uris = 0;
};

struct ResourceItem;

} // namespace details

class DocumentResources final : public DialogBase {
public:
    DocumentResources();

private:
    void documentReplaced() override;
    void select_page(const Glib::ustring& id);
    void refresh_page(const Glib::ustring& id);
    void refresh_current_page();
    void rebuild_stats();
    details::Statistics collect_statistics();
    void end_editing(const Glib::ustring& path, const Glib::ustring& new_text);
    void selectionModified(Inkscape::Selection *selection, unsigned flags) override;
    void update_buttons();
    std::shared_ptr<details::ResourceItem> selected_item();
    void clear_stores();

    Glib::RefPtr<Gtk::Builder> _builder;
    Glib::RefPtr<Gio::ListStoreBase> _item_store;
    Glib::RefPtr<Gtk::TreeModelFilter> _categories;
    Glib::RefPtr<Gtk::BoolFilter> _info_filter;
    std::unique_ptr<TextMatchingFilter> _item_filter;
    Glib::RefPtr<Gio::ListStoreBase> _info_store;
    Gtk::CellRendererPixbuf _image_renderer;
    SPDocument* _document = nullptr;
    auto_connection _selection_change;
    details::Statistics _stats;
    std::string _cur_page_id; // the last category that user selected
    int _showing_resource = -1; // ID of the resource that's currently presented
    std::unique_ptr<IconViewItemFactory> _item_factory;
    Gtk::GridView& _gridview;
    Glib::RefPtr<Gtk::SingleSelection> _item_selection_model;
    Gtk::ColumnView& _listview;
    Gtk::ListView& _selector;
    Gtk::Button& _edit;
    Gtk::Button& _select;
    Gtk::Button& _delete;
    Gtk::Button& _extract;
    Gtk::SearchEntry2& _search;
    boost::ptr_vector<Inkscape::UI::Widget::EntityEntry> _rdf_list;
    UI::Widget::Registry _wr;
    Gtk::CellRendererText* _label_renderer;
    auto_connection _document_modified;
    auto_connection _idle_refresh;
    Glib::RefPtr<Gtk::BoolFilter> _filter;
    Glib::RefPtr<Gtk::SingleSelection> _selection_model;
};

} // namespace Inkscape::UI::Dialog

#endif // SEEN_DOC_RESOURCES_H

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
