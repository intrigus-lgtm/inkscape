// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * Symbol, marker, pattern, gradient renderer
 *
 * Copyright (C) 2023 Michael Kowalski
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "object-renderer.h"
#include <cairo.h>
#include <cairomm/enums.h>
#include <cairomm/pattern.h>
#include <cairomm/surface.h>
#include <gdkmm/rgba.h>
#include <glibmm/ustring.h>
#include <optional>
#include "color.h"
#include "display/cairo-utils.h"
#include "document.h"
#include "gradient-chemistry.h"
#include "object/sp-gradient.h"
#include "object/sp-image.h"
#include "object/sp-marker.h"
#include "object/sp-object.h"
#include "object/sp-pattern.h"
#include "object/sp-use.h"
#include "ui/svg-renderer.h"
#include "ui/widget/stroke-style.h"
#include "xml/node.h"
#include "object/sp-defs.h"
#include "object/sp-item.h"
#include "object/sp-root.h"
#include "object/sp-symbol.h"
#include "pattern-manager.h"
#include "display/drawing.h"
#include "util/scope_exit.h"
#include "ui/cache/svg_preview_cache.h"
#include "xml/href-attribute-helper.h"

namespace Inkscape {

// traverse nodes starting from given 'object' until visitor returns object that evaluates to true
template<typename V>
bool visit_until(SPObject& object, V&& visitor) {
    if (visitor(object)) return true;

    // SPUse inserts referenced object as a child; skip it
    if (is<SPUse>(&object)) return false;

    for (auto&& child : object.children) {
        if (visit_until(child, visitor)) return true;
    }

    return false;
}

const char* style_from_use_element(const char* id, SPDocument* document) {
    if (!id || !*id || !document) return nullptr;

    auto root = document->getRoot();
    if (!root) return nullptr;

    const char* style = nullptr;
    Glib::ustring ident = "#";
    ident += id;

    visit_until(*root, [&](SPObject& obj){
        if (auto use = cast<SPUse>(&obj)) {
            if (auto href = Inkscape::getHrefAttribute(*use->getRepr()).second) {
                if (ident == href) {
                    // style = use->getRepr()->attribute("style");
                    style = use->getAttribute("style");
                    return true;
                }
            }
        }
        return false;
    });

    return style;
}


SPDocument* symbols_preview_doc() {
    auto buffer = R"A(
<svg xmlns="http://www.w3.org/2000/svg"
    xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"
    xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"
    xmlns:xlink="http://www.w3.org/1999/xlink">
  <use id="the_use" xlink:href="#the_symbol"/>
</svg>
)A";
    return SPDocument::createNewDocFromMem(buffer, strlen(buffer), false);
}

Cairo::RefPtr<Cairo::Surface> draw_symbol(SPObject& symbol, double box_w, double box_h, double device_scale, SPDocument* preview_document, bool style_from_use) {
    // Create a copy repr of the symbol with id="the_symbol"
    Inkscape::XML::Node* repr = symbol.getRepr()->duplicate(preview_document->getReprDoc());
    repr->setAttribute("id", "the_symbol");

    // First look for default style stored in <symbol>
    auto style = repr->attribute("inkscape:symbol-style");
    if (!style) {
        // If no default style in <symbol>, look in documents.

        // Read style from <use> element pointing to this symbol?
        if (style_from_use) {
            // When symbols are inserted from a set into a new document, styles they may rely on
            // are copied from original document and applied to the <use> symbol.
            // We need to use those styles to render symbols correctly, because some symbols only
            // define geometry and no presentation attributes and defaults (black fill, no stroke)
            // may be completely incorrect (for instance originals may have no fill and stroke).
            auto id = symbol.getId();
            style = style_from_use_element(id, symbol.document);
        }
        else {
            style = symbol.document->getReprRoot()->attribute("style");
        }
    }

    // This is for display in Symbols dialog only
    if (style) repr->setAttribute("style", style);

    // reach out to the document for CSS styles, in case symbol uses some class selectors
    SPDocument::install_reference_document scoped(preview_document, symbol.document);

    preview_document->getDefs()->getRepr()->appendChild(repr);
    Inkscape::GC::release(repr);

    // Uncomment this to get the preview_document documents saved (useful for debugging)
    // FILE *fp = fopen (g_strconcat(id, ".svg", NULL), "w");
    // sp_repr_save_stream(preview_document->getReprDoc(), fp);
    // fclose (fp);

    // Make sure preview_document is up-to-date.
    preview_document->ensureUpToDate();

    unsigned dkey = SPItem::display_key_new(1);
    Inkscape::Drawing drawing; // New drawing for offscreen rendering.
    drawing.setRoot(preview_document->getRoot()->invoke_show(drawing, dkey, SP_ITEM_SHOW_DISPLAY));
    auto invoke_hide_guard = scope_exit([&] { preview_document->getRoot()->invoke_hide(dkey); });
    // drawing.root()->setTransform(affine);
    drawing.setExact(); // Maximum quality for blurs.

    // Make sure we have symbol in preview_document
    SPObject* object_temp = preview_document->getObjectById("the_use");

    auto item = cast<SPItem>(object_temp);
    g_assert(item != nullptr);

    // We could use cache here, but it doesn't really work with the structure
    // of this user interface and we've already cached the pixbuf in the gtklist
    cairo_surface_t* s = nullptr;
    // Find object's bbox in document.
    // Note symbols can have own viewport... ignore for now.
    Geom::OptRect dbox = item->documentVisualBounds();

    if (dbox) {
        double width  = dbox->width();
        double height = dbox->height();

        if (width == 0.0) width = 1.0;
        if (height == 0.0) height = 1.0;

        auto scale = std::min(box_w / width, box_h / height);
        if (scale > 1.0) {
            scale = 1.0;
        }

        s = render_surface(drawing, scale, *dbox, Geom::IntPoint(box_w, box_h), device_scale, nullptr, true);
    }

    preview_document->getObjectByRepr(repr)->deleteObject(false);

    if (s) {
        cairo_surface_set_device_scale(s, device_scale, device_scale);
    }

    return Cairo::RefPtr<Cairo::Surface>(new Cairo::Surface(s, true));
}

