// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Document properties dialog, Gtkmm-style.
 */
/* Authors:
 *   bulia byak <buliabyak@users.sf.net>
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Jon Phillips <jon@rejon.org>
 *   Ralf Stephan <ralf@ark.in-berlin.de> (Gtkmm)
 *   Diederik van Lierop <mail@diedenrezi.nl>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 2006-2008 Johan Engelen  <johan@shouraizou.nl>
 * Copyright (C) 2000 - 2008 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "document-properties.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <gtkmm/adjustment.h>
#include <gtkmm/button.h>
#include <gtkmm/entry.h>
#include <gtkmm/enums.h>
#include <gtkmm/grid.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/object.h>
#include <gtkmm/popover.h>
#include <gtkmm/spinbutton.h>
#include <iterator>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <tuple>

#include <glibmm/convert.h>
#include <gtkmm/image.h>
#include <gtkmm/liststore.h>
#include <gtkmm/sizegroup.h>
#include <sigc++/adaptors/bind.h>
#include <sigc++/functors/mem_fun.h>

#include "angle.h"
#include "rdf.h"
#include "page-manager.h"
#include "selection.h"

#include "color/cms-system.h"
#include "helper/auto-connection.h"
#include "io/sys.h"
#include "object/color-profile.h"
#include "object/sp-grid.h"
#include "object/sp-root.h"
#include "object/sp-script.h"
#include "ui/dialog/filedialog.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/pack.h"
#include "ui/popup-menu.h"
#include "ui/util.h"
#include "ui/widget/alignment-selector.h"
#include "ui/widget/entity-entry.h"
#include "ui/widget/labelled.h"
#include "ui/widget/notebook-page.h"
#include "ui/widget/page-properties.h"
#include "ui/widget/popover-menu.h"
#include "ui/widget/popover-menu-item.h"
#include "ui/widget/scalar.h"
#include "util/expression-evaluator.h"

namespace Inkscape::UI {

namespace Widget {

class GridWidget final : public Gtk::Box
{
public:
    GridWidget(SPGrid *obj);

    void update();
    SPGrid *getGrid() { return _grid; }
    XML::Node *getGridRepr() { return repr; }
    Gtk::Box *getTabWidget() { return _tab; }

private:
    SPGrid *_grid = nullptr;
    XML::Node *repr = nullptr;

    Gtk::Box *_tab = nullptr;
    Gtk::Image *_tab_img = nullptr;
    Gtk::Label *_tab_lbl = nullptr;

    Gtk::Label *_name_label = nullptr;

    UI::Widget::Registry _wr;
    RegisteredCheckButton *_grid_rcb_enabled = nullptr;
    RegisteredCheckButton *_grid_rcb_snap_visible_only = nullptr;
    RegisteredCheckButton *_grid_rcb_visible = nullptr;
    RegisteredCheckButton *_grid_rcb_dotted = nullptr;
    AlignmentSelector     *_grid_as_alignment = nullptr;

    RegisteredUnitMenu *_rumg = nullptr;
    RegisteredScalarUnit *_rsu_ox = nullptr;
    RegisteredScalarUnit *_rsu_oy = nullptr;
    RegisteredScalarUnit *_rsu_sx = nullptr;
    RegisteredScalarUnit *_rsu_sy = nullptr;
    RegisteredScalar *_rsu_ax = nullptr;
    RegisteredScalar *_rsu_az = nullptr;
    RegisteredColorPicker *_rcp_gcol = nullptr;
    RegisteredColorPicker *_rcp_gmcol = nullptr;
    RegisteredInteger *_rsi = nullptr;
    RegisteredScalarUnit* _rsu_gx = nullptr;
    RegisteredScalarUnit* _rsu_gy = nullptr;
    RegisteredScalarUnit* _rsu_mx = nullptr;
    RegisteredScalarUnit* _rsu_my = nullptr;
    Gtk::MenuButton* _angle_popup = nullptr;
    Gtk::Entry* _aspect_ratio = nullptr;

    Inkscape::auto_connection _modified_signal;
};

} // namespace Widget

namespace Dialog {

static constexpr int SPACE_SIZE_X = 15;
static constexpr int SPACE_SIZE_Y = 10;

static void docprops_style_button(Gtk::Button& btn, char const* iconName)
{
    GtkWidget *child = sp_get_icon_image(iconName, GTK_ICON_SIZE_NORMAL);
    gtk_widget_set_visible(child, true);
    btn.set_child(*Gtk::manage(Glib::wrap(child)));
    btn.set_has_frame(false);
}

static bool do_remove_popup_menu(PopupMenuOptionalClick const click,
                                 Gtk::TreeView &tree_view, sigc::slot<void ()> const &slot)
{
    auto const selection = tree_view.get_selection();
    if (!selection) return false;

    auto const it = selection->get_selected();
    if (!it) return false;

    auto const mi = Gtk::make_managed<UI::Widget::PopoverMenuItem>(_("_Remove"), true);
    mi->signal_activate().connect(slot);
    auto const menu = std::make_shared<UI::Widget::PopoverMenu>(tree_view, Gtk::PositionType::BOTTOM);
    menu->append(*mi);
    UI::on_hide_reset(menu);

    if (click) {
        menu->popup_at(tree_view, click->x, click->y);
        return true;
    }

    auto const column = tree_view.get_column(0);
    g_return_val_if_fail(column, false);
    auto rect = Gdk::Rectangle{};
    tree_view.get_cell_area(Gtk::TreePath{it}, *column, rect);
    menu->popup_at(tree_view, rect.get_x() + rect.get_width () / 2.0,
                              rect.get_y() + rect.get_height());
    return true;
}

static void connect_remove_popup_menu(Gtk::TreeView &tree_view, sigc::slot<void ()> slot)
{
    UI::on_popup_menu(tree_view, sigc::bind(&do_remove_popup_menu,
                                            std::ref(tree_view), std::move(slot)));
}

DocumentProperties::DocumentProperties()
    : DialogBase("/dialogs/documentoptions", "DocumentProperties")
    , _page_page(Gtk::make_managed<UI::Widget::NotebookPage>(1, 1, false, true))
    , _page_guides(Gtk::make_managed<UI::Widget::NotebookPage>(1, 1))
    , _page_cms(Gtk::make_managed<UI::Widget::NotebookPage>(1, 1))
    , _page_scripting(Gtk::make_managed<UI::Widget::NotebookPage>(1, 1))
    , _page_external_scripts(Gtk::make_managed<UI::Widget::NotebookPage>(1, 1))
    , _page_embedded_scripts(Gtk::make_managed<UI::Widget::NotebookPage>(1, 1))
    , _page_metadata1(Gtk::make_managed<UI::Widget::NotebookPage>(1, 1))
    , _page_metadata2(Gtk::make_managed<UI::Widget::NotebookPage>(1, 1))
    //---------------------------------------------------------------
    // General guide options
    , _rcb_sgui(_("Show _guides"), _("Show or hide guides"), "showguides", _wr)
    , _rcb_lgui(_("Lock all guides"), _("Toggle lock of all guides in the document"), "inkscape:lockguides", _wr)
    , _rcp_gui(_("Guide co_lor:"), _("Guideline color"), _("Color of guidelines"), "guidecolor", "guideopacity", _wr)
    , _rcp_hgui(_("_Highlight color:"), _("Highlighted guideline color"),
                _("Color of a guideline when it is under mouse"), "guidehicolor", "guidehiopacity", _wr)
    , _create_guides_btn(_("Create guides around the current page"))
    , _delete_guides_btn(_("Delete all guides"))
    //---------------------------------------------------------------
    , _grids_label_crea("", Gtk::Align::START)
    , _grids_button_remove(C_("Grid", "_Remove"), _("Remove selected grid."))
    , _grids_label_def("", Gtk::Align::START)
    , _grids_vbox(Gtk::Orientation::VERTICAL)
    , _grids_hbox_crea(Gtk::Orientation::HORIZONTAL)
    // Attach nodeobservers to this document
    , _namedview_connection(this)
    , _root_connection(this)
{
    UI::pack_start(*this, _notebook, true, true);

    _notebook.append_page(*_page_page,      _("Display"));
    _notebook.append_page(*_page_guides,    _("Guides"));
    _notebook.append_page(_grids_vbox,      _("Grids"));
    _notebook.append_page(*_page_cms,       _("Color"));
    _notebook.append_page(*_page_scripting, _("Scripting"));
    _notebook.append_page(*_page_metadata1, _("Metadata"));
    _notebook.append_page(*_page_metadata2, _("License"));
    _notebook.signal_switch_page().connect([this](Gtk::Widget const *, unsigned const page){
        // we cannot use widget argument, as this notification fires during destruction with all pages passed one by one
        // page no 3 - cms
        if (page == 3) {
            // lazy-load color profiles; it can get prohibitively expensive when hundreds are installed
            populate_available_profiles();
        }
    });

    _wr.setUpdating (true);
    build_page();
    build_guides();
    build_gridspage();
    build_cms();
    build_scripting();
    build_metadata();
    _wr.setUpdating (false);

    _grids_button_remove.signal_clicked().connect([this]{ onRemoveGrid(); });
}

//========================================================================

/**
 * Helper function that sets widgets in a 2 by n table.
 * arr has two entries per table row. Each row is in the following form:
 *     widget, widget -> function adds a widget in each column.
 *     nullptr, widget -> function adds a widget that occupies the row.
 *     label, nullptr -> function adds label that occupies the row.
 *     nullptr, nullptr -> function adds an empty box that occupies the row.
 * This used to be a helper function for a 3 by n table
 */
