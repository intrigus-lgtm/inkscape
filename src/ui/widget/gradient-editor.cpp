// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * Gradient editor widget for "Fill and Stroke" dialog
 *
 * Author:
 *   Michael Kowalski
 *
 * Copyright (C) 2020-2021 Michael Kowalski
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "gradient-editor.h"

#include <array>
#include <initializer_list>
#include <utility>
#include <2geom/point.h>
#include <2geom/line.h>
#include <2geom/transforms.h>
#include <cairo.h>
#include <glibmm/i18n.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/builder.h>
#include <gtkmm/button.h>
#include <gtkmm/expander.h>
#include <gtkmm/grid.h>
#include <gtkmm/grid.h>
#include <gtkmm/image.h>
#include <gtkmm/liststore.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/treeview.h>
#include <sigc++/adaptors/bind.h>
#include <sigc++/functors/mem_fun.h>

#include "document-undo.h"
#include "gradient-chemistry.h"
#include "gradient-selector.h"
#include "preferences.h"
#include "display/cairo-utils.h"
#include "io/resource.h"
#include "object/sp-gradient-vector.h"
#include "object/sp-linear-gradient.h"
#include "svg/css-ostringstream.h"
#include "ui/builder-utils.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/widget/color-notebook.h"
#include "ui/widget/color-preview.h"
#include "ui/widget/popover-menu.h"
#include "ui/widget/popover-menu-item.h"

namespace Inkscape::UI::Widget {

using namespace Inkscape::IO;
using Inkscape::UI::Widget::ColorNotebook;

class scope {
public:
    scope(bool& flag): _flag(flag) {
        flag = true;
    }

    ~scope() {
        _flag = false;
    }

private:
    bool& _flag;
};

void set_icon(Gtk::Button &btn, char const *pixmap)
{
    btn.set_image_from_icon_name(pixmap, Gtk::IconSize::NORMAL);
}

// draw solid color circle with black outline; right side is to show checkerboard if color's alpha is > 0
Glib::RefPtr<Gdk::Pixbuf> draw_circle(int size, guint32 rgba) {
    int width = size;
    int height = size;
    gint w2 = width / 2;

    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cr = cairo_create(s);

    int x = 0, y = 0;
    double radius = size / 2;
    double degrees = M_PI / 180.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + radius, y + radius, radius, 0, 2 * M_PI);
    cairo_close_path(cr);
    // semi-transparent black outline
    cairo_set_source_rgba(cr, 0, 0, 0, 0.2);
    cairo_fill(cr);

    radius--;

    cairo_new_sub_path(cr);
    cairo_line_to(cr, x + w2, 0);
    cairo_line_to(cr, x + w2, height);
    cairo_arc(cr, x + w2, y + w2, radius, 90 * degrees, 270 * degrees);
    cairo_close_path(cr);

    // solid part
    ink_cairo_set_source_rgba32(cr, rgba | 0xff);
    cairo_fill(cr);

    x = w2;

    cairo_new_sub_path(cr);
    cairo_arc(cr, x, y + w2, radius, -90 * degrees, 90 * degrees);
    cairo_line_to(cr, x, y);
    cairo_close_path(cr);

    // (semi)transparent part
    if ((rgba & 0xff) != 0xff) {
        cairo_pattern_t* checkers = ink_cairo_pattern_create_checkerboard();
        cairo_set_source(cr, checkers);
        cairo_fill_preserve(cr);
        cairo_pattern_destroy(checkers);
    }
    ink_cairo_set_source_rgba32(cr, rgba);
    cairo_fill(cr);
    
    cairo_destroy(cr);
    cairo_surface_flush(s);

    GdkPixbuf* pixbuf = ink_pixbuf_create_from_cairo_surface(s);
    return Glib::wrap(pixbuf);
}


