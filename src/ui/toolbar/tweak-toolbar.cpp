// SPDX-License-Identifier: GPL-2.0-or-later
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

#include "tweak-toolbar.h"

#include <glibmm/i18n.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/togglebutton.h>

#include "desktop.h"
#include "ui/builder-utils.h"
#include "ui/tools/tweak-tool.h"
#include "ui/util.h"
#include "ui/widget/canvas.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/toolbar-menu-button.h"

namespace Inkscape::UI::Toolbar {

TweakToolbar::TweakToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    , _builder(create_builder("toolbar-tweak.ui"))
    , _width_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_width_item"))
    , _force_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_force_item"))
    , _fidelity_box(get_widget<Gtk::Box>(_builder, "_fidelity_box"))
    , _fidelity_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_fidelity_item"))
    , _pressure_btn(get_widget<Gtk::ToggleButton>(_builder, "_pressure_btn"))
    , _channels_box(get_widget<Gtk::Box>(_builder, "_channels_box"))
    , _doh_btn(get_widget<Gtk::ToggleButton>(_builder, "_doh_btn"))
    , _dos_btn(get_widget<Gtk::ToggleButton>(_builder, "_dos_btn"))
    , _dol_btn(get_widget<Gtk::ToggleButton>(_builder, "_dol_btn"))
    , _doo_btn(get_widget<Gtk::ToggleButton>(_builder, "_doo_btn"))
{
    _toolbar = &get_widget<Gtk::Box>(_builder, "tweak-toolbar");

    setup_derived_spin_button(_width_item, "width", 15, &TweakToolbar::width_value_changed);
    setup_derived_spin_button(_force_item, "force", 20, &TweakToolbar::force_value_changed);
    setup_derived_spin_button(_fidelity_item, "fidelity", 50, &TweakToolbar::fidelity_value_changed);

    // Configure mode buttons
    int btn_index = 0;
    for_each_child(get_widget<Gtk::Box>(_builder, "mode_buttons_box"), [&](Gtk::Widget &item){
        auto &btn = dynamic_cast<Gtk::ToggleButton &>(item);
        _mode_buttons.push_back(&btn);
        btn.signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &TweakToolbar::mode_changed), btn_index++));
        return ForEachResult::_continue;
    });

    auto prefs = Inkscape::Preferences::get();

    // Pressure button.
    _pressure_btn.signal_toggled().connect(sigc::mem_fun(*this, &TweakToolbar::pressure_state_changed));
    _pressure_btn.set_active(prefs->getBool("/tools/tweak/usepressure", true));

    // Set initial mode.
    int mode = prefs->getIntLimited("/tools/tweak/mode", 0, 0, _mode_buttons.size() - 1);
    _mode_buttons[mode]->set_active();

    // Configure channel buttons.
    // Translators: H, S, L, and O stands for:
    // Hue, Saturation, Lighting and Opacity respectively.
    _doh_btn.signal_toggled().connect(sigc::mem_fun(*this, &TweakToolbar::toggle_doh));
    _doh_btn.set_active(prefs->getBool("/tools/tweak/doh", true));
    _dos_btn.signal_toggled().connect(sigc::mem_fun(*this, &TweakToolbar::toggle_dos));
    _dos_btn.set_active(prefs->getBool("/tools/tweak/dos", true));
    _dol_btn.signal_toggled().connect(sigc::mem_fun(*this, &TweakToolbar::toggle_dol));
    _dol_btn.set_active(prefs->getBool("/tools/tweak/dol", true));
    _doo_btn.signal_toggled().connect(sigc::mem_fun(*this, &TweakToolbar::toggle_doo));
    _doo_btn.set_active(prefs->getBool("/tools/tweak/doo", true));

    // Fetch all the ToolbarMenuButtons at once from the UI file
    // Menu Button #1
    auto popover_box1 = &get_widget<Gtk::Box>(_builder, "popover_box1");
    auto menu_btn1 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn1");

    // Menu Button #2
    auto popover_box2 = &get_widget<Gtk::Box>(_builder, "popover_box2");
    auto menu_btn2 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn2");

    // Initialize all the ToolbarMenuButtons only after all the children of the
    // toolbar have been fetched. Otherwise, the children to be moved in the
    // popover will get mapped to a different position and it will probably
    // cause segfault.
    auto children = UI::get_children(*_toolbar);

    menu_btn1->init(1, "tag1", popover_box1, children);
    addCollapsibleButton(menu_btn1);
    menu_btn2->init(2, "tag2", popover_box2, children);
    addCollapsibleButton(menu_btn2);

    set_child(*_toolbar);

    // Elements must be hidden after being initially visible.
    if (mode == Inkscape::UI::Tools::TWEAK_MODE_COLORPAINT || mode == Inkscape::UI::Tools::TWEAK_MODE_COLORJITTER) {
        _fidelity_box.set_visible(false);
    } else {
        _channels_box.set_visible(false);
    }
}

TweakToolbar::~TweakToolbar() = default;

void TweakToolbar::setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name,
                                             double default_value, ValueChangedMemFun const value_changed_mem_fun)
{
    const Glib::ustring path = "/tools/tweak/" + name;
    auto const val = Preferences::get()->getDouble(path, default_value);

    auto adj = btn.get_adjustment();
    adj->set_value(val);
    adj->signal_value_changed().connect(sigc::mem_fun(*this, value_changed_mem_fun));

    btn.set_defocus_widget(_desktop->getCanvas());
}

void TweakToolbar::set_mode(int mode)
{
    _mode_buttons[mode]->set_active();
}

void TweakToolbar::width_value_changed()
{
    Preferences::get()->setDouble("/tools/tweak/width", _width_item.get_adjustment()->get_value() * 0.01);
}

void TweakToolbar::force_value_changed()
{
    Preferences::get()->setDouble("/tools/tweak/force", _force_item.get_adjustment()->get_value() * 0.01);
}

void TweakToolbar::mode_changed(int mode)
{
    Preferences::get()->setInt("/tools/tweak/mode", mode);

    bool flag = ((mode == Inkscape::UI::Tools::TWEAK_MODE_COLORPAINT) ||
                 (mode == Inkscape::UI::Tools::TWEAK_MODE_COLORJITTER));

    _channels_box.set_visible(flag);

    _fidelity_box.set_visible(!flag);
}

void TweakToolbar::fidelity_value_changed()
{
    Preferences::get()->setDouble("/tools/tweak/fidelity", _fidelity_item.get_adjustment()->get_value() * 0.01);
}

void TweakToolbar::pressure_state_changed()
{
    Preferences::get()->setBool("/tools/tweak/usepressure", _pressure_btn.get_active());
}

void TweakToolbar::toggle_doh()
{
    Preferences::get()->setBool("/tools/tweak/doh", _doh_btn.get_active());
}

void TweakToolbar::toggle_dos()
{
    Preferences::get()->setBool("/tools/tweak/dos", _dos_btn.get_active());
}

void TweakToolbar::toggle_dol()
{
    Preferences::get()->setBool("/tools/tweak/dol", _dol_btn.get_active());
}

void TweakToolbar::toggle_doo()
{
    Preferences::get()->setBool("/tools/tweak/doo", _doo_btn.get_active());
}

} // namespace Inkscape::UI::Toolbar

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
