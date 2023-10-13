// SPDX-License-Identifier: GPL-2.0-or-later
#include <2geom/parallelogram.h>
#include "ui/util.h"
#include "helper/geom.h"
#include "cairographics.h"
#include "stores.h"
#include "prefs.h"
#include "util.h"
#include "framecheck.h"

namespace Inkscape {
namespace UI {
namespace Widget {

CairoGraphics::CairoGraphics(Prefs const &prefs, Stores const &stores, PageInfo const &pi)
    : prefs(prefs)
    , stores(stores)
    , pi(pi) {}

std::unique_ptr<Graphics> Graphics::create_cairo(Prefs const &prefs, Stores const &stores, PageInfo const &pi)
{
    return std::make_unique<CairoGraphics>(prefs, stores, pi);
}

void CairoGraphics::set_outlines_enabled(bool enabled)
{
    outlines_enabled = enabled;
    if (!enabled) {
        store.outline_surface.clear();
        snapshot.outline_surface.clear();
    }
}

void CairoGraphics::recreate_store(Geom::IntPoint const &dims)
{
    auto surface_size = dims * scale_factor;

    auto make_surface = [&, this] {
        auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, surface_size.x(), surface_size.y());
        cairo_surface_set_device_scale(surface->cobj(), scale_factor, scale_factor); // No C++ API!
        return surface;
    };

    // Recreate the store surface.
    bool reuse_surface = store.surface && dimensions(store.surface) == surface_size;
    if (!reuse_surface) {
        store.surface = make_surface();
    }

    // Ensure the store surface is filled with the correct default background.
    if (background_in_stores) {
        auto cr = Cairo::Context::create(store.surface);
        paint_background(stores.store(), pi, page, desk, cr);
    } else if (reuse_surface) {
        auto cr = Cairo::Context::create(store.surface);
        cr->set_operator(Cairo::Context::Operator::CLEAR);
        cr->paint();
    }

    // Do the same for the outline surface (except always clearing it to transparent).
    if (outlines_enabled) {
        bool reuse_outline_surface = store.outline_surface && dimensions(store.outline_surface) == surface_size;
        if (!reuse_outline_surface) {
            store.outline_surface = make_surface();
        } else {
            auto cr = Cairo::Context::create(store.outline_surface);
            cr->set_operator(Cairo::Context::Operator::CLEAR);
            cr->paint();
        }
    }
}

void CairoGraphics::shift_store(Fragment const &dest)
{
    auto surface_size = dest.rect.dimensions() * scale_factor;

    // Determine the geometry of the shift.
    auto shift = dest.rect.min() - stores.store().rect.min();
    auto reuse_rect = (dest.rect & cairo_to_geom(stores.store().drawn->get_extents())).regularized();
    assert(reuse_rect); // Should not be called if there is no overlap.

    auto make_surface = [&, this] {
        auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, surface_size.x(), surface_size.y());
        cairo_surface_set_device_scale(surface->cobj(), scale_factor, scale_factor); // No C++ API!
        return surface;
    };

    // Create the new store surface.
    bool reuse_surface = snapshot.surface && dimensions(snapshot.surface) == surface_size;
    auto new_surface = reuse_surface ? std::move(snapshot.surface) : make_surface();

    // Paint background into region of store not covered by next operation.
    auto cr = Cairo::Context::create(new_surface);
    if (background_in_stores || reuse_surface) {
        auto reg = Cairo::Region::create(geom_to_cairo(dest.rect));
        reg->subtract(geom_to_cairo(*reuse_rect));
        reg->translate(-dest.rect.left(), -dest.rect.top());
        cr->save();
        region_to_path(cr, reg);
        cr->clip();
        if (background_in_stores) {
            paint_background(dest, pi, page, desk, cr);
        } else { // otherwise, reuse_surface is true
            cr->set_operator(Cairo::Context::Operator::CLEAR);
            cr->paint();
        }
        cr->restore();
    }

    // Copy re-usuable contents of old store into new store, shifted.
    cr->rectangle(reuse_rect->left() - dest.rect.left(), reuse_rect->top() - dest.rect.top(), reuse_rect->width(), reuse_rect->height());
    cr->clip();
    cr->set_source(store.surface, -shift.x(), -shift.y());
    cr->set_operator(Cairo::Context::Operator::SOURCE);
    cr->paint();

    // Set the result as the new store surface.
    snapshot.surface = std::move(store.surface);
    store.surface = std::move(new_surface);

