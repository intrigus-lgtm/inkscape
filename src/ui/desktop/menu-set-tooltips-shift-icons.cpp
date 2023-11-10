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
#include <gtkmm/button.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>

#include "inkscape-application.h"  // Action extra data
#include "ui/shortcuts.h"  // TEMP???
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
    /*
    int width{}, height{};

    if (shift_icons) {
        menu.add_css_class("shifticonmenu");
        gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &width, &height);
    }
    */

    bool shifted = false;

    // Calculate required shift. We need an example!
    // Search for GtkModelButton -> Gtk::Box, Gtk::Image (optional), Gtk::Label.
    static auto app = InkscapeApplication::instance();
    auto &label_to_tooltip_map = app->get_menu_label_to_tooltip_map();

    Inkscape::UI::for_each_child(menu, [&](Gtk::Widget &child) {

        // Set tooltip.
        Glib::ustring label;
        if (child.get_name() == "GtkModelButton") {
            label = find_label(child);
        }

        if (!label.empty()) {
            auto it = label_to_tooltip_map.find(label);
            if (it != label_to_tooltip_map.end()) {
                child.set_tooltip_text(it->second); // Set tooltip on GtkModelButton.
            }
        }

        // The ModelButton contains in order: GtkBox, GtkImage (optionally), GtkLabel, GtkPopoverMenu (optionally).
        // for (auto child2 = child.get_first_child(); child2 != nullptr; child2 = child2->get_next_sibling()) {
        //     if (auto box = dynamic_cast<Gtk::Box*>(child2)) {
        //         // Do something with box. We might be able to use box width to calculate shift.
        //     }
        //     if (auto image = dynamic_cast<Gtk::Image*>(child2)) {
        //         // Do something with image.
        //     }
        //     if (auto label = dynamic_cast<Gtk::Image*>(child2)) {
        //         // Do something with label. Redundant with above.
        //     }
        //     if (child2->get_name() == "GtkPopoverMenu") {
        //         // Doesn't work as there are whole lot of widgets in the tree.
        //         set_tooltips_and_shift_icons(*child2, shift_icons);
        //     }
        // }

        // TODO: GTK4: Figure out the kind of shift we need.

        // if (!shift_icons || shifted || !box) {
        //     return Inkscape::UI::ForEachResult::_continue;
        // }

        // width += box->get_spacing();
        // auto const margin_side = widget->get_direction() == Gtk::TextDirection::RTL ? "right" : "left";
        // // TODO: GTK4: as per TODOs above.
        // auto const css_str = Glib::ustring::compose(".shifticonmenu menuitem box { margin-%1: -%2px; }"
        //                                             ".shifticonmenu modelbutton box > label:only-child { margin-%1: %2px; }",
        //                                             margin_side, width);
        // Glib::RefPtr<Gtk::CssProvider> provider = Gtk::CssProvider::create();
        // provider->load_from_data(css_str);
        // auto const display = Gdk::Display::get_default();
        // Gtk::StyleContext::add_provider_for_display(display, provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        // shifted = true;

        // Recurse
        set_tooltips_and_shift_icons(child, shift_icons);

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