Glib::RefPtr<Gdk::Pixbuf> get_stop_pixmap(SPStop* stop) {
    const int size = 30;
    return draw_circle(size, stop->getColor().toRGBA32(stop->getOpacity()));
}

Glib::ustring get_repeat_icon(SPGradientSpread mode) {
    const char* ico = "";
    switch (mode) {
        case SP_GRADIENT_SPREAD_PAD:
            ico = "gradient-spread-pad";
            break;
        case SP_GRADIENT_SPREAD_REPEAT:
            ico = "gradient-spread-repeat";
            break;
        case SP_GRADIENT_SPREAD_REFLECT:
            ico = "gradient-spread-reflect";
            break;
        default:
            g_warning("Missing case in %s\n", __func__);
            break;
    }
    return ico;
}

GradientEditor::GradientEditor(const char* prefs) :
    _builder(Inkscape::UI::create_builder("gradient-edit.glade")),
    _selector(Gtk::make_managed<GradientSelector>()),
    _repeat_popover{std::make_unique<UI::Widget::PopoverMenu>(nullptr, Gtk::PositionType::BOTTOM)},
    _repeat_icon(get_widget<Gtk::Image>(_builder, "repeatIco")),
    _stop_tree(get_widget<Gtk::TreeView>(_builder, "stopList")),
    _offset_btn(get_widget<Gtk::SpinButton>(_builder, "offsetSpin")),
    _show_stops_list(get_widget<Gtk::Expander>(_builder, "stopsBtn")),
    _add_stop(get_widget<Gtk::Button>(_builder, "stopAdd")),
    _delete_stop(get_widget<Gtk::Button>(_builder, "stopDelete")),
    _stops_gallery(get_widget<Gtk::Box>(_builder, "stopsGallery")),
    _colors_box(get_widget<Gtk::Box>(_builder, "colorsBox")),
    _linear_btn(get_widget<Gtk::ToggleButton>(_builder, "linearBtn")),
    _radial_btn(get_widget<Gtk::ToggleButton>(_builder, "radialBtn")),
    _turn_gradient(get_widget<Gtk::Button>(_builder, "turnBtn")),
    _angle_adj(get_object<Gtk::Adjustment>(_builder, "adjustmentAngle")),
    _main_grid(get_widget<Gtk::Grid>(_builder, "mainGrid")),
    _prefs(prefs)
{
    // gradient type buttons; not currently used, hidden, WIP
    set_icon(_linear_btn, INKSCAPE_ICON("paint-gradient-linear"));
    set_icon(_radial_btn, INKSCAPE_ICON("paint-gradient-radial"));

    auto& reverse = get_widget<Gtk::Button>(_builder, "reverseBtn");
    set_icon(reverse, INKSCAPE_ICON("object-flip-horizontal"));
    reverse.signal_clicked().connect([=](){ reverse_gradient(); });

    set_icon(_turn_gradient, INKSCAPE_ICON("object-rotate-right"));
    _turn_gradient.signal_clicked().connect([=](){ turn_gradient(90, true); });
    _angle_adj->signal_value_changed().connect([=](){
        turn_gradient(_angle_adj->get_value(), false);
    });

    auto& gradBox = get_widget<Gtk::Box>(_builder, "gradientBox");
    const int dot_size = 8;
    _gradient_image.set_visible(true);
    _gradient_image.set_margin_start(dot_size / 2);
    _gradient_image.set_margin_end(dot_size / 2);
    // gradient stop selected in a gradient widget; sync list selection
    _gradient_image.signal_stop_selected().connect([=](size_t index) {
        select_stop(index);
        fire_stop_selected(get_current_stop());
    });
    _gradient_image.signal_stop_offset_changed().connect([=](size_t index, double offset) {
        set_stop_offset(index, offset);
    });
    _gradient_image.signal_add_stop_at().connect([=](double offset) {
        insert_stop_at(offset);
    });
    _gradient_image.signal_delete_stop().connect([=](size_t index) {
        delete_stop(index);
    });
    gradBox.append(_gradient_image);

    // add color selector
    auto const color_selector = Gtk::make_managed<ColorNotebook>(_selected_color);
    color_selector->set_label(_("Stop color"));
    color_selector->set_visible(true);
    _colors_box.append(*color_selector);

    // gradient library in a popup
    get_widget<Gtk::Popover>(_builder, "libraryPopover").set_child(*_selector);
    const int h = 5;
    const int v = 3;
    _selector->set_margin_start(h);
    _selector->set_margin_end(h);
    _selector->set_margin_top(v);
    _selector->set_margin_bottom(v);
    _selector->set_visible(true);
    _selector->show_edit_button(false);
    _selector->set_gradient_size(160, 20);
    _selector->set_name_col_size(120);
    // gradient changed is currently the only signal that GradientSelector can emit:
    _selector->signal_changed().connect([=](SPGradient* gradient) {
        // new gradient selected from the library
        _signal_changed.emit(gradient);
    });

    // construct store for a list of stops
    _stop_columns.add(_stopObj);
    _stop_columns.add(_stopIdx);
    _stop_columns.add(_stopID);
    _stop_columns.add(_stop_color);
    _stop_list_store = Gtk::ListStore::create(_stop_columns);
    _stop_tree.set_model(_stop_list_store);
    // indices in the stop list view; currently hidden
    // _stop_tree.append_column("n", _stopID); // 1-based stop index
    _stop_tree.append_column("c", _stop_color); // and its color

    auto selection = _stop_tree.get_selection();
    selection->signal_changed().connect([=]() {
        if (!_update.pending()) {
            stop_selected();
            fire_stop_selected(get_current_stop());
        }
    });

    _show_stops_list.property_expanded().signal_changed().connect(
        [&](){ show_stops(_show_stops_list.get_expanded()); }
    );

    set_icon(_add_stop, "list-add");
    _add_stop.signal_clicked().connect([=](){
        if (auto row = current_stop()) {
            auto index = row->get_value(_stopIdx);
            add_stop(static_cast<int>(index));
        }
    });

    set_icon(_delete_stop, "list-remove");
    _delete_stop.signal_clicked().connect([=]() {
        if (auto row = current_stop()) {
            auto index = row->get_value(_stopIdx);
            delete_stop(static_cast<int>(index));
        }
    });

    // connect gradient repeat modes menu
    static auto const repeats = std::to_array({std::pair
        {SP_GRADIENT_SPREAD_PAD    , _("None"     )},
        {SP_GRADIENT_SPREAD_REPEAT , _("Direct"   )},
        {SP_GRADIENT_SPREAD_REFLECT, _("Reflected")}
    });
    for (auto const &[mode, text] : repeats) {
        auto const icon = get_repeat_icon(mode);
        auto const item = Gtk::make_managed<UI::Widget::PopoverMenuItem>(text, false, icon);
        item->signal_activate().connect(sigc::bind(sigc::mem_fun(
                                            *this, &GradientEditor::set_repeat_mode), mode));
        _repeat_popover->append(*item);
    }
    get_widget<Gtk::MenuButton>(_builder, "repeatMode").set_popover(*_repeat_popover);
    set_repeat_icon(SP_GRADIENT_SPREAD_PAD);
    
    _selected_color.signal_changed.connect([=]() {
        set_stop_color(_selected_color.color(), _selected_color.alpha());
    });
    _selected_color.signal_dragged.connect([=]() {
        set_stop_color(_selected_color.color(), _selected_color.alpha());
    });

    _offset_btn.signal_changed().connect([=]() {
        if (auto row = current_stop()) {
            auto index = row->get_value(_stopIdx);
            double offset = _offset_btn.get_value();
            set_stop_offset(index, offset);
        }
    });

    append(_main_grid);

    // restore visibility of the stop list view
    _stops_list_visible = Inkscape::Preferences::get()->getBool(_prefs + "/stoplist", true);
    _show_stops_list.set_expanded(_stops_list_visible);
    update_stops_layout();
}

