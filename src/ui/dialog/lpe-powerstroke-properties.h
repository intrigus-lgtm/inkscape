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
#include <gtkmm/button.h>
#include <gtkmm/dialog.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/treemodel.h>

#include "live_effects/parameter/powerstrokepointarray.h"

class SPDesktop;

namespace Inkscape::UI::Dialog {

class PowerstrokePropertiesDialog : public Gtk::Dialog {
public:
    PowerstrokePropertiesDialog();

    PowerstrokePropertiesDialog(PowerstrokePropertiesDialog const &) = delete;
    PowerstrokePropertiesDialog &operator=(PowerstrokePropertiesDialog const &) = delete;

    Glib::ustring     getName() const { return "LayerPropertiesDialog"; }

    static void showDialog(SPDesktop *desktop, Geom::Point knotpoint, const Inkscape::LivePathEffect::PowerStrokePointArrayParamKnotHolderEntity *pt);

protected:
    Inkscape::LivePathEffect::PowerStrokePointArrayParamKnotHolderEntity *_knotpoint;

    Gtk::Label        _powerstroke_position_label;
    Gtk::SpinButton   _powerstroke_position_entry;
    Gtk::Label        _powerstroke_width_label;
    Gtk::SpinButton   _powerstroke_width_entry;
    Gtk::Grid         _layout_table;
    bool              _position_visible;

    Gtk::Button       _close_button;
    Gtk::Button       _apply_button;

    static PowerstrokePropertiesDialog &_instance() {
        static PowerstrokePropertiesDialog instance;
        return instance;
    }

    void _setPt(const Inkscape::LivePathEffect::PowerStrokePointArrayParamKnotHolderEntity *pt);

    void _apply();
    void _close();

    void _setKnotPoint(Geom::Point knotpoint);
    void _prepareLabelRenderer(Gtk::TreeModel::const_iterator const &row);

    friend class Inkscape::LivePathEffect::PowerStrokePointArrayParamKnotHolderEntity;
};

} // namespace Inkscape::UI::Dialog

#endif //INKSCAPE_DIALOG_LAYER_PROPERTIES_H

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
