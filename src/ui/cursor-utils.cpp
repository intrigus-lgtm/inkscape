// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cursor utilities
 *
 * Copyright (C) 2020 Tavmjong Bah
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 *
 */

#include "cursor-utils.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <memory>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <boost/functional/hash.hpp>
#include <glibmm/miscutils.h>
#include <giomm/file.h>
#include <gdkmm/cursor.h>
#include <gdkmm/pixbuf.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/settings.h>
#include <gtkmm/widget.h>

#include "document.h"
#include "preferences.h"
#include "display/cairo-utils.h"
#include "helper/pixbuf-ops.h"
#include "io/file.h"
#include "io/resource.h"
#include "object/sp-object.h"
#include "object/sp-root.h"
#include "util/units.h"

using Inkscape::IO::Resource::SYSTEM;
using Inkscape::IO::Resource::ICONS;

namespace Inkscape {

// SVG cursor unique ID/key
typedef std::tuple<std::string, std::string, std::string, std::uint32_t, std::uint32_t, double, double, bool, int> Key;

struct KeyHasher {
    std::size_t operator () (const Key& k) const { return boost::hash_value(k); }
};

/**
 * Loads an SVG cursor from the specified file name.
 *
 * Returns pointer to cursor (or null cursor if we could not load a cursor).
 */
Glib::RefPtr<Gdk::Cursor>
load_svg_cursor(Gtk::Widget &widget,
                std::string const &file_name,
                std::uint32_t const fill,
                std::uint32_t const stroke,
                double fill_opacity,
                double stroke_opacity)
{
    // GTK puts cursors in a "cursors" subdirectory of icon themes. We'll do the same... but
    // note that we cannot use the normal GTK method for loading cursors as GTK knows nothing
    // about scalable SVG cursors. We must locate and load the files ourselves. (Even if
    // GTK could handle scalable cursors, we would need to load the files ourselves inorder
    // to modify CSS 'fill' and 'stroke' properties.)

    Glib::RefPtr<Gdk::Cursor> cursor;

    // Make list of icon themes, highest priority first.
    std::vector<std::string> theme_names;

    // Set in preferences
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring theme_name = prefs->getString("/theme/iconTheme", prefs->getString("/theme/defaultIconTheme", ""));
    if (!theme_name.empty()) {
        theme_names.push_back(std::move(theme_name));
    }

    // System
    theme_name = Gtk::Settings::get_default()->property_gtk_icon_theme_name();
    theme_names.push_back(std::move(theme_name));

    // Our default
    theme_names.emplace_back("hicolor");

    // quantize opacity to limit number of cursor variations we generate
    fill_opacity   = std::floor(std::clamp(fill_opacity,   0.0, 1.0) * 100) / 100;
    stroke_opacity = std::floor(std::clamp(stroke_opacity, 0.0, 1.0) * 100) / 100;

    const auto enable_drop_shadow = prefs->getBool("/options/cursor-drop-shadow", true);

    // Find the rendered size of the icon.
    int scale = 1;
    // cursor scaling? note: true by default - this has to be in sync with inkscape-preferences where it is true
    bool cursor_scaling = prefs->getBool("/options/cursorscaling", true); // Fractional scaling is broken but we can't detect it.
    if (cursor_scaling) {
        scale = widget.get_scale_factor(); // Adjust for HiDPI screens.
    }

    static std::unordered_map<Key, Glib::RefPtr<Gdk::Cursor>, KeyHasher> cursor_cache;
    Key cursor_key;

    const auto cache_enabled = prefs->getBool("/options/cache_svg_cursors", true);
    if (cache_enabled) {
        // construct a key
        cursor_key = std::tuple{theme_names[0], theme_names[1], file_name,
                                fill, stroke, fill_opacity, stroke_opacity,
                                enable_drop_shadow, scale};
        if (auto const it = cursor_cache.find(cursor_key); it != cursor_cache.end()) {
            return it->second;
        }
    }

    // Find theme paths.
    auto const icon_theme = Gtk::IconTheme::get_for_display(widget.get_display());
    auto theme_paths = icon_theme->get_search_path();

    // Loop over theme names and paths, looking for file.
    Glib::RefPtr<Gio::File> file;
    std::string full_file_path;
    bool file_exists = false;
    for (auto const &theme_name : theme_names) {
        for (auto const &theme_path : theme_paths) {
            full_file_path = Glib::build_filename(theme_path, theme_name, "cursors", file_name);
            // std::cout << "Checking: " << full_file_path << std::endl;
            file = Gio::File::create_for_path(full_file_path);
            file_exists = file->query_exists();
            if (file_exists) break;
        }
        if (file_exists) break;
    }

    if (!file->query_exists()) {
        std::cerr << "load_svg_cursor: Cannot locate cursor file: " << file_name << std::endl;
        return cursor;
    }

    bool cancelled = false;
    std::unique_ptr<SPDocument> document;
    document.reset(ink_file_open(file, &cancelled));

    if (!document) {
        std::cerr << "load_svg_cursor: Could not open document: " << full_file_path << std::endl;
        return cursor;
    }

    SPRoot *root = document->getRoot();
    if (!root) {
        std::cerr << "load_svg_cursor: Could not find SVG element: " << full_file_path << std::endl;
        return cursor;
    }

    // Set the CSS 'fill' and 'stroke' properties on the SVG element (for cascading).
    SPCSSAttr *css = sp_repr_css_attr(root->getRepr(), "style");

    std::stringstream fill_stream;
    fill_stream << "#"
                << std::setfill ('0') << std::setw(6)
                << std::hex << (fill >> 8);
    std::stringstream stroke_stream;
    stroke_stream << "#"
                  << std::setfill ('0') << std::setw(6)
                  << std::hex << (stroke >> 8);

    sp_repr_css_set_property(css, "fill",   fill_stream.str().c_str());
    sp_repr_css_set_property(css, "stroke", stroke_stream.str().c_str());
    sp_repr_css_set_property_double(css, "fill-opacity",   fill_opacity);
    sp_repr_css_set_property_double(css, "stroke-opacity", stroke_opacity);
    root->changeCSS(css, "style");
    sp_repr_css_attr_unref(css);

    if (!enable_drop_shadow) {
        // turn off drop shadow, if any
        Glib::ustring shadow("drop-shadow");
        auto objects = document->getObjectsByClass(shadow);
        for (auto&& el : objects) {
            if (auto val = el->getAttribute("class")) {
                Glib::ustring cls = val;
                auto pos = cls.find(shadow);
                if (pos != Glib::ustring::npos) {
                    cls.erase(pos, shadow.length());
                }
                el->setAttribute("class", cls);
            }
        }
    }

    // Check for maximum size
    // int mwidth = 0;
    // int mheight = 0;
    // display->get_maximal_cursor_size(mwidth, mheight);
    // int normal_size = display->get_default_cursor_size();

    auto w = document->getWidth().value("px");
    auto h = document->getHeight().value("px");
    // Calculate the hotspot.
    int hotspot_x = root->getIntAttribute("inkscape:hotspot_x", 0); // Do not include surface scale factor!
    int hotspot_y = root->getIntAttribute("inkscape:hotspot_y", 0);

    Geom::Rect area(0, 0, cursor_scaling ? w * scale : w, cursor_scaling ? h * scale : h);
    int dpi = 96 * scale;
    // render document into internal bitmap; returns null on failure
    if (auto ink_pixbuf = std::unique_ptr<Inkscape::Pixbuf>(sp_generate_internal_bitmap(document.get(), area, dpi))) {
        auto pixbuf = Glib::wrap(ink_pixbuf->getPixbufRaw(), true);

        if (cursor_scaling) {
            // creating cursor from Cairo surface rather than pixbuf gives us opportunity to set device scaling;
            // what that means in practice is we can prepare high-res image and it will be used as-is on
            // a high-res display; cursors created from pixbuf are up-scaled to device pixels (blurry)
            // TODO: GTK4: Gdk::Cursor() cannot be created from Cairo::Surface,
            //             only Gdk::Texture, which has no scaling. What to do?
            pixbuf->scale_simple(w * scale, h * scale, Gdk::InterpType::BILINEAR);
        }

        auto texture = Gdk::Texture::create_for_pixbuf(std::move(pixbuf));
        cursor = Gdk::Cursor::create(std::move(texture), hotspot_x, hotspot_y);
    } else {
        std::cerr << "load_svg_cursor: failed to create pixbuf for: " << full_file_path << std::endl;
    }

    document.reset();

    if (cache_enabled) {
        cursor_cache[std::move(cursor_key)] = cursor;
    }

    return cursor;
}

/**
 * Loads an SVG cursor from the specified file name, and sets it as the cursor
 * of the given widget.
 */
void
set_svg_cursor(Gtk::Widget &widget,
                std::string const &file_name,
                std::uint32_t const fill,
                std::uint32_t const stroke,
                double fill_opacity,
                double stroke_opacity)
{
    auto cursor = load_svg_cursor(widget, file_name, fill, stroke, fill_opacity, stroke_opacity);
    widget.set_cursor(std::move(cursor));
}

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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
