// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A generic interface for plugging different
 *  autotracers into Inkscape.
 *
 * Authors:
 *   Bob Jamison <rjamison@earthlink.net>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *   PBS <pbs3141@gmail.com>
 *
 * Copyright (C) 2004-2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "trace.h"

#include <algorithm>
#include <cassert>
#include <mutex>
#include <optional>
#include <2geom/transforms.h>
#include <glibmm/i18n.h>
#include <glibmm/ustring.h>
#include <gtkmm/messagedialog.h>

#include "siox.h"
#include "desktop.h"
#include "document.h"
#include "document-undo.h"
#include "helper/geom.h"
#include "inkscape.h"
#include "message-stack.h"
#include "selection.h"
#include "svg/svg.h"
#include "async/async.h"
#include "async/progress-splitter.h"
#include "async/background-progress.h"
#include "display/cairo-utils.h"
#include "display/drawing.h"
#include "display/drawing-context.h"
#include "object/sp-item.h"
#include "object/sp-image.h"
#include "object/weakptr.h"
#include "ui/icon-names.h"
#include "ui/dialog-run.h"

namespace Inkscape::Trace {

namespace {

/**
 * Grab the image and siox items from the current selection, performing some validation.
 * \pre SP_ACTIVE_DESKTOP must not be null.
 */
std::optional<std::pair<SPImage*, std::vector<SPItem*>>> getImageAndItems(bool sioxEnabled, bool notifications = true)
{
    auto desktop = SP_ACTIVE_DESKTOP;
    auto msgStack = desktop->getMessageStack();
    auto sel = desktop->getSelection();

    if (sioxEnabled) {
        auto selection = std::vector<SPItem*>(sel->items().begin(), sel->items().end());
        std::sort(selection.begin(), selection.end(), sp_item_repr_compare_position_bool);

        SPImage *img = nullptr;
        std::vector<SPItem*> items;

        for (auto item : selection) {
            if (auto itemimg = cast<SPImage>(item)) {
                if (img) { // we want only one
                    if (notifications) msgStack->flash(Inkscape::ERROR_MESSAGE, _("Select only one <b>image</b> to trace"));
                    return {};
                }
                img = itemimg;
            } else if (img) { // Items are processed back-to-front, so this means "above the image".
                items.emplace_back(item);
            }
        }

        if (!img || items.empty()) {
            if (notifications) msgStack->flash(Inkscape::ERROR_MESSAGE, _("Select one image and one or more shapes above it"));
            return {};
        }

        return std::make_pair(img, std::move(items));
    } else {
        // SIOX not enabled. We want exactly one image selected.
        auto item = sel->singleItem();
        if (!item) {
            if (notifications) msgStack->flash(Inkscape::ERROR_MESSAGE, _("Select an <b>image</b> to trace")); // same as above
            return {};
        }

        auto img = cast<SPImage>(item);
        if (!img) {
            if (notifications) msgStack->flash(Inkscape::ERROR_MESSAGE, _("Select an <b>image</b> to trace"));
            return {};
        }

        return {{ img, {} }};
    }
}

/**
 * Given an SPImage, get the transform from pixbuf coordinates to the document.
 */
Geom::Affine getImageTransform(SPImage const *img)
{
    double x = img->x.computed;
    double y = img->y.computed;
    double w = img->width.computed;
    double h = img->height.computed;

    int iw = img->pixbuf->width();
    int ih = img->pixbuf->height();

    double wscale = w / iw;
    double hscale = h / ih;

    return Geom::Scale(wscale, hscale) * Geom::Translate(x, y) * img->transform;
}

Geom::IntPoint dimensions(Inkscape::Pixbuf const &pixbuf)
{
    return { pixbuf.width(), pixbuf.height() };
}

bool confirm_image_size(TracingEngine const *engine, Geom::IntPoint const &dimensions)
{
    if (engine->check_image_size(dimensions)) {
        char const *msg = _("Image looks too big. Process may take a while and it is"
                            " wise to save your document before continuing."
                            "\n\nContinue the procedure (without saving)?");
        Gtk::MessageDialog dialog(msg, false, Gtk::MessageType::WARNING, Gtk::ButtonsType::OK_CANCEL, true);

        if (Inkscape::UI::dialog_run(dialog) != Gtk::ResponseType::OK) {
            return false;
        }
    }

    return true;
}

/**
 * Given a list of SPItems, apply a transform and rasterize them to a surface of the specified dimensions.
 */
Cairo::RefPtr<Cairo::ImageSurface> rasterizeItems(std::vector<SPItem*> &items, Geom::Affine const &affine, Geom::IntPoint dimensions)
{
    auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, dimensions.x(), dimensions.y());
    auto dc = Inkscape::DrawingContext(surface->cobj(), {});
    auto const inv = affine.inverse();

    auto dkey = SPItem::display_key_new(1);
    Inkscape::Drawing drawing;

    for (auto item : items) {
        auto ai = item->invoke_show(drawing, dkey, SP_ITEM_SHOW_DISPLAY);
        drawing.setRoot(ai);
        auto rect = Geom::IntRect({0, 0}, dimensions);
        drawing.update(rect, inv);
        drawing.render(dc, rect);
        item->invoke_hide(dkey);
    }

    return surface;
}

class SioxImageCache
{
public:
    static auto &get()
    {
        static SioxImageCache const instance;
        return instance;
    }

    Glib::RefPtr<Gdk::Pixbuf> process(SioxImage const &sioximage, Async::Progress<double> &progress) const;

private:
    mutable std::mutex mutables;
    mutable unsigned last_hash = 0;
    mutable Glib::RefPtr<Gdk::Pixbuf> last_result;

    SioxImageCache() = default;
};

Glib::RefPtr<Gdk::Pixbuf> SioxImageCache::process(SioxImage const &sioximage, Async::Progress<double> &progress) const
{
    auto hash = sioximage.hash();

    auto g = std::lock_guard(mutables);

    if (hash == last_hash) {
        return last_result;
    }

    auto result = Siox(progress).extractForeground(sioximage, 0xffffff);

    // result.writePPM("siox2.ppm");

    last_hash = hash;
    last_result = result.getGdkPixbuf();

    return last_result;
}

Glib::RefPtr<Gdk::Pixbuf> sioxProcessImage(Glib::RefPtr<Gdk::Pixbuf> pixbuf, Cairo::RefPtr<Cairo::ImageSurface> siox_mask, Async::Progress<double> &progress)
{
    // Copy the pixbuf into the siox image.
    auto sioximage = SioxImage(pixbuf);
    int iwidth = sioximage.getWidth();
    int iheight = sioximage.getHeight();

    // Copy the mask into the siox image.
    assert(iwidth == siox_mask->get_width());
    assert(iheight == siox_mask->get_height());
    for (int y = 0; y < iheight; y++) {
        for (int x = 0; x < iwidth; x++) {
            auto p = siox_mask->get_data() + y * siox_mask->get_stride() + 4 * x;
            float a = p[3] / 255.0f;
            float cm = Siox::CERTAIN_BACKGROUND_CONFIDENCE + (Siox::UNKNOWN_REGION_CONFIDENCE - Siox::CERTAIN_BACKGROUND_CONFIDENCE) * a;
            sioximage.setConfidence(x, y, cm);
        }
    }

    /*auto tmp = simage;
    for (int i = 0; i < iwidth * iheight; i++) {
        tmp.getImageData()[i] = 255 * tmp.getConfidenceData()[i];
    }
    tmp.writePPM("/tmp/x1.ppm");*/

    // Process or retrieve from cache.
    return SioxImageCache::get().process(sioximage, progress);
}

} // namespace

namespace detail {

struct TraceFutureCreate
{
    TraceFutureCreate() = delete;

    static auto create(decltype(TraceFuture::channel) &&channel, decltype(TraceFuture::image_watcher) &&image_watcher)
    {
        TraceFuture result;
        result.channel = std::move(channel);
        result.image_watcher = std::move(image_watcher);
        return result;
    }
};

} // namespace detail

// Todo: Consider rewriting using C++20 coroutines.
class TraceTask
{
public:
    TraceTask(std::unique_ptr<TracingEngine> engine, bool sioxEnabled, std::function<void(double)> onprogress, std::function<void()> onfinished)
        : engine(std::move(engine))
        , sioxEnabled(sioxEnabled)
        , type(Type::Trace)
        , onprogress(std::move(onprogress))
        , onfinished_trace(std::move(onfinished)) {}

    TraceTask(std::unique_ptr<TracingEngine> engine, bool sioxEnabled, std::function<void(Glib::RefPtr<Gdk::Pixbuf>)> onfinished)
        : engine(std::move(engine))
        , sioxEnabled(sioxEnabled)
        , type(Type::Preview)
        , onprogress([] (auto&&) {})
        , onfinished_preview(std::move(onfinished)) {}

