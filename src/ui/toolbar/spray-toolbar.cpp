// SPDX-License-Identifier: GPL-2.0-or-later
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
 *   Jabiertxo Arraiza <jabier.arraiza@marker.es>
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Copyright (C) 2004 David Turner
 * Copyright (C) 2003 MenTaLguY
 * Copyright (C) 1999-2015 authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "spray-toolbar.h"

#include <glibmm/i18n.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/togglebutton.h>

#include "desktop.h"
#include "ui/builder-utils.h"
#include "ui/dialog/clonetiler.h"
#include "ui/dialog/dialog-base.h"
#include "ui/dialog/dialog-container.h"
#include "ui/simple-pref-pusher.h"
#include "ui/util.h"
#include "ui/widget/canvas.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/toolbar-menu-button.h"

// Disabled in 0.91 because of Bug #1274831 (crash, spraying an object
// with the mode: spray object in single path)
// Please enable again when working on 1.0
#define ENABLE_SPRAY_MODE_SINGLE_PATH

Inkscape::UI::Dialog::CloneTiler *get_clone_tiler_panel(SPDesktop *desktop)
{
    Inkscape::UI::Dialog::DialogBase *dialog = desktop->getContainer()->get_dialog("CloneTiler");
    if (!dialog) {
        desktop->getContainer()->new_dialog("CloneTiler");
        return dynamic_cast<Inkscape::UI::Dialog::CloneTiler *>(
            desktop->getContainer()->get_dialog("CloneTiler"));
    }
    return dynamic_cast<Inkscape::UI::Dialog::CloneTiler *>(dialog);
}

