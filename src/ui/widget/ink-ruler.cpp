// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Ruler widget. Indicates horizontal or vertical position of a cursor in a specified widget.
 *
 * Copyright (C) 2019, 2023 Tavmjong Bah
 *               2022 Martin Owens
 *
 * Rewrite of the 'C' ruler code which came originally from Gimp.
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 *
 */

#include <algorithm>
#include <cmath>
#include <sigc++/functors/mem_fun.h>
#include <cairomm/context.h>
#include <glibmm/ustring.h>
#include <giomm/menu.h>
#include <giomm/menuitem.h>
#include <gdkmm/general.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/popovermenu.h>

#include "ink-ruler.h"
#include "inkscape.h"
#include "ui/controller.h"
#include "ui/popup-menu.h"
#include "ui/themes.h"
#include "ui/util.h"
#include "util/units.h"

struct SPRulerMetric
{
  gdouble ruler_scale[16];
  gint    subdivide[5];
};

// Ruler metric for general use.
static SPRulerMetric const ruler_metric_general = {
  { 1, 2, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000, 25000, 50000, 100000 },
  { 1, 5, 10, 50, 100 }
};

// Ruler metric for inch scales.
static SPRulerMetric const ruler_metric_inches = {
  { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768 },
  { 1, 2, 4, 8, 16 }
};

// Half width of pointer triangle.
static double half_width = 5.0;

namespace Inkscape::UI::Widget {

Ruler::Ruler(Gtk::Orientation orientation)
    : Glib::ObjectBase{"InkRuler"}
    , WidgetVfuncsClassInit{}
    , Gtk::DrawingArea{}
    , _orientation(orientation)
    , _popover{create_context_menu()}
    , _backing_store(nullptr)
    , _lower(0)
    , _upper(1000)
    , _max_size(1000)
    , _unit(nullptr)
    , _backing_store_valid(false)
    , _rect()
    , _position(0)
{
    set_name("InkRuler");
    add_css_class(_orientation == Gtk::Orientation::HORIZONTAL ? "horz" : "vert");

    set_draw_func(sigc::mem_fun(*this, &Ruler::draw_func));

    Controller::add_motion<nullptr, &Ruler::on_motion, nullptr>(*this, *this);
    Controller::add_click(*this, sigc::mem_fun(*this, &Ruler::on_click_pressed), {}, Controller::Button::right);

    set_visible(false);

    auto prefs = Inkscape::Preferences::get();
    _watch_prefs = prefs->createObserver("/options/ruler/show_bbox", sigc::mem_fun(*this, &Ruler::on_prefs_changed));
    on_prefs_changed();

    INKSCAPE.themecontext->getChangeThemeSignal().connect([this]{ css_changed(nullptr); });

    signal_destroy().connect([this]{ unparent_children(); });
}

Ruler::~Ruler()
{
    unparent_children();
}

void Ruler::on_prefs_changed()
{
    auto prefs = Inkscape::Preferences::get();
    _sel_visible = prefs->getBool("/options/ruler/show_bbox", true);

    _backing_store_valid = false;
    queue_draw();
}

// Set display unit for ruler.
void
Ruler::set_unit(Inkscape::Util::Unit const *unit)
{
    if (_unit != unit) {
        _unit = unit;

        _backing_store_valid = false;
        queue_draw();
    }
}

// Set range for ruler, update ticks.
void
Ruler::set_range(double lower, double upper)
{
    if (_lower != lower || _upper != upper) {

        _lower = lower;
        _upper = upper;
        _max_size = _upper - _lower;
        if (_max_size == 0) {
            _max_size = 1;
        }

        _backing_store_valid = false;
        queue_draw();
    }
}

/**
 * Set the location of the currently selected page.
 */
void Ruler::set_page(double lower, double upper)
{
    if (_page_lower != lower || _page_upper != upper) {
        _page_lower = lower;
        _page_upper = upper;

        _backing_store_valid = false;
        queue_draw();
    }
}

/**
 * Set the location of the currently selected page.
 */
void Ruler::set_selection(double lower, double upper)
{
    if (_sel_lower != lower || _sel_upper != upper) {
        _sel_lower = lower;
        _sel_upper = upper;

        _backing_store_valid = false;
        queue_draw();
    }
}

// Add a widget (i.e. canvas) to monitor. Note, we don't worry about removing this signal as
// our ruler is tied tightly to the canvas, if one is destroyed, so is the other.
void
Ruler::add_track_widget(Gtk::Widget& widget)
{
    Controller::add_motion<nullptr, &Ruler::on_motion, nullptr>(widget, *this,
        Gtk::PropagationPhase::TARGET, Controller::When::before); // We connected 1st to event, so continue
}

void
Ruler::size_allocate_vfunc(int const width, int const height, int const baseline)
{
    _popover->present();
}

// Draws marker in response to motion events from canvas.  Position is defined in ruler pixel
// coordinates. The routine assumes that the ruler is the same width (height) as the canvas. If
// not, one could use Gtk::Widget::translate_coordinates() to convert the coordinates.
void
Ruler::on_motion(GtkEventControllerMotion const * motion, double const x, double const y)
{
    // This may come from a widget other than `this`, so translate to accommodate border, etc.
    auto const widget = Glib::wrap(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(motion)));
    double drawing_x{}, drawing_y{};
    widget->translate_coordinates(*this, std::lround(x), std::lround(y), drawing_x, drawing_y);

