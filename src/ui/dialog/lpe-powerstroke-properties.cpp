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

#include <boost/lexical_cast.hpp>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <sigc++/adaptors/bind.h>
#include <sigc++/adaptors/hide.h>
#include <sigc++/functors/mem_fun.h>

#include "desktop.h"
#include "document-undo.h"
#include "inkscape.h"
#include "layer-manager.h"
#include "selection-chemistry.h"
#include "ui/pack.h"

namespace Inkscape::UI::Dialog {

PowerstrokePropertiesDialog::PowerstrokePropertiesDialog()
    : _knotpoint(nullptr),
      _position_visible(false),
      _close_button(_("_Cancel"), true)
{
    Gtk::Box *mainVBox = get_content_area();

    _layout_table.set_row_spacing(4);
    _layout_table.set_column_spacing(4);

    // Layer name widgets
    _powerstroke_position_entry.set_activates_default(true);
    _powerstroke_position_entry.set_digits(4);
    _powerstroke_position_entry.set_increments(1,1);
    _powerstroke_position_entry.set_range(-SCALARPARAM_G_MAXDOUBLE, SCALARPARAM_G_MAXDOUBLE);
    _powerstroke_position_entry.set_hexpand();
    _powerstroke_position_label.set_label(_("Position:"));
    _powerstroke_position_label.set_halign(Gtk::Align::END);
    _powerstroke_position_label.set_valign(Gtk::Align::CENTER);

    _powerstroke_width_entry.set_activates_default(true);
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

    UI::pack_start(*mainVBox, _layout_table, true, true, 4);

    // Buttons
    _close_button.set_can_default();

    _apply_button.set_use_underline(true);
    _apply_button.set_can_default();

    _close_button.signal_clicked()
        .connect(sigc::mem_fun(*this, &PowerstrokePropertiesDialog::_close));
    _apply_button.signal_clicked()
        .connect(sigc::mem_fun(*this, &PowerstrokePropertiesDialog::_apply));

    signal_delete_event().connect(
        sigc::bind_return(
            sigc::hide(sigc::mem_fun(*this, &PowerstrokePropertiesDialog::_close)),
            true
        )
    );

    add_action_widget(_close_button, Gtk::ResponseType::CLOSE);
    add_action_widget(_apply_button, Gtk::ResponseType::APPLY);

    _apply_button.grab_default();

    show_all_children();

    set_focus(_powerstroke_width_entry);
}

void PowerstrokePropertiesDialog::showDialog(SPDesktop *desktop, Geom::Point knotpoint, const Inkscape::LivePathEffect::PowerStrokePointArrayParamKnotHolderEntity *pt)
{
    PowerstrokePropertiesDialog *dialog = new PowerstrokePropertiesDialog();

    dialog->_setKnotPoint(knotpoint);
    dialog->_setPt(pt);

    dialog->set_title(_("Modify Node Position"));
    dialog->_apply_button.set_label(_("_Move"));

    dialog->set_modal(true);
    desktop->setWindowTransient (dialog->gobj());
    dialog->property_destroy_with_parent() = true;

    dialog->set_visible(true);
    dialog->present();
}

void
PowerstrokePropertiesDialog::_apply()
{
    double d_pos   = _powerstroke_position_entry.get_value();
    double d_width = _powerstroke_width_entry.get_value();
    _knotpoint->knot_set_offset(Geom::Point(d_pos, d_width));
    _close();
}

void
PowerstrokePropertiesDialog::_close()
{
    destroy_();
    Glib::signal_idle().connect([this] { delete this; return false; });
}

void PowerstrokePropertiesDialog::_setKnotPoint(Geom::Point knotpoint)
{
    _powerstroke_position_entry.set_value(knotpoint.x());
    _powerstroke_width_entry.set_value(knotpoint.y());
}

void PowerstrokePropertiesDialog::_setPt(const Inkscape::LivePathEffect::PowerStrokePointArrayParamKnotHolderEntity *pt)
{
    _knotpoint = const_cast<Inkscape::LivePathEffect::PowerStrokePointArrayParamKnotHolderEntity *>(pt);
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
