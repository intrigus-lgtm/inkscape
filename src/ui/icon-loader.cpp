// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Icon Loader
 *//*
 * Authors:
 * see git history
 * Jabiertxo Arraiza <jabier.arraiza@marker.es>
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "icon-loader.h"

#include <unordered_map>
#include <giomm/themedicon.h>
#include <gdkmm/display.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/image.h>
#include <gtkmm/styleprovider.h>
#include <gtkmm/window.h>

#include "desktop.h"
#include "inkscape.h"

Gtk::Image *sp_get_icon_image(Glib::ustring const &icon_name, int size)
{
    Gtk::Image *icon = new Gtk::Image();
    icon->set_from_icon_name(icon_name);
    icon->set_pixel_size(size);
    return icon;
}

Gtk::Image *sp_get_icon_image(Glib::ustring const &icon_name, Gtk::IconSize icon_size)
{
    Gtk::Image *icon = new Gtk::Image();
    icon->set_from_icon_name(icon_name);
    icon->set_icon_size(icon_size);
    return icon;
}

GtkWidget *sp_get_icon_image(Glib::ustring const &icon_name, GtkIconSize icon_size)
{
    auto const widget = gtk_image_new_from_icon_name(icon_name.c_str());
    gtk_image_set_icon_size(GTK_IMAGE(widget), icon_size);
    return widget;
}

namespace Inkscape::UI {

// Maintain a map of every color requested to a CSS class that will apply it
[[nodiscard]] static Glib::ustring const &get_color_class(std::uint32_t const rgba_color,
                                                          Glib::RefPtr<Gdk::Display> const &display)
{
    static std::unordered_map<std::uint32_t, Glib::ustring> color_classes;
    auto &color_class = color_classes[rgba_color];
    if (!color_class.empty()) return color_class;

    // The CSS class is .icon-color-RRGGBBAA
    std::string hex(9, '\0');
    std::snprintf(hex.data(), 9, "%08X", rgba_color);
    color_class = Glib::ustring::compose("icon-color-%1", hex);
    // GTK CSS does not support #RRGGBBAA, so we split it
    hex.resize(6);
    auto const opacity = (rgba_color & 0xFF) / 255.0;
    // Add a persistent CSS provider for that class+color
    auto const css_provider = Gtk::CssProvider::create();
    auto const data = Glib::ustring::compose(
        ".symbolic .%1, .regular .%1 { -gtk-icon-style: symbolic; color: #%2; opacity: %3; }",
        color_class, hex, opacity);
    css_provider->load_from_data(data);
    // Add it with the needed priority = higher than themes.cpp _colorizeprovider
    static constexpr auto priority = GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1;
    Gtk::StyleProvider::add_provider_for_display(display, css_provider, priority);
    return color_class;
}

/**
 * Get the shape icon for this named shape type. For example 'rect'. These icons
 * are always symbolic icons no matter the theme in order to be coloured by the highlight
 * color.
 *
 * This function returns a struct containing the icon name you should use in a
 * GtkImage/GtkCellRenderer, & a CSS class that will apply the requested color.
 *
 * @param shape_type - A string id for the shape from SPItem->typeName()
 * @param rgba_color - The fg color of the shape icon, in 32-bit unsigned int RGBA format.
 */
GetShapeIconResult get_shape_icon(Glib::ustring const &shape_type, std::uint32_t const rgba_color)
{
    Glib::RefPtr<Gdk::Display> display = Gdk::Display::get_default();
    auto const icon_theme = Gtk::IconTheme::get_for_display(display);

    auto icon_name = Glib::ustring::compose("shape-%1-symbolic", shape_type);
    if (!icon_theme->has_icon(icon_name)) {
        icon_name = Glib::ustring::compose("%1-symbolic", shape_type);
        if (!icon_theme->has_icon(icon_name)) {
            icon_name = "shape-unknown-symbolic";
        }
    }

    return {std::move(icon_name), get_color_class(rgba_color, display)};
}

/// As get_shape_icon(), but returns a ready-made, managed Image having that icon name & CSS class.
Gtk::Image *get_shape_image(Glib::ustring const &shape_type, std::uint32_t const rgba_color,
                            Gtk::IconSize const icon_size)
{
    auto const [icon_name, color_class] = get_shape_icon(shape_type, rgba_color);
    auto const icon = Gio::ThemedIcon::create(icon_name);
    auto const image = Gtk::make_managed<Gtk::Image>(icon);
    image->set_icon_size(icon_size);
    image->add_css_class(color_class);
    return image;
}

} // namespace Inkscape::UI

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
