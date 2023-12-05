// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Star aux toolbar
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

#include "star-toolbar.h"

#include <glibmm/i18n.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/togglebutton.h>

#include "desktop.h"
#include "document-undo.h"
#include "object/sp-star.h"
#include "selection.h"
#include "ui/builder-utils.h"
#include "ui/icon-names.h"
#include "ui/tools/star-tool.h"
#include "ui/util.h"
#include "ui/widget/canvas.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/toolbar-menu-button.h"

using Inkscape::DocumentUndo;

namespace Inkscape::UI::Toolbar {

StarToolbar::StarToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    , _builder(create_builder("toolbar-star.ui"))
    , _mode_item(get_widget<Gtk::Label>(_builder, "_mode_item"))
    , _magnitude_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_magnitude_item"))
    , _spoke_box(get_widget<Gtk::Box>(_builder, "_spoke_box"))
    , _spoke_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_spoke_item"))
    , _roundedness_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_roundedness_item"))
    , _randomization_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_randomization_item"))
{
    _toolbar = &get_widget<Gtk::Box>(_builder, "star-toolbar");

    bool is_flat_sided = Inkscape::Preferences::get()->getBool("/tools/shapes/star/isflatsided", false);

    setup_derived_spin_button(_magnitude_item, "magnitude", is_flat_sided ? 3 : 2,
                              &StarToolbar::magnitude_value_changed);

    _magnitude_item.set_custom_numeric_menu_data({
        {2, ""},
        {3, _("triangle/tri-star")},
        {4, _("square/quad-star")},
        {5, _("pentagon/five-pointed star")},
        {6, _("hexagon/six-pointed star")},
        {7, ""},
        {8, ""},
        {10, ""},
        {12, ""},
        {20, ""}
    });

    setup_derived_spin_button(_spoke_item, "proportion", 0.5, &StarToolbar::proportion_value_changed);
    setup_derived_spin_button(_roundedness_item, "rounded", 0.0, &StarToolbar::rounded_value_changed);
    setup_derived_spin_button(_randomization_item, "randomized", 0.0, &StarToolbar::randomized_value_changed);

    /* Flatsided checkbox */
    _flat_item_buttons.push_back(&get_widget<Gtk::ToggleButton>(_builder, "flat_polygon_button"));
    _flat_item_buttons.push_back(&get_widget<Gtk::ToggleButton>(_builder, "flat_star_button"));
    _flat_item_buttons[is_flat_sided ? 0 : 1]->set_active();

    int btn_index = 0;

    for (auto btn : _flat_item_buttons) {
        btn->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &StarToolbar::side_mode_changed), btn_index++));
    }

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

    desktop->connectEventContextChanged(sigc::mem_fun(*this, &StarToolbar::watch_tool));

    set_child(*_toolbar);

    // Signals.
    get_widget<Gtk::Button>(_builder, "reset_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &StarToolbar::defaults));

    _spoke_item.set_visible(!is_flat_sided);
}

void StarToolbar::setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name,
                                            double default_value, ValueChangedMemFun const value_changed_mem_fun)
{
    const Glib::ustring path = "/tools/shapes/star/" + name;
    auto const val = Preferences::get()->getDouble(path, default_value);

    auto adj = btn.get_adjustment();
    adj->set_value(val);
    adj->signal_value_changed().connect(sigc::mem_fun(*this, value_changed_mem_fun));

    btn.set_defocus_widget(_desktop->getCanvas());
}

StarToolbar::~StarToolbar()
{
    if (_repr) { // remove old listener
        _repr->removeObserver(*this);
        Inkscape::GC::release(_repr);
        _repr = nullptr;
    }
}

void StarToolbar::side_mode_changed(int mode)
{
    bool const flat = mode == 0;

    if (DocumentUndo::getUndoSensitive(_desktop->getDocument())) {
        Preferences::get()->setBool("/tools/shapes/star/isflatsided", flat);
    }

    // quit if run by the attr_changed listener
    if (_freeze) {
        return;
    }

    // in turn, prevent listener from responding
    _freeze = true;

    auto adj = _magnitude_item.get_adjustment();
    _spoke_box.set_visible(!flat);

    for (auto item : _desktop->getSelection()->items()) {
        if (is<SPStar>(item)) {
            auto repr = item->getRepr();
            if (flat) {
                int sides = adj->get_value();
                if (sides < 3) {
                    repr->setAttributeInt("sodipodi:sides", 3);
                }
            }
            repr->setAttribute("inkscape:flatsided", flat ? "true" : "false");

            item->updateRepr();
        }
    }

    adj->set_lower(flat ? 3 : 2);
    if (flat && adj->get_value() < 3) {
        adj->set_value(3);
    }

    if (!_batchundo) {
        DocumentUndo::done(_desktop->getDocument(), flat ? _("Make polygon") : _("Make star"), INKSCAPE_ICON("draw-polygon-star"));
    }

    _freeze = false;
}

