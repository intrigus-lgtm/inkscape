// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A simple dialog for previewing document resources
 *
 * Copyright (C) 2023 Michael Kowalski
 */

#include "document-resources.h"

#include <cstddef>
#include <map>
#include <memory>

#include <cairo.h>
#include <cairomm/enums.h>
#include <cairomm/refptr.h>
#include <cairomm/surface.h>

#include <gdkmm/pixbuf.h>
#include <gdkmm/rgba.h>
#include <glib/gi18n.h>
#include <glibmm/exception.h>
#include <glibmm/fileutils.h>
#include <glibmm/main.h>
#include <glibmm/markup.h>
#include <glibmm/miscutils.h>
#include <glibmm/uriutils.h>
#include <glibmm/ustring.h>
#include <gtkmm/builder.h>
#include <gtkmm/button.h>
#include <gtkmm/celleditable.h>
#include <gtkmm/cellrendererpixbuf.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/dialog.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/enums.h>
#include <gtkmm/grid.h>
#include <gtkmm/iconview.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/paned.h>
#include <gtkmm/searchentry.h>
#include <gtkmm/stack.h>
#include <gtkmm/treemodelfilter.h>
#include <gtkmm/treemodelsort.h>
#include <gtkmm/treeselection.h>
#include <gtkmm/treeview.h>
#include <gtkmm/window.h>

#include "color.h"
#include "desktop.h"
#include "display/cairo-utils.h"
#include "document.h"
#include "document-undo.h"
#include "extension/system.h"
#include "helper/choose-file.h"
#include "helper/save-image.h"
#include "inkscape.h"
#include "object/sp-filter.h"
#include "object/filters/sp-filter-primitive.h"
#include "object/color-profile.h"
#include "object/sp-gradient.h"
#include "object/sp-font.h"
#include "object/sp-image.h"
#include "object/sp-item-group.h"
#include "object/sp-marker.h"
#include "object/sp-mesh-gradient.h"
#include "object/sp-object.h"
#include "object/sp-offset.h"
#include "object/sp-path.h"
#include "object/sp-pattern.h"
#include "object/tags.h"
#include "rdf.h"
#include "selection.h"
#include "object/sp-defs.h"
#include "object/sp-root.h"
#include "object/sp-symbol.h"
#include "object/sp-use.h"
#include "style.h"
#include "ui/builder-utils.h"
#include "ui/icon-names.h"
#include "ui/themes.h"
#include "ui/util.h"
#include "ui/widget/shapeicon.h"
#include "util/object-renderer.h"
#include "util/trim.h"
#include "xml/href-attribute-helper.h"

namespace Inkscape::UI::Dialog {

struct ItemColumns final : public Gtk::TreeModel::ColumnRecord {
    Gtk::TreeModelColumn<Glib::ustring> id;
    Gtk::TreeModelColumn<Glib::ustring> label;
    Gtk::TreeModelColumn<Cairo::RefPtr<Cairo::Surface>> image;
    Gtk::TreeModelColumn<bool> editable;
    Gtk::TreeModelColumn<SPObject*> object;
    Gtk::TreeModelColumn<int> color;

    ItemColumns() {
        add(id);
        add(label);
        add(image);
        add(editable);
        add(object);
        add(color);
    }
} g_item_columns;

struct InfoColumns final : public Gtk::TreeModel::ColumnRecord {
    Gtk::TreeModelColumn<Glib::ustring> item;
    Gtk::TreeModelColumn<Glib::ustring> value;
    Gtk::TreeModelColumn<uint32_t> count;
    Gtk::TreeModelColumn<SPObject*> object;

