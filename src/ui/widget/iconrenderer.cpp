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

#include "ui/widget/iconrenderer.h"

#include <utility>
#include <sigc++/functors/mem_fun.h>

namespace Inkscape::UI::Widget {

IconRenderer::IconRenderer() :
    Glib::ObjectBase(typeid(IconRenderer)),
    Gtk::CellRendererPixbuf(),
    _property_icon(*this, "icon", 0)
{
    property_mode() = Gtk::CellRendererMode::ACTIVATABLE;
    property_icon_size().set_value(Gtk::IconSize::NORMAL); // Previously ICON_SIZE_BUTTON

    set_icon_name();
    property_icon().signal_changed().connect(sigc::mem_fun(*this, &IconRenderer::set_icon_name));
}

/*
 * Called when an icon is clicked.
 */
IconRenderer::type_signal_activated IconRenderer::signal_activated()
{
    return m_signal_activated;
}

void IconRenderer::get_preferred_height_vfunc(Gtk::Widget &widget,
                                              int &min_h,
                                              int &nat_h) const
{
    Gtk::CellRendererPixbuf::get_preferred_height_vfunc(widget, min_h, nat_h);

    if (min_h) {
        min_h += (min_h) >> 1;
    }
    
    if (nat_h) {
        nat_h += (nat_h) >> 1;
    }
}

void IconRenderer::get_preferred_width_vfunc(Gtk::Widget &widget,
                                             int &min_w,
                                             int &nat_w) const
{
    Gtk::CellRendererPixbuf::get_preferred_width_vfunc(widget, min_w, nat_w);

    if (min_w) {
        min_w += (min_w) >> 1;
    }
    
    if (nat_w) {
        nat_w += (nat_w) >> 1;
    }
}

bool IconRenderer::activate_vfunc(Glib::RefPtr<Gdk::Event const> const &/*event*/,
                                  Gtk::Widget &/*widget*/,
                                  const Glib::ustring &path,
                                  const Gdk::Rectangle &/*background_area*/,
                                  const Gdk::Rectangle &/*cell_area*/,
                                  Gtk::CellRendererState /*flags*/)
{
    m_signal_activated.emit(path);
    return true;
}

void IconRenderer::add_icon(Glib::ustring name)
{
    // If we add name for current index (especially 0), ensure we set :icon-name
    if (property_icon().get_value() == _icons.size()) {
        property_icon_name().set_value(name);
    }

    _icons.push_back(std::move(name));
}

void IconRenderer::set_icon_name()
{
    int icon_index = property_icon().get_value();
    if(icon_index >= 0 && icon_index < _icons.size()) {
        property_icon_name().set_value(_icons[icon_index]);
    } else {
        property_icon_name().set_value("image-missing");
    }
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