    // Do the same for the outline store
    if (outlines_enabled) {
        // Create.
        bool reuse_outline_surface = snapshot.outline_surface && dimensions(snapshot.outline_surface) == surface_size;
        auto new_outline_surface = reuse_outline_surface ? std::move(snapshot.outline_surface) : make_surface();
        // Background.
        auto cr = Cairo::Context::create(new_outline_surface);
        if (reuse_outline_surface) {
            cr->set_operator(Cairo::Context::Operator::CLEAR);
            cr->paint();
        }
        // Copy.
        cr->rectangle(reuse_rect->left() - dest.rect.left(), reuse_rect->top() - dest.rect.top(), reuse_rect->width(), reuse_rect->height());
        cr->clip();
        cr->set_source(store.outline_surface, -shift.x(), -shift.y());
        cr->set_operator(Cairo::Context::Operator::SOURCE);
        cr->paint();
        // Set.
        snapshot.outline_surface = std::move(store.outline_surface);
        store.outline_surface = std::move(new_outline_surface);
    }
}

void CairoGraphics::swap_stores()
{
    std::swap(store, snapshot);
}

void CairoGraphics::fast_snapshot_combine()
{
    auto copy = [&, this] (Cairo::RefPtr<Cairo::ImageSurface> const &from,
                           Cairo::RefPtr<Cairo::ImageSurface> const &to) {
        auto cr = Cairo::Context::create(to);
        cr->set_antialias(Cairo::ANTIALIAS_NONE);
        cr->set_operator(Cairo::Context::Operator::SOURCE);
        cr->translate(-stores.snapshot().rect.left(), -stores.snapshot().rect.top());
        cr->transform(geom_to_cairo(stores.store().affine.inverse() * stores.snapshot().affine));
        cr->translate(-1.0, -1.0);
        region_to_path(cr, shrink_region(stores.store().drawn, 2));
        cr->translate(1.0, 1.0);
        cr->clip();
        cr->set_source(from, stores.store().rect.left(), stores.store().rect.top());
        Cairo::SurfacePattern(cr->get_source()->cobj()).set_filter(Cairo::SurfacePattern::Filter::FAST);
        cr->paint();
    };

                          copy(store.surface,         snapshot.surface);
    if (outlines_enabled) copy(store.outline_surface, snapshot.outline_surface);
}

void CairoGraphics::snapshot_combine(Fragment const &dest)
{
    // Create the new fragment.
    auto content_size = dest.rect.dimensions() * scale_factor;

    auto make_surface = [&] {
        auto result = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, content_size.x(), content_size.y());
        cairo_surface_set_device_scale(result->cobj(), scale_factor, scale_factor); // No C++ API!
        return result;
    };

    CairoFragment fragment;
                          fragment.surface         = make_surface();
    if (outlines_enabled) fragment.outline_surface = make_surface();

    auto copy = [&, this] (Cairo::RefPtr<Cairo::ImageSurface> const &store_from,
                           Cairo::RefPtr<Cairo::ImageSurface> const &snapshot_from,
                           Cairo::RefPtr<Cairo::ImageSurface> const &to, bool background) {
        auto cr = Cairo::Context::create(to);
        cr->set_antialias(Cairo::ANTIALIAS_NONE);
        cr->set_operator(Cairo::Context::Operator::SOURCE);
        if (background) paint_background(dest, pi, page, desk, cr);
        cr->translate(-dest.rect.left(), -dest.rect.top());
        cr->transform(geom_to_cairo(stores.snapshot().affine.inverse() * dest.affine));
        cr->rectangle(stores.snapshot().rect.left(), stores.snapshot().rect.top(), stores.snapshot().rect.width(), stores.snapshot().rect.height());
        cr->set_source(snapshot_from, stores.snapshot().rect.left(), stores.snapshot().rect.top());
        Cairo::SurfacePattern(cr->get_source()->cobj()).set_filter(Cairo::SurfacePattern::Filter::FAST);
        cr->fill();
        cr->transform(geom_to_cairo(stores.store().affine.inverse() * stores.snapshot().affine));
        cr->translate(-1.0, -1.0);
        region_to_path(cr, shrink_region(stores.store().drawn, 2));
        cr->translate(1.0, 1.0);
        cr->clip();
        cr->set_source(store_from, stores.store().rect.left(), stores.store().rect.top());
        Cairo::SurfacePattern(cr->get_source()->cobj()).set_filter(Cairo::SurfacePattern::Filter::FAST);
        cr->paint();
    };

                          copy(store.surface,         snapshot.surface,         fragment.surface,         background_in_stores);
    if (outlines_enabled) copy(store.outline_surface, snapshot.outline_surface, fragment.outline_surface, false);

    snapshot = std::move(fragment);
}

Cairo::RefPtr<Cairo::ImageSurface> CairoGraphics::request_tile_surface(Geom::IntRect const &rect, bool /*nogl*/)
{
    // Create temporary surface, isolated from store.
    auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, rect.width() *  scale_factor, rect.height() *  scale_factor);
    cairo_surface_set_device_scale(surface->cobj(), scale_factor, scale_factor);
    return surface;
}

