// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Arc aux toolbar
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

#include "arc-toolbar.h"

#include <glibmm/i18n.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/label.h>
#include <gtkmm/togglebutton.h>

#include "desktop.h"
#include "document-undo.h"
#include "mod360.h"
#include "object/sp-ellipse.h"
#include "object/sp-namedview.h"
#include "selection.h"
#include "ui/builder-utils.h"
#include "ui/icon-names.h"
#include "ui/tools/arc-tool.h"
#include "ui/util.h"
#include "ui/widget/canvas.h"
#include "ui/widget/combo-tool-item.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/toolbar-menu-button.h"
#include "ui/widget/unit-tracker.h"
#include "widgets/widget-sizes.h"

using Inkscape::UI::Widget::UnitTracker;
using Inkscape::DocumentUndo;
using Inkscape::Util::Quantity;

namespace Inkscape::UI::Toolbar {

ArcToolbar::ArcToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    , _tracker(new UnitTracker(Inkscape::Util::UNIT_TYPE_LINEAR))
    , _builder(create_builder("toolbar-arc.ui"))
    , _mode_item(get_widget<Gtk::Label>(_builder, "_mode_item"))
    , _rx_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_rx_item"))
    , _ry_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_ry_item"))
    , _start_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_start_item"))
    , _end_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_end_item"))
    , _make_whole(get_widget<Gtk::Button>(_builder, "_make_whole"))
{
    _toolbar = &get_widget<Gtk::Box>(_builder, "arc-toolbar");

    auto init_units = desktop->getNamedView()->display_units;
    _tracker->setActiveUnit(init_units);

    auto unit_menu = _tracker->create_tool_item(_("Units"), (""));
    get_widget<Gtk::Box>(_builder, "unit_menu_box").append(*unit_menu);

    setup_derived_spin_button(_rx_item, "rx");
    setup_derived_spin_button(_ry_item, "ry");
    setup_startend_button(_start_item, "start");
    setup_startend_button(_end_item, "end");

    _type_buttons.push_back(&get_widget<Gtk::ToggleButton>(_builder, "slice_btn"));
    _type_buttons.push_back(&get_widget<Gtk::ToggleButton>(_builder, "arc_btn"));
    _type_buttons.push_back(&get_widget<Gtk::ToggleButton>(_builder, "chord_btn"));

    int type = Preferences::get()->getInt("/tools/shapes/arc/arc_type", 0);
    _type_buttons[type]->set_active();
    int btn_index = 0;

    for (auto btn : _type_buttons) {
        btn->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &ArcToolbar::type_changed), btn_index++));
    }

    // TODO: Get rid of pointers.
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

    _make_whole.signal_clicked().connect(sigc::mem_fun(*this, &ArcToolbar::defaults));

    _single = true;

    // sensitivize make whole and open checkbox
    sensitivize(_start_item.get_adjustment()->get_value(), _end_item.get_adjustment()->get_value());

    desktop->connectEventContextChanged(sigc::mem_fun(*this, &ArcToolbar::check_ec));
}

void ArcToolbar::setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name)
{
    auto init_units = _desktop->getNamedView()->display_units;
    auto adj = btn.get_adjustment();
    const Glib::ustring path = "/tools/shapes/arc/" + static_cast<Glib::ustring>(name);
    auto val = Preferences::get()->getDouble(path, 0);
    val = Quantity::convert(val, "px", init_units);
    adj->set_value(val);

    adj->signal_value_changed().connect(sigc::bind(sigc::mem_fun(*this, &ArcToolbar::value_changed), adj, name));
    _tracker->addAdjustment(adj->gobj());

    btn.addUnitTracker(_tracker.get());
    btn.set_defocus_widget(_desktop->getCanvas());
    btn.set_sensitive(false);
}

void ArcToolbar::setup_startend_button(UI::Widget::SpinButton &btn, Glib::ustring const &name)
{
    auto adj = btn.get_adjustment();
    const Glib::ustring path = "/tools/shapes/arc/" + name;
    auto val = Preferences::get()->getDouble(path, 0);
    adj->set_value(val);

    btn.set_defocus_widget(_desktop->getCanvas());

    // Using the end item's adjustment when the name is "start" is intentional.
    auto adjustment = name == "start" ? _end_item.get_adjustment() : _start_item.get_adjustment();
    adj->signal_value_changed().connect(
        sigc::bind(sigc::mem_fun(*this, &ArcToolbar::startend_value_changed), adj, name, std::move(adjustment)));
}

ArcToolbar::~ArcToolbar()
{
    if (_repr) {
        _repr->removeObserver(*this);
        GC::release(_repr);
        _repr = nullptr;
    }
}

