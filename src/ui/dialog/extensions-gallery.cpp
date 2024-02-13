// SPDX-License-Identifier: GPL-2.0-or-later

#include "extensions-gallery.h"

#include <algorithm>
#include <cmath>
#include <gdkmm/paintable.h>
#include <gdkmm/texture.h>
#include <giomm/liststore.h>
#include <glibmm/objectbase.h>
#include <gtkmm/boolfilter.h>
#include <gtkmm/centerbox.h>
#include <gtkmm/filter.h>
#include <gtkmm/filterlistmodel.h>
#include <gtkmm/fixed.h>
#include <gtkmm/grid.h>
#include <gtkmm/gridview.h>
#include <gtkmm/listitemfactory.h>
#include <gtkmm/object.h>
#include <gtkmm/overlay.h>
#include <gtkmm/picture.h>
#include <gtkmm/scrollinfo.h>
#include <gtkmm/signallistitemfactory.h>
#include <gtkmm/singleselection.h>
#include <gtkmm/tooltip.h>
#include <gtkmm/widgetpaintable.h>
#include <initializer_list>
#include <iterator>
#include <list>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <2geom/point.h>
#include <2geom/rect.h>
#include <cairo.h>
#include <glibmm/markup.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/enums.h>
#include <gtkmm/iconview.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/paned.h>
#include <gtkmm/scale.h>
#include <gtkmm/searchentry2.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/treemodelfilter.h>
#include <gtkmm/treemodelsort.h>
#include <gtkmm/treeview.h>
#include <libintl.h>

#include "document.h"
#include "preferences.h"

#include "display/cairo-utils.h"
#include "extension/db.h"
#include "extension/effect.h"
#include "io/file.h"
#include "io/resource.h"
#include "io/sys.h"
#include "object/sp-item.h"
#include "ui/builder-utils.h"
#include "ui/dialog/dialog-base.h"
#include "ui/svg-renderer.h"
#include "ui/util.h"

namespace Inkscape::UI::Dialog {

struct EffectItem : public Glib::Object {
    std::string id;     // extension ID
    Glib::ustring name; // effect's name (translated)
    Glib::ustring tooltip;     // menu tip if present, access path otherwise (translated)
    Glib::ustring description; // short description (filters have one; translated)
    Glib::ustring access;   // menu access path (translated)
    Glib::ustring order;    // string to sort items (translated)
    Glib::ustring category; // category (from menu item; translated)
    Inkscape::Extension::Effect* effect;
    std::string icon; // path to effect's SVG icon file

    static Glib::RefPtr<EffectItem> create(
        std::string id,
        Glib::ustring name,
        Glib::ustring tooltip,
        Glib::ustring description,
        Glib::ustring access,
        Glib::ustring order,
        Glib::ustring category,
        Inkscape::Extension::Effect* effect,
        std::string icon
    ) {
        auto item = Glib::make_refptr_for_instance<EffectItem>(new EffectItem());
        item->id = id;
        item->name = name;
        item->tooltip = tooltip;
        item->description = description;
        item->access = access;
        item->order = order;
        item->category = category;
        item->effect = effect;
        item->icon = icon;
        return item;
    }
};

struct CategoriesColumns : public Gtk::TreeModel::ColumnRecord {
    Gtk::TreeModelColumn<Glib::ustring> id;
    Gtk::TreeModelColumn<Glib::ustring> name;

    CategoriesColumns() {
        add(id);
        add(name);
    }
} g_categories_columns;


Cairo::RefPtr<Cairo::Surface> add_shadow(Geom::Point image_size, Cairo::RefPtr<Cairo::Surface> image, int device_scale) {
    if (!image) return {};

    auto w = image_size.x();
    auto h = image_size.y();
    auto margin = 6;
    auto width =  w + 2 * margin;
    auto height = h + 2 * margin;
    auto rect = Geom::Rect::from_xywh(margin, margin, w, h);

    auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, width * device_scale, height * device_scale);
    cairo_surface_set_device_scale(surface->cobj(), device_scale, device_scale);
    auto ctx = Cairo::Context::create(surface);

