// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Martin Owens
 *
 * Copyright (C) 2020 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_INKSCAPE_UI_DIALOG_SHAPEICON_H
#define SEEN_INKSCAPE_UI_DIALOG_SHAPEICON_H

#include <cstdint>
#include <optional>
#include <string>
#include <typeinfo>
#include <glibmm/property.h>
#include <glibmm/propertyproxy.h>
#include <gtkmm/cellrendererpixbuf.h>
#include <sigc++/functors/mem_fun.h>

namespace Inkscape::UI::Widget {

// Object overlay states usually modify the icon and indicate
// That there may be non-item children under this item (e.g. clip)
using OverlayState = int;
enum OverlayStates : OverlayState {
    OVERLAY_NONE = 0,     // Nothing special about the object.
    OVERLAY_CLIP = 1,     // Object has a clip
    OVERLAY_MASK = 2,     // Object has a mask
    OVERLAY_BOTH = 3,     // Object has both clip and mask
};

/// Custom cell renderer for shapes of items in Objects dialog, w/ optional clip/mask icon overlaid
class CellRendererItemIcon : public Gtk::CellRendererPixbuf {
public:
    CellRendererItemIcon() :
        Glib::ObjectBase{typeid(*this)},
        Gtk::CellRendererPixbuf{},
        _property_shape_type(*this, "shape_type", "unknown"),
        _property_color(*this, "color", 0),
        _property_clipmask(*this, "clipmask", 0)
    {
        property_mode() = Gtk::CellRendererMode::ACTIVATABLE;
        property_icon_size().set_value(Gtk::IconSize::NORMAL); // Previously ICON_SIZE_MENU

        set_icon_name();
        auto const set = sigc::mem_fun(*this, &CellRendererItemIcon::set_icon_name);
        property_shape_type().signal_changed().connect(set);
        property_color     ().signal_changed().connect(set);
    } 
     
    Glib::PropertyProxy<std::string> property_shape_type() {
        return _property_shape_type.get_proxy();
    }
    Glib::PropertyProxy<unsigned int> property_color() {
        return _property_color.get_proxy();
    }
    Glib::PropertyProxy<unsigned int> property_clipmask() {
        return _property_clipmask.get_proxy();
    }
  
    typedef sigc::signal<void (Glib::ustring)> type_signal_activated;

    type_signal_activated signal_activated() {
        return _signal_activated;
    }

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

    type_signal_activated _signal_activated;
    Glib::Property<std::string> _property_shape_type;
    Glib::Property<unsigned int> _property_color;
    Glib::Property<unsigned int> _property_clipmask;

    Glib::ustring _color_class;
    std::optional<std::uint32_t> _widget_color;
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_INKSCAPE_UI_DIALOG_SHAPEICON_H

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
