// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A replacement for GTK3ʼs Gtk::MenuItem, as removed in GTK4.
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "ui/widget/popover-menu-item.h"

#include <pangomm/layout.h>
#include <giomm/themedicon.h>
#include <gtkmm/box.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/tooltip.h>

#include "ui/menuize.h"
#include "ui/util.h"
#include "ui/widget/popover-menu.h"

namespace Inkscape::UI::Widget {

PopoverMenuItem::PopoverMenuItem(Glib::ustring const &text,
                                 bool const mnemonic,
                                 Glib::ustring const &icon_name,
                                 Gtk::IconSize const icon_size,
                                 bool const popdown_on_activate)
    : Glib::ObjectBase{"PopoverMenuItem"}
    , CssNameClassInit{"menuitem"}
    , Gtk::Button{}
{
    get_style_context()->add_class("menuitem");
    set_has_frame(false);

    Gtk::Label *label = nullptr;
    Gtk::Image *image = nullptr;

    if (!text.empty()) {
        label = Gtk::make_managed<Gtk::Label>(text, Gtk::Align::START, Gtk::Align::CENTER, mnemonic);
    }

    if (!icon_name.empty()) {
        image = Gtk::make_managed<Gtk::Image>(Gio::ThemedIcon::create(icon_name));
        image->set_icon_size(icon_size);
    }

    if (label && image) {
        auto &hbox = *Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        hbox.append(*image);
        hbox.append(*label);
        set_child(hbox);
    } else if (label) {
        set_child(*label);
    } else if (image) {
        set_child(*image);
    }

    if (popdown_on_activate) {
        signal_activate().connect([this]
        {
            if (auto const menu = get_menu()) {
                menu->popdown();
            }
        });
    }

    UI::menuize(*this);
}

Glib::SignalProxy<void ()> PopoverMenuItem::signal_activate()
{
    return signal_clicked();
}

PopoverMenu *PopoverMenuItem::get_menu()
{
    PopoverMenu *result = nullptr;
    for_each_parent(*this, [&](Gtk::Widget &parent)
    {
        if (auto const menu = dynamic_cast<PopoverMenu *>(&parent)) {
            result = menu;
            return ForEachResult::_break;
        }
        return ForEachResult::_continue;
    });
    return result;
}

} // namespace Inkscape::UI::Widget

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
