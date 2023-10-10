// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * Image properties widget for "Fill and Stroke" dialog
 *
 * Copyright (C) 2023 Michael Kowalski
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "image-properties.h"

#include <array>
#include <sstream>
#include <string>
#include <cairomm/context.h>
#include <glib/gi18n.h>
#include <glibmm/convert.h>
#include <glibmm/markup.h>
#include <glibmm/ustring.h>
#include <gtkmm/builder.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/entry.h>
#include <gtkmm/enums.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>

#include "display/cairo-utils.h"
#include "document-undo.h"
#include "enums.h"
#include "helper/choose-file.h"
#include "helper/save-image.h"
#include "object/sp-image.h"
#include "ui/builder-utils.h"
#include "ui/icon-names.h"
#include "ui/pack.h"
#include "ui/util.h"
#include "util/format_size.h"
#include "util/object-renderer.h"
#include "xml/href-attribute-helper.h"

namespace Inkscape::UI::Widget {

namespace {

Cairo::RefPtr<Cairo::Surface> draw_preview(SPImage* image, double width, double height, int device_scale, uint32_t frame_color, uint32_t background) {
    if (!image || !image->pixbuf) return Cairo::RefPtr<Cairo::Surface>();

    object_renderer r;
    object_renderer::options opt;
    opt.frame(frame_color);
    auto s = image->style;
    // here for preview purposes using image's own opacity only
    double alpha = s && s->opacity.set && !s->opacity.inherit ? SP_SCALE24_TO_FLOAT(s->opacity.value) : 1.0;
    opt.image_opacity(alpha);
    opt.checkerboard(background);
    return r.render(*image, width, height, device_scale, opt);
}

void link_image(Gtk::Window* window, SPImage* image) {
    if (!window || !image) return;

    static std::string current_folder;
    std::vector<Glib::ustring> mime_types = {
        "image/png", "image/jpeg", "image/gif", "image/bmp", "image/tiff"
    };
    auto file = choose_file_open(_("Change Image"), window, mime_types, current_folder);
    if (file.empty()) return;

    // link image now
    // todo: set/calc dpi?
    // todo: set color profile?
    try {
        // convert filename to uri
        auto uri = Glib::filename_to_uri(file);
        setHrefAttribute(*image->getRepr(), uri);
    }
    catch (Glib::ConvertError const &e) {
        g_warning("Error converting path to URI: %s", e.what());
        setHrefAttribute(*image->getRepr(), file);
    }
    // SPImage modifies size when href changes; trigger it now before undo concludes
    // TODO: this needs to be fixed in SPImage
    image->document->_updateDocument(0);
    DocumentUndo::done(image->document, _("Change image"), INKSCAPE_ICON("shape-image"));
}

void set_rendering_mode(SPImage* image, int index) {
    static const std::array<const char*, 5> render = {
        "auto", "optimizeSpeed", "optimizeQuality", "crisp-edges", "pixelated"
    }; // SPImageRendering values

    if (!image || index < 0 || index >= render.size()) return;

    auto css = sp_repr_css_attr_new();
    sp_repr_css_set_property(css, "image-rendering", render[index]);
    if (auto image_node = image->getRepr()) {
        sp_repr_css_change(image_node, css, "style");
        DocumentUndo::done(image->document, _("Set image rendering option"), INKSCAPE_ICON("shape-image"));
    }
    sp_repr_css_attr_unref(css);
}

void set_aspect_ratio(SPImage* image, bool preserve_aspect_ratio) {
    if (!image) return;
    image->setAttribute("preserveAspectRatio", preserve_aspect_ratio ? "xMidYMid" : "none");
    DocumentUndo::done(image->document, _("Preserve image aspect ratio"), INKSCAPE_ICON("shape-image"));
}

} // unnamed namespace

ImageProperties::ImageProperties() :
    Glib::ObjectBase{"ImageProperties"}, WidgetVfuncsClassInit{}, // They are both needed w ClassInit
    Gtk::Box(Gtk::Orientation::HORIZONTAL),
    _builder(create_builder("image-properties.glade")),
    _preview(get_widget<Gtk::DrawingArea>(_builder, "preview")),
    _aspect(get_widget<Gtk::CheckButton>(_builder, "preserve")),
    _stretch(get_widget<Gtk::CheckButton>(_builder, "stretch")),
    _rendering(get_widget<Gtk::ComboBoxText>(_builder, "rendering")),
    _embed(get_widget<Gtk::Button>(_builder, "embed"))
{
    auto& main = get_widget<Gtk::Grid>(_builder, "main");
    UI::pack_start(*this, main, true, true);

    // arbitrarily selected max preview size for image content:
    _preview_max_width = 120;
    _preview_max_height = 90;

    _preview.set_draw_func([this](Cairo::RefPtr<Cairo::Context> const &ctx, int /*width*/, int /*height*/) {
        if (_preview_image) {
            ctx->set_source(_preview_image, 0, 0);
            ctx->paint();
        }
    });

    auto& change = get_widget<Gtk::Button>(_builder, "change-img");
    change.signal_clicked().connect([=](){
        if (_update.pending()) return;
        auto window = dynamic_cast<Gtk::Window*>(get_root());
        link_image(window, _image);
    });

    auto& extract = get_widget<Gtk::Button>(_builder, "export");
    extract.signal_clicked().connect([=](){
        if (_update.pending()) return;
        auto window = dynamic_cast<Gtk::Window*>(get_root());
        extract_image(window, _image);
    });

    _embed.signal_clicked().connect([=](){
        if (_update.pending() || !_image) return;
        // embed image in the current document
        Inkscape::Pixbuf copy(*_image->pixbuf);
        sp_embed_image(_image->getRepr(), &copy);
        DocumentUndo::done(_image->document, _("Embed image"), INKSCAPE_ICON("selection-make-bitmap-copy"));
    });

    _rendering.signal_changed().connect([=](){
        if (_update.pending()) return;
        auto index = _rendering.get_active_row_number();
        set_rendering_mode(_image, index);
    });

    _aspect.signal_toggled().connect([=](){
        if (_update.pending()) return;
        set_aspect_ratio(_image, _aspect.get_active());
    });
    _stretch.signal_toggled().connect([=](){
        if (_update.pending()) return;
        set_aspect_ratio(_image, !_stretch.get_active());
    });
}

ImageProperties::~ImageProperties() = default;

void ImageProperties::update(SPImage* image) {
    if (!image && !_image) return; // nothing to do

    _image = image;

    auto scoped(_update.block());

    auto small = [](const char* str) { return "<small>" + Glib::Markup::escape_text(str ? str : "") + "</small>"; };
    auto& name = get_widget<Gtk::Label>(_builder, "name");
    auto& info = get_widget<Gtk::Label>(_builder, "info");
    auto& url = get_widget<Gtk::Entry>(_builder, "href");

    if (!image) {
        name.set_markup(small("-"));
        info.set_markup(small("-"));
    }
    else {
        Glib::ustring id(image->getId() ? image->getId() : "");
        name.set_markup(small(id.empty() ? "-" : ("#" + id).c_str()));

        bool embedded = false;
        bool linked = false;
        auto href = Inkscape::getHrefAttribute(*image->getRepr()).second;
        if (href && std::strncmp(href, "data:", 5) == 0) {
            embedded = true;
        }
        else if (href && *href) {
            linked = true;
        }

        if (image->pixbuf) {
            std::ostringstream ost;
            if (!image->missing) {
                auto times = "\u00d7"; // multiplication sign
                // dimensions
                ost << image->pixbuf->width() << times << image->pixbuf->height() << " px\n";

                if (embedded) {
                    ost << _("Embedded");
                    ost << " (" << Util::format_file_size(std::strlen(href)) << ")\n";
                }
                if (linked) {
                    ost << _("Linked");
                    ost << '\n';
                }
                // color space
                if (image->color_profile && *image->color_profile) {
                    ost << _("Color profile:") << ' ' << image->color_profile << '\n';
                }
            }
            else {
                ost << _("Missing image") << '\n';
            }
            info.set_markup(small(ost.str().c_str()));
        }
        else {
            info.set_markup(small("-"));
        }

        url.set_text(linked ? href : "");
        url.set_sensitive(linked);
        _embed.set_sensitive(linked && image->pixbuf);

        // aspect ratio
        bool aspect_none = false;
        if (image->aspect_set) {
            aspect_none = image->aspect_align == SP_ASPECT_NONE;
        }
        if (aspect_none) {
            _stretch.set_active();
        }
        else {
            _aspect.set_active();
        }

        // rendering
        _rendering.set_active(image->style ? image->style->image_rendering.value : -1);
    }

    int width = _preview_max_width;
    int height = _preview_max_height;
    if (image && image->pixbuf) {
        double sw = image->pixbuf->width();
        double sh = image->pixbuf->height();
        double sx = sw / width;
        double sy = sh / height;
        auto scale = 1.0 / std::max(sx, sy);
        width = std::max(1, int(sw * scale + 0.5));
        height = std::max(1, int(sh * scale + 0.5));
    }
    // expand size to account for a frame around the image
    int frame = 2;
    width += frame;
    height += frame;
    _preview.set_size_request(width, height);
    _preview.queue_draw();

    // prepare preview
    auto device_scale = get_scale_factor();
    auto const fg = get_color();
    auto foreground = conv_gdk_color_to_rgba(fg, 0.30);
    update_bg_color();
    _preview_image = draw_preview(_image, width, height, device_scale, foreground, _background_color);
}

void ImageProperties::update_bg_color() {
    if (auto wnd = dynamic_cast<Gtk::Window*>(get_root())) {
        auto const color = get_color_with_class(*wnd, "theme_bg_color");
        _background_color = conv_gdk_color_to_rgba(color);
    }
    else {
        _background_color = 0x808080ff;
    }
}

void ImageProperties::css_changed(GtkCssStyleChange * /*change*/)
{
    update_bg_color();
    update(_image);
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
