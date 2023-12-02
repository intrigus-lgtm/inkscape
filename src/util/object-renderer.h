// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_OBJECT_RENDERER_H
#define SEEN_OBJECT_RENDERER_H

#include <cairomm/refptr.h>
#include <cairomm/surface.h>
#include <cairomm/context.h>
#include <gdkmm/rgba.h>
#include <glibmm/ustring.h>
#include <memory>
#include <optional>
#include "display/drawing.h"
#include "object/sp-object.h"
#include "document.h"

namespace Inkscape {

class object_renderer {
public:
    object_renderer();

    struct options {
        options() {}
        // foreground color, where used
        options& foreground(Gdk::RGBA fg) {
            _foreground = fg;
            return *this;
        }
        // background color, where used
        options& solid_background(uint32_t bg, double margin, double corner_radius = 0) {
            _add_background = true;
            _background = bg;
            _margin = margin;
            _radius = corner_radius;
            return *this;
        }
        // use checkerboard pattern for drawing background
        options& checkerboard(uint32_t color) {
            _checkerboard = color;
            return *this;
        }
        // option to add an outline to rendered image
        options& frame(uint32_t rgba, double thickness = 1) {
            _stroke = thickness;
            _draw_frame = true;
            _frame_rgba = rgba;
            return *this;
        }
        // option to reduce opacity of rendered image
        options& image_opacity(double alpha) {
            _image_opacity = alpha;
            return *this;
        }
        // for symbols only: take style from <use> element
        options& symbol_style_from_use(bool from_use_element = true) {
            _symbol_style_from_use = from_use_element;
            return *this;
        }

    private:
        friend class object_renderer;
        Gdk::RGBA _foreground;
        bool _add_background = false;
        uint32_t _background;
        double _margin = 0;
        double _radius = 0;
        bool _symbol_style_from_use = false;
        bool _draw_frame = false;
        double _stroke = 0;
        uint32_t _frame_rgba = 0;
        double _image_opacity = 1;
        std::optional<uint32_t> _checkerboard;
    };

    Cairo::RefPtr<Cairo::Surface> render(SPObject& object, double width, double height, double device_scale, options options = {});

private:
    std::unique_ptr<SPDocument> _symbol_document;
    std::unique_ptr<SPDocument> _sandbox;
};

// Place 'image' on a solid background with given color optionally adding border.
// If no image is provided, only background surface will be created.
Cairo::RefPtr<Cairo::Surface> add_background_to_image(Cairo::RefPtr<Cairo::Surface> image, uint32_t rgb, double margin, double radius, int device_scale, std::optional<uint32_t> border = std::optional<uint32_t>());

/**
 * Returns a new document containing default start, mid, and end markers.
 * Note 1: group IDs are matched against "group_id" to render correct preview object.
 * Note 2: paths/lines are kept outside of groups, so they don't inflate visible bounds
 * Note 3: invisible rects inside groups keep visual bounds from getting too small, so we can see relative marker sizes
 */
std::unique_ptr<SPDocument> ink_markers_preview_doc(const Glib::ustring& group_id);

/**
 * Creates a copy of the marker named mname, determines its visible and renderable
 * area in the bounding box, and then renders it. This allows us to fill in
 * preview images of each marker in the marker combobox.
 */
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
    int device_scale);

/**
 * Renders a preview of a gradient into the passed context.
 */
void draw_gradient(const Cairo::RefPtr<Cairo::Context>& cr, SPGradient* gradient, int x, int width);


} // namespace

#endif // SEEN_OBJECT_RENDERER_H
