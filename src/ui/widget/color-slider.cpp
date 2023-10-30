// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A slider with colored background - implementation.
 *//*
 * Authors:
 *   see git history
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <utility>
#include <sigc++/functors/mem_fun.h>
#include <gdkmm/general.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/gestureclick.h>

#include "ui/widget/color-slider.h"
#include "preferences.h"
#include "ui/controller.h"
#include "ui/widget/color-scales.h"

static const gint ARROW_SIZE = 8;

static const guchar *sp_color_slider_render_gradient(gint x0, gint y0, gint width, gint height, gint c[], gint dc[],
                                                     guint b0, guint b1, guint mask);
static const guchar *sp_color_slider_render_map(gint x0, gint y0, gint width, gint height, guchar *map, gint start,
                                                gint step, guint b0, guint b1, guint mask);

namespace Inkscape::UI::Widget {

ColorSlider::ColorSlider(Glib::RefPtr<Gtk::Adjustment> adjustment)
    : _dragging(false)
    , _value(0.0)
    , _oldvalue(0.0)
    , _map(nullptr)
{
    set_name("ColorSlider");

    set_draw_func(sigc::mem_fun(*this, &ColorSlider::draw_func));

    _c0[0] = 0x00;
    _c0[1] = 0x00;
    _c0[2] = 0x00;
    _c0[3] = 0xff;

    _cm[0] = 0xff;
    _cm[1] = 0x00;
    _cm[2] = 0x00;
    _cm[3] = 0xff;

    _c0[0] = 0xff;
    _c0[1] = 0xff;
    _c0[2] = 0xff;
    _c0[3] = 0xff;

    _b0 = 0x5f;
    _b1 = 0xa0;
    _bmask = 0x08;

    setAdjustment(std::move(adjustment));

    Controller::add_click(*this,
                          sigc::mem_fun(*this, &ColorSlider::on_click_pressed ),
                          sigc::mem_fun(*this, &ColorSlider::on_click_released),
                          Controller::Button::left);
    Controller::add_motion<nullptr, &ColorSlider::on_motion, nullptr>
                          (*this, *this);
}

ColorSlider::~ColorSlider()
{
    if (_adjustment) {
        _adjustment_changed_connection.disconnect();
        _adjustment_value_changed_connection.disconnect();
        _adjustment.reset();
    }
}

static bool get_constrained(Gdk::ModifierType const state)
{
    return Controller::has_flag(state, Gdk::ModifierType::CONTROL_MASK);
}

static double get_value_at(Gtk::Widget const &self, double const x, double const y)
{

    constexpr auto cx = 0; // formerly held CSS padding, now Box handles that
    auto const cw = self.get_width() - 2 * cx;
    return CLAMP((x - cx) / cw, 0.0, 1.0);
}

Gtk::EventSequenceState ColorSlider::on_click_pressed(Gtk::GestureClick const &click,
                                                      int /*n_press*/, double const x, double const y)
{
    signal_grabbed.emit();
    _dragging = true;
    _oldvalue = _value;
    auto const value = get_value_at(*this, x, y);
    auto const state = click.get_current_event_state();
    auto const constrained = get_constrained(state);
    ColorScales<>::setScaled(_adjustment, value, constrained);
    signal_dragged.emit();
    return Gtk::EventSequenceState::NONE;
}

Gtk::EventSequenceState ColorSlider::on_click_released(Gtk::GestureClick const & /*click*/,
                                                       int /*n_press*/, double /*x*/, double /*y*/)
{
    _dragging = false;
    signal_released.emit();
    if (_value != _oldvalue) {
        signal_value_changed.emit();
    }
    return Gtk::EventSequenceState::NONE;
}

void ColorSlider::on_motion(GtkEventControllerMotion const * const motion,
                            double const x, double const y)
{
    if (_dragging) {
        auto const value = get_value_at(*this, x, y);
        auto const state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(motion));
        auto const constrained = get_constrained((Gdk::ModifierType)state);
        ColorScales<>::setScaled(_adjustment, value, constrained);
        signal_dragged.emit();
    }
}

void ColorSlider::setAdjustment(Glib::RefPtr<Gtk::Adjustment> adjustment)
{
    if (!adjustment) {
        _adjustment = Gtk::Adjustment::create(0.0, 0.0, 1.0, 0.01, 0.0, 0.0);
    }
    else {
        adjustment->set_page_increment(0.0);
        adjustment->set_page_size(0.0);
    }

    if (_adjustment != adjustment) {
        if (_adjustment) {
            _adjustment_changed_connection.disconnect();
            _adjustment_value_changed_connection.disconnect();
        }

        _adjustment = std::move(adjustment);
        _adjustment_changed_connection =
            _adjustment->signal_changed().connect(sigc::mem_fun(*this, &ColorSlider::_onAdjustmentChanged));
        _adjustment_value_changed_connection =
            _adjustment->signal_value_changed().connect(sigc::mem_fun(*this, &ColorSlider::_onAdjustmentValueChanged));

        _value = ColorScales<>::getScaled(_adjustment);

        _onAdjustmentChanged();
    }
}

