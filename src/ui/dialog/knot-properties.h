// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief
 */
/* Author:
 *   Bryce W. Harrington <bryce@bryceharrington.com>
 *
 * Copyright (C) 2004 Bryce Harrington
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_DIALOG_KNOT_PROPERTIES_H
#define INKSCAPE_DIALOG_KNOT_PROPERTIES_H

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/window.h>
#include <2geom/point.h>

#include "ui/tools/measure-tool.h"

class SPDesktop;

namespace Inkscape::UI::Dialog {

// Used in Measure tool to set ends of "ruler" (via Shift-click)."

class KnotPropertiesDialog final : public Gtk::Window
{
public:
    static void showDialog(SPDesktop *desktop, SPKnot *knot, Glib::ustring const &unit_name);

protected:
    KnotPropertiesDialog();

    SPKnot *_knotpoint = nullptr;

    Gtk::Box          _mainbox;
    Gtk::Box          _buttonbox;
    Gtk::Label        _knot_x_label;
    Gtk::SpinButton   _knot_x_entry;
    Gtk::Label        _knot_y_label;
    Gtk::SpinButton   _knot_y_entry;
    Gtk::Grid         _layout_table;
    bool _position_visible = false;

    Gtk::Button       _close_button;
    Gtk::Button       _apply_button;
    Glib::ustring _unit_name;

    static KnotPropertiesDialog &_instance() {
        static KnotPropertiesDialog instance;
        return instance;
    }

    void _apply();

    void _setKnotPoint(Geom::Point const &knotpoint, Glib::ustring const &unit_name);

    friend class Inkscape::UI::Tools::MeasureTool;
};

} // namespace Inkscape::UI::Dialog

#endif // INKSCAPE_DIALOG_KNOT_PROPERTIES_H

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