    double const position = _orientation == Gtk::Orientation::HORIZONTAL ? drawing_x : drawing_y;
    if (position == _position) return;

    _position = position;
    // Find region to repaint (old and new marker positions).
    Cairo::RectangleInt new_rect = marker_rect();
    Cairo::RefPtr<Cairo::Region> region = Cairo::Region::create(new_rect);
    region->do_union(_rect);
    _rect = new_rect;
    // Queue repaint
    queue_draw();
}

Gtk::EventSequenceState
Ruler::on_click_pressed(Gtk::GestureClick const & /*click*/,
                        int /*n_press*/, double const x, double const y)
{
    UI::popup_at(*_popover, *this, x, y);
    return Gtk::EventSequenceState::CLAIMED;
}

std::pair<int, int>
Ruler::get_drawing_size()
{
    return {get_width(), get_height()};
}

// Update backing store when scale changes.
bool
Ruler::draw_scale(const::Cairo::RefPtr<::Cairo::Context>& cr_in)
{
    auto const [awidth, aheight] = get_drawing_size();

    // Create backing store (need surface_in to get scale factor correct).
    Cairo::RefPtr<Cairo::Surface> surface_in = cr_in->get_target();
    _backing_store = Cairo::Surface::create(surface_in, Cairo::CONTENT_COLOR_ALPHA, awidth, aheight);

    // Get context
    Cairo::RefPtr<::Cairo::Context> cr = ::Cairo::Context::create(_backing_store);

    // Color in page indication box
    if (double psize = std::abs(_page_upper - _page_lower)) {
        Gdk::Cairo::set_source_rgba(cr, _page_fill);
        cr->begin_new_path();
        if (_orientation == Gtk::Orientation::HORIZONTAL) {
            cr->rectangle(_page_lower, 0, psize, aheight);
        } else {
            cr->rectangle(0, _page_lower, awidth, psize);
        }
        cr->fill();
    } else {
        g_warning("No size?");
    }

    cr->set_line_width(1.0);

    // aparallel is the longer, oriented dimension of the ruler; aperpendicular shorter.
    auto const [aparallel, aperpendicular] = _orientation == Gtk::Orientation::HORIZONTAL
                                             ? std::pair{awidth , aheight}
                                             : std::pair{aheight, awidth };

    // Draw bottom/right line of ruler
    Gdk::Cairo::set_source_rgba(cr, _foreground);
    if (_orientation == Gtk::Orientation::HORIZONTAL) {
        cr->move_to(0     , aheight - 0.5);
        cr->line_to(awidth, aheight - 0.5);
    } else {
        cr->move_to(awidth - 0.5, 0      );
        cr->line_to(awidth - 0.5, aheight);
    }
    cr->stroke();

    // Draw a shadow which overlaps any previously painted object.
    auto paint_shadow = [&] (double size_x, double size_y, double width, double height) {
        auto trans = change_alpha(_shadow, 0.0);
        auto gr = create_cubic_gradient(Geom::Rect(0, 0, size_x, size_y), _shadow, trans, Geom::Point(0, 0.5), Geom::Point(0.5, 1));
        cr->rectangle(0, 0, width, height);
        cr->set_source(gr);
        cr->fill();
    };
    static constexpr int gradient_size = 4;
    if (_orientation == Gtk::Orientation::HORIZONTAL) {
        paint_shadow(0, gradient_size, awidth, gradient_size);
    } else {
        paint_shadow(gradient_size, 0, gradient_size, aheight);
    }

    // Figure out scale. Largest ticks must be far enough apart to fit largest text in vertical ruler.
    // We actually require twice the distance.
    unsigned int scale = std::ceil (_max_size); // Largest number
    Glib::ustring scale_text = std::to_string(scale);
    unsigned int digits = scale_text.length() + 1; // Add one for negative sign.
    unsigned int minimum = digits * _font_size * 2;

    auto const pixels_per_unit = aparallel / _max_size; // pixel per distance

    SPRulerMetric ruler_metric = ruler_metric_general;
    if (_unit == Util::UnitTable::get().getUnit("in")) {
        ruler_metric = ruler_metric_inches;
    }

    unsigned scale_index;
    for (scale_index = 0; scale_index < G_N_ELEMENTS (ruler_metric.ruler_scale)-1; ++scale_index) {
        if (ruler_metric.ruler_scale[scale_index] * std::abs (pixels_per_unit) > minimum) break;
    }

    // Now we find out what is the subdivide index for the closest ticks we can draw
    unsigned divide_index;
    for (divide_index = 0; divide_index < G_N_ELEMENTS (ruler_metric.subdivide)-1; ++divide_index) {
        if (ruler_metric.ruler_scale[scale_index] * std::abs (pixels_per_unit) < 5 * ruler_metric.subdivide[divide_index+1]) break;
    }

    // We'll loop over all ticks.
    double pixels_per_tick = pixels_per_unit *
        ruler_metric.ruler_scale[scale_index] / ruler_metric.subdivide[divide_index];

    double units_per_tick = pixels_per_tick/pixels_per_unit;
    double ticks_per_unit = 1.0/units_per_tick;

    // Find first and last ticks
    int start = 0;
    int end = 0;
    if (_lower < _upper) {
        start = std::floor (_lower * ticks_per_unit);
        end   = std::ceil  (_upper * ticks_per_unit);
    } else {
        start = std::floor (_upper * ticks_per_unit);
        end   = std::ceil  (_lower * ticks_per_unit);
    }

    // Loop over all ticks
    Gdk::Cairo::set_source_rgba(cr, _foreground);
    for (int i = start; i < end+1; ++i) {

        // Position of tick (add 0.5 to center tick on pixel).
        double position = std::floor(i*pixels_per_tick - _lower*pixels_per_unit) + 0.5;

        // Height of tick
        int size = aperpendicular - 7;
        for (int j = divide_index; j > 0; --j) {
            if (i%ruler_metric.subdivide[j] == 0) break;
            size = size / 2 + 1;
        }

        // Draw text for major ticks.
        if (i%ruler_metric.subdivide[divide_index] == 0) {
            cr->save();

            int label_value = std::round(i * units_per_tick);

            auto &label = _label_cache[label_value];
            if (!label) {
                label = draw_label(surface_in, label_value);
            }

            // Align text to pixel
            int x = 3;
            int y = position + 2.5;
            if (_orientation == Gtk::Orientation::HORIZONTAL) {
                x = position + 2.5;
                y = 3;
            }

            // We don't know the surface height/width, damn you cairo.
            cr->rectangle(x, y, 100, 100);
            cr->clip();
            cr->set_source(label, x, y);
            cr->paint();
            cr->restore();
        }

        // Draw ticks
        Gdk::Cairo::set_source_rgba(cr, _foreground);
        if (_orientation == Gtk::Orientation::HORIZONTAL) {
            cr->move_to(position, aheight - size);
            cr->line_to(position, aheight       );
        } else {
            cr->move_to(awidth - size, position);
            cr->line_to(awidth       , position);
        }
        cr->stroke();
    }

    // Draw a selection bar
    if (_sel_lower != _sel_upper && _sel_visible) {
        const auto radius = 3.0;
        const auto delta = _sel_upper - _sel_lower;
        const auto dxy = delta > 0 ? radius : -radius;
        double sy0 = _sel_lower;
        double sy1 = _sel_upper;
        double sx0 = floor(aperpendicular * 0.7);
        double sx1 = sx0;

        if (_orientation == Gtk::Orientation::HORIZONTAL) {
            std::swap(sy0, sx0);
            std::swap(sy1, sx1);
        }

        cr->set_line_width(2.0);

        if (fabs(delta) > 2 * radius) {
            Gdk::Cairo::set_source_rgba(cr, _select_stroke);
            if (_orientation == Gtk::Orientation::HORIZONTAL) {
                cr->move_to(sx0 + dxy, sy0);
                cr->line_to(sx1 - dxy, sy1);
            }
            else {
                cr->move_to(sx0, sy0 + dxy);
                cr->line_to(sx1, sy1 - dxy);
            }
            cr->stroke();
        }

        // Markers
        Gdk::Cairo::set_source_rgba(cr, _select_fill);
        cr->begin_new_path();
        cr->arc(sx0, sy0, radius, 0, 2 * M_PI);
        cr->arc(sx1, sy1, radius, 0, 2 * M_PI);
        cr->fill();

        Gdk::Cairo::set_source_rgba(cr, _select_stroke);
        cr->begin_new_path();
        cr->arc(sx0, sy0, radius, 0, 2 * M_PI);
        cr->stroke();
        cr->begin_new_path();
        cr->arc(sx1, sy1, radius, 0, 2 * M_PI);
        cr->stroke();
    }

    _backing_store_valid = true;
    return true;
}

/**
 * Generate the label as it's only small surface for caching.
 */
Cairo::RefPtr<Cairo::Surface> Ruler::draw_label(Cairo::RefPtr<Cairo::Surface> const &surface_in, int label_value)
{
    bool rotate = _orientation != Gtk::Orientation::HORIZONTAL;

    Glib::RefPtr<Pango::Layout> layout = create_pango_layout(std::to_string(label_value));

    int text_width;
    int text_height;
    layout->get_pixel_size(text_width, text_height);
    if (rotate) {
        std::swap(text_width, text_height);
    }

    auto surface = Cairo::Surface::create(surface_in, Cairo::CONTENT_COLOR_ALPHA, text_width, text_height);
    Cairo::RefPtr<::Cairo::Context> cr = ::Cairo::Context::create(surface);

    cr->save();
    Gdk::Cairo::set_source_rgba(cr, _foreground);
    if (rotate) {
        cr->translate(text_width / 2, text_height / 2);
        cr->rotate(-M_PI_2);
        cr->translate(-text_height / 2, -text_width / 2);
    }
    layout->show_in_cairo_context(cr);
    cr->restore();

    return surface;
}

// Draw position marker, we use doubles here.
void
Ruler::draw_marker(const Cairo::RefPtr<::Cairo::Context>& cr)
{
    auto const [awidth, aheight] = get_drawing_size();
    Gdk::Cairo::set_source_rgba(cr, _foreground);
    if (_orientation == Gtk::Orientation::HORIZONTAL) {
        cr->move_to(_position             , aheight             );
        cr->line_to(_position - half_width, aheight - half_width);
        cr->line_to(_position + half_width, aheight - half_width);
        cr->close_path();
     } else {
        cr->move_to(awidth             , _position             );
        cr->line_to(awidth - half_width, _position - half_width);
        cr->line_to(awidth - half_width, _position + half_width);
        cr->close_path();
    }
    cr->fill();
}

