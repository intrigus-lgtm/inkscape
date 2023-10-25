// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file OKHSL color wheel widget implementation.
 */
/*
 * Authors:
 *   Rafael Siejakowski <rs@rs-math.net>
 *
 * Copyright (C) 2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "ui/widget/oklab-color-wheel.h"

#include <algorithm>
#include <cmath>
#include <gtkmm/gestureclick.h>

#include "display/cairo-utils.h"
#include "oklab.h"

namespace Inkscape::UI::Widget {

OKWheel::OKWheel()
{
    // Set to black
    _values[H] = 0;
    _values[S] = 0;
    _values[L] = 0;
}

bool OKWheel::setRgb(double r, double g, double b,
                     bool /*overrideHue*/, bool const emit)
{
    using namespace Oklab;

    auto [h, s, l] = oklab_to_okhsl(rgb_to_oklab({ r, g, b }));

    auto const h2 = h * 2.0 * M_PI;
    bool const changed_hue = _values[H] != h2;
    _values[H] = h2;

    bool const changed_saturation = _values[S] != s;
    _values[S] = s;

    bool const changed_lightness = _values[L] != l;
    _values[L] = l;

    if (changed_lightness) {
        _updateChromaBounds();
        _redrawDisc();
    }

    bool const changed = changed_hue || changed_saturation || changed_lightness;
    if (changed && emit) color_changed();
    return changed;
}

void OKWheel::getRgb(double *red, double *green, double *blue) const
{
    using namespace Oklab;
    auto [r, g, b] = oklab_to_rgb(okhsl_to_oklab({ _values[H] / (2.0 * M_PI), _values[S], _values[L] }));
    *red   = r;
    *green = g;
    *blue  = b;
}

guint32 OKWheel::getRgb() const
{
    guint32 result = 0x0;
    double rgb[3];
    getRgbV(rgb);
    for (auto component : rgb) {
        result <<= 8;
        result |= SP_COLOR_F_TO_U(component);
    }
    return result;
}

/** @brief Compute the chroma bounds around the picker disc.
 *
 * Calculates the maximum absolute Lch chroma along rays emanating
 * from the center of the picker disc. CHROMA_BOUND_SAMPLES evenly
 * spaced rays will be used. The result is stored in _bounds.
 */
void OKWheel::_updateChromaBounds()
{
    double const angle_step = 360.0 / CHROMA_BOUND_SAMPLES;
    double hue_angle_deg = 0.0;
    for (unsigned i = 0; i < CHROMA_BOUND_SAMPLES; i++) {
        _bounds[i] = Oklab::max_chroma(_values[L], hue_angle_deg);
        hue_angle_deg += angle_step;
    }
}

/** @brief Update the size of the color disc and margins
 * depending on the widget's allocation.
 *
 * @return Whether the colorful disc background needs to be regenerated.
 */
bool OKWheel::_updateDimensions()
{
    auto const &allocation = get_drawing_area_allocation();
    auto width = allocation.get_width();
    auto height = allocation.get_height();
    double new_radius = 0.5 * std::min(width, height);
    // Allow the halo to fit at coordinate extrema.
    new_radius -= HALO_RADIUS + 0.5 * HALO_STROKE;
    bool disc_needs_redraw = (_disc_radius != new_radius);
    _disc_radius = new_radius;
    _margin = {std::max(0.0, 0.5 * (width  - 2.0 * _disc_radius)),
               std::max(0.0, 0.5 * (height - 2.0 * _disc_radius))};
    return disc_needs_redraw;
}

/** @brief Compute the ARGB32 color for a point inside the picker disc.
 *
 * The picker disc is viewed as the unit disc in the xy-plane, with
 * the y-axis pointing up. If the passed point lies outside of the unit
 * disc, the returned color is the same as for a point rescaled to the
 * unit circle (outermost possible color in that direction).
 *
 * @param point A point in the normalized disc coordinates.
 * @return a Cairo-compatible ARGB32 color.
 */
