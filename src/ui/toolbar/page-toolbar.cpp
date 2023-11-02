// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Page aux toolbar: Temp until we convert all toolbars to ui files with Gio::Actions.
 */
/* Authors:
 *   Martin Owens <doctormo@geek-2.com>
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>

 * Copyright (C) 2021 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "page-toolbar.h"

#include <regex>
#include <glibmm/i18n.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/entry.h>
#include <gtkmm/entrycompletion.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/popover.h>
#include <gtkmm/separator.h>
#include <sigc++/functors/mem_fun.h>

#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "extension/db.h"
#include "extension/template.h"
#include "io/resource.h"
#include "object/sp-page.h"
#include "ui/builder-utils.h"
#include "ui/icon-names.h"
#include "ui/popup-menu.h"
#include "ui/tools/pages-tool.h"
#include "ui/util.h"
#include "ui/widget/toolbar-menu-button.h"
#include "util/paper.h"
#include "util/units.h"

using Inkscape::IO::Resource::UIS;

namespace Inkscape::UI::Toolbar {

class SearchCols final : public Gtk::TreeModel::ColumnRecord
{
public:
    // These types must match those for the model in the ui file
    SearchCols()
    {
        add(name);
        add(label);
        add(key);
    }
    Gtk::TreeModelColumn<Glib::ustring> name;  // translated name
    Gtk::TreeModelColumn<Glib::ustring> label; // translated label
    Gtk::TreeModelColumn<Glib::ustring> key;
};

PageToolbar::PageToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    , _builder(create_builder("toolbar-page.ui"))
    , _combo_page_sizes(get_widget<Gtk::ComboBoxText>(_builder, "_combo_page_sizes"))
    , _text_page_margins(get_widget<Gtk::Entry>(_builder, "_text_page_margins"))
    , _margin_popover(get_widget<Gtk::Popover>(_builder, "margin_popover"))
    , _text_page_bleeds(get_widget<Gtk::Entry>(_builder, "_text_page_bleeds"))
    , _text_page_label(get_widget<Gtk::Entry>(_builder, "_text_page_label"))
    , _label_page_pos(get_widget<Gtk::Label>(_builder, "_label_page_pos"))
    , _btn_page_backward(get_widget<Gtk::Button>(_builder, "_btn_page_backward"))
    , _btn_page_foreward(get_widget<Gtk::Button>(_builder, "_btn_page_foreward"))
    , _btn_page_delete(get_widget<Gtk::Button>(_builder, "_btn_page_delete"))
    , _btn_move_toggle(get_widget<Gtk::Button>(_builder, "_btn_move_toggle"))
    , _sep1(get_widget<Gtk::Separator>(_builder, "_sep1"))
    , _sizes_list(get_object<Gtk::ListStore>(_builder, "_sizes_list"))
    , _sizes_search(get_object<Gtk::ListStore>(_builder, "_sizes_search"))
    , _margin_top(get_derived_widget<UI::Widget::MathSpinButton>(_builder, "_margin_top"))
    , _margin_right(get_derived_widget<UI::Widget::MathSpinButton>(_builder, "_margin_right"))
    , _margin_bottom(get_derived_widget<UI::Widget::MathSpinButton>(_builder, "_margin_bottom"))
    , _margin_left(get_derived_widget<UI::Widget::MathSpinButton>(_builder, "_margin_left"))
{
    _toolbar = &get_widget<Gtk::Box>(_builder, "page-toolbar");

    // Fetch all the ToolbarMenuButtons at once from the UI file
    // Menu Button #1
    auto popover_box1 = &get_widget<Gtk::Box>(_builder, "popover_box1");
    auto menu_btn1 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn1");

    // Initialize all the ToolbarMenuButtons only after all the children of the
    // toolbar have been fetched. Otherwise, the children to be moved in the
    // popover will get mapped to a different position and it will probably
    // cause segfault.
    auto children = UI::get_children(*_toolbar);

    menu_btn1->init(1, "tag1", popover_box1, children);
    addCollapsibleButton(menu_btn1);

    append(*_toolbar);

    _text_page_label.signal_changed().connect(sigc::mem_fun(*this, &PageToolbar::labelEdited));

    get_object<Gtk::EntryCompletion>(_builder, "_sizes_searcher")
        ->signal_match_selected()
        .connect(
            [this] (Gtk::TreeModel::iterator const &iter) {
                SearchCols cols;
                Gtk::TreeModel::Row row = *iter;
                Glib::ustring preset_key = row[cols.key];
                sizeChoose(preset_key);
                return false;
            },
            false);

    _text_page_bleeds.signal_activate().connect(sigc::mem_fun(*this, &PageToolbar::bleedsEdited));
    _text_page_margins.signal_activate().connect(sigc::mem_fun(*this, &PageToolbar::marginsEdited));
    _margin_popover.set_parent(*this);
    _text_page_margins.signal_icon_press().connect([&](Gtk::Entry::IconPosition) {
        if (auto page = _document->getPageManager().getSelected()) {
            auto margin = page->getMargin();
            auto unit = _document->getDisplayUnit()->abbr;
            auto scale = _document->getDocumentScale();
            _margin_top.set_value(margin.top().toValue(unit) * scale[Geom::Y]);
            _margin_right.set_value(margin.right().toValue(unit) * scale[Geom::X]);
            _margin_bottom.set_value(margin.bottom().toValue(unit) * scale[Geom::Y]);
            _margin_left.set_value(margin.left().toValue(unit) * scale[Geom::X]);
            _text_page_bleeds.set_text(page->getBleedLabel());
        }
        UI::popup_at(_margin_popover, _text_page_margins);
    });
    _margin_top.signal_value_changed().connect(sigc::mem_fun(*this, &PageToolbar::marginTopEdited));
    _margin_right.signal_value_changed().connect(sigc::mem_fun(*this, &PageToolbar::marginRightEdited));
    _margin_bottom.signal_value_changed().connect(sigc::mem_fun(*this, &PageToolbar::marginBottomEdited));
    _margin_left.signal_value_changed().connect(sigc::mem_fun(*this, &PageToolbar::marginLeftEdited));

    _combo_page_sizes.set_id_column(2);
    _combo_page_sizes.signal_changed().connect([this] {
        std::string preset_key = _combo_page_sizes.get_active_id();
        if (!preset_key.empty()) {
            sizeChoose(preset_key);
        }
    });

    _entry_page_sizes = dynamic_cast<Gtk::Entry *>(_combo_page_sizes.get_child());

    if (_entry_page_sizes) {
        _entry_page_sizes->set_placeholder_text(_("ex.: 100x100cm"));
        _entry_page_sizes->set_tooltip_text(_("Type in width & height of a page. (ex.: 15x10cm, 10in x 100mm)\n"
                                              "or choose preset from dropdown."));
        _entry_page_sizes->add_css_class("symbolic");
        _entry_page_sizes->signal_activate().connect(sigc::mem_fun(*this, &PageToolbar::sizeChanged));

        _entry_page_sizes->signal_icon_press().connect([this] (Gtk::Entry::IconPosition) {
            _document->getPageManager().changeOrientation();
            DocumentUndo::maybeDone(_document, "page-resize", _("Resize Page"), INKSCAPE_ICON("tool-pages"));
            setSizeText();
        });

        _entry_page_sizes->property_has_focus().signal_changed().connect([this] {
            if (!_document)
                return;
            auto const display_only = !has_focus();
            setSizeText(nullptr, display_only);
        });

        populate_sizes();
    }

    // Watch for when the tool changes
    _ec_connection = _desktop->connectEventContextChanged(sigc::mem_fun(*this, &PageToolbar::toolChanged));
    _doc_connection = _desktop->connectDocumentReplaced([=](SPDesktop *desktop, SPDocument *doc) {
        if (doc) {
            toolChanged(desktop, desktop->getTool());
        }
    });

    toolChanged(desktop, desktop->getTool());
}

/**
 * Take all selectable page sizes and add to search and dropdowns
 */