GradientEditor::~GradientEditor() noexcept {
}

void GradientEditor::set_stop_color(SPColor color, float opacity) {
    if (_update.pending()) return;

    SPGradient* vector = get_gradient_vector();
    if (!vector) return;

    if (auto row = current_stop()) {
        auto index = row->get_value(_stopIdx);
        SPStop* stop = sp_get_nth_stop(vector, index);
        if (stop && _document) {
            auto scoped(_update.block());

            // update list view too
            row->set_value(_stop_color, get_stop_pixmap(stop));

            sp_set_gradient_stop_color(_document, stop, color, opacity);
        }
    }
}

std::optional<Gtk::TreeRow> GradientEditor::current_stop() {
    auto sel = _stop_tree.get_selection();
    auto it = sel->get_selected();
    if (!it) {
        return std::nullopt;
    }
    else {
        return *it;
    }
}

SPStop* GradientEditor::get_nth_stop(size_t index) {
    if (SPGradient* vector = get_gradient_vector()) {
        return sp_get_nth_stop(vector, index);
    }
    return nullptr;
}

// stop has been selected in a list view
void GradientEditor::stop_selected() {
    if (auto row = current_stop()) {
        SPStop* stop = row->get_value(_stopObj);
        if (stop) {
            auto scoped(_update.block());

            _selected_color.setColor(stop->getColor());
            _selected_color.setAlpha(stop->getOpacity());

            auto stops = sp_get_before_after_stops(stop);
            if (stops.first && stops.second) {
                _offset_btn.set_range(stops.first->offset, stops.second->offset);
            }
            else {
                _offset_btn.set_range(stops.first ? stops.first->offset : 0, stops.second ? stops.second->offset : 1);
            }
            _offset_btn.set_sensitive();
            _offset_btn.set_value(stop->offset);

            int index = row->get_value(_stopIdx);
            _gradient_image.set_focused_stop(index);
        }
    }
    else {
        // no selection
        auto scoped(_update.block());

        _selected_color.setColor(SPColor());

        _offset_btn.set_range(0, 0);
        _offset_btn.set_value(0);
        _offset_btn.set_sensitive(false);
    }
}

