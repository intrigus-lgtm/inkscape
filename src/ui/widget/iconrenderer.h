// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Theodore Janeczko
 *
 * Copyright (C) Theodore Janeczko 2012 <flutterguy317@gmail.com>
 *               Martin Owens 2018 <doctormo@gmail.com>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_UI_WIDGET_ICONRENDERER_H
#define SEEN_UI_WIDGET_ICONRENDERER_H

#include <vector>
#include <glibmm/property.h>
#include <glibmm/propertyproxy.h>
#include <glibmm/ustring.h>
#include <gtkmm/cellrendererpixbuf.h>
#include <sigc++/signal.h>

namespace Inkscape::UI::Widget {

class IconRenderer : public Gtk::CellRendererPixbuf {
public:
    IconRenderer();

    Glib::PropertyProxy<int> property_icon() { return _property_icon.get_proxy(); }

    void add_icon(Glib::ustring name);

    typedef sigc::signal<void (Glib::ustring)> type_signal_activated;
    type_signal_activated signal_activated();

private:
    type_signal_activated m_signal_activated;

    void get_preferred_width_vfunc(Gtk::Widget &widget,
                                   int &min_w,
                                   int &nat_w) const override;
    
    void get_preferred_height_vfunc(Gtk::Widget &widget,
                                    int &min_h,
                                    int &nat_h) const override;

    bool activate_vfunc(Glib::RefPtr<Gdk::Event const> const &event,
                        Gtk::Widget &widget,
                        const Glib::ustring &path,
                        const Gdk::Rectangle &background_area,
                        const Gdk::Rectangle &cell_area,
                        Gtk::CellRendererState flags) override;
    
    Glib::Property<int> _property_icon;
    std::vector<Glib::ustring> _icons;
    void set_icon_name();
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_UI_WIDGET_ICONRENDERER_H

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
