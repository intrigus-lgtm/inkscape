// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * Pattern editor widget for "Fill and Stroke" dialog
 *
 * Copyright (C) 2022 Michael Kowalski
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "pattern-editor.h"

#include <iomanip>
#include <cairo.h>
#include <glibmm/i18n.h>
#include <gtkmm/builder.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/entry.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/paned.h>
#include <gtkmm/picture.h>
#include <gtkmm/scale.h>
#include <gtkmm/searchentry2.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/viewport.h>

#include "document.h"
#include "preferences.h"

#include "pattern-manager.h"
#include "pattern-manipulation.h"
#include "ui/builder-utils.h"
#include "ui/pack.h"
#include "ui/util.h"
#include "util-string/ustring-format.h"

namespace Inkscape::UI::Widget {

using namespace Inkscape::IO;

// default size of pattern image in a list
static constexpr int ITEM_WIDTH = 45;

// get slider position 'index' (linear) and transform that into gap percentage (non-linear)
static double slider_to_gap(double index, double upper) {
    auto v = std::tan(index / (upper + 1) * M_PI / 2.0) * 500;
    return std::round(v / 20) * 20;
}
// transform gap percentage value into slider position
static double gap_to_slider(double gap, double upper) {
    return std::atan(gap / 500) * (upper + 1) / M_PI * 2;
}

// tile size slider functions
static int slider_to_tile(double index) {
    return 30 + static_cast<int>(index) * 5;
}
static double tile_to_slider(int tile) {
    return (tile - 30) / 5.0;
}

Glib::ustring get_attrib(SPPattern* pattern, const char* attrib) {
    auto value = pattern->getAttribute(attrib);
    return value ? value : "";
}

double get_attrib_num(SPPattern* pattern, const char* attrib) {
    auto val = get_attrib(pattern, attrib);
    return strtod(val.c_str(), nullptr);
}

const double ANGLE_STEP = 15.0;

PatternEditor::PatternEditor(const char* prefs, Inkscape::PatternManager& manager) :
    _manager(manager),
    _builder(create_builder("pattern-edit.glade")),
    _offset_x(get_widget<Gtk::SpinButton>(_builder, "offset-x")),
    _offset_y(get_widget<Gtk::SpinButton>(_builder, "offset-y")),
    _scale_x(get_widget<Gtk::SpinButton>(_builder, "scale-x")),
    _scale_y(get_widget<Gtk::SpinButton>(_builder, "scale-y")),
    _angle_btn(get_widget<Gtk::SpinButton>(_builder, "angle")),
    _orient_slider(get_widget<Gtk::Scale>(_builder, "orient")),
    _gap_x_slider(get_widget<Gtk::Scale>(_builder, "gap-x")),
    _gap_y_slider(get_widget<Gtk::Scale>(_builder, "gap-y")),
    _gap_x_spin(get_widget<Gtk::SpinButton>(_builder, "gap-x-spin")),
    _gap_y_spin(get_widget<Gtk::SpinButton>(_builder, "gap-y-spin")),
    _edit_btn(get_widget<Gtk::Button>(_builder, "edit-pattern")),
    _preview_img(get_widget<Gtk::Picture>(_builder, "preview")),
    _preview(get_widget<Gtk::Viewport>(_builder, "preview-box")),
    _color_btn(get_widget<Gtk::Button>(_builder, "color-btn")),
    _color_label(get_widget<Gtk::Label>(_builder, "color-label")),
    _paned(get_widget<Gtk::Paned>(_builder, "paned")),
    _main_grid(get_widget<Gtk::Box>(_builder, "main-box")),
    _input_grid(get_widget<Gtk::Grid>(_builder, "input-grid")),
    _stock_gallery(get_widget<Gtk::FlowBox>(_builder, "flowbox")),
    _doc_gallery(get_widget<Gtk::FlowBox>(_builder, "doc-flowbox")),
    _link_scale(get_widget<Gtk::Button>(_builder, "link-scale")),
    _name_box(get_widget<Gtk::Entry>(_builder, "pattern-name")),
    _combo_set(get_widget<Gtk::ComboBoxText>(_builder, "pattern-combo")),
    _search_box(get_widget<Gtk::SearchEntry2>(_builder, "search")),
    _tile_slider(get_widget<Gtk::Scale>(_builder, "tile-slider")),
    _show_names(get_widget<Gtk::CheckButton>(_builder, "show-names")),
    _prefs(prefs)
{
    _color_picker = std::make_unique<ColorPicker>(
        _("Pattern color"), "", 0x7f7f7f00, true,
        &get_widget<Gtk::Button>(_builder, "color-btn"));
    _color_picker->use_transparency(false);
    _color_picker->connectChanged([=](guint color){
        if (_update.pending()) return;
        _signal_color_changed.emit(color);
    });

    // there's enough space for one set of controls only:
    auto set_gap_control = [=](){
        if (_precise_gap_control) {
            _gap_x_slider.set_visible(false);
            _gap_y_slider.set_visible(false);
            _gap_x_spin.set_visible();
            _gap_y_spin.set_visible();
        }
        else {
            _gap_x_spin.set_visible(false);
            _gap_y_spin.set_visible(false);
            _gap_x_slider.set_visible();
            _gap_y_slider.set_visible();
        }
    };

    _tile_size = Inkscape::Preferences::get()->getIntLimited(_prefs + "/tileSize", ITEM_WIDTH, 30, 1000);
    _tile_slider.set_value(tile_to_slider(_tile_size));
    _tile_slider.signal_change_value().connect([=](Gtk::ScrollType st, double value){
        if (_update.pending()) return true;
        auto scoped(_update.block());
        auto size = slider_to_tile(value);
        if (size != _tile_size) {
            _tile_slider.set_value(tile_to_slider(size));
            // change pattern tile size
            _tile_size = size;
            update_pattern_tiles();
            Inkscape::Preferences::get()->setInt(_prefs + "/tileSize", size);
        }
        return true;
    }, true);
    _precise_gap_control = Inkscape::Preferences::get()->getBool(_prefs + "/preciseGapControl", false);
    auto precise_gap = &get_widget<Gtk::CheckButton>(_builder, "gap-spin");
    auto& mouse_friendly = get_widget<Gtk::CheckButton>(_builder, "gap-slider");
    precise_gap->set_active(_precise_gap_control);
    mouse_friendly.set_active(!_precise_gap_control);
    precise_gap->signal_toggled().connect([=, this] {
        auto precise = precise_gap->get_active();
        if (_precise_gap_control != precise) {
            _precise_gap_control = precise;
            set_gap_control();
            Inkscape::Preferences::get()->setBool(_prefs + "/preciseGapControl", precise);
        }
    });

    auto show_labels = Inkscape::Preferences::get()->getBool(_prefs + "/showLabels", false);
    _show_names.set_active(show_labels);
    _show_names.signal_toggled().connect([=](){
        // toggle pattern labels
        _stock_pattern_store.store.refresh();
        _doc_pattern_store.store.refresh();
        Inkscape::Preferences::get()->setBool(_prefs + "/showLabels", _show_names.get_active());
    });

    const auto max = 180.0 / ANGLE_STEP;
    _orient_slider.set_range(-max, max);
    _orient_slider.set_increments(1, 1);
    _orient_slider.set_digits(0);
    _orient_slider.set_value(0);
    _orient_slider.signal_change_value().connect([=](Gtk::ScrollType st, double value){
        if (_update.pending()) return false;
        auto scoped(_update.block());
        // slider works with 15deg discrete steps
        _angle_btn.set_value(round(CLAMP(value, -max, max)) * ANGLE_STEP);
        _signal_changed.emit();
        return true;
    }, true);

    for (auto spin : {&_gap_x_spin, &_gap_y_spin}) {
        spin->signal_value_changed().connect([=](){
            if (_update.pending() || !spin->is_sensitive()) return;
            _signal_changed.emit();
        });
    }

    for (auto slider : {&_gap_x_slider, &_gap_y_slider}) {
        slider->set_increments(1, 1);
        slider->set_digits(0);
        slider->set_value(0);
        slider->set_format_value_func([=](double val){
            auto upper = slider->get_adjustment()->get_upper();
            return Inkscape::ustring::format_classic(std::fixed, std::setprecision(0), slider_to_gap(val, upper)) + "%";
        });
        slider->signal_change_value().connect([=](Gtk::ScrollType st, double value){
            if (_update.pending()) return false;
            _signal_changed.emit();
            return true;
        }, true);
    }

    set_gap_control();

    _angle_btn.signal_value_changed().connect([=]() {
        if (_update.pending() || !_angle_btn.is_sensitive()) return;
        auto scoped(_update.block());
        auto angle = _angle_btn.get_value();
        _orient_slider.set_value(round(angle / ANGLE_STEP));
        _signal_changed.emit();
    });

    _link_scale.signal_clicked().connect([=](){
        if (_update.pending()) return;
        auto scoped(_update.block());
        _scale_linked = !_scale_linked;
        if (_scale_linked) {
            // this is simplistic
            _scale_x.set_value(_scale_y.get_value());
        }
        update_scale_link();
        _signal_changed.emit();
    });

    for (auto el : {&_scale_x, &_scale_y, &_offset_x, &_offset_y}) {
        el->signal_value_changed().connect([=]() {
            if (_update.pending()) return;
            if (_scale_linked && (el == &_scale_x || el == &_scale_y)) {
                auto scoped(_update.block());
                // enforce uniform scaling
                (el == &_scale_x) ? _scale_y.set_value(el->get_value()) : _scale_x.set_value(el->get_value());
            }
            _signal_changed.emit();
        });
    }

    _name_box.signal_changed().connect([=](){
        if (_update.pending()) return;

        _signal_changed.emit();
    });

    _search_box.signal_search_changed().connect([=](){
        if (_update.pending()) return;

        // filter patterns
        _filter_text = _search_box.get_text();
        apply_filter(false);
        apply_filter(true);
    });

    // populate combo box with all patern categories
    auto pattern_categories = _manager.get_categories()->children();
    int cat_count = pattern_categories.size();
    for (auto row : pattern_categories) {
        auto name = row.get_value(_manager.columns.name);
        _combo_set.append(name);
    }

    get_widget<Gtk::Button>(_builder, "previous").signal_clicked().connect([=](){
        int previous = _combo_set.get_active_row_number() - 1;
        if (previous >= 0) _combo_set.set_active(previous);
    });
    get_widget<Gtk::Button>(_builder, "next").signal_clicked().connect([=](){
        auto next = _combo_set.get_active_row_number() + 1;
        if (next < cat_count) _combo_set.set_active(next);
    });
    _combo_set.signal_changed().connect([=](){
        // select pattern category to show
        auto index = _combo_set.get_active_row_number();
        select_pattern_set(index);
        Inkscape::Preferences::get()->setInt(_prefs + "/currentSet", index);
    });

    bind_store(_doc_gallery, _doc_pattern_store);
    bind_store(_stock_gallery, _stock_pattern_store);

    _stock_gallery.signal_child_activated().connect([=](Gtk::FlowBoxChild* box){
        if (_update.pending()) return;
        auto scoped(_update.block());
        auto pat = _stock_pattern_store.widgets_to_pattern[box];
        update_ui(pat);
        _doc_gallery.unselect_all();
        _signal_changed.emit();
    });

    _doc_gallery.signal_child_activated().connect([=](Gtk::FlowBoxChild* box){
        if (_update.pending()) return;
        auto scoped(_update.block());
        auto pat = _doc_pattern_store.widgets_to_pattern[box];
        update_ui(pat);
        _stock_gallery.unselect_all();
        _signal_changed.emit();
    });

    _edit_btn.signal_clicked().connect([=](){
        _signal_edit.emit();
    });

    _paned.set_position(Inkscape::Preferences::get()->getIntLimited(_prefs + "/handlePos", 50, 10, 9999));
    _paned.property_position().signal_changed().connect([=](){
        Inkscape::Preferences::get()->setInt(_prefs + "/handlePos", _paned.get_position());
    });

    // current pattern category
    _combo_set.set_active(Inkscape::Preferences::get()->getIntLimited(_prefs + "/currentSet", 0, 0, std::max(cat_count - 1, 0)));

    update_scale_link();
    UI::pack_start(*this, _main_grid);
}

void PatternEditor::bind_store(Gtk::FlowBox& list, PatternStore& pat) {
    pat.store.set_filter([=](const Glib::RefPtr<PatternItem>& p){
        if (!p) return false;
        if (_filter_text.empty()) return true;

        auto name = Glib::ustring(p->label).lowercase();
        auto expr = _filter_text.lowercase();
        auto pos = name.find(expr);
        return pos != Glib::ustring::npos;
    });

    list.bind_list_store(pat.store.get_store(), [=, &pat](const Glib::RefPtr<PatternItem>& item){
        auto const box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
        auto const image = Gtk::make_managed<Gtk::Image>(to_texture(item->pix));
        image->set_size_request(_tile_size, _tile_size);
        UI::pack_start(*box, *image);
        auto name = Glib::ustring(item->label.c_str());
        if (_show_names.get_active()) {
            auto const label = Gtk::make_managed<Gtk::Label>(name);
            label->add_css_class("small-font");
            // limit label size to tile size
            label->set_ellipsize(Pango::EllipsizeMode::END);
            label->set_max_width_chars(0);
            label->set_size_request(_tile_size);
            UI::pack_end(*box, *label);
        }
        image->set_tooltip_text(name);

        auto const cbox = Gtk::make_managed<Gtk::FlowBoxChild>();
        cbox->set_child(*box);
        cbox->add_css_class("pattern-item-box");
        pat.widgets_to_pattern[cbox] = item;
        return cbox;
    });
}

void PatternEditor::select_pattern_set(int index) {
    auto sets = _manager.get_categories()->children();
    if (index >= 0 && index < sets.size()) {
        auto row = sets[index];
        if (auto category = row.get_value(_manager.columns.category)) {
            set_stock_patterns(category->patterns);
        }
    }
}

void PatternEditor::update_scale_link() {
    _link_scale.set_child(get_widget<Gtk::Image>(_builder, _scale_linked ? "image-linked" : "image-unlinked"));
}

void PatternEditor::update_widgets_from_pattern(Glib::RefPtr<PatternItem>& pattern) {
    _input_grid.set_sensitive(!!pattern);

    static auto const empty = PatternItem::create();
    auto const &item = pattern ? *pattern : *empty;

    _name_box.set_text(item.label.c_str());

    _scale_x.set_value(item.transform.xAxis().length());
    _scale_y.set_value(item.transform.yAxis().length());

    // TODO if needed
    // auto units = get_attrib(pattern, "patternUnits");

    _scale_linked = item.uniform_scale;
    update_scale_link();

    _offset_x.set_value(item.offset.x());
    _offset_y.set_value(item.offset.y());

    auto degrees = 180.0 / M_PI * Geom::atan2(item.transform.xAxis());
    _orient_slider.set_value(round(degrees / ANGLE_STEP));
    _angle_btn.set_value(degrees);

    double x_index = gap_to_slider(item.gap[Geom::X], _gap_x_slider.get_adjustment()->get_upper());
    _gap_x_slider.set_value(x_index);
    _gap_x_spin.set_value(item.gap[Geom::X]);
    double y_index = gap_to_slider(item.gap[Geom::Y], _gap_y_slider.get_adjustment()->get_upper());
    _gap_y_slider.set_value(y_index);
    _gap_y_spin.set_value(item.gap[Geom::Y]);

    if (item.color.has_value()) {
        _color_picker->setRgba32(item.color->toRGBA32(1.0));
        _color_btn.set_sensitive();
        _color_label.set_opacity(1.0); // hack: sensitivity doesn't change appearance, so using opacity directly
    }
    else {
        _color_picker->setRgba32(0);
        _color_btn.set_sensitive(false);
        _color_label.set_opacity(0.6);
        _color_picker->closeWindow();
    }
}

void PatternEditor::update_ui(Glib::RefPtr<PatternItem> pattern) {
    update_widgets_from_pattern(pattern);
}

// sort patterns in-place by name/id
void sort_patterns(std::vector<Glib::RefPtr<PatternItem>>& list) {
    std::sort(list.begin(), list.end(), [](Glib::RefPtr<PatternItem>& a, Glib::RefPtr<PatternItem>& b) {
        if (!a || !b) return false;
        if (a->label == b->label) {
            return a->id < b->id;
        }
        return a->label < b->label;
    });
}

// given a pattern, create a PatternItem instance that describes it;
// input pattern can be a link or a root pattern
Glib::RefPtr<PatternItem> create_pattern_item(PatternManager& manager, SPPattern* pattern, int tile_size, double scale) {
    auto item = manager.get_item(pattern);
    if (item && scale > 0) {
        item->pix = manager.get_image(pattern, tile_size, tile_size, scale);
    }
    return item;
}

// update editor UI
void PatternEditor::set_selected(SPPattern* pattern) {
    auto scoped(_update.block());

    _stock_gallery.unselect_all();

    // current pattern (should be a link)
    auto link_pattern = pattern;
    if (pattern) pattern = pattern->rootPattern();

    if (pattern && pattern != link_pattern) {
        _current_pattern.id = pattern->getId();
        _current_pattern.link_id = link_pattern->getId();
        _current_pattern.offset = link_pattern->getTransform().translation();
    }
    else {
        _current_pattern.id.clear();
        _current_pattern.link_id.clear();
        _current_pattern.offset = {};
    }

    auto item = create_pattern_item(_manager, link_pattern, 0, 0);

    update_widgets_from_pattern(item);

    auto list = update_doc_pattern_list(pattern ? pattern->document : nullptr);
    if (pattern) {
        // patch up tile image on a list of document root patterns, it might have changed;
        // color attribute for instance is being set directly on the root pattern;
        // other attributes are per-object, so should not be taken into account when rendering tile
        for (auto& pattern_item : list) {
            if (pattern_item->id == item->id && pattern_item->collection == nullptr) {
                // update preview
                const double device_scale = get_scale_factor();
                pattern_item->pix = _manager.get_image(pattern, _tile_size, _tile_size, device_scale);
                item->pix = pattern_item->pix;
                break;
            }
        }
    }

    set_active(_doc_gallery, _doc_pattern_store, item);

    // generate large preview of selected pattern
    if (link_pattern) {
        const double device_scale = get_scale_factor();
        auto size = _preview.get_allocation();
        const int m = 1;
        if (size.get_width() <= m || size.get_height() <= m) {
            // widgets not resized yet, choose arbitrary size, so preview is not missing when widget is shown
            size.set_width(200);
            size.set_height(200);
        }
        // use white for checkerboard since most stock patterns are black
        unsigned int background = 0xffffffff;
        auto surface = _manager.get_preview(link_pattern, size.get_width(), size.get_height(), background, device_scale);
        _preview_img.set_paintable(to_texture(surface));
    } else {
        _preview_img.set_paintable(nullptr);
    }
}

// generate preview images for patterns
std::vector<Glib::RefPtr<PatternItem>> create_pattern_items(PatternManager& manager, const std::vector<SPPattern*>& list, int tile_size, double device_scale) {
    std::vector<Glib::RefPtr<PatternItem>> output;
    output.reserve(list.size());

    for (auto pat : list) {
        if (auto item = create_pattern_item(manager, pat, tile_size, device_scale)) {
            output.push_back(item);
        }
    }

    return output;
}

// populate store with document patterns if list has changed, minimize amount of work by using cached previews
std::vector<Glib::RefPtr<PatternItem>> PatternEditor::update_doc_pattern_list(SPDocument* document) {
    auto list = sp_get_pattern_list(document);
    std::shared_ptr<SPDocument> nil;
    const double device_scale = get_scale_factor();
    // create pattern items (cheap), but skip preview generation (expansive)
    auto patterns = create_pattern_items(_manager, list, 0, 0);
    bool modified = false;
    for (auto&& item : patterns) {
        auto it = _cached_items.find(item->id);
        if (it != end(_cached_items)) {
            // reuse cached preview image
            if (!item->pix) item->pix = it->second->pix;
        }
        else {
            if (!item->pix) {
                // generate preview for newly added pattern
                item->pix = _manager.get_image(cast<SPPattern>(document->getObjectById(item->id)), _tile_size, _tile_size, device_scale);
            }
            modified = true;
            _cached_items[item->id] = item;
        }
    }

    update_store(patterns, _doc_gallery, _doc_pattern_store);

    return patterns;
}

void PatternEditor::set_document(SPDocument* document) {
    _current_document = document;
    _cached_items.clear();
    update_doc_pattern_list(document);
}

// populate store with stock patterns
void PatternEditor::set_stock_patterns(const std::vector<SPPattern*>& list) {
    const double device_scale = get_scale_factor();
    auto patterns = create_pattern_items(_manager, list, _tile_size, device_scale);
    sort_patterns(patterns);
    update_store(patterns, _stock_gallery, _stock_pattern_store);
}

void PatternEditor::apply_filter(bool stock) {
    auto scoped(_update.block());
    if (!stock) {
        _doc_pattern_store.store.apply_filter();
    }
    else {
        _stock_pattern_store.store.apply_filter();
    }
}

void PatternEditor::update_store(const std::vector<Glib::RefPtr<PatternItem>>& list, Gtk::FlowBox& gallery, PatternStore& pat) {
    auto selected = get_active(gallery, pat);
    if (pat.store.assign(list)) {
        // reselect current
        set_active(gallery, pat, selected);
    }
}

Glib::RefPtr<PatternItem> PatternEditor::get_active(Gtk::FlowBox& gallery, PatternStore& pat) {
    auto empty = Glib::RefPtr<PatternItem>();

    auto sel = gallery.get_selected_children();
    if (sel.size() == 1) {
        return pat.widgets_to_pattern[sel.front()];
    }
    else {
        return empty;
    }
}

std::pair<Glib::RefPtr<PatternItem>, SPDocument*> PatternEditor::get_active() {
    SPDocument* stock = nullptr;
    auto sel = get_active(_doc_gallery, _doc_pattern_store);
    if (!sel) {
        sel = get_active(_stock_gallery, _stock_pattern_store);
        stock = sel ? sel->collection : nullptr;
    }
    return std::make_pair(sel, stock);
}

void PatternEditor::set_active(Gtk::FlowBox& gallery, PatternStore& pat, Glib::RefPtr<PatternItem> item) {
    bool selected = false;
    if (item) {
        UI::for_each_child(gallery, [=,&selected,&pat,&gallery](Gtk::Widget& widget){
            if (auto box = dynamic_cast<Gtk::FlowBoxChild*>(&widget)) {
                if (auto pattern = pat.widgets_to_pattern[box]) {
                    if (pattern->id == item->id && pattern->collection == item->collection) {
                        gallery.select_child(*box);
                        if (item->pix) {
                            // update preview, it might be stale
                            for_each_descendant(*box, [&](Gtk::Widget &widget){
                                if (auto const image = dynamic_cast<Gtk::Image *>(&widget)) {
                                    image->set(to_texture(item->pix));
                                    return ForEachResult::_break;
                                }
                                return ForEachResult::_continue;
                            });
                        }
                        selected = true;
                    }
                }
            }
            return UI::ForEachResult::_continue;
        });
    }

    if (!selected) {
        gallery.unselect_all();
    }
}

std::pair<std::string, SPDocument*> PatternEditor::get_selected() {
    // document patterns first
    auto active = get_active();
    auto sel = active.first;
    auto stock_doc = active.second;
    std::string id;
    if (sel) {
        if (stock_doc) {
            // for stock pattern, report its root pattern ID
            return std::make_pair(sel->id, stock_doc);
        }
        else {
            // for current document, if selection hasn't changed return linked pattern ID
            // so that we can modify its properties (transform, offset, gap)
            if (sel->id == _current_pattern.id.raw()) {
                return std::make_pair(_current_pattern.link_id, nullptr);
            }
            // different pattern from current document selected; use its root pattern
            // as a starting point; link pattern will be injected by adjust_pattern()
            return std::make_pair(sel->id, nullptr);
        }
    }
    else {
        // if nothing is selected, pick first stock pattern, so we have something to assign
        // to selected object(s); without it, pattern editing will not be activated
        if (auto first = _stock_pattern_store.store.get_store()->get_item(0)) {
            return std::make_pair(first->id, first->collection);
        }

        // no stock patterns available
        return std::make_pair("", nullptr);
    }
}

std::optional<unsigned int> PatternEditor::get_selected_color() {
    auto pat = get_active();
    if (pat.first && pat.first->color.has_value()) {
        return _color_picker->get_current_color();
    }
    return std::optional<unsigned int>(); // color not supported
}

Geom::Point PatternEditor::get_selected_offset() {
    return Geom::Point(_offset_x.get_value(), _offset_y.get_value());
}

Geom::Affine PatternEditor::get_selected_transform() {
    Geom::Affine matrix;

    matrix *= Geom::Scale(_scale_x.get_value(), _scale_y.get_value());
    matrix *= Geom::Rotate(_angle_btn.get_value() / 180.0 * M_PI);
    matrix.setTranslation(_current_pattern.offset);
    return matrix;
}

bool PatternEditor::is_selected_scale_uniform() {
    return _scale_linked;
}

Geom::Scale PatternEditor::get_selected_gap() {
    auto vx = _gap_x_slider.get_value();
    auto gap_x = _precise_gap_control ? _gap_x_spin.get_value() : slider_to_gap(vx, _gap_x_slider.get_adjustment()->get_upper());

    auto vy = _gap_y_slider.get_value();
    auto gap_y = _precise_gap_control ? _gap_y_spin.get_value() : slider_to_gap(vy, _gap_y_slider.get_adjustment()->get_upper());

    return Geom::Scale(gap_x, gap_y);
}

Glib::ustring PatternEditor::get_label() {
    return _name_box.get_text();
}

SPPattern* get_pattern(const PatternItem& item, SPDocument* document) {
    auto doc = item.collection ? item.collection : document;
    if (!doc) return nullptr;

    return cast<SPPattern>(doc->getObjectById(item.id));
}

void regenerate_tile_images(PatternManager& manager, PatternStore& pat_store, int tile_size, double device_scale, SPDocument* current) {
    auto& patterns = pat_store.store.get_items();
    for (auto& item : patterns) {
        if (auto pattern = get_pattern(*item.get(), current)) {
            item->pix = manager.get_image(pattern, tile_size, tile_size, device_scale);
        }
    }
    pat_store.store.refresh();
}

void PatternEditor::update_pattern_tiles() {
    const double device_scale = get_scale_factor();
    regenerate_tile_images(_manager, _doc_pattern_store, _tile_size, device_scale, _current_document);
    regenerate_tile_images(_manager, _stock_pattern_store, _tile_size, device_scale, nullptr);
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