void GradientEditor::insert_stop_at(double offset) {
    if (SPGradient* vector = get_gradient_vector()) {
        // only insert new stop if there are some stops present
        if (vector->hasStops()) {
            SPStop* stop = sp_gradient_add_stop_at(vector, offset);
            // just select next stop; newly added stop will be in a list view after selection refresh (on idle)
            auto pos = sp_number_of_stops_before_stop(vector, stop);
            auto selected = select_stop(pos);
            fire_stop_selected(stop);
            if (!selected) {
                select_stop(pos);
            }
        }
    }
}

void GradientEditor::add_stop(int index) {
    if (SPGradient* vector = get_gradient_vector()) {
        if (SPStop* current = sp_get_nth_stop(vector, index)) {
            SPStop* stop = sp_gradient_add_stop(vector, current);
            // just select next stop; newly added stop will be in a list view after selection refresh (on idle)
            select_stop(sp_number_of_stops_before_stop(vector, stop));
            fire_stop_selected(stop);
        }
    }
}

void GradientEditor::delete_stop(int index) {
    if (SPGradient* vector = get_gradient_vector()) {
        if (SPStop* stop = sp_get_nth_stop(vector, index)) {
            // try deleting a stop, if it can be
            sp_gradient_delete_stop(vector, stop);
        }
    }
}

// collapse/expand list of stops in the UI
void GradientEditor::show_stops(bool visible) {
    _stops_list_visible = visible;
    update_stops_layout();
    Inkscape::Preferences::get()->setBool(_prefs + "/stoplist", _stops_list_visible);
}

