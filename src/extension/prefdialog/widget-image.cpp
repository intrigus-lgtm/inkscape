// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Image widget for extensions
 *//*
 * Authors:
 *   Patrick Storz <eduard.braun2@gmx.de>
 *
 * Copyright (C) 2019 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "widget-image.h"

#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <gdkmm/pixbuf.h>
#include <gtkmm/image.h>

#include "xml/node.h"
#include "extension/extension.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"

namespace Inkscape::Extension {

WidgetImage::WidgetImage(Inkscape::XML::Node *xml, Inkscape::Extension::Extension *ext)
    : InxWidget(xml, ext)
{
    std::string image_path;

    // get path to image
    const char *content = nullptr;
    if (xml->firstChild()) {
        content = xml->firstChild()->content();
    }
    if (content) {
        image_path = content;
    } else {
        g_warning("Missing path for image widget in extension '%s'.", _extension->get_id());
        return;
    }

    // make sure path is absolute (relative paths are relative to .inx file's location)
    if (!Glib::path_is_absolute(image_path)) {
        image_path = Glib::build_filename(_extension->get_base_directory(), image_path);
    }

    // check if image exists
    if (Glib::file_test(image_path, Glib::FileTest::IS_REGULAR)) {
        _image_path = image_path;
    } else {
        _icon_name = INKSCAPE_ICON(image_path);
        if (_icon_name.empty()) {
            g_warning("Image file ('%s') not found for image widget in extension '%s'.",
                      image_path.c_str(), _extension->get_id());
        }
    }

    // parse width/height attributes
    const char *width = xml->attribute("width");
    const char *height = xml->attribute("height");
    if (width && height) {
        _width = strtoul(width, nullptr, 0);
        _height = strtoul(height, nullptr, 0);
    }
}

/** \brief  Create a label for the description */
Gtk::Widget *WidgetImage::get_widget(sigc::signal<void ()> * /*changeSignal*/)
{
    if (_hidden || (_image_path.empty() && _icon_name.empty())) {
        return nullptr;
    }

    Gtk::Image *image = nullptr;
    if (!_image_path.empty()) {
        // resize if requested
        if (_width && _height) {
            auto pixbuf = Gdk::Pixbuf::create_from_file(_image_path);
            pixbuf = pixbuf->scale_simple(_width, _height, Gdk::InterpType::BILINEAR);
            image = Gtk::make_managed<Gtk::Image>(pixbuf);
        } else {
            image = Gtk::make_managed<Gtk::Image>(_image_path);
        }
    } else if (_width || _height) {
        image = sp_get_icon_image(_icon_name, std::max(_width, _height));
    } else {
        image = sp_get_icon_image(_icon_name, Gtk::IconSize::LARGE);
    }

    image->set_visible(true);
    return image;
}

} // namespace Inkscape::Extension

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
