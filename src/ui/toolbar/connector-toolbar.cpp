// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Connector aux toolbar
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

#include "connector-toolbar.h"

#include <glibmm/i18n.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/builder.h>
#include <gtkmm/togglebutton.h>

#include "conn-avoid-ref.h"
#include "desktop.h"
#include "document-undo.h"
#include "enums.h"
#include "layer-manager.h"
#include "object/algorithms/graphlayout.h"
#include "object/sp-namedview.h"
#include "object/sp-path.h"
#include "selection.h"
#include "ui/builder-utils.h"
#include "ui/icon-names.h"
#include "ui/tools/connector-tool.h"
#include "ui/widget/canvas.h"
#include "ui/widget/spinbutton.h"

using Inkscape::DocumentUndo;

namespace Inkscape::UI::Toolbar {

ConnectorToolbar::ConnectorToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    , _builder(create_builder("toolbar-connector.ui"))
    , _orthogonal_btn(get_widget<Gtk::ToggleButton>             (_builder, "_orthogonal_btn"))
    , _curvature_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_curvature_item"))
    , _spacing_item  (get_derived_widget<UI::Widget::SpinButton>(_builder, "_spacing_item"))
    , _length_item   (get_derived_widget<UI::Widget::SpinButton>(_builder, "_length_item"))
    , _directed_btn  (get_widget<Gtk::ToggleButton>             (_builder, "_directed_btn"))
    , _overlap_btn   (get_widget<Gtk::ToggleButton>             (_builder, "_overlap_btn"))
{
    auto prefs = Inkscape::Preferences::get();

    setup_derived_spin_button(_curvature_item, "curvature", defaultConnCurvature, &ConnectorToolbar::curvature_changed);
    setup_derived_spin_button(_spacing_item, "spacing", defaultConnSpacing, &ConnectorToolbar::spacing_changed);
    setup_derived_spin_button(_length_item, "length", 100, &ConnectorToolbar::length_changed);

    _toolbar = &get_widget<Gtk::Box>(_builder, "connector-toolbar");
    set_child(*_toolbar);

    // Orthogonal connectors toggle button
    bool tbuttonstate = prefs->getBool("/tools/connector/orthogonal");
    _orthogonal_btn.set_active((tbuttonstate ? TRUE : FALSE));

    // Directed edges toggle button
    tbuttonstate = prefs->getBool("/tools/connector/directedlayout");
    _directed_btn.set_active(tbuttonstate ? TRUE : FALSE);

    // Avoid overlaps toggle button
    tbuttonstate = prefs->getBool("/tools/connector/avoidoverlaplayout");
    _overlap_btn.set_active(tbuttonstate ? TRUE : FALSE);

    // Signals.
    get_widget<Gtk::Button>(_builder, "avoid_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &ConnectorToolbar::path_set_avoid));
    get_widget<Gtk::Button>(_builder, "ignore_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &ConnectorToolbar::path_set_ignore));
    get_widget<Gtk::Button>(_builder, "graph_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &ConnectorToolbar::graph_layout));

    _orthogonal_btn.signal_toggled().connect(sigc::mem_fun(*this, &ConnectorToolbar::orthogonal_toggled));
    _directed_btn.signal_toggled().connect(sigc::mem_fun(*this, &ConnectorToolbar::directed_graph_layout_toggled));
    _overlap_btn.signal_toggled().connect(sigc::mem_fun(*this, &ConnectorToolbar::nooverlaps_graph_layout_toggled));
    desktop->getSelection()->connectChanged(sigc::mem_fun(*this, &ConnectorToolbar::selection_changed));

    // Code to watch for changes to the connector-spacing attribute in
    // the XML.
    Inkscape::XML::Node *repr = desktop->getNamedView()->getRepr();
    g_assert(repr != nullptr);

    if(_repr) {
        _repr->removeObserver(*this);
        Inkscape::GC::release(_repr);
        _repr = nullptr;
    }

    if (repr) {
        _repr = repr;
        Inkscape::GC::anchor(_repr);
        _repr->addObserver(*this);
        _repr->synthesizeEvents(*this);
    }
}

ConnectorToolbar::~ConnectorToolbar() = default;

void ConnectorToolbar::setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name,
                                                 double default_value, ValueChangedMemFun const value_changed_mem_fun)
{
    auto adj = btn.get_adjustment();
    const Glib::ustring path = "/tools/connector/" + name;
    auto const val = Preferences::get()->getDouble(path, default_value);
    adj->set_value(val);
    adj->signal_value_changed().connect(sigc::mem_fun(*this, value_changed_mem_fun));

    btn.set_defocus_widget(_desktop->getCanvas());
}

void ConnectorToolbar::path_set_avoid()
{
    Inkscape::UI::Tools::cc_selection_set_avoid(_desktop, true);
}

void ConnectorToolbar::path_set_ignore()
{
    Inkscape::UI::Tools::cc_selection_set_avoid(_desktop, false);
}

void ConnectorToolbar::orthogonal_toggled()
{
    auto doc = _desktop->getDocument();

    if (!DocumentUndo::getUndoSensitive(doc)) {
        return;
    }

    // quit if run by the _changed callbacks
    if (_freeze) {
        return;
    }

    // in turn, prevent callbacks from responding
    _freeze = true;

    bool is_orthog = _orthogonal_btn.get_active();
    gchar orthog_str[] = "orthogonal";
    gchar polyline_str[] = "polyline";
    gchar *value = is_orthog ? orthog_str : polyline_str ;

    bool modmade = false;
    auto itemlist= _desktop->getSelection()->items();
    for(auto i=itemlist.begin();i!=itemlist.end();++i){
        SPItem *item = *i;

        if (Inkscape::UI::Tools::cc_item_is_connector(item)) {
            item->setAttribute( "inkscape:connector-type", value);
            item->getAvoidRef().handleSettingChange();
            modmade = true;
        }
    }

    if (!modmade) {
        Preferences::get()->setBool("/tools/connector/orthogonal", is_orthog);
    } else {

        DocumentUndo::done(doc, is_orthog ? _("Set connector type: orthogonal"): _("Set connector type: polyline"), INKSCAPE_ICON("draw-connector"));
    }

    _freeze = false;
}

void ConnectorToolbar::curvature_changed()
{
    SPDocument *doc = _desktop->getDocument();

    if (!DocumentUndo::getUndoSensitive(doc)) {
        return;
    }


    // quit if run by the _changed callbacks
    if (_freeze) {
        return;
    }

    // in turn, prevent callbacks from responding
    _freeze = true;

    auto newValue = _curvature_item.get_adjustment()->get_value();
    gchar value[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_dtostr(value, G_ASCII_DTOSTR_BUF_SIZE, newValue);

    bool modmade = false;
    auto itemlist= _desktop->getSelection()->items();
    for(auto i=itemlist.begin();i!=itemlist.end();++i){
        SPItem *item = *i;

        if (Inkscape::UI::Tools::cc_item_is_connector(item)) {
            item->setAttribute( "inkscape:connector-curvature", value);
            item->getAvoidRef().handleSettingChange();
            modmade = true;
        }
    }

    if (!modmade) {
        Preferences::get()->setDouble(Glib::ustring("/tools/connector/curvature"), newValue);
    }
    else {
        DocumentUndo::done(doc, _("Change connector curvature"), INKSCAPE_ICON("draw-connector"));
    }

    _freeze = false;
}

void ConnectorToolbar::spacing_changed()
{
    SPDocument *doc = _desktop->getDocument();

    if (!DocumentUndo::getUndoSensitive(doc)) {
        return;
    }

    Inkscape::XML::Node *repr = _desktop->getNamedView()->getRepr();

    if (!repr->attribute("inkscape:connector-spacing") &&
        (_spacing_item.get_adjustment()->get_value() == defaultConnSpacing)) {
        // Don't need to update the repr if the attribute doesn't
        // exist and it is being set to the default value -- as will
        // happen at startup.
        return;
    }

    // quit if run by the attr_changed listener
    if (_freeze) {
        return;
    }

    // in turn, prevent listener from responding
    _freeze = true;

    repr->setAttributeCssDouble("inkscape:connector-spacing", _spacing_item.get_adjustment()->get_value());
    _desktop->getNamedView()->updateRepr();
    bool modmade = false;

    auto items = get_avoided_items(_desktop->layerManager().currentRoot(), _desktop);
    for (auto item : items) {
        Geom::Affine m = Geom::identity();
        avoid_item_move(&m, item);
        modmade = true;
    }

    if(modmade) {
        DocumentUndo::done(doc, _("Change connector spacing"), INKSCAPE_ICON("draw-connector"));
    }
    _freeze = false;
}

void ConnectorToolbar::graph_layout()
{
    assert(_desktop);
    if (!_desktop) {
        return;
    }
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    // hack for clones, see comment in align-and-distribute.cpp
    int saved_compensation = prefs->getInt("/options/clonecompensation/value", SP_CLONE_COMPENSATION_UNMOVED);
    prefs->setInt("/options/clonecompensation/value", SP_CLONE_COMPENSATION_UNMOVED);

    auto tmp = _desktop->getSelection()->items();
    std::vector<SPItem *> vec(tmp.begin(), tmp.end());
    graphlayout(vec);

    prefs->setInt("/options/clonecompensation/value", saved_compensation);

    DocumentUndo::done(_desktop->getDocument(), _("Arrange connector network"), INKSCAPE_ICON("dialog-align-and-distribute"));
}

void ConnectorToolbar::length_changed()
{
    Preferences::get()->setDouble("/tools/connector/length", _length_item.get_adjustment()->get_value());
}

void ConnectorToolbar::directed_graph_layout_toggled()
{
    Preferences::get()->setBool("/tools/connector/directedlayout", _directed_btn.get_active());
}

void ConnectorToolbar::selection_changed(Inkscape::Selection *selection)
{
    SPItem *item = selection->singleItem();
    if (is<SPPath>(item))
    {
        gdouble curvature = cast<SPPath>(item)->connEndPair.getCurvature();
        bool is_orthog = cast<SPPath>(item)->connEndPair.isOrthogonal();
        _orthogonal_btn.set_active(is_orthog);
        _curvature_item.get_adjustment()->set_value(curvature);
    }

}

void ConnectorToolbar::nooverlaps_graph_layout_toggled()
{
    Preferences::get()->setBool("/tools/connector/avoidoverlaplayout", _overlap_btn.get_active());
}

void ConnectorToolbar::notifyAttributeChanged(Inkscape::XML::Node &repr, GQuark name_,
                                              Inkscape::Util::ptr_shared,
                                              Inkscape::Util::ptr_shared)
{
    auto const name = g_quark_to_string(name_);
    if (!_freeze && (strcmp(name, "inkscape:connector-spacing") == 0) ) {
        gdouble spacing = repr.getAttributeDouble("inkscape:connector-spacing", defaultConnSpacing);

        _spacing_item.get_adjustment()->set_value(spacing);

        if (_desktop->getCanvas()) {
            _desktop->getCanvas()->grab_focus();
        }
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
