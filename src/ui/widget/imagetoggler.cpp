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

#include "ui/widget/imagetoggler.h"

#include <gtkmm/snapshot.h>
#include <sigc++/functors/mem_fun.h>

namespace Inkscape::UI::Widget {

ImageToggler::ImageToggler(char const *on, char const *off) :
    Glib::ObjectBase(typeid(ImageToggler)),
    Gtk::CellRendererPixbuf{},
    _pixOnName(on),
    _pixOffName(off),
    _property_active(*this, "active", false),
    _property_activatable(*this, "activatable", true),
    _property_gossamer(*this, "gossamer", false),
    _property_active_icon(*this, "active_icon", "")
{
    property_mode() = Gtk::CellRendererMode::ACTIVATABLE;
    property_icon_size().set_value(Gtk::IconSize::NORMAL); // Previously ICON_SIZE_MENU
    set_padding(6, 3);

    auto const set_icon = sigc::mem_fun(*this, &ImageToggler::set_icon_name);
    property_active     ().signal_changed().connect(set_icon);
    property_active_icon().signal_changed().connect(set_icon);
}

void ImageToggler::set_force_visible(bool const force_visible)
{
    _force_visible = force_visible;
}

void ImageToggler::set_icon_name()
{
    Glib::ustring icon_name;
    if (_property_active.get_value()) {
        icon_name = _property_active_icon.get_value();
        if (icon_name.empty()) icon_name = _pixOnName;
    } else {
        icon_name = _pixOffName;
    }
    property_icon_name().set_value(icon_name);
}

void ImageToggler::snapshot_vfunc(Glib::RefPtr<Gtk::Snapshot> const &snapshot,
                                  Gtk::Widget &widget,
                                  const Gdk::Rectangle &background_area,
                                  const Gdk::Rectangle &cell_area,
                                  Gtk::CellRendererState flags)
{
    // Hide when not being used.
    double alpha = 1.0;
    bool visible = _property_activatable.get_value()
                || _property_active.get_value()
                || _force_visible;
    if (!visible) {
        // XXX There is conflict about this value, some users want 0.2, others want 0.0
        alpha = 0.0;
    }
    if (!visible && _property_gossamer.get_value()) {
        alpha += 0.2;
    }
    if (alpha <= 0.0) {
        return;
    }

    // Apply alpha to output of Gtk::CellRendererPixbuf, plus x offset to replicate prev behaviour.
    snapshot->push_opacity(alpha);
    auto const point = GRAPHENE_POINT_INIT(-0.5f * property_xpad().get_value(), 0);
    gtk_snapshot_translate(snapshot->gobj(), &point);
    Gtk::CellRendererPixbuf::snapshot_vfunc(snapshot, widget, background_area, cell_area, flags);
    snapshot->pop();
}

bool
ImageToggler::activate_vfunc(Glib::RefPtr<Gdk::Event const> const &/*event*/,
                             Gtk::Widget &/*widget*/,
                             const Glib::ustring &path,
                             const Gdk::Rectangle &/*background_area*/,
                             const Gdk::Rectangle &/*cell_area*/,
                             Gtk::CellRendererState /*flags*/)
{
    _signal_toggled.emit(path);
    return false;
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