void PageToolbar::populate_sizes()
{
    SearchCols cols;

    Inkscape::Extension::DB::TemplateList extensions;
    Inkscape::Extension::db.get_template_list(extensions);

    for (auto tmod : extensions) {
        if (!tmod->can_resize())
            continue;
        for (auto preset : tmod->get_presets()) {
            auto label = preset->get_label();
            if (!label.empty()) label = _(label.c_str());

            if (preset->is_visible(Inkscape::Extension::TEMPLATE_SIZE_LIST)) {
                // Goes into drop down
                Gtk::TreeModel::Row row = *(_sizes_list->append());
                row[cols.name] = _(preset->get_name().c_str());
                row[cols.label] = " <small><span fgalpha=\"50%\">" + label + "</span></small>";
                row[cols.key] = preset->get_key();
            }
            if (preset->is_visible(Inkscape::Extension::TEMPLATE_SIZE_SEARCH)) {
                // Goes into text search
                Gtk::TreeModel::Row row = *(_sizes_search->append());
                row[cols.name] = _(preset->get_name().c_str());
                row[cols.label] = label;
                row[cols.key] = preset->get_key();
            }
        }
    }
}

PageToolbar::~PageToolbar()
{
    toolChanged(nullptr, nullptr);
}

void PageToolbar::toolChanged(SPDesktop *desktop, Inkscape::UI::Tools::ToolBase *tool)
{
    // Disconnect previous page changed signal
    _page_selected.disconnect();
    _pages_changed.disconnect();
    _page_modified.disconnect();
    _document = nullptr;

    if (dynamic_cast<Inkscape::UI::Tools::PagesTool *>(tool)) {
        // Save the document and page_manager for future use.
        if ((_document = desktop->getDocument())) {
            auto &page_manager = _document->getPageManager();
            // Connect the page changed signal and indicate changed
            _pages_changed = page_manager.connectPagesChanged(sigc::mem_fun(*this, &PageToolbar::pagesChanged));
            _page_selected = page_manager.connectPageSelected(sigc::mem_fun(*this, &PageToolbar::selectionChanged));
            // Update everything now.
            pagesChanged();
        }
    }
}

