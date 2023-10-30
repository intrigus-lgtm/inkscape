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

#include "gradient-with-stops.h"

#include <cmath>
#include <string>
#include <gdkmm/cursor.h>
#include <gdkmm/general.h>
#include <gtkmm/drawingarea.h>
#include <sigc++/functors/mem_fun.h>

#include "display/cairo-utils.h"
#include "io/resource.h"
#include "object/sp-gradient.h"
#include "object/sp-stop.h"
#include "ui/controller.h"
#include "ui/cursor-utils.h"
#include "ui/util.h"
#include "util/object-renderer.h"

// c.f. share/ui/style.css
// gradient's image height (multiple of checkerboard tiles, they are 6x6)
constexpr static int GRADIENT_IMAGE_HEIGHT = 3 * 6;

namespace Inkscape::UI::Widget {

using std::round;
using namespace Inkscape::IO;

std::string get_stop_template_path(const char* filename) {
    // "stop handle" template files path
    return Resource::get_filename(Resource::UIS, filename);
}

GradientWithStops::GradientWithStops() :
    Glib::ObjectBase{"GradientWithStops"},
    WidgetVfuncsClassInit{},
    Gtk::DrawingArea{},
    _template(get_stop_template_path("gradient-stop.svg").c_str()),
    _tip_template(get_stop_template_path("gradient-tip.svg").c_str())
{
    // default color, it will be updated
    _background_color.set_grey(0.5);

    // for theming
    set_name("GradientEdit");

    set_draw_func(sigc::mem_fun(*this, &GradientWithStops::draw_func));

    Controller::add_click(*this, sigc::mem_fun(*this, &GradientWithStops::on_click_pressed ),
                                          sigc::mem_fun(*this, &GradientWithStops::on_click_released),
                          Controller::Button::left);
    Controller::add_motion<nullptr, &GradientWithStops::on_motion, nullptr>(*this, *this);
    Controller::add_key<&GradientWithStops::on_key_pressed>(*this, *this);
    set_focusable(true);
}

GradientWithStops::~GradientWithStops() = default;

void GradientWithStops::set_gradient(SPGradient* gradient) {
    _gradient = gradient;

    // listen to release & changes
    _release  = gradient ? gradient->connectRelease([=](SPObject*){ set_gradient(nullptr); }) : sigc::connection();
    _modified = gradient ? gradient->connectModified([=](SPObject*, guint){ modified(); }) : sigc::connection();

    // TODO: check selected/focused stop index

    modified();

    set_sensitive(gradient != nullptr);
}

void GradientWithStops::modified() {
    // gradient has been modified

    // read all stops
    _stops.clear();

    if (_gradient) {
        SPStop* stop = _gradient->getFirstStop();
        while (stop) {
            _stops.push_back(stop_t {
                .offset = stop->offset, .color = stop->getColor(), .opacity = stop->getOpacity()
            });
            stop = stop->getNextStop();
        }
    }

    update();
}

void GradientWithStops::update() {
    queue_draw();
}

// capture background color when styles change
void GradientWithStops::css_changed(GtkCssStyleChange * /*change*/)
{
    if (auto wnd = dynamic_cast<Gtk::Window*>(this->get_root())) {
        _background_color = get_color_with_class(*wnd, "theme_bg_color");
    }

    // load and cache cursors
    if (!_cursor_mouseover) {
        _cursor_mouseover = Gdk::Cursor::create("grab");
        _cursor_dragging =  Gdk::Cursor::create("grabbing");
        _cursor_insert =    Gdk::Cursor::create("crosshair");
        set_stop_cursor(nullptr);
    }
}

// return on-screen position of the UI stop corresponding to the gradient's color stop at 'index'
GradientWithStops::stop_pos_t GradientWithStops::get_stop_position(size_t index, const layout_t& layout) const {
    if (!_gradient || index >= _stops.size()) {
        return stop_pos_t {};
    }

    // half of the stop template width; round it to avoid half-pixel coordinates
    const auto dx = round((_template.get_width_px() + 1) / 2);

    auto pos = [&](double offset) { return round(layout.x + layout.width * CLAMP(offset, 0, 1)); };
    const auto& v = _stops;

    auto offset = pos(v[index].offset);
    auto left = offset - dx;
    if (index > 0) {
        // check previous stop; it may overlap
        auto prev = pos(v[index - 1].offset) + dx;
        if (prev > left) {
            // overlap
            left = round((left + prev) / 2);
        }
    }

    auto right = offset + dx;
    if (index + 1 < v.size()) {
        // check next stop for overlap
        auto next = pos(v[index + 1].offset) - dx;
        if (right > next) {
            // overlap
            right = round((right + next) / 2);
        }
    }

    return stop_pos_t {
        .left = left,
        .tip = offset,
        .right = right,
        .top = layout.height - _template.get_height_px(),
        .bottom = layout.height
    };
}

// widget's layout; mainly location of the gradient's image and stop handles
GradientWithStops::layout_t GradientWithStops::get_layout() const {
    const auto stop_width = _template.get_width_px();
    const auto half_stop = round((stop_width + 1) / 2);
    const auto x = half_stop;
    const double width = get_width() - stop_width;
    const double height = get_height();

    return layout_t {
        .x = x,
        .y = 0,
        .width = width,
        .height = height
    };
}

// check if stop handle is under (x, y) location, return its index or -1 if not hit
int GradientWithStops::find_stop_at(double x, double y) const {
    if (!_gradient) return -1;

    const auto& v = _stops;
    const auto& layout = get_layout();

    // find stop handle at (x, y) position; note: stops may not be ordered by offsets
    for (size_t i = 0; i < v.size(); ++i) {
        auto pos = get_stop_position(i, layout);
        if (x >= pos.left && x <= pos.right && y >= pos.top && y <= pos.bottom) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

// this is range of offset adjustment for a given stop
GradientWithStops::limits_t GradientWithStops::get_stop_limits(int maybe_index) const {
    if (!_gradient) return limits_t {};

    // let negative index turn into a large out-of-range number
    auto index = static_cast<size_t>(maybe_index);

    const auto& v = _stops;

    if (index < v.size()) {
        double min = 0;
        double max = 1;

        if (v.size() > 1) {
                std::vector<double> offsets;
                offsets.reserve(v.size());
                for (auto& s : _stops) {
                    offsets.push_back(s.offset);
                }
                std::sort(offsets.begin(), offsets.end());

            // special cases:
            if (index == 0) { // first stop
                max = offsets[index + 1];
            }
            else if (index + 1 == v.size()) { // last stop
                min = offsets[index - 1];
            }
            else {
                // stops "inside" gradient
                min = offsets[index - 1];
                max = offsets[index + 1];
            }
        }
        return limits_t { .min_offset = min, .max_offset = max, .offset = v[index].offset };
    }
    else {
        return limits_t {};
    }
}

std::optional<bool> GradientWithStops::focus(Gtk::DirectionType const direction)
{
    // On arrow key, let ::key-pressed move focused stop (horz) / nothing (vert)
    if (!(direction == Gtk::DirectionType::TAB_FORWARD || direction == Gtk::DirectionType::TAB_BACKWARD)) {
        return true;
    }

    auto const backward = direction == Gtk::DirectionType::TAB_BACKWARD;
    auto const n_stops = _stops.size();

    if (has_focus()) {
        auto const new_stop = _focused_stop + (backward ? -1 : +1);
        // out of range: keep _focused_stop, but give up focus on widget overall
        if (!(new_stop >= 0 && new_stop < n_stops)) {
            return false; // let focus go
        }
        // in range: next/prev stop
        set_focused_stop(new_stop);
    } else {
        // didnʼt have focus: grab on 1st or last stop, relevant to direction
        grab_focus();
        if (n_stops > 0) { // …unless we have no stop, then just focus widget
            set_focused_stop(backward ? n_stops - 1 : 0);
        }
    }

    return true;
}

bool GradientWithStops::on_key_pressed(GtkEventControllerKey const * /*controller*/,
                                       unsigned const keyval, unsigned /*keycode*/,
                                       GdkModifierType const state)
{
    // currently all keyboard activity involves acting on focused stop handle; bail if nothing's selected
    if (_focused_stop < 0) return false;

    auto delta = _stop_move_increment;
    if (Controller::has_flag(state, GDK_SHIFT_MASK)) {
        delta *= 10;
    }

    switch (keyval) {
        case GDK_KEY_Left:
        case GDK_KEY_KP_Left:
            move_stop(_focused_stop, -delta);
            return true;

        case GDK_KEY_Right:
        case GDK_KEY_KP_Right:
            move_stop(_focused_stop, delta);
            return true;

        case GDK_KEY_BackSpace:
        case GDK_KEY_Delete:
            _signal_delete_stop.emit(_focused_stop);
            return true;
    }

    return false;
}

Gtk::EventSequenceState GradientWithStops::on_click_pressed(Gtk::GestureClick const & /*click*/,
                                                            int const n_press,
                                                            double const x, double const y)
{
    if (!_gradient) return Gtk::EventSequenceState::NONE;

    if (n_press == 1) {
        // single button press selects stop and can start dragging it

        if (!has_focus()) {
            // grab focus, so we can show selection indicator and move selected stop with left/right keys
            grab_focus();
        }

        // find stop handle
        auto const index = find_stop_at(x, y);

        if (index < 0) {
            set_focused_stop(-1); // no stop
            return Gtk::EventSequenceState::NONE;
        }

        set_focused_stop(index);

        // check if clicked stop can be moved
        auto limits = get_stop_limits(index);
        if (limits.min_offset < limits.max_offset) {
            // TODO: to facilitate selecting stops without accidentally moving them,
            // delay dragging mode until mouse cursor moves certain distance...
            _dragging = true;
            _pointer_x = x;
            _stop_offset = _stops.at(index).offset;

            if (_cursor_dragging) {
                set_stop_cursor(&_cursor_dragging);
            }
        }
    } else if (n_press == 2) {
        // double-click may insert a new stop
        auto const index = find_stop_at(x, y);
        if (index >= 0) return Gtk::EventSequenceState::NONE;

        auto layout = get_layout();
        if (layout.width > 0 && x > layout.x && x < layout.x + layout.width) {
            auto const position = (x - layout.x) / layout.width;
            // request new stop
            _signal_add_stop_at.emit(position);
        }
    }

    return Gtk::EventSequenceState::NONE;
}

Gtk::EventSequenceState GradientWithStops::on_click_released(Gtk::GestureClick const & /*click*/,
                                                             int /*n_press*/,
                                                             double const x, double const y)
{
    set_stop_cursor(get_cursor(x, y));
    _dragging = false;
    return Gtk::EventSequenceState::NONE;
}

// move stop by a given amount (delta)
void GradientWithStops::move_stop(int stop_index, double offset_shift) {
    auto layout = get_layout();
    if (layout.width > 0) {
        auto limits = get_stop_limits(stop_index);
        if (limits.min_offset < limits.max_offset) {
            auto new_offset = CLAMP(limits.offset + offset_shift, limits.min_offset, limits.max_offset);
            if (new_offset != limits.offset) {
                _signal_stop_offset_changed.emit(stop_index, new_offset);
            }
        }
    }
}

void GradientWithStops::on_motion(GtkEventControllerMotion const * /*motion*/,
                                  double const x, double const y)
{
    if (!_gradient) return;

    if (_dragging) {
        // move stop to a new position (adjust offset)
        auto dx = x - _pointer_x;
        auto layout = get_layout();
        if (layout.width > 0) {
            auto delta = dx / layout.width;
            auto limits = get_stop_limits(_focused_stop);
            if (limits.min_offset < limits.max_offset) {
                auto new_offset = CLAMP(_stop_offset + delta, limits.min_offset, limits.max_offset);
                _signal_stop_offset_changed.emit(_focused_stop, new_offset);
            }
        }
    } else { // !drag but may need to change cursor
        set_stop_cursor(get_cursor(x, y));
    }
}

Glib::RefPtr<Gdk::Cursor> const *
GradientWithStops::get_cursor(double const x, double const y) const
{
    if (!_gradient) return nullptr;

    // check if mouse if over stop handle that we can adjust
    auto index = find_stop_at(x, y);
    if (index >= 0) {
        auto limits = get_stop_limits(index);
        if (limits.min_offset < limits.max_offset && _cursor_mouseover) {
            return &_cursor_mouseover;
        }
    } else if (_cursor_insert) {
        return &_cursor_insert;
    }

    return nullptr;
}

void GradientWithStops::set_stop_cursor(Glib::RefPtr<Gdk::Cursor> const * const cursor)
{
    if (_cursor_current == cursor) return;

    if (cursor != nullptr) {
        set_cursor(*cursor);
    } else {
        set_cursor(""); // empty/default
    }

    _cursor_current = cursor;
}

void GradientWithStops::draw_func(Cairo::RefPtr<Cairo::Context> const &cr,
                                  int /*width*/, int /*height*/)
{
    const double scale = get_scale_factor();
    const auto layout = get_layout();

    if (layout.width <= 0) return;

    // empty gradient checkboard or gradient itself
    cr->rectangle(layout.x, layout.y, layout.width, GRADIENT_IMAGE_HEIGHT);
    draw_gradient(cr, _gradient, layout.x, layout.width);

    if (!_gradient) return;

    // draw stop handles

    cr->begin_new_path();

    auto const fg = get_color();
    auto const &bg = _background_color;

    // stop handle outlines and selection indicator use theme colors:
    _template.set_style(".outer", "fill", rgba_to_css_color(fg));
    _template.set_style(".inner", "stroke", rgba_to_css_color(bg));
    _template.set_style(".hole", "fill", rgba_to_css_color(bg));

    auto tip = _tip_template.render(scale);

    for (size_t i = 0; i < _stops.size(); ++i) {
        const auto& stop = _stops[i];

        // stop handle shows stop color and opacity:
        _template.set_style(".color", "fill", rgba_to_css_color(stop.color));
        _template.set_style(".opacity", "opacity", double_to_css_value(stop.opacity));

        // show/hide selection indicator
        const auto is_selected = _focused_stop == static_cast<int>(i);
        _template.set_style(".selected", "opacity", double_to_css_value(is_selected ? 1 : 0));

        // render stop handle
        auto pix = _template.render(scale);

        if (!pix) {
            g_warning("Rendering gradient stop failed.");
            break;
        }

        auto pos = get_stop_position(i, layout);

        // selected handle sports a 'tip' to make it easily noticeable
        if (is_selected && tip) {
            cr->save();
            // scale back to physical pixels
            cr->scale(1 / scale, 1 / scale);
            // paint tip bitmap
            Gdk::Cairo::set_source_pixbuf(cr, tip, round(pos.tip * scale - tip->get_width() / 2),
                                                   layout.y * scale);
            cr->paint();
            cr->restore();
        }

        // calc space available for stop marker
        cr->save();
        cr->rectangle(pos.left, layout.y, pos.right - pos.left, layout.height);
        cr->clip();
        // scale back to physical pixels
        cr->scale(1 / scale, 1 / scale);
        // paint bitmap
        Gdk::Cairo::set_source_pixbuf(cr, pix, round(pos.tip * scale - pix->get_width() / 2),
                                               pos.top * scale);
        cr->paint();
        cr->restore();
        cr->reset_clip();
    }
}

// focused/selected stop indicator
void GradientWithStops::set_focused_stop(int index) {
    if (_focused_stop == index) return;

    _focused_stop = index;
    _signal_stop_selected.emit(index);
    update();
}

} // namespace Inkscape::UI::Widget

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
