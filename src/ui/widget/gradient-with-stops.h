// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * Gradient image widget with stop handles
 */
/*
 * Author:
 *   Michael Kowalski
 *
 * Copyright (C) 2020-2021 Michael Kowalski
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_GRADIENT_WITH_STOPS_H
#define SEEN_GRADIENT_WITH_STOPS_H

#include <glibmm/refptr.h>
#include <gdkmm/rgba.h>
#include <gtk/gtk.h> // GtkEventControllerKey|Motion
#include <gtkmm/box.h>
#include <gtkmm/gesture.h> // Gtk::EventSequenceState
#include <sigc++/signal.h>

#include "helper/auto-connection.h"
#include "ui/svg-renderer.h"

namespace Gdk {
class Cursor;
} // namespace Gdk

namespace Gtk {
class DrawingArea;
class GestureMultiPress;
} // namespace Gtk

class SPGradient;

namespace Inkscape::UI::Widget {

// Box because GTK3 does not bother applying CSS bits like min-width|height on DrawingArea
// TODO: GTK4: Revisit whether that is still the case; hopefully it isnʼt, then just be DrawingArea
class GradientWithStops : public Gtk::Box {
public:
    GradientWithStops();
    ~GradientWithStops() override;

    // gradient to draw or nullptr
    void set_gradient(SPGradient* gradient);

    // set selected stop handle (or pass -1 to deselect)
    void set_focused_stop(int index);

    // stop has been selected
    sigc::signal<void (size_t)>& signal_stop_selected() {
        return _signal_stop_selected;
    }

    // request to change stop's offset
    sigc::signal<void (size_t, double)>& signal_stop_offset_changed() {
        return _signal_stop_offset_changed;
    }

    sigc::signal<void (double)>& signal_add_stop_at() {
        return _signal_add_stop_at;
    }

    sigc::signal<void (size_t)>& signal_delete_stop() {
        return _signal_delete_stop;
    }

private:
    bool on_drawing_area_draw(Cairo::RefPtr<Cairo::Context> const &cr);
    void on_style_updated() final;
    Gtk::EventSequenceState on_click_pressed (Gtk::GestureClick const &click,
                                              int n_press, double x, double y);
    Gtk::EventSequenceState on_click_released(Gtk::GestureClick const &click,
                                              int n_press, double x, double y);
    void on_motion(GtkEventControllerMotion const *motion, double x, double y);
    bool on_key_pressed(GtkEventControllerKey const *controller,
                        unsigned keyval, unsigned keycode, GdkModifierType state);
    bool on_focus(Gtk::DirectionType direction) final;
    void on_drawing_area_has_focus();

    void modified();

    // repaint widget
    void update();

    // index of gradient stop handle under (x, y) or -1
    int find_stop_at(double x, double y) const;

    // request stop move
    void move_stop(int stop_index, double offset_shift);

    // layout of gradient image/editor
    struct layout_t {
        double x, y, width, height;
    };
    layout_t get_layout() const;

    // position of single gradient stop handle
    struct stop_pos_t {
        double left, tip, right, top, bottom;
    };
    stop_pos_t get_stop_position(size_t index, const layout_t& layout) const;

    struct limits_t {
        double min_offset, max_offset, offset;
    };
    limits_t get_stop_limits(int index) const;

    Glib::RefPtr<Gdk::Cursor> const *get_cursor(double x, double y) const;
    void set_cursor(Glib::RefPtr<Gdk::Cursor> const *cursor);

    Gtk::DrawingArea *_drawing_area;
    SPGradient* _gradient = nullptr;

    struct stop_t {
        double offset;
        SPColor color;
        double opacity;
    };
    std::vector<stop_t> _stops;

    // handle stop SVG template
    svg_renderer _template;

    // selected handle indicator
    svg_renderer _tip_template;

    auto_connection _release;
    auto_connection _modified;
    Gdk::RGBA _background_color;
    sigc::signal<void (size_t)> _signal_stop_selected;
    sigc::signal<void (size_t, double)> _signal_stop_offset_changed;
    sigc::signal<void (double)> _signal_add_stop_at;
    sigc::signal<void (size_t)> _signal_delete_stop;
    bool _dragging = false;

    // index of handle stop that user clicked; may be out of range
    int _focused_stop = -1;

    double _pointer_x = 0;
    double _stop_offset = 0;
    Glib::RefPtr<Gdk::Cursor> _cursor_mouseover;
    Glib::RefPtr<Gdk::Cursor> _cursor_dragging;
    Glib::RefPtr<Gdk::Cursor> _cursor_insert;
    Glib::RefPtr<Gdk::Cursor> const *_cursor_current = nullptr;

    // TODO: customize this amount or read prefs
    double _stop_move_increment = 0.01;
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_GRADIENT_WITH_STOPS_H

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
