// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Dialog for editing power strokes.
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

#include "lpe-powerstroke-properties.h"

#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <sigc++/adaptors/bind.h>
#include <sigc++/adaptors/hide.h>
#include <sigc++/functors/mem_fun.h>

#include "desktop.h"

namespace Inkscape::UI::Dialog {

PowerstrokePropertiesDialog::PowerstrokePropertiesDialog()
    : _mainbox(Gtk::Orientation::VERTICAL)
    , _close_button(_("_Cancel"), true)
{
    set_name("PowerstrokePropertiesDialog");

    set_child(_mainbox);
    _mainbox.set_margin(2);
    _mainbox.set_spacing(4);

    _layout_table.set_row_spacing(4);
    _layout_table.set_column_spacing(4);

    // Layer name widgets
    _powerstroke_position_entry.set_activates_default();
    _powerstroke_position_entry.set_digits(4);
    _powerstroke_position_entry.set_increments(1,1);
    _powerstroke_position_entry.set_range(-SCALARPARAM_G_MAXDOUBLE, SCALARPARAM_G_MAXDOUBLE);
    _powerstroke_position_entry.set_hexpand();
    _powerstroke_position_label.set_label(_("Position:"));
    _powerstroke_position_label.set_halign(Gtk::Align::END);
    _powerstroke_position_label.set_valign(Gtk::Align::CENTER);

    _powerstroke_width_entry.set_activates_default();
    _powerstroke_width_entry.set_digits(4);
    _powerstroke_width_entry.set_increments(1,1);
    _powerstroke_width_entry.set_range(-SCALARPARAM_G_MAXDOUBLE, SCALARPARAM_G_MAXDOUBLE);
    _powerstroke_width_entry.set_hexpand();
    _powerstroke_width_label.set_label(_("Width:"));
    _powerstroke_width_label.set_halign(Gtk::Align::END);
    _powerstroke_width_label.set_valign(Gtk::Align::CENTER);

    _layout_table.attach(_powerstroke_position_label,0,0,1,1);
    _layout_table.attach(_powerstroke_position_entry,1,0,1,1);
    _layout_table.attach(_powerstroke_width_label,   0,1,1,1);
    _layout_table.attach(_powerstroke_width_entry,   1,1,1,1);

    _layout_table.set_expand();
    _mainbox.append(_layout_table);

    // Buttons
    _close_button.set_receives_default();

    _apply_button.set_use_underline(true);
    _apply_button.set_receives_default();

    _close_button.signal_clicked().connect([this] { destroy(); });
    _apply_button.signal_clicked().connect([this] { _apply(); });

    _mainbox.append(_buttonbox);
    _buttonbox.set_halign(Gtk::Align::END);
    _buttonbox.set_homogeneous();
    _buttonbox.set_spacing(4);

    _buttonbox.append(_close_button);
    _buttonbox.append(_apply_button);

    set_default_widget(_apply_button);

    set_focus(_powerstroke_width_entry);
}

void PowerstrokePropertiesDialog::showDialog(SPDesktop *desktop, Geom::Point const &knotpoint, LivePathEffect::PowerStrokePointArrayParamKnotHolderEntity *knot)
{
    auto dialog = Gtk::manage(new PowerstrokePropertiesDialog());

    dialog->_setKnotPoint(knotpoint);
    dialog->_knotpoint = knot;

    dialog->set_title(_("Modify Node Position"));
    dialog->_apply_button.set_label(_("_Move"));

    dialog->set_modal(true);
    desktop->setWindowTransient (dialog->gobj());
    dialog->property_destroy_with_parent() = true;

    dialog->present();
}

void PowerstrokePropertiesDialog::_apply()
{
    double d_pos   = _powerstroke_position_entry.get_value();
    double d_width = _powerstroke_width_entry.get_value();
    _knotpoint->knot_set_offset({d_pos, d_width});
    destroy();
}

void PowerstrokePropertiesDialog::_setKnotPoint(Geom::Point const &knotpoint)
{
    _powerstroke_position_entry.set_value(knotpoint.x());
    _powerstroke_width_entry.set_value(knotpoint.y());
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