    // transparent background
    ctx->rectangle(0, 0, width, height);
    ctx->set_source_rgba(1, 1, 1, 0);
    ctx->fill();

    // white image background
    ctx->rectangle(margin, margin, w, h);
    ctx->set_source_rgba(1, 1, 1, 1);
    ctx->fill();

    // image (centered)
    auto imgw = cairo_image_surface_get_width(image->cobj()) / device_scale;
    auto imgh = cairo_image_surface_get_height(image->cobj()) / device_scale;
    auto cx = floor(margin + (w - imgw) / 2.0);
    auto cy = floor(margin + (h - imgh) / 2.0);
    ctx->set_source(image, cx, cy);
    ctx->paint();

    // drop shadow
    auto black = 0x000000;
    ink_cairo_draw_drop_shadow(ctx, rect, margin, black, 0.30);

    return surface;
}

const std::vector<Inkscape::Extension::Effect*> prepare_effects(const std::vector<Inkscape::Extension::Effect*>& effects, bool get_effects) {
    std::vector<Inkscape::Extension::Effect*> out;

    std::copy_if(effects.begin(), effects.end(), std::back_inserter(out), [=](auto effect) {
        if (effect->hidden_from_menu()) return false;

        return effect->is_filter_effect() != get_effects;
    });

    return out;
}

Glib::ustring get_category(const std::list<Glib::ustring>& menu) {
    if (menu.empty()) return {};

    // effect's category; for filters it is always right, but effect extensions may be nested, so this is just a first level group
    return menu.front();
}

Cairo::RefPtr<Cairo::Surface> render_icon(Extension::Effect* effect, std::string icon, Geom::Point icon_size, int device_scale) {
    Cairo::RefPtr<Cairo::Surface> image;

    if (icon.empty() || !IO::file_test(icon.c_str(), G_FILE_TEST_EXISTS)) {
        // placeholder
        image = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, icon_size.x(), icon_size.y());
        cairo_surface_set_device_scale(image->cobj(), device_scale, device_scale);
    }
    else {
        // render icon
        try {
            auto file = Gio::File::create_for_path(icon);
            auto doc = std::shared_ptr<SPDocument>(ink_file_open(file, nullptr));
            if (!doc) return image;

            if (auto item = cast<SPItem>(doc->getObjectById("test-object"))) {
                effect->apply_filter(item);
            }
            svg_renderer r(doc);
            auto w = r.get_width_px();
            auto h = r.get_height_px();
            if (w > 0 && h > 0) {
                auto scale = std::max(w / icon_size.x(), h / icon_size.y());
                r.set_scale(1 / scale);
            }
            image = r.render_surface(device_scale);
        }
        catch (...) {
            g_warning("Cannot render icon for effect %s", effect->get_id());
        }
    }

    image = add_shadow(icon_size, image, device_scale);

    return image;
}

void add_effects(Gio::ListStore<EffectItem>& item_store, const std::vector<Inkscape::Extension::Effect*>& effects, bool root) {
    for (auto& effect : effects) {
        const auto id = effect->get_sanitized_id();

        std::string name = effect->get_name();
        // remove ellipsis and mnemonics
        auto pos = name.find("...", 0);
        if (pos != std::string::npos) {
            name.erase(pos, 3);
        }
        pos = name.find("…", 0);
        if (pos != std::string::npos) {
            name.erase(pos, 1);
        }
        pos = name.find("_", 0);
        if (pos != std::string::npos) {
            name.erase(pos, 1);
        }

        std::ostringstream order;
        std::ostringstream access;
        auto menu = effect->get_menu_list();
        for (auto& part : menu) {
            order << part.raw() << '\n'; // effect sorting order
            access << part.raw() << " \u25b8 "; // right pointing triangle; what about translations and RTL languages?
        }
        access << name;
        order << name;
        auto translated = [](const char* text) { return *text ? gettext(text) : ""; };
        auto description = effect->get_menu_tip();

        std::string dir(IO::Resource::get_path(IO::Resource::SYSTEM, IO::Resource::EXTENSIONS));
        auto icon = effect->find_icon_file(dir);

        if (icon.empty()) {
            // fallback image
            icon = Inkscape::IO::Resource::get_path_string(IO::Resource::SYSTEM, IO::Resource::UIS, "resources", root ? "missing-icon.svg" : "filter-test.svg");
        }

        item_store.append(EffectItem::create(
            id,
            name,
            description.empty() ? access.str().c_str() : translated(description.c_str()),
            translated(description.c_str()),
            access.str(),
            order.str(),
            get_category(menu),
            effect,
            icon
        ));
    }

    item_store.sort([](auto& a, auto& b) { return a->order.compare(b->order); });
}

std::set<std::string> add_categories(Glib::RefPtr<Gtk::ListStore>& store, const std::vector<Inkscape::Extension::Effect*>& effects) {
    std::set<std::string> categories;

    // collect categories
    for (auto& effect : effects) {
        auto menu = effect->get_menu_list();
        auto category = get_category(menu);
        if (!category.empty()) {
            categories.insert(category);
        }
    }

    auto row = *store->append();
    row[g_categories_columns.id] = "all";
    row[g_categories_columns.name] = _("All Effects");

    row = *store->append();
    row[g_categories_columns.id] = "-";

    for (auto cat : categories) {
        auto row = *store->append();
        row[g_categories_columns.id] = cat;
        row[g_categories_columns.name] = cat;
    }

    return categories;
}

ExtensionsGallery::ExtensionsGallery(ExtensionsGallery::Type type) :
    DialogBase(type == Effects ? "/dialogs/extensions-gallery/effects" : "/dialogs/extensions-gallery/filters",
        type == Effects ? "ExtensionsGallery" : "FilterGallery"),
    _builder(create_builder("dialog-extensions.glade")),
    _gridview(get_widget<Gtk::GridView>(_builder, "grid")),
    _search(get_widget<Gtk::SearchEntry2>(_builder, "search")),
    _run(get_widget<Gtk::Button>(_builder, "run")),
    _run_btn_label(get_widget<Gtk::Label>(_builder, "run-label")),
    _selector(get_widget<Gtk::TreeView>(_builder, "selector")),
    _image_cache(1000), // arbitrary limit for how many rendered thumbnails to keep around
    _type(type)
{
    _run_label = _type == Effects ? _run_btn_label.get_label() : _("_Apply");
    if (_type == Filters) {
        get_widget<Gtk::Label>(_builder, "header").set_label(_("Select filter to apply:"));
    }

    auto prefs = Preferences::get();
    // last selected effect
    auto selected = prefs->getString(_prefs_path + "/selected");
    // selected category
    _current_category = prefs->getString(_prefs_path + "/category", "all");
    auto show_list = prefs->getBool(_prefs_path + "/show-list", true);
    auto position = prefs->getIntLimited(_prefs_path + "/position", 120, 10, 1000);

    auto paned = &get_widget<Gtk::Paned>(_builder, "paned");
    auto show_categories_list = [=](bool show){
        paned->get_start_child()->set_visible(show);
    };
    paned->set_position(position);
    paned->property_position().signal_changed().connect([=](){
        if (auto const w = paned->get_start_child()) {
            if (w->is_visible()) prefs->setInt(_prefs_path + "/position", paned->get_position());
        }
    });

    // show/hide categories
    auto toggle = &get_widget<Gtk::ToggleButton>(_builder, "toggle");
    toggle->set_active(show_list);
    toggle->signal_toggled().connect([=](){
        auto visible = toggle->get_active();
        show_categories_list(visible);
        if (!visible) show_category("all"); // don't leave hidden category selection filter active
    });
    show_categories_list(show_list);

    _categories = get_object<Gtk::ListStore>(_builder, "categories-store");
    _selector.set_row_separator_func([=](auto&, auto& it) {
        Glib::ustring id;
        it->get_value(g_categories_columns.id.index(), id);
        return id == "-";
    });

    auto store = Gio::ListStore<EffectItem>::create();
    _filter = Gtk::BoolFilter::create({});
    _filtered_model = Gtk::FilterListModel::create(store, _filter);

    auto effects = prepare_effects(Inkscape::Extension::db.get_effect_list(), _type == Effects);

    add_effects(*store, effects, _type == Effects);

    auto categories = add_categories(_categories, effects);
    if (!categories.count(_current_category.raw())) {
        _current_category = "all";
    }
    _selector.set_model(_categories);

    _page_selection = _selector.get_selection();
    _selection_change = _page_selection->signal_changed().connect([=](){
        if (auto it = _page_selection->get_selected()) {
            Glib::ustring id;
            it->get_value(g_categories_columns.id.index(), id);
            show_category(id);
        }
    });

    _selection_model = Gtk::SingleSelection::create(_filtered_model);

    _factory = IconViewItemFactory::create([=](auto& ptr) -> IconViewItemFactory::ItemData {
        auto effect = std::dynamic_pointer_cast<EffectItem>(ptr);
        if (!effect) return {};

        auto tex = get_image(effect->id, effect->icon, effect->effect);
        auto name = Glib::Markup::escape_text(effect->name);
        return { .label_markup = name, .image = tex, .tooltip = effect->tooltip };
    });
    _gridview.set_min_columns(1);
    // max columns impacts number of prerendered items requested by gridview (= maxcol * 32 + 1),
    // so it needs to be artificially kept low to prevent gridview from rendering all items up front
    _gridview.set_max_columns(5);
    // gtk_list_base_set_anchor_max_widgets(0); - private method, no way to customize prerender items number
    _gridview.set_model(_selection_model);
    _gridview.set_factory(_factory->get_factory());
    // handle item activation (double-click)
    _gridview.signal_activate().connect([this](auto pos){
        _run.activate_action(_run.get_action_name());
    });
    _gridview.set_single_click_activate(false);

    _search.signal_search_changed().connect([=](){
        refilter();
    });

    _selection_model->signal_selection_changed().connect([this](auto first, auto count) {
        update_name();
    });

    // restore last selected category
    _categories->foreach([=](const auto& path, const auto&it) {
        Glib::ustring id;
        it->get_value(g_categories_columns.id.index(), id);

        if (id.raw() == _current_category.raw()) {
            _page_selection->select(path);
            return true;
        }
        return false;
    });

    // restore thumbnail size
    auto adj = get_object<Gtk::Adjustment>(_builder, "adjustment-thumbnails");
    _thumb_size_index = prefs->getIntLimited(_prefs_path + "/tile-size", 6, adj->get_lower(), adj->get_upper());
    auto scale = &get_widget<Gtk::Scale>(_builder, "thumb-size");
    scale->set_value(_thumb_size_index);

    // populate filtered model
    refilter();

    // initial selection
    _selection_model->select_item(0, true);

    // restore selection: last used extension
    if (!selected.empty()) {
        const auto N = _filtered_model->get_n_items();
        for (int pos = 0; pos < N; ++pos) {
            auto item = _filtered_model->get_typed_object<EffectItem>(pos);
            if (!item) continue;

            if (item->id == selected.raw()) {
                _selection_model->select_item(pos, true);
                //TODO: it's too early for scrolling to work (?)
                auto scroll = Gtk::ScrollInfo::create();
                scroll->set_enable_vertical();
                _gridview.scroll_to(pos, Gtk::ListScrollFlags::NONE, scroll);
                break;
            }
        }
    }

    update_name();

    scale->signal_value_changed().connect([=](){
        _thumb_size_index = scale->get_value();
        rebuild();
        prefs->setInt(_prefs_path + "/tile-size", _thumb_size_index);
    });

    append(get_widget<Gtk::Box>(_builder, "main"));
}

