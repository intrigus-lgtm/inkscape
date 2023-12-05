// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * LPE aux toolbar
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

#include "lpe-toolbar.h"

#include <gtkmm/togglebutton.h>

#include "live_effects/lpe-line_segment.h"
#include "selection.h"
#include "ui/builder-utils.h"
#include "ui/dialog/dialog-container.h"
#include "ui/tools/lpe-tool.h"
#include "ui/util.h"
#include "ui/widget/combo-tool-item.h"
#include "ui/widget/toolbar-menu-button.h"
#include "ui/widget/unit-tracker.h"

using Inkscape::UI::Widget::UnitTracker;
using Inkscape::Util::Unit;
using Inkscape::Util::Quantity;
using Inkscape::DocumentUndo;
using Inkscape::UI::Tools::ToolBase;
using Inkscape::UI::Tools::LpeTool;

namespace Inkscape::UI::Toolbar {

LPEToolbar::LPEToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    , _builder(create_builder("toolbar-lpe.ui"))
    , _show_bbox_btn(get_widget<Gtk::ToggleButton>(_builder, "_show_bbox_btn"))
    , _bbox_from_selection_btn(get_widget<Gtk::ToggleButton>(_builder, "_bbox_from_selection_btn"))
    , _measuring_btn(get_widget<Gtk::ToggleButton>(_builder, "_measuring_btn"))
    , _open_lpe_dialog_btn(get_widget<Gtk::ToggleButton>(_builder, "_open_lpe_dialog_btn"))
    , _tracker(new UnitTracker(Util::UNIT_TYPE_LINEAR))
    , _freeze(false)
    , _currentlpe(nullptr)
    , _currentlpeitem(nullptr)
{
    _tracker->setActiveUnit(_desktop->getNamedView()->display_units);

    auto unit = _tracker->getActiveUnit();
    g_return_if_fail(unit != nullptr);

    auto prefs = Inkscape::Preferences::get();
    prefs->setString("/tools/lpetool/unit", unit->abbr);

    _toolbar = &get_widget<Gtk::Box>(_builder, "lpe-toolbar");

    // Combo box to choose line segment type
    {
        UI::Widget::ComboToolItemColumns columns;
        Glib::RefPtr<Gtk::ListStore> store = Gtk::ListStore::create(columns);

        std::vector<gchar*> line_segment_dropdown_items_list = {
            _("Closed"),
            _("Open start"),
            _("Open end"),
            _("Open both")
        };

        for (auto item: line_segment_dropdown_items_list) {
            Gtk::TreeModel::Row row = *(store->append());
            row[columns.col_label    ] = item;
            row[columns.col_sensitive] = true;
        }

        _line_segment_combo = Gtk::manage(UI::Widget::ComboToolItem::create(_("Line Type"), _("Choose a line segment type"), "Not Used", store));
        _line_segment_combo->use_group_label(false);

        _line_segment_combo->set_active(0);

        _line_segment_combo->signal_changed().connect(sigc::mem_fun(*this, &LPEToolbar::change_line_segment_type));
        get_widget<Gtk::Box>(_builder, "line_segment_box").append(*_line_segment_combo);
    }

    // Configure mode buttons
    int btn_index = 0;
    for_each_child(get_widget<Gtk::Box>(_builder, "mode_buttons_box"), [&](Gtk::Widget &item){
        auto &btn = dynamic_cast<Gtk::ToggleButton &>(item);
        _mode_buttons.push_back(&btn);
        btn.signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &LPEToolbar::mode_changed), btn_index++));
        return ForEachResult::_continue;
    });

    int mode = prefs->getInt("/tools/lpetool/mode", 0);
    _mode_buttons[mode]->set_active();

    // Add the units menu
    {
        _units_item = _tracker->create_tool_item(_("Units"), (""));
        _units_item->signal_changed_after().connect(sigc::mem_fun(*this, &LPEToolbar::unit_changed));
        _units_item->set_sensitive( prefs->getBool("/tools/lpetool/show_measuring_info", true));
        get_widget<Gtk::Box>(_builder, "units_box").append(*_units_item);
    }

    // Set initial states
    _show_bbox_btn.set_active(prefs->getBool("/tools/lpetool/show_bbox", true));
    _bbox_from_selection_btn.set_active(false);
    _measuring_btn.set_active(prefs->getBool("/tools/lpetool/show_measuring_info", true));
    _open_lpe_dialog_btn.set_active(false);

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

    // Signals.
    _show_bbox_btn.signal_toggled().connect(sigc::mem_fun(*this, &LPEToolbar::toggle_show_bbox));
    _bbox_from_selection_btn.signal_toggled().connect(sigc::mem_fun(*this, &LPEToolbar::toggle_set_bbox));
    _measuring_btn.signal_toggled().connect(sigc::mem_fun(*this, &LPEToolbar::toggle_show_measuring_info));
    _open_lpe_dialog_btn.signal_toggled().connect(sigc::mem_fun(*this, &LPEToolbar::open_lpe_dialog));

    desktop->connectEventContextChanged(sigc::mem_fun(*this, &LPEToolbar::watch_ec));

    set_child(*_toolbar);
}

LPEToolbar::~LPEToolbar() = default;

void LPEToolbar::set_mode(int mode)
{
    _mode_buttons[mode]->set_active();
}

// this is called when the mode is changed via the toolbar (i.e., one of the subtool buttons is pressed)
void LPEToolbar::mode_changed(int mode)
{
    using namespace Inkscape::LivePathEffect;

    auto const tool = _desktop->getTool();
    if (!SP_LPETOOL_CONTEXT(tool)) {
        return;
    }

    // only take action if run by the attr_changed listener
    if (!_freeze) {
        // in turn, prevent listener from responding
        _freeze = true;

        EffectType type = lpesubtools[mode].type;

        auto const lc = SP_LPETOOL_CONTEXT(_desktop->getTool());
        bool success = UI::Tools::lpetool_try_construction(lc->getDesktop(), type);
        if (success) {
            // since the construction was already performed, we set the state back to inactive
            _mode_buttons[0]->set_active();
            mode = 0;
        } else {
            // switch to the chosen subtool
            SP_LPETOOL_CONTEXT(_desktop->getTool())->mode = type;
        }

        if (DocumentUndo::getUndoSensitive(_desktop->getDocument())) {
            Inkscape::Preferences *prefs = Inkscape::Preferences::get();
            prefs->setInt( "/tools/lpetool/mode", mode );
        }

        _freeze = false;
    }
}

void LPEToolbar::toggle_show_bbox()
{
    auto prefs = Inkscape::Preferences::get();

    bool show = _show_bbox_btn.get_active();
    prefs->setBool("/tools/lpetool/show_bbox",  show);

    if (auto const lc = dynamic_cast<LpeTool *>(_desktop->getTool())) {
        lc->reset_limiting_bbox();
    }
}

void LPEToolbar::toggle_set_bbox()
{
    auto selection = _desktop->getSelection();

    auto bbox = selection->visualBounds();

    if (bbox) {
        Geom::Point A(bbox->min());
        Geom::Point B(bbox->max());

        A *= _desktop->doc2dt();
        B *= _desktop->doc2dt();

        // TODO: should we provide a way to store points in prefs?
        auto prefs = Inkscape::Preferences::get();
        prefs->setDouble("/tools/lpetool/bbox_upperleftx", A[Geom::X]);
        prefs->setDouble("/tools/lpetool/bbox_upperlefty", A[Geom::Y]);
        prefs->setDouble("/tools/lpetool/bbox_lowerrightx", B[Geom::X]);
        prefs->setDouble("/tools/lpetool/bbox_lowerrighty", B[Geom::Y]);

        SP_LPETOOL_CONTEXT(_desktop->getTool())->reset_limiting_bbox();
    }

    _bbox_from_selection_btn.set_active(false);
}