void GradientEditor::update_stops_layout() {
    if (_stops_list_visible) {
        _stops_gallery.set_visible(true);
    }
    else {
        _stops_gallery.set_visible(false);
    }
}

double line_angle(const Geom::Line& line) {
    auto d = line.finalPoint() - line.initialPoint();
    return std::atan2(d.y(), d.x());
}

// turn linear gradient 90 degrees
void GradientEditor::turn_gradient(double angle, bool relative) {
    if (_update.pending() || !_document || !_gradient) return;

    if (auto linear = cast<SPLinearGradient>(_gradient)) {
        auto scoped(_update.block());

        auto line = Geom::Line(
            Geom::Point(linear->x1.computed, linear->y1.computed),
            Geom::Point(linear->x2.computed, linear->y2.computed)
        );
        auto center = line.pointAt(0.5);
        auto radians = angle / 180 * M_PI;
        if (!relative) {
            radians -= line_angle(line);
        }
        auto rotate = Geom::Translate(-center) * Geom::Rotate(radians) * Geom::Translate(center);
        auto rotated = line.transformed(rotate);

        linear->x1 = rotated.initialPoint().x();
        linear->y1 = rotated.initialPoint().y();
        linear->x2 = rotated.finalPoint().x();
        linear->y2 = rotated.finalPoint().y();

        _gradient->updateRepr();

        DocumentUndo::done(_document, _("Rotate gradient"), INKSCAPE_ICON("color-gradient"));
    }
}

void GradientEditor::reverse_gradient() {
    if (_document && _gradient) {
        // reverse works on a gradient definition, the one with stops:
        SPGradient* vector = get_gradient_vector();

        if (vector) {
            sp_gradient_reverse_vector(vector);
            DocumentUndo::done(_document, _("Reverse gradient"), INKSCAPE_ICON("color-gradient"));
        }
    }
}

void GradientEditor::set_repeat_mode(SPGradientSpread mode) {
    if (_update.pending()) return;

    if (_document && _gradient) {
        auto scoped(_update.block());

        // spread is set on a gradient reference, which is _gradient object
        _gradient->setSpread(mode);
        _gradient->updateRepr();

        DocumentUndo::done(_document, _("Set gradient repeat"), INKSCAPE_ICON("color-gradient"));

        set_repeat_icon(mode);
    }
}

void GradientEditor::set_repeat_icon(SPGradientSpread mode) {
    auto ico = get_repeat_icon(mode);
    if (!ico.empty()) {
        _repeat_icon.set_from_icon_name(ico);
    }
}

void GradientEditor::setGradient(SPGradient* gradient) {
    auto scoped(_update.block());
    auto scoped2(_notification.block());
    _gradient = gradient;
    _document = gradient ? gradient->document : nullptr;
    set_gradient(gradient);
}

SPGradient* GradientEditor::getVector() {
    return _selector->getVector();
}

void GradientEditor::setVector(SPDocument* doc, SPGradient* vector) {
    auto scoped(_update.block());
    _selector->setVector(doc, vector);
}

void GradientEditor::setMode(SelectorMode mode) {
    _selector->setMode(mode);
}

void GradientEditor::setUnits(SPGradientUnits units) {
    _selector->setUnits(units);
}

SPGradientUnits GradientEditor::getUnits() {
    return _selector->getUnits();
}

void GradientEditor::setSpread(SPGradientSpread spread) {
    _selector->setSpread(spread);
}

SPGradientSpread GradientEditor::getSpread() {
    return _selector->getSpread();
}

void GradientEditor::selectStop(SPStop* selected) {
    if (_notification.pending()) return;

    auto scoped(_notification.block());
    // request from the outside to sync stop selection
    const auto& items = _stop_tree.get_model()->children();
    auto it = std::find_if(items.begin(), items.end(), [=](const auto& row) {
        SPStop* stop = row.get_value(_stopObj);
        return stop == selected; 
    });
    if (it != items.end()) {
        select_stop(std::distance(items.begin(), it));
    }
}