void attach_all(Gtk::Grid &table, Gtk::Widget *const arr[], unsigned const n)
{
    for (unsigned i = 0, r = 0; i < n; i += 2) {
        if (arr[i] && arr[i+1]) {
            arr[i]->set_hexpand();
            arr[i+1]->set_hexpand();
            arr[i]->set_valign(Gtk::Align::CENTER);
            arr[i+1]->set_valign(Gtk::Align::CENTER);
            table.attach(*arr[i],   0, r, 1, 1);
            table.attach(*arr[i+1], 1, r, 1, 1);
        } else {
            if (arr[i+1]) {
                arr[i+1]->set_hexpand();
                arr[i+1]->set_valign(Gtk::Align::CENTER);
                table.attach(*arr[i+1], 0, r, 2, 1);
            } else if (arr[i]) {
                auto &label = dynamic_cast<Gtk::Label &>(*arr[i]);
                label.set_hexpand();
                label.set_halign(Gtk::Align::START);
                label.set_valign(Gtk::Align::CENTER);
                table.attach(label, 0, r, 2, 1);
            } else {
                auto const space = Gtk::make_managed<Gtk::Box>();
                space->set_size_request (SPACE_SIZE_X, SPACE_SIZE_Y);
                space->set_halign(Gtk::Align::CENTER);
                space->set_valign(Gtk::Align::CENTER);
                table.attach(*space, 0, r, 1, 1);
            }
        }
        ++r;
    }
}

void set_namedview_bool(SPDesktop* desktop, const Glib::ustring& operation, SPAttr key, bool on) {
    if (!desktop || !desktop->getDocument()) return;

    desktop->getNamedView()->change_bool_setting(key, on);

    desktop->getDocument()->setModifiedSinceSave();
    DocumentUndo::done(desktop->getDocument(), operation, "");
}

void set_color(SPDesktop* desktop, Glib::ustring operation, unsigned int rgba, SPAttr color_key, SPAttr opacity_key = SPAttr::INVALID) {
    if (!desktop || !desktop->getDocument()) return;

    desktop->getNamedView()->change_color(rgba, color_key, opacity_key);

    desktop->getDocument()->setModifiedSinceSave();
    DocumentUndo::maybeDone(desktop->getDocument(), ("document-color-" + operation).c_str(), operation, "");
}

void set_document_dimensions(SPDesktop* desktop, double width, double height, const Inkscape::Util::Unit* unit) {
    if (!desktop) return;

    Inkscape::Util::Quantity width_quantity  = Inkscape::Util::Quantity(width, unit);
    Inkscape::Util::Quantity height_quantity = Inkscape::Util::Quantity(height, unit);
    SPDocument* doc = desktop->getDocument();
    Inkscape::Util::Quantity const old_height = doc->getHeight();
    auto rect = Geom::Rect(Geom::Point(0, 0), Geom::Point(width_quantity.value("px"), height_quantity.value("px")));
    doc->fitToRect(rect, false);

    // The origin for the user is in the lower left corner; this point should remain stationary when
    // changing the page size. The SVG's origin however is in the upper left corner, so we must compensate for this
    if (!doc->is_yaxisdown()) {
        Geom::Translate const vert_offset(Geom::Point(0, (old_height.value("px") - height_quantity.value("px"))));
        doc->getRoot()->translateChildItems(vert_offset);
    }
    // units: this is most likely not needed, units are part of document size attributes
    // if (unit) {
        // set_namedview_value(desktop, "", SPAttr::UNITS)
        // write_str_to_xml(desktop, _("Set document unit"), "unit", unit->abbr.c_str());
    // }
    doc->setWidthAndHeight(width_quantity, height_quantity, true);

    DocumentUndo::done(doc, _("Set page size"), "");
}

void DocumentProperties::set_viewbox_pos(SPDesktop* desktop, double x, double y) {
    if (!desktop) return;

    auto document = desktop->getDocument();
    if (!document) return;

    auto box = document->getViewBox();
    document->setViewBox(Geom::Rect::from_xywh(x, y, box.width(), box.height()));
    DocumentUndo::done(document, _("Set viewbox position"), "");
    update_scale_ui(desktop);
}

void DocumentProperties::set_viewbox_size(SPDesktop* desktop, double width, double height) {
    if (!desktop) return;

    auto document = desktop->getDocument();
    if (!document) return;

    auto box = document->getViewBox();
    document->setViewBox(Geom::Rect::from_xywh(box.min()[Geom::X], box.min()[Geom::Y], width, height));
    DocumentUndo::done(document, _("Set viewbox size"), "");
    update_scale_ui(desktop);
}

// helper function to set document scale; uses magnitude of document width/height only, not computed (pixel) values
void set_document_scale_helper(SPDocument& document, double scale) {
    if (scale <= 0) return;

    auto root = document.getRoot();
    auto box = document.getViewBox();
    document.setViewBox(Geom::Rect::from_xywh(
        box.min()[Geom::X], box.min()[Geom::Y],
        root->width.value / scale, root->height.value / scale)
    );
}

void DocumentProperties::set_content_scale(SPDesktop *desktop, double scale)
{
    if (!desktop) return;

    auto document = desktop->getDocument();
    if (!document) return;

    if (scale > 0) {
        auto old_scale = document->getDocumentScale(false);
        auto delta = old_scale * Geom::Scale(scale).inverse();

        // Shapes in the document
        document->scaleContentBy(delta);

        // Pages, margins and bleeds
        document->getPageManager().scalePages(delta);

        // Grids
        if (auto nv = document->getNamedView()) {
            for (auto grid : nv->grids) {
                grid->scale(delta);
            }
        }
    }
}

void DocumentProperties::set_document_scale(SPDesktop* desktop, double scale) {
    if (!desktop) return;

    auto document = desktop->getDocument();
    if (!document) return;

    if (scale > 0) {
        set_document_scale_helper(*document, scale);
        update_viewbox_ui(desktop);
        update_scale_ui(desktop);
        DocumentUndo::done(document, _("Set page scale"), "");
    }
}

// document scale as a ratio of document size and viewbox size
// as described in Wiki: https://wiki.inkscape.org/wiki/index.php/Units_In_Inkscape
// for example: <svg width="100mm" height="100mm" viewBox="0 0 100 100"> will report 1:1 scale
std::optional<Geom::Scale> get_document_scale_helper(SPDocument& doc) {
    auto root = doc.getRoot();
    if (root &&
        root->width._set  && root->width.unit  != SVGLength::PERCENT &&
        root->height._set && root->height.unit != SVGLength::PERCENT) {
        if (root->viewBox_set) {
            // viewbox and document size present
            auto vw = root->viewBox.width();
            auto vh = root->viewBox.height();
            if (vw > 0 && vh > 0) {
                return Geom::Scale(root->width.value / vw, root->height.value / vh);
            }
        } else {
            // no viewbox, use SVG size in pixels
            auto w = root->width.computed;
            auto h = root->height.computed;
            if (w > 0 && h > 0) {
                return Geom::Scale(root->width.value / w, root->height.value / h);
            }
        }
    }

    // there is no scale concept applicable in the current state
    return std::optional<Geom::Scale>();
}

void DocumentProperties::update_scale_ui(SPDesktop* desktop) {
    if (!desktop) return;

    auto document = desktop->getDocument();
    if (!document) return;

    using UI::Widget::PageProperties;
    if (auto scale = get_document_scale_helper(*document)) {
        auto sx = (*scale)[Geom::X];
        auto sy = (*scale)[Geom::Y];
        double eps = 0.0001; // TODO: tweak this value
        bool uniform = fabs(sx - sy) < eps;
        _page->set_dimension(PageProperties::Dimension::Scale, sx, sx); // only report one, only one "scale" is used
        _page->set_check(PageProperties::Check::NonuniformScale, !uniform);
        _page->set_check(PageProperties::Check::DisabledScale, false);
    } else {
        // no scale
        _page->set_dimension(PageProperties::Dimension::Scale, 1, 1);
        _page->set_check(PageProperties::Check::NonuniformScale, false);
        _page->set_check(PageProperties::Check::DisabledScale, true);
    }
}

void DocumentProperties::update_viewbox_ui(SPDesktop* desktop) {
    if (!desktop) return;

    auto document = desktop->getDocument();
    if (!document) return;

    using UI::Widget::PageProperties;
    Geom::Rect viewBox = document->getViewBox();
    _page->set_dimension(PageProperties::Dimension::ViewboxPosition, viewBox.min()[Geom::X], viewBox.min()[Geom::Y]);
    _page->set_dimension(PageProperties::Dimension::ViewboxSize, viewBox.width(), viewBox.height());
}

