// SPDX-License-Identifier: GPL-2.0-or-later

#include "custom-tooltip.h"

#include <ctime>
#include <chrono>
#include <gdk/gdk.h>
#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/image.h>
#include <gtkmm/tooltip.h>

#include "ui/pack.h"

static gint timeoutid = -1;

static gboolean
delaytooltip (gpointer data)
{
    GdkDisplay *display = reinterpret_cast<GdkDisplay *>(data); 
    gtk_tooltip_trigger_tooltip_query(display);
    return true;
}

void sp_clear_custom_tooltip()
{
    if (timeoutid != -1) {
        g_source_remove(timeoutid);
        timeoutid = -1;
    }
}

bool
sp_query_custom_tooltip(int x, int y, bool keyboard_tooltip, const Glib::RefPtr<Gtk::Tooltip>& tooltipw, gint id, Glib::ustring tooltip, Glib::ustring icon, Gtk::IconSize iconsize, int delaytime)
{
    sp_clear_custom_tooltip();

    static gint last = -1;
    static auto start = std::chrono::steady_clock::now();
    auto end = std::chrono::steady_clock::now();
    if (last != id) {
        start = std::chrono::steady_clock::now();
        last = id;
    }
    auto const box = Gtk::make_managed<Gtk::Box>();
    auto const label = Gtk::make_managed<Gtk::Label>();
    label->set_line_wrap(true);
    label->set_markup(tooltip);
    label->set_max_width_chars(40);
    if (icon != "") {
	    Inkscape::UI::pack_start(*box, *Gtk::make_managed<Gtk::Image>(icon, iconsize), true, true, 2);
    }
    Inkscape::UI::pack_start(*box, *label, true, true, 2);
    tooltipw->set_custom(*box);
    box->add_css_class("symbolic");

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    if (elapsed.count() / delaytime < 0.5) {
        GdkDisplay *display = gdk_display_get_default();
        if (display) {
            timeoutid = g_timeout_add(501-elapsed.count(), delaytooltip, display);
        }
    }
    return elapsed.count() / delaytime > 0.5;
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
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:
