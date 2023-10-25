// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file OKHSL color wheel widget, based on the OKLab/OKLch color space.
 */
/*
 * Authors:
 *   Rafael Siejakowski <rs@rs-math.net>
 *
 * Copyright (C) 2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_OKLAB_COLOR_WHEEL_H
#define SEEN_OKLAB_COLOR_WHEEL_H

#include "ui/widget/ink-color-wheel.h"

#include <gtk/gtk.h> // GtkEventControllerMotion
#include <gtkmm/gesture.h> // Gtk::EventSequenceState

namespace Inkscape::UI::Widget {

/** @brief The color wheel used in the OKHSL picker. */
class OKWheel : public ColorWheel
{
public:
    OKWheel();

    /** @brief Set the displayed color to the specified gamma-compressed sRGB color. */
    /// See base doc & N.B. that overrideHue is unused by this class
    bool setRgb(double r, double g, double b,
                bool overrideHue = true, bool emit = true) override;

    /** @brief Get the gamma-compressed sRGB color from the picker wheel. */
    void getRgb(double *r, double *g, double *b) const override;
    void getRgbV(double *rgb) const override { getRgb(rgb, rgb + 1, rgb + 2); }
    guint32 getRgb() const override;

private:
    void on_drawing_area_draw(Cairo::RefPtr<Cairo::Context> const &cr, int, int) override;

    static unsigned constexpr H = 0, S = 1, L = 2; ///< Indices into _values

    /** How many samples for the chroma bounds to use for the color disc.
     * A larger value produces a nicer gradient at the cost of slower performance.
     */
    static unsigned constexpr CHROMA_BOUND_SAMPLES = 120;
    static double constexpr HALO_RADIUS = 4.5; ///< Radius of the halo around the current color.
    static double constexpr HALO_STROKE = 1.5; ///< Width of the halo's stroke.

    Geom::Point _curColorWheelCoords() const;
    uint32_t _discColor(Geom::Point const &point) const;
    Geom::Point _event2abstract(Geom::Point const &point) const;
    void _redrawDisc();
    bool _setColor(Geom::Point const &pt, bool emit = true);
    void _updateChromaBounds();
    bool _updateDimensions();

    // Event handlers
    bool _onClick(Geom::Point const &unit_pos);
    Gtk::EventSequenceState on_click_pressed (Gtk::GestureClick const &click,
                                              int n_press, double x, double y) final;
    Gtk::EventSequenceState on_click_released(Gtk::GestureClick const &click,
                                              int n_press, double x, double y) final;
    void on_motion(GtkEventControllerMotion const *motion, double x, double y) final;

    double _disc_radius = 1.0;
    Geom::Point _margin;
    Cairo::RefPtr<Cairo::ImageSurface> _disc;
    std::vector<uint8_t> _pixbuf;
    std::array<double, CHROMA_BOUND_SAMPLES> _bounds;
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_OKLAB_COLOR_WHEEL_H

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
