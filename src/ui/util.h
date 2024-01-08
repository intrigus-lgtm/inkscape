// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Utility functions for UI
 *
 * Authors:
 *   Tavmjong Bah
 *   John Smith
 *
 * Copyright (C) 2013, 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef UI_UTIL_SEEN
#define UI_UTIL_SEEN

#include <cstddef> // size_t
#include <exception>
#include <type_traits>
#include <vector>

#include <pangomm/layout.h> // Pango::EllipsizeMode
#include <gdkmm/rgba.h>
#include <gtkmm/cellrenderer.h>
#include <gtkmm/enums.h>
#include <gtkmm/widget.h>

#include <2geom/affine.h>
#include <2geom/point.h>
#include <2geom/rect.h>

/*
 * Use these errors when building from glade files for graceful
 * fallbacks and prevent crashes from corrupt ui files.
 */
class UIBuilderError : public std::exception {};
class UIFileUnavailable : public UIBuilderError {};
class WidgetUnavailable : public UIBuilderError {};

namespace Cairo {
class Matrix;
class ImageSurface;
} // namespace Cairo

namespace Glib {
class ustring;
} // namespace Glib

namespace Gtk {
class Label;
class Widget;
} // namespace Gtk

Glib::ustring ink_ellipsize_text(Glib::ustring const &src, std::size_t maxlen);

void reveal_widget(Gtk::Widget *widget, bool show);

// check if widget in a container is actually visible
bool is_widget_effectively_visible(Gtk::Widget const *widget);

namespace Inkscape::UI {

void set_icon_sizes(Gtk::Widget *parent, int pixel_size);
void set_icon_sizes(GtkWidget *parent, int pixel_size);

/// Utility function to ensure correct sizing after adding child widgets.
void resize_widget_children(Gtk::Widget *widget);

void gui_warning(const std::string &msg, Gtk::Window * parent_window = nullptr);

/// Whether for_each_*() will continue or stop after calling Func per child.
enum class ForEachResult {_continue, _break};

/// Get a vector of the widgetʼs children, from get_first_child() through each get_next_sibling().
std::vector<Gtk::Widget *> get_children(Gtk::Widget &widget);
/// Get the widgetʼs child at the given position. Throws std::out_of_range if the index is invalid.
Gtk::Widget &get_nth_child(Gtk::Widget &widget, std::size_t index);
/// For each child in get_children(widget), call widget.remove(*child). May not cause delete child!
template <typename Widget> void remove_all_children(Widget &widget)
{
    for (auto const child: get_children(widget)) {
        widget.remove(*child);
    }
}

/// Call Func with a reference to each child of parent, until it returns _break.
/// Accessing children changes between GTK3 & GTK4, so best consolidate it here.
/// @param widget    The initial widget at the top of the hierarchy, to start at
/// @param func      The widget-testing predicate, returning whether to continue
/// @param plus_self Whether to call the predicate @a func on the initial widget
/// @param recurse   Whether to recurse also calling @a func for nested children
/// @return The first widget for which @a func returns _break or nullptr if none
template <typename Func>
Gtk::Widget *for_each_child(Gtk::Widget &widget, Func &&func,
                            bool const plus_self = false, bool const recurse = false,
                            int const level = 0)
{
    static_assert(std::is_invocable_r_v<ForEachResult, Func, Gtk::Widget &>);

    if (plus_self && func(widget) == ForEachResult::_break) return &widget;
    if (!recurse && level > 0) return nullptr;

    for (auto const child: get_children(widget)) {
        auto const descendant = for_each_child(*child, func, true, recurse, level + 1);
        if (descendant) return descendant;
    }

    return nullptr;
}

/// Like for_each_child() but also tests the initial widget & recurses through childrenʼs children.
/// @param widget    The initial widget at the top of the hierarchy, to start at
/// @param func      The widget-testing predicate, returning whether to continue
/// @return The first widget for which @a func returns _break or nullptr if none
template <typename Func>
Gtk::Widget *for_each_descendant(Gtk::Widget &widget, Func &&func)
{
    return for_each_child(widget, std::forward<Func>(func), true, true);
}

/// Call Func with a reference to successive parents, until Func returns _break.
template <typename Func>
Gtk::Widget *for_each_parent(Gtk::Widget &widget, Func &&func)
{
    static_assert(std::is_invocable_r_v<ForEachResult, Func, Gtk::Widget &>);
    for (auto parent = widget.get_parent(); parent; parent = parent->get_parent()) {
        if (func(*parent) == ForEachResult::_break) {
            return parent;
        }
    }
    return nullptr;
}

[[nodiscard]] Gtk::Widget *find_widget_by_name(Gtk::Widget &parent, Glib::ustring const &name);
[[nodiscard]] Gtk::Widget *find_focusable_widget(Gtk::Widget &parent);
[[nodiscard]] bool is_descendant_of(Gtk::Widget const &descendant, Gtk::Widget const &ancestor);
[[nodiscard]] bool contains_focus(Gtk::Widget &widget);

[[nodiscard]] int get_font_size(Gtk::Widget &widget);

// If max_width_chars is > 0, then the created Label has :max-width-chars set to
// that limit, the :ellipsize mode is set to the passed-in @a mode, & a ::query-
// tooltip handler is connected to show the label as the tooltip when ellipsized
void ellipsize(Gtk::Label &label, int max_width_chars, Pango::EllipsizeMode mode);

} // namespace Inkscape::UI