void DocumentProperties::build_page()
{
    using UI::Widget::PageProperties;
    _page = Gtk::manage(PageProperties::create());
    _page_page->table().attach(*_page, 0, 0);

    _page->signal_color_changed().connect([this](unsigned const color, PageProperties::Color const element){
        if (_wr.isUpdating() || !_wr.desktop()) return;

        _wr.setUpdating(true);
        switch (element) {
            case PageProperties::Color::Desk:
                set_color(_wr.desktop(), _("Desk color"), color, SPAttr::INKSCAPE_DESK_COLOR);
                break;
            case PageProperties::Color::Background:
                set_color(_wr.desktop(), _("Background color"), color, SPAttr::PAGECOLOR);
                break;
            case PageProperties::Color::Border:
                set_color(_wr.desktop(), _("Border color"), color, SPAttr::BORDERCOLOR, SPAttr::BORDEROPACITY);
                break;
        }
        _wr.setUpdating(false);
    });

    _page->signal_dimension_changed().connect([this](double const x, double const y,
                                                     auto const unit,
                                                     PageProperties::Dimension const element)
    {
        if (_wr.isUpdating() || !_wr.desktop()) return;

        _wr.setUpdating(true);
        switch (element) {
            case PageProperties::Dimension::PageTemplate:
            case PageProperties::Dimension::PageSize:
                set_document_dimensions(_wr.desktop(), x, y, unit);
                update_viewbox(_wr.desktop());
                break;

            case PageProperties::Dimension::ViewboxSize:
                set_viewbox_size(_wr.desktop(), x, y);
                break;

            case PageProperties::Dimension::ViewboxPosition:
                set_viewbox_pos(_wr.desktop(), x, y);
                break;

            case PageProperties::Dimension::ScaleContent:
                set_content_scale(_wr.desktop(), x);
            case PageProperties::Dimension::Scale:
                set_document_scale(_wr.desktop(), x); // only uniform scale; there's no 'y' in the dialog
                break;
        }
        _wr.setUpdating(false);
    });

    _page->signal_check_toggled().connect([this](bool const checked, PageProperties::Check const element){
        if (_wr.isUpdating() || !_wr.desktop()) return;

        _wr.setUpdating(true);
        switch (element) {
            case PageProperties::Check::Checkerboard:
                set_namedview_bool(_wr.desktop(), _("Toggle checkerboard"), SPAttr::INKSCAPE_DESK_CHECKERBOARD, checked);
                break;
            case PageProperties::Check::Border:
                set_namedview_bool(_wr.desktop(), _("Toggle page border"), SPAttr::SHOWBORDER, checked);
                break;
            case PageProperties::Check::BorderOnTop:
                set_namedview_bool(_wr.desktop(), _("Toggle border on top"), SPAttr::BORDERLAYER, checked);
                break;
            case PageProperties::Check::Shadow:
                set_namedview_bool(_wr.desktop(), _("Toggle page shadow"), SPAttr::SHOWPAGESHADOW, checked);
                break;
            case PageProperties::Check::AntiAlias:
                set_namedview_bool(_wr.desktop(), _("Toggle anti-aliasing"), SPAttr::INKSCAPE_ANTIALIAS_RENDERING, checked);
                break;
            case PageProperties::Check::ClipToPage:
                set_namedview_bool(_wr.desktop(), _("Toggle clip to page mode"), SPAttr::INKSCAPE_CLIP_TO_PAGE_RENDERING, checked);
                break;
            case PageProperties::Check::PageLabelStyle:
                set_namedview_bool(_wr.desktop(), _("Toggle page label style"), SPAttr::PAGELABELSTYLE, checked);
        }
        _wr.setUpdating(false);
    });

    _page->signal_unit_changed().connect([this](Inkscape::Util::Unit const * const unit, PageProperties::Units const element){
        if (_wr.isUpdating() || !_wr.desktop()) return;

        if (element == PageProperties::Units::Display) {
            // display only units
            display_unit_change(unit);
        }
        else if (element == PageProperties::Units::Document) {
            // not used, fired with page size
        }
    });

    _page->signal_resize_to_fit().connect([this]{
        if (_wr.isUpdating() || !_wr.desktop()) return;

        if (auto document = getDocument()) {
            auto &page_manager = document->getPageManager();
            page_manager.selectPage(0);
            // fit page to selection or content, if there's no selection
            page_manager.fitToSelection(_wr.desktop()->getSelection());
            DocumentUndo::done(document, _("Resize page to fit"), INKSCAPE_ICON("tool-pages"));
            update_widgets();
        }
    });
}

void DocumentProperties::build_guides()
{
    auto const label_gui = Gtk::make_managed<Gtk::Label>();
    label_gui->set_markup (_("<b>Guides</b>"));

    _rcp_gui.set_margin_start(0);
    _rcp_hgui.set_margin_start(0);
    _rcp_gui.set_hexpand();
    _rcp_hgui.set_hexpand();
    _rcb_sgui.set_hexpand();
    auto const inner = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    inner->append(_rcb_sgui);
    inner->append(_rcb_lgui);
    inner->append(_rcp_gui);
    inner->append(_rcp_hgui);
    auto const spacer = Gtk::make_managed<Gtk::Label>();
    Gtk::Widget *const widget_array[] =
    {
        label_gui, nullptr,
        inner,     spacer,
        nullptr,   nullptr,
        nullptr,   &_create_guides_btn,
        nullptr,   &_delete_guides_btn
    };
    attach_all(_page_guides->table(), widget_array, G_N_ELEMENTS(widget_array));
    inner->set_hexpand(false);

    // Must use C API until GTK4.
    gtk_actionable_set_action_name(GTK_ACTIONABLE(_create_guides_btn.gobj()), "doc.create-guides-around-page");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(_delete_guides_btn.gobj()), "doc.delete-all-guides");
}

/// Populates the available color profiles combo box
void DocumentProperties::populate_available_profiles(){
    // scanning can be expensive; avoid if possible
    if (!_AvailableProfilesListStore->children().empty()) return;

    _AvailableProfilesListStore->clear(); // Clear any existing items in the combo box

    // Iterate through the list of profiles and add the name to the combo box.
    bool home = true; // initial value doesn't matter, it's just to avoid a compiler warning
    bool first = true;
    auto cms_system = Inkscape::CMSSystem::get();
    for (auto const &info: cms_system->get_system_profile_infos()) {
        Gtk::TreeModel::Row row;

        // add a separator between profiles from the user's home directory and system profiles
        if (!first && info.in_home() != home)
        {
          row = *(_AvailableProfilesListStore->append());
          row[_AvailableProfilesListColumns.fileColumn] = "<separator>";
          row[_AvailableProfilesListColumns.nameColumn] = "<separator>";
          row[_AvailableProfilesListColumns.separatorColumn] = true;
        }
        home = info.in_home();
        first = false;

        row = *(_AvailableProfilesListStore->append());
        row[_AvailableProfilesListColumns.fileColumn] = info.get_path();
        row[_AvailableProfilesListColumns.nameColumn] = info.get_name();
        row[_AvailableProfilesListColumns.separatorColumn] = false;
    }
}

/**
 * Cleans up name to remove disallowed characters.
 * Some discussion at http://markmail.org/message/bhfvdfptt25kgtmj
 * Allowed ASCII first characters:  ':', 'A'-'Z', '_', 'a'-'z'
 * Allowed ASCII remaining chars add: '-', '.', '0'-'9',
 *
 * @param str the string to clean up.
 *
 * Note: for use with ICC profiles only.
 * This function has been restored to make ICC profiles work, as their names need to be sanitized.
 * BUT, it is not clear to me whether we really need to strip all non-ASCII characters.
 * We do it currently, because sp_svg_read_icc_color cannot parse Unicode.
 */
void sanitizeName(std::string& str) {
    if (str.empty()) return;

    auto val = str.at(0);
    if ((val < 'A' || val > 'Z') && (val < 'a' || val > 'z') && val != '_' && val != ':') {
        str.insert(0, "_");
    }
    for (std::size_t i = 1; i < str.size(); i++) {
        auto val = str.at(i);
        if ((val < 'A' || val > 'Z') && (val < 'a' || val > 'z') && (val < '0' || val > '9') &&
            val != '_' && val != ':' && val != '-' && val != '.') {
            if (str.at(i - 1) == '-') {
                str.erase(i, 1);
                i--;
            } else {
                str.replace(i, 1, "-");
            }
        }
    }
    if (str.at(str.size() - 1) == '-') {
        str.pop_back();
    }
}

/// Links the selected color profile in the combo box to the document
void DocumentProperties::linkSelectedProfile()
{
    //store this profile in the SVG document (create <color-profile> element in the XML)
    if (auto document = getDocument()){
        // Find the index of the currently-selected row in the color profiles combobox
        Gtk::TreeModel::iterator iter = _AvailableProfilesList.get_active();
        if (!iter)
            return;

        // Read the filename and description from the list of available profiles
        Glib::ustring file = (*iter)[_AvailableProfilesListColumns.fileColumn];
        Glib::ustring name = (*iter)[_AvailableProfilesListColumns.nameColumn];

        std::vector<SPObject *> current = document->getResourceList( "iccprofile" );
        for (auto obj : current) {
            Inkscape::ColorProfile* prof = reinterpret_cast<Inkscape::ColorProfile*>(obj);
            if (!strcmp(prof->href, file.c_str()))
                return;
        }
        Inkscape::XML::Document *xml_doc = document->getReprDoc();
        Inkscape::XML::Node *cprofRepr = xml_doc->createElement("svg:color-profile");
        std::string nameStr = name.empty() ? "profile" : name; // TODO add some auto-numbering to avoid collisions
        sanitizeName(nameStr);
        cprofRepr->setAttribute("name", nameStr);
        cprofRepr->setAttribute("xlink:href", Glib::filename_to_uri(Glib::filename_from_utf8(file)));
        cprofRepr->setAttribute("id", file);

        // Checks whether there is a defs element. Creates it when needed
        Inkscape::XML::Node *defsRepr = sp_repr_lookup_name(xml_doc, "svg:defs");
        if (!defsRepr) {
            defsRepr = xml_doc->createElement("svg:defs");
            xml_doc->root()->addChild(defsRepr, nullptr);
        }

        g_assert(document->getDefs());
        defsRepr->addChild(cprofRepr, nullptr);

        // TODO check if this next line was sometimes needed. It being there caused an assertion.
        //Inkscape::GC::release(defsRepr);

        // inform the document, so we can undo
        DocumentUndo::done(document, _("Link Color Profile"), "");

        populate_linked_profiles_box();
    }
}

struct _cmp {
  bool operator()(const SPObject * const & a, const SPObject * const & b)
  {
    const Inkscape::ColorProfile &a_prof = reinterpret_cast<const Inkscape::ColorProfile &>(*a);
    const Inkscape::ColorProfile &b_prof = reinterpret_cast<const Inkscape::ColorProfile &>(*b);
    auto const a_name_casefold = g_utf8_casefold(a_prof.name, -1);
    auto const b_name_casefold = g_utf8_casefold(b_prof.name, -1);
    int result = g_strcmp0(a_name_casefold, b_name_casefold);
    g_free(a_name_casefold);
    g_free(b_name_casefold);
    return result < 0;
  }
};

template <typename From, typename To>
struct static_caster { To * operator () (From * value) const { return static_cast<To *>(value); } };

void DocumentProperties::populate_linked_profiles_box()
{
    _LinkedProfilesListStore->clear();
    if (auto document = getDocument()) {
        std::vector<SPObject *> current = document->getResourceList( "iccprofile" );
        if (! current.empty()) {
            _emb_profiles_observer.set((*(current.begin()))->parent);
        }

        std::set<Inkscape::ColorProfile *> _current;
        std::transform(current.begin(),
                       current.end(),
                       std::inserter(_current, _current.begin()),
                       static_caster<SPObject, Inkscape::ColorProfile>());

        for (auto const &profile: _current) {
            Gtk::TreeModel::Row row = *(_LinkedProfilesListStore->append());
            row[_LinkedProfilesListColumns.nameColumn] = profile->name;
        }
    }
}

void DocumentProperties::onColorProfileSelectRow()
{
    Glib::RefPtr<Gtk::TreeSelection> sel = _LinkedProfilesList.get_selection();
    if (sel) {
        _unlink_btn.set_sensitive(sel->count_selected_rows () > 0);
    }
}