void ArcToolbar::value_changed(Glib::RefPtr<Gtk::Adjustment> &adj, Glib::ustring const &value_name)
{
    // Per SVG spec "a [radius] value of zero disables rendering of the element".
    // However our implementation does not allow a setting of zero in the UI (not even in the XML editor)
    // and ugly things happen if it's forced here, so better leave the properties untouched.
    if (!adj->get_value()) {
        return;
    }

    Unit const *unit = _tracker->getActiveUnit();
    g_return_if_fail(unit != nullptr);

    SPDocument* document = _desktop->getDocument();

    if (DocumentUndo::getUndoSensitive(document)) {
        Preferences::get()->setDouble(Glib::ustring("/tools/shapes/arc/") + value_name,
                                      Quantity::convert(adj->get_value(), unit, "px"));
    }

    // quit if run by the attr_changed listener
    if (_freeze || _tracker->isUpdating()) {
        return;
    }

    // in turn, prevent listener from responding
    _freeze = true;

    bool modmade = false;
    Inkscape::Selection *selection = _desktop->getSelection();
    auto itemlist= selection->items();
    for(auto i=itemlist.begin();i!=itemlist.end();++i){
        SPItem *item = *i;
        if (is<SPGenericEllipse>(item)) {

            auto ge = cast<SPGenericEllipse>(item);

            if (value_name == "rx") {
                ge->setVisibleRx(Quantity::convert(adj->get_value(), unit, "px"));
            } else {
                ge->setVisibleRy(Quantity::convert(adj->get_value(), unit, "px"));
            }

            ge->normalize();
            ge->updateRepr();
            ge->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);

            modmade = true;
        }
    }

    if (modmade) {
        DocumentUndo::done(_desktop->getDocument(), _("Ellipse: Change radius"), INKSCAPE_ICON("draw-ellipse"));
    }

    _freeze = false;
}

void ArcToolbar::startend_value_changed(Glib::RefPtr<Gtk::Adjustment> &adj, Glib::ustring const &value_name,
                                        Glib::RefPtr<Gtk::Adjustment> &other_adj)

{
    if (DocumentUndo::getUndoSensitive(_desktop->getDocument())) {
        Preferences::get()->setDouble(Glib::ustring("/tools/shapes/arc/") + value_name, adj->get_value());
    }

    // quit if run by the attr_changed listener
    if (_freeze) {
        return;
    }

    // in turn, prevent listener from responding
    _freeze = true;

    bool modmade = false;
    auto itemlist= _desktop->getSelection()->items();
    for(auto i=itemlist.begin();i!=itemlist.end();++i){
        SPItem *item = *i;
        if (is<SPGenericEllipse>(item)) {

            auto ge = cast<SPGenericEllipse>(item);

            if (value_name == "start") {
                ge->start = (adj->get_value() * M_PI)/ 180;
            } else {
                ge->end = (adj->get_value() * M_PI)/ 180;
            }

            ge->normalize();
            ge->updateRepr();
            ge->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);

            modmade = true;
        }
    }

    sensitivize( adj->get_value(), other_adj->get_value() );

    if (modmade) {
        DocumentUndo::maybeDone(_desktop->getDocument(), value_name.c_str(), _("Arc: Change start/end"),
                                INKSCAPE_ICON("draw-ellipse"));
    }

    _freeze = false;
}

void ArcToolbar::type_changed(int type)
{
    if (DocumentUndo::getUndoSensitive(_desktop->getDocument())) {
        Preferences::get()->setInt("/tools/shapes/arc/arc_type", type);
    }

    // quit if run by the attr_changed listener
    if (_freeze) {
        return;
    }

    // in turn, prevent listener from responding
    _freeze = true;

    Glib::ustring arc_type = "slice";
    bool open = false;
    switch (type) {
        case 0:
            arc_type = "slice";
            open = false;
            break;
        case 1:
            arc_type = "arc";
            open = true;
            break;
        case 2:
            arc_type = "chord";
            open = true; // For backward compat, not truly open but chord most like arc.
            break;
        default:
            std::cerr << "sp_arctb_type_changed: bad arc type: " << type << std::endl;
    }

    bool modmade = false;
    auto itemlist= _desktop->getSelection()->items();
    for(auto i=itemlist.begin();i!=itemlist.end();++i){
        SPItem *item = *i;
        if (is<SPGenericEllipse>(item)) {
            Inkscape::XML::Node *repr = item->getRepr();
            repr->setAttribute("sodipodi:open", (open?"true":nullptr) );
            repr->setAttribute("sodipodi:arc-type", arc_type);
            item->updateRepr();
            modmade = true;
        }
    }

    if (modmade) {
        DocumentUndo::done(_desktop->getDocument(), _("Arc: Change arc type"), INKSCAPE_ICON("draw-ellipse"));
    }

    _freeze = false;
}

