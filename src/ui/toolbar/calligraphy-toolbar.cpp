// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Calligraphy aux toolbar
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

#include "calligraphy-toolbar.h"

#include <glibmm/i18n.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/togglebutton.h>

#include "desktop.h"
#include "ui/builder-utils.h"
#include "ui/dialog/calligraphic-profile-rename.h"
#include "ui/simple-pref-pusher.h"
#include "ui/util.h"
#include "ui/widget/canvas.h"
#include "ui/widget/combo-tool-item.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/toolbar-menu-button.h"
#include "ui/widget/unit-tracker.h"

using Inkscape::UI::Widget::UnitTracker;
using Inkscape::Util::Quantity;
using Inkscape::Util::Unit;
using Inkscape::Util::UnitTable;

std::vector<Glib::ustring> get_presets_list() {
    std::vector<Glib::ustring> presets = Inkscape::Preferences::get()->getAllDirs("/tools/calligraphic/preset");
    return presets;
}

namespace Inkscape::UI::Toolbar {

CalligraphyToolbar::CalligraphyToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    , _tracker{std::make_unique<UnitTracker>(Inkscape::Util::UNIT_TYPE_LINEAR)}
    , _presets_blocked(false)
    , _builder(create_builder("toolbar-calligraphy.ui"))
    , _profile_selector_combo(get_widget<Gtk::ComboBoxText>(_builder, "_profile_selector_combo"))
    , _width_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_width_item"))
    , _thinning_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_thinning_item"))
    , _mass_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_mass_item"))
    , _angle_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_angle_item"))
    , _usetilt_btn(&get_widget<Gtk::ToggleButton>(_builder, "_usetilt_btn"))
    , _flatness_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_flatness_item"))
    , _cap_rounding_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_cap_rounding_item"))
    , _tremor_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_tremor_item"))
    , _wiggle_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_wiggle_item"))
{
    auto *prefs = Inkscape::Preferences::get();

    _tracker->prependUnit(UnitTable::get().getUnit("px"));
    _tracker->changeLabel("%", 0, true);
    if (prefs->getBool("/tools/calligraphic/abs_width")) {
        _tracker->setActiveUnitByLabel(prefs->getString("/tools/calligraphic/unit"));
    }

    _toolbar = &get_widget<Gtk::Box>(_builder, "calligraphy-toolbar");

    auto usepressure_btn = &get_widget<Gtk::ToggleButton>(_builder, "usepressure_btn");
    auto tracebackground_btn = &get_widget<Gtk::ToggleButton>(_builder, "tracebackground_btn");

    // Setup the spin buttons.
    setup_derived_spin_button(_width_item, "width", 15.118, &CalligraphyToolbar::width_value_changed);
    setup_derived_spin_button(_thinning_item, "thinning", 10, &CalligraphyToolbar::velthin_value_changed);
    setup_derived_spin_button(_mass_item, "mass", 2.0, &CalligraphyToolbar::mass_value_changed);
    setup_derived_spin_button(_angle_item, "angle", 30, &CalligraphyToolbar::angle_value_changed);
    setup_derived_spin_button(_flatness_item, "flatness", -90, &CalligraphyToolbar::flatness_value_changed);
    setup_derived_spin_button(_cap_rounding_item, "cap_rounding", 0.0, &CalligraphyToolbar::cap_rounding_value_changed);
    setup_derived_spin_button(_tremor_item, "tremor", 0.0, &CalligraphyToolbar::tremor_value_changed);
    setup_derived_spin_button(_wiggle_item, "wiggle", 0.0, &CalligraphyToolbar::wiggle_value_changed);

    // Configure the calligraphic profile combo box text.
    build_presets_list();
    _profile_selector_combo.signal_changed().connect(sigc::mem_fun(*this, &CalligraphyToolbar::change_profile));

    // Unit menu.
    auto unit_menu = _tracker->create_tool_item(_("Units"), "");
    get_widget<Gtk::Box>(_builder, "unit_menu_box").append(*unit_menu);
    unit_menu->signal_changed_after().connect(sigc::mem_fun(*this, &CalligraphyToolbar::unit_changed));

    // Use pressure button.
    _widget_map["usepressure"] = G_OBJECT(usepressure_btn->gobj());
    _usepressure_pusher.reset(new SimplePrefPusher(usepressure_btn, "/tools/calligraphic/usepressure"));
    usepressure_btn->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &CalligraphyToolbar::on_pref_toggled),
                                                         usepressure_btn, "/tools/calligraphic/usepressure"));

    // Tracebackground button.
    _widget_map["tracebackground"] = G_OBJECT(tracebackground_btn->gobj());
    _tracebackground_pusher.reset(new SimplePrefPusher(tracebackground_btn, "/tools/calligraphic/tracebackground"));
    tracebackground_btn->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &CalligraphyToolbar::on_pref_toggled),
                                                             tracebackground_btn,
                                                             "/tools/calligraphic/tracebackground"));

    // Use tilt button.
    _widget_map["usetilt"] = G_OBJECT(_usetilt_btn->gobj());
    _usetilt_pusher.reset(new SimplePrefPusher(_usetilt_btn, "/tools/calligraphic/usetilt"));
    _usetilt_btn->signal_toggled().connect(sigc::mem_fun(*this, &CalligraphyToolbar::tilt_state_changed));
    _angle_item.set_sensitive(!prefs->getBool("/tools/calligraphic/usetilt", true));
    _usetilt_btn->set_active(prefs->getBool("/tools/calligraphic/usetilt", true));

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

    set_child(*_toolbar);

    // Signals.
    get_widget<Gtk::Button>(_builder, "profile_edit_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &CalligraphyToolbar::edit_profile));
}