void DocumentProperties::removeSelectedProfile(){
    Glib::ustring name;
    if(_LinkedProfilesList.get_selection()) {
        Gtk::TreeModel::iterator i = _LinkedProfilesList.get_selection()->get_selected();

        if(i){
            name = (*i)[_LinkedProfilesListColumns.nameColumn];
        } else {
            return;
        }
    }
    if (auto document = getDocument()) {
        std::vector<SPObject *> current = document->getResourceList( "iccprofile" );
        for (auto obj : current) {
            Inkscape::ColorProfile* prof = reinterpret_cast<Inkscape::ColorProfile*>(obj);
            if (!name.compare(prof->name)){
                prof->deleteObject(true, false);
                DocumentUndo::done(document, _("Remove linked color profile"), "");
                break; // removing the color profile likely invalidates part of the traversed list, stop traversing here.
            }
        }
    }

    populate_linked_profiles_box();
    onColorProfileSelectRow();
}

bool DocumentProperties::_AvailableProfilesList_separator(Glib::RefPtr<Gtk::TreeModel> const &model,
                                                          Gtk::TreeModel::const_iterator const &iter)
{
    bool separator = (*iter)[_AvailableProfilesListColumns.separatorColumn];
    return separator;
}

void DocumentProperties::build_cms()
{
    Gtk::Label *label_link= Gtk::make_managed<Gtk::Label>("", Gtk::Align::START);
    label_link->set_markup (_("<b>Linked Color Profiles:</b>"));
    auto const label_avail = Gtk::make_managed<Gtk::Label>("", Gtk::Align::START);
    label_avail->set_markup (_("<b>Available Color Profiles:</b>"));

    _unlink_btn.set_tooltip_text(_("Unlink Profile"));
    docprops_style_button(_unlink_btn, INKSCAPE_ICON("list-remove"));

    int row = 0;

    label_link->set_hexpand();
    label_link->set_halign(Gtk::Align::START);
    label_link->set_valign(Gtk::Align::CENTER);
    _page_cms->table().attach(*label_link, 0, row, 3, 1);

    row++;

    _LinkedProfilesListScroller.set_hexpand();
    _LinkedProfilesListScroller.set_valign(Gtk::Align::CENTER);
    _page_cms->table().attach(_LinkedProfilesListScroller, 0, row, 3, 1);

    row++;

    auto const spacer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
    spacer->set_size_request(SPACE_SIZE_X, SPACE_SIZE_Y);

    spacer->set_hexpand();
    spacer->set_valign(Gtk::Align::CENTER);
    _page_cms->table().attach(*spacer, 0, row, 3, 1);

    row++;

    label_avail->set_hexpand();
    label_avail->set_halign(Gtk::Align::START);
    label_avail->set_valign(Gtk::Align::CENTER);
    _page_cms->table().attach(*label_avail, 0, row, 3, 1);

    row++;

    _AvailableProfilesList.set_hexpand();
    _AvailableProfilesList.set_valign(Gtk::Align::CENTER);
    _page_cms->table().attach(_AvailableProfilesList, 0, row, 1, 1);

    _unlink_btn.set_halign(Gtk::Align::CENTER);
    _unlink_btn.set_valign(Gtk::Align::CENTER);
    _page_cms->table().attach(_unlink_btn, 2, row, 1, 1);

    // Set up the Available Profiles combo box
    _AvailableProfilesListStore = Gtk::ListStore::create(_AvailableProfilesListColumns);
    _AvailableProfilesList.set_model(_AvailableProfilesListStore);
    _AvailableProfilesList.pack_start(_AvailableProfilesListColumns.nameColumn);
    _AvailableProfilesList.set_row_separator_func(sigc::mem_fun(*this, &DocumentProperties::_AvailableProfilesList_separator));
    _AvailableProfilesList.signal_changed().connect( sigc::mem_fun(*this, &DocumentProperties::linkSelectedProfile) );

    //# Set up the Linked Profiles combo box
    _LinkedProfilesListStore = Gtk::ListStore::create(_LinkedProfilesListColumns);
    _LinkedProfilesList.set_model(_LinkedProfilesListStore);
    _LinkedProfilesList.append_column(_("Profile Name"), _LinkedProfilesListColumns.nameColumn);
//    _LinkedProfilesList.append_column(_("Color Preview"), _LinkedProfilesListColumns.previewColumn);
    _LinkedProfilesList.set_headers_visible(false);
// TODO restore?    _LinkedProfilesList.set_fixed_height_mode(true);

    populate_linked_profiles_box();

    _LinkedProfilesListScroller.set_child(_LinkedProfilesList);
    _LinkedProfilesListScroller.set_has_frame(true);
    _LinkedProfilesListScroller.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::ALWAYS);
    _LinkedProfilesListScroller.set_size_request(-1, 90);

    _unlink_btn.signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::removeSelectedProfile));

    _LinkedProfilesList.get_selection()->signal_changed().connect( sigc::mem_fun(*this, &DocumentProperties::onColorProfileSelectRow) );

    connect_remove_popup_menu(_LinkedProfilesList, sigc::mem_fun(*this, &DocumentProperties::removeSelectedProfile));

    if (auto document = getDocument()) {
        std::vector<SPObject *> current = document->getResourceList( "defs" );
        if (!current.empty()) {
            _emb_profiles_observer.set((*(current.begin()))->parent);
        }
        _emb_profiles_observer.signal_changed().connect(sigc::mem_fun(*this, &DocumentProperties::populate_linked_profiles_box));
        onColorProfileSelectRow();
    }
}

void DocumentProperties::build_scripting()
{
    _page_scripting->table().attach(_scripting_notebook, 0, 0, 1, 1);

    _scripting_notebook.append_page(*_page_external_scripts, _("External scripts"));
    _scripting_notebook.append_page(*_page_embedded_scripts, _("Embedded scripts"));

    //# External scripts tab
    Gtk::Label *label_external= Gtk::make_managed<Gtk::Label>("", Gtk::Align::START);
    label_external->set_markup (_("<b>External script files:</b>"));

    _external_add_btn.set_tooltip_text(_("Add the current file name or browse for a file"));
    docprops_style_button(_external_add_btn, INKSCAPE_ICON("list-add"));

    _external_remove_btn.set_tooltip_text(_("Remove"));
    docprops_style_button(_external_remove_btn, INKSCAPE_ICON("list-remove"));

    int row = 0;

    label_external->set_hexpand();
    label_external->set_halign(Gtk::Align::START);
    label_external->set_valign(Gtk::Align::CENTER);
    _page_external_scripts->table().attach(*label_external, 0, row, 3, 1);

    row++;

    _ExternalScriptsListScroller.set_hexpand();
    _ExternalScriptsListScroller.set_valign(Gtk::Align::CENTER);
    _page_external_scripts->table().attach(_ExternalScriptsListScroller, 0, row, 3, 1);

    row++;

    auto const spacer_external = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
    spacer_external->set_size_request(SPACE_SIZE_X, SPACE_SIZE_Y);

    spacer_external->set_hexpand();
    spacer_external->set_valign(Gtk::Align::CENTER);
    _page_external_scripts->table().attach(*spacer_external, 0, row, 3, 1);

    row++;

    _script_entry.set_hexpand();
    _script_entry.set_valign(Gtk::Align::CENTER);
    _page_external_scripts->table().attach(_script_entry, 0, row, 1, 1);

    _external_add_btn.set_halign(Gtk::Align::CENTER);
    _external_add_btn.set_valign(Gtk::Align::CENTER);
    _external_add_btn.set_margin_start(2);
    _external_add_btn.set_margin_end(2);

    _page_external_scripts->table().attach(_external_add_btn, 1, row, 1, 1);

    _external_remove_btn.set_halign(Gtk::Align::CENTER);
    _external_remove_btn.set_valign(Gtk::Align::CENTER);
    _page_external_scripts->table().attach(_external_remove_btn, 2, row, 1, 1);

    //# Set up the External Scripts box
    _ExternalScriptsListStore = Gtk::ListStore::create(_ExternalScriptsListColumns);
    _ExternalScriptsList.set_model(_ExternalScriptsListStore);
    _ExternalScriptsList.append_column(_("Filename"), _ExternalScriptsListColumns.filenameColumn);
    _ExternalScriptsList.set_headers_visible(true);
// TODO restore?    _ExternalScriptsList.set_fixed_height_mode(true);

    //# Embedded scripts tab
    Gtk::Label *label_embedded= Gtk::make_managed<Gtk::Label>("", Gtk::Align::START);
    label_embedded->set_markup (_("<b>Embedded script files:</b>"));

    _embed_new_btn.set_tooltip_text(_("New"));
    docprops_style_button(_embed_new_btn, INKSCAPE_ICON("list-add"));

    _embed_remove_btn.set_tooltip_text(_("Remove"));
    docprops_style_button(_embed_remove_btn, INKSCAPE_ICON("list-remove"));

    _embed_button_box.append(_embed_new_btn);
    _embed_button_box.append(_embed_remove_btn);
    _embed_button_box.set_halign(Gtk::Align::END);

    row = 0;

    label_embedded->set_hexpand();
    label_embedded->set_halign(Gtk::Align::START);
    label_embedded->set_valign(Gtk::Align::CENTER);
    _page_embedded_scripts->table().attach(*label_embedded, 0, row, 3, 1);

    row++;

    _EmbeddedScriptsListScroller.set_hexpand();
    _EmbeddedScriptsListScroller.set_valign(Gtk::Align::CENTER);
    _page_embedded_scripts->table().attach(_EmbeddedScriptsListScroller, 0, row, 3, 1);

    row++;

    _embed_button_box.set_hexpand();
    _embed_button_box.set_valign(Gtk::Align::CENTER);
    _page_embedded_scripts->table().attach(_embed_button_box, 0, row, 1, 1);

    row++;

    auto const spacer_embedded = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
    spacer_embedded->set_size_request(SPACE_SIZE_X, SPACE_SIZE_Y);
    spacer_embedded->set_hexpand();
    spacer_embedded->set_valign(Gtk::Align::CENTER);
    _page_embedded_scripts->table().attach(*spacer_embedded, 0, row, 3, 1);

    row++;

    //# Set up the Embedded Scripts box
    _EmbeddedScriptsListStore = Gtk::ListStore::create(_EmbeddedScriptsListColumns);
    _EmbeddedScriptsList.set_model(_EmbeddedScriptsListStore);
    _EmbeddedScriptsList.append_column(_("Script ID"), _EmbeddedScriptsListColumns.idColumn);
    _EmbeddedScriptsList.set_headers_visible(true);
// TODO restore?    _EmbeddedScriptsList.set_fixed_height_mode(true);

    //# Set up the Embedded Scripts content box
    Gtk::Label *label_embedded_content= Gtk::make_managed<Gtk::Label>("", Gtk::Align::START);
    label_embedded_content->set_markup (_("<b>Content:</b>"));

    label_embedded_content->set_hexpand();
    label_embedded_content->set_halign(Gtk::Align::START);
    label_embedded_content->set_valign(Gtk::Align::CENTER);
    _page_embedded_scripts->table().attach(*label_embedded_content, 0, row, 3, 1);

    row++;

    _EmbeddedContentScroller.set_hexpand();
    _EmbeddedContentScroller.set_valign(Gtk::Align::CENTER);
    _page_embedded_scripts->table().attach(_EmbeddedContentScroller, 0, row, 3, 1);

    _EmbeddedContentScroller.set_child(_EmbeddedContent);
    _EmbeddedContentScroller.set_has_frame(true);
    _EmbeddedContentScroller.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    _EmbeddedContentScroller.set_size_request(-1, 140);

    _EmbeddedScriptsList.signal_cursor_changed().connect(sigc::mem_fun(*this, &DocumentProperties::changeEmbeddedScript));
    _EmbeddedScriptsList.get_selection()->signal_changed().connect( sigc::mem_fun(*this, &DocumentProperties::onEmbeddedScriptSelectRow) );

    _ExternalScriptsList.get_selection()->signal_changed().connect( sigc::mem_fun(*this, &DocumentProperties::onExternalScriptSelectRow) );

    _EmbeddedContent.get_buffer()->signal_changed().connect(sigc::mem_fun(*this, &DocumentProperties::editEmbeddedScript));

    populate_script_lists();

    _ExternalScriptsListScroller.set_child(_ExternalScriptsList);
    _ExternalScriptsListScroller.set_has_frame(true);
    _ExternalScriptsListScroller.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::ALWAYS);
    _ExternalScriptsListScroller.set_size_request(-1, 90);

    _external_add_btn.signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::addExternalScript));

    _EmbeddedScriptsListScroller.set_child(_EmbeddedScriptsList);
    _EmbeddedScriptsListScroller.set_has_frame(true);
    _EmbeddedScriptsListScroller.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::ALWAYS);
    _EmbeddedScriptsListScroller.set_size_request(-1, 90);

    _embed_new_btn.signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::addEmbeddedScript));

    _external_remove_btn.signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::removeExternalScript));
    _embed_remove_btn.signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::removeEmbeddedScript));

    connect_remove_popup_menu(_ExternalScriptsList, sigc::mem_fun(*this, &DocumentProperties::removeExternalScript));
    connect_remove_popup_menu(_EmbeddedScriptsList, sigc::mem_fun(*this, &DocumentProperties::removeEmbeddedScript));

    //TODO: review this observers code:
    if (auto document = getDocument()) {
        std::vector<SPObject *> current = document->getResourceList( "script" );
        if (! current.empty()) {
            _scripts_observer.set((*(current.begin()))->parent);
        }
        _scripts_observer.signal_changed().connect(sigc::mem_fun(*this, &DocumentProperties::populate_script_lists));
        onEmbeddedScriptSelectRow();
        onExternalScriptSelectRow();
    }
}

void DocumentProperties::build_metadata()
{
    using Inkscape::UI::Widget::EntityEntry;

    auto const label = Gtk::make_managed<Gtk::Label>();
    label->set_markup (_("<b>Dublin Core Entities</b>"));
    label->set_halign(Gtk::Align::START);
    label->set_valign(Gtk::Align::CENTER);
    _page_metadata1->table().attach (*label, 0,0,2,1);

     /* add generic metadata entry areas */
    int row = 1;
    for (auto entity = rdf_work_entities; entity && entity->name; ++entity, ++row) {
        if (entity->editable == RDF_EDIT_GENERIC) {
            auto w = std::unique_ptr<EntityEntry>{EntityEntry::create(entity, _wr)};

            w->_label.set_halign(Gtk::Align::START);
            w->_label.set_valign(Gtk::Align::CENTER);
            _page_metadata1->table().attach(w->_label, 0, row, 1, 1);

            w->_packable->set_hexpand();
            w->_packable->set_valign(Gtk::Align::CENTER);
            _page_metadata1->table().attach(*w->_packable, 1, row, 1, 1);

            _rdflist.push_back(std::move(w));
        }
    }

    auto const button_save = Gtk::make_managed<Gtk::Button>(_("_Save as default"),true);
    button_save->set_tooltip_text(_("Save this metadata as the default metadata"));
    auto const button_load = Gtk::make_managed<Gtk::Button>(_("Use _default"),true);
    button_load->set_tooltip_text(_("Use the previously saved default metadata here"));

    auto const box_buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    UI::pack_end(*box_buttons, *button_save, true, true, 6);
    UI::pack_end(*box_buttons, *button_load, true, true, 6);
    UI::pack_end(*_page_metadata1, *box_buttons, false, false);

    button_save->signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::save_default_metadata));
    button_load->signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::load_default_metadata));

    row = 0;
    auto const llabel = Gtk::make_managed<Gtk::Label>();
    llabel->set_markup (_("<b>License</b>"));
    llabel->set_halign(Gtk::Align::START);
    llabel->set_valign(Gtk::Align::CENTER);
    _page_metadata2->table().attach(*llabel, 0, row, 2, 1);

    /* add license selector pull-down and URI */
    ++row;
    _licensor.init (_wr);

    _licensor.set_hexpand();
    _licensor.set_valign(Gtk::Align::CENTER);
    _page_metadata2->table().attach(_licensor, 0, row, 2, 1);
}

void DocumentProperties::addExternalScript(){

    auto document = getDocument();
    if (!document)
        return;

    if (_script_entry.get_text().empty() ) {
        // Click Add button with no filename, show a Browse dialog
        browseExternalScript();
    }

    if (!_script_entry.get_text().empty()) {
        Inkscape::XML::Document *xml_doc = document->getReprDoc();
        Inkscape::XML::Node *scriptRepr = xml_doc->createElement("svg:script");
        scriptRepr->setAttributeOrRemoveIfEmpty("xlink:href", _script_entry.get_text());
        _script_entry.set_text("");

        xml_doc->root()->addChild(scriptRepr, nullptr);

        // inform the document, so we can undo
        DocumentUndo::done(document, _("Add external script..."), "");

        populate_script_lists();
    }
}

static Inkscape::UI::Dialog::FileOpenDialog * selectPrefsFileInstance = nullptr;

void  DocumentProperties::browseExternalScript() {

    // Get the current directory for finding files.
    static std::string open_path;
    Inkscape::UI::Dialog::get_start_directory(open_path, _prefs_path);

    // Create a dialog.
    SPDesktop *desktop = getDesktop();
    if (desktop && !selectPrefsFileInstance) {
        selectPrefsFileInstance =
            Inkscape::UI::Dialog::FileOpenDialog::create(
                *desktop->getToplevel(),
                open_path,
                Inkscape::UI::Dialog::CUSTOM_TYPE,
                _("Select a script to load"));
        selectPrefsFileInstance->addFilterMenu(_("JavaScript Files"), "*.js");
    }

    // Show the dialog.
    bool const success = selectPrefsFileInstance->show();

    if (!success) {
        return;
    }

    // User selected something, get file.
    auto file = selectPrefsFileInstance->getFile();
    if (!file) {
        return;
    }

    auto path = file->get_path();
    if (!path.empty()) {
        open_path = path;;
    }

    if (!open_path.empty()) {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        prefs->setString(_prefs_path, open_path);
    }

    _script_entry.set_text(file->get_parse_name());
}

void DocumentProperties::addEmbeddedScript(){
    if(auto document = getDocument()) {
        Inkscape::XML::Document *xml_doc = document->getReprDoc();
        Inkscape::XML::Node *scriptRepr = xml_doc->createElement("svg:script");

        xml_doc->root()->addChild(scriptRepr, nullptr);

        // inform the document, so we can undo
        DocumentUndo::done(document, _("Add embedded script..."), "");
        populate_script_lists();
    }
}

void DocumentProperties::removeExternalScript(){
    Glib::ustring name;
    if(_ExternalScriptsList.get_selection()) {
        Gtk::TreeModel::iterator i = _ExternalScriptsList.get_selection()->get_selected();

        if(i){
            name = (*i)[_ExternalScriptsListColumns.filenameColumn];
        } else {
            return;
        }
    }

    auto document = getDocument();
    if (!document)
        return;
    std::vector<SPObject *> current = document->getResourceList( "script" );
    for (auto obj : current) {
        if (obj) {
            auto script = cast<SPScript>(obj);
            if (script && (name == script->xlinkhref)) {

                //XML Tree being used directly here while it shouldn't be.
                Inkscape::XML::Node *repr = obj->getRepr();
                if (repr){
                    sp_repr_unparent(repr);

                    // inform the document, so we can undo
                    DocumentUndo::done(document, _("Remove external script"), "");
                }
            }
        }
    }

    populate_script_lists();
}

void DocumentProperties::removeEmbeddedScript(){
    Glib::ustring id;
    if(_EmbeddedScriptsList.get_selection()) {
        Gtk::TreeModel::iterator i = _EmbeddedScriptsList.get_selection()->get_selected();

        if(i){
            id = (*i)[_EmbeddedScriptsListColumns.idColumn];
        } else {
            return;
        }
    }

    if (auto document = getDocument()) {
        if (auto obj = document->getObjectById(id)) {
            //XML Tree being used directly here while it shouldn't be.
            if (auto repr = obj->getRepr()){
                sp_repr_unparent(repr);

                // inform the document, so we can undo
                DocumentUndo::done(document, _("Remove embedded script"), "");
            }
        }
    }

    populate_script_lists();
}

