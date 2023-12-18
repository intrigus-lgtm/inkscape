// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Page properties widget
 */
/*
 * Authors:
 *   Mike Kowalski
 *
 * Copyright (C) 2021 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "page-properties.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include <glibmm/i18n.h>
#include <giomm/menu.h>
#include <giomm/simpleaction.h>
#include <giomm/simpleactiongroup.h>
#include <gtkmm/builder.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/expander.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/popovermenu.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/togglebutton.h>

#include "page-size-preview.h"
#include "ui/builder-utils.h"
#include "ui/menuize.h"
#include "ui/operation-blocker.h"
#include "ui/util.h"
#include "ui/widget/color-picker.h"
#include "ui/widget/registry.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/unit-menu.h"
#include "util/paper.h"

using Inkscape::UI::create_builder;
using Inkscape::UI::get_widget;

namespace Inkscape::UI::Widget {

const char* g_linked = "entries-linked-symbolic";
const char* g_unlinked = "entries-unlinked-symbolic";
const char* s_linked = "scale-linked-symbolic";
const char* s_unlinked = "scale-unlinked-symbolic";

static std::tuple<int, Glib::ustring, std::string> get_sorter(PaperSize const &page)
{
    // Millimeters has so many items that, to avoid the Popover going off screen, we split them up.
    // For the rest, a heuristic to sort US sizes first, then ISO A–E, then everything else: Others

    std::string const &abbr = page.unit->abbr, &name = page.name;

    // Translators: "US" is an abbreviation for United States.
    if (static auto const us = _("US"); abbr == "in" && name.find(us) != name.npos) {
        return {0, us, abbr};
    }

    if (abbr == "mm" && name.size() >= 2 && name[0] >= 'A' && name[0] <= 'E' &&
                                            name[1] >= '0' && name[1] <= '9')
    {
        // Translators: %1 is a paper size class, e.g. 'A' or 'B' – as in "A4".
        static auto const format = _("ISO %1");
        return {1, Glib::ustring::compose(format, name[0]), abbr};
    }

    static auto const others = _("Others");
    return {2, others, abbr};
}

class PagePropertiesBox final : public PageProperties {
    void create_template_menu()
    {
        static auto const group_name = "page-properties", action_name = "template";
        static auto const get_detailed_action = [](int const index)
            { return Glib::ustring::compose("%1.%2(%3)", group_name, action_name, index); };

        _page_sizes = PaperSize::getPageSizes();
        std::stable_sort(_page_sizes.begin(), _page_sizes.end(), [](auto const &l, auto const &r)
                         { return get_sorter(l) < get_sorter(r); });

        auto group = Gio::SimpleActionGroup::create();
        _template_action = group->add_action_radio_integer(action_name, 0);
        _template_action->property_state().signal_changed().connect([this] {
            _templates_menu_button.set_active(false);
            int index;
            _template_action->get_state(index);
            set_page_template(index);
        });
        insert_action_group(group_name, std::move(group));

        Glib::RefPtr<Gio::Menu> menu = Gio::Menu::create(), submenu = {};
        Glib::ustring prev_submenu_label;
        for (std::size_t i = 0; i < _page_sizes.size(); ++i) {
            auto const &page = _page_sizes[i];
            if (auto const [order, label, abbr] = get_sorter(page);
                prev_submenu_label != label)
            {
                submenu = Gio::Menu::create();
                menu->append_submenu(label, submenu);
                prev_submenu_label = label;
            }
            submenu->append(page.getDescription(false), get_detailed_action(i));
        }
        menu->append(_("Custom"), get_detailed_action(_page_sizes.size())); // sentinel
        _templates_popover.set_menu_model(std::move(menu));
        UI::menuize_popover(_templates_popover);
    }

public:
  PagePropertiesBox()
      // clang-format-off
      : _builder(create_builder("page-properties.glade"))
      , _main_grid            (get_widget<Gtk::Grid>             (_builder, "main-grid"))
      , _left_grid            (get_widget<Gtk::Grid>             (_builder, "left-grid"))
      , _page_width           (get_derived_widget<MathSpinButton>(_builder, "page-width"))
      , _page_height          (get_derived_widget<MathSpinButton>(_builder, "page-height"))
      , _portrait             (get_widget<Gtk::CheckButton>      (_builder, "page-portrait"))
      , _landscape            (get_widget<Gtk::CheckButton>      (_builder, "page-landscape"))
      , _scale_x              (get_derived_widget<MathSpinButton>(_builder, "scale-x"))
      , _link_scale_content   (get_widget<Gtk::Button>           (_builder, "link-scale-content"))
      , _unsupported_size     (get_widget<Gtk::Label>            (_builder, "unsupported"))
      , _nonuniform_scale     (get_widget<Gtk::Label>            (_builder, "nonuniform-scale"))
      , _doc_units            (get_widget<Gtk::Label>            (_builder, "user-units"))
      , _viewbox_x            (get_derived_widget<MathSpinButton>(_builder, "viewbox-x"))
      , _viewbox_y            (get_derived_widget<MathSpinButton>(_builder, "viewbox-y"))
      , _viewbox_width        (get_derived_widget<MathSpinButton>(_builder, "viewbox-width"))
      , _viewbox_height       (get_derived_widget<MathSpinButton>(_builder, "viewbox-height"))
      , _templates_menu_button(get_widget<Gtk::MenuButton>       (_builder, "page-menu-btn"))
      , _templates_popover    (get_widget<Gtk::PopoverMenu>      (_builder, "templates-popover"))
      , _template_name        (get_widget<Gtk::Label>            (_builder, "page-template-name"))
      , _preview_box          (get_widget<Gtk::Box>              (_builder, "preview-box"))
      , _checkerboard         (get_widget<Gtk::CheckButton>      (_builder, "checkerboard"))
      , _antialias            (get_widget<Gtk::CheckButton>      (_builder, "use-antialias"))
      , _clip_to_page         (get_widget<Gtk::CheckButton>      (_builder, "clip-to-page"))
      , _page_label_style     (get_widget<Gtk::CheckButton>      (_builder, "page-label-style"))
      , _border               (get_widget<Gtk::CheckButton>      (_builder, "border"))
      , _border_on_top        (get_widget<Gtk::CheckButton>      (_builder, "border-top"))
      , _shadow               (get_widget<Gtk::CheckButton>      (_builder, "shadow"))
      , _link_width_height    (get_widget<Gtk::Button>           (_builder, "link-width-height"))
      , _viewbox_expander     (get_widget<Gtk::Expander>         (_builder, "viewbox-expander"))
      , _linked_viewbox_scale (get_widget<Gtk::Image>            (_builder, "linked-scale-img"))
      , _display_units        (get_derived_widget<UnitMenu>      (_builder, "display-units"))
      , _page_units           (get_derived_widget<UnitMenu>      (_builder, "page-units"))
      // clang-format-on
    {
        _backgnd_color_picker = std::make_unique<ColorPicker>(
            _("Background color"), "", 0xffffff00, true,
            &get_widget<Gtk::Button>(_builder, "background-color"));
        _backgnd_color_picker->use_transparency(false);

        _border_color_picker = std::make_unique<ColorPicker>(
            _("Border and shadow color"), "", 0x0000001f, true,
            &get_widget<Gtk::Button>(_builder, "border-color"));

        _desk_color_picker = std::make_unique<ColorPicker>(
            _("Desk color"), "", 0xd0d0d0ff, true,
            &get_widget<Gtk::Button>(_builder, "desk-color"));
        _desk_color_picker->use_transparency(false);

        for (auto element : {Color::Background, Color::Border, Color::Desk}) {
            get_color_picker(element).connectChanged([=](unsigned const rgba) {
                update_preview_color(element, rgba);
                if (_update.pending()) return;
                _signal_color_changed.emit(rgba, element);
            });
        }

        _display_units.setUnitType(UNIT_TYPE_LINEAR);
        _display_units.signal_changed().connect([=](){ set_display_unit(); });

        _page_units.setUnitType(UNIT_TYPE_LINEAR);
        _current_page_unit = _page_units.getUnit();
        _page_units.signal_changed().connect([=](){ set_page_unit(); });

        create_template_menu();

        _preview->set_expand(true);
        _preview_box.append(*_preview);

        for (auto check : {Check::Border, Check::Shadow, Check::Checkerboard, Check::BorderOnTop, Check::AntiAlias, Check::ClipToPage, Check::PageLabelStyle}) {
            auto checkbutton = &get_checkbutton(check);
            checkbutton->signal_toggled().connect([=](){ fire_checkbox_toggled(*checkbutton, check); });
        }
        _border.signal_toggled().connect([=](){
            _preview->draw_border(_border.get_active());
        });
        _shadow.signal_toggled().connect([=](){
            _preview->enable_drop_shadow(_shadow.get_active());
        });
        _checkerboard.signal_toggled().connect([=](){
            _preview->enable_checkerboard(_checkerboard.get_active());
        });

        _viewbox_expander.property_expanded().signal_changed().connect([=](){
            // hide/show viewbox controls
            show_viewbox(_viewbox_expander.get_expanded());
        });
        show_viewbox(_viewbox_expander.get_expanded());

        _link_width_height.signal_clicked().connect([=](){
            // toggle size link
            _locked_size_ratio = !_locked_size_ratio;
            // set image
            _link_width_height.set_image_from_icon_name(_locked_size_ratio && _size_ratio > 0 ? g_linked : g_unlinked, Gtk::IconSize::LARGE);
        });
        _link_width_height.set_image_from_icon_name(g_unlinked, Gtk::IconSize::LARGE);

        _link_scale_content.signal_clicked().connect([=](){
            _locked_content_scale = !_locked_content_scale;
            _link_scale_content.set_image_from_icon_name(_locked_content_scale ? s_linked : s_unlinked, Gtk::IconSize::LARGE);
        });
        _link_scale_content.set_image_from_icon_name(s_unlinked, Gtk::IconSize::LARGE);

        // set image for linked scale
        _linked_viewbox_scale.set_from_icon_name(s_linked);

        // report page size changes
        _page_width .signal_value_changed().connect([=](){ set_page_size_linked(true); });
        _page_height.signal_value_changed().connect([=](){ set_page_size_linked(false); });
        // enforce uniform scale thru viewbox
        _viewbox_width. signal_value_changed().connect([=](){ set_viewbox_size_linked(true); });
        _viewbox_height.signal_value_changed().connect([=](){ set_viewbox_size_linked(false); });

        _landscape.signal_toggled().connect([=](){ if (_landscape.get_active()) swap_width_height(); });
        _portrait .signal_toggled().connect([=](){ if (_portrait .get_active()) swap_width_height(); });

        for (auto dim : {Dimension::Scale, Dimension::ViewboxPosition}) {
            auto pair = get_dimension(dim);
            auto b1 = &pair.first;
            auto b2 = &pair.second;
            if (dim == Dimension::Scale) {
                // uniform scale: report the same x and y
                b1->signal_value_changed().connect([=](){
                    // Report the dimention differently if locked
                    fire_value_changed(*b1, *b1, nullptr, _locked_content_scale ? Dimension::ScaleContent : Dimension::Scale);
                });
            }
            else {
                b1->signal_value_changed().connect([=](){ fire_value_changed(*b1, *b2, nullptr, dim); });
                b2->signal_value_changed().connect([=](){ fire_value_changed(*b1, *b2, nullptr, dim); });
            }
        }

        auto& page_resize = get_widget<Gtk::Button>(_builder, "page-resize");
        page_resize.signal_clicked().connect([=](){ _signal_resize_to_fit.emit(); });

        append(_main_grid);
        set_visible(true);
    }

private:
    void show_viewbox(bool show_widgets) {
        auto const show = [=](Gtk::Widget * const w){ w->set_visible(show_widgets); };

        for (auto const widget : UI::get_children(_left_grid)) {
            if (widget->has_css_class("viewbox")) {
                show(widget);
            }
        }
    }

    void update_preview_color(Color const element, unsigned const rgba) {
        switch (element) {
            case Color::Desk: _preview->set_desk_color(rgba); break;
            case Color::Border: _preview->set_border_color(rgba); break;
            case Color::Background: _preview->set_page_color(rgba); break;
        }
    }

    void set_page_template(std::int32_t const index) {
        if (_update.pending()) return;

        g_assert(index >= 0 && index <= _page_sizes.size());

        if (index != _page_sizes.size()) { // sentinel for Custom
            auto scoped(_update.block());
            auto const &page = _page_sizes.at(index);
            auto width = page.width;
            auto height = page.height;
            if (_landscape.get_active() != (width > height)) {
                std::swap(width, height);
            }
            _page_width.set_value(width);
            _page_height.set_value(height);
            _page_units.setUnit(page.unit->abbr);
            _doc_units.set_text(page.unit->abbr);
            _current_page_unit = _page_units.getUnit();
            if (width > 0 && height > 0) {
                _size_ratio = width / height;
            }
        }

        set_page_size(true);
    }

    void changed_linked_value(bool width_changing, Gtk::SpinButton& wedit, Gtk::SpinButton& hedit) {
        if (_size_ratio > 0) {
            auto scoped(_update.block());
            if (width_changing) {
                auto width = wedit.get_value();
                hedit.set_value(width / _size_ratio);
            }
            else {
                auto height = hedit.get_value();
                wedit.set_value(height * _size_ratio);
            }
        }
    }

    void set_viewbox_size_linked(bool width_changing) {
        if (_update.pending()) return;

        if (_scale_is_uniform) {
            // viewbox size - width and height always linked to make scaling uniform
            changed_linked_value(width_changing, _viewbox_width, _viewbox_height);
        }

        auto width  = _viewbox_width.get_value();
        auto height = _viewbox_height.get_value();
        _signal_dimension_changed.emit(width, height, nullptr, Dimension::ViewboxSize);
    }

    void set_page_size_linked(bool width_changing) {
        if (_update.pending()) return;

        // if size ratio is locked change the other dimension too
        if (_locked_size_ratio) {
            changed_linked_value(width_changing, _page_width, _page_height);
        }
        set_page_size();
    }

    void set_page_size(bool template_selected = false) {
        auto pending = _update.pending();

        auto scoped(_update.block());

        auto unit = _page_units.getUnit();
        auto width = _page_width.get_value();
        auto height = _page_height.get_value();
        _preview->set_page_size(width, height);
        if (width != height) {
            (width > height ? _landscape : _portrait).set_active();
            _portrait.set_sensitive();
            _landscape.set_sensitive();
        } else {
            _portrait.set_sensitive(false);
            _landscape.set_sensitive(false);
        }
        if (width > 0 && height > 0) {
            _size_ratio = width / height;
        }

        auto templ = find_page_template(width, height, *unit);
        auto const index = std::distance(_page_sizes.cbegin(), templ);
        _template_action->set_state(Glib::Variant<std::int32_t>::create(index));

        Glib::ustring const label = templ != _page_sizes.cend() && !templ->name.empty()
                                    ? _(templ->name.c_str()) : _("Custom");
        _template_name.set_label(label);
        _templates_menu_button.set_tooltip_text(label);

        if (!pending) {
            _signal_dimension_changed.emit(width, height, unit,
                template_selected ? Dimension::PageTemplate : Dimension::PageSize);
        }
    }

    void swap_width_height() {
        if (_update.pending()) return;

        {
            auto scoped(_update.block());
            auto width = _page_width.get_value();
            auto height = _page_height.get_value();
            _page_width.set_value(height);
            _page_height.set_value(width);
        }
        set_page_size();
    };

    void set_display_unit() {
        if (_update.pending()) return;

        const auto unit = _display_units.getUnit();
        _signal_unit_changed.emit(unit, Units::Display);
    }

    void set_page_unit() {
        if (_update.pending()) return;

        const auto old_unit = _current_page_unit;
        _current_page_unit = _page_units.getUnit();
        const auto new_unit = _current_page_unit;

        {
            auto width = _page_width.get_value();
            auto height = _page_height.get_value();
            Quantity w(width, old_unit->abbr);
            Quantity h(height, old_unit->abbr);
            auto scoped(_update.block());
            _page_width.set_value(w.value(new_unit));
            _page_height.set_value(h.value(new_unit));
        }
        _doc_units.set_text(new_unit->abbr);
        set_page_size();
        _signal_unit_changed.emit(new_unit, Units::Document);
    }

    void set_color(Color element, unsigned int color) override {
        auto scoped(_update.block());

        get_color_picker(element).setRgba32(color);
        update_preview_color(element, color);
    }

    void set_check(Check element, bool checked) override {
        auto scoped(_update.block());

        if (element == Check::NonuniformScale) {
            _nonuniform_scale.set_visible(checked);
            _scale_is_uniform = !checked;
            _scale_x.set_sensitive(_scale_is_uniform);
            _linked_viewbox_scale.set_from_icon_name(_scale_is_uniform ? g_linked : g_unlinked);
        }
        else if (element == Check::DisabledScale) {
            _scale_x.set_sensitive(!checked);
        }
        else if (element == Check::UnsupportedSize) {
            _unsupported_size.set_visible(checked);
        }
        else {
            get_checkbutton(element).set_active(checked);

            // special cases
            if (element == Check::Checkerboard) _preview->enable_checkerboard(checked);
            if (element == Check::Shadow) _preview->enable_drop_shadow(checked);
            if (element == Check::Border) _preview->draw_border(checked);
        }
    }

    void set_dimension(Dimension dimension, double x, double y) override {
        auto scoped(_update.block());

        auto dim = get_dimension(dimension);
        dim.first.set_value(x);
        dim.second.set_value(y);

        set_page_size();
    }

    void set_unit(Units unit, const Glib::ustring& abbr) override {
        auto scoped(_update.block());

        if (unit == Units::Display) {
            _display_units.setUnit(abbr);
        }
        else if (unit == Units::Document) {
            _doc_units.set_text(abbr);
            _page_units.setUnit(abbr);
            _current_page_unit = _page_units.getUnit();
            set_page_size();
        }
    }

    ColorPicker& get_color_picker(Color element) {
        switch (element) {
            case Color::Background: return *_backgnd_color_picker;
            case Color::Desk: return *_desk_color_picker;
            case Color::Border: return *_border_color_picker;

            default:
                throw std::runtime_error("missing case in get_color_picker");
        }
    }

    void fire_value_changed(Gtk::SpinButton& b1, Gtk::SpinButton& b2, const Util::Unit* unit, Dimension dim) {
        if (!_update.pending()) {
            _signal_dimension_changed.emit(b1.get_value(), b2.get_value(), unit, dim);
        }
    }

    void fire_checkbox_toggled(Gtk::CheckButton& checkbox, Check check) {
        if (!_update.pending()) {
            _signal_check_toggled.emit(checkbox.get_active(), check);
        }
    }

    std::vector<PaperSize>::const_iterator
    find_page_template(double const width, double const height, Unit const &unit)
    {
        Quantity w(std::min(width, height), &unit);
        Quantity h(std::max(width, height), &unit);

        static constexpr double eps = 1e-6;
        return std::find_if(_page_sizes.cbegin(), _page_sizes.cend(), [&](auto const &page)
        {
            Quantity pw(std::min(page.width, page.height), page.unit);
            Quantity ph(std::max(page.width, page.height), page.unit);
            if (are_near(w, pw, eps) && are_near(h, ph, eps)) {
                return true;
            }
            return false;
        });
    }

    Gtk::CheckButton& get_checkbutton(Check check) {
        switch (check) {
            case Check::AntiAlias: return _antialias;
            case Check::Border: return _border;
            case Check::Shadow: return _shadow;
            case Check::BorderOnTop: return _border_on_top;
            case Check::Checkerboard: return _checkerboard;
            case Check::ClipToPage: return _clip_to_page;
            case Check::PageLabelStyle: return _page_label_style;

            default:
                throw std::runtime_error("missing case in get_checkbutton");
        }
    }

    typedef std::pair<Gtk::SpinButton&, Gtk::SpinButton&> spin_pair;
    spin_pair get_dimension(Dimension dimension) {
        switch (dimension) {
            case Dimension::PageSize: return spin_pair(_page_width, _page_height);
            case Dimension::PageTemplate: return spin_pair(_page_width, _page_height);
            case Dimension::Scale:
            case Dimension::ScaleContent: return spin_pair(_scale_x, _scale_x);
            case Dimension::ViewboxPosition: return spin_pair(_viewbox_x, _viewbox_y);
            case Dimension::ViewboxSize: return spin_pair(_viewbox_width, _viewbox_height);

            default:
                throw std::runtime_error("missing case in get_dimension");
        }
    }

    Glib::RefPtr<Gtk::Builder> _builder;
    Gtk::Grid &_main_grid;
    Gtk::Grid &_left_grid;
    MathSpinButton &_page_width;
    MathSpinButton &_page_height;
    Gtk::CheckButton &_portrait;
    Gtk::CheckButton &_landscape;
    MathSpinButton &_scale_x;
    Gtk::Button &_link_scale_content;
    Gtk::Label &_unsupported_size;
    Gtk::Label &_nonuniform_scale;
    Gtk::Label &_doc_units;
    MathSpinButton &_viewbox_x;
    MathSpinButton &_viewbox_y;
    MathSpinButton &_viewbox_width;
    MathSpinButton &_viewbox_height;
    std::unique_ptr<ColorPicker> _backgnd_color_picker;
    std::unique_ptr<ColorPicker> _border_color_picker;
    std::unique_ptr<ColorPicker> _desk_color_picker;
    std::vector<PaperSize> _page_sizes;
    Glib::RefPtr<Gio::SimpleAction> _template_action;
    Gtk::MenuButton &_templates_menu_button;
    Gtk::PopoverMenu &_templates_popover;
    Gtk::Label &_template_name;
    Gtk::Box &_preview_box;
    std::unique_ptr<PageSizePreview> _preview = std::make_unique<PageSizePreview>();
    Gtk::CheckButton &_border;
    Gtk::CheckButton &_border_on_top;
    Gtk::CheckButton &_shadow;
    Gtk::CheckButton &_checkerboard;
    Gtk::CheckButton &_antialias;
    Gtk::CheckButton &_clip_to_page;
    Gtk::CheckButton &_page_label_style;
    Gtk::Button &_link_width_height;
    Gtk::Expander &_viewbox_expander;
    Gtk::Image &_linked_viewbox_scale;

    UnitMenu &_display_units;
    UnitMenu &_page_units;
    const Unit *_current_page_unit = nullptr;
    OperationBlocker _update;
    double _size_ratio = 1; // width to height ratio
    bool _locked_size_ratio = false;
    bool _scale_is_uniform = true;
    bool _locked_content_scale = false;
};

PageProperties* PageProperties::create() {
    return new PagePropertiesBox();
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8 :
