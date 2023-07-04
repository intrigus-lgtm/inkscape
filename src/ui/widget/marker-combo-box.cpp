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

#include "marker-combo-box.h"

#include <chrono>
#include <optional>
#include <sstream>
#include <utility>

#include <glibmm/fileutils.h>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/grid.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/window.h>

#include "helper/stock-items.h"
#include "io/resource.h"
#include "object/sp-defs.h"
#include "object/sp-marker.h"
#include "object/sp-root.h"
#include "ui/builder-utils.h"
#include "ui/svg-renderer.h"
#include "ui/util.h"
#include "util/object-renderer.h"

#define noTIMING_INFO 1;

using Inkscape::UI::get_widget;
using Inkscape::UI::create_builder;

// size of marker image in a list
static constexpr int ITEM_WIDTH  = 40;
static constexpr int ITEM_HEIGHT = 32;

namespace Inkscape::UI::Widget {

// separator for FlowBox widget
static cairo_surface_t* create_separator(double alpha, int width, int height, int device_scale) {
    width *= device_scale;
    height *= device_scale;
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* ctx = cairo_create(surface);
    cairo_set_source_rgba(ctx, 0.5, 0.5, 0.5, alpha);
    cairo_move_to(ctx, 0.5, height / 2 + 0.5);
    cairo_line_to(ctx, width + 0.5, height / 2 + 0.5);
    cairo_set_line_width(ctx, 1.0 * device_scale);
    cairo_stroke(ctx);
    cairo_surface_flush(surface);
    cairo_surface_set_device_scale(surface, device_scale, device_scale);
    return surface;
}

// empty image; "no marker"
static Cairo::RefPtr<Cairo::Surface> g_image_none;
// error extracting/rendering marker; "bad marker"
static Cairo::RefPtr<Cairo::Surface> g_bad_marker;

Glib::ustring get_attrib(SPMarker* marker, const char* attrib) {
    auto value = marker->getAttribute(attrib);
    return value ? value : "";
}

double get_attrib_num(SPMarker* marker, const char* attrib) {
    auto val = get_attrib(marker, attrib);
    return strtod(val.c_str(), nullptr);
}

MarkerComboBox::MarkerComboBox(Glib::ustring id, int l) :
    _combo_id(std::move(id)),
    _loc(l),
    _builder(create_builder("marker-popup.glade")),
    _marker_list(get_widget<Gtk::FlowBox>(_builder, "flowbox")),
    _preview(get_widget<Gtk::Image>(_builder, "preview")),
    _marker_name(get_widget<Gtk::Label>(_builder, "marker-id")),
    _link_scale(get_widget<Gtk::Button>(_builder, "link-scale")),
    _scale_x(get_widget<Gtk::SpinButton>(_builder, "scale-x")),
    _scale_y(get_widget<Gtk::SpinButton>(_builder, "scale-y")),
    _scale_with_stroke(get_widget<Gtk::CheckButton>(_builder, "scale-with-stroke")),
    _menu_btn(get_widget<Gtk::MenuButton>(_builder, "menu-btn")),
    _angle_btn(get_widget<Gtk::SpinButton>(_builder, "angle")),
    _offset_x(get_widget<Gtk::SpinButton>(_builder, "offset-x")),
    _offset_y(get_widget<Gtk::SpinButton>(_builder, "offset-y")),
    _input_grid(get_widget<Gtk::Grid>(_builder, "input-grid")),
    _orient_auto_rev(get_widget<Gtk::RadioButton>(_builder, "orient-auto-rev")),
    _orient_auto(get_widget<Gtk::RadioButton>(_builder, "orient-auto")),
    _orient_angle(get_widget<Gtk::RadioButton>(_builder, "orient-angle")),
    _orient_flip_horz(get_widget<Gtk::Button>(_builder, "btn-horz-flip")),
    _current_img(get_widget<Gtk::Image>(_builder, "current-img")),
    _edit_marker(get_widget<Gtk::Button>(_builder, "edit-marker"))
{
    _background_color = 0x808080ff;
    _foreground_color = 0x808080ff;

    if (!g_image_none) {
        auto device_scale = get_scale_factor();
        g_image_none = Cairo::RefPtr<Cairo::Surface>(new Cairo::Surface(create_separator(1, ITEM_WIDTH, ITEM_HEIGHT, device_scale)));
    }

    if (!g_bad_marker) {
        auto path = Inkscape::IO::Resource::get_filename(Inkscape::IO::Resource::UIS, "bad-marker.svg");
        Inkscape::svg_renderer renderer(path.c_str());
        g_bad_marker = renderer.render_surface(1.0);
    }

    prepend(_menu_btn);

    _preview.signal_size_allocate().connect([=](Gtk::Allocation& a){
        // refresh after preview widget has been finally resized/expanded
        if (_preview_no_alloc) update_preview(find_marker_item(get_current()));
    });

    _marker_store = Gio::ListStore<MarkerItem>::create();
    _marker_list.bind_list_store(_marker_store, [=](const Glib::RefPtr<MarkerItem>& item){
        auto const image = Gtk::make_managed<Gtk::Image>(item->pix);
        image->set_visible(true);
        auto const box = Gtk::make_managed<Gtk::FlowBoxChild>();
        box->add(*image);
        if (item->separator) {
            image->set_sensitive(false);
            image->set_can_focus(false);
            image->set_size_request(-1, 10);
            box->set_sensitive(false);
            box->set_can_focus(false);
            box->get_style_context()->add_class("marker-separator");
        }
        else {
            box->get_style_context()->add_class("marker-item-box");
        }
        _widgets_to_markers[image] = item;
        box->set_size_request(item->width, item->height);
        return box;
    });

    _sandbox = Inkscape::ink_markers_preview_doc(_combo_id);

    set_sensitive(true);

    _marker_list.signal_selected_children_changed().connect([=](){
        auto item = get_active();
        if (!item && !_marker_list.get_selected_children().empty()) {
            _marker_list.unselect_all();
        }
    });

    _marker_list.signal_child_activated().connect([=](Gtk::FlowBoxChild* box){
        if (box->get_sensitive()) _signal_changed.emit();
    });

    auto set_orient = [=](bool enable_angle, const char* value) {
        if (_update.pending()) return;
        _angle_btn.set_sensitive(enable_angle);
        sp_marker_set_orient(get_current(), value);
    };
    _orient_auto_rev.signal_toggled().connect([=](){ set_orient(false, "auto-start-reverse"); });
    _orient_auto.signal_toggled().connect([=]()    { set_orient(false, "auto"); });
    _orient_angle.signal_toggled().connect([=]()   { set_orient(true, _angle_btn.get_text().c_str()); });
    _orient_flip_horz.signal_clicked().connect([=]() { sp_marker_flip_horizontally(get_current()); });

    _angle_btn.signal_value_changed().connect([=]() {
        if (_update.pending() || !_angle_btn.is_sensitive()) return;
        sp_marker_set_orient(get_current(), _angle_btn.get_text().c_str());
    });

    auto set_scale = [=](bool changeWidth) {
        if (_update.pending()) return;

        if (auto marker = get_current()) {
            auto sx = _scale_x.get_value();
            auto sy = _scale_y.get_value();
            auto width  = get_attrib_num(marker, "markerWidth");
            auto height = get_attrib_num(marker, "markerHeight");
            if (_scale_linked && width > 0.0 && height > 0.0) {
                auto scoped(_update.block());
                if (changeWidth) {
                    // scale height proportionally
                    sy = height * (sx / width);
                    _scale_y.set_value(sy);
                }
                else {
                    // scale width proportionally
                    sx = width * (sy / height);
                    _scale_x.set_value(sx);
                }
            }
            sp_marker_set_size(marker, sx, sy);
        }
    };

    // delay setting scale to idle time; if invoked by focus change due to new marker selection
    // it leads to marker list rebuild and apparent flowbox content corruption
    auto idle_set_scale = [=](bool changeWidth) {
        if (_update.pending()) return;

        if (auto orig_marker = get_current()) {
            _idle = Glib::signal_idle().connect([=](){
                if (auto marker = get_current()) {
                    if (marker == orig_marker) {
                        set_scale(changeWidth);
                    }
                }
                return false; // don't call again
            });
        }
    };

    _link_scale.signal_clicked().connect([=](){
        if (_update.pending()) return;
        _scale_linked = !_scale_linked;
        sp_marker_set_uniform_scale(get_current(), _scale_linked);
        update_scale_link();
    });

    _scale_x.signal_value_changed().connect([=]() { idle_set_scale(true); });
    _scale_y.signal_value_changed().connect([=]() { idle_set_scale(false); });

    _scale_with_stroke.signal_toggled().connect([=](){
        if (_update.pending()) return;
        sp_marker_scale_with_stroke(get_current(), _scale_with_stroke.get_active());
    });

    auto set_offset = [=](){
        if (_update.pending()) return;
        sp_marker_set_offset(get_current(), _offset_x.get_value(), _offset_y.get_value());
    };
    _offset_x.signal_value_changed().connect([=]() { set_offset(); });
    _offset_y.signal_value_changed().connect([=]() { set_offset(); });

    // request to edit marker on canvas; close popup to get it out of the way and call marker edit tool
    _edit_marker.signal_clicked().connect([this]{ _menu_btn.get_popover()->popdown(); _signal_edit(); });

    // before showing popover refresh marker attributes
    _menu_btn.get_popover()->signal_show().connect([=](){ update_ui(get_current(), false); }, false);

    update_scale_link();
    _current_img.set(g_image_none);
    set_visible(true);
}

void MarkerComboBox::update_widgets_from_marker(SPMarker* marker) {
    _input_grid.set_sensitive(marker != nullptr);

    if (marker) {
        _scale_x.set_value(get_attrib_num(marker, "markerWidth"));
        _scale_y.set_value(get_attrib_num(marker, "markerHeight"));
        auto units = get_attrib(marker, "markerUnits");
        _scale_with_stroke.set_active(units == "strokeWidth" || units == "");
        auto aspect = get_attrib(marker, "preserveAspectRatio");
        _scale_linked = aspect != "none";
        update_scale_link();
    // marker->setAttribute("markerUnits", scale_with_stroke ? "strokeWidth" : "userSpaceOnUse");
        _offset_x.set_value(get_attrib_num(marker, "refX"));
        _offset_y.set_value(get_attrib_num(marker, "refY"));
        auto orient = get_attrib(marker, "orient");

        // try parsing as number
        _angle_btn.set_value(strtod(orient.c_str(), nullptr));
        if (orient == "auto-start-reverse") {
            _orient_auto_rev.set_active();
            _angle_btn.set_sensitive(false);
        }
        else if (orient == "auto") {
            _orient_auto.set_active();
            _angle_btn.set_sensitive(false);
        }
        else {
            _orient_angle.set_active();
            _angle_btn.set_sensitive(true);
        }
    }
}

void MarkerComboBox::update_scale_link() {
    _link_scale.remove();
    _link_scale.add(get_widget<Gtk::Image>(_builder, _scale_linked ? "image-linked" : "image-unlinked"));
}

// update marker image inside the menu button
void MarkerComboBox::update_menu_btn(Glib::RefPtr<MarkerItem> marker) {
    _current_img.set(marker ? marker->pix : g_image_none);
}

// update marker preview image in the popover panel
void MarkerComboBox::update_preview(Glib::RefPtr<MarkerItem> item) {
    Cairo::RefPtr<Cairo::Surface> surface;
    Glib::ustring label;

    if (!item) {
        // TRANSLATORS: None - no marker selected for a path
        label = _("None");
    }

    if (item && item->source && !item->id.empty()) {
        Inkscape::Drawing drawing;
        unsigned const visionkey = SPItem::display_key_new(1);
        drawing.setRoot(_sandbox->getRoot()->invoke_show(drawing, visionkey, SP_ITEM_SHOW_DISPLAY));
        // generate preview
        auto alloc = _preview.get_allocation();
        auto size = Geom::IntPoint(alloc.get_width() - 10, alloc.get_height() - 10);
        if (size.x() > 0 && size.y() > 0) {
            surface = create_marker_image(size, item->id.c_str(), item->source, drawing, visionkey, true, true, 2.60);
        }
        else {
            // too early, preview hasn't been expanded/resized yet
            _preview_no_alloc = true;
        }
        _sandbox->getRoot()->invoke_hide(visionkey);
        label = _(item->label.c_str());
    }

    _preview.set(surface);
    std::ostringstream ost;
    ost << "<small>" << label.raw() << "</small>";
    _marker_name.set_markup(ost.str().c_str());
}

bool MarkerComboBox::MarkerItem::operator == (const MarkerItem& item) const {
    return
        id == item.id &&
        label == item.label &&
        separator == item.separator &&
        stock == item.stock &&
        history == item.history &&
        source == item.source &&
        width == item.width &&
        height == item.height;
}

// find marker object by ID in a document
SPMarker* find_marker(SPDocument* document, const Glib::ustring& marker_id) {
    if (!document) return nullptr;

    SPDefs* defs = document->getDefs();
    if (!defs) return nullptr;

    for (auto& child : defs->children) {
        if (is<SPMarker>(&child)) {
            auto marker = cast<SPMarker>(&child);
            auto id = marker->getId();
            if (id && marker_id == id) {
                // found it
                return marker;
            }
        }
    }

    // not found
    return nullptr;
}

SPMarker* MarkerComboBox::get_current() const {
    // find current marker
    return find_marker(_document, _current_marker_id);
}

void MarkerComboBox::set_active(Glib::RefPtr<MarkerItem> item) {
    bool selected = false;
    if (item) {
        _marker_list.foreach([=,&selected](Gtk::Widget& widget){
            if (auto box = dynamic_cast<Gtk::FlowBoxChild*>(&widget)) {
                if (auto marker = _widgets_to_markers[box->get_child()]) {
                    if (*marker.get() == *item.get()) {
                        _marker_list.select_child(*box);
                        selected = true;
                    }
                }
            }
        });
    }

    if (!selected) {
        _marker_list.unselect_all();
    }
}

Glib::RefPtr<MarkerComboBox::MarkerItem> MarkerComboBox::find_marker_item(SPMarker* marker) {
    std::string id;
    if (marker != nullptr) {
        if (auto markname = marker->getRepr()->attribute("id")) {
            id = markname;
        }
    }

    Glib::RefPtr<MarkerItem> marker_item;
    if (!id.empty()) {
        for (auto&& item : _history_items) {
            if (item->id == id) {
                marker_item = item;
                break;
            }
        }
    }

    return marker_item;
}

Glib::RefPtr<MarkerComboBox::MarkerItem> MarkerComboBox::get_active() {
    auto empty = Glib::RefPtr<MarkerItem>();
    auto sel = _marker_list.get_selected_children();
    if (sel.size() == 1) {
        auto item = _widgets_to_markers[sel.front()->get_child()];
        if (item && item->separator) {
            return empty;
        }
        return item;
    }
    else {
        return empty;
    }
}

void MarkerComboBox::setDocument(SPDocument *document)
{
    if (_document != document) {

        if (_document) {
            modified_connection.disconnect();
        }

        _document = document;

        if (_document) {
            modified_connection = _document->getDefs()->connectModified([=](SPObject*, unsigned int){
                refresh_after_markers_modified();
            });
        }

        _current_marker_id = "";

        refresh_after_markers_modified();
    }
}

/**
 * This function is invoked after document "defs" section changes.
 * It will change when current marker's attributes are modified in this popup
 * and this function will refresh the recent list and a preview to reflect the changes.
 * It would be more efficient if there was a way to determine what has changed
 * and perform only more targeted update.
 */
void MarkerComboBox::refresh_after_markers_modified() {
    if (_update.pending()) return;

    auto scoped(_update.block());

    /*
     * Seems to be no way to get notified of changes just to markers,
     * so listen to changes in all defs and check if the number of markers has changed here
     * to avoid unnecessary refreshes when things like gradients change
    */
   // TODO: detect changes to markers; ignore changes to everything else;
   // simple count check doesn't cut it, so just do it unconditionally for now
    marker_list_from_doc(_document, true);

    auto marker = find_marker_item(get_current());
    update_menu_btn(marker);
    update_preview(marker);
}

Glib::RefPtr<MarkerComboBox::MarkerItem> MarkerComboBox::add_separator(bool filler) {
    auto item = Glib::RefPtr<MarkerItem>(new MarkerItem);
    item->history = false;
    item->separator = true;
    item->id = "None";
    item->label = filler ? "filler" : "Separator";
    item->stock = false;
    if (!filler) {
        auto device_scale = get_scale_factor();
        static Cairo::RefPtr<Cairo::Surface> separator(new Cairo::Surface(create_separator(0.7, ITEM_WIDTH, 10, device_scale)));
        item->pix = separator;
    }
    item->height = 10;
    item->width = -1;
    return item;
}

/**
 * Init the combobox widget to display markers from markers.svg
 */
void
MarkerComboBox::init_combo()
{
    if (_update.pending()) return;

    static SPDocument *markers_doc = nullptr;

    // find and load markers.svg
    if (markers_doc == nullptr) {
        using namespace Inkscape::IO::Resource;
        auto markers_source = get_path_string(SYSTEM, MARKERS, "markers.svg");
        if (Glib::file_test(markers_source, Glib::FileTest::IS_REGULAR)) {
            markers_doc = SPDocument::createNewDoc(markers_source.c_str(), false);
        }
    }

    // load markers from markers.svg
    if (markers_doc) {
        marker_list_from_doc(markers_doc, false);
    }

    refresh_after_markers_modified();
}

/**
 * Sets the current marker in the marker combobox.
 */
void MarkerComboBox::set_current(SPObject *marker)
{
    auto sp_marker = cast<SPMarker>(marker);

    bool reselect = sp_marker != get_current();

    update_ui(sp_marker, reselect);
}

void MarkerComboBox::update_ui(SPMarker* marker, bool select) {
    auto scoped(_update.block());

    auto id = marker ? marker->getId() : nullptr;
    _current_marker_id = id ? id : "";

    auto marker_item = find_marker_item(marker);

    if (select) {
        set_active(marker_item);
    }

    update_widgets_from_marker(marker);
    update_menu_btn(marker_item);
    update_preview(marker_item);
}

/**
 * Return a uri string representing the current selected marker used for setting the marker style in the document
 */
std::string MarkerComboBox::get_active_marker_uri()
{
    /* Get Marker */
    auto item = get_active();
    if (!item) {
        return std::string();
    }

    std::string marker;

    if (item->id != "none") {
        bool stockid = item->stock;

        std::string markurn = stockid ? "urn:inkscape:marker:" + item->id : item->id;
        auto mark = cast<SPMarker>(get_stock_item(markurn.c_str(), stockid));

        if (mark) {
            Inkscape::XML::Node* repr = mark->getRepr();
            auto id = repr->attribute("id");
            if (id) {
                std::ostringstream ost;
                ost << "url(#" << id << ")";
                marker = ost.str();
            }
            if (stockid) {
                mark->getRepr()->setAttribute("inkscape:collect", "always");
            }
            // adjust marker's attributes (or add missing ones) to stay in sync with marker tool
            sp_validate_marker(mark, _document);
        }
    } else {
        marker = item->id;
    }

    return marker;
}

/**
 * Pick up all markers from source and add items to the list/store.
 * If 'history' is true, then update recently used in-document portion of the list;
 * otherwise update list of stock markers, which is displayed after recent ones
 */
void MarkerComboBox::marker_list_from_doc(SPDocument* source, bool history) {
    std::vector<SPMarker*> markers = get_marker_list(source);
    remove_markers(history);
    add_markers(markers, source, history);
    update_store();
}

void MarkerComboBox::update_store() {
    _marker_store->freeze_notify();

    auto selected = get_active();

    _marker_store->remove_all();
    _widgets_to_markers.clear();

    // recent and user-defined markers come first
    for (auto&& item : _history_items) {
        _marker_store->append(item);
    }

    // separator
    if (!_history_items.empty()) {
        // add empty boxes to fill up the row to 'max' elements and then
        // extra ones to create entire new empty row (a separator of sorts)
        auto max = _marker_list.get_max_children_per_line();
        auto fillup = max - _history_items.size() % max;

        for (int i = 0; i < fillup; ++i) {
            _marker_store->append(add_separator(true));
        }
        for (int i = 0; i < max; ++i) {
            _marker_store->append(add_separator(false));
        }
    }

    // stock markers
    for (auto&& item : _stock_items) {
        _marker_store->append(item);
    }

    _marker_store->thaw_notify();

    // reselect current
    set_active(selected);
}
/**
 *  Returns a vector of markers in the defs of the given source document as a vector.
 *  Returns empty vector if there are no markers in the document.
 *  If validate is true then it runs each marker through the validation routine that alters some attributes.
 */
std::vector<SPMarker*> MarkerComboBox::get_marker_list(SPDocument* source)
{
    std::vector<SPMarker *> ml;
    if (source == nullptr) return ml;

    SPDefs *defs = source->getDefs();
    if (!defs) {
        return ml;
    }

    for (auto& child: defs->children) {
        if (is<SPMarker>(&child)) {
            auto marker = cast<SPMarker>(&child);
            ml.push_back(marker);
        }
    }
    return ml;
}

/**
 * Remove history or non-history markers from the combo
 */
void MarkerComboBox::remove_markers (gboolean history)
{
    if (history) {
        _history_items.clear();
    }
    else {
        _stock_items.clear();
    }
}

/**
 * Adds markers in marker_list to the combo
 */
void MarkerComboBox::add_markers (std::vector<SPMarker *> const& marker_list, SPDocument *source, gboolean history)
{
    // Do this here, outside of loop, to speed up preview generation:
    Inkscape::Drawing drawing;
    unsigned const visionkey = SPItem::display_key_new(1);
    drawing.setRoot(_sandbox->getRoot()->invoke_show(drawing, visionkey, SP_ITEM_SHOW_DISPLAY));

    if (history) {
        // add "None"
        auto item = Glib::RefPtr<MarkerItem>(new MarkerItem);
        item->pix = g_image_none;
        item->history = true;
        item->separator = false;
        item->id = "None";
        item->label = "None";
        item->stock = false;
        item->width = ITEM_WIDTH;
        item->height = ITEM_HEIGHT;
        _history_items.push_back(item);
    }

#if TIMING_INFO
auto old_time =  std::chrono::high_resolution_clock::now();
#endif

    for (auto i:marker_list) {

        Inkscape::XML::Node *repr = i->getRepr();
        gchar const *markid = repr->attribute("inkscape:stockid") ? repr->attribute("inkscape:stockid") : repr->attribute("id");

        // generate preview
        auto pixbuf = create_marker_image(Geom::IntPoint(ITEM_WIDTH, ITEM_HEIGHT), repr->attribute("id"), source, drawing, visionkey, false, true, 1.50);

        auto item = Glib::RefPtr<MarkerItem>(new MarkerItem);
        item->source = source;
        item->pix = pixbuf;
        if (auto id = repr->attribute("id")) {
            item->id = id;
        }
        item->label = markid ? markid : "";
        item->stock = !history;
        item->history = history;
        item->width = ITEM_WIDTH;
        item->height = ITEM_HEIGHT;

        if (history) {
            _history_items.emplace_back(std::move(item));
        }
        else {
            _stock_items.emplace_back(std::move(item));
        }
    }

    _sandbox->getRoot()->invoke_hide(visionkey);

#if TIMING_INFO
auto current_time =  std::chrono::high_resolution_clock::now();
auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - old_time);
g_warning("%s render time for %d markers: %d ms", combo_id, (int)marker_list.size(), static_cast<int>(elapsed.count()));
#endif
}

/**
 * Creates a copy of the marker named mname, determines its visible and renderable
 * area in the bounding box, and then renders it. This allows us to fill in
 * preview images of each marker in the marker combobox.
 */
Cairo::RefPtr<Cairo::Surface>
MarkerComboBox::create_marker_image(Geom::IntPoint pixel_size, gchar const *mname,
    SPDocument *source, Inkscape::Drawing &drawing, unsigned /*visionkey*/, bool checkerboard, bool no_clip, double scale)
{
    std::optional<guint32> checkerboard_color;
    if (checkerboard) {
        checkerboard_color = _background_color;
    }

    int device_scale = get_scale_factor();
    auto const fg = get_foreground_color(get_style_context());

    return Inkscape::create_marker_image(_combo_id, _sandbox.get(), fg, pixel_size, mname, source,
        drawing, checkerboard_color, no_clip, scale, device_scale);
}

// capture background color when styles change
void MarkerComboBox::on_style_updated() {
    auto background = _background_color;
    if (auto wnd = dynamic_cast<Gtk::Window*>(this->get_root())) {
        auto sc = wnd->get_style_context();
        auto const color = get_color_with_class(sc, "theme_bg_color");
        background =
            gint32(0xff * color.get_red()) << 24 |
            gint32(0xff * color.get_green()) << 16 |
            gint32(0xff * color.get_blue()) << 8 |
            0xff;
    }

    auto const color = get_foreground_color(get_style_context());
    auto foreground =
        gint32(0xff * color.get_red()) << 24 |
        gint32(0xff * color.get_green()) << 16 |
        gint32(0xff * color.get_blue()) << 8 |
        0xff;
    if (foreground != _foreground_color || background != _background_color) {
        _foreground_color = foreground;
        _background_color = background;
        // theme changed?
        init_combo();
    }
}

sigc::connection MarkerComboBox::connect_changed(sigc::slot<void ()> slot)
{
    return _signal_changed.connect(std::move(slot));
}

sigc::connection MarkerComboBox::connect_edit(sigc::slot<void ()> slot)
{
    return _signal_edit.connect(std::move(slot));
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