void PageToolbar::labelEdited()
{
    auto text = _text_page_label.get_text();
    if (auto page = _document->getPageManager().getSelected()) {
        page->setLabel(text.empty() ? nullptr : text.c_str());
        DocumentUndo::maybeDone(_document, "page-relabel", _("Relabel Page"), INKSCAPE_ICON("tool-pages"));
    }
}

void PageToolbar::bleedsEdited()
{
    auto text = _text_page_bleeds.get_text();

    // And modifiction to the bleed causes pages to be enabled
    auto &pm = _document->getPageManager();
    pm.enablePages();

    if (auto page = pm.getSelected()) {
        page->setBleed(text);
        DocumentUndo::maybeDone(_document, "page-bleed", _("Edit page bleed"), INKSCAPE_ICON("tool-pages"));
        _text_page_bleeds.set_text(page->getBleedLabel());
    }
}

void PageToolbar::marginsEdited()
{
    auto text = _text_page_margins.get_text();

    // And modifiction to the margin causes pages to be enabled
    auto &pm = _document->getPageManager();
    pm.enablePages();

    if (auto page = pm.getSelected()) {
        page->setMargin(text);
        DocumentUndo::maybeDone(_document, "page-margin", _("Edit page margin"), INKSCAPE_ICON("tool-pages"));
        setMarginText(page);
    }
}

void PageToolbar::marginTopEdited()
{
    marginSideEdited(0, _margin_top.get_text());
}
void PageToolbar::marginRightEdited()
{
    marginSideEdited(1, _margin_right.get_text());
}
void PageToolbar::marginBottomEdited()
{
    marginSideEdited(2, _margin_bottom.get_text());
}
void PageToolbar::marginLeftEdited()
{
    marginSideEdited(3, _margin_left.get_text());
}
void PageToolbar::marginSideEdited(int side, const Glib::ustring &value)
{
    // And modifiction to the margin causes pages to be enabled
    auto &pm = _document->getPageManager();
    pm.enablePages();

    if (auto page = pm.getSelected()) {
        page->setMarginSide(side, value, false);
        DocumentUndo::maybeDone(_document, "page-margin", _("Edit page margin"), INKSCAPE_ICON("tool-pages"));
        setMarginText(page);
    }
}

