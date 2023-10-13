// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Dialog for moving knots. Only used by Measure Tool.
 */

/* Author:
 *   Bryce W. Harrington <bryce@bryceharrington.com>
 *   Andrius R. <knutux@gmail.com>
 *   Abhishek Sharma
 *
 * Copyright (C) 2004 Bryce Harrington
 * Copyright (C) 2006 Andrius R.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "knot-properties.h"

#include <boost/lexical_cast.hpp>
#include <glibmm/i18n.h>
#include <glibmm/main.h>

#include "desktop.h"
#include "ui/knot/knot.h"
#include "ui/pack.h"
#include "util/units.h"

namespace Inkscape::UI::Dialog {

KnotPropertiesDialog::KnotPropertiesDialog()
    : _knotpoint(nullptr),
      _position_visible(false),
      _close_button(_("_Close"), true)
{
    Gtk::Box *mainVBox = get_content_area();

    _layout_table.set_row_spacing(4);
    _layout_table.set_column_spacing(4);
    _unit_name = "";

    // Layer name widgets
    _knot_x_entry.set_activates_default(true);
    _knot_x_entry.set_digits(4);
    _knot_x_entry.set_increments(1,1);
    _knot_x_entry.set_range(-G_MAXDOUBLE, G_MAXDOUBLE);
    _knot_x_entry.set_hexpand();
    _knot_x_label.set_label(_("Position X:"));
    _knot_x_label.set_halign(Gtk::Align::END);
    _knot_x_label.set_valign(Gtk::Align::CENTER);

    _knot_y_entry.set_activates_default(true);
    _knot_y_entry.set_digits(4);
    _knot_y_entry.set_increments(1,1);
    _knot_y_entry.set_range(-G_MAXDOUBLE, G_MAXDOUBLE);
    _knot_y_entry.set_hexpand();
    _knot_y_label.set_label(_("Position Y:"));
    _knot_y_label.set_halign(Gtk::Align::END);
    _knot_y_label.set_valign(Gtk::Align::CENTER);

    _layout_table.attach(_knot_x_label, 0, 0, 1, 1);
    _layout_table.attach(_knot_x_entry, 1, 0, 1, 1);

    _layout_table.attach(_knot_y_label, 0, 1, 1, 1);
    _layout_table.attach(_knot_y_entry, 1, 1, 1, 1);

    UI::pack_start(*mainVBox, _layout_table, true, true, 4);

    // Buttons
    _close_button.set_can_default();

    _apply_button.set_use_underline(true);
    _apply_button.set_can_default();

    _close_button.signal_clicked()
        .connect(sigc::mem_fun(*this, &KnotPropertiesDialog::_close));
    _apply_button.signal_clicked()
        .connect(sigc::mem_fun(*this, &KnotPropertiesDialog::_apply));

    signal_delete_event().connect(
        sigc::bind_return(
            sigc::hide(sigc::mem_fun(*this, &KnotPropertiesDialog::_close)),
            true
        )
    );
    add_action_widget(_close_button, Gtk::ResponseType::CLOSE);
    add_action_widget(_apply_button, Gtk::ResponseType::APPLY);

    _apply_button.grab_default();

    show_all_children();

    set_focus(_knot_y_entry);
}

void KnotPropertiesDialog::showDialog(SPDesktop *desktop, const SPKnot *pt, Glib::ustring const unit_name)
{
    KnotPropertiesDialog *dialog = new KnotPropertiesDialog();
    dialog->_setKnotPoint(pt->position(), unit_name);
    dialog->_setPt(pt);

    dialog->set_title(_("Modify Knot Position"));
    dialog->_apply_button.set_label(_("_Move"));

    dialog->set_modal(true);
    desktop->setWindowTransient (dialog->gobj());
    dialog->property_destroy_with_parent() = true;

    dialog->set_visible(true);
    dialog->present();
}

void
KnotPropertiesDialog::_apply()
{
    double d_x   = Inkscape::Util::Quantity::convert(_knot_x_entry.get_value(), _unit_name, "px");
    double d_y =  Inkscape::Util::Quantity::convert(_knot_y_entry.get_value(), _unit_name, "px");
    _knotpoint->moveto(Geom::Point(d_x, d_y));
    _knotpoint->moved_signal.emit(_knotpoint, _knotpoint->position(), 0);
    _close();
}

void
KnotPropertiesDialog::_close()
{
    destroy_();
    Glib::signal_idle().connect(
        sigc::bind_return(
            sigc::bind(sigc::ptr_fun<void*, void>(&::operator delete), this),
            false 
        )
    );
}

void KnotPropertiesDialog::_setKnotPoint(Geom::Point knotpoint, Glib::ustring const unit_name)
{
    _unit_name = unit_name;
    _knot_x_entry.set_value( Inkscape::Util::Quantity::convert(knotpoint.x(), "px", _unit_name));
    _knot_y_entry.set_value( Inkscape::Util::Quantity::convert(knotpoint.y(), "px", _unit_name));
    _knot_x_label.set_label(g_strdup_printf(_("Position X (%s):"), _unit_name.c_str()));
    _knot_y_label.set_label(g_strdup_printf(_("Position Y (%s):"), _unit_name.c_str()));
}

void KnotPropertiesDialog::_setPt(const SPKnot *pt)
{
	_knotpoint = const_cast<SPKnot *>(pt);
}

} // namespace Inkscape::UI::Dialog

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
