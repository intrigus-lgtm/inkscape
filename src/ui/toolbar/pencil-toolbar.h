// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_PENCIL_TOOLBAR_H
#define SEEN_PENCIL_TOOLBAR_H

/**
 * @file
 * Pencil and pen toolbars
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

#include "toolbar.h"

namespace Gtk {
class Builder;
class Button;
class ToggleButton;
} // namespace Gtk

class SPDesktop;

namespace Inkscape {
namespace XML {
class Node;
}

namespace UI {
namespace Widget {
class SpinButton;
class ComboToolItem;
}

namespace Toolbar {

class PencilToolbar final : public Toolbar
{
public:
    PencilToolbar(SPDesktop *desktop, bool pencil_mode);
    ~PencilToolbar() override;

private:
    using ValueChangedMemFun = void (PencilToolbar::*)();

    Glib::RefPtr<Gtk::Builder> _builder;

    bool const _tool_is_pencil;
    std::vector<Gtk::ToggleButton *> _mode_buttons;
    Gtk::Button &_flatten_spiro_bspline_btn;

    Gtk::ToggleButton &_usepressure_btn;
    Gtk::Box &_minpressure_box;
    UI::Widget::SpinButton &_minpressure_item;
    Gtk::Box &_maxpressure_box;
    UI::Widget::SpinButton &_maxpressure_item;
    UI::Widget::ComboToolItem *_cap_item;
    UI::Widget::SpinButton &_tolerance_item;
    Gtk::ToggleButton &_simplify_btn;
    Gtk::Button &_flatten_simplify_btn;

    UI::Widget::ComboToolItem *_shape_item;
    Gtk::Box &_shapescale_box;
    UI::Widget::SpinButton &_shapescale_item;

    XML::Node *_repr = nullptr;
    bool _freeze = false;

    void add_powerstroke_cap();
    void add_shape_option();
    void setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name, double default_value,
                                   ValueChangedMemFun const value_changed_mem_fun);
    void hide_extra_widgets();
    void mode_changed(int mode);
    Glib::ustring const freehand_tool_name();
    void minpressure_value_changed();
    void maxpressure_value_changed();
    void shapewidth_value_changed();
    void use_pencil_pressure();
    void tolerance_value_changed();
    void change_shape(int shape);
    void update_width_value(int shape);
    void change_cap(int cap);
    void simplify_lpe();
    void simplify_flatten();
    void flatten_spiro_bspline();
};

} // namespace Toolbar
}
}

#endif /* !SEEN_PENCIL_TOOLBAR_H */
