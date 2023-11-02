// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Combobox for selecting dash patterns - implementation.
 */
/* Author:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *
 * Copyright (C) 2002 Lauris Kaplinski
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "dash-selector.h"

#include <cstring>
#include <numeric>
#include <2geom/coord.h>
#include <glibmm/i18n.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/liststore.h>

#include "preferences.h"
#include "style.h"
#include "ui/dialog-events.h"
#include "ui/pack.h"
#include "ui/util.h"
#include "ui/widget/spinbutton.h"

namespace Inkscape::UI::Widget {

gchar const *const DashSelector::_prefs_path = "/palette/dashes";

static std::vector<std::vector<double>> s_dashes;

DashSelector::DashSelector()
    : Gtk::Box(Gtk::Orientation::HORIZONTAL, 4),
      _preview_width(100),
      _preview_height(16),
      _preview_lineheight(2)
{
    // TODO: find something more sensible here!!
    init_dashes();

    // TODO: GTK4: Replace w/ MenuButton+GridView (not PopoverMenu as we dynamically render imgs)
    _dash_store = Gtk::ListStore::create(dash_columns);
    _dash_combo.set_model(_dash_store);
    _dash_combo.pack_start(_image_renderer);
    _dash_combo.set_cell_data_func(_image_renderer, sigc::mem_fun(*this, &DashSelector::prepareImageRenderer));
    _dash_combo.set_tooltip_text(_("Dash pattern"));
    _dash_combo.set_visible(true);
    _dash_combo.signal_changed().connect( sigc::mem_fun(*this, &DashSelector::on_selection) );
    // show dashes in two columns to eliminate or minimize scrolling
    _dash_combo.set_wrap_width(2);

    UI::pack_start(*this, _dash_combo, true, true);

    _offset = Gtk::Adjustment::create(0.0, 0.0, 1000.0, 0.1, 1.0, 0.0);
    _offset->signal_value_changed().connect(sigc::mem_fun(*this, &DashSelector::offset_value_changed));
    _sb = Gtk::make_managed<UI::Widget::SpinButton>(_offset, 0.1, 2);
    _sb->set_tooltip_text(_("Pattern offset"));
    sp_dialog_defocus_on_enter_cpp(_sb);
    _sb->set_width_chars(4);
    _sb->set_visible(true);

    UI::pack_start(*this, *_sb, false, false);

    for (std::size_t i = 0; i < s_dashes.size(); ++i) {
        Gtk::TreeModel::Row row = *(_dash_store->append());
        row[dash_columns.dash] = i;
    }

    _pattern = &s_dashes.front();
}

DashSelector::~DashSelector() = default;

void DashSelector::prepareImageRenderer( Gtk::TreeModel::const_iterator const &row ) {
    // dashes are rendered on the fly to adapt to current theme colors
    std::size_t index = (*row)[dash_columns.dash];
    Cairo::RefPtr<Cairo::Surface> surface;
    if (index == 1) {
        // add the custom one as a second option; it'll show up at the top of second column
        // TRANSLATORS: 'Custom' here means, that user-defined dash pattern is specified in an entry box
        surface = sp_text_to_pixbuf(_("Custom"));
    }
    else if (index < s_dashes.size()) {
        // add the dash to the combobox
        surface = sp_dash_to_pixbuf(s_dashes[index]);
    }
    else {
        surface = Cairo::RefPtr<Cairo::Surface>(new Cairo::Surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1)));
        g_warning("No surface in prepareImageRenderer.");
    }
    _image_renderer.property_texture() = to_texture(surface);
}

static std::vector<double> map_values(const std::vector<SPILength>& values) {
    std::vector<double> out;
    out.reserve(values.size());
    for (auto&& v : values) {
        out.push_back(v.value);
    }
    return out;
}

void DashSelector::init_dashes() {
    if (s_dashes.empty()) {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        std::vector<Glib::ustring> dash_prefs = prefs->getAllDirs(_prefs_path);
        
        if (!dash_prefs.empty()) {
            SPStyle style;
            s_dashes.reserve(dash_prefs.size() + 1);
            
            for (auto & dash_pref : dash_prefs) {
                style.readFromPrefs( dash_pref );
                
                if (!style.stroke_dasharray.values.empty()) {
                    s_dashes.emplace_back(map_values(style.stroke_dasharray.values));
                } else {
                    s_dashes.emplace_back();
                }
            }
        } else {
            g_warning("Missing stock dash definitions. DashSelector::init_dashes.");
            //  This code may never execute - a new preferences.xml is created for a new user.  Maybe if the user deletes dashes from preferences.xml?
            s_dashes.emplace_back();
        }

        std::vector<double> custom {1, 2, 1, 4}; // 'custom' dashes second on the list, so they are at the top of the second column in a combo box
        s_dashes.insert(s_dashes.begin() + 1, custom);
    }
}

void DashSelector::set_dash(const std::vector<double>& dash, double offset) {
    int pos = -1;    // Allows custom patterns to remain unscathed by this.

    double delta = std::accumulate(dash.begin(), dash.end(), 0.0) / (10000.0 * (dash.empty() ? 1 : dash.size()));

    int index = 0;
    for (auto&& pattern : s_dashes) {
        if (dash.size() == pattern.size() &&
            std::equal(dash.begin(), dash.end(), pattern.begin(),
                       [=](double a, double b) { return Geom::are_near(a, b, delta); })) {
            pos = index;
            break;
        }
        ++index;
    }

    if (pos >= 0) {
        _pattern = &s_dashes.at(pos);
        _dash_combo.set_active(pos);
        _offset->set_value(offset);
    }
    else { // Hit a custom pattern in the SVG, write it into the combobox.
        pos = 1;  // the one slot for custom patterns
        _pattern = &s_dashes[pos];
        _pattern->assign(dash.begin(), dash.end());
        _dash_combo.set_active(pos);
        _offset->set_value(offset);
    }
}

const std::vector<double>& DashSelector::get_dash(double* offset) const {
    if (offset) *offset = _offset->get_value();
    return *_pattern;
}

double DashSelector::get_offset() {
    return _offset ? _offset->get_value() : 0.0;
}

/**
 * Fill a pixbuf with the dash pattern using standard cairo drawing
 */