void PageToolbar::sizeChoose(const std::string &preset_key)
{
    if (auto preset = Extension::Template::get_any_preset(preset_key)) {
        auto &pm = _document->getPageManager();
        // The page orientation is a part of the toolbar widget, so we pass this
        // as a specially named pref, the extension can then decide to use it or not.
        auto p_rect = pm.getSelectedPageRect();
        std::string orient = p_rect.width() > p_rect.height() ? "land" : "port";

        auto page = pm.getSelected();
        preset->resize_to_template(_document, page, {
            {"orientation", orient},
        });
        if (page) {
            page->setSizeLabel(preset->get_name());
        }

        setSizeText();
        DocumentUndo::maybeDone(_document, "page-resize", _("Resize Page"), INKSCAPE_ICON("tool-pages"));
    } else {
        // Page not found, i.e., "Custom" was selected or user is typing in.
        _entry_page_sizes->grab_focus();
    }
}

/**
 * Convert the parsed sections of a text input into a desktop pixel value.
 */
double PageToolbar::_unit_to_size(std::string number, std::string unit_str,
                                  std::string const &backup)
{
    // We always support comma, even if not in that particular locale.
    std::replace(number.begin(), number.end(), ',', '.');
    double value = std::stod(number);

    // Get the best unit, for example 50x40cm means cm for both
    if (unit_str.empty() && !backup.empty())
        unit_str = backup;
    if (unit_str == "\"")
        unit_str = "in";

    // Output is always in px as it's the most useful.
    auto px = Inkscape::Util::UnitTable::get().getUnit("px");

    // Convert from user entered unit to display unit
    if (!unit_str.empty())
        return Inkscape::Util::Quantity::convert(value, unit_str, px);

    // Default unit is the document's display unit
    auto unit = _document->getDisplayUnit();
    return Inkscape::Util::Quantity::convert(value, unit, px);
}

/**
 * A manually typed input size, parse out what we can understand from
 * the text or ignore it if the text can't be parsed.
 *
 * Format: 50cm x 40mm
 *         20',40"
 *         30,4-40.2
 */
void PageToolbar::sizeChanged()
{
    // Parse the size out of the typed text if possible.
    Glib::ustring cb_text = std::string(_combo_page_sizes.get_active_text());

    // Replace utf8 x with regular x
    auto pos = cb_text.find_first_of("×");
    if (pos != cb_text.npos) {
        cb_text.replace(pos, 1, "x");
    }
    // Remove parens from auto generated names
    auto pos1 = cb_text.find_first_of("(");
    auto pos2 = cb_text.find_first_of(")");
    if (pos1 != cb_text.npos && pos2 != cb_text.npos && pos1 < pos2) {
        cb_text = cb_text.substr(pos1+1, pos2-pos1-1);
    }
    std::string text = cb_text;

    // This does not support negative values, because pages can not be negatively sized.
    static std::string arg = "([0-9]+[\\.,]?[0-9]*|\\.[0-9]+) ?(px|mm|cm|in|\\\")?";
    // We can't support × here since it's UTF8 and this doesn't match
    static std::regex re_size("^ *" + arg + " *([ *Xx,\\-]) *" + arg + " *$");

    std::smatch matches;
    if (std::regex_match(text, matches, re_size)) {
        // Convert the desktop px back into document units for 'resizePage'
        double width = _unit_to_size(matches[1], matches[2], matches[5]);
        double height = _unit_to_size(matches[4], matches[5], matches[2]);
        if (width > 0 && height > 0) {
            _document->getPageManager().resizePage(width, height);
        }
    }
    setSizeText();
}

/**
 * Sets the size of the current page into the entry page size.
 */
