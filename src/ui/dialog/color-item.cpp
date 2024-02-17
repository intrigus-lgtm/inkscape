// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Color item used in palettes and swatches UI.
 */
/* Authors: PBS <pbs3141@gmail.com>
 * Copyright (C) 2022 PBS
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "color-item.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>
#include <cairomm/context.h>
#include <cairomm/pattern.h>
#include <cairomm/surface.h>
#include <glibmm/bytes.h>
#include <glibmm/convert.h>
#include <glibmm/i18n.h>
#include <giomm/menu.h>
#include <giomm/menuitem.h>
#include <giomm/simpleaction.h>
#include <giomm/simpleactiongroup.h>
#include <gdkmm/contentprovider.h>
#include <gdkmm/general.h>
#include <gdkmm/pixbuf.h>
#include <gdkmm/texture.h>
#include <gtkmm/dragsource.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/popover.h>
#include <sigc++/functors/mem_fun.h>

#include "desktop-style.h"
#include "document.h"
#include "document-undo.h"
#include "hsluv.h"
#include "inkscape-preferences.h"
#include "message-context.h"
#include "preferences.h"
#include "selection.h"
#include "actions/actions-tools.h"
#include "display/cairo-utils.h"
#include "helper/sigc-track-obj.h"
#include "io/resource.h"
#include "io/sys.h"
#include "object/sp-gradient.h"
#include "object/tags.h"
#include "svg/svg-color.h"
#include "ui/controller.h"
#include "ui/dialog/dialog-base.h"
#include "ui/dialog/dialog-container.h"
#include "ui/icon-names.h"
#include "ui/menuize.h"
#include "ui/util.h"

[[nodiscard]] static auto const &get_removecolor()
{
    // The "remove-color" image.
    static Glib::RefPtr<Gdk::Pixbuf> pixbuf;
    if (pixbuf) return pixbuf;

    auto path_utf8 = (Glib::ustring)Inkscape::IO::Resource::get_path(Inkscape::IO::Resource::SYSTEM,
        Inkscape::IO::Resource::UIS, "resources", "remove-color.png");
    auto path = Glib::filename_from_utf8(path_utf8);
    pixbuf = Gdk::Pixbuf::create_from_file(path);
    if (!pixbuf) {
        g_warning("Null pixbuf for %p [%s]", path.c_str(), path.c_str());
    }
    return pixbuf;
}

namespace Inkscape::UI::Dialog {

ColorItem::ColorItem(PaintDef const &paintdef, DialogBase *dialog)
    : dialog(dialog)
{
    if (paintdef.get_type() == PaintDef::RGB) {
        pinned_default = false;
        data = RGBData{paintdef.get_rgb()};
    } else {
        pinned_default = true;
        data = PaintNone{};
        add_css_class("paint-none");
    }
    description = paintdef.get_description();
    color_id = paintdef.get_color_id();
    tooltip = paintdef.get_tooltip();

    common_setup();
}

ColorItem::ColorItem(SPGradient *gradient, DialogBase *dialog)
    : dialog(dialog)
{
    data = GradientData{gradient};
    description = gradient->defaultLabel();
    color_id = gradient->getId();

    gradient->connectRelease(SIGC_TRACKING_ADAPTOR([this] (SPObject*) {
        std::get<GradientData>(data).gradient = nullptr;
    }, *this));

    gradient->connectModified(SIGC_TRACKING_ADAPTOR([this] (SPObject *obj, unsigned flags) {
        if (flags & SP_OBJECT_STYLE_MODIFIED_FLAG) {
            cache_dirty = true;
            queue_draw();
        }
        description = obj->defaultLabel();
        _signal_modified.emit();
        if (is_pinned() != was_grad_pinned) {
            was_grad_pinned = is_pinned();
            _signal_pinned.emit();
        }
    }, *this));

    was_grad_pinned = is_pinned();
    common_setup();
}

ColorItem::ColorItem(Glib::ustring name) : description(std::move(name)) {
    bool group = !description.empty();
    set_name("ColorItem");
    set_tooltip_text(description);
    color_id = "-";
    add_css_class(group ? "group" : "filler");
}

ColorItem::~ColorItem() = default;

bool ColorItem::is_group() const {
    return !dialog && color_id == "-" && !description.empty();
}

bool ColorItem::is_filler() const {
    return !dialog && color_id == "-" && description.empty();
}

void ColorItem::common_setup()
{
    set_name("ColorItem");
    set_tooltip_text(description + (tooltip.empty() ? tooltip : "\n" + tooltip));
    set_draw_func(sigc::mem_fun(*this, &ColorItem::draw_func));

    Controller::add_drag_source(*this, {
        .button  = Controller::Button::left,
        .actions = Gdk::DragAction::MOVE | Gdk::DragAction::COPY,
        .prepare = sigc::mem_fun(*this, &ColorItem::on_drag_prepare),
        .begin   = sigc::mem_fun(*this, &ColorItem::on_drag_begin)
    });

    Controller::add_motion<&ColorItem::on_motion_enter,
                           nullptr,
                           &ColorItem::on_motion_leave>
                          (*this, *this, Gtk::PropagationPhase::TARGET);

    Controller::add_click(*this,
                          sigc::mem_fun(*this, &ColorItem::on_click_pressed),
                          sigc::mem_fun(*this, &ColorItem::on_click_released));
}

void ColorItem::set_pinned_pref(const std::string &path)
{
    pinned_pref = path + "/pinned/" + color_id;
}

void ColorItem::draw_color(Cairo::RefPtr<Cairo::Context> const &cr, int w, int h) const
{
    if (std::holds_alternative<Undefined>(data)) {
        // there's no color to paint; indicate clearly that there is nothing to select:
        auto y = h / 2 + 0.5;
        auto width = w / 4;
        auto x = (w - width) / 2 - 0.5;
        cr->move_to(x, y);
        cr->line_to(x + width, y);
        auto const fg = get_color();
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.5);
        cr->set_line_width(1);
        cr->stroke();
    }
    else if (is_paint_none()) {
        if (auto const pixbuf = get_removecolor()) {
            const auto device_scale = get_scale_factor();
            cr->save();
            cr->scale((double)w / pixbuf->get_width() / device_scale, (double)h / pixbuf->get_height() / device_scale);
            Gdk::Cairo::set_source_pixbuf(cr, pixbuf, 0, 0);
            cr->paint();
            cr->restore();
        }
    } else if (auto const rgbdata = std::get_if<RGBData>(&data)) {
        auto [r, g, b] = rgbdata->rgb;
        cr->set_source_rgb(r / 255.0, g / 255.0, b / 255.0);
        cr->paint();
        // there's no way to query background color to check if color item stands out,
        // so we apply faint outline to let users make out color shapes blending with background
        auto const fg = get_color();
        cr->rectangle(0.5, 0.5, w - 1, h - 1);
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.07);
        cr->set_line_width(1);
        cr->stroke();
    } else if (auto const graddata = std::get_if<GradientData>(&data)) {
        // Gradient pointer may be null if the gradient was destroyed.
        auto grad = graddata->gradient;
        if (!grad) return;

        auto pat_checkerboard = Cairo::RefPtr<Cairo::Pattern>(new Cairo::Pattern(ink_cairo_pattern_create_checkerboard(), true));
        auto pat_gradient     = Cairo::RefPtr<Cairo::Pattern>(new Cairo::Pattern(grad->create_preview_pattern(w),         true));

        cr->set_source(pat_checkerboard);
        cr->paint();
        cr->set_source(pat_gradient);
        cr->paint();
    }
}

void ColorItem::draw_func(Cairo::RefPtr<Cairo::Context> const &cr, int const w, int const h)
{
    // Only using caching for none and gradients. None is included because the image is huge.
    bool const use_cache = std::holds_alternative<PaintNone>(data) || std::holds_alternative<GradientData>(data);

    if (use_cache) {
        auto scale = get_scale_factor();
        // Ensure cache exists and has correct size.
        if (!cache || cache->get_width() != w * scale || cache->get_height() != h * scale) {
            cache = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, w * scale, h * scale);
            cairo_surface_set_device_scale(cache->cobj(), scale, scale);
            cache_dirty = true;
        }
        // Ensure cache contents is up-to-date.
        if (cache_dirty) {
            draw_color(Cairo::Context::create(cache), w * scale, h * scale);
            cache_dirty = false;
        }
        // Paint from cache.
        cr->set_source(cache, 0, 0);
        cr->paint();
    } else {
        // Paint directly.
        draw_color(cr, w, h);
    }

    // Draw fill/stroke indicators.
    if (is_fill || is_stroke) {
        double const lightness = Hsluv::rgb_to_perceptual_lightness(average_color());
        auto [gray, alpha] = Hsluv::get_contrasting_color(lightness);
        cr->set_source_rgba(gray, gray, gray, alpha);

        // Scale so that the square -1...1 is the biggest possible square centred in the widget.
        auto minwh = std::min(w, h);
        cr->translate((w - minwh) / 2.0, (h - minwh) / 2.0);
        cr->scale(minwh / 2.0, minwh / 2.0);
        cr->translate(1.0, 1.0);

        if (is_fill) {
            cr->arc(0.0, 0.0, 0.35, 0.0, 2 * M_PI);
            cr->fill();
        }

        if (is_stroke) {
            cr->set_fill_rule(Cairo::Context::FillRule::EVEN_ODD);
            cr->arc(0.0, 0.0, 0.65, 0.0, 2 * M_PI);
            cr->arc(0.0, 0.0, 0.5, 0.0, 2 * M_PI);
            cr->fill();
        }
    }
}

void ColorItem::size_allocate_vfunc(int const width, int const height, int const baseline)
{
    Gtk::DrawingArea::size_allocate_vfunc(width, height, baseline);

    cache_dirty = true;
}

void ColorItem::on_motion_enter(GtkEventControllerMotion const * /*motion*/,
                                double /*x*/, double /*y*/)
{
    assert(dialog);

    mouse_inside = true;
    if (auto desktop = dialog->getDesktop()) {
        auto msg = Glib::ustring::compose(_("Color: <b>%1</b>; <b>Click</b> to set fill, <b>Shift+click</b> to set stroke"), description);
        desktop->tipsMessageContext()->set(Inkscape::INFORMATION_MESSAGE, msg.c_str());
    }
}

