// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Ruler widget. Indicates horizontal or vertical position of a cursor in a specified widget.
 *
 * Copyright (C) 2019 Tavmjong Bah
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 *
 */

#ifndef INK_RULER_H
#define INK_RULER_H

/* Rewrite of the C Ruler. */

#include <memory>
#include <unordered_map>
#include <utility>
#include <cairomm/refptr.h>
#include <cairomm/types.h> // Cairo::RectangleInt
#include <pangomm/fontdescription.h>
#include <gdkmm/rgba.h>
#include <gtk/gtk.h> // GtkEventControllerMotion
#include <gtkmm/box.h>
#include <gtkmm/enums.h> // Gtk::Orientation
#include <gtkmm/gesture.h> // Gtk::EventSequenceState

#include "preferences.h"
#include "ui/widget/css-changed-class-init.h"

namespace Cairo {
class Context;
} // namespace Cairo

namespace Gtk {
class DrawingArea;
class GestureClick;
class Popover;
} // namespace Gtk

namespace Inkscape::Util {
class Unit;
} // namespace Inkscape::Util

namespace Inkscape::UI::Widget {
  
// Box because GTK3 does not bother applying CSS bits like border-*|min-width|height on DrawingArea
// TODO: GTK4: Revisit whether that is still the case; hopefully it isnʼt, then just be DrawingArea
class Ruler
    : public CssChangedClassInit
    , public Gtk::Box
{
public:
    Ruler(Gtk::Orientation orientation);

    void set_unit(Inkscape::Util::Unit const *unit);
    void set_range(double lower, double upper);
    void set_page(double lower, double upper);
    void set_selection(double lower, double upper);

    void add_track_widget(Gtk::Widget& widget);

private:
    std::pair<int, int> get_drawing_size();
    bool draw_scale(const Cairo::RefPtr<::Cairo::Context>& cr);
    void draw_marker(const Cairo::RefPtr<::Cairo::Context>& cr);
    Cairo::RectangleInt marker_rect();
    void draw_func(Cairo::RefPtr<Cairo::Context> const &cr, int width, int height);
    void css_changed(GtkCssStyleChange *) final;
    void on_prefs_changed();

    void on_motion(GtkEventControllerMotion const *motion, double x, double y);
    Gtk::EventSequenceState on_click_pressed(Gtk::GestureClick const &click,
                                             int n_press, double x, double y);

    void set_context_menu();
    Cairo::RefPtr<Cairo::Surface> draw_label(Cairo::RefPtr<Cairo::Surface> const &surface_in, int label_value);

    Gtk::DrawingArea *_drawing_area;
    Inkscape::PrefObserver _watch_prefs;
    std::unique_ptr<Gtk::Popover> _popover;
    Gtk::Orientation    _orientation;
    Inkscape::Util::Unit const* _unit;
    double _lower;
    double _upper;
    double _position;
    double _max_size;

    // Page block
    double _page_lower = 0.0;
    double _page_upper = 0.0;

    // Selection block
    double _sel_lower = 0.0;
    double _sel_upper = 0.0;
    double _sel_visible = true;

    bool   _backing_store_valid;
    Cairo::RefPtr<::Cairo::Surface> _backing_store;
    Cairo::RectangleInt _rect;

    std::unordered_map<int, Cairo::RefPtr<::Cairo::Surface>> _label_cache;

    // Cached style properties
    Gdk::RGBA _shadow;
    Gdk::RGBA _foreground;
    int _font_size;
    Gdk::RGBA _page_fill;
    Gdk::RGBA _select_fill;
    Gdk::RGBA _select_stroke;
};

} // namespace Inkscape::UI::Widget

#endif // INK_RULER_H

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