// Mix two RGBA colors using simple linear interpolation:
//  0 -> only a, 1 -> only b, x in 0..1 -> (1 - x)*a + x*b
Gdk::RGBA mix_colors(const Gdk::RGBA& a, const Gdk::RGBA& b, float ratio);

// Create the same color, but with a different opacity (alpha)
Gdk::RGBA change_alpha(const Gdk::RGBA& color, double new_alpha);

/// Calculate luminance of an RGBA color from its RGB in range 0 to 1 inclusive.
/// This uses the perceived brightness formula given at: https://www.w3.org/TR/AERT/#color-contrast
double get_luminance(const Gdk::RGBA &color);

// Get CSS color for a Widget, based on its current state & a given CSS class.
// N.B.!! Big GTK devs donʼt think changing classes should work ‘within a frame’
// …but it does… & GTK3 GtkCalendar does that – so keep doing it, till we canʼt!
Gdk::RGBA get_color_with_class(Gtk::Widget &widget,
                               Glib::ustring const &css_class);

guint32 to_guint32(Gdk::RGBA const &rgba);
Gdk::RGBA to_rgba(guint32 const u32);

// convert Gdk::RGBA into 32-bit rrggbbaa color, optionally replacing alpha, if specified
uint32_t conv_gdk_color_to_rgba(const Gdk::RGBA& color, double replace_alpha = -1);

Geom::IntRect cairo_to_geom(const Cairo::RectangleInt &rect);
Cairo::RectangleInt geom_to_cairo(const Geom::IntRect &rect);
Cairo::Matrix geom_to_cairo(const Geom::Affine &affine);
Geom::IntPoint dimensions(const Cairo::RefPtr<Cairo::ImageSurface> &surface);
Geom::IntPoint dimensions(const Gdk::Rectangle &allocation);

// create a gradient with multiple steps to approximate profile described by given cubic spline
Cairo::RefPtr<Cairo::LinearGradient> create_cubic_gradient(
    Geom::Rect rect,
    const Gdk::RGBA& from,
    const Gdk::RGBA& to,
    Geom::Point ctrl1,
    Geom::Point ctrl2,
    Geom::Point p0 = Geom::Point(0, 0),
    Geom::Point p1 = Geom::Point(1, 1),
    int steps = 8
);

// If on Windows, get the native window & set it to DWMA_USE_IMMERSIVE_DARK_MODE
void set_dark_titlebar(Glib::RefPtr<Gdk::Surface> const &surface, bool is_dark);

// Cover for Glib::wrap not passing through const.
template <typename T>
auto const_wrap(T const *p, bool take_copy = false)
{
    auto unconst_p = const_cast<T*>(p);
    auto unconst_wrapped = Glib::wrap(unconst_p, take_copy);
    using wrapped_T = typename decltype(unconst_wrapped)::element_type;
    return std::shared_ptr<wrapped_T const>(std::move(unconst_wrapped));
}

/**
 * Convert an image surface in ARGB32 format to a texture.
 * The texture shares data with the surface, so the surface shouldn't modified afterwards.
 */
Glib::RefPtr<Gdk::Texture> to_texture(Cairo::RefPtr<Cairo::Surface> const &surface);

#endif // UI_UTIL_SEEN

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