namespace Inkscape::UI::Toolbar {

SprayToolbar::SprayToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    , _builder(create_builder("toolbar-spray.ui"))
    , _width_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_width_item"))
    , _population_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_population_item"))
    , _rotation_box(get_widget<Gtk::Box>(_builder, "_rotation_box"))
    , _rotation_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_rotation_item"))
    , _scale_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_scale_item"))
    , _use_pressure_scale_btn(get_widget<Gtk::ToggleButton>(_builder, "_use_pressure_scale_btn"))
    , _sd_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_sd_item"))
    , _mean_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_mean_item"))
    , _over_no_transparent_btn(get_widget<Gtk::ToggleButton>(_builder, "_over_no_transparent_btn"))
    , _over_transparent_btn(get_widget<Gtk::ToggleButton>(_builder, "_over_transparent_btn"))
    , _pick_no_overlap_btn(get_widget<Gtk::ToggleButton>(_builder, "_pick_no_overlap_btn"))
    , _no_overlap_btn(get_widget<Gtk::ToggleButton>(_builder, "_no_overlap_btn"))
    , _offset_box(get_widget<Gtk::Box>(_builder, "_offset_box"))
    , _offset_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_offset_item"))
    , _picker_btn(get_widget<Gtk::ToggleButton>(_builder, "_picker_btn"))
    , _pick_fill_btn(get_widget<Gtk::ToggleButton>(_builder, "_pick_fill_btn"))
    , _pick_stroke_btn(get_widget<Gtk::ToggleButton>(_builder, "_pick_stroke_btn"))
    , _pick_inverse_value_btn(get_widget<Gtk::ToggleButton>(_builder, "_pick_inverse_value_btn"))
    , _pick_center_btn(get_widget<Gtk::ToggleButton>(_builder, "_pick_center_btn"))
{
    _toolbar = &get_widget<Gtk::Box>(_builder, "spray-toolbar");

    auto use_pressure_width_btn = &get_widget<Gtk::ToggleButton>(_builder, "use_pressure_width_btn");
    auto use_pressure_population_btn = &get_widget<Gtk::ToggleButton>(_builder, "use_pressure_population_btn");

    // Setup the spin buttons.
    setup_derived_spin_button(_width_item, "width", 15, &SprayToolbar::width_value_changed);
    setup_derived_spin_button(_population_item, "population", 70, &SprayToolbar::population_value_changed);
    setup_derived_spin_button(_rotation_item, "rotation_variation", 0, &SprayToolbar::rotation_value_changed);
    setup_derived_spin_button(_scale_item, "scale_variation", 0, &SprayToolbar::scale_value_changed);
    setup_derived_spin_button(_sd_item, "standard_deviation", 70, &SprayToolbar::standard_deviation_value_changed);
    setup_derived_spin_button(_mean_item, "mean", 0, &SprayToolbar::mean_value_changed);
    setup_derived_spin_button(_offset_item, "offset", 100, &SprayToolbar::offset_value_changed);

    // Configure mode buttons
    int btn_index = 0;
    for_each_child(get_widget<Gtk::Box>(_builder, "mode_buttons_box"), [&](Gtk::Widget &item){
        auto &btn = dynamic_cast<Gtk::ToggleButton &>(item);
        _mode_buttons.push_back(&btn);
        btn.signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &SprayToolbar::mode_changed), btn_index++));
        return ForEachResult::_continue;
    });

    // Width pressure button.
    _use_pressure_width_pusher.reset(new UI::SimplePrefPusher(use_pressure_width_btn, "/tools/spray/usepressurewidth"));
    use_pressure_width_btn->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &SprayToolbar::on_pref_toggled),
                                                                use_pressure_width_btn->get_active(),
                                                                "/tools/spray/usepressurewidth"));

    // Population pressure button.
    _use_pressure_population_pusher.reset(
        new UI::SimplePrefPusher(use_pressure_population_btn, "/tools/spray/usepressurepopulation"));
    use_pressure_population_btn->signal_toggled().connect(
        sigc::bind(sigc::mem_fun(*this, &SprayToolbar::on_pref_toggled), use_pressure_population_btn->get_active(),
                   "/tools/spray/usepressurepopulation"));

    auto prefs = Inkscape::Preferences::get();

    // Scale pressure button.
    _use_pressure_scale_btn.set_active(prefs->getBool("/tools/spray/usepressurescale", false));
    _use_pressure_scale_btn.signal_toggled().connect(sigc::mem_fun(*this, &SprayToolbar::toggle_pressure_scale));

    // Over no transparent button.
    _over_no_transparent_btn.set_active(prefs->getBool("/tools/spray/over_no_transparent", true));
    _over_no_transparent_btn.signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &SprayToolbar::on_pref_toggled),
                                                                 _over_no_transparent_btn.get_active(),
                                                                 "/tools/spray/over_no_transparent"));

    // Over transparent button.
    _over_transparent_btn.set_active(prefs->getBool("/tools/spray/over_transparent", true));
    _over_transparent_btn.signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &SprayToolbar::on_pref_toggled),
                                                              _over_transparent_btn.get_active(),
                                                              "/tools/spray/over_transparent"));

    // Pick no overlap button.
    _pick_no_overlap_btn.set_active(prefs->getBool("/tools/spray/pick_no_overlap", false));
    _pick_no_overlap_btn.signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &SprayToolbar::on_pref_toggled),
                                                             _pick_no_overlap_btn.get_active(),
                                                             "/tools/spray/pick_no_overlap"));

    // Overlap button.
    _no_overlap_btn.set_active(prefs->getBool("/tools/spray/no_overlap", false));
    _no_overlap_btn.signal_toggled().connect(sigc::mem_fun(*this, &SprayToolbar::toggle_no_overlap));

    // Picker button.
    _picker_btn.set_active(prefs->getBool("/tools/spray/picker", false));
    _picker_btn.signal_toggled().connect(sigc::mem_fun(*this, &SprayToolbar::toggle_picker));

    // Pick fill button.
    _pick_fill_btn.set_active(prefs->getBool("/tools/spray/pick_fill", false));
    _pick_fill_btn.signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &SprayToolbar::on_pref_toggled),
                                                       _pick_fill_btn.get_active(), "/tools/spray/pick_fill"));

    // Pick stroke button.
    _pick_stroke_btn.set_active(prefs->getBool("/tools/spray/pick_stroke", false));
    _pick_stroke_btn.signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &SprayToolbar::on_pref_toggled),
                                                         _pick_stroke_btn.get_active(), "/tools/spray/pick_stroke"));

    // Inverse value size button.
    _pick_inverse_value_btn.set_active(prefs->getBool("/tools/spray/pick_inverse_value", false));
    _pick_inverse_value_btn.signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &SprayToolbar::on_pref_toggled),
                                                                _pick_inverse_value_btn.get_active(),
                                                                "/tools/spray/pick_inverse_value"));

    // Pick from center button.
    _pick_center_btn.set_active(prefs->getBool("/tools/spray/pick_center", true));
    _pick_center_btn.signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &SprayToolbar::on_pref_toggled),
                                                         _pick_center_btn.get_active(), "/tools/spray/pick_center"));

    // Fetch all the ToolbarMenuButtons at once from the UI file
    // Menu Button #1
    auto popover_box1 = &get_widget<Gtk::Box>(_builder, "popover_box1");
    auto menu_btn1 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn1");

    // Menu Button #2
    auto popover_box2 = &get_widget<Gtk::Box>(_builder, "popover_box2");
    auto menu_btn2 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn2");

    // Menu Button #3
    auto popover_box3 = &get_widget<Gtk::Box>(_builder, "popover_box3");
    auto menu_btn3 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn3");

    // Menu Button #4
    auto popover_box4 = &get_widget<Gtk::Box>(_builder, "popover_box4");
    auto menu_btn4 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn4");

    // Initialize all the ToolbarMenuButtons only after all the children of the
    // toolbar have been fetched. Otherwise, the children to be moved in the
    // popover will get mapped to a different position and it will probably
    // cause segfault.
    auto children = UI::get_children(*_toolbar);

    menu_btn1->init(1, "tag1", popover_box1, children);
    addCollapsibleButton(menu_btn1);

    menu_btn2->init(2, "tag2", popover_box2, children);
    addCollapsibleButton(menu_btn2);

    menu_btn3->init(3, "tag3", popover_box3, children);
    addCollapsibleButton(menu_btn3);

    menu_btn4->init(4, "tag4", popover_box4, children);
    addCollapsibleButton(menu_btn4);

    append(*_toolbar);

    int mode = prefs->getIntLimited("/tools/spray/mode", 1, 0, _mode_buttons.size() - 1);
    _mode_buttons[mode]->set_active();
    init();
}