void ColorItem::on_motion_leave(GtkEventControllerMotion const * /*motion*/)
{
    assert(dialog);

    mouse_inside = false;
    if (auto desktop = dialog->getDesktop()) {
        desktop->tipsMessageContext()->clear();
    }
}

Gtk::EventSequenceState ColorItem::on_click_pressed(Gtk::GestureClick const &click,
                                                    int /*n_press*/, double /*x*/, double /*y*/)
{
    assert(dialog);

    if (click.get_current_button() == 3) {
        on_rightclick();
        return Gtk::EventSequenceState::CLAIMED;
    }
    // Return true necessary to avoid stealing the canvas focus.
    return Gtk::EventSequenceState::CLAIMED;
}

Gtk::EventSequenceState ColorItem::on_click_released(Gtk::GestureClick const &click,
                                                     int /*n_press*/, double /*x*/, double /*y*/)
{
    assert(dialog);

    auto const button = click.get_current_button();
    if (mouse_inside && (button == 1 || button == 2)) {
        auto const state = click.get_current_event_state();
        auto const stroke = button == 2 || Controller::has_flag(state, Gdk::ModifierType::SHIFT_MASK);
        on_click(stroke);
        return Gtk::EventSequenceState::CLAIMED;
    }
    return Gtk::EventSequenceState::NONE;
}