void ArcToolbar::defaults()
{
    _start_item.get_adjustment()->set_value(0.0);
    _end_item.get_adjustment()->set_value(0.0);

    if(_desktop->getCanvas()) _desktop->getCanvas()->grab_focus();
}

void ArcToolbar::sensitivize(double v1, double v2)
{
    if (v1 == 0 && v2 == 0) {
        if (_single) { // only for a single selected ellipse (for now)
            for (auto btn : _type_buttons) btn->set_sensitive(false);
            _make_whole.set_sensitive(false);
        }
    } else {
        for (auto btn : _type_buttons) btn->set_sensitive(true);
        _make_whole.set_sensitive(true);
    }
}

void ArcToolbar::check_ec(SPDesktop *desktop, Inkscape::UI::Tools::ToolBase *tool)
{
    if (dynamic_cast<Tools::ArcTool const *>(tool)) {
        _changed = _desktop->getSelection()->connectChanged(sigc::mem_fun(*this, &ArcToolbar::selection_changed));
        selection_changed(desktop->getSelection());
    } else {
        if (_changed) {
            _changed.disconnect();
            if(_repr) {
                _repr->removeObserver(*this);
                Inkscape::GC::release(_repr);
                _repr = nullptr;
            }
        }
    }
}

void ArcToolbar::selection_changed(Inkscape::Selection *selection)
{
    int n_selected = 0;
    Inkscape::XML::Node *repr = nullptr;

    if ( _repr ) {
        _item = nullptr;
        _repr->removeObserver(*this);
        GC::release(_repr);
        _repr = nullptr;
    }

    SPItem *item = nullptr;

    for(auto i : selection->items()){
        if (is<SPGenericEllipse>(i)) {
            n_selected++;
            item = i;
            repr = item->getRepr();
        }
    }

    _single = false;
    if (n_selected == 0) {
        _mode_item.set_markup(_("<b>New:</b>"));
    } else if (n_selected == 1) {
        _single = true;
        _mode_item.set_markup(_("<b>Change:</b>"));
        _rx_item.set_sensitive(true);
        _ry_item.set_sensitive(true);

        if (repr) {
            _repr = repr;
            _item = item;
            Inkscape::GC::anchor(_repr);
            _repr->addObserver(*this);
            _repr->synthesizeEvents(*this);
        }
    } else {
        // FIXME: implement averaging of all parameters for multiple selected
        //gtk_label_set_markup(GTK_LABEL(l), _("<b>Average:</b>"));
        _mode_item.set_markup(_("<b>Change:</b>"));
        sensitivize( 1, 0 );
    }
}


void ArcToolbar::notifyAttributeChanged(Inkscape::XML::Node &repr, GQuark,
                                        Inkscape::Util::ptr_shared,
                                        Inkscape::Util::ptr_shared)
{
    // quit if run by the _changed callbacks
    if (_freeze) {
        return;
    }

    // in turn, prevent callbacks from responding
    _freeze = true;

    if (auto ge = cast<SPGenericEllipse>(_item)) {
        Unit const *unit = _tracker->getActiveUnit();
        g_return_if_fail(unit != nullptr);

        gdouble rx = ge->getVisibleRx();
        gdouble ry = ge->getVisibleRy();
        _rx_item.get_adjustment()->set_value(Quantity::convert(rx, "px", unit));
        _ry_item.get_adjustment()->set_value(Quantity::convert(ry, "px", unit));
    }

    gdouble start = repr.getAttributeDouble("sodipodi:start", 0.0);;
    gdouble end = repr.getAttributeDouble("sodipodi:end", 0.0);

    _start_item.get_adjustment()->set_value(mod360((start * 180) / M_PI));
    _end_item.get_adjustment()->set_value(mod360((end * 180) / M_PI));

    sensitivize(_start_item.get_adjustment()->get_value(), _end_item.get_adjustment()->get_value());

    char const *arctypestr = nullptr;
    arctypestr = repr.attribute("sodipodi:arc-type");
    if (!arctypestr) { // For old files.
        char const *openstr = nullptr;
        openstr = repr.attribute("sodipodi:open");
        arctypestr = (openstr ? "arc" : "slice");
    }

    if (!strcmp(arctypestr,"slice")) {
        _type_buttons[0]->set_active();
    } else if (!strcmp(arctypestr,"arc")) {
        _type_buttons[1]->set_active();
    } else {
        _type_buttons[2]->set_active();
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
