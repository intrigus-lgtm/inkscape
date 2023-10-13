// SPDX-License-Identifier: GPL-2.0-or-later

#include "extensions-gallery.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <list>
#include <memory>
#include <set>
#include <sstream>
#include <string>
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
#include <gtkmm/searchentry.h>
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

namespace Inkscape::UI::Dialog {

struct EffectColumns : public Gtk::TreeModel::ColumnRecord {
    Gtk::TreeModelColumn<std::string> id;     // extension ID
    Gtk::TreeModelColumn<Glib::ustring> name; // effect's name (translated)
    Gtk::TreeModelColumn<Glib::ustring> tooltip;     // menu tip if present, access path otherwise (translated)
    Gtk::TreeModelColumn<Glib::ustring> description; // short description (filters have one; translated)
    Gtk::TreeModelColumn<Glib::ustring> access;   // menu access path (translated)
    Gtk::TreeModelColumn<Glib::ustring> order;    // string to sort items (translated)
    Gtk::TreeModelColumn<Glib::ustring> category; // category (from menu item; translated)
    Gtk::TreeModelColumn<Inkscape::Extension::Effect*> effect;
    Gtk::TreeModelColumn<Cairo::RefPtr<Cairo::Surface>> image;
    Gtk::TreeModelColumn<std::string> icon; // path to effect's SVG icon file

