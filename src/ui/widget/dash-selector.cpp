// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * A widget for selecting dash patterns and setting the dash offset.
 */
/* Authors:
 *   Tavmjong Bah (Rewrite to use Gio::ListStore and Gtk::GridView).
 *
 * Original authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Maximilian Albert <maximilian.albert@gmail.com> (gtkmm-ification)
 *
 * Copyright (C) 2002 Lauris Kaplinski
 * Copyright (C) 2023 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "dash-selector.h"

#include <iostream>
#include <numeric>  // std::accumulate
#include <vector>

#include <giomm.h>
#include <glibmm/i18n.h>
#include <glibmm/regex.h>
#include <gdkmm/general.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/gridview.h>
#include <gtkmm/image.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/popover.h>
#include <gtkmm/signallistitemfactory.h>
#include <gtkmm/singleselection.h>

#include <2geom/coord.h> // Geom::are_near

#include "preferences.h"
#include "style.h"  // Read dash patterns from preferences.
#include "ui/dialog-events.h"
#include "ui/widget/spinbutton.h"

namespace Inkscape::UI::Widget {
namespace {

constexpr auto DRAWING_AREA_WIDTH = 100;
constexpr auto DRAWING_AREA_HEIGHT = 16;

std::vector<std::vector<double>> get_dash_patterns()
{
    std::vector<std::vector<double>> dash_patterns;

    auto prefs = Preferences::get();
    auto const dash_prefs = prefs->getAllDirs("/palette/dashes");

    SPStyle style;
    std::vector<double> dash_pattern;
    for (auto const &dash_pref : dash_prefs) {
        style.readFromPrefs(dash_pref);
        dash_pattern.clear();
        for (auto const &v : style.stroke_dasharray.values) {
            dash_pattern.push_back(v.value);
        }
        dash_patterns.emplace_back(std::move(dash_pattern));
    }

    return dash_patterns;
}

class DashPattern : public Glib::Object
{
public:
    std::vector<double> dash_pattern;
    bool custom = false;

    static Glib::RefPtr<DashPattern> create(std::vector<double> dash_pattern) {
        return Glib::make_refptr_for_instance<DashPattern>(new DashPattern(std::move(dash_pattern)));
    }

private:
    DashPattern(std::vector<double> dash_pattern)
        : dash_pattern(std::move(dash_pattern))
    {}
};

} // namespace

DashSelector::DashSelector()
    : Gtk::Box(Gtk::Orientation::HORIZONTAL, 4)
{
    set_name("DashSelector");
    set_hexpand();
    set_halign(Gtk::Align::FILL);
    set_valign(Gtk::Align::CENTER);

    // Create liststore
    auto dash_patterns = get_dash_patterns();
    auto liststore = Gio::ListStore<DashPattern>::create();
    for (auto const &dash_pattern : dash_patterns) {
        liststore->append(DashPattern::create(dash_pattern));
    }

    // Add custom pattern slot (upper right corner).
    auto custom_pattern = DashPattern::create({1, 2, 1, 4});
    custom_pattern->custom = true;
    liststore->insert(1, custom_pattern);

    selection = Gtk::SingleSelection::create(liststore);
    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect(sigc::mem_fun(*this, &DashSelector::setup_listitem_cb));
    factory->signal_bind() .connect(sigc::mem_fun(*this, &DashSelector::bind_listitem_cb));

    auto gridview = Gtk::make_managed<Gtk::GridView>(selection, factory);
    gridview->set_min_columns(2);
    gridview->set_max_columns(2);
    gridview->set_single_click_activate(true);
    gridview->signal_activate().connect(sigc::bind<0>(sigc::mem_fun(*this, &DashSelector::activate), gridview));

    popover = Gtk::make_managed<Gtk::Popover>();
    popover->set_child(*gridview);

    // Menubutton
    drawing_area = Gtk::make_managed<Gtk::DrawingArea>();
    drawing_area->set_content_width(DRAWING_AREA_WIDTH);
    drawing_area->set_content_height(DRAWING_AREA_HEIGHT);
    drawing_area->set_draw_func(sigc::bind(sigc::mem_fun(*this, &DashSelector::draw_pattern), std::vector<double>{}));

    auto menubutton = Gtk::make_managed<Gtk::MenuButton>();
    menubutton->set_child(*drawing_area);
    gtk_menu_button_set_always_show_arrow(menubutton->gobj(), true); // No C++ API!
    menubutton->set_popover(*popover);

    append(*menubutton);

    // Offset spinbutton
    adjustment = Gtk::Adjustment::create(0.0, 0.0, 1000.0, 0.1, 1.0, 0.0);
    adjustment->signal_value_changed().connect([this] {
        offset = adjustment->get_value();
        changed_signal.emit();
    });
    auto spinbutton = Gtk::make_managed<Inkscape::UI::Widget::SpinButton>(adjustment, 0.1, 2); // Climb rate, digits.
    spinbutton->set_tooltip_text(_("Dash pattern offset"));
    spinbutton->set_width_chars(5);
    sp_dialog_defocus_on_enter(*spinbutton);

    append(*spinbutton);
}

DashSelector::~DashSelector() = default;

// Set dash pattern from outside class.
void DashSelector::set_dash_pattern(std::vector<double> const &new_dash_pattern, double new_offset)
{
    // See if there is already a dash pattern that matches (within delta).

    // Set the criteria for matching (sum of dash lengths / number of dashes):
    double const delta = std::accumulate(new_dash_pattern.begin(), new_dash_pattern.end(), 0.0)
                       / (10000.0 * (new_dash_pattern.empty() ? 1.0 : new_dash_pattern.size()));

    int position = 1; // Position for custom dash patterns.
    auto const item_count = selection->get_n_items();
    for (int index = 0; index < item_count; ++index) {
        auto const &item = dynamic_cast<DashPattern &>(*selection->get_object(index));
        if (std::equal(new_dash_pattern.begin(), new_dash_pattern.end(), item.dash_pattern.begin(), item.dash_pattern.end(),
                       [=] (double a, double b) { return Geom::are_near(a, b, delta); }))
        {
            position = index;
            break;
        }
    }

    // Set selected pattern in GridView.
    selection->set_selected(position);

    if (position == 1) {
        // Custom pattern!

        // Update custom dash patterns.
        auto &item = dynamic_cast<DashPattern &>(*selection->get_object(position));
        item.dash_pattern.assign(new_dash_pattern.begin(), new_dash_pattern.end());
    }

    // Update MenuButton DrawingArea and offset.
    dash_pattern = new_dash_pattern;
    offset = new_offset;
    update(position);
}

// Update display, offset. Common code for when dash changes (must not trigger signal!).
void DashSelector::update(int position)
{
    // Update MenuButton DrawingArea.
    if (position == 1) {
        drawing_area->set_draw_func(sigc::mem_fun(*this, &DashSelector::draw_text));
    } else {
        drawing_area->set_draw_func(sigc::bind(sigc::mem_fun(*this, &DashSelector::draw_pattern), dash_pattern));
    }

    // If no dash pattern, reset offset to zero.
    offset = dash_pattern.empty() ? 0.0 : offset;
    adjustment->set_value(offset);
}

// User selected new dash pattern in GridView.
void DashSelector::activate(Gtk::GridView *grid, unsigned position)
{
    auto &model = dynamic_cast<Gtk::SingleSelection &>(*grid->get_model());
    auto const &item = dynamic_cast<DashPattern &>(*model.get_selected_item());

    // Update MenuButton DrawingArea, offset, emit changed signal.
    dash_pattern = item.dash_pattern;
    update(position);
    popover->popdown();

    changed_signal.emit(); // Ensure Pattern widget updated.
}

void DashSelector::setup_listitem_cb(Glib::RefPtr<Gtk::ListItem> const &list_item)
{
    auto drawing_area = Gtk::make_managed<Gtk::DrawingArea>();
    drawing_area->set_content_width(DRAWING_AREA_WIDTH);
    drawing_area->set_content_height(DRAWING_AREA_HEIGHT);
    list_item->set_child(*drawing_area);
}

void DashSelector::bind_listitem_cb(Glib::RefPtr<Gtk::ListItem> const &list_item)
{
    auto const &dash_pattern = dynamic_cast<DashPattern &>(*list_item->get_item());
    auto &drawing_area = dynamic_cast<Gtk::DrawingArea &>(*list_item->get_child());

    if (dash_pattern.custom) {
        drawing_area.set_draw_func(sigc::mem_fun(*this, &DashSelector::draw_text));
    } else {
        drawing_area.set_draw_func(sigc::bind(sigc::mem_fun(*this, &DashSelector::draw_pattern), dash_pattern.dash_pattern));
    }
}

// Draw dash pattern in a Gtk::DrawingArea.
void DashSelector::draw_pattern(Cairo::RefPtr<Cairo::Context> const &cr, int width, int height,
                                std::vector<double> const &pattern)
{
    cr->set_line_width(2);
    cr->scale(2, 1);
    cr->set_dash(pattern, 0);
    Gdk::Cairo::set_source_rgba(cr, get_color());
    cr->move_to(0, height/2);
    cr->line_to(width, height / 2);
    cr->stroke();
}

// Draw text in a Gtk::DrawingArea.
void DashSelector::draw_text(Cairo::RefPtr<Cairo::Context> const &cr, int width, int height)
{
    cr->select_font_face("Sans", Cairo::ToyFontFace::Slant::NORMAL, Cairo::ToyFontFace::Weight::NORMAL);
    cr->set_font_size(12);
    Gdk::Cairo::set_source_rgba(cr, get_color());
    cr->move_to(16.0, (height + 12) / 2.0);
    cr->show_text(_("Custom"));
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