void ColorItem::on_click(bool stroke)
{
    assert(dialog);

    auto desktop = dialog->getDesktop();
    if (!desktop) return;

    auto attr_name = stroke ? "stroke" : "fill";
    auto css = std::unique_ptr<SPCSSAttr, void(*)(SPCSSAttr*)>(sp_repr_css_attr_new(), [] (auto p) {sp_repr_css_attr_unref(p);});

    Glib::ustring descr;
    if (is_paint_none()) {
        sp_repr_css_set_property(css.get(), attr_name, "none");
        descr = stroke ? _("Set stroke color to none") : _("Set fill color to none");
    } else if (auto const rgbdata = std::get_if<RGBData>(&data)) {
        auto [r, g, b] = rgbdata->rgb;
        std::uint32_t rgba = (r << 24) | (g << 16) | (b << 8) | 0xff;
        char buf[64];
        sp_svg_write_color(buf, sizeof(buf), rgba);
        sp_repr_css_set_property(css.get(), attr_name, buf);
        descr = stroke ? _("Set stroke color from swatch") : _("Set fill color from swatch");
    } else if (auto const graddata = std::get_if<GradientData>(&data)) {
        auto grad = graddata->gradient;
        if (!grad) return;
        auto colorspec = "url(#" + Glib::ustring(grad->getId()) + ")";
        sp_repr_css_set_property(css.get(), attr_name, colorspec.c_str());
        descr = stroke ? _("Set stroke color from swatch") : _("Set fill color from swatch");
    }

    sp_desktop_set_style(desktop, css.get());

    DocumentUndo::done(desktop->getDocument(), descr.c_str(), INKSCAPE_ICON("swatches"));
}

void ColorItem::on_rightclick()
{
    // Only re/insert actions on click, not in ctor, to avoid performance hit on rebuilding palette
    auto const main_actions = Gio::SimpleActionGroup::create();
    main_actions->add_action("set-fill"  , sigc::mem_fun(*this, &ColorItem::action_set_fill  ));
    main_actions->add_action("set-stroke", sigc::mem_fun(*this, &ColorItem::action_set_stroke));
    main_actions->add_action("delete"    , sigc::mem_fun(*this, &ColorItem::action_delete    ));
    main_actions->add_action("edit"      , sigc::mem_fun(*this, &ColorItem::action_edit      ));
    main_actions->add_action("toggle-pin", sigc::mem_fun(*this, &ColorItem::action_toggle_pin));
    insert_action_group("color-item", main_actions);

    auto const menu = Gio::Menu::create();

    // TRANSLATORS: An item in context menu on a colour in the swatches
    menu->append(_("Set Fill"  ), "color-item.set-fill"  );
    menu->append(_("Set Stroke"), "color-item.set-stroke");

    auto section = menu;

    if (auto const graddata = std::get_if<GradientData>(&data)) {
        section = Gio::Menu::create();
        menu->append_section(section);
        section->append(_("Delete" ), "color-item.delete");
        section->append(_("Edit..."), "color-item.edit"  );
        section = Gio::Menu::create();
        menu->append_section(section);
    }

    section->append(is_pinned() ? _("Unpin Color") : _("Pin Color"), "color-item.toggle-pin");

    // If document has gradients, add Convert section w/ actions to convert them to swatches.
    auto grad_names = std::vector<Glib::ustring>{};
    for (auto const obj : dialog->getDesktop()->getDocument()->getResourceList("gradient")) {
        auto const grad = static_cast<SPGradient *>(obj);
        if (grad->hasStops() && !grad->isSwatch()) {
            grad_names.emplace_back(grad->getId());
        }
    }
    if (!grad_names.empty()) {
        auto const convert_actions = Gio::SimpleActionGroup::create();
        auto const convert_submenu = Gio::Menu::create();

        std::sort(grad_names.begin(), grad_names.end());
        for (auto const &name: grad_names) {
            convert_actions->add_action(name, sigc::bind(sigc::mem_fun(*this, &ColorItem::action_convert), name));
            convert_submenu->append(name, "color-item-convert." + name);
        }

        insert_action_group("color-item-convert", convert_actions);

        section = Gio::Menu::create();
        section->append_submenu(_("Convert"), convert_submenu);
        menu->append_section(section);
    }

    // static to only create/show 1 menu over all items & avoid lifetime hassles
    static std::unique_ptr<Gtk::Popover> popover;
    popover = UI::make_menuized_popover(std::move(menu), *this);
    popover->popup();
}

void ColorItem::action_set_fill()
{
    on_click(false);
}

void ColorItem::action_set_stroke()
{
    on_click(true);
}

void ColorItem::action_delete()
{
    auto const grad = std::get<GradientData>(data).gradient;
    if (!grad) return;

    grad->setSwatch(false);
    DocumentUndo::done(grad->document, _("Delete swatch"), INKSCAPE_ICON("color-gradient"));
}

void ColorItem::action_edit()
{
    auto const grad = std::get<GradientData>(data).gradient;
    if (!grad) return;

    auto desktop = dialog->getDesktop();
    auto selection = desktop->getSelection();
    auto items = std::vector<SPItem*>(selection->items().begin(), selection->items().end());

    if (!items.empty()) {
        auto query = SPStyle(desktop->doc());
        int result = objects_query_fillstroke(items, &query, true);
        if (result == QUERY_STYLE_MULTIPLE_SAME || result == QUERY_STYLE_SINGLE) {
            if (query.fill.isPaintserver()) {
                if (cast<SPGradient>(query.getFillPaintServer()) == grad) {
                    desktop->getContainer()->new_dialog("FillStroke");
                    return;
                }
            }
        }
    }

    // Otherwise, invoke the gradient tool.
    set_active_tool(desktop, "Gradient");
}