void ColorSlider::_onAdjustmentChanged() { queue_draw(); }

void ColorSlider::_onAdjustmentValueChanged()
{
    if (_value != ColorScales<>::getScaled(_adjustment)) {
        constexpr int cx = 0, cy = 0; // formerly held CSS padding, now Box handles that
        auto const cw = get_width();
        if ((gint)(ColorScales<>::getScaled(_adjustment) * cw) != (gint)(_value * cw)) {
            gint ax, ay;
            gfloat value;
            value = _value;
            _value = ColorScales<>::getScaled(_adjustment);
            ax = (int)(cx + value * cw - ARROW_SIZE / 2 - 2);
            ay = cy;
            queue_draw();
        }
        else {
            _value = ColorScales<>::getScaled(_adjustment);
        }
    }
}

void ColorSlider::setColors(guint32 start, guint32 mid, guint32 end)
{
    // Remove any map, if set
    _map = nullptr;

    _c0[0] = start >> 24;
    _c0[1] = (start >> 16) & 0xff;
    _c0[2] = (start >> 8) & 0xff;
    _c0[3] = start & 0xff;

    _cm[0] = mid >> 24;
    _cm[1] = (mid >> 16) & 0xff;
    _cm[2] = (mid >> 8) & 0xff;
    _cm[3] = mid & 0xff;

    _c1[0] = end >> 24;
    _c1[1] = (end >> 16) & 0xff;
    _c1[2] = (end >> 8) & 0xff;
    _c1[3] = end & 0xff;

    queue_draw();
}

void ColorSlider::setMap(const guchar *map)
{
    _map = const_cast<guchar *>(map);

    queue_draw();
}

void ColorSlider::setBackground(guint dark, guint light, guint size)
{
    _b0 = dark;
    _b1 = light;
    _bmask = size;

    queue_draw();
}

void ColorSlider::draw_func(Cairo::RefPtr<Cairo::Context> const &cr,
                            int const width, int const height)
{
    // padding/carea are no longer used/useful, just kept to minimise code diff
    static Gtk::Border const padding{};
    auto const scale = get_scale_factor();
    Gdk::Rectangle const carea{0, 0, width * scale, height * scale};

    // changing scale to draw pixmap at display resolution
    cr->save();
    cr->scale(1.0 / scale, 1.0 / scale);

    if (_map) {
        /* Render map pixelstore */
        gint d = (1024 << 16) / carea.get_width();
        gint s = 0;

        const guchar *b =
            sp_color_slider_render_map(0, 0, carea.get_width(), carea.get_height(), _map, s, d, _b0, _b1, _bmask * scale);

        if (b != nullptr && carea.get_width() > 0) {
            Glib::RefPtr<Gdk::Pixbuf> pb = Gdk::Pixbuf::create_from_data(
                b, Gdk::Colorspace::RGB, false, 8, carea.get_width(), carea.get_height(), carea.get_width() * 3);

            Gdk::Cairo::set_source_pixbuf(cr, pb, carea.get_x(), carea.get_y());
            cr->paint();
        }
    }
    else {
        gint c[4], dc[4];

        /* Render gradient */

        // part 1: from c0 to cm
        if (carea.get_width() > 0) {
            for (gint i = 0; i < 4; i++) {
                c[i] = _c0[i] << 16;
                dc[i] = ((_cm[i] << 16) - c[i]) / (carea.get_width() / 2);
            }
            guint wi = carea.get_width() / 2;
            const guchar *b = sp_color_slider_render_gradient(0, 0, wi, carea.get_height(), c, dc, _b0, _b1, _bmask * scale);

            /* Draw pixelstore 1 */
            if (b != nullptr && wi > 0) {
                Glib::RefPtr<Gdk::Pixbuf> pb =
                    Gdk::Pixbuf::create_from_data(b, Gdk::Colorspace::RGB, false, 8, wi, carea.get_height(), wi * 3);

                Gdk::Cairo::set_source_pixbuf(cr, pb, carea.get_x(), carea.get_y());
                cr->paint();
            }
        }

        // part 2: from cm to c1
        if (carea.get_width() > 0) {
            for (gint i = 0; i < 4; i++) {
                c[i] = _cm[i] << 16;
                dc[i] = ((_c1[i] << 16) - c[i]) / (carea.get_width() / 2);
            }
            guint wi = carea.get_width() / 2;
            const guchar *b = sp_color_slider_render_gradient(carea.get_width() / 2, 0, wi, carea.get_height(), c, dc,
                                                              _b0, _b1, _bmask * scale);

            /* Draw pixelstore 2 */
            if (b != nullptr && wi > 0) {
                Glib::RefPtr<Gdk::Pixbuf> pb =
                    Gdk::Pixbuf::create_from_data(b, Gdk::Colorspace::RGB, false, 8, wi, carea.get_height(), wi * 3);

                Gdk::Cairo::set_source_pixbuf(cr, pb, carea.get_width() / 2 + carea.get_x(), carea.get_y());
                cr->paint();
            }
        }
    }

    cr->restore();

    /* Draw arrow */
    gint x = (int)(_value * (carea.get_width() / scale) - ARROW_SIZE / 2 + carea.get_x() / scale);
    gint y1 = carea.get_y() / scale;
    gint y2 = carea.get_y() / scale + carea.get_height() / scale - 1;
    cr->set_line_width(2.0);

    // Define top arrow
    cr->move_to(x - 0.5, y1 + 0.5);
    cr->line_to(x + ARROW_SIZE - 0.5, y1 + 0.5);
    cr->line_to(x + (ARROW_SIZE - 1) / 2.0, y1 + ARROW_SIZE / 2.0 + 0.5);
    cr->close_path();

    // Define bottom arrow
    cr->move_to(x - 0.5, y2 + 0.5);
    cr->line_to(x + ARROW_SIZE - 0.5, y2 + 0.5);
    cr->line_to(x + (ARROW_SIZE - 1) / 2.0, y2 - ARROW_SIZE / 2.0 + 0.5);
    cr->close_path();

    // Render both arrows
    cr->set_source_rgb(0.0, 0.0, 0.0);
    cr->stroke_preserve();
    cr->set_source_rgb(1.0, 1.0, 1.0);
    cr->fill();
}

} // namespace Inkscape::UI::Widget

