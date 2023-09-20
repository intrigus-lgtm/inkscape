// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Go over a widget representing a menu, & set tooltips on its items from app label-to-tooltip map.
 * Optionally (per Preference) shift Gtk::MenuItems with icons to align with Toggle & Radio buttons
 */
/*
 * Authors:
 *   Tavmjong Bah       <tavmjong@free.fr>
 *   Patrick Storz      <eduard.braun2@gmx.de>
 *   Daniel Boles       <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2020-2023 Authors
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 * Read the file 'COPYING' for more information.
 *
 */

#include "ui/desktop/menu-set-tooltips-shift-icons.h"

#include <glibmm/ustring.h>
#include <gtkmm/box.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/menu.h>
#include <gtkmm/menuitem.h>

#include "inkscape-application.h"  // Action extra data
#include "ui/util.h"

// Could be used to update status bar.
// bool on_enter_notify(GdkEventCrossing* crossing_event, Gtk::MenuItem* menuitem)
// {
//     return false;
// }

[[nodiscard]] static Glib::ustring find_label(Gtk::Widget &parent)
{
    Glib::ustring label;
    Inkscape::UI::for_each_child(parent, [&](Gtk::Widget const &child)
    {
        if (auto const label_widget = dynamic_cast<Gtk::Label const *>(&child)) {
            label = label_widget->get_label();
            return Inkscape::UI::ForEachResult::_break;
        }
        return Inkscape::UI::ForEachResult::_continue;
    });
    return label;
}

/**
 * Go over a widget representing a menu, & set tooltips on its items from app label-to-tooltip map.
 * @param shift_icons If true:
 * Install CSS to shift icons into the space reserved for toggles (i.e. check and radio items).
 * The CSS will apply to all menu icons but is updated as each menu is shown.
 * @returns whether icons were shifted during this or an inner recursive call
 */
bool
set_tooltips_and_shift_icons(Gtk::Widget &menu, bool const shift_icons)
{
    int width{}, height{};

    if (shift_icons) {
        menu.add_css_class("shifticonmenu");
        gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &width, &height);
    }

    bool shifted = false;

    // Calculate required shift. We need an example!
    // Search for Gtk::MenuItem -> Gtk::Box -> Gtk::Image
    static auto app = InkscapeApplication::instance();
    auto &label_to_tooltip_map = app->get_menu_label_to_tooltip_map();

    Inkscape::UI::for_each_child(menu, [&](Gtk::Widget &child)
    {
        Gtk::Widget *widget = nullptr;
        Gtk::Box *box = nullptr;
        Glib::ustring label;

        // TODO: GTK4: Replace with appropriate widgets/nodes for PopoverMenuBar
        if (auto const menuitem = dynamic_cast<Gtk::MenuItem *>(&child)) {
            widget = menuitem;

            auto submenu = menuitem->get_submenu();
            if (submenu) {
                shifted = set_tooltips_and_shift_icons(*submenu, shift_icons);
            }

            label = menuitem->get_label();
            if (label.empty()) {
                box = dynamic_cast<Gtk::Box *>(menuitem->get_child());
                if (box) {
                    label = find_label(*box);
                }
            }
        } else if (child.get_name() == "modelbutton") {
            // TODO: GTK4: layout of internal Box/Label children has changed
            widget = &child;
            box = dynamic_cast<Gtk::Box *>(widget->get_first_child());
            if (box) {
                label = find_label(*box);
            }
        }

        if (!widget || label.empty()) {
            return Inkscape::UI::ForEachResult::_continue;
        }

        auto it = label_to_tooltip_map.find(label);
        if (it != label_to_tooltip_map.end()) {
            widget->set_tooltip_text(it->second);
        }

        if (!shift_icons || shifted || !box) {
            return Inkscape::UI::ForEachResult::_continue;
        }

        width += box->get_spacing();
        auto const margin_side = widget->get_direction() == Gtk::TextDirection::RTL ? "right" : "left";
        // TODO: GTK4: as per TODOs above.
        auto const css_str = Glib::ustring::compose(".shifticonmenu menuitem box { margin-%1: -%2px; }"
                                                    ".shifticonmenu modelbutton box > label:only-child { margin-%1: %2px; }",
                                                    margin_side, width);
        Glib::RefPtr<Gtk::CssProvider> provider = Gtk::CssProvider::create();
        provider->load_from_data(css_str);
        auto const screen = Gdk::Screen::get_default();
        Gtk::StyleContext::add_provider_for_screen(screen, provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        shifted = true;
        return Inkscape::UI::ForEachResult::_continue;
    });

    return shifted;
}

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