void draw_gradient(const Cairo::RefPtr<Cairo::Context>& cr, SPGradient* gradient, int x, int width) {
    cairo_pattern_t* check = ink_cairo_pattern_create_checkerboard();

    cairo_set_source(cr->cobj(), check);
    cr->fill_preserve();
    cairo_pattern_destroy(check);

    if (gradient) {
        auto p = gradient->create_preview_pattern(width);
        if (!p) {
            return;
        }
        cairo_matrix_t m;
        cairo_matrix_init_translate(&m, -x, 0);
        cairo_pattern_set_matrix(p, &m);
        cairo_set_source(cr->cobj(), p);
        cr->fill();
        cairo_pattern_destroy(p);
    }
}

Cairo::RefPtr<Cairo::Surface> draw_gradient(SPGradient* gradient, double width, double height, double device_scale, bool stops) {
    auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, width * device_scale, height * device_scale);
    cairo_surface_set_device_scale(surface->cobj(), device_scale, device_scale);
    auto ctx = Cairo::Context::create(surface);

    auto h = stops ? height / 2 : height;
    auto x = 0.5 * device_scale;
    auto y = 0.5 * device_scale;
    width -= device_scale;
    h -= device_scale;

    ctx->rectangle(x, y, width, h);
    draw_gradient(ctx, gradient, 0, width);

    // border
    ctx->rectangle(x, y, width, h);
    ctx->set_source_rgb(0.5, 0.5, 0.5);
    ctx->set_line_width(1.0);
    ctx->stroke();

    if (stops) {
        double radius = 3;
        auto v = gradient->getVector();
        for (auto& stop : v->vector.stops) {
            double py = h + 2 * radius;
            double px = std::round(stop.offset * width);
            ctx->arc(px, py, radius, 0, 2 * M_PI);
            ctx->set_source_rgba(stop.color.v.c[0], stop.color.v.c[1], stop.color.v.c[2], stop.opacity);
            ctx->fill_preserve();
            ctx->set_source_rgb(0.5, 0.5, 0.5);
            ctx->stroke();
        }
    }

    return surface;
}