Cairo::RefPtr<Cairo::Surface> DashSelector::sp_dash_to_pixbuf(const std::vector<double>& pattern) {
    auto device_scale = get_scale_factor();

    auto height = _preview_height * device_scale;
    auto width = _preview_width * device_scale;
    auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, width, height);
    auto const s = surface->cobj();
    cairo_t *ct = cairo_create(s);

    auto const fg = get_color();

    cairo_set_line_width (ct, _preview_lineheight * device_scale);
    cairo_scale (ct, _preview_lineheight * device_scale, 1);
    cairo_move_to (ct, 0, height/2);
    cairo_line_to (ct, width, height/2);
    cairo_set_dash(ct, pattern.data(), pattern.size(), 0);
    cairo_set_source_rgb(ct, fg.get_red(), fg.get_green(), fg.get_blue());
    cairo_stroke (ct);

    cairo_destroy(ct);
    cairo_surface_flush(s);

    cairo_surface_set_device_scale(s, device_scale, device_scale);
    return surface;
}

/**
 * Fill a pixbuf with a text label using standard cairo drawing
 */
Cairo::RefPtr<Cairo::Surface> DashSelector::sp_text_to_pixbuf(const char* text) {
    auto device_scale = get_scale_factor();
    auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, _preview_width  * device_scale,
                                                                     _preview_height * device_scale);
    auto const s = surface->cobj();
    cairo_t *ct = cairo_create(s);

    cairo_select_font_face (ct, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    // todo: how to find default font face and size?
    cairo_set_font_size (ct, 12 * device_scale);
    auto const fg = get_color();
    cairo_set_source_rgb(ct, fg.get_red(), fg.get_green(), fg.get_blue());
    cairo_move_to (ct, 16.0 * device_scale, 13.0 * device_scale);
    cairo_show_text (ct, text);

    cairo_destroy(ct);
    cairo_surface_flush(s);

    cairo_surface_set_device_scale(s, device_scale, device_scale);
    return surface;
}

void DashSelector::on_selection()
{
    _pattern = &s_dashes.at(_dash_combo.get_active()->get_value(dash_columns.dash));
    changed_signal.emit();
}

void DashSelector::offset_value_changed()
{
    Glib::ustring offset = _("Pattern offset");
    offset += " (";
    offset += Glib::ustring::format(_sb->get_value());
    offset += ")";
    _sb->set_tooltip_text(offset.c_str());
    changed_signal.emit();
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