SPGradient* GradientEditor::get_gradient_vector() {
    if (!_gradient) return nullptr;
    return sp_gradient_get_forked_vector_if_necessary(_gradient, false);
}

void GradientEditor::set_gradient(SPGradient* gradient) {
    auto scoped(_update.block());

    // remember which stop is selected, so we can restore it
    size_t selected_stop_index = 0;
    if (auto it = _stop_tree.get_selection()->get_selected()) {
        selected_stop_index = it->get_value(_stopIdx);
    }

    _stop_list_store->clear();

    SPGradient* vector = gradient ? gradient->getVector() : nullptr;

    if (vector) {
        vector->ensureVector();
    }

    _gradient_image.set_gradient(vector);

    if (!vector || !vector->hasStops()) return;

    size_t index = 0;
    for (auto& child : vector->children) {
        if (is<SPStop>(&child)) {
            auto stop = cast<SPStop>(&child);
            auto it = _stop_list_store->append();
            it->set_value(_stopObj, stop);
            it->set_value(_stopIdx, index);
            it->set_value(_stopID, Glib::ustring::compose("%1.", index + 1));
            it->set_value(_stop_color, get_stop_pixmap(stop));

            ++index;
        }
    }

    auto mode = gradient->isSpreadSet() ? gradient->getSpread() : SP_GRADIENT_SPREAD_PAD;
    set_repeat_icon(mode);

    auto can_rotate = false;
    // only linear gradient can be rotated currently
    if (auto linear = cast<SPLinearGradient>(gradient)) {
        can_rotate = true;
        auto line = Geom::Line(
            Geom::Point(linear->x1.computed, linear->y1.computed),
            Geom::Point(linear->x2.computed, linear->y2.computed)
        );
        auto angle = line_angle(line) * 180 / M_PI;
        _angle_adj->set_value(angle);
    }
    _turn_gradient.set_sensitive(can_rotate);
    get_widget<Gtk::SpinButton>(_builder, "angle").set_sensitive(can_rotate);
    get_widget<Gtk::Scale>(_builder, "angleSlider").set_sensitive(can_rotate);

    // list not empty?
    if (index > 0) {
        select_stop(std::min(selected_stop_index, index - 1));
        // update related widgets
        stop_selected();
        //
        // emit_stop_selected(get_current_stop());
    }
}

void GradientEditor::set_stop_offset(size_t index, double offset) {
    if (_update.pending()) return;

    // adjust stop's offset after user edits it in offset spin button or drags stop handle 
    SPStop* stop = get_nth_stop(index);
    if (stop) {
        auto scoped(_update.block());

        stop->offset = offset;
        if (auto repr = stop->getRepr()) {
            repr->setAttributeCssDouble("offset", stop->offset);
        }

        DocumentUndo::maybeDone(stop->document, "gradient:stop:offset", _("Change gradient stop offset"), INKSCAPE_ICON("color-gradient"));
    }
}

// select requested stop in a list view
bool GradientEditor::select_stop(size_t index) {
    if (!_gradient) return false;

    bool selected = false;
    const auto& items = _stop_tree.get_model()->children();
    if (index < items.size()) {
        auto it = items.begin();
        std::advance(it, index);
        auto path = _stop_tree.get_model()->get_path(it);
        _stop_tree.get_selection()->select(it);
        _stop_tree.scroll_to_cell(path, *_stop_tree.get_column(0));
        selected = true;
    }

    return selected;
}

SPStop* GradientEditor::get_current_stop() {
    if (auto row = current_stop()) {
        SPStop* stop = row->get_value(_stopObj);
        return stop;
    }
    return nullptr;
}

void GradientEditor::fire_stop_selected(SPStop* stop) {
    if (!_notification.pending()) {
        auto scoped(_notification.block());
        emit_stop_selected(stop);
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
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:
