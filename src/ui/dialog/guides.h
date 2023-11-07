// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author:
 *   Andrius R. <knutux@gmail.com>
 *   Johan Engelen
 *
 * Copyright (C) 2006-2007 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_DIALOG_GUIDELINE_H
#define INKSCAPE_DIALOG_GUIDELINE_H

#include <gtkmm/checkbutton.h>
#include <gtkmm/colorbutton.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/window.h>

#include <2geom/point.h>

#include "ui/widget/unit-menu.h"
#include "ui/widget/scalar-unit.h"
#include "ui/widget/entry.h"

class SPGuide;
class SPDesktop;

namespace Inkscape::UI::Widget { class UnitMenu; }

namespace Inkscape::UI::Dialog {

/**
 * Dialog for modifying guidelines.
 */
class GuidelinePropertiesDialog final : public Gtk::Window
{
public:
    static void showDialog(SPGuide *guide, SPDesktop *desktop);

private:
    GuidelinePropertiesDialog(SPGuide *guide, SPDesktop *desktop);
    ~GuidelinePropertiesDialog() override;

    void _setup();

    void _onOK();
    void _onOKimpl();
    void _onDelete();
    void _onDuplicate();

    void _response(gint response);
    void _modeChanged();

    SPDesktop *_desktop;
    SPGuide *_guide;

    Gtk::Grid   _layout_table;
    Gtk::Label  _label_name;
    Gtk::Label  _label_descr;
    Gtk::CheckButton _locked_toggle;
    Gtk::CheckButton _relative_toggle;
    Inkscape::UI::Widget::UnitMenu _unit_menu;
    Inkscape::UI::Widget::ScalarUnit _spin_button_x;
    Inkscape::UI::Widget::ScalarUnit _spin_button_y;
    Inkscape::UI::Widget::Entry _label_entry;
    Gtk::ColorButton _color;

    Inkscape::UI::Widget::ScalarUnit _spin_angle;

    bool _mode = true;
    Geom::Point _oldpos;
    double _oldangle = 0;
};

} // namespace Inkscape::UI::Dialog

#endif // INKSCAPE_DIALOG_GUIDELINE_H

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