    InfoColumns() {
        add(item);
        add(value);
        add(count);
        add(object);
    }
} g_info_columns;

enum Resources : int {
    Stats, Colors, Fonts, Styles, Patterns, Symbols, Markers, Gradients, Swatches, Images, Filters, External, Metadata
};

const std::unordered_map<std::string, Resources> g_id_to_resource = {
    {"colors",    Colors},
    {"swatches",  Swatches},
    {"fonts",     Fonts},
    {"stats",     Stats},
    {"styles",    Styles},
    {"patterns",  Patterns},
    {"symbols",   Symbols},
    {"markers",   Markers},
    {"gradients", Gradients},
    {"images",    Images},
    {"filters",   Filters},
    {"external",  External},
    {"metadata",  Metadata},
    // to do: SVG fonts
    // other resources
};

std::size_t get_resource_count(details::Statistics const &stats, Resources const rsrc)
{
    switch (rsrc) {
        case Colors:    return stats.colors;
        case Swatches:  return stats.swatches;
        case Fonts:     return stats.fonts;
        case Symbols:   return stats.symbols;
        case Gradients: return stats.gradients;
        case Patterns:  return stats.patterns;
        case Images:    return stats.images;
        case Filters:   return stats.filters;
        case Markers:   return stats.markers;
        case Metadata:  return stats.metadata;
        case Styles:    return stats.styles;
        case External:  return stats.external_uris;

        case Stats:     return 1;

        default:
            break;
    }
    return 0;
}

Resources id_to_resource(const std::string& id) {
    auto it = g_id_to_resource.find(id);
    if (it == end(g_id_to_resource)) return Stats;

    return it->second;
}

std::size_t get_resource_count(std::string const &id, details::Statistics const &stats)
{
    auto it = g_id_to_resource.find(id);
    if (it == end(g_id_to_resource)) return 0;

    return get_resource_count(stats, it->second);
}

bool is_resource_present(std::string const &id, details::Statistics const &stats)
{
    return get_resource_count(id, stats) > 0;
}

std::string choose_file(Glib::ustring title, Gtk::Window* parent, Glib::ustring mime_type, Glib::ustring file_name) {
    static std::string current_folder;
    return Inkscape::choose_file_save(title, parent, mime_type, file_name, current_folder);
}

void save_gimp_palette(std::string fname, const std::vector<int>& colors, const char* name) {
    try {
        std::ostringstream ost;
        ost << "GIMP Palette\n";
        if (name && *name) {
            ost << "Name: " << name << "\n";
        }
        ost << "#\n";
        for (auto c : colors) {
            auto r = (c >> 16) & 0xff;
            auto g = (c >> 8) & 0xff;
            auto b = c & 0xff;
            ost << r << ' ' << g << ' ' << b << '\n';
        }
        Glib::file_set_contents(fname, ost.str());
    }
    catch (Glib::Exception& ex) {
        g_warning("Error saving color palette: %s", ex.what().c_str());
    }
    catch (...) {
        g_warning("Error saving color palette.");
    }
}

void extract_colors(Gtk::Window* parent, const std::vector<int>& colors, const char* name) {
    if (colors.empty() || !parent) return;

    auto fname = choose_file(_("Export Color Palette"), parent, "application/color-palette", "color-palette.gpl");
    if (fname.empty()) return;

    // export palette
    save_gimp_palette(fname, colors, name);
}

void delete_object(SPObject* object, Inkscape::Selection* selection) {
    if (!object || !selection) return;

    auto document = object->document;

    if (auto pattern = cast<SPPattern>(object)) {
        // delete action fails for patterns; remove them by deleting their nodes
        sp_repr_unparent(pattern->getRepr());
        DocumentUndo::done(document, _("Delete pattern"), INKSCAPE_ICON("document-resources"));
    }
    else if (auto gradient = cast<SPGradient>(object)) {
        // delete action fails for gradients; remove them by deleting their nodes
        sp_repr_unparent(gradient->getRepr());
        DocumentUndo::done(document, _("Delete gradient"), INKSCAPE_ICON("document-resources"));
    }
    else {
        selection->set(object);
        selection->deleteItems();
    }
}

namespace details {

// editing "inkscape:label"
Glib::ustring get_inkscape_label(const SPObject& object) {
    auto label = object.getAttribute("inkscape:label");
    return Glib::ustring(label ? label : "");
}
void set_inkscape_label(SPObject& object, const Glib::ustring& label) {
    object.setAttribute("inkscape:label", label.c_str());
}

// editing title element
Glib::ustring get_title(const SPObject& object) {
    auto title = object.title();
    Glib::ustring str(title ? title : "");
    g_free(title);
    return str;
}
void set_title(SPObject& object, const Glib::ustring& title) {
    object.setTitle(title.c_str());
}

} // namespace details

// label editing: get/set functions for various object types;
// by default "inkscape:label" will be used (expressed as SPObject);
// if some types need exceptions to this ruke, they can provide their own edit functions;
// note: all most-derived types need to be listed to specify overrides
std::map<std::type_index, std::function<Glib::ustring (const SPObject&)>> g_get_label = {
    // default: editing "inkscape:label" as a description;
    // patterns use Inkscape-specific "inkscape:label" attribute;
    // gradients can also use labels instead of IDs;
    // filters; to do - editing in a tree view;
    // images can use both, label & title; defaulting to label for consistency
    {typeid(SPObject), details::get_inkscape_label},
    // exception: symbols use <title> element for description
    {typeid(SPSymbol), details::get_title},
    // markers use stockid for some reason - label: to do
    {typeid(SPMarker), details::get_inkscape_label},
};

std::map<std::type_index, std::function<void (SPObject&, const Glib::ustring&)>> g_set_label = {
    {typeid(SPObject), details::set_inkscape_label},
    {typeid(SPSymbol), details::set_title},
    {typeid(SPMarker), details::set_inkscape_label},
};

// liststore columns from glade file
constexpr int COL_ID = 1;
constexpr int COL_ICON = 2;
constexpr int COL_COUNT = 3;

DocumentResources::DocumentResources()
    : DialogBase("/dialogs/document-resources", "DocumentResources"),
    _builder(create_builder("dialog-document-resources.glade")),
    _iconview(get_widget<Gtk::IconView>(_builder, "iconview")),
    _treeview(get_widget<Gtk::TreeView>(_builder, "treeview")),
    _selector(get_widget<Gtk::TreeView>(_builder, "tree")),
    _edit(get_widget<Gtk::Button>(_builder, "edit")),
    _select(get_widget<Gtk::Button>(_builder, "select")),
    _delete(get_widget<Gtk::Button>(_builder, "delete")),
    _extract(get_widget<Gtk::Button>(_builder, "extract")),
    _search(get_widget<Gtk::SearchEntry>(_builder, "search")) {

    _info_store = Gtk::ListStore::create(g_info_columns);
    _item_store = Gtk::ListStore::create(g_item_columns);
    auto filtered_info = Gtk::TreeModelFilter::create(_info_store);
    auto filtered_items = Gtk::TreeModelFilter::create(_item_store);
    auto model = Gtk::TreeModelSort::create(filtered_items);
    model->set_sort_column(g_item_columns.label.index(), Gtk::SortType::ASCENDING);

    add(get_widget<Gtk::Box>(_builder, "main"));

    _iconview.set_model(model);
    _iconview.set_text_column(g_item_columns.label);
    _label_renderer = dynamic_cast<Gtk::CellRendererText*>(_iconview.get_first_cell());
    assert(_label_renderer);
    _label_renderer->property_editable() = true;
    _label_renderer->signal_editing_started().connect([this](Gtk::CellEditable * const cell, Glib::ustring const &path){
        start_editing(cell, path);
    });
    _label_renderer->signal_edited().connect([this](Glib::ustring const &path, Glib::ustring const &new_text){
        end_editing(path, new_text);
    });

    _iconview.pack_start(_image_renderer);
    _iconview.add_attribute(_image_renderer, "surface", g_item_columns.image);

    _treeview.set_model(filtered_info);

    auto treestore = get_object<Gtk::ListStore>(_builder, "liststore");
    _selector.set_row_separator_func([=](Glib::RefPtr<Gtk::TreeModel> const &/*model*/,
                                         Gtk::TreeModel::const_iterator const &it)
    {
        Glib::ustring id;
        it->get_value(COL_ID, id);
        return id == "-";
    });
    _categories = Gtk::TreeModelFilter::create(treestore);
    _categories->set_visible_func([this](Gtk::TreeModel::const_iterator const &it){
        Glib::ustring id;
        it->get_value(COL_ID, id);
        return id == "-" || is_resource_present(id, _stats);
    });
    _selector.set_model(_categories);
    auto const icon_renderer = Gtk::make_managed<UI::Widget::CellRendererItemIcon>();
    _selector.insert_column("", *icon_renderer, 0);
    auto column = _selector.get_column(0);
    column->add_attribute(*icon_renderer, icon_renderer->property_shape_type().get_name(), COL_ICON);
    auto const count_renderer = Gtk::make_managed<Gtk::CellRendererText>();
    auto count_column = _selector.get_column(_selector.append_column("", *count_renderer) - 1);
    count_column->add_attribute(*count_renderer, "text", COL_COUNT);
    count_column->set_cell_data_func(*count_renderer, [=](Gtk::CellRenderer * const r,
                                                          Gtk::TreeModel::const_iterator const &it)
    {
        std::uint64_t count;
        it->get_value(COL_COUNT, count);
        count_renderer->property_text().set_value(count > 0 ? std::to_string(count) : "");
    });
    count_renderer->set_padding(3, 4);

    _wr.setUpdating(true); // set permanently

    for (auto entity = rdf_work_entities; entity && entity->name; ++entity) {
        if (entity->editable != RDF_EDIT_GENERIC) continue;

        auto w = Inkscape::UI::Widget::EntityEntry::create(entity, _wr);
        _rdf_list.push_back(w);
    }

    _page_selection = _selector.get_selection();
    _selection_change = _page_selection->signal_changed().connect([this]{
        if (auto it = _page_selection->get_selected()) {
            Glib::ustring id;
            it->get_value(COL_ID, id);
            select_page(id);
        }
    });

    auto paned = &get_widget<Gtk::Paned>(_builder, "paned");
    auto const move = [=, this](){
        auto pos = paned->get_position();
        get_widget<Gtk::Label>(_builder, "spacer").set_size_request(pos);
    };
    paned->property_position().signal_changed().connect([=](){ move(); });
    move();

    _edit.signal_clicked().connect([this]{
        auto sel = _iconview.get_selected_items();
        if (sel.size() == 1) {
            // todo: investigate why this doesn't work initially:
            _iconview.set_cursor(sel.front(), true);
        }
        // treeview todo if needed
    });

    // selectable elements can be selected on the canvas;
    // even elements in <defs> can be selected (same as in XML dialog)
    _select.signal_clicked().connect([this]{
        auto document = getDocument();
        auto desktop = getDesktop();
        if (!document || !desktop) return;

        if (auto row = selected_item()) {
            Glib::ustring id = row[g_item_columns.id];
            if (auto object = document->getObjectById(id)) {
                // select object
                desktop->getSelection()->set(object);
            }
        }
        else {
            // to do: select from treeview if needed
        }
    });

    _search.signal_search_changed().connect([=](){
        filtered_items->freeze_notify();
        filtered_items->refilter();
        filtered_items->thaw_notify();

        filtered_info->freeze_notify();
        filtered_info->refilter();
        filtered_info->thaw_notify();
    });

    // filter gridview
    filtered_items->set_visible_func([this](Gtk::TreeModel::const_iterator const &it){
        if (_search.get_text_length() == 0) return true;
    
        auto str = _search.get_text().lowercase();
        Glib::ustring label = (*it)[g_item_columns.label];
        return label.lowercase().find(str) != Glib::ustring::npos;
    });
    // filter treeview too
    filtered_info->set_visible_func([this](Gtk::TreeModel::const_iterator const &it){
        if (_search.get_text_length() == 0) return true;
    
        auto str = _search.get_text().lowercase();
        Glib::ustring value = (*it)[g_info_columns.value];
        return value.lowercase().find(str) != Glib::ustring::npos;
    });

    _delete.signal_clicked().connect([this]{
        // delete selected object
        if (auto row = selected_item()) {
            SPObject* object = row[g_item_columns.object];
            delete_object(object, getDesktop()->getSelection());
        }
    });

    _extract.signal_clicked().connect([this]{
        auto const window = dynamic_cast<Gtk::Window *>(get_root());

        switch (_showing_resource) {
        case Images:
            // extract selected image
            if (auto row = selected_item()) {
                SPObject* object = row[g_item_columns.object];
                extract_image(window, cast<SPImage>(object));
            }
            break;
        case Colors:
            // export colors into a GIMP palette
            if (_document) {
                std::vector<int> colors;
                _item_store->foreach_iter([&](Gtk::TreeModel::const_iterator const &it)
                {
                    int c;
                    it->get_value(g_item_columns.color.index(), c);
                    colors.push_back(c);
                    return false; // false means continue
                });
                extract_colors(window, colors, _document->getDocumentName());
            }
            break;
        default:
            // nothing else so far
            break;
        }
    });

    _iconview.signal_selection_changed().connect([this]{
        update_buttons();
    });

}

Gtk::TreeModel::Row DocumentResources::selected_item() {
    auto sel = _iconview.get_selected_items();
    auto model = _iconview.get_model();
    Gtk::TreeModel::Row row;
    if (sel.size() == 1 && model) {
        row = *model->get_iter(sel.front());
    }
    return row;
}

void DocumentResources::update_buttons() {
    if (!_iconview.get_visible()) return;

    auto single_sel = !!selected_item();

    _edit.set_sensitive(single_sel);
    _extract.set_sensitive(single_sel || _showing_resource == Colors);
    _delete.set_sensitive(single_sel);
    _select.set_sensitive(single_sel);
}

Cairo::RefPtr<Cairo::Surface> render_color(uint32_t rgb, double size, double radius, int device_scale) {
    Cairo::RefPtr<Cairo::Surface> nul;
    return add_background_to_image(nul, rgb, size / 2, radius, device_scale, 0x7f7f7f00);
}

void collect_object_colors(SPObject& obj, std::map<std::string, SPColor>& colors) {
    auto style = obj.style;

    if (style->stroke.set && style->stroke.colorSet) {
        const auto& c = style->stroke.value.color;
        colors[c.toString()] = c;
    }

    if (style->color.set) {
        const auto& c = style->color.value.color;
        colors[c.toString()] = c;
    }

    if (style->fill.set) {
        const auto& c = style->fill.value.color;
        colors[c.toString()] = c;
    }

    if (style->solid_color.set) {
        const auto& c = style->solid_color.value.color;
        colors[c.toString()] = c;
    }
}

// traverse all nodes starting from given 'object'
template<typename V>
void apply_visitor(SPObject& object, V&& visitor) {
    visitor(object);

    // SPUse inserts referenced object as a child; skip it
    if (is<SPUse>(&object)) return;

    for (auto&& child : object.children) {
        apply_visitor(child, visitor);
    }
}

std::map<std::string, SPColor> collect_colors(SPObject* object) {
    std::map<std::string, SPColor> colors;
    if (object) {
        apply_visitor(*object, [&](SPObject& obj){ collect_object_colors(obj, colors); });
    }
    return colors;
}

void collect_used_fonts(SPObject& object, std::set<std::string>& fonts) {
    auto style = object.style;

    if (style->font_specification.set) {
        auto fspec = style->font_specification.value();
        if (fspec && *fspec) {
            fonts.insert(fspec);
        }
    }
    else if (style->font.set) {
        // some SVG files won't have Inkscape-specific fontspec; read font settings instead
        auto font = style->font.get_value();
        if (style->font_style.set) {
            font += ' ' + style->font_style.get_value();
        }
        fonts.insert(font);
    }
}

std::set<std::string> collect_fontspecs(SPObject* object) {
    std::set<std::string> fonts;
    if (object) {
        apply_visitor(*object, [&](SPObject& obj){ collect_used_fonts(obj, fonts); });
    }
    return fonts;
}

template<typename T>
bool filter_element(T& object) { return true; }

template<>
bool filter_element<SPPattern>(SPPattern& object) { return object.hasChildren(); }

template<>
bool filter_element<SPGradient>(SPGradient& object) { return object.hasStops(); }

template<typename T>
std::vector<T*> collect_items(SPObject* object, bool (*filter)(T&) = filter_element<T>) {
    std::vector<T*> items;
    if (object) {
        apply_visitor(*object, [&](SPObject& obj){
            if (auto t = cast<T>(&obj)) {
                if (filter(*t)) items.push_back(t);
            }
        });
    }
    return items;
}

std::unordered_map<std::string, size_t> collect_styles(SPObject* root) {
    std::unordered_map<std::string, size_t> map;
    if (!root) return map;

    apply_visitor(*root, [&](SPObject& obj){
        if (auto style = obj.getAttribute("style")) {
            map[style]++;
        }
    });

    return map;
}

bool has_external_ref(SPObject& obj) {
    bool present = false;
    if (auto href = Inkscape::getHrefAttribute(*obj.getRepr()).second) {
        if (*href && *href != '#' && *href != '?') {
            auto scheme = Glib::uri_parse_scheme(href);
            // There are tens of schemes: https://www.iana.org/assignments/uri-schemes/uri-schemes.xhtml
            // TODO: Which ones to collect as external resources?
            if (scheme == "file" || scheme == "http" || scheme == "https" || scheme.empty()) {
                present = true;
            }
        }
    }
    return present;
}

details::Statistics collect_statistics(SPObject* root) {
    details::Statistics stats;

    if (!root) {
        return stats;
    }

    std::map<std::string, SPColor> colors;
    std::set<std::string> fonts;

    apply_visitor(*root, [&](SPObject& obj){
        // order of tests is important; derived classes first, before base,
        // so meshgradient first, gradient next

        if (auto pattern = cast<SPPattern>(&obj)) {
            if (filter_element(*pattern)) {
                stats.patterns++;
            }
        }
        else if (is<SPMeshGradient>(&obj)) {
            stats.meshgradients++;
        }
        else if (auto gradient = cast<SPGradient>(&obj)) {
            if (filter_element(*gradient)) {
                if (gradient->isSwatch()) {
                    stats.swatches++;
                }
                else {
                    stats.gradients++;
                }
            }
        }
        else if (auto marker = cast<SPMarker>(&obj)) {
            if (filter_element(*marker)) {
                stats.markers++;
            }
        }
        else if (auto symbol = cast<SPSymbol>(&obj)) {
            if (filter_element(*symbol)) {
                stats.symbols++;
            }
        }
        else if (is<SPFont>(&obj)) { // SVG font
            stats.svg_fonts++;
        }
        else if (is<SPImage>(&obj)) {
            stats.images++;
        }
        else if (auto group = cast<SPGroup>(&obj)) {
            if (strcmp(group->getRepr()->name(), "svg:g") == 0) {
                switch (group->layerMode()) {
                    case SPGroup::GROUP:
                        stats.groups++;
                        break;
                    case SPGroup::LAYER:
                        stats.layers++;
                        break;
                }
            }
        }
        else if (is<SPPath>(&obj)) {
            stats.paths++;
        }
        else if (is<SPFilter>(&obj)) {
            stats.filters++;
        }
        else if (is<ColorProfile>(&obj)) {
            stats.colorprofiles++;
        }

        if (auto style = obj.getAttribute("style")) {
            if (*style) stats.styles++;
        }

        if (has_external_ref(obj)) {
            stats.external_uris++;
        }

        collect_object_colors(obj, colors);
        collect_used_fonts(obj, fonts);

        // verify:
        stats.nodes++;
    });

    stats.colors = colors.size();
    stats.fonts = fonts.size();

    return stats;
}

details::Statistics DocumentResources::collect_statistics()
{
    auto root = _document ? _document->getRoot() : nullptr;
    auto stats = ::Inkscape::UI::Dialog::collect_statistics(root);

    if (_document) {
        for (auto& el : _rdf_list) {
            bool read_only = true;
            el.update(_document, read_only);
            if (!el.content().empty()) stats.metadata++;
        }
    }

    return stats;
}

void DocumentResources::rebuild_stats() {
    _stats = collect_statistics();

    if (auto desktop = getDesktop()) {
        _wr.setDesktop(desktop);
    }

    _categories->refilter();
    _categories->foreach_iter([this](Gtk::TreeModel::iterator const &it){
        Glib::ustring id;
        it->get_value(COL_ID, id);
        auto count = get_resource_count(id, _stats);
        if (id == "stats") count = 0; // don't show count 1 for "overview"
        it->set_value(COL_COUNT, count);
        return false; // false means continue
    });
    _selector.columns_autosize();
}

void DocumentResources::documentReplaced() {
    _document = getDocument();
    if (_document) {
        _document_modified = _document->connectModified([this](unsigned){
            // brute force refresh, but throttled
            _idle_refresh = Glib::signal_timeout().connect([this]{
                rebuild_stats();
                refresh_current_page();
                return false;
            }, 200);
        });
    }
    else {
        _document_modified.disconnect();
    }

    rebuild_stats();
    refresh_current_page();
}

void DocumentResources::refresh_current_page() {
    auto page = _cur_page_id;
    if (!is_resource_present(page, _stats)) {
        page = "stats";
    }
    auto model = _selector.get_model();

    model->foreach([&](Gtk::TreeModel::Path const &path,
                       Gtk::TreeModel::const_iterator const &it)
    {
        Glib::ustring id;
        it->get_value(COL_ID, id);

        if (id.raw() == page) {
            _page_selection->select(path);
            refresh_page(id);
            return true;
        }
        return false;
    });
}

void DocumentResources::selectionModified(Inkscape::Selection* selection, unsigned flags)
{
    // no op so far
}

auto get_id = [](const SPObject* object) { auto id = object->getId(); return id ? id : ""; };
auto label_fmt = [](const char* label, const Glib::ustring& id) { return label && *label ? label : '#' + id; };

void add_colors(Glib::RefPtr<Gtk::ListStore> item_store, const std::map<std::string, SPColor>& colors, int device_scale) {
    for (auto&& it : colors) {
        const auto& color = it.second;

        auto row = *item_store->append();
        auto name = color.toString();
        auto rgba32 = color.toRGBA32(0xff);
        auto rgb24 = rgba32 >> 8;

        row[g_item_columns.id] = name;
        row[g_item_columns.label] = name;
        row[g_item_columns.color] = rgb24;
        int size = 20;
        double radius = 2.0;
        row[g_item_columns.image] = render_color(rgba32, size, radius, device_scale);
        row[g_item_columns.object] = nullptr;
    }
}

void _add_items_with_images(Glib::RefPtr<Gtk::ListStore> item_store, const std::vector<SPObject*>& items, double width, double height, int device_scale, bool use_title, object_renderer::options opt) {
    object_renderer renderer;
    item_store->freeze_notify();

    for (auto item : items) {
        auto row = *item_store->append();

        auto id = get_id(item);
        row[g_item_columns.id] = id;

        if (use_title) {
            auto title = item->title();
            row[g_item_columns.label] = label_fmt(title, id);
            g_free(title);
        }
        else {
            auto label = item->getAttribute("inkscape:label");
            row[g_item_columns.label] = label_fmt(label, id);
        }
        row[g_item_columns.image] = renderer.render(*item, width, height, device_scale, opt);
        row[g_item_columns.object] = item;
    }

    item_store->thaw_notify();
}

template<typename T>
void add_items_with_images(Glib::RefPtr<Gtk::ListStore> item_store, const std::vector<T*>& items, double width, double height, int device_scale, bool use_title = false, object_renderer::options opt = {}) {
    static_assert(std::is_base_of<SPObject, T>::value);
    _add_items_with_images(item_store, reinterpret_cast<const std::vector<SPObject*>&>(items), width, height, device_scale, use_title, opt);
}

void add_fonts(Glib::RefPtr<Gtk::ListStore> store, const std::set<std::string>& fontspecs) {
    size_t i = 1;
    for (auto&& fs : fontspecs) {
        auto row = *store->append();
        row[g_info_columns.item] = Glib::ustring::compose("%1 %2", _("Font"), i++);
        auto name = Glib::Markup::escape_text(fs);
        row[g_info_columns.value] = Glib::ustring::format(
            "<span allow_breaks='false' size='xx-large' font='", fs, "'>", name, "</span>\n",
            "<span allow_breaks='false' size='small' alpha='60%'>", name, "</span>"
        );
    }
}

void add_stats(Glib::RefPtr<Gtk::ListStore> info_store, SPDocument* document, const details::Statistics& stats) {
    auto read_only = true;
    auto license = document ? rdf_get_license(document, read_only) : nullptr;

    std::pair<const char*, std::string> str[] = {
        {_("Document"), document && document->getDocumentFilename() ? document->getDocumentFilename() : "-"},
        {_("License"), license && license->name ? license->name : "-"},
        {_("Metadata"), stats.metadata > 0 ? C_("Adjective for Metadata status", "Present") : "-"},
    };
    for (auto& pair : str) {
        auto row = *info_store->append();
        row[g_info_columns.item] = pair.first;
        row[g_info_columns.value] = Glib::Markup::escape_text(pair.second);
    }

    std::pair<const char*, size_t> kv[] = {
        {_("Colors"), stats.colors},
        {_("Color profiles"), stats.colorprofiles},
        {_("Swatches"), stats.swatches},
        {_("Fonts"), stats.fonts},
        {_("Gradients"), stats.gradients},
        {_("Mesh gradients"), stats.meshgradients},
        {_("Patterns"), stats.patterns},
        {_("Symbols"), stats.symbols},
        {_("Markers"), stats.markers},
        {_("Filters"), stats.filters},
        {_("Images"), stats.images},
        {_("SVG fonts"), stats.svg_fonts},
        {_("Layers"), stats.layers},
        {_("Total elements"), stats.nodes},
        {_("Groups"), stats.groups},
        {_("Paths"), stats.paths},
        {_("External URIs"), stats.external_uris},
    };
    for (auto& pair : kv) {
        auto row = *info_store->append();
        row[g_info_columns.item] = pair.first;
        row[g_info_columns.value] = pair.second ? std::to_string(pair.second) : "-";
    }
}

void add_metadata(Glib::RefPtr<Gtk::ListStore> info_store, SPDocument* document, 
    const boost::ptr_vector<Inkscape::UI::Widget::EntityEntry>& rdf_list) {

    for (auto& entry : rdf_list) {
        auto row = *info_store->append();
        auto label = entry._label.get_label();
        Util::trim(label, ":");
        row[g_info_columns.item] = label;
        row[g_info_columns.value] = Glib::Markup::escape_text(entry.content());
    }
}

void add_filters(Glib::RefPtr<Gtk::ListStore> info_store, const std::vector<SPFilter*>& filters) {
    for (auto& filter : filters) {
        auto row = *info_store->append();
        auto label = filter->getAttribute("inkscape:label");
        auto name = Glib::ustring(label ? label : filter->getId());
        row[g_info_columns.item] = name;
        std::ostringstream ost;
        bool first = true;
        for (auto& obj : filter->children) {
            if (auto primitive = cast<SPFilterPrimitive>(&obj)) {
                if (!first) ost << ", ";
                Glib::ustring name = primitive->getRepr()->name();
                if (name.find("svg:") != std::string::npos) {
                    name.erase(name.find("svg:"), 4);
                }
                ost << name;
                first = false;
            }
        }
        row[g_info_columns.value] = Glib::Markup::escape_text(ost.str());
    }
}

void add_styles(Glib::RefPtr<Gtk::ListStore> info_store, const std::unordered_map<std::string, size_t>& map) {
    std::vector<std::string> vect;
    vect.reserve(map.size());
    for (auto style : map) {
        vect.emplace_back(style.first);
    }
    std::sort(vect.begin(), vect.end());
    info_store->freeze_notify();
    int n = 1;
    for (auto& style : vect) {
        auto row = *info_store->append();
        row[g_info_columns.item] = _("Style ") + std::to_string(n++);
        row[g_info_columns.count] = map.find(style)->second;
        row[g_info_columns.value] = Glib::Markup::escape_text(style);
    }
    info_store->thaw_notify();
}

void add_refs(Glib::RefPtr<Gtk::ListStore> info_store, const std::vector<SPObject*>& objects) {
    info_store->freeze_notify();
    for (auto& obj : objects) {
        auto href = Inkscape::getHrefAttribute(*obj->getRepr()).second;
        if (!href) continue;

        auto row = *info_store->append();
        row[g_info_columns.item] = label_fmt(nullptr, get_id(obj));
        row[g_info_columns.value] = href;
        row[g_info_columns.object] = obj;
    }
    info_store->thaw_notify();
}

void DocumentResources::select_page(const Glib::ustring& id) {
    if (_cur_page_id == id.raw()) return;

    _cur_page_id = id;
    refresh_page(id);
}

void DocumentResources::clear_stores() {
    _item_store->freeze_notify();
    _item_store->clear();
    _item_store->thaw_notify();

    _info_store->freeze_notify();
    _info_store->clear();
    _info_store->thaw_notify();
}

void DocumentResources::refresh_page(const Glib::ustring& id) {
    auto rsrc = id_to_resource(id);

    // GTK spits out a lot of warnings and errors from filtered model.
    // I don't know how to fix them.
    // https://gitlab.gnome.org/GNOME/gtk/-/issues/1150
    // Clear sorting? Remove filtering?
    // GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID

    clear_stores();

    auto root = _document ? _document->getRoot() : nullptr;
    auto defs = _document ? _document->getDefs() : nullptr;

    int device_scale = get_scale_factor();
    auto tab = "iconview";
    auto has_count = false;
    auto item_width = 90;
    auto const color = get_color();
    auto label_editable = false;
    auto items_selectable = true;
    auto can_delete = false; // enable where supported
    auto can_extract = false;

    switch (rsrc) {
    case Colors:
        add_colors(_item_store, collect_colors(root), device_scale);
        item_width = 70;
        items_selectable = false; // to do: make selectable?
        can_extract = true;
        break;

    case Symbols:
        {
            auto opt = object_renderer::options();
            if (auto const window = dynamic_cast<Gtk::Window *>(get_root());
                INKSCAPE.themecontext->isCurrentThemeDark(window))
            {
                // white background for typically black symbols, so they don't disappear in a dark theme
                opt.solid_background(0xf0f0f0ff, 3, 3);
            }
            opt.symbol_style_from_use();
            add_items_with_images(_item_store, collect_items<SPSymbol>(defs), 70, 60, device_scale, true, opt);
        }
        label_editable = true;
        can_delete = true;
        break;

    case Patterns:
        add_items_with_images(_item_store, collect_items<SPPattern>(defs), 80, 70, device_scale);
        label_editable = true;
        can_delete = true;
        break;

    case Markers:
        add_items_with_images(_item_store, collect_items<SPMarker>(defs), 70, 60, device_scale, false,
            object_renderer::options().foreground(color));
        label_editable = true;
        can_delete = true;
        break;

    case Gradients:
        add_items_with_images(_item_store,
            collect_items<SPGradient>(defs, [](auto& g){ return filter_element(g) && !g.isSwatch(); }),
            180, 22, device_scale);
        label_editable = true;
        can_delete = true;
        break;

    case Swatches:
        add_items_with_images(_item_store,
            collect_items<SPGradient>(defs, [](auto& g){ return filter_element(g) && g.isSwatch(); }),
            100, 22, device_scale);
        label_editable = true;
        can_delete = true;
        break;

    case Fonts:
        add_fonts(_info_store, collect_fontspecs(root));
        tab = "treeview";
        items_selectable = false;
        break;

    case Filters:
        add_filters(_info_store, collect_items<SPFilter>(defs));
        label_editable = true;
        tab = "treeview";
        items_selectable = false; // to do: make selectable
        break;

    case Styles:
        add_styles(_info_store, collect_styles(root));
        tab = "treeview";
        has_count = true;
        items_selectable = false; // to do: make selectable?
        break;

    case Images:
        add_items_with_images(_item_store, collect_items<SPImage>(root), 110, 110, device_scale);
        label_editable = true;
        can_extract = true;
        can_delete = true;
        break;

    case External:
        add_refs(_info_store, collect_items<SPObject>(root, [](auto& obj){ return has_external_ref(obj); }));
        tab = "treeview";
        items_selectable = false; // to do: make selectable
        break;

    case Stats:
        add_stats(_info_store, _document, _stats);
        tab = "treeview";
        items_selectable = false;
        break;

    case Metadata:
        add_metadata(_info_store, _document, _rdf_list);
        tab = "treeview";
        items_selectable = false;
        break;
    }

    _showing_resource = rsrc;

    _treeview.get_column(1)->set_visible(has_count);
    _label_renderer->property_editable() = label_editable;
    _edit   .set_visible(label_editable);
    _select .set_visible(items_selectable);
    _delete .set_visible(can_delete);
    _extract.set_visible(can_extract);

    _iconview.set_item_width(item_width);
    get_widget<Gtk::Stack>(_builder, "stack").set_visible_child(tab);
    update_buttons();
}

void DocumentResources::start_editing(Gtk::CellEditable* cell, const Glib::ustring& path) {
    auto entry = dynamic_cast<Gtk::Entry*>(cell);
    entry->set_has_frame();
}

void DocumentResources::end_editing(const Glib::ustring& path, const Glib::ustring& new_text) {
    auto model = _iconview.get_model();
    Gtk::TreeModel::Row row = *model->get_iter(path);
    if (!row) return;

    SPObject* object = row[g_item_columns.object];
    if (!object) {
        g_warning("Missing object ptr, cannot edit object's name.");
        return;
    }

    // try object-specific edit functions first; if not present fall back to generic
    auto getter = g_get_label[typeid(*object)];
    auto setter = g_set_label[typeid(*object)];
    if (!getter || !setter) {
        getter = g_get_label[typeid(SPObject)];
        setter = g_set_label[typeid(SPObject)];
    }

    auto name = getter(*object);
    if (new_text == name) return;

    setter(*object, new_text);

    auto id = get_id(object);
    row[g_item_columns.label] = label_fmt(new_text.c_str(), id);

    if (auto document = object->document) {
        DocumentUndo::done(document, _("Edit object title"), INKSCAPE_ICON("document-resources"));
    }
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