CalligraphyToolbar::~CalligraphyToolbar() = default;

void CalligraphyToolbar::setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name,
                                                   double default_value, ValueChangedMemFun const value_changed_mem_fun)
{
    auto prefs = Preferences::get();
    const Glib::ustring path = "/tools/calligraphic/" + name;
    auto const val = prefs->getDouble(path, default_value);
    auto adj = btn.get_adjustment();

    if (name == "width") {
        Unit const *unit = UnitTable::get().getUnit(prefs->getString("/tools/calligraphic/unit"));
        adj = Gtk::Adjustment::create(Quantity::convert(val, "px", unit), 0.001, 100, 1.0, 10.0);
        btn.set_adjustment(adj);
    } else {
        adj->set_value(val);
    }

    adj->signal_value_changed().connect(sigc::mem_fun(*this, value_changed_mem_fun));

    _widget_map[name] = G_OBJECT(adj->gobj());
    _tracker->addAdjustment(adj->gobj());

    btn.set_defocus_widget(_desktop->getCanvas());
}

void CalligraphyToolbar::width_value_changed()
{
    Unit const *unit = _tracker->getActiveUnit();
    g_return_if_fail(unit != nullptr);
    auto prefs = Inkscape::Preferences::get();
    prefs->setBool("/tools/calligraphic/abs_width", _tracker->getCurrentLabel() != "%");
    prefs->setDouble("/tools/calligraphic/width",
                     Quantity::convert(_width_item.get_adjustment()->get_value(), unit, "px"));
    update_presets_list();
}

void CalligraphyToolbar::velthin_value_changed()
{
    Preferences::get()->setDouble("/tools/calligraphic/thinning", _thinning_item.get_adjustment()->get_value());
    update_presets_list();
}

void CalligraphyToolbar::angle_value_changed()
{
    Preferences::get()->setDouble("/tools/calligraphic/angle", _angle_item.get_adjustment()->get_value());
    update_presets_list();
}

void CalligraphyToolbar::flatness_value_changed()
{
    Preferences::get()->setDouble("/tools/calligraphic/flatness", _flatness_item.get_adjustment()->get_value());
    update_presets_list();
}

void CalligraphyToolbar::cap_rounding_value_changed()
{
    Preferences::get()->setDouble("/tools/calligraphic/cap_rounding", _cap_rounding_item.get_adjustment()->get_value());
    update_presets_list();
}

void CalligraphyToolbar::tremor_value_changed()
{
    Preferences::get()->setDouble("/tools/calligraphic/tremor", _tremor_item.get_adjustment()->get_value());
    update_presets_list();
}

void CalligraphyToolbar::wiggle_value_changed()
{
    Preferences::get()->setDouble("/tools/calligraphic/wiggle", _wiggle_item.get_adjustment()->get_value());
    update_presets_list();
}

