// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Inkscape canvas widget.
 */
/*
 * Authors:
 *   Tavmjong Bah
 *   PBS <pbs3141@gmail.com>
 *
 * Copyright (C) 2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_WIDGET_CANVAS_H
#define INKSCAPE_UI_WIDGET_CANVAS_H

#include <memory>
#include <optional>
#include <2geom/int-rect.h>
#include <2geom/rect.h>
#include <glibmm/refptr.h>
#include <gtk/gtk.h> // GtkEventController*
#include <gtkmm/gesture.h> // Gtk::EventSequenceState
#include <sigc++/signal.h>

#include "display/rendermode.h"
#include "events/enums.h"
#include "optglarea.h"

namespace Gdk {
class Rectangle;
} // namespace Gdk

namespace Gtk {
class GestureClick;
} // namespace Gtk

class SPDesktop;

namespace Inkscape {

class CanvasItem;
class CanvasItemGroup;
class CMSTransform;
class Drawing;

namespace UI::Widget {

class CanvasPrivate;

/**
 * A widget for Inkscape's canvas.
 */
class Canvas final : public OptGLArea
{
    using parent_type = OptGLArea;

public:
    Canvas();
    ~Canvas() final;

    /* Configuration */

    // Desktop (Todo: Remove.)
    void set_desktop(SPDesktop *desktop) { _desktop = desktop; }
    SPDesktop *get_desktop() const { return _desktop; }

    // Drawing
    void set_drawing(Inkscape::Drawing *drawing);

    // Canvas item root
    CanvasItemGroup *get_canvas_item_root() const;

    // Geometry
    void set_pos   (const Geom::IntPoint &pos);
    void set_pos   (const Geom::Point    &fpos) { set_pos(fpos.round()); }
    void set_affine(const Geom::Affine   &affine);
    const Geom::IntPoint &get_pos   () const { return _pos; }
    const Geom::Affine   &get_affine() const { return _affine; }
    const Geom::Affine   &get_geom_affine() const; // tool-base.cpp (todo: remove this dependency)

    // Background
    void set_desk  (uint32_t rgba);
    void set_border(uint32_t rgba);
    void set_page  (uint32_t rgba);

    //  Rendering modes
    void set_render_mode(Inkscape::RenderMode mode);
    void set_color_mode (Inkscape::ColorMode  mode);
    void set_split_mode (Inkscape::SplitMode  mode);
    Inkscape::RenderMode get_render_mode() const { return _render_mode; }
    Inkscape::ColorMode  get_color_mode()  const { return _color_mode; }
    Inkscape::SplitMode  get_split_mode()  const { return _split_mode; }
    void set_clip_to_page_mode(bool clip);
    void set_antialiasing_enabled(bool enabled);

    // CMS
    void set_cms_active(bool active) { _cms_active = active; }
    bool get_cms_active() const { return _cms_active; }

    /* Observers */

    // Geometry
    Geom::IntPoint get_dimensions() const;
    bool world_point_inside_canvas(Geom::Point const &world) const; // desktop-events.cpp
    Geom::Point canvas_to_world(Geom::Point const &window) const;
    Geom::IntRect get_area_world() const;
    bool canvas_point_in_outline_zone(Geom::Point const &world) const;

    // State
    bool is_dragging() const { return _is_dragging; } // selection-chemistry.cpp

    // Mouse
    std::optional<Geom::Point> get_last_mouse() const; // desktop-widget.cpp

    /* Methods */

    // Invalidation
    void redraw_all();                  // Mark everything as having changed.
    void redraw_area(Geom::Rect const &area); // Mark a rectangle of world space as having changed.
    void redraw_area(int x0, int y0, int x1, int y1);
    void redraw_area(Geom::Coord x0, Geom::Coord y0, Geom::Coord x1, Geom::Coord y1);
    void request_update();              // Mark geometry as needing recalculation.

    // Callback run on destructor of any canvas item
    void canvas_item_destructed(Inkscape::CanvasItem *item);

    // State
    Inkscape::CanvasItem *get_current_canvas_item() const { return _current_canvas_item; }
    void                  set_current_canvas_item(Inkscape::CanvasItem *item) {
        _current_canvas_item = item;
    }
    Inkscape::CanvasItem *get_grabbed_canvas_item() const { return _grabbed_canvas_item; }
    void                  set_grabbed_canvas_item(Inkscape::CanvasItem *item, EventMask mask) {
        _grabbed_canvas_item = item;
        _grabbed_event_mask = mask;
    }
    void set_all_enter_events(bool on) { _all_enter_events = on; }

    void enable_autoscroll();

    sigc::connection connectPreDraw(sigc::slot<void ()> &&slot) { return _signal_pre_draw.connect(std::move(slot)); }
    sigc::connection connectFocusIn(sigc::slot<void ()> &&slot) { return _signal_focus_in.connect(std::move(slot)); }
    sigc::connection connectFocusOut(sigc::slot<void ()> &&slot) { return _signal_focus_out.connect(std::move(slot)); }

private:
    // EventControllerScroll
    bool on_scroll(GtkEventControllerScroll const *controller,
                   double dx, double dy);

    // GtkGestureClick
    Gtk::EventSequenceState on_button_pressed (Gtk::GestureClick const &controller,
                                               int n_press, double x, double y);
    Gtk::EventSequenceState on_button_released(Gtk::GestureClick const &controller,
                                               int n_press, double x, double y);

    // EventControllerMotion
    void on_motion(GtkEventControllerMotion const *controller, double x, double y);
    void on_enter (GtkEventControllerMotion const *controller, double x, double y);
    void on_leave (GtkEventControllerMotion const *controller);

    // EventControllerFocus
    void on_focus_in();
    void on_focus_out();

    // EventControllerKey
    bool on_key_pressed (GtkEventControllerKey const *controller,
                         unsigned keyval, unsigned keycode, GdkModifierType state);
    bool on_key_released(GtkEventControllerKey const *controller,
                         unsigned keyval, unsigned keycode, GdkModifierType state);

    void on_realize() final;
    void on_unrealize() final;
    void size_allocate_vfunc(int width, int height, int baseline) final;

    Glib::RefPtr<Gdk::GLContext> create_context() final;
    void paint_widget(Cairo::RefPtr<Cairo::Context> const &) final;

    /* Configuration */

    // Desktop
    SPDesktop *_desktop = nullptr;

    // Drawing
    Inkscape::Drawing *_drawing = nullptr;

    // Geometry
    Geom::IntPoint _pos = {0, 0}; ///< Coordinates of top-left pixel of canvas view within canvas.
    Geom::Affine _affine; ///< The affine that we have been requested to draw at.

    // Rendering modes
    Inkscape::RenderMode _render_mode = Inkscape::RenderMode::NORMAL;
    Inkscape::SplitMode  _split_mode  = Inkscape::SplitMode::NORMAL;
    Inkscape::ColorMode  _color_mode  = Inkscape::ColorMode::NORMAL;
    bool _antialiasing_enabled = true;

    // CMS
    bool _cms_active = false;
    std::shared_ptr<CMSTransform const> _cms_transform; ///< The lcms transform to apply to canvas.
    void set_cms_transform(); ///< Set the lcms transform.

    /* Internal state */

    // Event handling/item picking
    bool     _left_grabbed_item; ///< Relied upon by connector tool.
    bool     _all_enter_events;  ///< Keep all enter events. Only set true in connector-tool.cpp.
    bool     _is_dragging;       ///< Used in selection-chemistry to block undo/redo.
    int      _state;             ///< Last known modifier state (SHIFT, CTRL, etc.).

    Inkscape::CanvasItem *_current_canvas_item;     ///< Item containing cursor, nullptr if none.
    Inkscape::CanvasItem *_current_canvas_item_new; ///< Item to become _current_item, nullptr if none.
    Inkscape::CanvasItem *_grabbed_canvas_item;     ///< Item that holds a pointer grab; nullptr if none.
    EventMask _grabbed_event_mask;

    // Drawing
    bool _need_update = true; // Set true so setting CanvasItem bounds are calculated at least once.

    // Split view
    Inkscape::SplitDirection _split_direction;
    Geom::Point _split_frac;
    Inkscape::SplitDirection _hover_direction;
    bool _split_dragging;
    Geom::IntPoint _split_drag_start;

    sigc::signal<void ()> _signal_pre_draw;
    sigc::signal<void ()> _signal_focus_in;
    sigc::signal<void ()> _signal_focus_out;

    void update_cursor();

    // Opaque pointer to implementation
    friend class CanvasPrivate;
    std::unique_ptr<CanvasPrivate> d;
};

} // namespace UI::Widget

} // namespace Inkscape

#endif // INKSCAPE_UI_WIDGET_CANVAS_H

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
