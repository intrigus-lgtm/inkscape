// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Gradient aux toolbar
 *
 * Authors:
 *   bulia byak <bulia@dr.com>
 *   Johan Engelen <j.b.c.engelen@ewi.utwente.nl>
 *   Abhishek Sharma
 *   Tavmjong Bah <tavjong@free.fr>
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Copyright (C) 2012 Tavmjong Bah
 * Copyright (C) 2007 Johan Engelen
 * Copyright (C) 2005 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "mesh-toolbar.h"

#include <glibmm/i18n.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/liststore.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/togglebutton.h>

#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "gradient-drag.h"
#include "inkscape.h"
#include "object/sp-defs.h"
#include "object/sp-mesh-gradient.h"
#include "selection.h"
#include "style.h"
#include "ui/builder-utils.h"
#include "ui/dialog-run.h"
#include "ui/icon-names.h"
#include "ui/simple-pref-pusher.h"
#include "ui/tools/mesh-tool.h"
#include "ui/util.h"
#include "ui/widget/canvas.h"
#include "ui/widget/combo-tool-item.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/toolbar-menu-button.h"

using Inkscape::DocumentUndo;
using Inkscape::UI::Tools::MeshTool;

static bool blocked = false;

// Get a list of selected meshes taking into account fill/stroke toggles
std::vector<SPMeshGradient *>  ms_get_dt_selected_gradients(Inkscape::Selection *selection)
{
    std::vector<SPMeshGradient *> ms_selected;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool edit_fill   = prefs->getBool("/tools/mesh/edit_fill",   true);
    bool edit_stroke = prefs->getBool("/tools/mesh/edit_stroke", true);

    auto itemlist= selection->items();
    for(auto i=itemlist.begin();i!=itemlist.end();++i){
        SPItem *item = *i;// get the items gradient, not the getVector() version
        SPStyle *style = item->style;

        if (style) {
            if (edit_fill   && style->fill.isPaintserver()) {
                SPPaintServer *server = item->style->getFillPaintServer();
                auto mesh = cast<SPMeshGradient>(server);
                if (mesh) {
                    ms_selected.push_back(mesh);
                }
            }

            if (edit_stroke && style->stroke.isPaintserver()) {
                SPPaintServer *server = item->style->getStrokePaintServer();
                auto mesh = cast<SPMeshGradient>(server);
                if (mesh) {
                    ms_selected.push_back(mesh);
                }
            }
        }
    }
    return ms_selected;
}

/*
 * Get the current selection status from the desktop
 */
void ms_read_selection( Inkscape::Selection *selection,
                        SPMeshGradient *&ms_selected,
                        bool &ms_selected_multi,
                        SPMeshType &ms_type,
                        bool &ms_type_multi )
{
    ms_selected = nullptr;
    ms_selected_multi = false;
    ms_type = SP_MESH_TYPE_COONS;
    ms_type_multi = false;

    bool first = true;

    // Read desktop selection, taking into account fill/stroke toggles
    std::vector<SPMeshGradient *> meshes = ms_get_dt_selected_gradients( selection );
    for (auto & meshe : meshes) {
        if (first) {
            ms_selected = meshe;
            ms_type = meshe->type;
            first = false;
        } else {
            if (ms_selected != meshe) {
                ms_selected_multi = true;
            }
            if (ms_type != meshe->type) {
                ms_type_multi = true;
            }
        }
    }
}

/*
 * Callback functions for user actions
 */
/** Temporary hack: Returns the mesh tool in the active desktop.
 * Will go away during tool refactoring. */
static MeshTool *get_mesh_tool()
{
    MeshTool *mesh_tool = nullptr;
    if (SP_ACTIVE_DESKTOP ) {
        auto const tool = SP_ACTIVE_DESKTOP->getTool();
        mesh_tool = dynamic_cast<MeshTool *>(tool);
    }
    return mesh_tool;
}

