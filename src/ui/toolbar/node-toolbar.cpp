// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Node aux toolbar
 */
/* Authors:
 *   MenTaLguY <mental@rydia.net>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Frank Felfe <innerspace@iname.com>
 *   John Cliff <simarilius@yahoo.com>
 *   David Turner <novalis@gnu.org>
 *   Josh Andler <scislac@scislac.com>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *   Tavmjong Bah <tavmjong@free.fr>
 *   Abhishek Sharma
 *   Kris De Gussem <Kris.DeGussem@gmail.com>
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Copyright (C) 2004 David Turner
 * Copyright (C) 2003 MenTaLguY
 * Copyright (C) 1999-2011 authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "node-toolbar.h"

#include <glibmm/i18n.h>
#include <giomm/simpleactiongroup.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/image.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/togglebutton.h>
#include <sigc++/functors/mem_fun.h>

#include "desktop.h"
#include "document-undo.h"
#include "inkscape.h"
#include "object/sp-namedview.h"
#include "page-manager.h"
#include "selection.h"
#include "ui/builder-utils.h"
#include "ui/simple-pref-pusher.h"
#include "ui/tool/control-point-selection.h"
#include "ui/tool/multi-path-manipulator.h"
#include "ui/tools/node-tool.h"
#include "ui/util.h"
#include "ui/widget/canvas.h"
#include "ui/widget/combo-tool-item.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/toolbar-menu-button.h"
#include "ui/widget/unit-tracker.h"
#include "widgets/widget-sizes.h"

using Inkscape::UI::Widget::UnitTracker;
using Inkscape::Util::Unit;
using Inkscape::Util::Quantity;
using Inkscape::DocumentUndo;
using Inkscape::UI::Tools::NodeTool;

/** Temporary hack: Returns the node tool in the active desktop.
 * Will go away during tool refactoring. */
static NodeTool *get_node_tool()
{
    if (SP_ACTIVE_DESKTOP) {
        return dynamic_cast<NodeTool *>(SP_ACTIVE_DESKTOP->getTool());
    }
    return nullptr;
}