void CalligraphyToolbar::mass_value_changed()
{
    Preferences::get()->setDouble("/tools/calligraphic/mass", _mass_item.get_adjustment()->get_value());
    update_presets_list();
}

void CalligraphyToolbar::on_pref_toggled(Gtk::ToggleButton *item, Glib::ustring const &path)
{
    Preferences::get()->setBool(path, item->get_active());
    update_presets_list();
}

void CalligraphyToolbar::update_presets_list()
{
    if (_presets_blocked) {
        return;
    }

    auto presets = get_presets_list();

    int index = 1;  // 0 is for no preset.
    for (auto i = presets.begin(); i != presets.end(); ++i, ++index) {
        bool match = true;

        auto preset = Preferences::get()->getAllEntries(*i);
        for (auto & j : preset) {
            Glib::ustring entry_name = j.getEntryName();
            if (entry_name == "id" || entry_name == "name") {
                continue;
            }

            void *widget = _widget_map[entry_name.data()];
            if (widget) {
                if (GTK_IS_ADJUSTMENT(widget)) {
                    double v = j.getDouble();
                    GtkAdjustment* adj = static_cast<GtkAdjustment *>(widget);
                    //std::cout << "compared adj " << attr_name << gtk_adjustment_get_value(adj) << " to " << v << "\n";
                    if (fabs(gtk_adjustment_get_value(adj) - v) > 1e-6) {
                        match = false;
                        break;
                    }
                } else if (GTK_IS_TOGGLE_BUTTON(widget)) {
                    bool v = j.getBool();
                    auto toggle = GTK_TOGGLE_BUTTON(widget);
                    //std::cout << "compared toggle " << attr_name << gtk_toggle_action_get_active(toggle) << " to " << v << "\n";
                    if (static_cast<bool>(gtk_toggle_button_get_active(toggle)) != v) {
                        match = false;
                        break;
                    }
                }
            }
        }

        if (match) {
            // newly added item is at the same index as the
            // save command, so we need to change twice for it to take effect
            _profile_selector_combo.set_active(0);
            _profile_selector_combo.set_active(index);
            return;
        }
    }

    // no match found
    _profile_selector_combo.set_active(0);
}

void CalligraphyToolbar::tilt_state_changed()
{
    _angle_item.set_sensitive(!_usetilt_btn->get_active());
    on_pref_toggled(_usetilt_btn, "/tools/calligraphic/usetilt");
}

void CalligraphyToolbar::build_presets_list()
{
    _presets_blocked = true;

    _profile_selector_combo.remove_all();
    _profile_selector_combo.append(_("No preset"));

    // iterate over all presets to populate the list
    auto presets = get_presets_list();

    for (auto & preset : presets) {
        Glib::ustring preset_name = Preferences::get()->getString(preset + "/name");

        if (!preset_name.empty()) {
            _profile_selector_combo.append(_(preset_name.data()));
        }
    }

    _presets_blocked = false;

    update_presets_list();
}

void CalligraphyToolbar::change_profile()
{
    auto mode = _profile_selector_combo.get_active_row_number();

    if (_presets_blocked) {
        return;
    }

    // mode is one-based so we subtract 1
    std::vector<Glib::ustring> presets = get_presets_list();

    Glib::ustring preset_path = "";
    if (mode - 1 < presets.size()) {
        preset_path = presets.at(mode - 1);
    }

    if (!preset_path.empty()) {
        _presets_blocked = true; //temporarily block the selector so no one will updadte it while we're reading it

        std::vector<Inkscape::Preferences::Entry> preset = Preferences::get()->getAllEntries(preset_path);

        // Shouldn't this be std::map?
        for (auto & i : preset) {
            Glib::ustring entry_name = i.getEntryName();
            if (entry_name == "id" || entry_name == "name") {
                continue;
            }
            void *widget = _widget_map[entry_name.data()];
            if (widget) {
                if (GTK_IS_ADJUSTMENT(widget)) {
                    GtkAdjustment* adj = static_cast<GtkAdjustment *>(widget);
                    gtk_adjustment_set_value(adj, i.getDouble());
                    //std::cout << "set adj " << attr_name << " to " << v << "\n";
                } else if (GTK_IS_TOGGLE_BUTTON(widget)) {
                    auto toggle = GTK_TOGGLE_BUTTON(widget);
                    gtk_toggle_button_set_active(toggle, i.getBool());
                    //std::cout << "set toggle " << attr_name << " to " << v << "\n";
                } else {
                    g_warning("Unknown widget type for preset: %s\n", entry_name.data());
                }
            } else {
                g_warning("Bad key found in a preset record: %s\n", entry_name.data());
            }
        }
        _presets_blocked = false;
    }
}

