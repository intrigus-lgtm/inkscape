// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Paint bucket aux toolbar
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

#include "paintbucket-toolbar.h"

#include <glibmm/i18n.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/liststore.h>
#include <gtkmm/treemodel.h>

#include "desktop.h"
#include "preferences.h"
#include "ui/builder-utils.h"
#include "ui/tools/flood-tool.h"
#include "ui/util.h"
#include "ui/widget/canvas.h"
#include "ui/widget/combo-tool-item.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/toolbar-menu-button.h"
#include "ui/widget/unit-tracker.h"

namespace Inkscape::UI::Toolbar {

PaintbucketToolbar::PaintbucketToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    , _tracker{std::make_unique<UI::Widget::UnitTracker>(Inkscape::Util::UNIT_TYPE_LINEAR)}
    , _builder(create_builder("toolbar-paintbucket.ui"))
    , _threshold_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_threshold_item"))
    , _offset_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_offset_item"))
{
    auto prefs = Preferences::get();

    _toolbar = &get_widget<Gtk::Box>(_builder, "paintbucket-toolbar");

    // Setup the spin buttons.
    setup_derived_spin_button(_threshold_item, "threshold", 5, &PaintbucketToolbar::threshold_changed);
    setup_derived_spin_button(_offset_item, "offset", 0, &PaintbucketToolbar::offset_changed);

    // Channel
    {
        UI::Widget::ComboToolItemColumns columns;

        Glib::RefPtr<Gtk::ListStore> store = Gtk::ListStore::create(columns);

        for (auto item: Inkscape::UI::Tools::FloodTool::channel_list) {
            Gtk::TreeModel::Row row = *(store->append());
            row[columns.col_label    ] = _(item);
            row[columns.col_sensitive] = true;
        }

        _channels_item = Gtk::manage(UI::Widget::ComboToolItem::create(_("Fill by"), Glib::ustring(), "Not Used", store));
        _channels_item->use_group_label(true);

        int channels = prefs->getInt("/tools/paintbucket/channels", 0);
        _channels_item->set_active(channels);

        _channels_item->signal_changed().connect(sigc::mem_fun(*this, &PaintbucketToolbar::channels_changed));
        get_widget<Gtk::Box>(_builder, "channels_box").append(*_channels_item);

        // Create the units menu.
        Glib::ustring stored_unit = prefs->getString("/tools/paintbucket/offsetunits");
        if (!stored_unit.empty()) {
            Unit const *u = Util::UnitTable::get().getUnit(stored_unit);
            _tracker->setActiveUnit(u);
        }
    }

    // Auto Gap
    {
        UI::Widget::ComboToolItemColumns columns;

        Glib::RefPtr<Gtk::ListStore> store = Gtk::ListStore::create(columns);

        for (auto item: Inkscape::UI::Tools::FloodTool::gap_list) {
            Gtk::TreeModel::Row row = *(store->append());
            row[columns.col_label    ] = g_dpgettext2(nullptr, "Flood autogap", item);
            row[columns.col_sensitive] = true;
        }

        _autogap_item = Gtk::manage(UI::Widget::ComboToolItem::create(_("Close gaps"), Glib::ustring(), "Not Used", store));
        _autogap_item->use_group_label(true);

        int autogap = prefs->getInt("/tools/paintbucket/autogap", 0);
        _autogap_item->set_active(autogap);

        _autogap_item->signal_changed().connect(sigc::mem_fun(*this, &PaintbucketToolbar::autogap_changed));
        get_widget<Gtk::Box>(_builder, "autogap_box").append(*_autogap_item);

        auto units_menu = _tracker->create_tool_item(_("Units"), (""));
        get_widget<Gtk::Box>(_builder, "unit_menu_box").append(*units_menu);
    }

    // Fetch all the ToolbarMenuButtons at once from the UI file
    // Menu Button #1
    auto popover_box1 = &get_widget<Gtk::Box>(_builder, "popover_box1");
    auto menu_btn1 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn1");

    // Initialize all the ToolbarMenuButtons only after all the children of the
    // toolbar have been fetched. Otherwise, the children to be moved in the
    // popover will get mapped to a different position and it will probably
    // cause segfault.
    auto children = UI::get_children(*_toolbar);

    menu_btn1->init(1, "tag1", popover_box1, children);
    addCollapsibleButton(menu_btn1);

    set_child(*_toolbar);

    // Signals.
    get_widget<Gtk::Button>(_builder, "reset_btn")
        .signal_clicked()
        .connect(sigc::mem_fun(*this, &PaintbucketToolbar::defaults));
}

PaintbucketToolbar::~PaintbucketToolbar() = default;

void PaintbucketToolbar::setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name,
                                                   double default_value, ValueChangedMemFun const value_changed_mem_fun)
{
    const Glib::ustring path = "/tools/painbucket/" + name;
    auto const val = Preferences::get()->getDouble(path, default_value);

    auto adj = btn.get_adjustment();
    adj->set_value(val);
    adj->signal_value_changed().connect(sigc::mem_fun(*this, value_changed_mem_fun));

    if (name == "offset") {
        _tracker->addAdjustment(adj->gobj());
        btn.addUnitTracker(_tracker.get());
    }

    btn.set_defocus_widget(_desktop->getCanvas());
}

void PaintbucketToolbar::channels_changed(int channels)
{
    Inkscape::UI::Tools::FloodTool::set_channels(channels);
}

void PaintbucketToolbar::threshold_changed()
{
    Preferences::get()->setInt("/tools/paintbucket/threshold", (gint)_threshold_item.get_adjustment()->get_value());
}

void PaintbucketToolbar::offset_changed()
{
    Unit const *unit = _tracker->getActiveUnit();
    auto *prefs = Inkscape::Preferences::get();

    // Don't adjust the offset value because we're saving the
    // unit and it'll be correctly handled on load.
    prefs->setDouble("/tools/paintbucket/offset", (gdouble)_offset_item.get_adjustment()->get_value());

    g_return_if_fail(unit != nullptr);
    prefs->setString("/tools/paintbucket/offsetunits", unit->abbr);
}

void PaintbucketToolbar::autogap_changed(int autogap)
{
    Preferences::get()->setInt("/tools/paintbucket/autogap", autogap);
}

void PaintbucketToolbar::defaults()
{
    // FIXME: make defaults settable via Inkscape Options
    _threshold_item.get_adjustment()->set_value(15);
    _offset_item.get_adjustment()->set_value(0.0);

    _channels_item->set_active(Inkscape::UI::Tools::FLOOD_CHANNELS_RGB);
    _autogap_item->set_active(0);
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
