// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Jon A. Cruz
 *   Johan B. C. Engelen
 *
 * Copyright (C) 2006-2008 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_UI_DIALOG_IMAGETOGGLER_H
#define SEEN_UI_DIALOG_IMAGETOGGLER_H

#include <string>
#include <gtkmm/cellrendererpixbuf.h>
#include <glibmm/property.h>
#include <glibmm/propertyproxy.h>
#include <sigc++/signal.h>

namespace Inkscape::UI::Widget {

class ImageToggler : public Gtk::CellRendererPixbuf {
public:
    ImageToggler(char const *on, char const *off);

    sigc::signal<void (const Glib::ustring&)> signal_toggled() { return _signal_toggled;}

    Glib::PropertyProxy<bool> property_active() { return _property_active.get_proxy(); }
    Glib::PropertyProxy<bool> property_activatable() { return _property_activatable.get_proxy(); }
    Glib::PropertyProxy<bool> property_gossamer() { return _property_gossamer.get_proxy(); }
    Glib::PropertyProxy<std::string> property_active_icon() { return _property_active_icon.get_proxy(); }

    /// Sets whether to force visible icons in ALL cells of the column, EVEN IF their activatable &
    /// property_active() properties are false. The ObjectsPanel uses this to show all blend icons.
    void set_force_visible(bool force_visible);

private:
    void set_icon_name();
    void snapshot_vfunc(Glib::RefPtr<Gtk::Snapshot> const &snapshot,
                        Gtk::Widget &widget,
                        const Gdk::Rectangle &background_area,
                        const Gdk::Rectangle &cell_area,
                        Gtk::CellRendererState flags) override;

    bool activate_vfunc(Glib::RefPtr<Gdk::Event const> const &event,
                        Gtk::Widget &widget,
                        const Glib::ustring &path,
                        const Gdk::Rectangle &background_area,
                        const Gdk::Rectangle &cell_area,
                        Gtk::CellRendererState flags) override;

    Glib::ustring _pixOnName;
    Glib::ustring _pixOffName;
    bool _force_visible = false;
    Glib::Property<bool> _property_active;
    Glib::Property<bool> _property_activatable;
    Glib::Property<bool> _property_gossamer;
    Glib::Property<std::string> _property_active_icon;

    sigc::signal<void (const Glib::ustring&)> _signal_toggled;
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_UI_DIALOG_IMAGETOGGLER_H

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
