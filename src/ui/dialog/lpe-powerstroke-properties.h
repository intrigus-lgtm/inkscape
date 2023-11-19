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

#ifndef INKSCAPE_DIALOG_POWERSTROKE_PROPERTIES_H
#define INKSCAPE_DIALOG_POWERSTROKE_PROPERTIES_H

#include <2geom/point.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/window.h>

#include "live_effects/parameter/powerstrokepointarray.h"

class SPDesktop;

namespace Inkscape::UI::Dialog {

class PowerstrokePropertiesDialog : public Gtk::Window
{
public:
    static void showDialog(SPDesktop *desktop, Geom::Point const &knotpoint, LivePathEffect::PowerStrokePointArrayParamKnotHolderEntity *knot);

protected:
    PowerstrokePropertiesDialog();

    LivePathEffect::PowerStrokePointArrayParamKnotHolderEntity *_knotpoint = nullptr;

    Gtk::Box _mainbox;
    Gtk::Box _buttonbox;

    Gtk::Label        _powerstroke_position_label;
    Gtk::SpinButton   _powerstroke_position_entry;
    Gtk::Label        _powerstroke_width_label;
    Gtk::SpinButton   _powerstroke_width_entry;
    Gtk::Grid         _layout_table;
    bool              _position_visible = false;

    Gtk::Button       _close_button;
    Gtk::Button       _apply_button;

    void _apply();

    void _setKnotPoint(Geom::Point const &knotpoint);
    void _prepareLabelRenderer(Gtk::TreeModel::const_iterator const &row);

    friend class LivePathEffect::PowerStrokePointArrayParamKnotHolderEntity;
};

} // namespace Inkscape::UI::Dialog

#endif // INKSCAPE_DIALOG_POWERSTROKE_PROPERTIES_H

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