// This is a pixel aligned integer rectangle that encloses the position marker. Used to define the
// redraw area.
Cairo::RectangleInt
Ruler::marker_rect()
{
    auto const [awidth, aheight] = get_drawing_size();

    Cairo::RectangleInt rect;
    rect.x = 0;
    rect.y = 0;
    rect.width = 0;
    rect.height = 0;

    // Find size of rectangle to enclose triangle.
    if (_orientation == Gtk::Orientation::HORIZONTAL) {
        rect.x = std::floor(_position - half_width);
        rect.y = std::floor(  aheight - half_width);
        rect.width  = std::ceil(half_width * 2.0 + 1);
        rect.height = std::ceil(half_width);
    } else {
        rect.x = std::floor(  awidth - half_width);
        rect.y = std::floor(_position - half_width);
        rect.width  = std::ceil(half_width);
        rect.height = std::ceil(half_width * 2.0 + 1);
    }

    return rect;
}

// Draw the ruler using the tick backing store.
void
Ruler::draw_func(Cairo::RefPtr<Cairo::Context> const &cr, int /*width*/, int /*height*/)
{
    if (!_backing_store_valid) {
        draw_scale (cr);
    }

    cr->set_source (_backing_store, 0, 0);
    cr->paint();

    draw_marker (cr);
}

// Update ruler on style change (font-size, etc.)
void
Ruler::css_changed(GtkCssStyleChange * const change)
{
    // Cache all our colors to speed up rendering.

    _foreground = get_color();
    _font_size = get_font_size(*this);

    _shadow = get_color_with_class(*this, "shadow");
    _page_fill = get_color_with_class(*this, "page");

    add_css_class("selection");
    _select_fill = get_color_with_class(*this, "background");
    _select_stroke = get_color_with_class(*this, "border");
    remove_css_class("selection");

    _label_cache.clear();
    _backing_store_valid = false;

    queue_resize();
    queue_draw();
}

/**
 * Return a contextmenu for the ruler
 */
std::unique_ptr<Gtk::Popover> Ruler::create_context_menu()
{
    auto unit_menu = Gio::Menu::create();

    for (auto &pair : Util::UnitTable::get().units(Inkscape::Util::UNIT_TYPE_LINEAR)) {
        auto unit = pair.second.abbr;
        Glib::ustring action_name = "doc.set-display-unit('" + unit + "')";
        auto item = Gio::MenuItem::create(unit, action_name);
        unit_menu->append_item(item);
    }

    auto popover = std::make_unique<Gtk::PopoverMenu>(unit_menu);
    popover->set_parent(*this);
    popover->set_autohide(true);
    return popover;
}

void Ruler::unparent_children()
{
    _popover->unparent();
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