void ColorItem::action_toggle_pin()
{
    if (auto const graddata = std::get_if<GradientData>(&data)) {
        auto const grad = graddata->gradient;
        if (!grad) return;

        grad->setPinned(!is_pinned());
        DocumentUndo::done(grad->document, is_pinned() ? _("Pin swatch") : _("Unpin swatch"), INKSCAPE_ICON("color-gradient"));
    } else {
        Inkscape::Preferences::get()->setBool(pinned_pref, !is_pinned());
    }
}

void ColorItem::action_convert(Glib::ustring const &name)
{
    // This will not be needed until next menu
    remove_action_group("color-item-convert");

    auto const doc = dialog->getDesktop()->getDocument();
    auto const resources = doc->getResourceList("gradient");
    auto const it = std::find_if(resources.cbegin(), resources.cend(),
                                 [&](auto &_){ return _->getId() == name; });
    if (it == resources.cend()) return;

    auto const grad = static_cast<SPGradient *>(*it);
    grad->setSwatch();
    DocumentUndo::done(doc, _("Add gradient stop"), INKSCAPE_ICON("color-gradient"));
}

PaintDef ColorItem::to_paintdef() const
{
    if (is_paint_none()) {
        return PaintDef();
    } else if (auto const rgbdata = std::get_if<RGBData>(&data)) {
        return PaintDef(rgbdata->rgb, description, "");
    } else if (auto const graddata = std::get_if<GradientData>(&data)) {
        auto const grad = graddata->gradient;
        assert(grad != nullptr);
        return PaintDef({0, 0, 0}, grad->getId(), "");
    }

    // unreachable
    assert(false);
    return {};
}

Glib::RefPtr<Gdk::ContentProvider> ColorItem::on_drag_prepare(Gtk::DragSource const &, double, double)
{
    if (!dialog) return {};

    Glib::Value<PaintDef> value;
    value.init(value.value_type());
    value.set(to_paintdef());
    return Gdk::ContentProvider::create(value);
}

void ColorItem::on_drag_begin(Gtk::DragSource &source, Glib::RefPtr<Gdk::Drag> const &/*drag*/)
{
    constexpr int w = 32;
    constexpr int h = 24;

    auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, w, h);
    draw_color(Cairo::Context::create(surface), w, h);
    auto const pixbuf = Gdk::Pixbuf::create(surface, 0, 0, w, h);
    auto const texture = Gdk::Texture::create_for_pixbuf(pixbuf);
    source.set_icon(texture, 0, 0);
}

void ColorItem::set_fill(bool b)
{
    is_fill = b;
    queue_draw();
}

void ColorItem::set_stroke(bool b)
{
    is_stroke = b;
    queue_draw();
}

bool ColorItem::is_pinned() const
{
    if (auto const graddata = std::get_if<GradientData>(&data)) {
        auto const grad = graddata->gradient;
        return grad && grad->isPinned();
    } else {
        return Inkscape::Preferences::get()->getBool(pinned_pref, pinned_default);
    }
}

std::array<double, 3> ColorItem::average_color() const
{
    if (is_paint_none()) {
        return {1.0, 1.0, 1.0};
    } else if (auto const rgbdata = std::get_if<RGBData>(&data)) {
        auto [r, g, b] = rgbdata->rgb;
        return {r / 255.0, g / 255.0, b / 255.0};
    } else if (auto const graddata = std::get_if<GradientData>(&data)) {
        auto grad = graddata->gradient;
        auto pat = Cairo::RefPtr<Cairo::Pattern>(new Cairo::Pattern(grad->create_preview_pattern(1), true));
        auto img = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, 1, 1);
        auto cr = Cairo::Context::create(img);
        cr->set_source_rgb(196.0 / 255.0, 196.0 / 255.0, 196.0 / 255.0);
        cr->paint();
        cr->set_source(pat);
        cr->paint();
        auto rgb = img->get_data();
        return {rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0};
    }

    // unreachable
    assert(false);
    return {1.0, 1.0, 1.0};
}

bool ColorItem::is_paint_none() const {
    return std::holds_alternative<PaintNone>(data);
}


} // namespace Inkscape::UI::Dialog

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
