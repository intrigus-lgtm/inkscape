// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_TWEAK_TOOLBAR_H
#define SEEN_TWEAK_TOOLBAR_H

/**
 * @file
 * Tweak aux toolbar
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
class ToggleButton;
} // namespace Gtk

class SPDesktop;

namespace Inkscape {
namespace UI {
namespace Widget {
class SpinButton;
}

namespace Toolbar {

class TweakToolbar final : public Toolbar
{
public:
    TweakToolbar(SPDesktop *desktop);
    ~TweakToolbar() override;

    void set_mode(int mode);

private:
    using ValueChangedMemFun = void (TweakToolbar::*)();
    Glib::RefPtr<Gtk::Builder> _builder;
    std::vector<Gtk::ToggleButton *> _mode_buttons;

    UI::Widget::SpinButton &_width_item;
    UI::Widget::SpinButton &_force_item;
    Gtk::Box &_fidelity_box;
    UI::Widget::SpinButton &_fidelity_item;

    Gtk::ToggleButton &_pressure_btn;

    Gtk::Box &_channels_box;
    Gtk::ToggleButton &_doh_btn;
    Gtk::ToggleButton &_dos_btn;
    Gtk::ToggleButton &_dol_btn;
    Gtk::ToggleButton &_doo_btn;

    void setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name, double default_value,
                                   ValueChangedMemFun const value_changed_mem_fun);
    void width_value_changed();
    void force_value_changed();
    void mode_changed(int mode);
    void fidelity_value_changed();
    void pressure_state_changed();
    void toggle_doh();
    void toggle_dos();
    void toggle_dol();
    void toggle_doo();
};

} // namespace Toolbar
}
}

#endif /* !SEEN_SELECT_TOOLBAR_H */
