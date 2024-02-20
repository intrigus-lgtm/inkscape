// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Drag and drop of drawings onto canvas.
 */

/* Authors:
 *
 * Copyright (C) Tavmjong Bah 2019
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "drag-and-drop.h"

#include <glibmm/i18n.h>
#include <glibmm/value.h>
#include <glibmm/miscutils.h>
#include <giomm/inputstream.h>
#include <giomm/memoryoutputstream.h>
#include <gtkmm/droptarget.h>

#include "desktop-style.h"
#include "document.h"
#include "document-undo.h"
#include "gradient-drag.h"
#include "file.h"
#include "selection.h"
#include "style.h"
#include "layer-manager.h"

#include "extension/find_extension_by_mime.h"

#include "object/sp-shape.h"
#include "object/sp-text.h"
#include "object/sp-flowtext.h"

#include "path/path-util.h"

#include "svg/svg-color.h" // write color

#include "ui/clipboard.h"
#include "ui/interface.h"
#include "ui/tools/tool-base.h"
#include "ui/widget/canvas.h"  // Target, canvas to world transform.
#include "ui/widget/desktop-widget.h"

#include "widgets/paintdef.h"

using Inkscape::DocumentUndo;

namespace {

/*
 * Gtk API wrapping - Todo: Improve gtkmm.
 */

template <typename T>
T *get(GValue *value)
{
    if (G_VALUE_HOLDS(value, Glib::Value<T>::value_type())) {
        return reinterpret_cast<T *>(g_value_get_boxed(value));
    } else {
        return nullptr;
    }
}

template <typename T>
T const *get(GValue const *value)
{
    if (G_VALUE_HOLDS(value, Glib::Value<T>::value_type())) {
        return reinterpret_cast<T const *>(g_value_get_boxed(value));
    } else {
        return nullptr;
    }
}

template <typename T>
T *get(Glib::ValueBase &value)
{
    return get<T>(value.gobj());
}

template <typename T>
T const *get(Glib::ValueBase const &value)
{
    return get<T>(value.gobj());
}

bool holds(Glib::ValueBase const &value, GType type)
{
    return G_VALUE_HOLDS(value.gobj(), type);
}

template <typename T>
GValue make_value(T &&t)
{
    Glib::Value<T> v;
    v.init(v.value_type());
    *reinterpret_cast<T *>(g_value_get_boxed(v.gobj())) = std::forward<T>(t);
    return std::exchange(*v.gobj(), GValue(G_VALUE_INIT));
}

template <typename T, typename F>
void foreach(GSList *list, F &&f)
{
    g_slist_foreach(list, +[] (void *ptr, void *data) {
        auto t = reinterpret_cast<T *>(ptr);
        auto f = reinterpret_cast<F *>(data);
        f->operator()(t);
    }, &f);
}

std::span<char const> get_span(Glib::RefPtr<Glib::Bytes> const &bytes)
{
    gsize size{};
    return {reinterpret_cast<char const *>(bytes->get_data(size)), size};
}

template <typename T>
Glib::RefPtr<Glib::Bytes> make_bytes(T &&t)
{
    using Td = std::decay_t<T>;
    auto const p = new Td(std::forward<T>(t));
    auto const span = std::span<char const>(*p);
    return Glib::wrap(g_bytes_new_with_free_func(span.data(), span.size_bytes(), +[] (void *p) {
        delete reinterpret_cast<Td *>(p);
    }, p));
}

template <typename T>
GValue from_bytes(Glib::RefPtr<Glib::Bytes> &&bytes, char const *mime_type) = delete;

template <typename T>
void deserialize_func(GdkContentDeserializer *deserializer)
{
    auto const in = Glib::wrap(gdk_content_deserializer_get_input_stream(deserializer), true);
    auto const out = Gio::MemoryOutputStream::create();
    out->splice_async(in, [deserializer, out] (Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
            out->splice_finish(result);
            out->close();
            *gdk_content_deserializer_get_value(deserializer) = from_bytes<T>(out->steal_as_bytes(), gdk_content_deserializer_get_mime_type(deserializer));
            gdk_content_deserializer_return_success(deserializer);
        } catch (Glib::Error const &error) {
            gdk_content_deserializer_return_error(deserializer, g_error_copy(error.gobj()));
        }
    }, Gio::OutputStream::SpliceFlags::CLOSE_SOURCE);
};

template <typename T>
void register_deserializer(char const *mime_type)
{
    gdk_content_register_deserializer(mime_type, Glib::Value<T>::value_type(), deserialize_func<T>, nullptr, nullptr);
}

template <typename T>
Glib::RefPtr<Glib::Bytes> to_bytes(T const &t, char const *mime_type) = delete;

