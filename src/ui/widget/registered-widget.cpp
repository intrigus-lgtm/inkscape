// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Johan Engelen <j.b.c.engelen@utwente.nl>
 *   bulia byak <buliabyak@users.sf.net>
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Jon Phillips <jon@rejon.org>
 *   Ralf Stephan <ralf@ark.in-berlin.de> (Gtkmm)
 *   Abhishek Sharma
 *
 * Copyright (C) 2000 - 2007 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "registered-widget.h"

#include <glibmm/i18n.h>
#include <gtkmm/label.h>

#include "object/sp-root.h"
#include "svg/svg-color.h"
#include "svg/stringstream.h"
#include "util/safe-printf.h"

namespace Inkscape::UI::Widget {

/*#########################################
 * Registered CHECKBUTTON
 */

RegisteredCheckButton::RegisteredCheckButton(Glib::ustring const &label, Glib::ustring const &tip,
                                             Glib::ustring const &key, Registry &wr, bool right,
                                             Inkscape::XML::Node * const repr_in, SPDocument * const doc_in,
                                             char const * const active_str, char const * const inactive_str)
    : RegisteredWidget<Gtk::CheckButton>()
    , _active_str(active_str)
    , _inactive_str(inactive_str)
{
    init_parent(key, wr, repr_in, doc_in);

    setProgrammatically = false;

    set_tooltip_text(tip);

    auto const l = Gtk::make_managed<Gtk::Label>();
    l->set_markup(label);
    l->set_use_underline(true);
    set_child(*l);

    set_halign(right ? Gtk::Align::END : Gtk::Align::START);
    set_valign(Gtk::Align::CENTER);
}

void
RegisteredCheckButton::setActive(bool b)
{
    setProgrammatically = true;

    set_active(b);

    // The subordinate button is greyed out if the main button is unchecked
    for (auto const subordinate: _subordinate_widgets) {
        subordinate->set_sensitive(b);
    }

    setProgrammatically = false;
}

void
RegisteredCheckButton::on_toggled()
{
    if (setProgrammatically) {
        setProgrammatically = false;
        return;
    }

    if (_wr->isUpdating())
        return;

    _wr->setUpdating(true);

    auto const active = get_active();

    write_to_xml(active ? _active_str : _inactive_str);

    // The subordinate button is greyed out if the main button is unchecked
    for (auto const subordinate: _subordinate_widgets) {
        subordinate->set_sensitive(active);
    }

    _wr->setUpdating(false);
}

/*#########################################
 * Registered TOGGLEBUTTON
 */

RegisteredToggleButton::RegisteredToggleButton(
    Glib::ustring const &/*label*/,
    Glib::ustring const &tip, Glib::ustring const &key, Registry &wr, bool right,
    Inkscape::XML::Node * const repr_in, SPDocument * const doc_in,
    char const * const icon_active, char const * const icon_inactive)
: RegisteredWidget<Gtk::ToggleButton>()
{
    init_parent(key, wr, repr_in, doc_in);

    setProgrammatically = false;

    set_tooltip_text(tip);

    set_halign(right ? Gtk::Align::END : Gtk::Align::START);
    set_valign(Gtk::Align::CENTER);
}

void
RegisteredToggleButton::setActive(bool b)
{
    setProgrammatically = true;

    set_active(b);

    //The subordinate button is greyed out if the main button is untoggled
    for (auto const subordinate: _subordinate_widgets) {
        subordinate->set_sensitive(b);
    }

    setProgrammatically = false;
}

void
RegisteredToggleButton::on_toggled()
{
    if (setProgrammatically) {
        setProgrammatically = false;
        return;
    }

    if (_wr->isUpdating())
        return;

    _wr->setUpdating(true);

    auto const active = get_active();
    write_to_xml(active ? "true" : "false");

    //The subordinate button is greyed out if the main button is untoggled
    for (auto const subordinate: _subordinate_widgets) {
        subordinate->set_sensitive(active);
    }

    _wr->setUpdating(false);
}

/*#########################################
 * Registered UNITMENU
 */

RegisteredUnitMenu::RegisteredUnitMenu(Glib::ustring const &label, Glib::ustring const &key,
                                       Registry &wr,
                                       Inkscape::XML::Node * const repr_in, SPDocument * const doc_in)
    :  RegisteredWidget<Labelled>{label, Glib::ustring{} /*tooltip*/, new UnitMenu()}
{
    init_parent(key, wr, repr_in, doc_in);

    getUnitMenu()->setUnitType(UNIT_TYPE_LINEAR);
    _changed_connection = getUnitMenu()->signal_changed().connect(sigc::mem_fun (*this, &RegisteredUnitMenu::on_changed));
}

void
RegisteredUnitMenu::setUnit(Glib::ustring unit)
{
    getUnitMenu()->setUnit(unit);
}

void
RegisteredUnitMenu::on_changed()
{
    if (_wr->isUpdating())
        return;

    Inkscape::SVGOStringStream os;
    os << getUnitMenu()->getUnitAbbr();

    _wr->setUpdating(true);

    write_to_xml(os.str().c_str());

    _wr->setUpdating(false);
}

/*#########################################
 * Registered SCALARUNIT
 */

RegisteredScalarUnit::RegisteredScalarUnit(Glib::ustring const &label, Glib::ustring const &tip,
                                           Glib::ustring const &key,
                                           RegisteredUnitMenu &rum, Registry &wr,
                                           Inkscape::XML::Node * const repr_in, SPDocument * const doc_in,
                                           RSU_UserUnits const user_units)
    : RegisteredWidget<ScalarUnit>(label, tip, UNIT_TYPE_LINEAR, Glib::ustring{}, rum.getUnitMenu()),
      _um(nullptr)
{
    init_parent(key, wr, repr_in, doc_in);

    setProgrammatically = false;

    initScalar(-1e6, 1e6);
    setUnit(rum.getUnitMenu()->getUnitAbbr());
    setDigits(2);
    _um = rum.getUnitMenu();
    _user_units = user_units;
    _value_changed_connection = signal_value_changed().connect(sigc::mem_fun(*this, &RegisteredScalarUnit::on_value_changed));
}

void
RegisteredScalarUnit::on_value_changed()
{
    if (setProgrammatically) {
        setProgrammatically = false;
        return;
    }

    if (_wr->isUpdating())
        return;

    _wr->setUpdating(true);

    Inkscape::SVGOStringStream os;
    if (_user_units != RSU_none) {
        // Output length in 'user units', taking into account scale in 'x' or 'y'.
        double scale = 1.0;
        if (doc) {
            SPRoot *root = doc->getRoot();
            if (root->viewBox_set) {
                // check to see if scaling is uniform
                if(Geom::are_near((root->viewBox.width() * root->height.computed) / (root->width.computed * root->viewBox.height()), 1.0, Geom::EPSILON)) {
                    scale = (root->viewBox.width() / root->width.computed + root->viewBox.height() / root->height.computed)/2.0;
                } else if (_user_units == RSU_x) { 
                    scale = root->viewBox.width() / root->width.computed;
                } else {
                    scale = root->viewBox.height() / root->height.computed;
                }
            }
        }
        os << getValue("px") * scale;
    } else {
        // Output using unit identifiers.
        os << getValue("");
        if (_um)
            os << _um->getUnitAbbr();
    }

    write_to_xml(os.str().c_str());
    _wr->setUpdating(false);
}

/*#########################################
 * Registered SCALAR
 */

RegisteredScalar::RegisteredScalar(Glib::ustring const &label, Glib::ustring const &tip,
                                   Glib::ustring const &key, Registry &wr,
                                   Inkscape::XML::Node * const repr_in,
                                   SPDocument * const doc_in)
    : RegisteredWidget<Scalar>(label, tip)
{
    init_parent(key, wr, repr_in, doc_in);

    setProgrammatically = false;
    setRange(-1e6, 1e6);
    setDigits(2);
    setIncrements(0.1, 1.0);
    _value_changed_connection = signal_value_changed().connect(sigc::mem_fun(*this, &RegisteredScalar::on_value_changed));
}

void
RegisteredScalar::on_value_changed()
{
    if (setProgrammatically) {
        setProgrammatically = false;
        return;
    }
    if (_wr->isUpdating()) {
        return;
    }
    _wr->setUpdating(true);

    Inkscape::SVGOStringStream os;
    //Force exact 0 if decimals over to 6
    double val = getValue() < 1e-6 && getValue() > -1e-6?0.0:getValue();
    os << val;

    //TODO: Test is ok remove this sensitives
    //also removed in registered text and in registered random
    //set_sensitive(false);

    write_to_xml(os.str().c_str());

    //set_sensitive(true);

    _wr->setUpdating(false);
}

/*#########################################
 * Registered TEXT
 */

RegisteredText::RegisteredText(Glib::ustring const &label, Glib::ustring const &tip,
                               Glib::ustring const &key, Registry &wr,
                               Inkscape::XML::Node * const repr_in, SPDocument * const doc_in)
    : RegisteredWidget<Text>(label, tip)
{
    init_parent(key, wr, repr_in, doc_in);

    setProgrammatically = false;
    _activate_connection = signal_activate().connect (sigc::mem_fun (*this, &RegisteredText::on_activate));
}

void
RegisteredText::on_activate()
{
    if (setProgrammatically) {
        setProgrammatically = false;
        return;
    }

    if (_wr->isUpdating()) {
        return;
    }
    _wr->setUpdating(true);
    Glib::ustring str(getText());
    Inkscape::SVGOStringStream os;
    os << str;
    write_to_xml(os.str().c_str());
    _wr->setUpdating(false);
}

/*#########################################
 * Registered COLORPICKER
 */

RegisteredColorPicker::RegisteredColorPicker(Glib::ustring const &label,
                                             Glib::ustring const &title,
                                             Glib::ustring const &tip,
                                             Glib::ustring const &ckey,
                                             Glib::ustring const &akey,
                                             Registry &wr,
                                             Inkscape::XML::Node *repr_in,
                                             SPDocument *doc_in)
    : RegisteredWidget<LabelledColorPicker>{label, title, tip, 0u, true}
{
    init_parent("", wr, repr_in, doc_in);

    _ckey = ckey;
    _akey = akey;
    _changed_connection = connectChanged(sigc::mem_fun(*this, &RegisteredColorPicker::on_changed));
}

void
RegisteredColorPicker::setRgba32(std::uint32_t const rgba)
{
    LabelledColorPicker::setRgba32(rgba);
}

void
RegisteredColorPicker::closeWindow()
{
    LabelledColorPicker::closeWindow();
}

void
RegisteredColorPicker::on_changed(std::uint32_t const rgba)
{
    if (_wr->isUpdating())
        return;

    _wr->setUpdating(true);

    // Use local repr here. When repr is specified, use that one, but
    // if repr==NULL, get the repr of namedview of active desktop.
    Inkscape::XML::Node *local_repr = repr;
    SPDocument *local_doc = doc;
    if (!local_repr) {
        SPDesktop *dt = _wr->desktop();
        if (!dt) {
            _wr->setUpdating(false);
            return;
        }
        local_repr = dt->getNamedView()->getRepr();
        local_doc = dt->getDocument();
    }
    gchar c[32];
    if (_akey == _ckey + "_opacity_LPE") { //For LPE parameter we want stored with alpha
        safeprintf(c, "#%08x", rgba);
    } else {
        sp_svg_write_color(c, sizeof(c), rgba);
    }
    {
        DocumentUndo::ScopedInsensitive _no_undo(local_doc);
        local_repr->setAttribute(_ckey, c);
        local_repr->setAttributeCssDouble(_akey.c_str(), (rgba & 0xff) / 255.0);
    }
    local_doc->setModifiedSinceSave();
    DocumentUndo::done(local_doc, "registered-widget.cpp: RegisteredColorPicker::on_changed", ""); // TODO Fix description.

    _wr->setUpdating(false);
}

/*#########################################
 * Registered INTEGER
 */

RegisteredInteger::RegisteredInteger(Glib::ustring const &label, Glib::ustring const &tip,
                                     Glib::ustring const &key, Registry &wr,
                                     Inkscape::XML::Node *repr_in, SPDocument *doc_in)
    : RegisteredWidget<Scalar>{label, tip, 0u},
      setProgrammatically(false)
{
    init_parent(key, wr, repr_in, doc_in);

    setRange(0, 1e6);
    setDigits(0);
    setIncrements(1, 10);

    _changed_connection = signal_value_changed().connect(sigc::mem_fun(*this, &RegisteredInteger::on_value_changed));
}

void
RegisteredInteger::on_value_changed()
{
    if (setProgrammatically) {
        setProgrammatically = false;
        return;
    }

    if (_wr->isUpdating())
        return;

    _wr->setUpdating(true);

    Inkscape::SVGOStringStream os;
    os << getValue();
    write_to_xml(os.str().c_str());

    _wr->setUpdating(false);
}

/*#########################################
 * Registered TRANSFORMEDPOINT
 */

RegisteredTransformedPoint::RegisteredTransformedPoint(Glib::ustring const &label, Glib::ustring const &tip,
                                                       Glib::ustring const &key, Registry &wr,
                                                       Inkscape::XML::Node * const repr_in,
                                                       SPDocument * const doc_in)
    : RegisteredWidget<Point>(label, tip),
      to_svg(Geom::identity())
{
    init_parent(key, wr, repr_in, doc_in);

    setRange(-1e6, 1e6);
    setDigits(2);
    setIncrements(0.1, 1.0);
    _value_x_changed_connection = signal_x_value_changed().connect(sigc::mem_fun(*this, &RegisteredTransformedPoint::on_value_changed));
    _value_y_changed_connection = signal_y_value_changed().connect(sigc::mem_fun(*this, &RegisteredTransformedPoint::on_value_changed));
}

void
RegisteredTransformedPoint::setValue(Geom::Point const & p)
{
    Geom::Point new_p = p * to_svg.inverse();
    Point::setValue(new_p);  // the Point widget should display things in canvas coordinates
}

void
RegisteredTransformedPoint::setTransform(Geom::Affine const & canvas_to_svg)
{
    // check if matrix is singular / has inverse
    if (!canvas_to_svg.isSingular()) {
        to_svg = canvas_to_svg;
    } else {
        // set back to default
        to_svg = Geom::identity();
    }
}

void
RegisteredTransformedPoint::on_value_changed()
{
    if (setProgrammatically()) {
        clearProgrammatically();
        return;
    }

    if (_wr->isUpdating())
        return;

    _wr->setUpdating(true);

    Geom::Point pos = getValue() * to_svg;

    Inkscape::SVGOStringStream os;
    os << pos;

    write_to_xml(os.str().c_str());

    _wr->setUpdating(false);
}

/*#########################################
 * Registered TRANSFORMEDPOINT
 */

RegisteredVector::RegisteredVector(Glib::ustring const &label, Glib::ustring const &tip,
                                   Glib::ustring const &key, Registry &wr,
                                   Inkscape::XML::Node * const repr_in,
                                   SPDocument * const doc_in)
    : RegisteredWidget<Point>(label, tip),
      _polar_coords(false)
{
    init_parent(key, wr, repr_in, doc_in);

    setRange(-1e6, 1e6);
    setDigits(2);
    setIncrements(0.1, 1.0);
    _value_x_changed_connection = signal_x_value_changed().connect(sigc::mem_fun(*this, &RegisteredVector::on_value_changed));
    _value_y_changed_connection = signal_y_value_changed().connect(sigc::mem_fun(*this, &RegisteredVector::on_value_changed));
}

void
RegisteredVector::setValue(Geom::Point const & p)
{
    if (!_polar_coords) {
        Point::setValue(p);
    } else {
        Geom::Point polar;
        polar[Geom::X] = atan2(p) *180/M_PI;
        polar[Geom::Y] = p.length();
        Point::setValue(polar);
    }
}

void
RegisteredVector::setValue(Geom::Point const & p, Geom::Point const & origin)
{
    RegisteredVector::setValue(p);
    _origin = origin;
}

void RegisteredVector::setPolarCoords(bool polar_coords)
{
    _polar_coords = polar_coords;
    if (polar_coords) {
        xwidget.getLabel()->set_text(_("Angle:"));
        ywidget.getLabel()->set_text(_("Distance:"));
    } else {
        xwidget.getLabel()->set_text(_("X:"));
        ywidget.getLabel()->set_text(_("Y:"));
    }
}

void
RegisteredVector::on_value_changed()
{
    if (setProgrammatically()) {
        clearProgrammatically();
        return;
    }

    if (_wr->isUpdating())
        return;

    _wr->setUpdating(true);

    Geom::Point origin = _origin;
    Geom::Point vector = getValue();
    if (_polar_coords) {
        vector = Geom::Point::polar(vector[Geom::X]*M_PI/180, vector[Geom::Y]);
    }

    Inkscape::SVGOStringStream os;
    os << origin << " , " << vector;

    write_to_xml(os.str().c_str());

    _wr->setUpdating(false);
}

/*#########################################
 * Registered RANDOM
 */

RegisteredRandom::RegisteredRandom(Glib::ustring const &label, Glib::ustring const &tip,
                                   Glib::ustring const &key, Registry &wr,
                                   Inkscape::XML::Node * const repr_in,
                                   SPDocument * const doc_in)
    : RegisteredWidget<Random>(label, tip)
{
    init_parent(key, wr, repr_in, doc_in);

    setProgrammatically = false;
    setRange(-1e6, 1e6);
    setDigits(2);
    setIncrements(0.1, 1.0);
    _value_changed_connection = signal_value_changed().connect(sigc::mem_fun(*this, &RegisteredRandom::on_value_changed));
    _reseeded_connection = signal_reseeded.connect(sigc::mem_fun(*this, &RegisteredRandom::on_value_changed));
}

void
RegisteredRandom::setValue(double val, long startseed)
{
    Scalar::setValue(val);
    setStartSeed(startseed);
}

void
RegisteredRandom::on_value_changed()
{
    if (setProgrammatically) {
        setProgrammatically = false;
        return;
    }

    if (_wr->isUpdating()) {
        return;
    }
    _wr->setUpdating(true);

    Inkscape::SVGOStringStream os;
    //Force exact 0 if decimals over to 6
    double val = getValue() < 1e-6 && getValue() > -1e-6?0.0:getValue();
    os << val << ';' << getStartSeed();
    write_to_xml(os.str().c_str());
    _wr->setUpdating(false);
}

/*#########################################
 * Registered FONT-BUTTON
 */

RegisteredFontButton::RegisteredFontButton(Glib::ustring const &label, Glib::ustring const &tip,
                                           Glib::ustring const &key, Registry &wr,
                                           Inkscape::XML::Node * const repr_in,
                                           SPDocument * const doc_in)
    : RegisteredWidget<FontButton>(label, tip)
{
    init_parent(key, wr, repr_in, doc_in);
    _signal_font_set =  signal_font_value_changed().connect(sigc::mem_fun(*this, &RegisteredFontButton::on_value_changed));
}

void
RegisteredFontButton::setValue(Glib::ustring fontspec)
{
    FontButton::setValue(fontspec);
}

void
RegisteredFontButton::on_value_changed()
{

    if (_wr->isUpdating())
        return;

    _wr->setUpdating(true);

    Inkscape::SVGOStringStream os;
    os << getValue();
    write_to_xml(os.str().c_str());

    _wr->setUpdating(false);
}

} // namespace Inkscape::UI::Widget

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