SprayToolbar::~SprayToolbar() = default;

void SprayToolbar::setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name,
                                             double default_value, ValueChangedMemFun const value_changed_mem_fun)
{
    const Glib::ustring path = "/tools/spray/" + name;
    auto const val = Preferences::get()->getDouble(path, default_value);

    auto adj = btn.get_adjustment();
    adj->set_value(val);
    adj->signal_value_changed().connect(sigc::mem_fun(*this, value_changed_mem_fun));

    btn.set_defocus_widget(_desktop->getCanvas());
}

void SprayToolbar::width_value_changed()
{
    Preferences::get()->setDouble("/tools/spray/width", _width_item.get_adjustment()->get_value());
}

void SprayToolbar::mean_value_changed()
{
    Preferences::get()->setDouble("/tools/spray/mean", _mean_item.get_adjustment()->get_value());
}

void SprayToolbar::standard_deviation_value_changed()
{
    Preferences::get()->setDouble("/tools/spray/standard_deviation", _sd_item.get_adjustment()->get_value());
}

void SprayToolbar::mode_changed(int mode)
{
    Preferences::get()->setInt("/tools/spray/mode", mode);
    init();
}

void SprayToolbar::init()
{
    int mode = Preferences::get()->getInt("/tools/spray/mode", 0);

    bool show = true;

    if(mode == 3 || mode == 2){
        show = false;
    }

    _over_no_transparent_btn.set_visible(show);
    _over_transparent_btn.set_visible(show);
    _pick_no_overlap_btn.set_visible(show);
    _no_overlap_btn.set_visible(show);

    _picker_btn.set_visible(show);
    _pick_fill_btn.set_visible(show);
    _pick_stroke_btn.set_visible(show);
    _pick_inverse_value_btn.set_visible(show);
    _pick_center_btn.set_visible(show);
    _offset_item.set_visible(show);

    if(mode == 2){
        show = true;
    }

    _rotation_box.set_visible(show);
    update_widgets();
}

void SprayToolbar::population_value_changed()
{
    Preferences::get()->setDouble("/tools/spray/population", _population_item.get_adjustment()->get_value());
}

void SprayToolbar::rotation_value_changed()
{
    Preferences::get()->setDouble("/tools/spray/rotation_variation", _rotation_item.get_adjustment()->get_value());
}

void SprayToolbar::update_widgets()
{
    _offset_item.get_adjustment()->set_value(100.0);

    bool no_overlap_is_active = _no_overlap_btn.get_active() && _no_overlap_btn.get_visible();
    _offset_box.set_visible(no_overlap_is_active);
    if (_use_pressure_scale_btn.get_active()) {
        _scale_item.get_adjustment()->set_value(0.0);
        _scale_item.set_sensitive(false);
    } else {
        _scale_item.set_sensitive(true);
    }

    bool picker_is_active = _picker_btn.get_active() && _picker_btn.get_visible();
    _pick_fill_btn.set_visible(picker_is_active);
    _pick_stroke_btn.set_visible(picker_is_active);
    _pick_inverse_value_btn.set_visible(picker_is_active);
    _pick_center_btn.set_visible(picker_is_active);
}

void SprayToolbar::toggle_no_overlap()
{
    bool active = _no_overlap_btn.get_active();
    Preferences::get()->setBool("/tools/spray/no_overlap", active);
    update_widgets();
}

void SprayToolbar::scale_value_changed()
{
    Preferences::get()->setDouble("/tools/spray/scale_variation", _scale_item.get_adjustment()->get_value());
}

void SprayToolbar::offset_value_changed()
{
    Preferences::get()->setDouble("/tools/spray/offset", _offset_item.get_adjustment()->get_value());
}

void SprayToolbar::toggle_pressure_scale()
{
    auto prefs = Inkscape::Preferences::get();
    bool active = _use_pressure_scale_btn.get_active();
    prefs->setBool("/tools/spray/usepressurescale", active);
    if(active){
        prefs->setDouble("/tools/spray/scale_variation", 0);
    }
    update_widgets();
}

void SprayToolbar::toggle_picker()
{
    auto prefs = Inkscape::Preferences::get();
    bool active = _picker_btn.get_active();
    prefs->setBool("/tools/spray/picker", active);
    if(active){
        prefs->setBool("/dialogs/clonetiler/dotrace", false);
        SPDesktop *dt = _desktop;
        if (Inkscape::UI::Dialog::CloneTiler *ct = get_clone_tiler_panel(dt)){
            dt->getContainer()->new_dialog("CloneTiler");
            ct->show_page_trace();
        }
    }
    update_widgets();
}

void SprayToolbar::on_pref_toggled(bool active, const Glib::ustring &path)
{
    Preferences::get()->setBool(path, active);
}

void SprayToolbar::set_mode(int mode)
{
    _mode_buttons[mode]->set_active();
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