template <typename T>
void serialize_func(GdkContentSerializer *serializer)
{
    auto const out = Glib::wrap(gdk_content_serializer_get_output_stream(serializer), true);
    auto const bytes = to_bytes(*get<T>(gdk_content_serializer_get_value(serializer)), gdk_content_serializer_get_mime_type(serializer));
    auto const span = get_span(bytes);
    out->write_all_async(span.data(), span.size_bytes(), [serializer, out, bytes] (Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
            gsize _;
            out->write_all_finish(result, _);
            gdk_content_serializer_return_success(serializer);
        } catch (Glib::Error const &error) {
            gdk_content_serializer_return_error(serializer, g_error_copy(error.gobj()));
        }
    });
};

template <typename T>
void register_serializer(char const *mime_type)
{
    gdk_content_register_serializer(Glib::Value<T>::value_type(), mime_type, serialize_func<T>, nullptr, nullptr);
}

/*
 * Actual code
 */

struct DnDSvg
{
    Glib::RefPtr<Glib::Bytes> bytes;
};

template <>
GValue from_bytes<DnDSvg>(Glib::RefPtr<Glib::Bytes> &&bytes, char const *)
{
    return make_value(DnDSvg{std::move(bytes)});
}

template <>
GValue from_bytes<PaintDef>(Glib::RefPtr<Glib::Bytes> &&bytes, char const *mime_type)
{
    PaintDef result;
    if (!result.fromMIMEData(mime_type, get_span(bytes))) {
        throw Glib::Error(G_FILE_ERROR, 0, "Failed to parse colour");
    }
    return make_value(std::move(result));
}

template <>
Glib::RefPtr<Glib::Bytes> to_bytes<PaintDef>(PaintDef const &paintdef, char const *mime_type)
{
    return make_bytes(paintdef.getMIMEData(mime_type));
}

template <>
Glib::RefPtr<Glib::Bytes> to_bytes<DnDSymbol>(DnDSymbol const &symbol, char const *)
{
    return make_bytes(symbol.id.raw());
}

std::vector<GType> const &get_drop_types()
{
    static auto const instance = [] () -> std::vector<GType> {
        for (auto mime_type : {"image/svg", "image/svg+xml"}) {
            register_deserializer<DnDSvg>(mime_type);
        }

        for (auto mime_type : {mimeOSWB_COLOR, mimeX_COLOR}) {
            register_deserializer<PaintDef>(mime_type);
        }

        for (auto mime_type : {mimeOSWB_COLOR, mimeX_COLOR, mimeTEXT}) {
            register_serializer<PaintDef>(mime_type);
        }

        register_serializer<DnDSymbol>("text/plain");

        return {
            Glib::Value<PaintDef>::value_type(),
            Glib::Value<DnDSvg>::value_type(),
            GDK_TYPE_FILE_LIST,
            Glib::Value<DnDSymbol>::value_type(),
            GDK_TYPE_TEXTURE
        };
    }();

    return instance;
}