uint32_t OKWheel::_discColor(Geom::Point const &point) const
{
    using namespace Oklab;
    using Display::AssembleARGB32;

    double saturation = point.length();
    if (saturation == 0.0) {
        auto [r, g, b] = oklab_to_rgb({ _values[L], 0.0, 0.0 });
        return AssembleARGB32(0xFF, (guint)(r * 255.5), (guint)(g * 255.5), (guint)(b * 255.5));
    } else if (saturation > 1.0) {
        saturation = 1.0;
    }

    double const hue_radians = Geom::Angle(Geom::atan2(point)).radians0();

    // Find the precomputed chroma bounds on both sides of this angle.
    unsigned previous_sample = std::floor(hue_radians * 0.5 * CHROMA_BOUND_SAMPLES / M_PI);
    if (previous_sample >= CHROMA_BOUND_SAMPLES) {
        previous_sample = 0;
    }
    unsigned const next_sample = (previous_sample == CHROMA_BOUND_SAMPLES - 1) ? 0 : previous_sample + 1;
    double const previous_sample_angle = 2.0 * M_PI * previous_sample / CHROMA_BOUND_SAMPLES;
    double const angle_delta = hue_radians - previous_sample_angle;
    double const t = angle_delta * 0.5 * CHROMA_BOUND_SAMPLES / M_PI;
    double const chroma_bound_estimate = Geom::lerp(t, _bounds[previous_sample], _bounds[next_sample]);
    double const absolute_chroma = chroma_bound_estimate * saturation;

    auto [r, g, b] = oklab_to_rgb(oklch_radians_to_oklab({ _values[L], absolute_chroma, hue_radians }));
    return AssembleARGB32(0xFF, (guint)(r * 255.5), (guint)(g * 255.5), (guint)(b * 255.5));
}

/** @brief Returns the position of the current color in the coordinates
 * of the picker wheel.
 *
 * The picker wheel is inscribed in a square with side length 2 * _disc_radius.
 * The point (0, 0) corresponds to the center of the disc; y-axis points down.
 */
Geom::Point OKWheel::_curColorWheelCoords() const
{
    Geom::Point result;
    Geom::sincos(_values[H], result.y(), result.x());
    result *= _values[S];
    return result * Geom::Scale(_disc_radius, -_disc_radius);
}

/** @brief Draw the widget into the Cairo context. */
void OKWheel::on_drawing_area_draw(Cairo::RefPtr<Cairo::Context> const &cr, int, int)
{
    if(_updateDimensions()) {
        _redrawDisc();
    }

    cr->save();
    cr->set_antialias(Cairo::ANTIALIAS_SUBPIXEL);

    // Draw the colorful disc background from the cached pixbuf,
    // clipping to a geometric circle (avoids aliasing).
    cr->translate(_margin[Geom::X], _margin[Geom::Y]);
    cr->move_to(2 * _disc_radius, _disc_radius);
    cr->arc(_disc_radius, _disc_radius, _disc_radius, 0.0, 2.0 * M_PI);
    cr->close_path();
    cr->set_source(_disc, 0, 0);
    cr->fill();

    // Draw the halo around the current color.
    {
        auto const where = _curColorWheelCoords();
        cr->translate(_disc_radius, _disc_radius);
        cr->move_to(where.x() + HALO_RADIUS, where.y());
        cr->arc(where.x(), where.y(), HALO_RADIUS, 0.0, 2.0 * M_PI);
        cr->close_path();
        // Fill the halo with the current color.
        {
            double r, g, b;
            getRgb(&r, &g, &b);
            cr->set_source_rgba(r, g, b, 1.0);
        }
        cr->fill_preserve();

        // Stroke the border of the halo.
        {
            auto [gray, alpha] = Hsluv::get_contrasting_color(_values[L]);
            cr->set_source_rgba(gray, gray, gray, alpha);
        }
        cr->set_line_width(HALO_STROKE);
        cr->stroke();
    }
    cr->restore();
}

/** @brief Recreate the pixel buffer containing the colourful disc. */
void OKWheel::_redrawDisc()
{
    int const size = std::ceil(2.0 * _disc_radius);
    _pixbuf.resize(4 * size * size);

    double const radius = 0.5 * size;
    double const inverse_radius = 1.0 / radius;

    // Fill buffer with (<don't care>, R, G, B) values.
    uint32_t *pos = (uint32_t *)(_pixbuf.data());
    for (int y = 0; y < size; y++) {
        // Convert (x, y) to a coordinate system where the
        // disc is the unit disc and the y-axis points up.
        double const normalized_y = inverse_radius * (radius - y);
        for (int x = 0; x < size; x++) {
            auto const pt = Geom::Point(inverse_radius * (x - radius), normalized_y);
            *pos++ = _discColor(pt);
        }
    }

    int const stride = Cairo::ImageSurface::format_stride_for_width(Cairo::Surface::Format::RGB24, size);
    _disc = Cairo::ImageSurface::create(_pixbuf.data(), Cairo::Surface::Format::RGB24, size, size, stride);
}

/** @brief Convert widget (event) coordinates to an abstract coordinate system
 * in which the picker disc is the unit disc and the y-axis points up.
 */
Geom::Point OKWheel::_event2abstract(Geom::Point const &event_pt) const
{
    auto result = event_pt - _margin - Geom::Point(_disc_radius, _disc_radius);
    double const scale = 1.0 / _disc_radius;
    return result * Geom::Scale(scale, -scale);
}

/** @brief Set the current color based on a point on the wheel.
 *
 * @param pt A point in the abstract coordinate system in which the picker
 * disc is the unit disc and the y-axis points up.
 */
bool OKWheel::_setColor(Geom::Point const &pt, bool const emit)
{
    auto const s = std::clamp(pt.length(), 0.0, 1.0);

    Geom::Angle clicked_hue = _values[S] ? Geom::atan2(pt) : 0.0;
    auto const h = clicked_hue.radians0();

    bool const changed = _values[S] != s || _values[H] != h;
    if (changed) {
        _values[S] = s;
        _values[H] = h;
        if (emit) color_changed();
    }
    return changed;
}

/** @brief Handle a left mouse click on the widget.
 *
 * @param pt The clicked point expressed in the coordinate system in which
 *           the picker disc is the unit disc and the y-axis points up.
 * @return Whether the click has been handled.
 */
bool OKWheel::_onClick(Geom::Point const &pt)
{
    auto r = pt.length();
    if (r > 1.0) { // Clicked outside the disc, no cookie.
        return false;
    }
    _adjusting = true;
    _setColor(pt);
    return true;
}

/** @brief Handle a button press event. */
Gtk::EventSequenceState OKWheel::on_click_pressed(Gtk::GestureClick const &click,
                                                  int /*n_press*/, double const x, double const y)
{
    if (click.get_current_button() == 1) {
        // Convert the click coordinates to the abstract coords in which
        // the picker disc is the unit disc in the xy-plane.
        if (_onClick(_event2abstract({x, y}))) {
            return Gtk::EventSequenceState::CLAIMED;
        }
    }
    // TODO: add a context menu to copy out the CSS4 color values.
    return Gtk::EventSequenceState::NONE;
}

/** @brief Handle a button release event. */
Gtk::EventSequenceState OKWheel::on_click_released(Gtk::GestureClick const & /*click*/,
                                                   int /*n_press*/, double /*x*/, double /*y*/)
{
    _adjusting = false;
    return Gtk::EventSequenceState::CLAIMED;
}

/** @brief Handle a drag (motion notify event). */
void OKWheel::on_motion(GtkEventControllerMotion const * /*motion*/, double x, double y)
{
    if (_adjusting) {
        _setColor(_event2abstract({x, y}));
    }
}

} // namespace Inkscape::UI::Widget

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