std::unique_ptr<SPDocument> ink_markers_preview_doc(const Glib::ustring& group_id)
{
gchar const *buffer = R"A(
    <svg xmlns="http://www.w3.org/2000/svg"
         xmlns:xlink="http://www.w3.org/1999/xlink"
         id="MarkerSample">

    <defs id="defs">
      <filter id="softGlow" height="1.2" width="1.2" x="0.0" y="0.0">
      <!-- <feMorphology operator="dilate" radius="1" in="SourceAlpha" result="thicken" id="feMorphology2" /> -->
      <!-- Use a gaussian blur to create the soft blurriness of the glow -->
      <feGaussianBlur in="SourceAlpha" stdDeviation="3" result="blurred" id="feGaussianBlur4" />
      <!-- Change the color -->
      <feFlood flood-color="rgb(255,255,255)" result="glowColor" id="feFlood6" flood-opacity="0.70" />
      <!-- Color in the glows -->
      <feComposite in="glowColor" in2="blurred" operator="in" result="softGlow_colored" id="feComposite8" />
      <!--	Layer the effects together -->
      <feMerge id="feMerge14">
        <feMergeNode in="softGlow_colored" id="feMergeNode10" />
        <feMergeNode in="SourceGraphic" id="feMergeNode12" />
      </feMerge>
      </filter>
    </defs>

    <!-- cross at the end of the line to help position marker -->
    <symbol id="cross" width="25" height="25" viewBox="0 0 25 25">
      <path class="cross" style="mix-blend-mode:difference;stroke:#7ff;stroke-opacity:1;fill:none;display:block" d="M 0,0 M 25,25 M 10,10 15,15 M 10,15 15,10" />
      <!-- <path class="cross" style="mix-blend-mode:difference;stroke:#7ff;stroke-width:1;stroke-opacity:1;fill:none;display:block;-inkscape-stroke:hairline" d="M 0,0 M 25,25 M 10,10 15,15 M 10,15 15,10" /> -->
    </symbol>

    <!-- very short path with 1px stroke used to measure size of marker -->
    <path id="measure-marker" style="stroke-width:1.0;stroke-opacity:0.01;marker-start:url(#sample)" d="M 0,9999 m 0,0.1" />

    <path id="line-marker-start" class="line colors" style="stroke-width:2;stroke-opacity:0.2" d="M 12.5,12.5 l 1000,0" />
    <!-- <g id="marker-start" class="group" style="filter:url(#softGlow)"> -->
    <g id="marker-start" class="group">
      <path class="colors" style="stroke-width:2;stroke-opacity:0;marker-start:url(#sample)"
       d="M 12.5,12.5 L 25,12.5"/>
      <rect x="0" y="0" width="25" height="25" style="fill:none;stroke:none"/>
      <use xlink:href="#cross" width="25" height="25" />
    </g>

    <path id="line-marker-mid" class="line colors" style="stroke-width:2;stroke-opacity:0.2" d="M -1000,12.5 L 1000,12.5" />
    <g id="marker-mid" class="group">
      <path class="colors" style="stroke-width:2;stroke-opacity:0;marker-mid:url(#sample)"
       d="M 0,12.5 L 12.5,12.5 L 25,12.5"/>
      <rect x="0" y="0" width="25" height="25" style="fill:none;stroke:none"/>
      <use xlink:href="#cross" width="25" height="25" />
    </g>

    <path id="line-marker-end" class="line colors" style="stroke-width:2;stroke-opacity:0.2" d="M -1000,12.5 L 12.5,12.5" />
    <g id="marker-end" class="group">
      <path class="colors" style="stroke-width:2;stroke-opacity:0;marker-end:url(#sample)"
       d="M 0,12.5 L 12.5,12.5"/>
      <rect x="0" y="0" width="25" height="25" style="fill:none;stroke:none"/>
      <use xlink:href="#cross" width="25" height="25" />
    </g>

  </svg>
)A";

    auto document = std::unique_ptr<SPDocument>(SPDocument::createNewDocFromMem(buffer, strlen(buffer), false));
    // only leave requested group, so nothing else gets rendered
    for (auto&& group : document->getObjectsByClass("group")) {
        assert(group->getId());
        if (group->getId() != group_id) {
            group->deleteObject();
        }
    }
    auto id = "line-" + group_id;
    for (auto&& line : document->getObjectsByClass("line")) {
        assert(line->getId());
        if (line->getId() != id) {
            line->deleteObject();
        }
    }
    return document;
}


Cairo::RefPtr<Cairo::Surface> create_marker_image(
    const Glib::ustring& group_id,
    SPDocument* _sandbox,
    Gdk::RGBA marker_color,
    Geom::IntPoint pixel_size,
    const char* mname,
    SPDocument* source,
    Inkscape::Drawing& drawing,
    std::optional<guint32> checkerboard,
    bool no_clip,
    double scale,
    int device_scale)
{
    Cairo::RefPtr<Cairo::Surface> g_bad_marker;

    // Retrieve the marker named 'mname' from the source SVG document
    const SPObject* marker = source ? source->getObjectById(mname) : nullptr;
    if (marker == nullptr) {
        g_warning("bad mname: %s", mname);
        return g_bad_marker;
    }

    SPObject *oldmarker = _sandbox->getObjectById("sample");
    if (oldmarker) {
        oldmarker->deleteObject(false);
    }

    // Create a copy repr of the marker with id="sample"
    Inkscape::XML::Document *xml_doc = _sandbox->getReprDoc();
    Inkscape::XML::Node *mrepr = marker->getRepr()->duplicate(xml_doc);
    mrepr->setAttribute("id", "sample");

    // Replace the old sample in the sandbox by the new one
    Inkscape::XML::Node *defsrepr = _sandbox->getObjectById("defs")->getRepr();

    // TODO - This causes a SIGTRAP on windows
    defsrepr->appendChild(mrepr);

    Inkscape::GC::release(mrepr);

    // If the marker color is a url link to a pattern or gradient copy that too
    SPObject *mk = source->getObjectById(mname);
    SPCSSAttr *css_marker = sp_css_attr_from_object(mk->firstChild(), SP_STYLE_FLAG_ALWAYS);
    //const char *mfill = sp_repr_css_property(css_marker, "fill", "none");
    const char *mstroke = sp_repr_css_property(css_marker, "fill", "none");

    if (!strncmp (mstroke, "url(", 4)) {
        SPObject *linkObj = getMarkerObj(mstroke, source);
        if (linkObj) {
            Inkscape::XML::Node *grepr = linkObj->getRepr()->duplicate(xml_doc);
            SPObject *oldmarker = _sandbox->getObjectById(linkObj->getId());
            if (oldmarker) {
                oldmarker->deleteObject(false);
            }
            defsrepr->appendChild(grepr);
            Inkscape::GC::release(grepr);

            if (is<SPGradient>(linkObj)) {
                SPGradient *vector = sp_gradient_get_forked_vector_if_necessary(cast<SPGradient>(linkObj), false);
                if (vector) {
                    Inkscape::XML::Node *grepr = vector->getRepr()->duplicate(xml_doc);
                    SPObject *oldmarker = _sandbox->getObjectById(vector->getId());
                    if (oldmarker) {
                        oldmarker->deleteObject(false);
                    }
                    defsrepr->appendChild(grepr);
                    Inkscape::GC::release(grepr);
                }
            }
        }
    }

// Uncomment this to get the sandbox documents saved (useful for debugging)
    // FILE *fp = fopen (g_strconcat(combo_id, mname, ".svg", nullptr), "w");
    // sp_repr_save_stream(_sandbox->getReprDoc(), fp);
    // fclose (fp);

    // object to render; note that the id is the same as that of the combo we're building
    SPObject *object = _sandbox->getObjectById(group_id);

    if (object == nullptr || !is<SPItem>(object)) {
        g_warning("no obj: %s", group_id.c_str());
        return g_bad_marker;
    }

    Gdk::RGBA fg = marker_color;
    auto fgcolor = rgba_to_css_color(fg);
    fg.set_red(1 - fg.get_red());
    fg.set_green(1 - fg.get_green());
    fg.set_blue(1 - fg.get_blue());
    auto bgcolor = rgba_to_css_color(fg);
    auto objects = _sandbox->getObjectsBySelector(".colors");
    for (auto el : objects) {
        if (SPCSSAttr* css = sp_repr_css_attr(el->getRepr(), "style")) {
            sp_repr_css_set_property(css, "fill", bgcolor.c_str());
            sp_repr_css_set_property(css, "stroke", fgcolor.c_str());
            el->changeCSS(css, "style");
            sp_repr_css_attr_unref(css);
        }
    }

    auto cross = _sandbox->getObjectsBySelector(".cross");
    double stroke = 0.5;
    for (auto el : cross) {
        if (SPCSSAttr* css = sp_repr_css_attr(el->getRepr(), "style")) {
            sp_repr_css_set_property(css, "display", checkerboard ? "block" : "none");
            sp_repr_css_set_property_double(css, "stroke-width", stroke);
            el->changeCSS(css, "style");
            sp_repr_css_attr_unref(css);
        }
    }

    // SPDocument::install_reference_document scoped(_sandbox, source);

    _sandbox->getRoot()->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
    _sandbox->ensureUpToDate();

    auto item = cast<SPItem>(object);
    // Find object's bbox in document
    Geom::OptRect dbox = item->documentVisualBounds();

    if (!dbox) {
        g_warning("no dbox");
        return g_bad_marker;
    }

    if (auto measure = cast<SPItem>(_sandbox->getObjectById("measure-marker"))) {
        if (auto box = measure->documentVisualBounds()) {
            // check size of the marker applied to a path with stroke of 1px
            auto size = std::max(box->width(), box->height());
            const double small = 5.0;
            // if too small, then scale up; clip needs to be enabled for scale to work
            if (size > 0 && size < small) {
                auto factor = 1 + small - size;
                scale *= factor;
                no_clip = false;

                // adjust cross stroke
                stroke /= factor;
                for (auto el : cross) {
                    if (SPCSSAttr* css = sp_repr_css_attr(el->getRepr(), "style")) {
                        sp_repr_css_set_property_double(css, "stroke-width", stroke);
                        el->changeCSS(css, "style");
                        sp_repr_css_attr_unref(css);
                    }
                }

                _sandbox->getRoot()->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
                _sandbox->ensureUpToDate();
            }
        }
    }

    /* Update to renderable state */
    // const double device_scale = get_scale_factor();
    guint32 bgnd_color = checkerboard.has_value() ? *checkerboard : 0;
    auto surface = render_surface(drawing, scale, *dbox, pixel_size, device_scale, checkerboard.has_value() ? &bgnd_color : nullptr, no_clip);
    cairo_surface_set_device_scale(surface, device_scale, device_scale);
    return Cairo::RefPtr<Cairo::Surface>(new Cairo::Surface(surface, true));
}