bool on_drop(Glib::ValueBase const &value, double x, double y, SPDesktopWidget *dtw, Glib::RefPtr<Gtk::DropTarget> const &drop_target)
{
    auto const desktop = dtw->get_desktop();
    auto const canvas = dtw->get_canvas();
    auto const doc = desktop->doc();
    auto const prefs = Inkscape::Preferences::get();

    auto const canvas_pos = Geom::Point{x, y};
    auto const world_pos = canvas->canvas_to_world(canvas_pos);
    auto const dt_pos = desktop->w2d(world_pos);

    if (auto const paintdef = get<PaintDef>(value)) {
        auto const item = desktop->getItemAtPoint(world_pos, true);
        if (!item) {
            return false;
        }

        auto find_gradient = [&] () -> SPGradient * {
            for (auto obj : doc->getResourceList("gradient")) {
                auto const grad = cast_unsafe<SPGradient>(obj);
                if (grad->hasStops() && grad->getId() == paintdef->get_description()) {
                    return grad;
                }
            }
            return nullptr;
        };

        std::string colorspec;
        if (paintdef->get_type() == PaintDef::NONE) {
            colorspec = "none";
        } else {
            if (auto const grad = find_gradient()) {
                colorspec = std::string{"url(#"} + grad->getId() + ")";
            } else {
                auto const [r, g, b] = paintdef->get_rgb();
                colorspec.resize(63);
                sp_svg_write_color(colorspec.data(), colorspec.size() + 1, SP_RGBA32_U_COMPOSE(r, g, b, 0xff));
                colorspec.resize(std::strlen(colorspec.c_str()));
            }
        }

        if (desktop->getTool() && desktop->getTool()->get_drag()) {
            if (desktop->getTool()->get_drag()->dropColor(item, colorspec.c_str(), dt_pos)) {
                DocumentUndo::done(doc , _("Drop color on gradient"), "");
                desktop->getTool()->get_drag()->updateDraggers();
                return true;
            }
        }

        //if (tools_active(desktop, TOOLS_TEXT)) {
        //    if (sp_text_context_drop_color(c, button_doc)) {
        //        SPDocumentUndo::done(doc , _("Drop color on gradient stop"), "");
        //    }
        //}

        bool fillnotstroke = drop_target->get_current_drop()->get_actions() != Gdk::DragAction::MOVE;
        if (fillnotstroke && (is<SPShape>(item) || is<SPText>(item) || is<SPFlowtext>(item))) {
            if (auto const curve = curve_for_item(item)) {
                auto const pathv = curve->get_pathvector() * (item->i2dt_affine() * desktop->d2w());

                double dist;
                pathv.nearestTime(world_pos, &dist);

                double const stroke_tolerance =
                    (!item->style->stroke.isNone() ?
                         desktop->current_zoom() *
                             item->style->stroke_width.computed *
                             item->i2dt_affine().descrim() * 0.5
                                                   : 0.0)
                    + prefs->getIntLimited("/options/dragtolerance/value", 0, 0, 100);

                if (dist < stroke_tolerance) {
                    fillnotstroke = false;
                }
            }
        }

        auto const css = sp_repr_css_attr_new();
        sp_repr_css_set_property(css, fillnotstroke ? "fill" : "stroke", colorspec.c_str());
        sp_desktop_apply_css_recursive(item, css, true);
        sp_repr_css_attr_unref(css);

        item->updateRepr();
        DocumentUndo::done(doc,  _("Drop color"), "");
        return true;
    } else if (auto const dndsvg = get<DnDSvg>(value)) {
        auto prefs_scope = prefs->temporaryPreferences();
        prefs->setBool("/options/onimport", true);

        auto const data = get_span(dndsvg->bytes);
        if (data.empty()) {
            return false;
        }

        auto const newdoc = sp_repr_read_mem(data.data(), data.size_bytes(), SP_SVG_NS_URI);
        if (!newdoc) {
            sp_ui_error_dialog(_("Could not parse SVG data"));
            return false;
        }

        auto const root = newdoc->root();
        auto const style = root->attribute("style");

        auto const xml_doc = doc->getReprDoc();
        auto const newgroup = xml_doc->createElement("svg:g");
        newgroup->setAttribute("style", style);
        for (auto child = root->firstChild(); child; child = child->next()) {
            newgroup->appendChild(child->duplicate(xml_doc));
        }

        Inkscape::GC::release(newdoc);

        // Add it to the current layer

        // Greg's edits to add intelligent positioning of svg drops
        auto const new_obj = desktop->layerManager().currentLayer()->appendChildRepr(newgroup);

        auto const selection = desktop->getSelection();
        selection->set(cast<SPItem>(new_obj));

        // move to mouse pointer
        desktop->getDocument()->ensureUpToDate();
        if (auto const sel_bbox = selection->visualBounds()) {
            selection->moveRelative(desktop->point() - sel_bbox->midpoint(), false);
        }

        Inkscape::GC::release(newgroup);
        DocumentUndo::done(doc, _("Drop SVG"), "");
        return true;
    } else if (holds(value, GDK_TYPE_FILE_LIST)) {
        auto prefs_scope = prefs->temporaryPreferences();
        prefs->setBool("/options/onimport", true);

        auto list = reinterpret_cast<GSList *>(g_value_get_boxed(value.gobj()));
        foreach<GFile>(list, [&] (GFile *f) {
            auto const path = g_file_get_path(f);
            if (path && std::strlen(path) > 2) {
                file_import(doc, path, nullptr);
            }
        });

        return true;
    } else if (holds(value, Glib::Value<DnDSymbol>::value_type())) {
        auto cm = Inkscape::UI::ClipboardManager::get();
        cm->insertSymbol(desktop, dt_pos);
        DocumentUndo::done(doc, _("Drop Symbol"), "");
        return true;
    } else if (holds(value, GDK_TYPE_TEXTURE)) {
        auto const ext = Inkscape::Extension::find_by_mime("image/png");
        bool const save = std::strcmp(ext->get_param_optiongroup("link"), "embed") == 0;
        ext->set_param_optiongroup("link", "embed");
        ext->set_gui(false);

        // Absolutely stupid, and the same for clipboard.cpp.
        auto const filename = Glib::build_filename(Glib::get_user_cache_dir(), "inkscape-dnd-import");
        auto const img = Glib::wrap(GDK_TEXTURE(g_value_get_object(value.gobj())), true);
        img->save_to_png(filename);
        file_import(doc, filename, ext);
        unlink(filename.c_str());

        ext->set_param_optiongroup("link", save ? "embed" : "link");
        ext->set_gui(true);
        DocumentUndo::done(doc, _("Drop bitmap image"), "");
        return true;
    }

    return false;
}

} // namespace

void ink_drag_setup(SPDesktopWidget *dtw)
{
    auto drop_target = Gtk::DropTarget::create(G_TYPE_INVALID, Gdk::DragAction::COPY | Gdk::DragAction::MOVE);
    drop_target->set_gtypes(get_drop_types());
    drop_target->signal_drop().connect(sigc::bind(&on_drop, dtw, drop_target), false);
    dtw->get_canvas()->add_controller(drop_target);
}

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