namespace Inkscape::UI::Toolbar {

MeshToolbar::MeshToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    // , _edit_fill_pusher(nullptr)
    , _builder(create_builder("toolbar-mesh.ui"))
    , _row_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_row_item"))
    , _col_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_col_item"))
    , _edit_fill_btn(&get_widget<Gtk::ToggleButton>(_builder, "_edit_fill_btn"))
    , _edit_stroke_btn(&get_widget<Gtk::ToggleButton>(_builder, "_edit_stroke_btn"))
{
    auto prefs = Preferences::get();

    _toolbar = &get_widget<Gtk::Box>(_builder, "mesh-toolbar");

    auto show_handles_btn = &get_widget<Gtk::ToggleButton>(_builder, "show_handles_btn");

    // Configure the types combo box.
    UI::Widget::ComboToolItemColumns columns;
    Glib::RefPtr<Gtk::ListStore> store = Gtk::ListStore::create(columns);
    Gtk::TreeModel::Row row;

    row = *(store->append());
    row[columns.col_label] = C_("Type", "Coons");
    row[columns.col_sensitive] = true;

    row = *(store->append());
    row[columns.col_label] = _("Bicubic");
    row[columns.col_sensitive] = true;

    _select_type_item = Gtk::manage(UI::Widget::ComboToolItem::create(
        _("Smoothing"),
        // TRANSLATORS: Type of Smoothing. See https://en.wikipedia.org/wiki/Coons_patch
        _("Coons: no smoothing. Bicubic: smoothing across patch boundaries."), "Not Used", store));
    _select_type_item->use_group_label(true);
    _select_type_item->set_active(0);

    _select_type_item->signal_changed().connect(sigc::mem_fun(*this, &MeshToolbar::type_changed));
    get_widget<Gtk::Box>(_builder, "select_type_box").append(*_select_type_item);

    // Setup the spin buttons.
    setup_derived_spin_button(_row_item, "mesh_rows", 1, &MeshToolbar::row_changed);
    setup_derived_spin_button(_col_item, "mesh_cols", 1, &MeshToolbar::col_changed);

    // Configure mode buttons
    int mode = prefs->getInt("/tools/mesh/mesh_geometry", SP_MESH_GEOMETRY_NORMAL);

    int btn_index = 0;
    for_each_child(get_widget<Gtk::Box>(_builder, "new_type_buttons_box"), [&](Gtk::Widget &item){
        auto &btn = dynamic_cast<Gtk::ToggleButton &>(item);
        btn.set_active(btn_index == mode);
        btn.signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &MeshToolbar::new_geometry_changed), btn_index++));
        return ForEachResult::_continue;
    });

    mode = prefs->getInt("/tools/mesh/newfillorstroke");

    btn_index = 0;
    for_each_child(get_widget<Gtk::Box>(_builder, "new_fillstroke_buttons_box"), [&](Gtk::Widget &item){
        auto &btn = dynamic_cast<Gtk::ToggleButton &>(item);
        btn.set_active(btn_index == mode);
        btn.signal_clicked().connect(
            sigc::bind(sigc::mem_fun(*this, &MeshToolbar::new_fillstroke_changed), btn_index++));
        return ForEachResult::_continue;
    });

    // Edit fill mesh.
    _edit_fill_pusher.reset(new UI::SimplePrefPusher(_edit_fill_btn, "/tools/mesh/edit_fill"));

    // Edit stroke mesh.
    _edit_stroke_pusher.reset(new UI::SimplePrefPusher(_edit_stroke_btn, "/tools/mesh/edit_stroke"));

    // Show/hide side and tensor handles.
    _show_handles_pusher.reset(new UI::SimplePrefPusher(show_handles_btn, "/tools/mesh/show_handles"));

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

    set_child(*_toolbar);

    // Signals.
    _edit_fill_btn->signal_toggled().connect(sigc::mem_fun(*this, &MeshToolbar::toggle_fill_stroke));
    _edit_stroke_btn->signal_toggled().connect(sigc::mem_fun(*this, &MeshToolbar::toggle_fill_stroke));
    show_handles_btn->signal_toggled().connect(sigc::mem_fun(*this, &MeshToolbar::toggle_handles));
    get_widget<Gtk::Button>(_builder, "toggle_sides_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &MeshToolbar::toggle_sides));
    get_widget<Gtk::Button>(_builder, "make_elliptical_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &MeshToolbar::make_elliptical));
    get_widget<Gtk::Button>(_builder, "pick_colors_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &MeshToolbar::pick_colors));
    get_widget<Gtk::Button>(_builder, "scale_mesh_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &MeshToolbar::fit_mesh));

    get_widget<Gtk::Button>(_builder, "warning_btn").signal_clicked().connect([this] { warning_popup(); });

    desktop->connectEventContextChanged(sigc::mem_fun(*this, &MeshToolbar::watch_ec));
}

MeshToolbar::~MeshToolbar() = default;

void MeshToolbar::setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name,
                                            double default_value, ValueChangedMemFun const value_changed_mem_fun)
{
    const Glib::ustring path = "/tools/mesh/" + name;
    auto const val = Preferences::get()->getDouble(path, default_value);

    auto adj = btn.get_adjustment();
    adj->set_value(val);
    adj->signal_value_changed().connect(sigc::mem_fun(*this, value_changed_mem_fun));

    btn.set_defocus_widget(_desktop->getCanvas());
}

void MeshToolbar::new_geometry_changed(int mode)
{
    Preferences::get()->setInt("/tools/mesh/mesh_geometry", mode);
}

void MeshToolbar::new_fillstroke_changed(int mode)
{
    Preferences::get()->setInt("/tools/mesh/newfillorstroke", mode);
}

void MeshToolbar::row_changed()
{
    if (blocked) {
        return;
    }

    blocked = TRUE;

    int rows = _row_item.get_adjustment()->get_value();

    Preferences::get()->setInt("/tools/mesh/mesh_rows", rows);

    blocked = FALSE;
}

void MeshToolbar::col_changed()
{
    if (blocked) {
        return;
    }

    blocked = TRUE;

    int cols = _col_item.get_adjustment()->get_value();

    Preferences::get()->setInt("/tools/mesh/mesh_cols", cols);

    blocked = FALSE;
}

void MeshToolbar::toggle_fill_stroke()
{
    auto prefs = Inkscape::Preferences::get();
    prefs->setBool("/tools/mesh/edit_fill", _edit_fill_btn->get_active());
    prefs->setBool("/tools/mesh/edit_stroke", _edit_stroke_btn->get_active());

    MeshTool *mt = get_mesh_tool();
    if (mt) {
        GrDrag *drag = mt->get_drag();
        drag->updateDraggers();
        drag->updateLines();
        drag->updateLevels();
        selection_changed(nullptr); // Need to update Type widget
    }
}

void MeshToolbar::toggle_handles()
{
    MeshTool *mt = get_mesh_tool();
    if (mt) {
        GrDrag *drag = mt->get_drag();
        drag->refreshDraggers();
    }
}

void MeshToolbar::watch_ec(SPDesktop *desktop, Inkscape::UI::Tools::ToolBase *tool)
{
    if (dynamic_cast<MeshTool*>(tool)) {
        // connect to selection modified and changed signals
        Inkscape::Selection *selection = desktop->getSelection();
        SPDocument *document = desktop->getDocument();

        c_selection_changed = selection->connectChanged(sigc::mem_fun(*this, &MeshToolbar::selection_changed));
        c_selection_modified = selection->connectModified(sigc::mem_fun(*this, &MeshToolbar::selection_modified));
        c_subselection_changed = desktop->connectToolSubselectionChanged(sigc::mem_fun(*this, &MeshToolbar::drag_selection_changed));

        c_defs_release = document->getDefs()->connectRelease(sigc::mem_fun(*this, &MeshToolbar::defs_release));
        c_defs_modified = document->getDefs()->connectModified(sigc::mem_fun(*this, &MeshToolbar::defs_modified));
        selection_changed(selection);
    } else {
        if (c_selection_changed)
            c_selection_changed.disconnect();
        if (c_selection_modified)
            c_selection_modified.disconnect();
        if (c_subselection_changed)
            c_subselection_changed.disconnect();
        if (c_defs_release)
            c_defs_release.disconnect();
        if (c_defs_modified)
            c_defs_modified.disconnect();
    }
}

void MeshToolbar::selection_modified(Inkscape::Selection *selection, guint /*flags*/)
{
    selection_changed(selection);
}

void MeshToolbar::drag_selection_changed(gpointer /*dragger*/)
{
    selection_changed(nullptr);
}

void MeshToolbar::defs_release(SPObject * /*defs*/)
{
    selection_changed(nullptr);
}

void MeshToolbar::defs_modified(SPObject * /*defs*/, guint /*flags*/)
{
    selection_changed(nullptr);
}

/*
 * Core function, setup all the widgets whenever something changes on the desktop
 */
void MeshToolbar::selection_changed(Inkscape::Selection * /* selection */)
{
    // std::cout << "ms_tb_selection_changed" << std::endl;

    if (blocked)
        return;

    if (!_desktop) {
        return;
    }

    Inkscape::Selection *selection = _desktop->getSelection(); // take from desktop, not from args
    if (selection) {
        // ToolBase *ev = sp_desktop_event_context(desktop);
        // GrDrag *drag = NULL;
        // if (ev) {
        //     drag = ev->get_drag();
        //     // Hide/show handles?
        // }

        SPMeshGradient *ms_selected = nullptr;
        SPMeshType ms_type = SP_MESH_TYPE_COONS;
        bool ms_selected_multi = false;
        bool ms_type_multi = false;
        ms_read_selection( selection, ms_selected, ms_selected_multi, ms_type, ms_type_multi );
        // std::cout << "   type: " << ms_type << std::endl;

        if (_select_type_item) {
            _select_type_item->set_sensitive(!ms_type_multi);
            blocked = TRUE;
            _select_type_item->set_active(ms_type);
            blocked = FALSE;
        }
    }
}

void MeshToolbar::warning_popup()
{
    char *msg = _("Mesh gradients are part of SVG 2:\n"
                  "* Syntax may change.\n"
                  "* Web browser implementation is not guaranteed.\n"
                  "\n"
                  "For web: convert to bitmap (Edit->Make bitmap copy).\n"
                  "For print: export to PDF.");
    auto dialog = std::make_unique<Gtk::MessageDialog>(msg, false, Gtk::MessageType::WARNING, Gtk::ButtonsType::OK, true);
    dialog_show_modal_and_selfdestruct(std::move(dialog), get_root());
}

/**
 * Sets mesh type: Coons, Bicubic
 */
void MeshToolbar::type_changed(int mode)
{
    if (blocked) {
        return;
    }

    Inkscape::Selection *selection = _desktop->getSelection();
    std::vector<SPMeshGradient *> meshes = ms_get_dt_selected_gradients(selection);

    SPMeshType type = (SPMeshType) mode;
    for (auto & meshe : meshes) {
        meshe->type = type;
        meshe->type_set = true;
        meshe->updateRepr();
    }
    if (!meshes.empty() ) {
        DocumentUndo::done(_desktop->getDocument(), _("Set mesh type"), INKSCAPE_ICON("mesh-gradient"));
    }
}

void MeshToolbar::toggle_sides()
{
    if (MeshTool *mt = get_mesh_tool()) {
        mt->corner_operation(MG_CORNER_SIDE_TOGGLE);
    }
}

void MeshToolbar::make_elliptical()
{
    if (MeshTool *mt = get_mesh_tool()) {
        mt->corner_operation(MG_CORNER_SIDE_ARC);
    }
}

void MeshToolbar::pick_colors()
{
    if (MeshTool *mt = get_mesh_tool()) {
        mt->corner_operation(MG_CORNER_COLOR_PICK);
    }
}

void MeshToolbar::fit_mesh()
{
    if (MeshTool *mt = get_mesh_tool()) {
        mt->fit_mesh_in_bbox();
    }
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