void DocumentProperties::onExternalScriptSelectRow()
{
    Glib::RefPtr<Gtk::TreeSelection> sel = _ExternalScriptsList.get_selection();
    if (sel) {
        _external_remove_btn.set_sensitive(sel->count_selected_rows () > 0);
    }
}

void DocumentProperties::onEmbeddedScriptSelectRow()
{
    Glib::RefPtr<Gtk::TreeSelection> sel = _EmbeddedScriptsList.get_selection();
    if (sel) {
        _embed_remove_btn.set_sensitive(sel->count_selected_rows () > 0);
    }
}

void DocumentProperties::changeEmbeddedScript(){
    Glib::ustring id;
    if(_EmbeddedScriptsList.get_selection()) {
        Gtk::TreeModel::iterator i = _EmbeddedScriptsList.get_selection()->get_selected();

        if(i){
            id = (*i)[_EmbeddedScriptsListColumns.idColumn];
        } else {
            return;
        }
    }

    auto document = getDocument();
    if (!document)
        return;

    bool voidscript=true;
    std::vector<SPObject *> current = document->getResourceList( "script" );
    for (auto obj : current) {
        if (id == obj->getId()){
            int count = (int) obj->children.size();

            if (count>1)
                g_warning("TODO: Found a script element with multiple (%d) child nodes! We must implement support for that!", count);

            //XML Tree being used directly here while it shouldn't be.
            SPObject* child = obj->firstChild();
            //TODO: shouldn't we get all children instead of simply the first child?

            if (child && child->getRepr()){
                if (auto const content = child->getRepr()->content()) {
                    voidscript = false;
                    _EmbeddedContent.get_buffer()->set_text(content);
                }
            }
        }
    }

    if (voidscript)
        _EmbeddedContent.get_buffer()->set_text("");
}

void DocumentProperties::editEmbeddedScript(){
    Glib::ustring id;
    if(_EmbeddedScriptsList.get_selection()) {
        Gtk::TreeModel::iterator i = _EmbeddedScriptsList.get_selection()->get_selected();

        if(i){
            id = (*i)[_EmbeddedScriptsListColumns.idColumn];
        } else {
            return;
        }
    }

    auto document = getDocument();
    if (!document)
        return;

    for (auto obj : document->getResourceList("script")) {
        if (id == obj->getId()) {
            //XML Tree being used directly here while it shouldn't be.
            Inkscape::XML::Node *repr = obj->getRepr();
            if (repr){
                auto tmp = obj->children | boost::adaptors::transformed([](SPObject& o) { return &o; });
                std::vector<SPObject*> vec(tmp.begin(), tmp.end());
                for (auto const child: vec) {
                    child->deleteObject();
                }
                obj->appendChildRepr(document->getReprDoc()->createTextNode(_EmbeddedContent.get_buffer()->get_text().c_str()));

                //TODO repr->set_content(_EmbeddedContent.get_buffer()->get_text());

                // inform the document, so we can undo
                DocumentUndo::done(document, _("Edit embedded script"), "");
            }
        }
    }
}

void DocumentProperties::populate_script_lists(){
    _ExternalScriptsListStore->clear();
    _EmbeddedScriptsListStore->clear();
    auto document = getDocument();
    if (!document)
        return;

    std::vector<SPObject *> current = getDocument()->getResourceList( "script" );
    if (!current.empty()) {
        SPObject *obj = *(current.begin());
        g_assert(obj != nullptr);
        _scripts_observer.set(obj->parent);
    }
    for (auto obj : current) {
        auto script = cast<SPScript>(obj);
        g_assert(script != nullptr);
        if (script->xlinkhref)
        {
            Gtk::TreeModel::Row row = *(_ExternalScriptsListStore->append());
            row[_ExternalScriptsListColumns.filenameColumn] = script->xlinkhref;
        }
        else // Embedded scripts
        {
            Gtk::TreeModel::Row row = *(_EmbeddedScriptsListStore->append());
            row[_EmbeddedScriptsListColumns.idColumn] = obj->getId();
        }
    }
}

/**
* Called for _updating_ the dialog. DO NOT call this a lot. It's expensive!
* Will need to probably create a GridManager with signals to each Grid attribute
*/
void DocumentProperties::rebuild_gridspage()
{
    while (_grids_notebook.get_n_pages() != 0) {
        _grids_notebook.remove_page(-1); // this also deletes the page.
    }
    for (auto grid : getDesktop()->getNamedView()->grids) {
        add_grid_widget(grid);
    }
    _grids_button_remove.set_sensitive(_grids_notebook.get_n_pages() > 0);
}

void DocumentProperties::add_grid_widget(SPGrid *grid, bool select)
{
    auto const widget = Gtk::make_managed<Inkscape::UI::Widget::GridWidget>(grid);
    _grids_notebook.append_page(*widget, *widget->getTabWidget());

    _grids_button_remove.set_sensitive(true);
    if (select) {
        _grids_notebook.set_current_page(_grids_notebook.get_n_pages() - 1);
    }
}

void DocumentProperties::remove_grid_widget(XML::Node &node)
{
    // The SPObject is already gone, so we're working from the xml node directly.
    for (auto const child : UI::get_children(_grids_notebook)) {
        if (auto widget = dynamic_cast<Inkscape::UI::Widget::GridWidget *>(child)) {
            if (&node == widget->getGridRepr()) {
                _grids_notebook.remove_page(*child);
                break;
            }
        }
    }

    _grids_button_remove.set_sensitive(_grids_notebook.get_n_pages() > 0);
}

/**
 * Build grid page of dialog.
 */
void DocumentProperties::build_gridspage()
{
    /// \todo FIXME: gray out snapping when grid is off.
    /// Dissenting view: you want snapping without grid.

    _grids_label_crea.set_markup(_("<b>Creation</b>"));
    _grids_label_crea.add_css_class("heading");
    _grids_label_def.set_markup(_("<b>Defined grids</b>"));
    _grids_label_def.add_css_class("heading");
    _grids_hbox_crea.set_spacing(5);
    UI::pack_start(_grids_hbox_crea, *Gtk::make_managed<Gtk::Label>("Add grid:"), false, true);
    auto const btn_size = Gtk::SizeGroup::create(Gtk::SizeGroup::Mode::HORIZONTAL);
    for (auto const &[label, type, icon]: {std::tuple
        {C_("Grid", "Rectangular"), GridType::RECTANGULAR, "grid-rectangular"},
        {C_("Grid", "Axonometric"), GridType::AXONOMETRIC, "grid-axonometric"},
        {C_("Grid", "Modular"), GridType::MODULAR, "grid-modular"}
    }) {
        auto const btn = Gtk::make_managed<Gtk::Button>(label, false);
        btn->set_image_from_icon_name(icon, Gtk::IconSize::NORMAL);
        btn_size->add_widget(*btn);
        UI::pack_start(_grids_hbox_crea, *btn, false, true);
        btn->signal_clicked().connect([this, type = type]{ onNewGrid(type); });
    }

    _grids_vbox.set_name("NotebookPage");
    _grids_vbox.set_margin(4);
    _grids_vbox.set_spacing(4);
    UI::pack_start(_grids_vbox, _grids_label_crea, false, false);
    UI::pack_start(_grids_vbox, _grids_hbox_crea, false, false);
    UI::pack_start(_grids_vbox, _grids_label_def, false, false);
    UI::pack_start(_grids_vbox, _grids_notebook, false, false);
    _grids_notebook.set_scrollable();
    UI::pack_start(_grids_vbox, _grids_button_remove, false, false);
}

void DocumentProperties::update_viewbox(SPDesktop* desktop) {
    if (!desktop) return;

    auto* document = desktop->getDocument();
    if (!document) return;

    using UI::Widget::PageProperties;
    SPRoot* root = document->getRoot();
    if (root->viewBox_set) {
        auto& vb = root->viewBox;
        _page->set_dimension(PageProperties::Dimension::ViewboxPosition, vb.min()[Geom::X], vb.min()[Geom::Y]);
        _page->set_dimension(PageProperties::Dimension::ViewboxSize, vb.width(), vb.height());
    }

    update_scale_ui(desktop);
}

/**
 * Update dialog widgets from desktop. Also call updateWidget routines of the grids.
 */
void DocumentProperties::update_widgets()
{
    auto desktop = getDesktop();
    auto document = getDocument();
    if (_wr.isUpdating() || !document) return;

    auto nv = desktop->getNamedView();
    auto &page_manager = document->getPageManager();

    _wr.setUpdating(true);

    SPRoot *root = document->getRoot();

    double doc_w = root->width.value;
    Glib::ustring doc_w_unit = Util::UnitTable::get().getUnit(root->width.unit)->abbr;
    bool percent = doc_w_unit == "%";
    if (doc_w_unit == "") {
        doc_w_unit = "px";
    } else if (doc_w_unit == "%" && root->viewBox_set) {
        doc_w_unit = "px";
        doc_w = root->viewBox.width();
    }
    double doc_h = root->height.value;
    Glib::ustring doc_h_unit = Util::UnitTable::get().getUnit(root->height.unit)->abbr;
    percent = percent || doc_h_unit == "%";
    if (doc_h_unit == "") {
        doc_h_unit = "px";
    } else if (doc_h_unit == "%" && root->viewBox_set) {
        doc_h_unit = "px";
        doc_h = root->viewBox.height();
    }
    using UI::Widget::PageProperties;
    // dialog's behavior is not entirely correct when document sizes are expressed in '%', so put up a disclaimer
    _page->set_check(PageProperties::Check::UnsupportedSize, percent);

    _page->set_dimension(PageProperties::Dimension::PageSize, doc_w, doc_h);
    _page->set_unit(PageProperties::Units::Document, doc_w_unit);

    update_viewbox_ui(desktop);
    update_scale_ui(desktop);

    if (nv->display_units) {
        _page->set_unit(PageProperties::Units::Display, nv->display_units->abbr);
    }
    _page->set_check(PageProperties::Check::Checkerboard, nv->desk_checkerboard);
    _page->set_color(PageProperties::Color::Desk, nv->desk_color);
    _page->set_color(PageProperties::Color::Background, page_manager.background_color);
    _page->set_check(PageProperties::Check::Border, page_manager.border_show);
    _page->set_check(PageProperties::Check::BorderOnTop, page_manager.border_on_top);
    _page->set_color(PageProperties::Color::Border, page_manager.border_color);
    _page->set_check(PageProperties::Check::Shadow, page_manager.shadow_show);
    _page->set_check(PageProperties::Check::PageLabelStyle, page_manager.label_style != "default");
    _page->set_check(PageProperties::Check::AntiAlias, nv->antialias_rendering);
    _page->set_check(PageProperties::Check::ClipToPage, nv->clip_to_page);

    //-----------------------------------------------------------guide page

    _rcb_sgui.setActive (nv->getShowGuides());
    _rcb_lgui.setActive (nv->getLockGuides());
    _rcp_gui.setRgba32 (nv->guidecolor);
    _rcp_hgui.setRgba32 (nv->guidehicolor);

    //------------------------------------------------Color Management page

    populate_linked_profiles_box();

    //-----------------------------------------------------------meta pages
    // update the RDF entities; note that this may modify document, maybe doc-undo should be called?
    if (auto document = getDocument()) {
        for (auto const &it : _rdflist) {
            bool read_only = false;
            it->update(document, read_only);
        }
        _licensor.update(document);
    }
    _wr.setUpdating (false);
}

