// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Inkscape::Widgets::PageSelector - select and move to pages
 *
 * Authors:
 *   Martin Owens
 *
 * Copyright (C) 2021 Martin Owens
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "page-selector.h"

#include <cstring>
#include <string>
#include <glibmm/i18n.h>

#include "desktop.h"
#include "document.h"
#include "object/sp-namedview.h"
#include "object/sp-page.h"
#include "page-manager.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/pack.h"

namespace Inkscape {
namespace UI {
namespace Widget {

PageSelector::PageSelector(SPDesktop *desktop)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL)
    , _desktop(desktop)
{
    set_name("PageSelector");

    _prev_button.set_image_from_icon_name(INKSCAPE_ICON("pan-start"), Gtk::IconSize::NORMAL);
    _prev_button.set_has_frame(false);
    _prev_button.set_tooltip_text(_("Move to previous page"));
    _prev_button.signal_clicked().connect(sigc::mem_fun(*this, &PageSelector::prevPage));

    _next_button.set_image_from_icon_name(INKSCAPE_ICON("pan-end"), Gtk::IconSize::NORMAL);
    _next_button.set_has_frame(false);
    _next_button.set_tooltip_text(_("Move to next page"));
    _next_button.signal_clicked().connect(sigc::mem_fun(*this, &PageSelector::nextPage));

    _selector.set_tooltip_text(_("Current page"));

    _page_model = Gtk::ListStore::create(_model_columns);
    _selector.set_model(_page_model);
    _selector.pack_start(_label_renderer);
    _selector.set_cell_data_func(_label_renderer, sigc::mem_fun(*this, &PageSelector::renderPageLabel));

    _selector_changed_connection =
        _selector.signal_changed().connect(sigc::mem_fun(*this, &PageSelector::setSelectedPage));

    UI::pack_start(*this, _prev_button, UI::PackOptions::expand_padding);
    UI::pack_start(*this, _selector, UI::PackOptions::expand_widget);
    UI::pack_start(*this, _next_button, UI::PackOptions::expand_padding);

    _doc_replaced_connection =
        _desktop->connectDocumentReplaced(sigc::hide<0>(sigc::mem_fun(*this, &PageSelector::setDocument)));

    setDocument(desktop->getDocument());
}

PageSelector::~PageSelector()
{
    _doc_replaced_connection.disconnect();
    _selector_changed_connection.disconnect();
    setDocument(nullptr);
}

void PageSelector::setDocument(SPDocument *document)
{
    _document = document;
    _pages_changed_connection.disconnect();
    _page_selected_connection.disconnect();
    if (document) {
        auto &page_manager = document->getPageManager();
        _pages_changed_connection =
            page_manager.connectPagesChanged(sigc::mem_fun(*this, &PageSelector::pagesChanged));
        _page_selected_connection =
            page_manager.connectPageSelected(sigc::mem_fun(*this, &PageSelector::selectonChanged));
        pagesChanged();
    }
}

void PageSelector::pagesChanged()
{
    _selector_changed_connection.block();
    auto &page_manager = _document->getPageManager();

    // Destroy all existing pages in the model.
    while (!_page_model->children().empty()) {
        Gtk::ListStore::iterator row(_page_model->children().begin());
        // Put cleanup here if any
        _page_model->erase(row);
    }

    // Hide myself when there's no pages (single page document)
    this->set_visible(page_manager.hasPages());

    // Add in pages, do not use getResourcelist("page") because the items
    // are not guaranteed to be in node order, they are in first-seen order.
    for (auto &page : page_manager.getPages()) {
        Gtk::ListStore::iterator row(_page_model->append());
        row->set_value(_model_columns.object, page);
    }

    selectonChanged(page_manager.getSelected());

    _selector_changed_connection.unblock();
}

void PageSelector::selectonChanged(SPPage *page)
{
    _selector_changed_connection.block();
    _next_button.set_sensitive(_document->getPageManager().hasNextPage());
    _prev_button.set_sensitive(_document->getPageManager().hasPrevPage());

    auto active = _selector.get_active();

    if (!active || active->get_value(_model_columns.object) != page) {
        for (auto row : _page_model->children()) {
            if (page == row.get_value(_model_columns.object)) {
                _selector.set_active(row.get_iter());
                break;
            }
        }
    }
    _selector_changed_connection.unblock();
}

/**
 * Render the page icon into a suitable label.
 */
void PageSelector::renderPageLabel(Gtk::TreeModel::const_iterator const &row)
{
    SPPage *page = (*row)[_model_columns.object];

    if (page && page->getRepr()) {
        int page_num = page->getPagePosition();

        gchar *format;
        if (auto label = page->label()) {
            format = g_strdup_printf("<span size=\"smaller\"><tt>%d.</tt>%s</span>", page_num, label);
        } else {
            format = g_strdup_printf("<span size=\"smaller\"><i>%s</i></span>", page->getDefaultLabel().c_str());
        }

        _label_renderer.property_markup() = format;
        g_free(format);
    } else {
        _label_renderer.property_markup() = "⚠️";
    }

    _label_renderer.property_ypad() = 1;
}

void PageSelector::setSelectedPage()
{
    SPPage *page = _selector.get_active()->get_value(_model_columns.object);
    if (page && _document->getPageManager().selectPage(page)) {
        _document->getPageManager().zoomToSelectedPage(_desktop);
    }
}

void PageSelector::nextPage()
{
    if (_document->getPageManager().selectNextPage()) {
        _document->getPageManager().zoomToSelectedPage(_desktop);
    }
}

void PageSelector::prevPage()
{
    if (_document->getPageManager().selectPrevPage()) {
        _document->getPageManager().zoomToSelectedPage(_desktop);
    }
}

} // namespace Widget
} // namespace UI
} // namespace Inkscape

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