namespace Inkscape::UI::Toolbar {

NodeToolbar::NodeToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    , _tracker{std::make_unique<UnitTracker>(Inkscape::Util::UNIT_TYPE_LINEAR)}
    , _freeze(false)
    , _builder(create_builder("toolbar-node.ui"))
    , _nodes_lpeedit_btn(get_widget<Gtk::Button>(_builder, "_nodes_lpeedit_btn"))
    , _show_helper_path_btn(&get_widget<Gtk::ToggleButton>(_builder, "_show_helper_path_btn"))
    , _show_handles_btn(&get_widget<Gtk::ToggleButton>(_builder, "_show_handles_btn"))
    , _show_transform_handles_btn(&get_widget<Gtk::ToggleButton>(_builder, "_show_transform_handles_btn"))
    , _object_edit_mask_path_btn(&get_widget<Gtk::ToggleButton>(_builder, "_object_edit_mask_path_btn"))
    , _object_edit_clip_path_btn(&get_widget<Gtk::ToggleButton>(_builder, "_object_edit_clip_path_btn"))
    , _nodes_x_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_nodes_x_item"))
    , _nodes_y_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_nodes_y_item"))
{
    Unit doc_units = *desktop->getNamedView()->display_units;
    _tracker->setActiveUnit(&doc_units);

    _toolbar = &get_widget<Gtk::Box>(_builder, "node-toolbar");

    // Setup the derived spin buttons.
    setup_derived_spin_button(_nodes_x_item, "Xcoord");
    setup_derived_spin_button(_nodes_y_item, "Ycoord");

    auto unit_menu = _tracker->create_tool_item(_("Units"), (""));
    get_widget<Gtk::Box>(_builder, "unit_menu_box").append(*unit_menu);

    // Fetch all the ToolbarMenuButtons at once from the UI file
    // Menu Button #1
    auto popover_box1 = &get_widget<Gtk::Box>(_builder, "popover_box1");
    auto menu_btn1 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn1");

    // Menu Button #2
    auto popover_box2 = &get_widget<Gtk::Box>(_builder, "popover_box2");
    auto menu_btn2 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn2");

    // Initialize all the ToolbarMenuButtons only after all the children of the
    // toolbar have been fetched. Otherwise, the children to be moved in the
    // popover will get mapped to a different position and it will probably
    // cause segfault.
    auto children = UI::get_children(*_toolbar);

    menu_btn1->init(1, "tag1", popover_box1, children);
    addCollapsibleButton(menu_btn1);
    menu_btn2->init(2, "tag2", popover_box2, children);
    addCollapsibleButton(menu_btn2);

    set_child(*_toolbar);

    // Attach the signals.

    get_widget<Gtk::Button>(_builder, "insert_node_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &NodeToolbar::edit_add));

    setup_insert_node_menu();

    get_widget<Gtk::Button>(_builder, "delete_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &NodeToolbar::edit_delete));

    get_widget<Gtk::Button>(_builder, "join_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &NodeToolbar::edit_join));
    get_widget<Gtk::Button>(_builder, "break_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &NodeToolbar::edit_break));

    get_widget<Gtk::Button>(_builder, "join_segment_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &NodeToolbar::edit_join_segment));
    get_widget<Gtk::Button>(_builder, "delete_segment_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &NodeToolbar::edit_delete_segment));

    get_widget<Gtk::Button>(_builder, "cusp_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &NodeToolbar::edit_cusp));
    get_widget<Gtk::Button>(_builder, "smooth_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &NodeToolbar::edit_smooth));
    get_widget<Gtk::Button>(_builder, "symmetric_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &NodeToolbar::edit_symmetrical));
    get_widget<Gtk::Button>(_builder, "auto_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &NodeToolbar::edit_auto));

    get_widget<Gtk::Button>(_builder, "line_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &NodeToolbar::edit_toline));
    get_widget<Gtk::Button>(_builder, "curve_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &NodeToolbar::edit_tocurve));

    _pusher_show_outline.reset(new UI::SimplePrefPusher(_show_helper_path_btn, "/tools/nodes/show_outline"));
    _show_helper_path_btn->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &NodeToolbar::on_pref_toggled),
                                                               _show_helper_path_btn, "/tools/nodes/show_outline"));

    _pusher_show_handles.reset(new UI::SimplePrefPusher(_show_handles_btn, "/tools/nodes/show_handles"));
    _show_handles_btn->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &NodeToolbar::on_pref_toggled),
                                                           _show_handles_btn, "/tools/nodes/show_handles"));

    _pusher_show_transform_handles.reset(
        new UI::SimplePrefPusher(_show_transform_handles_btn, "/tools/nodes/show_transform_handles"));
    _show_transform_handles_btn->signal_toggled().connect(
        sigc::bind(sigc::mem_fun(*this, &NodeToolbar::on_pref_toggled), _show_transform_handles_btn,
                   "/tools/nodes/show_transform_handles"));

    _pusher_edit_masks.reset(new SimplePrefPusher(_object_edit_mask_path_btn, "/tools/nodes/edit_masks"));
    _object_edit_mask_path_btn->signal_toggled().connect(sigc::bind(
        sigc::mem_fun(*this, &NodeToolbar::on_pref_toggled), _object_edit_mask_path_btn, "/tools/nodes/edit_masks"));

    _pusher_edit_clipping_paths.reset(
        new SimplePrefPusher(_object_edit_clip_path_btn, "/tools/nodes/edit_clipping_paths"));
    _object_edit_clip_path_btn->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &NodeToolbar::on_pref_toggled),
                                                                    _object_edit_clip_path_btn,
                                                                    "/tools/nodes/edit_clipping_paths"));

    sel_changed(desktop->getSelection());
    desktop->connectEventContextChanged(sigc::mem_fun(*this, &NodeToolbar::watch_ec));
}

NodeToolbar::~NodeToolbar() = default;

void NodeToolbar::setup_derived_spin_button(Inkscape::UI::Widget::SpinButton &btn, Glib::ustring const &name)
{
    const Glib::ustring path = "/tools/nodes/" + name;
    auto const val = Preferences::get()->getDouble(path, 0);
    auto adj = btn.get_adjustment();
    adj->set_value(val);
    adj->signal_value_changed().connect(sigc::bind(sigc::mem_fun(*this, &NodeToolbar::value_changed), Geom::X));

    _tracker->addAdjustment(adj->gobj());
    btn.addUnitTracker(_tracker.get());
    btn.set_defocus_widget(_desktop->getCanvas());

    // TODO: Handle this in the toolbar class.
    btn.set_sensitive(false);
}

void NodeToolbar::setup_insert_node_menu()
{
    // insert_node_menu
    auto const actions = Gio::SimpleActionGroup::create();
    actions->add_action("insert-min-x", sigc::mem_fun(*this, &NodeToolbar::edit_add_min_x));
    actions->add_action("insert-max-x", sigc::mem_fun(*this, &NodeToolbar::edit_add_max_x));
    actions->add_action("insert-min-y", sigc::mem_fun(*this, &NodeToolbar::edit_add_min_y));
    actions->add_action("insert-max-y", sigc::mem_fun(*this, &NodeToolbar::edit_add_max_y));
    insert_action_group("node-toolbar", actions);
}

void NodeToolbar::value_changed(Geom::Dim2 d)
{
    auto adj = (d == Geom::X) ? _nodes_x_item.get_adjustment() : _nodes_y_item.get_adjustment();

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    if (!_tracker) {
        return;
    }

    Unit const *unit = _tracker->getActiveUnit();

    if (DocumentUndo::getUndoSensitive(_desktop->getDocument())) {
        prefs->setDouble(Glib::ustring("/tools/nodes/") + (d == Geom::X ? "x" : "y"),
            Quantity::convert(adj->get_value(), unit, "px"));
    }

    // quit if run by the attr_changed listener
    if (_freeze || _tracker->isUpdating()) {
        return;
    }

    // in turn, prevent listener from responding
    _freeze = true;

    NodeTool *nt = get_node_tool();
    if (nt && !nt->_selected_nodes->empty()) {
        double val = Quantity::convert(adj->get_value(), unit, "px");
        double oldval = nt->_selected_nodes->pointwiseBounds()->midpoint()[d];

        // Adjust the coordinate to the current page, if needed
        auto &pm = _desktop->getDocument()->getPageManager();
        if (prefs->getBool("/options/origincorrection/page", true)) {
            auto page = pm.getSelectedPageRect();
            oldval -= page.corner(0)[d];
        }

        Geom::Point delta(0,0);
        delta[d] = val - oldval;
        nt->_multipath->move(delta);
    }

    _freeze = false;
}

void NodeToolbar::sel_changed(Inkscape::Selection *selection)
{
    SPItem *item = selection->singleItem();
    if (item && is<SPLPEItem>(item)) {
        if (cast_unsafe<SPLPEItem>(item)->hasPathEffect()) {
            _nodes_lpeedit_btn.set_sensitive(true);
        } else {
            _nodes_lpeedit_btn.set_sensitive(false);
        }
    } else {
        _nodes_lpeedit_btn.set_sensitive(false);
    }
}

void NodeToolbar::watch_ec(SPDesktop *desktop, Inkscape::UI::Tools::ToolBase *tool)
{
    if (INK_IS_NODE_TOOL(tool)) {
        // watch selection
        c_selection_changed = desktop->getSelection()->connectChanged(sigc::mem_fun(*this, &NodeToolbar::sel_changed));
        c_selection_modified = desktop->getSelection()->connectModified(sigc::mem_fun(*this, &NodeToolbar::sel_modified));
        c_subselection_changed = desktop->connect_control_point_selected([this](void* sender, Inkscape::UI::ControlPointSelection* selection) {
            coord_changed(selection);
        });

        sel_changed(desktop->getSelection());
    } else {
        if (c_selection_changed)
            c_selection_changed.disconnect();
        if (c_selection_modified)
            c_selection_modified.disconnect();
        if (c_subselection_changed)
            c_subselection_changed.disconnect();
    }
}

void NodeToolbar::sel_modified(Inkscape::Selection *selection, guint /*flags*/)
{
    sel_changed(selection);
}

/* is called when the node selection is modified */
void NodeToolbar::coord_changed(Inkscape::UI::ControlPointSelection *selected_nodes) // gpointer /*shape_editor*/)
{
    // quit if run by the attr_changed listener
    if (_freeze) {
        return;
    }

    // in turn, prevent listener from responding
    _freeze = true;

    if (!_tracker) {
        return;
    }
    Unit const *unit = _tracker->getActiveUnit();
    g_return_if_fail(unit != nullptr);

    if (!selected_nodes || selected_nodes->empty()) {
        // no path selected
        _nodes_x_item.set_sensitive(false);
        _nodes_y_item.set_sensitive(false);
    } else {
        _nodes_x_item.set_sensitive(true);
        _nodes_y_item.set_sensitive(true);
        auto adj_x = _nodes_x_item.get_adjustment();
        auto adj_y = _nodes_y_item.get_adjustment();
        Geom::Coord oldx = Quantity::convert(adj_x->get_value(), unit, "px");
        Geom::Coord oldy = Quantity::convert(adj_y->get_value(), unit, "px");
        Geom::Point mid = selected_nodes->pointwiseBounds()->midpoint();

        // Adjust shown coordinate according to the selected page
        if (Preferences::get()->getBool("/options/origincorrection/page", true)) {
            auto &pm = _desktop->getDocument()->getPageManager();
            mid *= pm.getSelectedPageAffine().inverse();
        }

        if (oldx != mid[Geom::X]) {
            adj_x->set_value(Quantity::convert(mid[Geom::X], "px", unit));
        }
        if (oldy != mid[Geom::Y]) {
            adj_y->set_value(Quantity::convert(mid[Geom::Y], "px", unit));
        }
    }

    _freeze = false;
}

void NodeToolbar::edit_add()
{
    NodeTool *nt = get_node_tool();
    if (nt) {
        nt->_multipath->insertNodes();
    }
}

void NodeToolbar::edit_add_min_x()
{
    NodeTool *nt = get_node_tool();
    if (nt) {
        nt->_multipath->insertNodesAtExtrema(Inkscape::UI::PointManipulator::EXTR_MIN_X);
    }
}

void NodeToolbar::edit_add_max_x()
{
    NodeTool *nt = get_node_tool();
    if (nt) {
        nt->_multipath->insertNodesAtExtrema(Inkscape::UI::PointManipulator::EXTR_MAX_X);
    }
}

void NodeToolbar::edit_add_min_y()
{
    NodeTool *nt = get_node_tool();
    if (nt) {
        nt->_multipath->insertNodesAtExtrema(Inkscape::UI::PointManipulator::EXTR_MIN_Y);
    }
}

void NodeToolbar::edit_add_max_y()
{
    NodeTool *nt = get_node_tool();
    if (nt) {
        nt->_multipath->insertNodesAtExtrema(Inkscape::UI::PointManipulator::EXTR_MAX_Y);
    }
}

void NodeToolbar::edit_delete()
{
    NodeTool *nt = get_node_tool();
    if (nt) {
        nt->_multipath->deleteNodes(Preferences::get()->getBool("/tools/nodes/delete_preserves_shape", true));
    }
}

void NodeToolbar::edit_join()
{
    NodeTool *nt = get_node_tool();
    if (nt) {
        nt->_multipath->joinNodes();
    }
}

void NodeToolbar::edit_break()
{
    NodeTool *nt = get_node_tool();
    if (nt) {
        nt->_multipath->breakNodes();
    }
}

void NodeToolbar::edit_delete_segment()
{
    NodeTool *nt = get_node_tool();
    if (nt) {
        nt->_multipath->deleteSegments();
    }
}

void NodeToolbar::edit_join_segment()
{
    NodeTool *nt = get_node_tool();
    if (nt) {
        nt->_multipath->joinSegments();
    }
}

void NodeToolbar::edit_cusp()
{
    NodeTool *nt = get_node_tool();
    if (nt) {
        nt->_multipath->setNodeType(Inkscape::UI::NODE_CUSP);
    }
}

void NodeToolbar::edit_smooth()
{
    NodeTool *nt = get_node_tool();
    if (nt) {
        nt->_multipath->setNodeType(Inkscape::UI::NODE_SMOOTH);
    }
}

void NodeToolbar::edit_symmetrical()
{
    NodeTool *nt = get_node_tool();
    if (nt) {
        nt->_multipath->setNodeType(Inkscape::UI::NODE_SYMMETRIC);
    }
}

void NodeToolbar::edit_auto()
{
    NodeTool *nt = get_node_tool();
    if (nt) {
        nt->_multipath->setNodeType(Inkscape::UI::NODE_AUTO);
    }
}

void NodeToolbar::edit_toline()
{
    NodeTool *nt = get_node_tool();
    if (nt) {
        nt->_multipath->setSegmentType(Inkscape::UI::SEGMENT_STRAIGHT);
    }
}

void NodeToolbar::edit_tocurve()
{
    NodeTool *nt = get_node_tool();
    if (nt) {
        nt->_multipath->setSegmentType(Inkscape::UI::SEGMENT_CUBIC_BEZIER);
    }
}

void NodeToolbar::on_pref_toggled(Gtk::ToggleButton *item, const Glib::ustring &path)
{
    Preferences::get()->setBool(path, item->get_active());
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