//--------------------------------------------------------------------

void DocumentProperties::on_response (int id)
{
    if (id == Gtk::ResponseType::DELETE_EVENT || id == Gtk::ResponseType::CLOSE)
    {
        _rcp_gui.closeWindow();
        _rcp_hgui.closeWindow();
    }

    if (id == Gtk::ResponseType::CLOSE)
        set_visible(false);
}

void DocumentProperties::load_default_metadata()
{
    /* Get the data RDF entities data from preferences*/
    for (auto const &it : _rdflist) {
        it->load_from_preferences ();
    }
}

void DocumentProperties::save_default_metadata()
{
    /* Save these RDF entities to preferences*/
    if (auto document = getDocument()) {
        for (auto const &it : _rdflist) {
            it->save_to_preferences(document);
        }
    }
}

void DocumentProperties::WatchConnection::connect(Inkscape::XML::Node *node)
{
    disconnect();
    if (!node) return;

    _node = node;
    _node->addObserver(*this);
}

void DocumentProperties::WatchConnection::disconnect() {
    if (_node) {
        _node->removeObserver(*this);
        _node = nullptr;
    }
}

void DocumentProperties::WatchConnection::notifyChildAdded(XML::Node&, XML::Node &child, XML::Node*)
{
    if (auto grid = cast<SPGrid>(_dialog->getDocument()->getObjectByRepr(&child))) {
        _dialog->add_grid_widget(grid, true);
    }
}

void DocumentProperties::WatchConnection::notifyChildRemoved(XML::Node&, XML::Node &child, XML::Node*)
{
    _dialog->remove_grid_widget(child);
}

void DocumentProperties::WatchConnection::notifyAttributeChanged(XML::Node&, GQuark, Util::ptr_shared, Util::ptr_shared)
{
    _dialog->update_widgets();
}

void DocumentProperties::documentReplaced()
{
    _root_connection.disconnect();
    _namedview_connection.disconnect();

    if (auto desktop = getDesktop()) {
        _wr.setDesktop(desktop);
        _namedview_connection.connect(desktop->getNamedView()->getRepr());
        if (auto document = desktop->getDocument()) {
            _root_connection.connect(document->getRoot()->getRepr());
        }
        populate_linked_profiles_box();
        update_widgets();
        rebuild_gridspage();
    }
}

void DocumentProperties::update()
{
    update_widgets();
}

/*########################################################################
# BUTTON CLICK HANDLERS    (callbacks)
########################################################################*/

void DocumentProperties::onNewGrid(GridType grid_type)
{
    auto desktop = getDesktop();
    auto document = getDocument();
    if (!desktop || !document) return;

    auto repr = desktop->getNamedView()->getRepr();
    SPGrid::create_new(document, repr, grid_type);
    // flip global switch, so snapping to grid works
    desktop->getNamedView()->newGridCreated();

    DocumentUndo::done(document, _("Create new grid"), INKSCAPE_ICON("document-properties"));
}

void DocumentProperties::onRemoveGrid()
{
    int pagenum = _grids_notebook.get_current_page();
    if (pagenum == -1) // no pages
      return;

    if (auto widget = dynamic_cast<Inkscape::UI::Widget::GridWidget *>(_grids_notebook.get_nth_page(pagenum))) {
        widget->getGrid()->deleteObject();
        DocumentUndo::done(getDocument(), _("Remove grid"), INKSCAPE_ICON("document-properties"));
    } else {
        g_warning("Can't find GridWidget for currently selected grid.");
    }
}

/* This should not effect anything in the SVG tree (other than "inkscape:document-units").
   This should only effect values displayed in the GUI. */
void DocumentProperties::display_unit_change(const Inkscape::Util::Unit* doc_unit)
{
    SPDocument *document = getDocument();
    // Don't execute when change is being undone
    if (!document || !DocumentUndo::getUndoSensitive(document)) {
        return;
    }
    // Don't execute when initializing widgets
    if (_wr.isUpdating()) {
        return;
    }

    auto action = document->getActionGroup()->lookup_action("set-display-unit");
    action->activate(doc_unit->abbr);
}

} // namespace Dialog