    EffectColumns() {
        add(id);
        add(name);
        add(tooltip);
        add(description);
        add(access);
        add(order);
        add(category);
        add(effect);
        add(image);
        add(icon);
    }
} g_effect_columns;

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

void add_effects(Glib::RefPtr<Gtk::ListStore>& item_store, const std::vector<Inkscape::Extension::Effect*>& effects, bool root) {
    for (auto& effect : effects) {
        const auto id = effect->get_sanitized_id();

        auto row = *item_store->append();

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
        row[g_effect_columns.id] = id;
        row[g_effect_columns.name] = name;
        row[g_effect_columns.tooltip] = description.empty() ? access.str().c_str() : translated(description.c_str());
        row[g_effect_columns.description] = translated(description.c_str());
        row[g_effect_columns.access] = access.str();
        row[g_effect_columns.order] = order.str();
        row[g_effect_columns.category] = get_category(menu);
        row[g_effect_columns.effect] = effect;

        std::string dir(IO::Resource::get_path(IO::Resource::SYSTEM, IO::Resource::EXTENSIONS));
        auto icon = effect->find_icon_file(dir);

        if (icon.empty()) {
            // fallback image
            icon = Inkscape::IO::Resource::get_path_string(IO::Resource::SYSTEM, IO::Resource::UIS, "resources", root ? "missing-icon.svg" : "filter-test.svg");
        }
        row[g_effect_columns.icon] = icon;
    }
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
    _grid(get_widget<Gtk::IconView>(_builder, "grid")),
    _search(get_widget<Gtk::SearchEntry>(_builder, "search")),
    _run(get_widget<Gtk::Button>(_builder, "run")),
    _selector(get_widget<Gtk::TreeView>(_builder, "selector")),
    _image_cache(1000), // arbitrary limit for how many rendered thumbnails to keep around
    _type(type)
{
    _run_label = _type == Effects ? _run.get_label() : _("_Apply");
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
        paned->get_child1()->set_visible(show);
    };
    paned->set_position(position);
    paned->property_position().signal_changed().connect([=](){
        if (auto w = paned->get_child1()) {
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
    _selector.set_row_separator_func([=](Glib::RefPtr<Gtk::TreeModel> const &,
                                         Gtk::TreeModel::const_iterator const &it)
    {
        Glib::ustring id;
        it->get_value(g_categories_columns.id.index(), id);
        return id == "-";
    });

    _store = Gtk::ListStore::create(g_effect_columns);
    _filtered = Gtk::TreeModelFilter::create(_store);
    auto model = Gtk::TreeModelSort::create(_filtered);

    auto effects = prepare_effects(Inkscape::Extension::db.get_effect_list(), _type == Effects);

    add_effects(_store, effects, _type == Effects);
    model->set_sort_column(g_effect_columns.order.index(), Gtk::SortType::ASCENDING);

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

    _grid.pack_start(_image_renderer);
    _grid.add_attribute(_image_renderer, "surface", g_effect_columns.image);
    _grid.set_cell_data_func(_image_renderer, [=](const Gtk::TreeModel::const_iterator& it){
        Gdk::Rectangle rect;
        Gtk::TreeModel::Path path(it);
        if (_grid.get_cell_rect(path, rect)) {
            auto height = _grid.get_allocated_height();
            bool visible = !(rect.get_x() < 0 && rect.get_y() < 0);
            // cell rect coordinates are not affected by scrolling
            if (visible && (rect.get_y() + rect.get_height() < 0 || rect.get_y() > 0 + height)) {
                visible = false;
            }
            get_cell_data_func(&_image_renderer, *it, visible);
        }
    });

    _grid.set_text_column(g_effect_columns.name);
    _grid.set_tooltip_column(g_effect_columns.tooltip.index());
    _grid.set_item_width(80); // min width to accomodate labels
    _grid.set_column_spacing(0);
    _grid.set_row_spacing(0);
    _grid.set_model(model);
    _grid.signal_item_activated().connect([=](const Gtk::TreeModel::Path& path){
        _run.clicked();
    });

    _search.signal_search_changed().connect([=](){
        refilter();
    });

    _filtered->set_visible_func([=](const Gtk::TreeModel::const_iterator& it){
        // filter by category
        if (_current_category != "all") {
            Glib::ustring cat = (*it)[g_effect_columns.category];
            if (_current_category != cat) return false;
        }

        // filter by name
        if (_search.get_text_length() == 0) return true;
    
        auto str = _search.get_text().lowercase();
        Glib::ustring text = (*it)[g_effect_columns.access];
        return text.lowercase().find(str) != Glib::ustring::npos;
    });

    // restore selection: last used extension
    if (!selected.empty()) {
        model->foreach_path([=](const Gtk::TreeModel::Path& path){
            auto it = model->get_iter(path);
            if (selected.raw() == static_cast<std::string>((*it)[g_effect_columns.id])) {
                _grid.select_path(path);
                return true;
            }
            return false;
        });
    }

    update_name();

    _grid.signal_selection_changed().connect([=](){
        update_name();
    });

    _categories->foreach([=](Gtk::TreeModel::Path const &path,
                             Gtk::TreeModel::const_iterator const &it)
    {
        Glib::ustring id;
        it->get_value(g_categories_columns.id.index(), id);

        if (id == _current_category) {
            _page_selection->select(path);
            return true;
        }
        return false;
    });

    // thumbnail size
    auto adj = get_object<Gtk::Adjustment>(_builder, "adjustment-thumbnails");
    _thumb_size_index = prefs->getIntLimited(_prefs_path + "/tile-size", 6, adj->get_lower(), adj->get_upper());
    auto scale = &get_widget<Gtk::Scale>(_builder, "thumb-size");
    scale->set_value(_thumb_size_index);
    scale->signal_value_changed().connect([=](){
        _thumb_size_index = scale->get_value();
        rebuild();
        prefs->setInt(_prefs_path + "/tile-size", _thumb_size_index);
    });

    refilter();

    add(get_widget<Gtk::Box>(_builder, "main"));
}

void ExtensionsGallery::update_name() {
    auto& label = get_widget<Gtk::Label>(_builder, "name");
    auto& info = get_widget<Gtk::Label>(_builder, "info");

    if (auto row = selected_item()) {
        // access path - where to find it in the main menu
        label.set_label(row[g_effect_columns.access]);
        label.set_tooltip_text(row[g_effect_columns.access]);

        // set action name
        std::string id = row[g_effect_columns.id];
        gtk_actionable_set_action_name(GTK_ACTIONABLE(_run.gobj()), ("app." + id).c_str());
        _run.set_sensitive();
        // add ellipsis if extension takes input
        auto& effect = *row[g_effect_columns.effect];
        _run.set_label(_run_label + (effect.takes_input() ? _("...") : ""));
        // info: extension description
        Glib::ustring desc = row[g_effect_columns.description];
        info.set_markup("<i>" + Glib::Markup::escape_text(desc) + "</i>");
        info.set_tooltip_text(desc);

        auto prefs = Inkscape::Preferences::get();
        prefs->setString(_prefs_path + "/selected", id);
    }
    else {
        label.set_label("");
        label.set_tooltip_text("");
        info.set_text("");
        info.set_tooltip_text("");
        _run.set_label(_run_label);
        _run.set_sensitive(false);
    }
}

Gtk::TreeModel::Row ExtensionsGallery::selected_item() {
    auto sel = _grid.get_selected_items();
    auto model = _grid.get_model();
    Gtk::TreeModel::Row row;
    if (sel.size() == 1 && model) {
        row = *model->get_iter(sel.front());
    }
    return row;
}

void ExtensionsGallery::show_category(const Glib::ustring& id) {
    if (_current_category == id) return;

    _current_category = id;

    auto prefs = Preferences::get();
    prefs->setString(_prefs_path + "/category", id);

    refilter();
}

void ExtensionsGallery::refilter() {
    // filter results
    _filtered->freeze_notify();
    _filtered->refilter();
    _filtered->thaw_notify();
}

void ExtensionsGallery::rebuild() {
    _image_cache.clear();
    _grid.queue_draw();
    //ToDo: revisit in gtk4 - this code forces iconview to resize items to new image size
    auto model = _grid.get_model();
    _grid.unset_model();
    _grid.set_model(model);
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

// This is an attempt to render images on-demand (visible only), as opposed to all of them in the store.
// Hopefully this can be simplified in gtk4.
void ExtensionsGallery::get_cell_data_func(Gtk::CellRenderer* cell_renderer, Gtk::TreeModel::Row row, bool visible)
{
    std::string icon_file = (row)[g_effect_columns.icon];
    std::string cache_key = (row)[g_effect_columns.id];

    Cairo::RefPtr<Cairo::Surface> surface;
    auto icon_size = get_thumbnail_size(_thumb_size_index, _type);

    if (!visible) {
        // cell is not visible, so this is layout pass; return empty image of the right size
        if (!_blank_image || _blank_image->get_width() != icon_size.x() || _blank_image->get_height() != icon_size.y()) {
            _blank_image = _blank_image.cast_static(render_icon(nullptr, {}, icon_size, get_scale_factor()));
        }
        surface = _blank_image;
    }
    else {
        // cell is visible, so we need to return correct symbol image and render it if it's missing
        if (auto image = _image_cache.get(cache_key)) {
            // cache hit
            surface = *image;
        }
        else {
            // render
            Extension::Effect* effect = row[g_effect_columns.effect];
            surface = render_icon(effect, icon_file, icon_size, get_scale_factor());
            row[g_effect_columns.image] = surface;
            _image_cache.insert(cache_key, surface);
        }
    }
    cell_renderer->set_property("surface", surface);
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
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:
