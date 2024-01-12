// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * @file
 * HSLuv color wheel widget, based on the web implementation at
 * https://www.hsluv.org
*//*
 * Authors:
 *   Tavmjong Bah
 *   Massinissa Derriche <massinissa.derriche@gmail.com>
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2018, 2021, 2023 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INK_COLORWHEEL_H
#define INK_COLORWHEEL_H

#include <array>
#include <memory>
#include <utility>
#include <vector>
#include <2geom/point.h>
#include <2geom/line.h>
#include <sigc++/signal.h>
#include <gtk/gtk.h> // GtkEventControllerKey
#include <gtkmm/aspectframe.h>
#include <gtkmm/gesture.h> // Gtk::EventSequenceState

#include "hsluv.h"

#include "ui/widget/widget-vfuncs-class-init.h" // for focus

namespace Gtk {
class DrawingArea;
class GestureClick;
} // namespace Gtk

namespace Inkscape::UI::Widget {

class Bin;

struct ColorPoint final
{
    ColorPoint();
    ColorPoint(double x, double y, double r, double g, double b);
    ColorPoint(double x, double y, guint color);

    guint32 get_color() const;
    std::pair<double const &, double const &> get_xy() const;

    void set_color(Hsluv::Triplet const &rgb)
    {
        r = rgb[0];
        g = rgb[1];
        b = rgb[2];
    }

    // eurgh!
    double x;
    double y;
    double r;
    double g;
    double b;
};

/**
 * @class ColorWheel
 */
// AspectFrame because we are circular & enforcing 1:1 eases drawing without overallocating buffers
class ColorWheel : public Gtk::AspectFrame
{
public:
    ColorWheel();

    /// Set the RGB of the wheel. If @a emit is true & hue changes, we call color_changed() for you
    /// @param r the red component, from 0.0 to 1.0
    /// @param g the green component, from 0.0 to 1.0
    /// @param b the blue component, from 0.0 to 1.0
    /// @param overrideHue whether to set hue to 0 if min==max(r,g,b) – only used by ColorwheelHSL…
    /// @param emit false if you want to manually call color_changed() e.g. to avoid multiple emits
    /// @return whether or not the value actually changed, to enable avoiding redraw if it does not
    virtual bool setRgb(double r, double g, double b,
                        bool overrideHue = true, bool emit = true) = 0;
    virtual void getRgb(double *r, double *g, double *b) const = 0;
    virtual void getRgbV(double *rgb) const = 0;
    virtual guint32 getRgb() const = 0;

    /// Set the hue of the wheel. If @a emit is true & hue changes, we call color_changed() for you
    /// @param emit false if you want to manually call color_changed() e.g. to avoid multiple emits
    /// @return whether or not the value actually changed, to enable avoiding redraw if it does not
    virtual bool setHue       (double h, bool emit = true);
    /// Ditto setHue(), but changes the saturation instead.
    virtual bool setSaturation(double s, bool emit = true);
    /// Ditto setHue(), but changes the lightness instead.
    virtual bool setLightness (double l, bool emit = true);

    void getValues(double *a, double *b, double *c) const;
    bool isAdjusting() const { return _adjusting; }

    /// Connect a slot to be called after the color has changed.
    sigc::connection connect_color_changed(sigc::slot<void ()>);

protected:
    std::array<double, 3> _values = {};
    bool _adjusting = false;

    /// Call when color has changed! Emits signal_color_changed & calls _drawing_area->queue_draw()
    void color_changed();
    void queue_drawing_area_draw();

    [[nodiscard]] Gtk::Allocation get_drawing_area_allocation() const;
    [[nodiscard]] bool drawing_area_has_focus() const;
    void focus_drawing_area();

private:
    sigc::signal<void ()> _signal_color_changed;

    UI::Widget::Bin *_bin;
    Gtk::DrawingArea *_drawing_area;
    virtual void on_drawing_area_size(int width, int height, int baseline) {}
    virtual void on_drawing_area_draw(Cairo::RefPtr<Cairo::Context> const &cr, int, int) = 0;

    /// All event controllers are connected to the DrawingArea.
    virtual Gtk::EventSequenceState on_click_pressed (Gtk::GestureClick const &click,
                                                      int n_press, double x, double y) = 0;
    virtual Gtk::EventSequenceState on_click_released(Gtk::GestureClick const &click,
                                                      int n_press, double x, double y) = 0;
    virtual void on_motion(GtkEventControllerMotion const *motion, double x, double y) = 0;
    virtual bool on_key_pressed(GtkEventControllerKey const *key_event,
                                unsigned keyval, unsigned keycode, GdkModifierType state)
                               { return false; }
    bool on_key_released(GtkEventControllerKey const *key_event,
                         unsigned keyval, unsigned keycode, GdkModifierType state);
};

/**
 * @class ColorWheelHSL
 */
class ColorWheelHSL
    : public WidgetVfuncsClassInit // As Gtkmm4 doesn't wrap focus_vfunc
    , public ColorWheel
{
public:
    ColorWheelHSL()
        : Glib::ObjectBase{"ColorWheelHSL"}
        , WidgetVfuncsClassInit{} {}
    bool setHue       (double h, bool emit = true) final;
    bool setSaturation(double s, bool emit = true) final;
    bool setLightness (double l, bool emit = true) final;
    bool setRgb(double r, double g, double b,
                bool overrideHue = true, bool emit = true) override;
    void getRgb(double *r, double *g, double *b) const override;
    void getRgbV(double *rgb) const override;
    guint32 getRgb() const override;
    void getHsl(double *h, double *s, double *l) const;

private:
    void on_drawing_area_size(int width, int height, int baseline) override;
    void on_drawing_area_draw(Cairo::RefPtr<Cairo::Context> const &cr, int, int) override;
    std::optional<bool> focus(Gtk::DirectionType direction) override;

    bool _set_from_xy(double x, double y);
    bool set_from_xy_delta(double dx, double dy);
    bool _is_in_ring(double x, double y);
    bool _is_in_triangle(double x, double y);
    void _update_ring_color(double x, double y);

    enum class DragMode {
        NONE,
        HUE,
        SATURATION_VALUE
    };

    static constexpr double _ring_width = 0.2;
    DragMode _mode = DragMode::NONE;
    bool _focus_on_ring = true;

    Gtk::EventSequenceState on_click_pressed (Gtk::GestureClick const &click,
                                              int n_press, double x, double y) final;
    Gtk::EventSequenceState on_click_released(Gtk::GestureClick const &click,
                                              int n_press, double x, double y) final;
    void on_motion(GtkEventControllerMotion const *motion, double x, double y) final;
    bool on_key_pressed(GtkEventControllerKey const *key_event,
                        unsigned keyval, unsigned keycode, GdkModifierType state) final;

    // caches to speed up drawing
    using TriangleCorners = std::array<ColorPoint, 3>;
    using MinMax          = std::array<double    , 2>;
    std::optional<Geom::IntPoint> _cache_size;
    std::optional<MinMax             > _radii;
    std::optional<TriangleCorners    > _triangle_corners;
    std::optional<Geom::Point        > _marker_point;
    std::vector  <guint32            > _buffer_ring, _buffer_triangle;
    Cairo::RefPtr<Cairo::ImageSurface> _source_ring, _source_triangle;
    [[nodiscard]] MinMax          const &get_radii           ();
    [[nodiscard]] TriangleCorners const &get_triangle_corners();
    [[nodiscard]] Geom::Point     const &get_marker_point    ();
                  void                   update_ring_source    ();
    [[nodiscard]] TriangleCorners        update_triangle_source();
};

/**
 * @class ColorWheelHSLuv
 */
class ColorWheelHSLuv : public ColorWheel
{
public:
    ColorWheelHSLuv();

    /// See base doc & N.B. that overrideHue is unused by this class
    bool setRgb(double r, double g, double b,
                bool overrideHue = true, bool emit = true) override;
    void getRgb(double *r, double *g, double *b) const override;
    void getRgbV(double *rgb) const override;
    guint32 getRgb() const override;

    bool setHsluv(double h, double s, double l);
    bool setLightness(double l, bool emit) final;

    void getHsluv(double *h, double *s, double *l) const;
    void updateGeometry();

private:
    void on_drawing_area_draw(Cairo::RefPtr<Cairo::Context> const &cr, int, int) final;

    bool _set_from_xy(double const x, double const y);
    void _setFromPoint(Geom::Point const &pt) { _set_from_xy(pt[Geom::X], pt[Geom::Y]); }
    void _updatePolygon();
    bool _vertex() const;

    Gtk::EventSequenceState on_click_pressed (Gtk::GestureClick const &click,
                                              int n_press, double x, double y) final;
    Gtk::EventSequenceState on_click_released(Gtk::GestureClick const &click,
                                              int n_press, double x, double y) final;
    void on_motion(GtkEventControllerMotion const *motion, double x, double y) final;
    bool on_key_pressed(GtkEventControllerKey const *key_event,
                        unsigned keyval, unsigned keycode, GdkModifierType state) final;

    double _scale = 1.0;
    std::unique_ptr<Hsluv::PickerGeometry> _picker_geometry;
    std::vector<guint32> _buffer_polygon;
    Cairo::RefPtr<::Cairo::ImageSurface> _surface_polygon;
    Geom::IntPoint _cache_size;
    int _square_size = 1;
};

} // namespace Inkscape::UI::Widget

#endif // INK_COLORWHEEL_HSLUV_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8: textwidth=99:
