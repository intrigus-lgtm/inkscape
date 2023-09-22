// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef INKSCAPE_UI_WIDGET_CUSTOM_TOOLTIP_H
#define INKSCAPE_UI_WIDGET_CUSTOM_TOOLTIP_H

#include <gtkmm/tooltip.h>

void sp_clear_custom_tooltip();

bool
sp_query_custom_tooltip(
    int x, 
    int y, 
    bool keyboard_tooltip, 
    const Glib::RefPtr<Gtk::Tooltip>& tooltipw, 
    gint id, 
    Glib::ustring tooltip, 
    Glib::ustring icon = "", 
    Gtk::IconSize iconsize = Gtk::IconSize::LARGE, 
    int delaytime = 1000.0);

#endif // INKSCAPE_UI_WIDGET_CUSTOM_TOOLTIP_H