    TraceTask(TraceTask const &) = delete;
    TraceTask &operator=(TraceTask const &) = delete;

    TraceFuture launch(std::unique_ptr<TraceTask> self);

private:
    std::unique_ptr<TracingEngine> engine;
    bool sioxEnabled;

    // Whether this is the full trace task, or just the preview task.
    enum class Type
    {
        Trace,
        Preview
    };
    Type type;

    // Unsafe. Cannot call from worker thread since may perform actions in main thread. (This isn't Rust so we need a comment.)
    std::function<void(double)> onprogress;
    std::function<void()> onfinished_trace; // For trace task.
    std::function<void(Glib::RefPtr<Gdk::Pixbuf>)> onfinished_preview; // For preview task.

    // Unsafe. Cannot lock from worker thread since must be destroyed by main thread. (See above.)
    std::weak_ptr<SPWeakPtr<SPImage>> image_watcher_weak;

    std::shared_ptr<Inkscape::Pixbuf const> image_pixbuf;
    Geom::Affine image_transform;
    Cairo::RefPtr<Cairo::ImageSurface> siox_mask;
    Async::Channel::Source channel;

    TraceResult traceresult;

    void do_async_work(std::unique_ptr<TraceTask> self);
    void do_final_work(std::unique_ptr<TraceTask> self);
};

TraceFuture trace(std::unique_ptr<TracingEngine> engine, bool sioxEnabled, std::function<void(double)> onprogress, std::function<void()> onfinished)
{
    auto task = std::make_unique<TraceTask>(std::move(engine), sioxEnabled, std::move(onprogress), std::move(onfinished));
    auto saved = task.get();
    return saved->launch(std::move(task));
}

TraceFuture preview(std::unique_ptr<TracingEngine> engine, bool sioxEnabled, std::function<void(Glib::RefPtr<Gdk::Pixbuf>)> onfinished)
{
    auto task = std::make_unique<TraceTask>(std::move(engine), sioxEnabled, std::move(onfinished));
    auto saved = task.get();
    return saved->launch(std::move(task));
}

TraceFuture TraceTask::launch(std::unique_ptr<TraceTask> self)
{
    // Grab data and validate setup.

    auto desktop = SP_ACTIVE_DESKTOP;
    if (!desktop) {
        g_warning("Trace: No active desktop\n");
        return {};
    }

    auto msgStack = desktop->getMessageStack();

    auto doc = SP_ACTIVE_DOCUMENT;
    if (!doc) {
        if (type == Type::Trace) msgStack->flash(Inkscape::ERROR_MESSAGE, _("Trace: No active document"));
        return {};
    }
    doc->ensureUpToDate();

    auto imageanditems = getImageAndItems(sioxEnabled, type == Type::Trace);
    if (!imageanditems) {
        return {};
    }

    // Copy into coroutine frame.

    auto image = imageanditems->first;

    image_pixbuf = image->pixbuf; // Note: image->pixbuf is immutable, so can be shared thread-safely.
    if (!image_pixbuf) {
        if (type == Type::Trace) msgStack->flash(Inkscape::ERROR_MESSAGE, _("Trace: Image has no bitmap data"));
        return {};
    }

    if (type == Type::Trace && !confirm_image_size(engine.get(), dimensions(*image_pixbuf))) {
        // Image is too big and user decided to cancel.
        return {};
    }

    image_transform = getImageTransform(image);

    if (sioxEnabled) {
        siox_mask = rasterizeItems(imageanditems->second, image_transform, dimensions(*image_pixbuf));
    }

    if (type == Type::Trace) msgStack->flash(Inkscape::NORMAL_MESSAGE, _("Trace: Starting trace..."));

    // Open channel and launch background task.

    auto [src, dst] = Async::Channel::create();
    auto image_watcher = std::make_shared<SPWeakPtr<SPImage>>(image);

    channel = std::move(src);
    image_watcher_weak = image_watcher;

    Async::fire_and_forget([this, self = std::move(self)] () mutable {
        do_async_work(std::move(self));
    });

    return detail::TraceFutureCreate::create(std::move(dst), std::move(image_watcher));
}

void TraceTask::do_async_work(std::unique_ptr<TraceTask> self)
{
    if (!channel) {
        // Cancelled while suspended.
        return;
    }

    try {
        auto progress = Async::BackgroundProgress(channel, onprogress);
        auto throttled = Async::ProgressTimeThrottler(progress, std::chrono::milliseconds(10));

        // Get progress subobjects for siox and trace sub-tasks.
        std::optional<Async::SubProgress<double>> sub_siox, sub_trace;

        Async::ProgressSplitter(throttled)
            .add_if(sub_siox, 0.1, sioxEnabled)
            .add_if(sub_trace, 0.9, type == Type::Trace);

        // Convert the pixbuf to a GdkPixbuf, which due to immutability requires making a copy first.
        auto copy = Pixbuf(*image_pixbuf);
        auto gdkpixbuf = Glib::wrap(copy.getPixbufRaw(), true);

        // If SIOX has been enabled, run SIOX processing.
        if (sioxEnabled) {
            gdkpixbuf = sioxProcessImage(gdkpixbuf, siox_mask, *sub_siox);
            siox_mask.clear();
            sub_siox->report_or_throw(1.0);
        }

        // If in preview mode, compute and return the preview and exit now.
        if (type == Type::Preview) {
            gdkpixbuf = engine->preview(gdkpixbuf);
            channel.run(std::bind(onfinished_preview, std::move(gdkpixbuf)));
            return;
        }

        // Actually perform the tracing.
        traceresult = engine->trace(gdkpixbuf, *sub_trace);

        gdkpixbuf.reset();

        progress.report_or_throw(1.0);

        // Return to the original thread for the remainder of the processing.
        channel.run([this, self = std::move(self)] () mutable {
            do_final_work(std::move(self));
        });

    } catch (Async::CancelledException const &) {

        // no need to emit signals if manually aborted

    } catch (...) {

        g_warning("TraceTask::do_async_work: tracing aborted due to exception");
        if (type == Type::Trace) {
            channel.run(onfinished_trace);
        } else {
            channel.run(std::bind(onfinished_preview, Glib::RefPtr<Gdk::Pixbuf>()));
        }

    }
}

void TraceTask::do_final_work(std::unique_ptr<TraceTask> self)
{
    assert(type == Type::Trace);
    assert(channel);

    auto doc = SP_ACTIVE_DOCUMENT;
    auto desktop = SP_ACTIVE_DESKTOP;
    auto image_watcher = image_watcher_weak.lock();

    if (!doc || !desktop || !image_watcher || traceresult.empty()) {
        onfinished_trace();
        return;
    }

    auto image = image_watcher->get();
    if (!image) {
        // Image was deleted.
        onfinished_trace();
        return;
    }

    auto msgStack = desktop->getMessageStack();
    auto selection = desktop->getSelection();

    // Get pointers to the <image> and its parent.
    // XML Tree being used directly here while it shouldn't be
    Inkscape::XML::Node *imgRepr = image->getRepr();
    Inkscape::XML::Node *par     = imgRepr->parent();

    // Update the image transform - it may have changed from its initial value.
    image_transform = getImageTransform(image);

    // OK. Now let's start making new nodes.
    Inkscape::XML::Document *xml_doc = desktop->doc()->getReprDoc();
    Inkscape::XML::Node *groupRepr = nullptr;

    // If more than one path, make a <g>roup of <path>s.
    int nrPaths = traceresult.size();
    if (nrPaths > 1) {
        groupRepr = xml_doc->createElement("svg:g");
        par->addChild(groupRepr, imgRepr);
    }

    long totalNodeCount = 0;

    for (auto const &result : traceresult) {
        totalNodeCount += count_pathvector_nodes(result.path);

        Inkscape::XML::Node *pathRepr = xml_doc->createElement("svg:path");
        pathRepr->setAttributeOrRemoveIfEmpty("style", result.style);
        pathRepr->setAttributeOrRemoveIfEmpty("d", sp_svg_write_path(result.path * image_transform));

        if (nrPaths > 1) {
            groupRepr->addChild(pathRepr, nullptr);
        } else {
            par->addChild(pathRepr, imgRepr);
        }

        if (nrPaths == 1) {
            selection->clear();
            selection->add(pathRepr);
        }

        Inkscape::GC::release(pathRepr);
    }

    // If we have a group, then focus on it.
    if (nrPaths > 1) {
        selection->clear();
        selection->add(groupRepr);
        Inkscape::GC::release(groupRepr);
    }

    // Inform the document, so we can undo.
    DocumentUndo::done(doc, _("Trace bitmap"), INKSCAPE_ICON("bitmap-trace"));

    auto const msg = Glib::ustring::compose(_("Trace: Done. %1 node(s) created"), totalNodeCount);
    msgStack->flash(Inkscape::NORMAL_MESSAGE, msg.c_str());

    onfinished_trace();
}

} // namespace Inkscape::Trace

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