namespace Widget {

GridWidget::GridWidget(SPGrid *grid)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , _grid(grid)
    , repr(grid->getRepr())
{
    Inkscape::XML::Node *repr = grid->getRepr();
    auto doc = grid->document;

    // Tab label is constructed here and passed back to parent widget for display to
    // reduce the number of watchers that have to keep tabs on the properties
    _tab = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    _tab->set_halign(Gtk::Align::START);
    _tab->set_hexpand(false);
    _tab_img = Gtk::make_managed<Gtk::Image>();
    _tab_lbl = Gtk::make_managed<Gtk::Label>("-", true);
    UI::pack_start(*_tab, *_tab_img);
    UI::pack_start(*_tab, *_tab_lbl);

    _name_label = Gtk::make_managed<Gtk::Label>("", Gtk::Align::CENTER);
    _name_label->set_margin_bottom(4);
    _name_label->add_css_class("heading");
    _name_label->add_css_class("heading");
    UI::pack_start(*this, *_name_label, false, false);

    _wr.setUpdating(true);
    _grid_rcb_enabled = Gtk::make_managed<Inkscape::UI::Widget::RegisteredCheckButton>(
            _("_Enabled"),
            _("Makes the grid available for working with on the canvas."),
            "enabled", _wr, false, repr, doc);

    _grid_rcb_snap_visible_only = Gtk::make_managed<Inkscape::UI::Widget::RegisteredCheckButton>(
            _("Snap to visible _grid lines only"),
            _("When zoomed out, not all grid lines will be displayed. Only the visible ones will be snapped to"),
            "snapvisiblegridlinesonly", _wr, false, repr, doc);

    _grid_rcb_visible = Gtk::make_managed<Inkscape::UI::Widget::RegisteredCheckButton>(
            _("_Visible"),
            _("Determines whether the grid is displayed or not. Objects are still snapped to invisible grids."),
            "visible", _wr, false, repr, doc);

    _grid_as_alignment = Gtk::make_managed<Inkscape::UI::Widget::AlignmentSelector>();
    _grid_as_alignment->connectAlignmentClicked([this, grid](int const align) {
        auto dimensions = grid->document->getDimensions();
        dimensions[Geom::X] *= align % 3 * 0.5;
        dimensions[Geom::Y] *= align / 3 * 0.5;
        dimensions *= grid->document->doc2dt();
        grid->setOrigin(dimensions);
    });

    auto const left = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    UI::pack_start(*left, *_grid_rcb_enabled, false, false);
    UI::pack_start(*left, *_grid_rcb_visible, false, false);
    UI::pack_start(*left, *_grid_rcb_snap_visible_only, false, false);
    if (auto label = dynamic_cast<Gtk::Label*>(_grid_rcb_snap_visible_only->get_child())) {
        label->set_wrap();
    }

    _grid_rcb_dotted = Gtk::make_managed<Inkscape::UI::Widget::RegisteredCheckButton>(
            _("_Show dots instead of lines"), _("If set, displays dots at gridpoints instead of gridlines"),
            "dotted", _wr, false, repr, doc );
    UI::pack_start(*left, *_grid_rcb_dotted, false, false);

    auto align = Gtk::make_managed<Gtk::Label>(_("Align to page:"));
    align->set_margin_top(8);
    UI::pack_start(*left, *align, false, false);
    UI::pack_start(*left, *_grid_as_alignment, false, false);

    _angle_popup = Gtk::make_managed<Gtk::MenuButton>();
    auto angle_popover = Gtk::make_managed<Gtk::Popover>();
    _angle_popup->set_popover(*angle_popover);
    _angle_popup->set_valign(Gtk::Align::CENTER);
    // set grid angles from given width to height ratio
    auto angle = Gtk::make_managed<Gtk::Label>(_("Set angle from aspect ratio:"));
    angle->set_xalign(0);
    auto subgrid = Gtk::make_managed<Gtk::Grid>();
    subgrid->set_margin_top(8);
    subgrid->set_margin_bottom(8);
    subgrid->set_margin_start(8);
    subgrid->set_margin_end(8);
    subgrid->set_row_spacing(4);
    subgrid->set_column_spacing(4);
    _aspect_ratio = Gtk::make_managed<Gtk::Entry>();
    _aspect_ratio->set_max_width_chars(9);
    subgrid->attach(*angle, 0, 0);
    subgrid->attach(*_aspect_ratio, 0, 1);
    auto apply = Gtk::make_managed<Gtk::Button>(_("Set"));
    apply->set_halign(Gtk::Align::CENTER);
    apply->set_size_request(100);
    subgrid->attach(*apply, 0, 2);
    // TRANSLATORS: Axonometric grid looks like a pattern of parallelograms. Their width to height proportions
    // can be manipulated by changing angles in the axonometric grid. This DX/DY ratio does just that.
    // Pressing "Set" button will calculate grid angles to produce parallelograms with requested widh to height ratio.
    apply->set_tooltip_text(_("Set grid angles to make grid parallelograms width to height\nproportion match given aspect ratio"));
    apply->signal_clicked().connect([=, this](){
        auto text = _aspect_ratio->get_text();
        try {
            ExpressionEvaluator ex(text.c_str());
            auto result = ex.evaluate();
            if (!std::isfinite(result.value) || result.value <= 0) return;

            auto angle = Geom::deg_from_rad(std::atan(1.0 / result.value));
            if (angle > 0.0 && angle < 90.0) {
                _rsu_ax->setValue(angle, false);
                _rsu_az->setValue(angle, false);
            }
        }
        catch (EvaluatorException& e) {
            // ignoring user input error for now
        }
    });
    angle_popover->set_child(*subgrid);
    angle_popover->signal_show().connect([=](){
        if (!_grid) return;

        auto ax = _grid->getAngleX();
        auto az = _grid->getAngleZ();
        // try to guess ratio if angles are the same, otherwise leave ratio boxes intact
        if (az == ax) {
            auto ratio = std::tan(Geom::rad_from_deg(ax));
            if (ratio > 0) {
                _aspect_ratio->set_text(ratio > 1.0 ?
                    Glib::ustring::format("1 : ", ratio) :
                    Glib::ustring::format(1.0 / ratio, " : 1")
                );
            }
        }
    });

    _rumg = Gtk::make_managed<RegisteredUnitMenu>(
                _("Grid _units:"), "units", _wr, repr, doc);
    _rsu_ox = Gtk::make_managed<RegisteredScalarUnit>(
                _("_Origin X:"), _("X coordinate of grid origin"), "originx",
                *_rumg, _wr, repr, doc, RSU_x);
    _rsu_oy = Gtk::make_managed<RegisteredScalarUnit>(
                _("O_rigin Y:"), _("Y coordinate of grid origin"), "originy",
                *_rumg, _wr, repr, doc, RSU_y);
    _rsu_sx = Gtk::make_managed<RegisteredScalarUnit>(
                "-", _("Distance between horizontal grid lines"), "spacingx",
                *_rumg, _wr, repr, doc, RSU_x);
    _rsu_sy = Gtk::make_managed<RegisteredScalarUnit>(
                "-", _("Distance between vertical grid lines"), "spacingy",
                *_rumg, _wr, repr, doc, RSU_y);

    _rsu_gx = Gtk::make_managed<RegisteredScalarUnit>(
                _("Gap _X:"), _("Horizontal distance between blocks"), "gapx",
                *_rumg, _wr, repr, doc, RSU_x);
    _rsu_gy = Gtk::make_managed<RegisteredScalarUnit>(
                _("Gap _Y:"), _("Vertical distance between blocks"), "gapy",
                *_rumg, _wr, repr, doc, RSU_y);
    _rsu_mx = Gtk::make_managed<RegisteredScalarUnit>(
                _("_Margin X:"), _("Horizontal block margin"), "marginx",
                *_rumg, _wr, repr, doc, RSU_x);
    _rsu_my = Gtk::make_managed<RegisteredScalarUnit>(
                _("M_argin Y:"), _("Vertical block margin"), "marginy",
                *_rumg, _wr, repr, doc, RSU_y);

    _rsu_ax = Gtk::make_managed<RegisteredScalar>(
                _("An_gle X:"), _("Angle of x-axis"), "gridanglex", _wr, repr, doc);
    _rsu_az = Gtk::make_managed<RegisteredScalar>(
                _("Ang_le Z:"), _("Angle of z-axis"), "gridanglez", _wr, repr, doc);
    _rcp_gcol = Gtk::make_managed<RegisteredColorPicker>(
                _("Minor grid line _color:"), _("Minor grid line color"), _("Color of the minor grid lines"),
                "color", "opacity", _wr, repr, doc);
    _rcp_gmcol = Gtk::make_managed<RegisteredColorPicker>(
                _("Ma_jor grid line color:"), _("Major grid line color"),
                _("Color of the major (highlighted) grid lines"),
                "empcolor", "empopacity", _wr, repr, doc);
    _rsi = Gtk::make_managed<RegisteredInteger>(
                _("Major grid line e_very:"), _("Number of lines"),
                "empspacing", _wr, repr, doc);

    for (auto labelled : std::to_array<Labelled*>(
        {_rumg, _rsu_ox, _rsu_oy, _rsu_sx, _rsu_sy, _rsu_gx, _rsu_gy, _rsu_mx, _rsu_my,
            _rsu_ax, _rsu_az, _rcp_gcol, _rcp_gmcol, _rsi})) {
        labelled->getLabel()->set_hexpand();
    }

    _rumg->set_hexpand();
    _rsu_ax->set_hexpand();
    _rsu_az->set_hexpand();
    _rcp_gcol->set_hexpand();
    _rcp_gmcol->set_hexpand();
    _rsi->set_hexpand();
    _rsi->setWidthChars(5);

    _rsu_ox->setProgrammatically = false;
    _rsu_oy->setProgrammatically = false;

    auto const column = Gtk::make_managed<Gtk::Grid>();
    column->set_row_spacing(4);
    column->set_column_spacing(4);
    int row = 0;
    column->attach(*_rumg, 0, row++);

    int angle_row = 0;
    for (auto rs : std::to_array<Scalar*>({_rsu_ox, _rsu_oy, _rsu_sx, _rsu_sy, _rsu_ax, _rsu_az, _rsu_gx, _rsu_gy, _rsu_mx, _rsu_my})) {
        rs->setDigits(6);
        rs->setIncrements(0.1, 1.0);
        rs->set_hexpand();
        rs->setWidthChars(12);
        if (rs == _rsu_ax) angle_row = row;
        column->attach(*rs, 0, row++);
    }

    column->attach(*_rcp_gcol, 0, row++);
    column->attach(*_rcp_gmcol, 0, row++);
    column->attach(*_rsi, 0, row++);
    column->attach(*_angle_popup, 1, angle_row, 1, 2);

    _modified_signal = grid->connectModified([this, grid](SPObject const * /*obj*/, unsigned /*flags*/) {
        update();
    });
    update();

    column->set_hexpand(false);

    auto const inner = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    UI::pack_start(*inner, *left, true, true);
    UI::pack_start(*inner, *column, false, false);
    UI::pack_start(*this, *inner, false, false);
    set_margin(4);

    auto widgets = UI::get_children(*left);
    widgets.erase(std::remove(widgets.begin(), widgets.end(), _grid_rcb_enabled), widgets.end());
    widgets.push_back(column);
    _grid_rcb_enabled->setSubordinateWidgets(std::move(widgets));

    _wr.setUpdating(false);
}

/**
 * Keep the grid up to date with it's values.
 */
void GridWidget::update()
{
    _wr.setUpdating (true);
    auto scale = _grid->document->getDocumentScale();

    const auto modular = _grid->getType() == GridType::MODULAR;
    const auto axonometric = _grid->getType() == GridType::AXONOMETRIC;
    const auto rectangular = _grid->getType() == GridType::RECTANGULAR;

    _rumg->setUnit(_grid->getUnit()->abbr);

    // Doc to px so unit is conserved in RegisteredScalerUnit
    auto origin = _grid->getOrigin() * scale;
    _rsu_ox->setValueKeepUnit(origin[Geom::X], "px");
    _rsu_oy->setValueKeepUnit(origin[Geom::Y], "px");

    auto spacing = _grid->getSpacing() * scale;
    _rsu_sx->setValueKeepUnit(spacing[Geom::X], "px");
    _rsu_sy->setValueKeepUnit(spacing[Geom::Y], "px");
    _rsu_sx->getLabel()->set_markup_with_mnemonic(modular ? _("Block _width:") : _("Spacing _X:"));
    _rsu_sy->getLabel()->set_markup_with_mnemonic(modular ? _("Block _height:") : _("Spacing _Y:"));

    auto show = [](Gtk::Widget* w, bool do_show){
        w->set_visible(do_show);
    };

    show(_rsu_ax, axonometric);
    show(_rsu_az, axonometric);
    show(_angle_popup, axonometric);
    if (axonometric) {
        _rsu_ax->setValue(_grid->getAngleX());
        _rsu_az->setValue(_grid->getAngleZ());
    }

    show(_rsu_gx, modular);
    show(_rsu_gy, modular);
    show(_rsu_mx, modular);
    show(_rsu_my, modular);
    if (modular) {
        auto gap = _grid->get_gap() * scale;
        auto margin = _grid->get_margin() * scale;
        _rsu_gx->setValueKeepUnit(gap.x(), "px");
        _rsu_gy->setValueKeepUnit(gap.y(), "px");
        _rsu_mx->setValueKeepUnit(margin.x(), "px");
        _rsu_my->setValueKeepUnit(margin.y(), "px");
    }

    _rcp_gcol->setRgba32 (_grid->getMinorColor());
    _rcp_gmcol->setRgba32 (_grid->getMajorColor());

    show(_rsi, !modular);
    _rsi->setValue(_grid->getMajorLineInterval());

    _grid_rcb_enabled->setActive(_grid->isEnabled());
    _grid_rcb_visible->setActive(_grid->isVisible());

    if (_grid_rcb_dotted)
        _grid_rcb_dotted->setActive(_grid->isDotted());

    show(_grid_rcb_snap_visible_only, !modular);
    _grid_rcb_snap_visible_only->setActive(_grid->getSnapToVisibleOnly());
    // which condition to use to call setActive?
    // _grid_rcb_snap_visible_only->setActive(grid->snapper()->getSnapVisibleOnly());
    _grid_rcb_enabled->setActive(_grid->snapper()->getEnabled());

    show(_grid_rcb_dotted, rectangular);
    show(_rsu_sx, !axonometric);

    _name_label->set_markup(Glib::ustring("<b>") + _grid->displayName() + "</b>");
    _tab_lbl->set_label(_grid->getId() ? _grid->getId() : "-");
    _tab_img->set_from_icon_name(_grid->typeName());

    _wr.setUpdating (false);
}

} // namespace Widget

} // namespace Inkscape::UI

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