Cairo::RefPtr<Cairo::Surface> render_image(const Inkscape::Pixbuf* pixbuf, int width, int height, int device_scale) {
    Cairo::RefPtr<Cairo::Surface> surface;

    if (!pixbuf || width <= 0 || height <= 0 || pixbuf->width() <= 0 || pixbuf->height() <= 0) return surface;

    auto src = Cairo::RefPtr<Cairo::Surface>(new Cairo::Surface(pixbuf->getSurfaceRaw(), false));
    surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, width * device_scale, height * device_scale);
    cairo_surface_set_device_scale(surface->cobj(), device_scale, device_scale);

    auto ctx = Cairo::Context::create(surface);

    double sw = pixbuf->width();
    double sh = pixbuf->height();
    double sx = sw / width;
    double sy = sh / height;
    auto scale = 1.0 / std::max(sx, sy);
    double dx = width - scale * sw;
    double dy = height - scale * sh;

    ctx->translate(dx / 2, dy / 2);
    ctx->scale(scale, scale);
    ctx->set_source(src, 0, 0);
    ctx->set_operator(Cairo::Context::Operator::OVER);
    ctx->paint();

    return surface;
}

Cairo::RefPtr<Cairo::Surface> add_background_to_image(Cairo::RefPtr<Cairo::Surface> image, uint32_t rgb, double margin, double radius, int device_scale, std::optional<uint32_t> border) {
    auto w = image ? cairo_image_surface_get_width(image->cobj()) : 0;
    auto h = image ? cairo_image_surface_get_height(image->cobj()) : 0;
    auto width =  w / device_scale + 2 * margin;
    auto height = h / device_scale + 2 * margin;

    auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, width * device_scale, height * device_scale);
    cairo_surface_set_device_scale(surface->cobj(), device_scale, device_scale);
    auto ctx = Cairo::Context::create(surface);

    auto x = 0;
    auto y = 0;
    if (border.has_value()) {
        x += 0.5 * device_scale;
        y += 0.5 * device_scale;
        width -= device_scale;
        height -= device_scale;
    }
    ctx->arc(x + width - radius, y + radius, radius, -M_PI_2, 0);
    ctx->arc(x + width - radius, y + height - radius, radius, 0, M_PI_2);
    ctx->arc(x + radius, y + height - radius, radius, M_PI_2, M_PI);
    ctx->arc(x + radius, y + radius, radius, M_PI, 3 * M_PI_2);
    ctx->close_path();

    ctx->set_source_rgb(SP_RGBA32_R_F(rgb), SP_RGBA32_G_F(rgb), SP_RGBA32_B_F(rgb));
    if (border.has_value()) {
        ctx->fill_preserve();

        auto b = *border;
        ctx->set_source_rgb(SP_RGBA32_R_F(b), SP_RGBA32_G_F(b), SP_RGBA32_B_F(b));
        ctx->set_line_width(1.0);
        ctx->stroke();
    }
    else {
        ctx->fill();
    }

    if (image) {
        ctx->set_source(image, margin, margin);
        ctx->paint();
    }

    return surface;
}

