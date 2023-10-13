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

#include "ui/widget/objects-dialog-cells.h"
#include "color-rgba.h"
#include "preferences.h"

namespace Inkscape {
namespace UI {
namespace Widget {

/**
 * A colored tag cell which indicates which layer an object is in.
 */
ColorTagRenderer::ColorTagRenderer() :
    Glib::ObjectBase(typeid(CellRenderer)),
    Gtk::CellRenderer(),
    _property_color(*this, "tagcolor", 0),
    _property_hover(*this, "taghover", false)
{
    property_mode() = Gtk::CellRendererMode::ACTIVATABLE;

    int dummy_width;
    // height size is not critical
    Gtk::IconSize::lookup(Gtk::ICON_SIZE_MENU, dummy_width, _height);
}

void ColorTagRenderer::render_vfunc(const Cairo::RefPtr<Cairo::Context>& cr, 
                      Gtk::Widget& widget,
                      const Gdk::Rectangle& background_area,
                      const Gdk::Rectangle& cell_area,
                      Gtk::CellRendererState flags) {
    cr->rectangle(cell_area.get_x(), cell_area.get_y(), cell_area.get_width(), cell_area.get_height());
    ColorRGBA color(_property_color.get_value());
    cr->set_source_rgb(color[0], color[1], color[2]);
    cr->fill();
    if (_property_hover.get_value()) {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        Glib::ustring themeiconname = prefs->getString("/theme/iconTheme", prefs->getString("/theme/defaultIconTheme", ""));
        guint32 colorsetbase = prefs->getUInt("/theme/" + themeiconname + "/symbolicBaseColor", 0x2E3436ff);
        double r = ((colorsetbase >> 24) & 0xFF) / 255.0;
        double g = ((colorsetbase >> 16) & 0xFF) / 255.0;
        double b = ((colorsetbase >> 8) & 0xFF) / 255.0;
        cr->set_source_rgba(r, g, b, 0.6);
        cr->rectangle(background_area.get_x() + 0.5, background_area.get_y() + 0.5, background_area.get_width() - 1.0, background_area.get_height() - 1.0);
        cr->set_line_width(1.0);
        cr->stroke();
    }
}

void ColorTagRenderer::get_preferred_width_vfunc(Gtk::Widget& widget, int& min_w, int& nat_w) const {
    min_w = nat_w = _width;
}

void ColorTagRenderer::get_preferred_height_vfunc(Gtk::Widget& widget, int& min_h, int& nat_h) const {
    min_h = 1;
    nat_h = _height;
}

bool ColorTagRenderer::activate_vfunc(GdkEvent* event, Gtk::Widget& /*widget*/, const Glib::ustring& path,
        const Gdk::Rectangle& /*background_area*/, const Gdk::Rectangle& /*cell_area*/,
        Gtk::CellRendererState /*flags*/) {
    _signal_clicked.emit(path);
    return false;
}

} // namespace Widget
} // namespace UI
} // namespace Inkscape

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


