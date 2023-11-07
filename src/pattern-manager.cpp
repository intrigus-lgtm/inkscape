// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtkmm/liststore.h>
#include <glibmm/i18n.h>

#include "pattern-manager.h"
#include "pattern-manipulation.h"
#include "document.h"
#include "manipulation/copy-resource.h"
#include "style.h"
#include "object/sp-pattern.h"
#include "object/sp-defs.h"
#include "object/sp-root.h"
#include "util/statics.h"
#include "util/units.h"
#include "ui/svg-renderer.h"

using Inkscape::UI::Widget::PatternItem;

namespace Inkscape {

// pattern preview for UI list, with light gray background and border
std::shared_ptr<SPDocument> get_preview_document() {
char const* buffer = R"A(
<svg width="40" height="40" viewBox="0 0 40 40"
   xmlns:xlink="http://www.w3.org/1999/xlink"
   xmlns="http://www.w3.org/2000/svg">
  <defs id="defs">
  </defs>
  <g id="layer1">
    <rect
       style="fill:#f0f0f0;fill-opacity:1;stroke:none"
       id="rect2620"
       width="100%" height="100%" x="0" y="0" />
    <rect
       style="fill:url(#sample);fill-opacity:1;stroke:black;stroke-opacity:0.3;stroke-width:1px"
       id="rect236"
       width="100%" height="100%" x="0" y="0" />
  </g>
</svg>
)A";
    return std::shared_ptr<SPDocument>(SPDocument::createNewDocFromMem(buffer, strlen(buffer), false));
}

// pattern preview document without background
std::shared_ptr<SPDocument> get_big_preview_document() {
char const* buffer = R"A(
<svg width="100" height="100"
   xmlns:xlink="http://www.w3.org/1999/xlink"
   xmlns="http://www.w3.org/2000/svg">
  <defs id="defs">
  </defs>
  <g id="layer1">
    <rect
       style="fill:url(#sample);fill-opacity:1;stroke:none"
       width="100%" height="100%" x="0" y="0" />
  </g>
</svg>
)A";
    return std::shared_ptr<SPDocument>(SPDocument::createNewDocFromMem(buffer, strlen(buffer), false));
}

PatternManager& PatternManager::get() {
    struct ConstructiblePatternManager : PatternManager {};
    static auto factory = Inkscape::Util::Static<ConstructiblePatternManager>();
    return factory.get();
}

PatternManager::PatternManager() {
    _preview_doc = get_preview_document();
    if (!_preview_doc.get() || !_preview_doc->getReprDoc()) {
        throw std::runtime_error("Pattern embedded preview document cannot be loaded");
    }

    _big_preview_doc = get_big_preview_document();
    if (!_big_preview_doc.get() || !_big_preview_doc->getReprDoc()) {
        throw std::runtime_error("Pattern embedded big preview document cannot be loaded");
    }

    auto model = Gtk::ListStore::create(columns);

    _documents = sp_get_stock_patterns();
    std::vector<SPPattern*> all;

    for (auto& doc : _documents) {
        auto patterns = sp_get_pattern_list(doc.get());
        all.insert(all.end(), patterns.begin(), patterns.end());

        Glib::ustring name = doc->getDocumentName();
        name = name.substr(0, name.rfind(".svg"));
        auto category = Glib::RefPtr<Category>(new Category(name, std::move(patterns), false));
        _categories.push_back(category);
    }

    for (auto pat : all) {
        // create empty items for stock patterns
        _cache[pat] = Glib::RefPtr<Inkscape::UI::Widget::PatternItem>();
    }

    // special "all patterns" category
    auto all_patterns = Glib::RefPtr<Category>(new Category(_("All patterns"), std::move(all), true));
    _categories.push_back(all_patterns);

    // sort by name, keep "all patterns" category first
    std::sort(_categories.begin(), _categories.end(), [](const Glib::RefPtr<Category>& a, const Glib::RefPtr<Category>& b){
        if (a->all != b->all) {
            return a->all;
        }
        return a->name < b->name;
    });

    for (auto& category : _categories) {
        auto row = *model->append();
        row[columns.name] = category->name;
        row[columns.category] = category;
        row[columns.all_patterns] = category->all;
    }

    _model = model;
}