/* Colors are << 16 */

inline bool checkerboard(gint x, gint y, guint size) {
	return ((x / size) & 1) != ((y / size) & 1);
}

static const guchar *sp_color_slider_render_gradient(gint x0, gint y0, gint width, gint height, gint c[], gint dc[],
                                                     guint b0, guint b1, guint mask)
{
    static guchar *buf = nullptr;
    static gint bs = 0;
    guchar *dp;
    gint x, y;
    guint r, g, b, a;

    if (buf && (bs < width * height)) {
        g_free(buf);
        buf = nullptr;
    }
    if (!buf) {
        buf = g_new(guchar, width * height * 3);
        bs = width * height;
    }

    dp = buf;
    r = c[0];
    g = c[1];
    b = c[2];
    a = c[3];
    for (x = x0; x < x0 + width; x++) {
        gint cr, cg, cb, ca;
        guchar *d;
        cr = r >> 16;
        cg = g >> 16;
        cb = b >> 16;
        ca = a >> 16;
        d = dp;
        for (y = y0; y < y0 + height; y++) {
            guint bg, fc;
            /* Background value */
            bg = checkerboard(x, y, mask) ? b0 : b1;
            fc = (cr - bg) * ca;
            d[0] = bg + ((fc + (fc >> 8) + 0x80) >> 8);
            fc = (cg - bg) * ca;
            d[1] = bg + ((fc + (fc >> 8) + 0x80) >> 8);
            fc = (cb - bg) * ca;
            d[2] = bg + ((fc + (fc >> 8) + 0x80) >> 8);
            d += 3 * width;
        }
        r += dc[0];
        g += dc[1];
        b += dc[2];
        a += dc[3];
        dp += 3;
    }

    return buf;
}

/* Positions are << 16 */

static const guchar *sp_color_slider_render_map(gint x0, gint y0, gint width, gint height, guchar *map, gint start,
                                                gint step, guint b0, guint b1, guint mask)
{
    static guchar *buf = nullptr;
    static gint bs = 0;
    guchar *dp;
    gint x, y;

    if (buf && (bs < width * height)) {
        g_free(buf);
        buf = nullptr;
    }
    if (!buf) {
        buf = g_new(guchar, width * height * 3);
        bs = width * height;
    }

    dp = buf;
    for (x = x0; x < x0 + width; x++) {
        gint cr, cg, cb, ca;
        guchar *d = dp;
        guchar *sp = map + 4 * (start >> 16);
        cr = *sp++;
        cg = *sp++;
        cb = *sp++;
        ca = *sp++;
        for (y = y0; y < y0 + height; y++) {
            guint bg, fc;
            /* Background value */
            bg = checkerboard(x, y, mask) ? b0 : b1;
            fc = (cr - bg) * ca;
            d[0] = bg + ((fc + (fc >> 8) + 0x80) >> 8);
            fc = (cg - bg) * ca;
            d[1] = bg + ((fc + (fc >> 8) + 0x80) >> 8);
            fc = (cb - bg) * ca;
            d[2] = bg + ((fc + (fc >> 8) + 0x80) >> 8);
            d += 3 * width;
        }
        dp += 3;
        start += step;
    }

    return buf;
}

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