void CalligraphyToolbar::edit_profile()
{
    save_profile(nullptr);
}

void CalligraphyToolbar::unit_changed(int /* NotUsed */)
{
    Unit const *unit = _tracker->getActiveUnit();
    g_return_if_fail(unit != nullptr);
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setBool("/tools/calligraphic/abs_width", _tracker->getCurrentLabel() != "%");
    prefs->setDouble("/tools/calligraphic/width",
                     CLAMP(prefs->getDouble("/tools/calligraphic/width"), Quantity::convert(0.001, unit, "px"),
                           Quantity::convert(100, unit, "px")));
    prefs->setString("/tools/calligraphic/unit", unit->abbr);
}

void CalligraphyToolbar::save_profile(GtkWidget * /*widget*/)
{
    using Inkscape::UI::Dialog::CalligraphicProfileRename;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (! _desktop) {
        return;
    }

    if (_presets_blocked) {
        return;
    }

    Glib::ustring current_profile_name = _profile_selector_combo.get_active_text();

    if (current_profile_name == _("No preset")) {
        current_profile_name = "";
    }

    CalligraphicProfileRename::show(_desktop, current_profile_name);
    if ( !CalligraphicProfileRename::applied()) {
        // dialog cancelled
        update_presets_list();
        return;
    }
    Glib::ustring new_profile_name = CalligraphicProfileRename::getProfileName();

    if (new_profile_name.empty()) {
        // empty name entered
        update_presets_list ();
        return;
    }

    _presets_blocked = true;

    // If there's a preset with the given name, find it and set save_path appropriately
    auto presets = get_presets_list();
    int total_presets = presets.size();
    int new_index = -1;
    Glib::ustring save_path; // profile pref path without a trailing slash

    int temp_index = 0;
    for (std::vector<Glib::ustring>::iterator i = presets.begin(); i != presets.end(); ++i, ++temp_index) {
        Glib::ustring name = prefs->getString(*i + "/name");
        if (!name.empty() && (new_profile_name == name || current_profile_name == name)) {
            new_index = temp_index;
            save_path = *i;
            break;
        }
    }

    if ( CalligraphicProfileRename::deleted() && new_index != -1) {
        prefs->remove(save_path);
        _presets_blocked = false;
        build_presets_list();
        return;
    }

    if (new_index == -1) {
        // no preset with this name, create
        new_index = total_presets + 1;
        gchar *profile_id = g_strdup_printf("/dcc%d", new_index);
        save_path = Glib::ustring("/tools/calligraphic/preset") + profile_id;
        g_free(profile_id);
    }

    for (auto const &[widget_name, widget] : _widget_map) {
        if (widget) {
            if (GTK_IS_ADJUSTMENT(widget)) {
                GtkAdjustment* adj = GTK_ADJUSTMENT(widget);
                prefs->setDouble(save_path + "/" + widget_name, gtk_adjustment_get_value(adj));
                //std::cout << "wrote adj " << widget_name << ": " << v << "\n";
            } else if (GTK_IS_TOGGLE_BUTTON(widget)) {
                auto toggle = GTK_TOGGLE_BUTTON(widget);
                prefs->setBool(save_path + "/" + widget_name, gtk_toggle_button_get_active(toggle));
                //std::cout << "wrote tog " << widget_name << ": " << v << "\n";
            } else {
                g_warning("Unknown widget type for preset: %s\n", widget_name.c_str());
            }
        } else {
            g_warning("Bad key when writing preset: %s\n", widget_name.c_str());
        }
    }
    prefs->setString(save_path + "/name", new_profile_name);

    _presets_blocked = true;
    build_presets_list();
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