void PageToolbar::setSizeText(SPPage *page, bool display_only)
{
    _size_edited.block();
    SearchCols cols;

    if (!page)
        page = _document->getPageManager().getSelected();

    auto label = _document->getPageManager().getSizeLabel(page);

    // If this is a known size in our list, add the size paren to it.
    for (auto &row : _sizes_search->children()) {
        if (label == row[cols.name].operator Glib::ustring().raw()) {
            label = label + " (" + row[cols.label] + ")";
            break;
        }
    }
    _entry_page_sizes->set_text(label);

    // Orientation button
    auto box = page ? page->getDesktopRect() : *_document->preferredBounds();
    auto const icon = box.width() > box.height() ? "page-landscape" : "page-portrait";
    if (box.width() == box.height()) {
        _entry_page_sizes->unset_icon(Gtk::Entry::IconPosition::SECONDARY);
    } else {
        _entry_page_sizes->set_icon_from_icon_name(INKSCAPE_ICON(icon), Gtk::Entry::IconPosition::SECONDARY);
    }

    if (!display_only) {
        // The user has started editing the combo box; we set up a convenient initial state.
        // Select text if box is currently in focus.
        if (_entry_page_sizes->has_focus()) {
            _entry_page_sizes->select_region(0, -1);
        }
    }
    _size_edited.unblock();
}

void PageToolbar::setMarginText(SPPage *page)
{
    _text_page_margins.set_text(page ? page->getMarginLabel() : "");
    _text_page_margins.set_sensitive(true);
}

void PageToolbar::pagesChanged()
{
    selectionChanged(_document->getPageManager().getSelected());
}

void PageToolbar::selectionChanged(SPPage *page)
{
    _label_edited.block();
    _page_modified.disconnect();
    auto &page_manager = _document->getPageManager();
    _text_page_label.set_tooltip_text(_("Page label"));

    setMarginText(page);

    // Set label widget content with page label.
    if (page) {
        _text_page_label.set_sensitive(true);
        _text_page_label.set_placeholder_text(page->getDefaultLabel());

        if (auto label = page->label()) {
            _text_page_label.set_text(label);
        } else {
            _text_page_label.set_text("");
        }


        // TRANSLATORS: "%1" is replaced with the page we are on, and "%2" is the total number of pages.
        auto label = Glib::ustring::compose(_("%1/%2"), page->getPagePosition(), page_manager.getPageCount());
        _label_page_pos.set_label(label);

        _page_modified = page->connectModified([this] (SPObject *obj, unsigned flags) {
            if (auto page = cast<SPPage>(obj)) {
                // Make sure we don't 'select' on removal of the page
                if (flags & SP_OBJECT_MODIFIED_FLAG) {
                    selectionChanged(page);
                }
            }
        });
    } else {
        _text_page_label.set_text("");
        _text_page_label.set_sensitive(false);
        _text_page_label.set_placeholder_text(_("Single Page Document"));
        _label_page_pos.set_label(_("1/-"));

        _page_modified = _document->connectModified([this] (unsigned) {
            selectionChanged(nullptr);
        });
    }
    if (!page_manager.hasPrevPage() && !page_manager.hasNextPage() && !page) {
        _sep1.set_visible(false);
        _label_page_pos.set_visible(false);
        _btn_page_backward.set_visible(false);
        _btn_page_foreward.set_visible(false);
        _btn_page_delete.set_visible(false);
        _btn_move_toggle.set_sensitive(false);
    } else {
        // Set the forward and backward button sensitivities
        _sep1.set_visible(true);
        _label_page_pos.set_visible(true);
        _btn_page_backward.set_visible(true);
        _btn_page_foreward.set_visible(true);
        _btn_page_backward.set_sensitive(page_manager.hasPrevPage());
        _btn_page_foreward.set_sensitive(page_manager.hasNextPage());
        _btn_page_delete.set_visible(true);
        _btn_move_toggle.set_sensitive(true);
    }
    setSizeText(page);
    _label_edited.unblock();
}

void PageToolbar::size_allocate_vfunc(int const width, int const height, int const baseline)
{
    Toolbar::size_allocate_vfunc(width, height, baseline);

    _margin_popover.present();
}

} // namespace Inkscape::UI::Toolbar

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