void LPEToolbar::change_line_segment_type(int mode)
{
    using namespace Inkscape::LivePathEffect;

    // quit if run by the attr_changed listener
    if (_freeze) {
        return;
    }

    // in turn, prevent listener from responding
    _freeze = true;
    auto line_seg = dynamic_cast<LPELineSegment *>(_currentlpe);

    if (_currentlpeitem && line_seg) {
        line_seg->end_type.param_set_value(static_cast<Inkscape::LivePathEffect::EndType>(mode));
        sp_lpe_item_update_patheffect(_currentlpeitem, true, true);
    }

    _freeze = false;
}

void LPEToolbar::toggle_show_measuring_info()
{
    auto const lc = dynamic_cast<LpeTool *>(_desktop->getTool());
    if (!lc) {
        return;
    }

    bool show = _measuring_btn.get_active();

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setBool("/tools/lpetool/show_measuring_info",  show);

    lc->show_measuring_info(show);

    _units_item->set_sensitive( show );
}

void LPEToolbar::unit_changed(int /* NotUsed */)
{
    Unit const *unit = _tracker->getActiveUnit();
    g_return_if_fail(unit != nullptr);
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setString("/tools/lpetool/unit", unit->abbr);

    if (auto const lc = SP_LPETOOL_CONTEXT(_desktop->getTool())) {
        lc->delete_measuring_items();
        lc->create_measuring_items();
    }
}

void LPEToolbar::open_lpe_dialog()
{
    if (dynamic_cast<LpeTool *>(_desktop->getTool())) {
        _desktop->getContainer()->new_dialog("LivePathEffect");
    } else {
        std::cerr << "LPEToolbar::open_lpe_dialog: LPEToolbar active but current tool is not LPE tool!" << std::endl;
    }
    _open_lpe_dialog_btn.set_active(false);
}

void LPEToolbar::watch_ec(SPDesktop *desktop, Inkscape::UI::Tools::ToolBase *tool)
{
    if (SP_LPETOOL_CONTEXT(tool)) {
        // Watch selection
        c_selection_modified = desktop->getSelection()->connectModified(sigc::mem_fun(*this, &LPEToolbar::sel_modified));
        c_selection_changed = desktop->getSelection()->connectChanged(sigc::mem_fun(*this, &LPEToolbar::sel_changed));
        sel_changed(desktop->getSelection());
    } else {
        if (c_selection_modified)
            c_selection_modified.disconnect();
        if (c_selection_changed)
            c_selection_changed.disconnect();
    }
}

void LPEToolbar::sel_modified(Inkscape::Selection *selection, guint /*flags*/)
{
    auto const tool = selection->desktop()->getTool();
    if (auto const lc = SP_LPETOOL_CONTEXT(tool)) {
        lc->update_measuring_items();
    }
}

void LPEToolbar::sel_changed(Inkscape::Selection *selection)
{
    using namespace Inkscape::LivePathEffect;
    auto const tool = selection->desktop()->getTool();
    auto const lc = SP_LPETOOL_CONTEXT(tool);
    if (!lc) {
        return;
    }

    lc->delete_measuring_items();
    lc->create_measuring_items(selection);

    // activate line segment combo box if a single item with LPELineSegment is selected
    SPItem *item = selection->singleItem();
    if (item && is<SPLPEItem>(item) && UI::Tools::lpetool_item_has_construction(item)) {

        auto lpeitem = cast<SPLPEItem>(item);
        Effect* lpe = lpeitem->getCurrentLPE();
        if (lpe && lpe->effectType() == LINE_SEGMENT) {
            LPELineSegment *lpels = static_cast<LPELineSegment*>(lpe);
            _currentlpe = lpe;
            _currentlpeitem = lpeitem;
            _line_segment_combo->set_sensitive(true);
            _line_segment_combo->set_active( lpels->end_type.get_value() );
        } else {
            _currentlpe = nullptr;
            _currentlpeitem = nullptr;
            _line_segment_combo->set_sensitive(false);
        }

    } else {
        _currentlpe = nullptr;
        _currentlpeitem = nullptr;
        _line_segment_combo->set_sensitive(false);
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8 :
