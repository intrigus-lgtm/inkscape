// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Mike Kowalski
 *   Martin Owens
 *
 * Copyright (C) 2021-2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_UI_WIDGET_OBJECTS_CELLS_H
#define SEEN_UI_WIDGET_OBJECTS_CELLS_H

#include <gtkmm/cellrenderer.h>
#include <glibmm/property.h>
#include <glibmm/refptr.h>

namespace Gdk {
class Event;
} // namespace Gdk

namespace Gtk {
class Snapshot;
class Widget;
} // namespace Gtk

namespace Inkscape::UI::Widget {

class ColorTagRenderer : public Gtk::CellRenderer {
public:
    ColorTagRenderer();

    Glib::PropertyProxy<unsigned int> property_color() {
        return _property_color.get_proxy();
    }
    Glib::PropertyProxy<bool> property_hover() {
        return _property_hover.get_proxy();
    }
    sigc::signal<void (const Glib::ustring&)> signal_clicked() {
        return _signal_clicked;
    }
    
    int get_width() const { return _width; }

private:
    void snapshot_vfunc(Glib::RefPtr<Gtk::Snapshot> const &snapshot,
                        Gtk::Widget &widget,
                        const Gdk::Rectangle& background_area,
                        const Gdk::Rectangle& cell_area,
                        Gtk::CellRendererState flags ) override;

    void get_preferred_width_vfunc(Gtk::Widget& widget, int& min_w, int& nat_w) const override;
    void get_preferred_height_vfunc(Gtk::Widget& widget, int& min_h, int& nat_h) const override;

    bool activate_vfunc(Glib::RefPtr<Gdk::Event const> const &event,
                        Gtk::Widget &widget,
                        const Glib::ustring &path,
                        const Gdk::Rectangle &background_area,
                        const Gdk::Rectangle &cell_area,
                        Gtk::CellRendererState flags) override;

    int _width = 8;
    int _height;
    Glib::Property<unsigned int> _property_color;
    Glib::Property<bool> _property_hover;
    sigc::signal<void (const Glib::ustring&)> _signal_clicked;
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_UI_WIDGET_OBJECTS_CELLS_H

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