Cairo::RefPtr<Cairo::Surface> draw_frame(Cairo::RefPtr<Cairo::Surface> image, double image_alpha, uint32_t frame_rgba, double thickness, std::optional<uint32_t> checkerboard_color, int device_scale) {
    if (!image) return image;

    auto w = cairo_image_surface_get_width(image->cobj());
    auto h = cairo_image_surface_get_height(image->cobj());
    auto width =  w / device_scale + 2 * thickness;
    auto height = h / device_scale + 2 * thickness;

    auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, width * device_scale, height * device_scale);
    cairo_surface_set_device_scale(surface->cobj(), device_scale, device_scale);
    auto ctx = Cairo::Context::create(surface);

    if (checkerboard_color) {
        Cairo::RefPtr<Cairo::Pattern> pattern(new Cairo::Pattern(ink_cairo_pattern_create_checkerboard(*checkerboard_color)));
        ctx->save();
        ctx->set_operator(Cairo::Context::Operator::SOURCE);
        ctx->set_source(pattern);
        ctx->rectangle(thickness, thickness, width - 2*thickness, height - 2*thickness);
        ctx->fill();
        ctx->restore();
    }

    ctx->rectangle(thickness / 2, thickness / 2, width - thickness, height - thickness);

    if (thickness > 0) {
        ctx->set_source_rgba(SP_RGBA32_R_F(frame_rgba), SP_RGBA32_G_F(frame_rgba), SP_RGBA32_B_F(frame_rgba), SP_RGBA32_A_F(frame_rgba));
        ctx->set_line_width(thickness);
        ctx->stroke();
    }

    ctx->set_source(image, thickness, thickness);
    ctx->paint_with_alpha(image_alpha);

    return surface;
}


object_renderer:: object_renderer() {
}

Cairo::RefPtr<Cairo::Surface> object_renderer::render(SPObject& object, double width, double height, double device_scale, object_renderer::options opt) {

    Cairo::RefPtr<Cairo::Surface> surface;
    if (opt._draw_frame) {
        width -= 2 * opt._stroke;
        height -= 2 * opt._stroke;
    }
    if (width <= 0 || height <= 0) return surface;

    if (is<SPSymbol>(&object)) {
        if (!_symbol_document) {
            _symbol_document.reset(symbols_preview_doc());
        }
        surface = draw_symbol(object, width, height, device_scale, _symbol_document.get(), opt._symbol_style_from_use);
    }
    else if (is<SPMarker>(&object)) {
        const auto group = "marker-mid";
        if (!_sandbox) {
            _sandbox = ink_markers_preview_doc(group);
        }
        std::optional<guint32> checkerboard; // rgb background color
        bool no_clip = true;
        double scale = 1.0;

        unsigned const dkey = SPItem::display_key_new(1);
        Inkscape::Drawing drawing; // New drawing for offscreen rendering.
        drawing.setRoot(_sandbox->getRoot()->invoke_show(drawing, dkey, SP_ITEM_SHOW_DISPLAY));
        auto invoke_hide_guard = scope_exit([&] { _sandbox->getRoot()->invoke_hide(dkey); });
        drawing.setExact(); // Maximum quality for blurs.

        surface = create_marker_image(group, _sandbox.get(), opt._foreground, Geom::IntPoint(width, height), object.getId(),
            object.document, drawing, checkerboard, no_clip, scale, device_scale);
    }
    else if (is<SPGradient>(&object)) {
        surface = draw_gradient(cast<SPGradient>(&object), width, height, device_scale, false);
    }
    else if (auto pattern = cast<SPPattern>(&object)) {
        surface = PatternManager::get().get_image(pattern, width, height, device_scale);
    }
    else if (auto image = cast<SPImage>(&object)) {
        surface = render_image(image->pixbuf.get(), width, height, device_scale);
    }
    else {
        g_warning("object_renderer: don't know how to render this object type");
    }

    if (opt._add_background) {
        surface = add_background_to_image(surface, opt._background, opt._margin, opt._radius, device_scale);
    }

    // extra decorators: frame, opacity change, checkerboard background
    if (opt._draw_frame || opt._image_opacity != 1 || opt._checkerboard.has_value()) {
        surface = draw_frame(surface, opt._image_opacity, opt._frame_rgba, opt._stroke, opt._checkerboard, device_scale);
    }

    return surface;
}

} // namespace