Cairo::RefPtr<Cairo::Surface> create_pattern_image(std::shared_ptr<SPDocument>& sandbox,
    char const* name, SPDocument* source, double scale,
    std::optional<unsigned int> checkerboard = std::optional<unsigned int>()) {

    // Retrieve the pattern named 'name' from the source SVG document
    SPObject const* pattern = source->getObjectById(name);
    if (pattern == nullptr) {
        g_warning("bad name: %s", name);
        return Cairo::RefPtr<Cairo::Surface>();
    }

    auto list = sandbox->getDefs()->childList(true);
    for (auto obj : list) {
        obj->deleteObject();
        sp_object_unref(obj);
    }

    SPDocument::install_reference_document scoped(sandbox.get(), source);

    // Create a copy of the pattern, name it "sample"
    auto copy = sp_copy_resource(pattern, sandbox.get());
    copy->getRepr()->setAttribute("id", "sample");

    sandbox->getRoot()->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
    sandbox->ensureUpToDate();

    svg_renderer renderer(sandbox);
    if (checkerboard.has_value()) {
        renderer.set_checkerboard_color(*checkerboard);
    }
    auto surface = renderer.render_surface(scale);
    if (surface) {
        cairo_surface_set_device_scale(surface->cobj(), scale, scale);
    }

    // delete sample to relese href to the original pattern, if any has been referenced by 'copy'
    SPObject* oldpattern = sandbox->getObjectById("sample");
    if (oldpattern) {
        oldpattern->deleteObject(false);
    }
    return surface;
}

// given a pattern, create a PatternItem instance that describes it;
// input pattern can be a link or a root pattern
Glib::RefPtr<PatternItem> create_pattern_item(std::shared_ptr<SPDocument>& sandbox, SPPattern* pattern, bool stock_pattern, double scale) {
    if (!pattern) return {};

    auto item = PatternItem::create();

    //  this is a link:       this is a root:
    // <pattern href="abc"/> <pattern id="abc"/>
    // if pattern is a root one to begin with, then both pointers will be the same:
    auto link_pattern = pattern;
    auto root_pattern = pattern->rootPattern();

    // get label and ID from root pattern
    Inkscape::XML::Node* repr = root_pattern->getRepr();
    if (auto id = repr->attribute("id")) {
        item->id = id;
    }
    item->label = sp_get_pattern_label(root_pattern);
    item->stock = stock_pattern;
    // read transformation from a link pattern
    item->transform = link_pattern->get_this_transform();
    item->offset = Geom::Point(link_pattern->x(), link_pattern->y());

    // reading color style form "root" pattern; linked one won't have any effect, as it's not a parrent
    if (root_pattern->style && root_pattern->style->isSet(SPAttr::FILL) && root_pattern->style->fill.isColor()) {
        item->color.emplace(SPColor(root_pattern->style->fill.value.color));
    }
    // uniform scaling?
    if (link_pattern->aspect_set) {
        auto preserve = link_pattern->getAttribute("preserveAspectRatio");
        item->uniform_scale = preserve && strcmp(preserve, "none") != 0;
    }
    // pattern tile gap (only valid for link patterns)
    item->gap = link_pattern != root_pattern ? sp_pattern_get_gap(link_pattern) : Geom::Scale(0, 0);

    if (sandbox) {
        // generate preview
        item->pix = create_pattern_image(sandbox, link_pattern->getId(), link_pattern->document, scale);
    }

    // which collection stock pattern comes from
    item->collection = stock_pattern ? pattern->document : nullptr;

    return item;
}

Cairo::RefPtr<Cairo::Surface> PatternManager::get_image(SPPattern* pattern, int width, int height, double device_scale) {
    if (!pattern) return {};

    _preview_doc->setWidth(Inkscape::Util::Quantity(width, "px"));
    _preview_doc->setHeight(Inkscape::Util::Quantity(height, "px"));
    return create_pattern_image(_preview_doc, pattern->getId(), pattern->document, device_scale);
}

Cairo::RefPtr<Cairo::Surface> PatternManager::get_preview(SPPattern* pattern, int width, int height, unsigned int rgba_background, double device_scale) {
    if (!pattern) return {};

    _big_preview_doc->setWidth(Inkscape::Util::Quantity(width, "px"));
    _big_preview_doc->setHeight(Inkscape::Util::Quantity(height, "px"));

    return create_pattern_image(_big_preview_doc, pattern->getId(), pattern->document, device_scale, rgba_background);
}

Glib::RefPtr<Inkscape::UI::Widget::PatternItem> PatternManager::get_item(SPPattern* pattern) {
    Glib::RefPtr<Inkscape::UI::Widget::PatternItem> item;
    if (!pattern) return item;

    auto it = _cache.find(pattern);
    // if pattern entry was present in the cache, then it is a stock pattern
    bool stock = it != _cache.end();
    if (!stock || !it->second) {
        // generate item
        std::shared_ptr<SPDocument> nil;
        item = create_pattern_item(nil, pattern, stock, 0);

        if (stock) {
            _cache[pattern] = item;
        }
    }
    else {
        item = it->second;
    }

    return item;
}

Glib::RefPtr<Gtk::TreeModel> PatternManager::get_categories() {
    return _model;
}

} // namespace