void CairoGraphics::draw_tile(Fragment const &fragment, Cairo::RefPtr<Cairo::ImageSurface> surface, Cairo::RefPtr<Cairo::ImageSurface> outline_surface)
{
    // Blit from the temporary surface to the store.
    auto diff = fragment.rect.min() - stores.store().rect.min();

    auto cr = Cairo::Context::create(store.surface);
    cr->set_operator(Cairo::Context::Operator::SOURCE);
    cr->set_source(surface, diff.x(), diff.y());
    cr->rectangle(diff.x(), diff.y(), fragment.rect.width(), fragment.rect.height());
    cr->fill();

    if (outlines_enabled) {
        auto cr = Cairo::Context::create(store.outline_surface);
        cr->set_operator(Cairo::Context::Operator::SOURCE);
        cr->set_source(outline_surface, diff.x(), diff.y());
        cr->rectangle(diff.x(), diff.y(), fragment.rect.width(), fragment.rect.height());
        cr->fill();
    }
}

void CairoGraphics::paint_widget(Fragment const &view, PaintArgs const &a, Cairo::RefPtr<Cairo::Context> const &cr)
{
    auto f = FrameCheck::Event();

    // Turn off anti-aliasing while compositing the widget for large performance gains. (We can usually
    // get away with it without any negative visual impact; when we can't, we turn it back on.)
    cr->set_antialias(Cairo::ANTIALIAS_NONE);

    // Due to a Cairo bug, Cairo sometimes draws outside of its clip region. This results in flickering as Canvas content is drawn
    // over the bottom scrollbar. This cannot be fixed by setting the correct clip region, as Cairo detects that and turns it into
    // a no-op. Hence the following workaround, which recreates the clip region from scratch, is required.
    auto rlist = cairo_copy_clip_rectangle_list(cr->cobj());
    cr->reset_clip();
    for (int i = 0; i < rlist->num_rectangles; i++) {
        cr->rectangle(rlist->rectangles[i].x, rlist->rectangles[i].y, rlist->rectangles[i].width, rlist->rectangles[i].height);
    }
    cr->clip();
    cairo_rectangle_list_destroy(rlist);

    // Draw background if solid colour optimisation is not enabled. (If enabled, it is baked into the stores.)
    if (!background_in_stores) {
        if (prefs.debug_framecheck) f = FrameCheck::Event("background");
        paint_background(view, pi, page, desk, cr);
    }

    // Even if in solid colour mode, draw the part of background that is not going to be rendered.
    if (background_in_stores) {
        auto const &s = stores.mode() == Stores::Mode::Decoupled ? stores.snapshot() : stores.store();
        if (!(Geom::Parallelogram(s.rect) * s.affine.inverse() * view.affine).contains(view.rect)) {
            if (prefs.debug_framecheck) f = FrameCheck::Event("background", 2);
            cr->save();
            cr->set_fill_rule(Cairo::Context::FillRule::EVEN_ODD);
            cr->rectangle(0, 0, view.rect.width(), view.rect.height());
            cr->translate(-view.rect.left(), -view.rect.top());
            cr->transform(geom_to_cairo(s.affine.inverse() * view.affine));
            cr->rectangle(s.rect.left(), s.rect.top(), s.rect.width(), s.rect.height());
            cr->clip();
            cr->transform(geom_to_cairo(view.affine.inverse() * s.affine));
            cr->translate(view.rect.left(), view.rect.top());
            paint_background(view, pi, page, desk, cr);
            cr->restore();
        }
    }

    auto draw_store = [&, this] (Cairo::RefPtr<Cairo::ImageSurface> const &store, Cairo::RefPtr<Cairo::ImageSurface> const &snapshot_store) {
        if (stores.mode() == Stores::Mode::Normal) {
            // Blit store to view.
            if (prefs.debug_framecheck) f = FrameCheck::Event("draw");
            cr->save();
            auto const &r = stores.store().rect;
            cr->translate(-view.rect.left(), -view.rect.top());
            cr->transform(geom_to_cairo(stores.store().affine.inverse() * view.affine)); // Almost always the identity.
            cr->rectangle(r.left(), r.top(), r.width(), r.height());
            cr->set_source(store, r.left(), r.top());
            Cairo::SurfacePattern(cr->get_source()->cobj()).set_filter(Cairo::SurfacePattern::Filter::FAST);
            cr->fill();
            cr->restore();
        } else {
            // Draw transformed snapshot, clipped to the complement of the store's clean region.
            if (prefs.debug_framecheck) f = FrameCheck::Event("composite", 1);

            cr->save();
            cr->set_fill_rule(Cairo::Context::FillRule::EVEN_ODD);
            cr->rectangle(0, 0, view.rect.width(), view.rect.height());
            cr->translate(-view.rect.left(), -view.rect.top());
            cr->transform(geom_to_cairo(stores.store().affine.inverse() * view.affine));
            region_to_path(cr, stores.store().drawn);
            cr->transform(geom_to_cairo(stores.snapshot().affine.inverse() * stores.store().affine));
            cr->clip();
            auto const &r = stores.snapshot().rect;
            cr->rectangle(r.left(), r.top(), r.width(), r.height());
            cr->clip();
            cr->set_source(snapshot_store, r.left(), r.top());
            Cairo::SurfacePattern(cr->get_source()->cobj()).set_filter(Cairo::SurfacePattern::Filter::FAST);
            cr->paint();
            if (prefs.debug_show_snapshot) {
                cr->set_source_rgba(0, 0, 1, 0.2);
                cr->set_operator(Cairo::Context::Operator::OVER);
                cr->paint();
            }
            cr->restore();

            // Draw transformed store, clipped to drawn region.
            if (prefs.debug_framecheck) f = FrameCheck::Event("composite", 0);
            cr->save();
            cr->translate(-view.rect.left(), -view.rect.top());
            cr->transform(geom_to_cairo(stores.store().affine.inverse() * view.affine));
            cr->set_source(store, stores.store().rect.left(), stores.store().rect.top());
            Cairo::SurfacePattern(cr->get_source()->cobj()).set_filter(Cairo::SurfacePattern::Filter::FAST);
            region_to_path(cr, stores.store().drawn);
            cr->fill();
            cr->restore();
        }
    };

    auto draw_overlay = [&, this] {
        // Get whitewash opacity.
        double outline_overlay_opacity = prefs.outline_overlay_opacity / 100.0;

        // Partially obscure drawing by painting semi-transparent white, then paint outline content.
        // Note: Unfortunately this also paints over the background, but this is unavoidable.
        cr->save();
        cr->set_operator(Cairo::Context::Operator::OVER);
        cr->set_source_rgb(1.0, 1.0, 1.0);
        cr->paint_with_alpha(outline_overlay_opacity);
        draw_store(store.outline_surface, snapshot.outline_surface);
        cr->restore();
    };

    if (a.splitmode == Inkscape::SplitMode::SPLIT) {

        // Calculate the clipping rectangles for split view.
        auto [store_clip, outline_clip] = calc_splitview_cliprects(view.rect.dimensions(), a.splitfrac, a.splitdir);

        // Draw normal content.
        cr->save();
        cr->rectangle(store_clip.left(), store_clip.top(), store_clip.width(), store_clip.height());
        cr->clip();
        cr->set_operator(background_in_stores ? Cairo::Context::Operator::SOURCE : Cairo::Context::Operator::OVER);
        draw_store(store.surface, snapshot.surface);
        if (a.render_mode == Inkscape::RenderMode::OUTLINE_OVERLAY) draw_overlay();
        cr->restore();

        // Draw outline.
        if (background_in_stores) {
            cr->save();
            cr->translate(outline_clip.left(), outline_clip.top());
            paint_background(Fragment{view.affine, view.rect.min() + outline_clip}, pi, page, desk, cr);
            cr->restore();
        }
        cr->save();
        cr->rectangle(outline_clip.left(), outline_clip.top(), outline_clip.width(), outline_clip.height());
        cr->clip();
        cr->set_operator(Cairo::Context::Operator::OVER);
        draw_store(store.outline_surface, snapshot.outline_surface);
        cr->restore();

    } else {

        // Draw the normal content over the whole view.
        cr->set_operator(background_in_stores ? Cairo::Context::Operator::SOURCE : Cairo::Context::Operator::OVER);
        draw_store(store.surface, snapshot.surface);
        if (a.render_mode == Inkscape::RenderMode::OUTLINE_OVERLAY) draw_overlay();

        // Draw outline if in X-ray mode.
        if (a.splitmode == Inkscape::SplitMode::XRAY && a.mouse) {
            // Clip to circle
            cr->set_antialias(Cairo::ANTIALIAS_DEFAULT);
            cr->arc(a.mouse->x(), a.mouse->y(), prefs.xray_radius, 0, 2 * M_PI);
            cr->clip();
            cr->set_antialias(Cairo::ANTIALIAS_NONE);
            // Draw background.
            paint_background(view, pi, page, desk, cr);
            // Draw outline.
            cr->set_operator(Cairo::Context::Operator::OVER);
            draw_store(store.outline_surface, snapshot.outline_surface);
        }
    }

    // The rest can be done with antialiasing.
    cr->set_antialias(Cairo::ANTIALIAS_DEFAULT);

    if (a.splitmode == Inkscape::SplitMode::SPLIT) {
        paint_splitview_controller(view.rect.dimensions(), a.splitfrac, a.splitdir, a.hoverdir, cr);
    }
}

} // namespace Widget
} // namespace UI
} // namespace Inkscape

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