void StarToolbar::magnitude_value_changed()
{
    auto adj = _magnitude_item.get_adjustment();

    if (DocumentUndo::getUndoSensitive(_desktop->getDocument())) {
        // do not remember prefs if this call is initiated by an undo change, because undoing object
        // creation sets bogus values to its attributes before it is deleted
        Preferences::get()->setInt("/tools/shapes/star/magnitude", adj->get_value());
    }

    // quit if run by the attr_changed listener
    if (_freeze) {
        return;
    }

    // in turn, prevent listener from responding
    _freeze = true;

    for (auto item : _desktop->getSelection()->items()) {
        if (is<SPStar>(item)) {
            auto repr = item->getRepr();
            repr->setAttributeInt("sodipodi:sides", adj->get_value());
            double arg1 = repr->getAttributeDouble("sodipodi:arg1", 0.5);
            repr->setAttributeSvgDouble("sodipodi:arg2", arg1 + M_PI / adj->get_value());
            item->updateRepr();
        }
    }
    if (!_batchundo) {
        DocumentUndo::maybeDone(_desktop->getDocument(), "star:numcorners", _("Star: Change number of corners"), INKSCAPE_ICON("draw-polygon-star"));
    }

    _freeze = false;
}

void StarToolbar::proportion_value_changed()
{
    auto adj = _spoke_item.get_adjustment();

    if (DocumentUndo::getUndoSensitive(_desktop->getDocument())) {
        if (!std::isnan(adj->get_value())) {
            Preferences::get()->setDouble("/tools/shapes/star/proportion", adj->get_value());
        }
    }

    // quit if run by the attr_changed listener
    if (_freeze) {
        return;
    }

    // in turn, prevent listener from responding
    _freeze = true;

    for (auto item : _desktop->getSelection()->items()) {
        if (is<SPStar>(item)) {
            auto repr = item->getRepr();

            double r1 = repr->getAttributeDouble("sodipodi:r1", 1.0);
            double r2 = repr->getAttributeDouble("sodipodi:r2", 1.0);

            if (r2 < r1) {
                repr->setAttributeSvgDouble("sodipodi:r2", r1 * adj->get_value());
            } else {
                repr->setAttributeSvgDouble("sodipodi:r1", r2 * adj->get_value());
            }

            item->updateRepr();
        }
    }

    if (!_batchundo) {
        DocumentUndo::maybeDone(_desktop->getDocument(), "star:spokeratio", _("Star: Change spoke ratio"), INKSCAPE_ICON("draw-polygon-star"));
    }

    _freeze = false;
}

void StarToolbar::rounded_value_changed()
{
    auto adj = _roundedness_item.get_adjustment();

    if (DocumentUndo::getUndoSensitive(_desktop->getDocument())) {
        Preferences::get()->setDouble("/tools/shapes/star/rounded", adj->get_value());
    }

    // quit if run by the attr_changed listener
    if (_freeze) {
        return;
    }

    // in turn, prevent listener from responding
    _freeze = true;

    for (auto item : _desktop->getSelection()->items()) {
        if (is<SPStar>(item)) {
            auto repr = item->getRepr();
            repr->setAttributeSvgDouble("inkscape:rounded", adj->get_value());
            item->updateRepr();
        }
    }
    if (!_batchundo) {
        DocumentUndo::maybeDone(_desktop->getDocument(), "star:rounding", _("Star: Change rounding"), INKSCAPE_ICON("draw-polygon-star"));
    }

    _freeze = false;
}

void StarToolbar::randomized_value_changed()
{
    auto adj = _randomization_item.get_adjustment();

    if (DocumentUndo::getUndoSensitive(_desktop->getDocument())) {
        Preferences::get()->setDouble("/tools/shapes/star/randomized", adj->get_value());
    }

    // quit if run by the attr_changed listener
    if (_freeze) {
        return;
    }

    // in turn, prevent listener from responding
    _freeze = true;

    for (auto item : _desktop->getSelection()->items()) {
        if (is<SPStar>(item)) {
            auto repr = item->getRepr();
            repr->setAttributeSvgDouble("inkscape:randomized", adj->get_value());
            item->updateRepr();
        }
    }
    if (!_batchundo) {
        DocumentUndo::maybeDone(_desktop->getDocument(), "star:randomisation", _("Star: Change randomization"), INKSCAPE_ICON("draw-polygon-star"));
    }

    _freeze = false;
}

