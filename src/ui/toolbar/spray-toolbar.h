// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_SPRAY_TOOLBAR_H
#define SEEN_SPRAY_TOOLBAR_H

/**
 * @file
 * Spray aux toolbar
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
 * Copyright (C) 1999-2015 authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "toolbar.h"

namespace Gtk {
class Button;
class ToggleButton;
class Builder;
} // namespace Gtk

class SPDesktop;

namespace Inkscape {
namespace UI {
class SimplePrefPusher;

namespace Widget {
class SpinButton;
}

namespace Toolbar {

class SprayToolbar final : public Toolbar
{
public:
    SprayToolbar(SPDesktop *desktop);
    ~SprayToolbar() override;

    void set_mode(int mode);

private:
    using ValueChangedMemFun = void (SprayToolbar::*)();

    Glib::RefPtr<Gtk::Builder> _builder;

    std::vector<Gtk::ToggleButton *> _mode_buttons;

    UI::Widget::SpinButton &_width_item;
    UI::Widget::SpinButton &_population_item;

    Gtk::Box &_rotation_box;
    UI::Widget::SpinButton &_rotation_item;
    UI::Widget::SpinButton &_scale_item;
    Gtk::ToggleButton &_use_pressure_scale_btn;

    UI::Widget::SpinButton &_sd_item;
    UI::Widget::SpinButton &_mean_item;

    Gtk::ToggleButton &_over_no_transparent_btn;
    Gtk::ToggleButton &_over_transparent_btn;
    Gtk::ToggleButton &_pick_no_overlap_btn;
    Gtk::ToggleButton &_no_overlap_btn;
    Gtk::Box &_offset_box;
    UI::Widget::SpinButton &_offset_item;

    Gtk::ToggleButton &_picker_btn;
    Gtk::ToggleButton &_pick_fill_btn;
    Gtk::ToggleButton &_pick_stroke_btn;
    Gtk::ToggleButton &_pick_inverse_value_btn;
    Gtk::ToggleButton &_pick_center_btn;

    // TODO: Check if these can be moved to the constructor.
    std::unique_ptr<SimplePrefPusher> _use_pressure_width_pusher;
    std::unique_ptr<SimplePrefPusher> _use_pressure_population_pusher;

    void width_value_changed();
    void mean_value_changed();
    void standard_deviation_value_changed();
    void mode_changed(int mode);
    void init();
    void population_value_changed();
    void rotation_value_changed();
    void update_widgets();
    void scale_value_changed();
    void offset_value_changed();
    void on_pref_toggled(bool active, const Glib::ustring &path);
    void toggle_no_overlap();
    void toggle_pressure_scale();
    void toggle_picker();
    void setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name, double default_value,
                                   ValueChangedMemFun const value_changed_mem_fun);
};
}
}
}

#endif /* !SEEN_SELECT_TOOLBAR_H */