void ExtensionsGallery::update_name() {
    auto& label = get_widget<Gtk::Label>(_builder, "name");
    auto& info = get_widget<Gtk::Label>(_builder, "info");

    auto item = _selection_model->get_selected_item();
    if (auto effect = std::dynamic_pointer_cast<EffectItem>(item)) {
        // access path - where to find it in the main menu
        label.set_label(effect->access);
        label.set_tooltip_text(effect->access);

        // set action name
        _run.set_action_name("app." + effect->id);
        _run.set_sensitive();
        // add ellipsis if extension takes input
        auto& effect_obj = *effect->effect;
        _run_btn_label.set_label(_run_label + (effect_obj.takes_input() ? _("...") : ""));
        // info: extension description, if any
        Glib::ustring desc = effect->description;
        info.set_markup("<i>" + Glib::Markup::escape_text(desc) + "</i>");
        info.set_tooltip_text(desc);

        auto prefs = Inkscape::Preferences::get();
        prefs->setString(_prefs_path + "/selected", effect->id);
    }
    else {
        label.set_label("");
        label.set_tooltip_text("");
        info.set_text("");
        info.set_tooltip_text("");
        _run_btn_label.set_label(_run_label);
        _run.set_sensitive(false);
    }
}

void ExtensionsGallery::show_category(const Glib::ustring& id) {
    if (_current_category.raw() == id.raw()) return;

    _current_category = id;

    auto prefs = Preferences::get();
    prefs->setString(_prefs_path + "/category", id);

    refilter();
}

bool ExtensionsGallery::is_item_visible(const Glib::RefPtr<Glib::ObjectBase>& item) const {
    auto effect_ptr = std::dynamic_pointer_cast<EffectItem>(item);
    if (!effect_ptr) return false;

    const auto& effect = *effect_ptr;

    // filter by category
    if (_current_category != "all") {
        if (_current_category.raw() != effect.category.raw()) return false;
    }

    // filter by name
    auto str = _search.get_text().lowercase();
    if (str.empty()) return true;

    Glib::ustring text = effect.access;
    return text.lowercase().find(str) != Glib::ustring::npos;
}

void ExtensionsGallery::refilter() {
    // When a new expression is set in the BoolFilter, it emits signal_changed(),
    // and the FilterListModel re-evaluates the filter.
    auto expression = Gtk::ClosureExpression<bool>::create([this](auto& item){ return is_item_visible(item); });
    // filter results
    _filter->set_expression(expression);
}

void ExtensionsGallery::rebuild() {
    // empty cache, so item will get re-rendered at new size
    _image_cache.clear();
    // remove all
    auto none = Gtk::ClosureExpression<bool>::create([this](auto& item){ return false; });
    _filter->set_expression(none);
    // restore
    refilter();
}

Geom::Point get_thumbnail_size(int index, ExtensionsGallery::Type type) {
    auto effects = type == ExtensionsGallery::Effects;
    // effect icons range of sizes starts smaller, while filter icons benefit from larger sizes
    int min_size = effects ? 35 : 50;
    const double factor = std::pow(2.0, 1.0 / 6.0);
    // thumbnail size: starting from min_size and growing exponentially
    auto size = std::round(std::pow(factor, index) * min_size);

    auto icon_size = Geom::Point(size, size);
    if (effects) {
        // effects icons have a 70x60 size ratio
        auto height = std::round(size * 6.0 / 7.0);
        icon_size = Geom::Point(size, height);
    }
    return icon_size;
}

Glib::RefPtr<Gdk::Texture> ExtensionsGallery::get_image(const std::string& key, const std::string& icon, Extension::Effect* effect) {
    if (auto image = _image_cache.get(key)) {
        // cache hit
        return *image;
    }
    else {
        // render
        auto icon_size = get_thumbnail_size(_thumb_size_index, _type);
        auto surface = render_icon(effect, icon, icon_size, get_scale_factor());
        auto tex = to_texture(surface);
        _image_cache.insert(key, tex);
        return tex;
    }
}

} // namespace Inkscape::UI::Dialog
