// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Measure aux toolbar
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

#include "measure-toolbar.h"

#include <glibmm/i18n.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/togglebutton.h>

#include "desktop.h"
#include "document-undo.h"
#include "message-stack.h"
#include "object/sp-namedview.h"
#include "ui/builder-utils.h"
#include "ui/tools/measure-tool.h"
#include "ui/util.h"
#include "ui/widget/canvas.h"
#include "ui/widget/combo-tool-item.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/toolbar-menu-button.h"
#include "ui/widget/unit-tracker.h"

using Inkscape::UI::Widget::UnitTracker;
using Inkscape::Util::Unit;
using Inkscape::DocumentUndo;
using Inkscape::UI::Tools::MeasureTool;

static MeasureTool *get_measure_tool(SPDesktop *desktop)
{
    if (desktop) {
        return dynamic_cast<MeasureTool *>(desktop->getTool());
    }
    return nullptr;
}

namespace Inkscape::UI::Toolbar {

MeasureToolbar::MeasureToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    , _tracker(new UnitTracker(Inkscape::Util::UNIT_TYPE_LINEAR))
    , _builder(create_builder("toolbar-measure.ui"))
    , _font_size_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_font_size_item"))
    , _precision_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_precision_item"))
    , _scale_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_scale_item"))
    , _only_selected_btn(get_widget<Gtk::ToggleButton>(_builder, "_only_selected_btn"))
    , _ignore_1st_and_last_btn(get_widget<Gtk::ToggleButton>(_builder, "_ignore_1st_and_last_btn"))
    , _inbetween_btn(get_widget<Gtk::ToggleButton>(_builder, "_inbetween_btn"))
    , _show_hidden_btn(get_widget<Gtk::ToggleButton>(_builder, "_show_hidden_btn"))
    , _all_layers_btn(get_widget<Gtk::ToggleButton>(_builder, "_all_layers_btn"))
    , _offset_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_offset_item"))
{
    auto prefs = Inkscape::Preferences::get();
    auto unit = desktop->getNamedView()->getDisplayUnit();
    _tracker->setActiveUnitByAbbr(prefs->getString("/tools/measure/unit", unit->abbr).c_str());

    _toolbar = &get_widget<Gtk::Box>(_builder, "measure-toolbar");

    auto unit_menu = _tracker->create_tool_item(_("Units"), (""));
    unit_menu->signal_changed().connect(sigc::mem_fun(*this, &MeasureToolbar::unit_changed));
    get_widget<Gtk::Box>(_builder, "unit_menu_box").append(*unit_menu);

    setup_derived_spin_button(_font_size_item, "fontsize", 10.0, &MeasureToolbar::fontsize_value_changed);
    setup_derived_spin_button(_precision_item, "precision", 2, &MeasureToolbar::precision_value_changed);
    setup_derived_spin_button(_scale_item, "scale", 100.0, &MeasureToolbar::scale_value_changed);
    setup_derived_spin_button(_offset_item, "offset", 5.0, &MeasureToolbar::offset_value_changed);

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
    menu_btn2->init(2, "tag2", popover_box2, children);
    addCollapsibleButton(menu_btn1);
    addCollapsibleButton(menu_btn2);

    // Signals.
    _only_selected_btn.set_active(prefs->getBool("/tools/measure/only_selected", false));
    _only_selected_btn.signal_toggled().connect(sigc::mem_fun(*this, &MeasureToolbar::toggle_only_selected));

    _ignore_1st_and_last_btn.set_active(prefs->getBool("/tools/measure/ignore_1st_and_last", true));
    _ignore_1st_and_last_btn.signal_toggled().connect(
        sigc::mem_fun(*this, &MeasureToolbar::toggle_ignore_1st_and_last));

    _inbetween_btn.set_active(prefs->getBool("/tools/measure/show_in_between", true));
    _inbetween_btn.signal_toggled().connect(sigc::mem_fun(*this, &MeasureToolbar::toggle_show_in_between));

    _show_hidden_btn.set_active(prefs->getBool("/tools/measure/show_hidden", true));
    _show_hidden_btn.signal_toggled().connect(sigc::mem_fun(*this, &MeasureToolbar::toggle_show_hidden));

    _all_layers_btn.set_active(prefs->getBool("/tools/measure/all_layers", true));
    _all_layers_btn.signal_toggled().connect(sigc::mem_fun(*this, &MeasureToolbar::toggle_all_layers));

    get_widget<Gtk::Button>(_builder, "reverse_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &MeasureToolbar::reverse_knots));

    get_widget<Gtk::Button>(_builder, "to_phantom_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &MeasureToolbar::to_phantom));

    get_widget<Gtk::Button>(_builder, "to_guides_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &MeasureToolbar::to_guides));

    get_widget<Gtk::Button>(_builder, "to_item_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &MeasureToolbar::to_item));

    get_widget<Gtk::Button>(_builder, "mark_dimension_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &MeasureToolbar::to_mark_dimension));

    set_child(*_toolbar);
}

MeasureToolbar::~MeasureToolbar() = default;

void MeasureToolbar::setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name,
                                               double default_value, ValueChangedMemFun const value_changed_mem_fun)
{
    auto adj = btn.get_adjustment();
    const Glib::ustring path = "/tools/measure/" + name;
    auto const val = Preferences::get()->getDouble(path, default_value);
    adj->set_value(val);
    adj->signal_value_changed().connect(sigc::mem_fun(*this, value_changed_mem_fun));

    btn.set_defocus_widget(_desktop->getCanvas());
}

void MeasureToolbar::fontsize_value_changed()
{
    if (DocumentUndo::getUndoSensitive(_desktop->getDocument())) {
        Preferences::get()->setDouble(Glib::ustring("/tools/measure/fontsize"),
                                      _font_size_item.get_adjustment()->get_value());
        MeasureTool *mt = get_measure_tool(_desktop);
        if (mt) {
            mt->showCanvasItems();
        }
    }
}

void MeasureToolbar::unit_changed(int /* notUsed */)
{
    Glib::ustring const unit = _tracker->getActiveUnit()->abbr;
    Preferences::get()->setString("/tools/measure/unit", unit);
    MeasureTool *mt = get_measure_tool(_desktop);
    if (mt) {
        mt->showCanvasItems();
    }
}

void MeasureToolbar::precision_value_changed()
{
    if (DocumentUndo::getUndoSensitive(_desktop->getDocument())) {
        Preferences::get()->setInt(Glib::ustring("/tools/measure/precision"),
                                   _precision_item.get_adjustment()->get_value());
        MeasureTool *mt = get_measure_tool(_desktop);
        if (mt) {
            mt->showCanvasItems();
        }
    }
}

void MeasureToolbar::scale_value_changed()
{
    if (DocumentUndo::getUndoSensitive(_desktop->getDocument())) {
        Preferences::get()->setDouble(Glib::ustring("/tools/measure/scale"), _scale_item.get_adjustment()->get_value());
        MeasureTool *mt = get_measure_tool(_desktop);
        if (mt) {
            mt->showCanvasItems();
        }
    }
}

void MeasureToolbar::offset_value_changed()
{
    if (DocumentUndo::getUndoSensitive(_desktop->getDocument())) {
        Preferences::get()->setDouble(Glib::ustring("/tools/measure/offset"),
                                      _offset_item.get_adjustment()->get_value());
        MeasureTool *mt = get_measure_tool(_desktop);
        if (mt) {
            mt->showCanvasItems();
        }
    }
}

void MeasureToolbar::toggle_only_selected()
{
    bool active = _only_selected_btn.get_active();
    Preferences::get()->setBool("/tools/measure/only_selected", active);
    if ( active ) {
        _desktop->messageStack()->flash(Inkscape::INFORMATION_MESSAGE, _("Measures only selected."));
    } else {
        _desktop->messageStack()->flash(Inkscape::INFORMATION_MESSAGE, _("Measure all."));
    }
    MeasureTool *mt = get_measure_tool(_desktop);
    if (mt) {
        mt->showCanvasItems();
    }
}

void MeasureToolbar::toggle_ignore_1st_and_last()
{
    bool active = _ignore_1st_and_last_btn.get_active();
    Preferences::get()->setBool("/tools/measure/ignore_1st_and_last", active);
    if ( active ) {
        _desktop->messageStack()->flash(Inkscape::INFORMATION_MESSAGE, _("Start and end measures inactive."));
    } else {
        _desktop->messageStack()->flash(Inkscape::INFORMATION_MESSAGE, _("Start and end measures active."));
    }
    MeasureTool *mt = get_measure_tool(_desktop);
    if (mt) {
        mt->showCanvasItems();
    }
}

void MeasureToolbar::toggle_show_in_between()
{
    bool active = _inbetween_btn.get_active();
    Preferences::get()->setBool("/tools/measure/show_in_between", active);
    if ( active ) {
        _desktop->messageStack()->flash(Inkscape::INFORMATION_MESSAGE, _("Compute all elements."));
    } else {
        _desktop->messageStack()->flash(Inkscape::INFORMATION_MESSAGE, _("Compute max length."));
    }
    MeasureTool *mt = get_measure_tool(_desktop);
    if (mt) {
        mt->showCanvasItems();
    }
}

void MeasureToolbar::toggle_show_hidden()
{
    bool active = _show_hidden_btn.get_active();
    Preferences::get()->setBool("/tools/measure/show_hidden", active);
    if ( active ) {
        _desktop->messageStack()->flash(Inkscape::INFORMATION_MESSAGE, _("Show all crossings."));
    } else {
        _desktop->messageStack()->flash(Inkscape::INFORMATION_MESSAGE, _("Show visible crossings."));
    }
    MeasureTool *mt = get_measure_tool(_desktop);
    if (mt) {
        mt->showCanvasItems();
    }
}

void MeasureToolbar::toggle_all_layers()
{
    bool active = _all_layers_btn.get_active();
    Preferences::get()->setBool("/tools/measure/all_layers", active);
    if ( active ) {
        _desktop->messageStack()->flash(Inkscape::INFORMATION_MESSAGE, _("Use all layers in the measure."));
    } else {
        _desktop->messageStack()->flash(Inkscape::INFORMATION_MESSAGE, _("Use current layer in the measure."));
    }
    MeasureTool *mt = get_measure_tool(_desktop);
    if (mt) {
        mt->showCanvasItems();
    }
}

void MeasureToolbar::reverse_knots()
{
    MeasureTool *mt = get_measure_tool(_desktop);
    if (mt) {
        mt->reverseKnots();
    }
}

void MeasureToolbar::to_phantom()
{
    MeasureTool *mt = get_measure_tool(_desktop);
    if (mt) {
        mt->toPhantom();
    }
}

void MeasureToolbar::to_guides()
{
    MeasureTool *mt = get_measure_tool(_desktop);
    if (mt) {
        mt->toGuides();
    }
}

void MeasureToolbar::to_item()
{
    MeasureTool *mt = get_measure_tool(_desktop);
    if (mt) {
        mt->toItem();
    }
}

void MeasureToolbar::to_mark_dimension()
{
    MeasureTool *mt = get_measure_tool(_desktop);
    if (mt) {
        mt->toMarkDimension();
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