void StarToolbar::defaults()
{
    _batchundo = true;

    // fixme: make settable in prefs!
    int mag = 5;
    double prop = 0.5;
    bool flat = false;
    double randomized = 0;
    double rounded = 0;

    _flat_item_buttons[flat ? 0 : 1]->set_active();

    _spoke_item.set_visible(!flat);

    if (_magnitude_item.get_adjustment()->get_value() == mag) {
        // Ensure handler runs even if value not changed, to reset inner handle.
        magnitude_value_changed();
    } else {
        _magnitude_item.get_adjustment()->set_value(mag);
    }
    _spoke_item.get_adjustment()->set_value(prop);
    _roundedness_item.get_adjustment()->set_value(rounded);
    _randomization_item.get_adjustment()->set_value(randomized);

    DocumentUndo::done(_desktop->getDocument(), _("Star: Reset to defaults"), INKSCAPE_ICON("draw-polygon-star"));
    _batchundo = false;
}

void StarToolbar::watch_tool(SPDesktop *desktop, UI::Tools::ToolBase *tool)
{
    _changed.disconnect();
    if (dynamic_cast<UI::Tools::StarTool const*>(tool)) {
        _changed = desktop->getSelection()->connectChanged(sigc::mem_fun(*this, &StarToolbar::selection_changed));
        selection_changed(desktop->getSelection());
    }
}

/**
 *  \param selection Should not be NULL.
 */
void StarToolbar::selection_changed(Selection *selection)
{
    int n_selected = 0;
    XML::Node *repr = nullptr;

    if (_repr) { // remove old listener
        _repr->removeObserver(*this);
        Inkscape::GC::release(_repr);
        _repr = nullptr;
    }

    for (auto item : selection->items()) {
        if (is<SPStar>(item)) {
            n_selected++;
            repr = item->getRepr();
        }
    }

    if (n_selected == 0) {
        _mode_item.set_markup(_("<b>New:</b>"));
    } else if (n_selected == 1) {
        _mode_item.set_markup(_("<b>Change:</b>"));

        if (repr) {
            _repr = repr;
            Inkscape::GC::anchor(_repr);
            _repr->addObserver(*this);
            _repr->synthesizeEvents(*this);
        }
    } else {
        // FIXME: implement averaging of all parameters for multiple selected stars
        //gtk_label_set_markup(GTK_LABEL(l), _("<b>Average:</b>"));
        //gtk_label_set_markup(GTK_LABEL(l), _("<b>Change:</b>"));
    }
}

void StarToolbar::notifyAttributeChanged(Inkscape::XML::Node &repr, GQuark name_,
                                         Inkscape::Util::ptr_shared,
                                         Inkscape::Util::ptr_shared)
{
    // quit if run by the _changed callbacks
    if (_freeze) {
        return;
    }

    auto const name = g_quark_to_string(name_);

    // in turn, prevent callbacks from responding
    _freeze = true;

    bool isFlatSided = Preferences::get()->getBool("/tools/shapes/star/isflatsided", false);
    auto mag_adj = _magnitude_item.get_adjustment();
    auto spoke_adj = _magnitude_item.get_adjustment();

    if (!strcmp(name, "inkscape:randomized")) {
        double randomized = repr.getAttributeDouble("inkscape:randomized", 0.0);
        _randomization_item.get_adjustment()->set_value(randomized);
    } else if (!strcmp(name, "inkscape:rounded")) {
        double rounded = repr.getAttributeDouble("inkscape:rounded", 0.0);
        _roundedness_item.get_adjustment()->set_value(rounded);
    } else if (!strcmp(name, "inkscape:flatsided")) {
        char const *flatsides = repr.attribute("inkscape:flatsided");
        if (flatsides && !strcmp(flatsides,"false")) {
            _flat_item_buttons[1]->set_active();
            _spoke_item.set_visible(true);
            mag_adj->set_lower(2);
        } else {
            _flat_item_buttons[0]->set_active();
            _spoke_item.set_visible(false);
            mag_adj->set_lower(3);
        }
    } else if (!strcmp(name, "sodipodi:r1") || !strcmp(name, "sodipodi:r2") && !isFlatSided) {
        double r1 = repr.getAttributeDouble("sodipodi:r1", 1.0);
        double r2 = repr.getAttributeDouble("sodipodi:r2", 1.0);

        if (r2 < r1) {
            spoke_adj->set_value(r2 / r1);
        } else {
            spoke_adj->set_value(r1 / r2);
        }
    } else if (!strcmp(name, "sodipodi:sides")) {
        int sides = repr.getAttributeInt("sodipodi:sides", 0);
        mag_adj->set_value(sides);
    }

    _freeze = false;
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
