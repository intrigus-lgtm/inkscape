// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Color palette widget
 */
/* Authors:
 *   Michael Kowalski
 *
 * Copyright (C) 2021 Michael Kowalski
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <utility>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/ustring.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/builder.h>
#include <gtkmm/button.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/flowboxchild.h>
#include <gtkmm/label.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/popover.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/scale.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/scrollbar.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>
#include <sigc++/functors/mem_fun.h>

#include "color-palette.h"
#include "ui/builder-utils.h"
#include "ui/dialog/color-item.h"
#include "ui/util.h"
#include "ui/widget/color-palette-preview.h"
#include "ui/widget/popover-menu.h"
#include "ui/widget/popover-menu-item.h"

namespace Inkscape::UI::Widget {

[[nodiscard]] static auto make_menu()
{
    auto const separator = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    separator->set_margin_top   (5);
    separator->set_margin_bottom(5);

    auto const config = Gtk::make_managed<PopoverMenuItem>(_("Configure..."), true);

    auto menu = std::make_unique<PopoverMenu>(nullptr, Gtk::PositionType::TOP);
    menu->add_css_class("ColorPalette");
    menu->append(*separator);
    menu->append(*config);

    return std::make_pair(std::move(menu), std::ref(*config));
}

ColorPalette::ColorPalette():
    _builder(create_builder("color-palette.glade")),
    _normal_box(get_widget<Gtk::FlowBox>(_builder, "flow-box")),
    _pinned_box(get_widget<Gtk::FlowBox>(_builder, "pinned")),
    _scroll_btn(get_widget<Gtk::FlowBox>(_builder, "scroll-buttons")),
    _scroll_left(get_widget<Gtk::Button>(_builder, "btn-left")),
    _scroll_right(get_widget<Gtk::Button>(_builder, "btn-right")),
    _scroll_up(get_widget<Gtk::Button>(_builder, "btn-up")),
    _scroll_down(get_widget<Gtk::Button>(_builder, "btn-down")),
    _scroll(get_widget<Gtk::ScrolledWindow>(_builder, "scroll-wnd"))
{
    get_widget<Gtk::CheckButton>(_builder, "show-labels").set_visible(false);
    _normal_box.set_filter_func([](Gtk::FlowBoxChild*){ return true; });

    auto& box = get_widget<Gtk::Box>(_builder, "palette-box");
    set_child(box);

    auto [menu, config] = make_menu();
    _menu = std::move(menu);
    auto& btn_menu = get_widget<Gtk::MenuButton>(_builder, "btn-menu");
    btn_menu.set_popover(*_menu);
    auto& dlg = get_settings_popover();
    config.signal_activate().connect([&, this] {
        dlg.set_parent(btn_menu);
        dlg.popup();
    });

    auto& size = get_widget<Gtk::Scale>(_builder, "size-slider");
    size.signal_change_value().connect([=,&size](Gtk::ScrollType, double val) {
        _set_tile_size(static_cast<int>(size.get_value()));
        _signal_settings_changed.emit();
        return true;
    }, true);

    auto& aspect = get_widget<Gtk::Scale>(_builder, "aspect-slider");
    aspect.signal_change_value().connect([=,&aspect](Gtk::ScrollType, double val) {
        _set_aspect(aspect.get_value());
        _signal_settings_changed.emit();
        return true;
    }, true);

    auto& border = get_widget<Gtk::Scale>(_builder, "border-slider");
    border.signal_change_value().connect([=,&border](Gtk::ScrollType, double val) {
        _set_tile_border(static_cast<int>(border.get_value()));
        _signal_settings_changed.emit();
        return true;
    }, true);

    auto& rows = get_widget<Gtk::Scale>(_builder, "row-slider");
    rows.signal_change_value().connect([=,&rows](Gtk::ScrollType, double val) {
        _set_rows(static_cast<int>(rows.get_value()));
        _signal_settings_changed.emit();
        return true;
    }, true);

    auto& sb = get_widget<Gtk::CheckButton>(_builder, "use-sb");
    sb.set_active(_force_scrollbar);
    sb.signal_toggled().connect([=,&sb](){
        _enable_scrollbar(sb.get_active());
        _signal_settings_changed.emit();
    });

    auto& stretch = get_widget<Gtk::CheckButton>(_builder, "stretch");
    stretch.set_active(_force_scrollbar);
    stretch.signal_toggled().connect([=,&stretch](){
        _enable_stretch(stretch.get_active());
        _signal_settings_changed.emit();
    });
    update_stretch();

    auto& large = get_widget<Gtk::CheckButton>(_builder, "enlarge");
    large.set_active(_large_pinned_panel);
    large.signal_toggled().connect([=,&large](){
        _set_large_pinned_panel(large.get_active());
        _signal_settings_changed.emit();
    });
    update_checkbox();

    auto& sl = get_widget<Gtk::CheckButton>(_builder, "show-labels");
    sl.set_visible(false);
    sl.set_active(_show_labels);
    sl.signal_toggled().connect([=,&sl](){
        _show_labels = sl.get_active();
        _signal_settings_changed.emit();
        rebuild_widgets();
    });

    _scroll.set_min_content_height(1);

    _scroll_down.signal_clicked().connect([=](){ scroll(0, get_palette_height(), get_tile_height() + _border, true); });
    _scroll_up.signal_clicked().connect([=](){ scroll(0, -get_palette_height(), get_tile_height() + _border, true); });
    _scroll_left.signal_clicked().connect([=](){ scroll(-10 * (get_tile_width() + _border), 0, 0.0, false); });
    _scroll_right.signal_clicked().connect([=](){ scroll(10 * (get_tile_width() + _border), 0, 0.0, false); });

    set_vexpand_set(true);
    set_up_scrolling();

    connectBeforeResize([this] (int w, int h, int) {
        auto const a = Geom::IntPoint{w, h};
        if (_allocation == a) return;
        _allocation = a;
        set_up_scrolling();
    });
}

ColorPalette::~ColorPalette() {
    if (_active_timeout) {
        g_source_remove(_active_timeout);
    }
}

Gtk::Popover& ColorPalette::get_settings_popover() {
    return get_widget<Gtk::Popover>(_builder, "config-popup");
}

void ColorPalette::set_settings_visibility(bool show) {
    auto& btn_menu = get_widget<Gtk::MenuButton>(_builder, "btn-menu");
    btn_menu.set_visible(show);
}

void ColorPalette::do_scroll(int dx, int dy) {
    if (auto vert = _scroll.get_vscrollbar()) {
        vert->get_adjustment()->set_value(vert->get_adjustment()->get_value() + dy);
    }
    if (auto horz = _scroll.get_hscrollbar()) {
        horz->get_adjustment()->set_value(horz->get_adjustment()->get_value() + dx);
    }
}

std::pair<double, double> get_range(Gtk::Scrollbar& sb) {
    auto adj = sb.get_adjustment();
    return std::make_pair(adj->get_lower(), adj->get_upper() - adj->get_page_size());
}

gboolean ColorPalette::scroll_cb(gpointer self) {
    auto ptr = static_cast<ColorPalette*>(self);
    bool fire_again = false;

    if (auto vert = ptr->_scroll.get_vscrollbar()) {
        auto value = vert->get_adjustment()->get_value();
        // is this the final adjustment step?
        if (fabs(ptr->_scroll_final - value) < fabs(ptr->_scroll_step)) {
            vert->get_adjustment()->set_value(ptr->_scroll_final);
            fire_again = false; // cancel timer
        }
        else {
            auto pos = value + ptr->_scroll_step;
            vert->get_adjustment()->set_value(pos);
            auto range = get_range(*vert);
            if (pos > range.first && pos < range.second) {
                // not yet done
                fire_again = true; // fire this callback again
            }
        }
    }

    if (!fire_again) {
        ptr->_active_timeout = 0;
    }

    return fire_again;
}

void ColorPalette::scroll(int dx, int dy, double snap, bool smooth) {
    if (auto vert = _scroll.get_vscrollbar()) {
        if (smooth && dy != 0.0) {
            _scroll_final = vert->get_adjustment()->get_value() + dy;
            if (snap > 0) {
                // round it to whole 'dy' increments
                _scroll_final -= fmod(_scroll_final, snap);
            }
            auto range = get_range(*vert);
            if (_scroll_final < range.first) {
                _scroll_final = range.first;
            }
            else if (_scroll_final > range.second) {
                _scroll_final = range.second;
            }
            _scroll_step = dy / 4.0;
            if (!_active_timeout && vert->get_adjustment()->get_value() != _scroll_final) {
                // limit refresh to 60 fps, in practice it will be slower
                _active_timeout = g_timeout_add(1000 / 60, &ColorPalette::scroll_cb, this);
            }
        }
        else {
            vert->get_adjustment()->set_value(vert->get_adjustment()->get_value() + dy);
        }
    }
    if (auto horz = _scroll.get_hscrollbar()) {
        horz->get_adjustment()->set_value(horz->get_adjustment()->get_value() + dx);
    }
}

int ColorPalette::get_tile_size() const {
    return _size;
}

int ColorPalette::get_tile_border() const {
    return _border;
}

int ColorPalette::get_rows() const {
    return _rows;
}

double ColorPalette::get_aspect() const {
    return _aspect;
}

void ColorPalette::set_tile_border(int border) {
    _set_tile_border(border);
    auto& slider = get_widget<Gtk::Scale>(_builder, "border-slider");
    slider.set_value(border);
}

void ColorPalette::_set_tile_border(int border) {
    if (border == _border) return;

    if (border < 0 || border > 100) {
        g_warning("Unexpected tile border size of color palette: %d", border);
        return;
    }

    _border = border;
    refresh();
}

void ColorPalette::set_tile_size(int size) {
    _set_tile_size(size);
    auto& slider = get_widget<Gtk::Scale>(_builder, "size-slider");
    slider.set_value(size);
}

void ColorPalette::_set_tile_size(int size) {
    if (size == _size) return;

    if (size < 1 || size > 1000) {
        g_warning("Unexpected tile size for color palette: %d", size);
        return;
    }

    _size = size;
    refresh();
}

void ColorPalette::set_aspect(double aspect) {
    _set_aspect(aspect);
    auto& slider = get_widget<Gtk::Scale>(_builder, "aspect-slider");
    slider.set_value(aspect);
}

void ColorPalette::_set_aspect(double aspect) {
    if (aspect == _aspect) return;

    if (aspect < -2.0 || aspect > 2.0) {
        g_warning("Unexpected aspect ratio for color palette: %f", aspect);
        return;
    }

    _aspect = aspect;
    refresh();
}

void ColorPalette::refresh() {
    set_up_scrolling();
    queue_resize();
}

void ColorPalette::set_rows(int rows) {
    _set_rows(rows);
    auto& slider = get_widget<Gtk::Scale>(_builder, "row-slider");
    slider.set_value(rows);
}

void ColorPalette::_set_rows(int rows) {
    if (rows == _rows) return;

    if (rows < 1 || rows > 1000) {
        g_warning("Unexpected number of rows for color palette: %d", rows);
        return;
    }
    _rows = rows;
    update_checkbox();
    refresh();
}

void ColorPalette::update_checkbox() {
    auto& sb = get_widget<Gtk::CheckButton>(_builder, "use-sb");
    // scrollbar can only be applied to single-row layouts
    bool sens = _rows == 1;
    if (sb.get_sensitive() != sens) sb.set_sensitive(sens);
}

void ColorPalette::set_compact(bool compact) {
    if (_compact != compact) {
        _compact = compact;
        set_up_scrolling();

        get_widget<Gtk::Scale>(_builder, "row-slider").set_visible(compact);
        get_widget<Gtk::Label>(_builder, "row-label").set_visible(compact);
        get_widget<Gtk::CheckButton>(_builder, "enlarge").set_visible(compact);
        // get_widget<Gtk::CheckButton>(_builder, "show-labels").set_visible(false);
    }
}

bool ColorPalette::is_scrollbar_enabled() const {
    return _force_scrollbar;
}

bool ColorPalette::is_stretch_enabled() const {
    return _stretch_tiles;
}

void ColorPalette::enable_stretch(bool enable) {
    auto& stretch = get_widget<Gtk::CheckButton>(_builder, "stretch");
    stretch.set_active(enable);
    _enable_stretch(enable);
}

void ColorPalette::_enable_stretch(bool enable) {
    if (_stretch_tiles == enable) return;

    _stretch_tiles = enable;
    _normal_box.set_halign(enable ? Gtk::Align::FILL : Gtk::Align::START);
    update_stretch();
    refresh();
}

void ColorPalette::enable_labels(bool labels) {
    auto& sl = get_widget<Gtk::CheckButton>(_builder, "show-labels");
    sl.set_active(labels);
    if (_show_labels != labels) {
        _show_labels = labels;
        rebuild_widgets();
        refresh();
    }
}

void ColorPalette::update_stretch() {
    auto& aspect = get_widget<Gtk::Scale>(_builder, "aspect-slider");
    aspect.set_sensitive(!_stretch_tiles);
    auto& label = get_widget<Gtk::Label>(_builder, "aspect-label");
    label.set_sensitive(!_stretch_tiles);
}

void ColorPalette::enable_scrollbar(bool show) {
    auto& sb = get_widget<Gtk::CheckButton>(_builder, "use-sb");
    sb.set_active(show);
    _enable_scrollbar(show);
}

void ColorPalette::_enable_scrollbar(bool show) {
    if (_force_scrollbar == show) return;

    _force_scrollbar = show;
    set_up_scrolling();
}

void ColorPalette::set_up_scrolling() {
    auto &box = get_widget<Gtk::Box>(_builder, "palette-box");
    auto &btn_menu = get_widget<Gtk::MenuButton>(_builder, "btn-menu");
    auto const colors = UI::get_children(_normal_box);
    auto normal_count = std::max(1, static_cast<int>(colors.size()));
    auto pinned_count = std::max(1, static_cast<int>(UI::get_children(_pinned_box).size()));

    _normal_box.set_max_children_per_line(normal_count);
    _normal_box.set_min_children_per_line(1);
    _pinned_box.set_max_children_per_line(pinned_count);
    _pinned_box.set_min_children_per_line(1);

    auto alloc_width = _normal_box.get_parent()->get_allocated_width();
    // if page-size is defined, align color tiles in columns
    if (_page_size > 1 && alloc_width > 1 && !_show_labels && !colors.empty()) {
        int width = get_tile_width();
        if (width > 1) {
            int cols = alloc_width / (width + _border);
            cols = std::max(cols - cols % _page_size, _page_size);
            if (_normal_box.get_max_children_per_line() != cols) {
                _normal_box.set_max_children_per_line(cols);
            }
        }
    }

    if (_compact) {
        box.set_orientation(Gtk::Orientation::HORIZONTAL);
        btn_menu.set_margin_bottom(0);
        btn_menu.set_margin_end(0);
        // in compact mode scrollbars are hidden; they take up too much space
        set_valign(Gtk::Align::START);
        set_vexpand(false);

        _scroll.set_valign(Gtk::Align::END);
        _normal_box.set_valign(Gtk::Align::END);

        if (_rows == 1 && _force_scrollbar) {
            // horizontal scrolling with single row
            _normal_box.set_min_children_per_line(normal_count);

            _scroll_btn.set_visible(false);

            if (_force_scrollbar) {
                _scroll_left.set_visible(false);
                _scroll_right.set_visible(false);
            }
            else {
                _scroll_left.set_visible(true);
                _scroll_right.set_visible(true);
            }

            // ideally we should be able to use POLICY_AUTOMATIC, but on some themes this leads to a scrollbar
            // that obscures color tiles (it overlaps them); thus resorting to manual scrollbar selection
            _scroll.set_policy(_force_scrollbar ? Gtk::PolicyType::ALWAYS : Gtk::PolicyType::EXTERNAL, Gtk::PolicyType::NEVER);
        }
        else {
            // vertical scrolling with multiple rows
            // 'external' allows scrollbar to shrink vertically
            _scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::EXTERNAL);
            _scroll_left.set_visible(false);
            _scroll_right.set_visible(false);
            _scroll_btn.set_visible(true);
        }

        int div = _large_pinned_panel ? (_rows > 2 ? 2 : 1) : _rows;
        _pinned_box.set_max_children_per_line(std::max((pinned_count + div - 1) / div, 1));
        _pinned_box.set_margin_end(_border);
    }
    else {
        box.set_orientation(Gtk::Orientation::VERTICAL);
        btn_menu.set_margin_bottom(2);
        btn_menu.set_margin_end(2);
        // in normal mode use regular full-size scrollbars
        set_valign(Gtk::Align::FILL);
        set_vexpand(true);

        _scroll_left.set_visible(false);
        _scroll_right.set_visible(false);
        _scroll_btn.set_visible(false);

        _normal_box.set_valign(Gtk::Align::START);
        _scroll.set_valign(Gtk::Align::FILL);
        // 'always' allocates space for scrollbar
        _scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::ALWAYS);
    }

    resize();
}

int ColorPalette::get_tile_size(bool horz) const {
    if (_stretch_tiles) return _size;

    double aspect = horz ? _aspect : -_aspect;
    int scale = _show_labels ? 2.0 : 1.0;
    int size = 0;

    if (aspect > 0) {
        size = static_cast<int>(round((1.0 + aspect) * _size));
    }
    else if (aspect < 0) {
        size = static_cast<int>(round((1.0 / (1.0 - aspect)) * _size));
    }
    else {
        size = _size;
    }
    return size * scale;
}

int ColorPalette::get_tile_width() const {
    return get_tile_size(true);
}

int ColorPalette::get_tile_height() const {
    return get_tile_size(false);
}

int ColorPalette::get_palette_height() const {
    return (get_tile_height() + _border) * _rows;
}

void ColorPalette::set_large_pinned_panel(bool large) {
    auto& checkbox = get_widget<Gtk::CheckButton>(_builder, "enlarge");
    checkbox.set_active(large);
    _set_large_pinned_panel(large);
}

void ColorPalette::_set_large_pinned_panel(bool large) {
    if (_large_pinned_panel == large) return;

    _large_pinned_panel = large;
    refresh();
}

bool ColorPalette::is_pinned_panel_large() const {
    return _large_pinned_panel;
}

bool ColorPalette::are_labels_enabled() const {
    return _show_labels;
}

void ColorPalette::resize() {
    if ((_rows == 1 && _force_scrollbar) || !_compact) {
        // auto size for single row to allocate space for scrollbar
        _scroll.set_size_request(-1, -1);
    }
    else {
        // exact size for multiple rows
        int height = get_palette_height() - _border;
        _scroll.set_size_request(1, height);
    }

    _normal_box.set_column_spacing(_border);
    _normal_box.set_row_spacing(_border);
    _pinned_box.set_column_spacing(_border);
    _pinned_box.set_row_spacing(_border);

    int width = get_tile_width();
    int height = get_tile_height();
    for (auto item : _normal_items) {
        item->set_size_request(width, height);
    }

    int pinned_width = width;
    int pinned_height = height;
    if (_large_pinned_panel) {
        double mult = _rows > 2 ? _rows / 2.0 : 2.0;
        pinned_width = pinned_height = static_cast<int>((height + _border) * mult - _border);
    }
    for (auto item : _pinned_items) {
        item->set_size_request(pinned_width, pinned_height);
    }
}

void ColorPalette::set_colors(std::vector<Dialog::ColorItem*> const &swatches)
{
    _normal_items.clear();
    _pinned_items.clear();
    
    for (auto item : swatches) {
        if (item->is_pinned()) {
            _pinned_items.emplace_back(item);
        } else {
            _normal_items.emplace_back(item);
        }
        item->signal_modified().connect([=] {
            UI::for_each_child(*item->get_parent(), [=](Gtk::Widget& w) {
                if (auto label = dynamic_cast<Gtk::Label *>(&w)) {
                    label->set_text(item->get_description());
                }
                return UI::ForEachResult::_continue;
            });
        });
    }
    rebuild_widgets();
    refresh();
}

Gtk::Widget *ColorPalette::_get_widget(Dialog::ColorItem *item) {
    if (auto parent = item->get_parent()) {
        auto &flowbox = dynamic_cast<Gtk::FlowBox &>(*parent);
        flowbox.remove(*item);
    }
    if (_show_labels) {
        item->set_valign(Gtk::Align::CENTER);
        auto const box = Gtk::make_managed<Gtk::Box>();
        auto const label = Gtk::make_managed<Gtk::Label>(item->get_description());
        box->append(*item);
        box->append(*label);
        return box;
    }
    return Gtk::manage(item);
}

void ColorPalette::rebuild_widgets()
{
    _normal_box.freeze_notify();
    _pinned_box.freeze_notify();

    UI::remove_all_children(_normal_box);
    UI::remove_all_children(_pinned_box);

    for (auto item : _normal_items) {
        // in a tile mode (no labels) group headers are hidden:
        if (!_show_labels && item->is_group()) continue;

        // in a list mode with labels, do not show fillers:
        if (_show_labels && item->is_filler()) continue;

        _normal_box.append(*_get_widget(item));
    }
    for (auto item : _pinned_items) {
        _pinned_box.append(*_get_widget(item));
    }

    set_up_scrolling();

    _normal_box.thaw_notify();
    _pinned_box.thaw_notify();
}

class ColorPaletteMenuItem : public PopoverMenuItem {
public:
    ColorPaletteMenuItem(Gtk::CheckButton *&group,
                         Glib::ustring const &label,
                         Glib::ustring id,
                         std::vector<rgb_t> colors)
        : Glib::ObjectBase{"ColorPaletteMenuItem"}
        , PopoverMenuItem{}
        , _radio_button{Gtk::make_managed<Gtk::CheckButton>(label)}
        , _preview{Gtk::make_managed<ColorPalettePreview>(std::move(colors))}
        , id{std::move(id)}
    {
        if (group) {
            _radio_button->set_group(*group);
        } else {
            group = _radio_button;
        }
        auto const box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 1);
        box->append(*_radio_button);
        box->append(*_preview);
        set_child(*box);
    }

    void set_active(bool const active) { _radio_button->set_active(active); }

    Glib::ustring const id;

private:
    Gtk::CheckButton    *_radio_button = nullptr;
    ColorPalettePreview *_preview      = nullptr;
};

void ColorPalette::set_palettes(std::vector<palette_t> const &palettes)
{
    for (auto const &item: _palette_menu_items) {
        _menu->remove(*item);
    }

    _palette_menu_items.clear();
    _palette_menu_items.reserve(palettes.size());

    Gtk::CheckButton *group = nullptr;
    // We prepend in reverse so we add the palettes above the constant separator & Configure items.
    for (auto it = palettes.crbegin(); it != palettes.crend(); ++it) {
        auto& name = it->name;
        auto& id = it->id;
        auto item = std::make_unique<ColorPaletteMenuItem>(group, name, id, it->colors);
        item->signal_activate().connect([=](){
            if (!_in_update) {
                _in_update = true;
                _signal_palette_selected.emit(id);
                _in_update = false;
            }
        });
        item->set_visible(true);
        _menu->prepend(*item);
        _palette_menu_items.push_back(std::move(item));
    }
}

sigc::signal<void (Glib::ustring)>& ColorPalette::get_palette_selected_signal() {
    return _signal_palette_selected;
}

sigc::signal<void ()>& ColorPalette::get_settings_changed_signal() {
    return _signal_settings_changed;
}

void ColorPalette::set_selected(const Glib::ustring& id) {
    _in_update = true;

    for (auto const &item : _palette_menu_items) {
        item->set_active(item->id == id);
    }

    _in_update = false;
}

void ColorPalette::set_page_size(int page_size) {
    _page_size = page_size;
}

void ColorPalette::set_filter(std::function<bool (const Dialog::ColorItem&)> filter) {
    _normal_box.set_filter_func([=](Gtk::FlowBoxChild* c){
        auto child = c->get_child();
        if (auto box = dynamic_cast<Gtk::Box*>(child)) {
            child = UI::get_children(*box).at(0);
        }
        if (auto color = dynamic_cast<Dialog::ColorItem*>(child)) {
            return filter(*color);
        }
        return true;
    });
}

void ColorPalette::apply_filter() {
    _normal_box.invalidate_filter();
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
