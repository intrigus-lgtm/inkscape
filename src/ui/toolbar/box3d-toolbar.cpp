// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * 3d box aux toolbar
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

#include "box3d-toolbar.h"

#include <glibmm/i18n.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/togglebutton.h>

#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "object/box3d.h"
#include "object/persp3d.h"
#include "selection.h"
#include "ui/builder-utils.h"
#include "ui/icon-names.h"
#include "ui/tools/box3d-tool.h"
#include "ui/widget/canvas.h"
#include "ui/widget/spinbutton.h"

using Inkscape::DocumentUndo;

namespace Inkscape::UI::Toolbar {

/// Normalize angle so that it lies in the interval [0, 360).
static double normalize_angle(double a)
{
    return a - std::floor(a / 360.0) * 360.0;
}

Box3DToolbar::Box3DToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    , _builder(create_builder("toolbar-box3d.ui"))
    , _angle_x_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_angle_x_item"))
    , _vp_x_state_btn(get_widget<Gtk::ToggleButton>(_builder, "_vp_x_state_btn"))
    , _angle_y_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_angle_y_item"))
    , _vp_y_state_btn(get_widget<Gtk::ToggleButton>(_builder, "_vp_y_state_btn"))
    , _angle_z_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_angle_z_item"))
    , _vp_z_state_btn(get_widget<Gtk::ToggleButton>(_builder, "_vp_z_state_btn"))
{
    auto prefs = Inkscape::Preferences::get();

    _toolbar = &get_widget<Gtk::Box>(_builder, "box3d-toolbar");

    _vp_x_state_btn.signal_toggled().connect(
        sigc::bind(sigc::mem_fun(*this, &Box3DToolbar::vp_state_changed), Proj::X));
    _vp_x_state_btn.set_active(prefs->getBool("/tools/shapes/3dbox/vp_x_state", true));

    _vp_y_state_btn.signal_toggled().connect(
        sigc::bind(sigc::mem_fun(*this, &Box3DToolbar::vp_state_changed), Proj::Y));
    _vp_y_state_btn.set_active(prefs->getBool("/tools/shapes/3dbox/vp_y_state", true));

    _vp_z_state_btn.signal_toggled().connect(
        sigc::bind(sigc::mem_fun(*this, &Box3DToolbar::vp_state_changed), Proj::Z));
    _vp_z_state_btn.set_active(prefs->getBool("/tools/shapes/3dbox/vp_z_state", true));

    setup_derived_spin_button(_angle_x_item, "box3d_angle_x", Proj::X);
    setup_derived_spin_button(_angle_y_item, "box3d_angle_y", Proj::Y);
    setup_derived_spin_button(_angle_z_item, "box3d_angle_z", Proj::Z);

    desktop->connectEventContextChanged(sigc::mem_fun(*this, &Box3DToolbar::check_ec));

    set_child(*_toolbar);
}

void Box3DToolbar::setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name,
                                             Proj::Axis const axis)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    auto document = _desktop->getDocument();
    auto persp_impl = document->getCurrentPersp3DImpl();

    const Glib::ustring path = "/tools/shapes/3dbox/" + name;
    auto const val = prefs->getDouble(path, 30);

    auto adj = btn.get_adjustment();
    adj->set_value(val);

    adj->signal_value_changed().connect(
        sigc::bind(sigc::mem_fun(*this, &Box3DToolbar::angle_value_changed), adj, axis));

    bool const is_sensitive = !persp_impl || !Persp3D::VP_is_finite(persp_impl, axis);

    btn.set_sensitive(is_sensitive);
    btn.set_defocus_widget(_desktop->getCanvas());
}

void
Box3DToolbar::angle_value_changed(Glib::RefPtr<Gtk::Adjustment> &adj,
                                  Proj::Axis                     axis)
{
    SPDocument *document = _desktop->getDocument();

    // quit if run by the attr_changed or selection changed listener
    if (_freeze) {
        return;
    }

    // in turn, prevent listener from responding
    _freeze = true;

    std::list<Persp3D *> sel_persps = _desktop->getSelection()->perspList();
    if (sel_persps.empty()) {
        // this can happen when the document is created; we silently ignore it
        return;
    }
    Persp3D *persp = sel_persps.front();

    persp->perspective_impl->tmat.set_infinite_direction (axis,
                                                          adj->get_value());
    persp->updateRepr();

    // TODO: use the correct axis here, too
    DocumentUndo::maybeDone(document, "perspangle", _("3D Box: Change perspective (angle of infinite axis)"), INKSCAPE_ICON("draw-cuboid"));

    _freeze = false;
}

void
Box3DToolbar::vp_state_changed(Proj::Axis axis)
{
    // TODO: Take all selected perspectives into account
    auto sel_persps = _desktop->getSelection()->perspList();
    if (sel_persps.empty()) {
        // this can happen when the document is created; we silently ignore it
        return;
    }
    Persp3D *persp = sel_persps.front();

    bool set_infinite = false;

    switch(axis) {
        case Proj::X:
            set_infinite = _vp_x_state_btn.get_active();
            break;
        case Proj::Y:
            set_infinite = _vp_y_state_btn.get_active();
            break;
        case Proj::Z:
            set_infinite = _vp_z_state_btn.get_active();
            break;
        default:
            return;
    }

    persp->set_VP_state (axis, set_infinite ? Proj::VP_INFINITE : Proj::VP_FINITE);
}

void
Box3DToolbar::check_ec(SPDesktop* desktop, Inkscape::UI::Tools::ToolBase* tool)
{
    if (dynamic_cast<Inkscape::UI::Tools::Box3dTool*>(tool)) {
        _changed = desktop->getSelection()->connectChanged(sigc::mem_fun(*this, &Box3DToolbar::selection_changed));
        selection_changed(desktop->getSelection());
    } else {
        if (_changed)
            _changed.disconnect();

        if (_repr) { // remove old listener
            _repr->removeObserver(*this);
            Inkscape::GC::release(_repr);
            _repr = nullptr;
        }
    }
}

Box3DToolbar::~Box3DToolbar()
{
    if (_repr) { // remove old listener
        _repr->removeObserver(*this);
        Inkscape::GC::release(_repr);
        _repr = nullptr;
    }
}

/**
 *  \param selection Should not be NULL.
 */
// FIXME: This should rather be put into persp3d-reference.cpp or something similar so that it reacts upon each
//        Change of the perspective, and not of the current selection (but how to refer to the toolbar then?)
void
Box3DToolbar::selection_changed(Inkscape::Selection *selection)
{
    // Here the following should be done: If all selected boxes have finite VPs in a certain direction,
    // disable the angle entry fields for this direction (otherwise entering a value in them should only
    // update the perspectives with infinite VPs and leave the other ones untouched).

    Inkscape::XML::Node *persp_repr = nullptr;

    if (_repr) { // remove old listener
        _repr->removeObserver(*this);
        Inkscape::GC::release(_repr);
        _repr = nullptr;
    }

    SPItem *item = selection->singleItem();
    auto box = cast<SPBox3D>(item);
    if (box) {
        // FIXME: Also deal with multiple selected boxes
        Persp3D *persp = box->get_perspective();
        if (!persp) {
            g_warning("Box has no perspective set!");
            return;
        }
        persp_repr = persp->getRepr();
        if (persp_repr) {
            _repr = persp_repr;
            Inkscape::GC::anchor(_repr);
            _repr->addObserver(*this);
            _repr->synthesizeEvents(*this);

            selection->document()->setCurrentPersp3D(Persp3D::get_from_repr(_repr));
            Inkscape::Preferences *prefs = Inkscape::Preferences::get();
            prefs->setString("/tools/shapes/3dbox/persp", _repr->attribute("id"));

            _freeze = true;
            resync_toolbar(_repr);
            _freeze = false;
        }
    }
}

void
Box3DToolbar::resync_toolbar(Inkscape::XML::Node *persp_repr)
{
    if (!persp_repr) {
        g_warning ("No perspective given to box3d_resync_toolbar().");
        return;
    }

    Persp3D *persp = Persp3D::get_from_repr(persp_repr);
    if (!persp) {
        // Hmm, is it an error if this happens?
        return;
    }
    set_button_and_adjustment(persp, Proj::X, _angle_x_item, _vp_x_state_btn);
    set_button_and_adjustment(persp, Proj::Y, _angle_y_item, _vp_y_state_btn);
    set_button_and_adjustment(persp, Proj::Z, _angle_z_item, _vp_z_state_btn);
}

void Box3DToolbar::set_button_and_adjustment(Persp3D *persp, Proj::Axis axis, UI::Widget::SpinButton &spin_btn,
                                             Gtk::ToggleButton &toggle_btn)
{
    // TODO: Take all selected perspectives into account but don't touch the state button if not all of them
    //       have the same state (otherwise a call to box3d_vp_z_state_changed() is triggered and the states
    //       are reset).
    bool is_infinite = !Persp3D::VP_is_finite(persp->perspective_impl.get(), axis);
    auto adj = spin_btn.get_adjustment();

    if (is_infinite) {
        toggle_btn.set_active(true);
        spin_btn.set_sensitive(true);

        double angle = persp->get_infinite_angle(axis);
        if (angle != Geom::infinity()) { // FIXME: We should catch this error earlier (don't show the spinbutton at all)
            adj->set_value(normalize_angle(angle));
        }
    } else {
        toggle_btn.set_active(false);
        spin_btn.set_sensitive(false);
    }
}

void Box3DToolbar::notifyAttributeChanged(Inkscape::XML::Node &repr, GQuark, Inkscape::Util::ptr_shared, Inkscape::Util::ptr_shared)
{
    // quit if run by the attr_changed or selection changed listener
    if (_freeze) {
        return;
    }

    // set freeze so that it can be caught in box3d_angle_z_value_changed() (to avoid calling
    // SPDocumentUndo::maybeDone() when the document is undo insensitive)
    _freeze = true;

    // TODO: Only update the appropriate part of the toolbar
//    if (!strcmp(name, "inkscape:vp_z")) {
        resync_toolbar(&repr);
//    }

    Persp3D *persp = Persp3D::get_from_repr(&repr);
    if (persp) {
        persp->update_box_reprs();
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
