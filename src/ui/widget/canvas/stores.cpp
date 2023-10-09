// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Abstraction of the store/snapshot mechanism.
 */
/*
 * Copyright (C) 2022 PBS <pbs3141@gmail.com>
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "stores.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <utility>
#include <vector>
#include <2geom/transforms.h>
#include <2geom/parallelogram.h>
#include <2geom/point.h>
#include <2geom/convex-hull.h>

#include "graphics.h"
#include "helper/geom.h"
#include "prefs.h"

namespace Inkscape::UI::Widget {

namespace {

// Determine whether an affine transformation approximately maps the unit square [0, 1]^2 to itself.
bool preserves_unitsquare(Geom::Affine const &affine)
{
    return approx_dihedral(Geom::Translate(0.5, 0.5) * affine * Geom::Translate(-0.5, -0.5));
}

// Apply an affine transformation to a region, then return a strictly smaller region approximating it, made from chunks of size roughly d.
// To reduce computation, only the intersection of the result with bounds will be valid.
auto region_affine_approxinwards(Cairo::RefPtr<Cairo::Region> const &reg, Geom::Affine const &affine, Geom::IntRect const &bounds, int d = 200)
{
    // Trivial empty case.
    if (reg->empty()) return Cairo::Region::create();

    // Trivial identity case.
    if (affine.isIdentity(0.001)) return reg->copy();

    // Fast-path for rectilinear transformations.
    if (affine.withoutTranslation().isScale(0.001)) {
        auto regdst = Cairo::Region::create();

        auto transform = [&] (const Geom::IntPoint &p) {
            return (Geom::Point(p) * affine).round();
        };

        for (int i = 0; i < reg->get_num_rectangles(); i++) {
            auto rect = cairo_to_geom(reg->get_rectangle(i));
            regdst->do_union(geom_to_cairo(Geom::IntRect(transform(rect.min()), transform(rect.max()))));
        }

        return regdst;
    }

    // General case.
    auto ext = cairo_to_geom(reg->get_extents());
    auto rectdst = ((Geom::Parallelogram(ext) * affine).bounds().roundOutwards() & bounds).regularized();
    if (!rectdst) return Cairo::Region::create();
    auto rectsrc = (Geom::Parallelogram(*rectdst) * affine.inverse()).bounds().roundOutwards();

    auto regdst = Cairo::Region::create(geom_to_cairo(*rectdst));
    auto regsrc = Cairo::Region::create(geom_to_cairo(rectsrc));
    regsrc->subtract(reg);

    double fx = min(absolute(Geom::Point(1.0, 0.0) * affine.withoutTranslation()));
    double fy = min(absolute(Geom::Point(0.0, 1.0) * affine.withoutTranslation()));

    for (int i = 0; i < regsrc->get_num_rectangles(); i++) {
        auto rect = cairo_to_geom(regsrc->get_rectangle(i));
        int nx = std::ceil(rect.width()  * fx / d);
        int ny = std::ceil(rect.height() * fy / d);
        auto pt = [&] (int x, int y) {
            return rect.min() + (rect.dimensions() * Geom::IntPoint(x, y)) / Geom::IntPoint(nx, ny);
        };
        for (int x = 0; x < nx; x++) {
            for (int y = 0; y < ny; y++) {
                auto r = Geom::IntRect(pt(x, y), pt(x + 1, y + 1));
                auto r2 = (Geom::Parallelogram(r) * affine).bounds().roundOutwards();
                regdst->subtract(geom_to_cairo(r2));
            }
        }
    }

    return regdst;
}

} // namespace

Geom::IntRect Stores::centered(Fragment const &view) const
{
    // Return the visible region of the view, plus the prerender and padding margins.
    return expandedBy(view.rect, _prefs.prerender + _prefs.padding);
}

void Stores::recreate_store(Fragment const &view)
{
    // Recreate the store at the view's affine.
    _store.affine = view.affine;
    _store.rect = centered(view);
    _store.drawn = Cairo::Region::create();
    // Tell the graphics to create a blank new store.
    _graphics->recreate_store(_store.rect.dimensions());
}

void Stores::shift_store(Fragment const &view)
{
    // Create a new fragment centred on the viewport.
    auto rect = centered(view);
    // Tell the graphics to copy the drawn part of the old store to the new store.
    _graphics->shift_store(Fragment{ _store.affine, rect });
    // Set the shifted store as the new store.
    _store.rect = rect;
    // Clip the drawn region to the new store.
    _store.drawn->intersect(geom_to_cairo(_store.rect));
};

void Stores::take_snapshot(Fragment const &view)
{
    // Copy the store to the snapshot, leaving us temporarily in an invalid state.
    _snapshot = std::move(_store);
    // Tell the graphics to do the same, except swapping them so we can re-use the old snapshot store.
    _graphics->swap_stores();
    // Reset the store.
    recreate_store(view);
    // Transform the snapshot's drawn region to the new store's affine.
    _snapshot.drawn = shrink_region(region_affine_approxinwards(_snapshot.drawn, _snapshot.affine.inverse() * _store.affine, _store.rect), 4, -2);
}

void Stores::snapshot_combine(Fragment const &view)
{
    // Add the drawn region to the snapshot drawn region (they both exist in store space, so this is valid), and save its affine.
    _snapshot.drawn->do_union(_store.drawn);
    auto old_store_affine = _store.affine;

    // Get the list of corner points in the store's drawn region and the snapshot bounds rect, all at the view's affine.
    std::vector<Geom::Point> pts;
    auto add_rect = [&, this] (Geom::Parallelogram const &pl) {
        for (int i = 0; i < 4; i++) {
            pts.emplace_back(Geom::Point(pl.corner(i)));
        }
    };
    auto add_store = [&, this] (Store const &s) {
        int nrects = s.drawn->get_num_rectangles();
        auto affine = s.affine.inverse() * view.affine;
        for (int i = 0; i < nrects; i++) {
            add_rect(Geom::Parallelogram(cairo_to_geom(s.drawn->get_rectangle(i))) * affine);
        }
    };
    add_store(_store);
    add_rect(Geom::Parallelogram(_snapshot.rect) * _snapshot.affine.inverse() * view.affine);

    // Compute their minimum-area bounding box as a fragment - an (affine, rect) pair.
    auto const [rot, optrect] = Geom::ConvexHull(pts).minAreaRotation();
    auto rect = *optrect; // non-empty since pts is non-empty
    auto affine = view.affine * rot;

    // Check if the paste transform takes the snapshot store exactly onto the new fragment, possibly with a dihedral transformation.
    auto paste = Geom::Scale(_snapshot.rect.dimensions())
               * Geom::Translate(_snapshot.rect.min())
               * _snapshot.affine.inverse()
               * affine
               * Geom::Translate(-rect.min())
               * Geom::Scale(rect.dimensions()).inverse();
    if (preserves_unitsquare(paste)) {
        // If so, simply take the new fragment to be exactly the same as the snapshot store.
        rect   = _snapshot.rect;
        affine = _snapshot.affine;
    }

    // Compute the scale difference between the backing store and the new fragment, giving the amount of detail that would be lost by pasting.
    if ( double scale_ratio = std::sqrt(std::abs(_store.affine.det() / affine.det()));
                scale_ratio > 4.0 )
    {
        // Zoom the new fragment in to increase its quality.
        double grow = scale_ratio / 2.0;
        rect   *= Geom::Scale(grow);
        affine *= Geom::Scale(grow);
    }

    // Do not allow the fragment to become more detailed than the window.
    if ( double scale_ratio = std::sqrt(std::abs(affine.det() / view.affine.det()));
                scale_ratio > 1.0 )
    {
        // Zoom the new fragment out to reduce its quality.
        double shrink = 1.0 / scale_ratio;
        rect   *= Geom::Scale(shrink);
        affine *= Geom::Scale(shrink);
    }

    // Find the bounding rect of the visible region + prerender margin within the new fragment. We do not want to discard this content in the next clipping step.
    auto renderable = (Geom::Parallelogram(expandedBy(view.rect, _prefs.prerender)) * view.affine.inverse() * affine).bounds() & rect;

    // Cap the dimensions of the new fragment to slightly larger than the maximum dimension of the window by clipping it towards the screen centre. (Lower in Cairo mode since otherwise too slow to cope.)
    double max_dimension = max(view.rect.dimensions()) * (_graphics->is_opengl() ? 1.7 : 0.8);
    auto dimens = rect.dimensions();
    dimens.x() = std::min(dimens.x(), max_dimension);
    dimens.y() = std::min(dimens.y(), max_dimension);
    auto center = Geom::Rect(view.rect).midpoint() * view.affine.inverse() * affine;
    center.x() = Util::safeclamp(center.x(), rect.left() + dimens.x() * 0.5, rect.right()  - dimens.x() * 0.5);
    center.y() = Util::safeclamp(center.y(), rect.top()  + dimens.y() * 0.5, rect.bottom() - dimens.y() * 0.5);
    rect = Geom::Rect(center - dimens * 0.5, center + dimens * 0.5);

    // Ensure the new fragment contains the renderable rect from earlier, enlarging it and reducing resolution if necessary.
    if (!rect.contains(renderable)) {
        auto oldrect = rect;
        rect.unionWith(renderable);
        double shrink = 1.0 / std::max(rect.width() / oldrect.width(), rect.height() / oldrect.height());
        rect   *= Geom::Scale(shrink);
        affine *= Geom::Scale(shrink);
    }

    // Calculate the paste transform from the snapshot store to the new fragment (again).
    paste = Geom::Scale(_snapshot.rect.dimensions())
          * Geom::Translate(_snapshot.rect.min())
          * _snapshot.affine.inverse()
          * affine
          * Geom::Translate(-rect.min())
          * Geom::Scale(rect.dimensions()).inverse();

    if (_prefs.debug_logging) std::cout << "New fragment dimensions " << rect.width() << ' ' << rect.height() << std::endl;

    if (paste.isIdentity(0.001) && rect.dimensions().round() == _snapshot.rect.dimensions()) {
        // Fast path: simply paste the backing store onto the snapshot store.
        if (_prefs.debug_logging) std::cout << "Fast snapshot combine" << std::endl;
        _graphics->fast_snapshot_combine();
    } else {
        // General path: paste the snapshot store and then the backing store onto a new fragment, then set that as the snapshot store.
        auto frag_rect = rect.roundOutwards();
        _graphics->snapshot_combine(Fragment{ affine, frag_rect });
        _snapshot.rect = frag_rect;
        _snapshot.affine = affine;
    }

    // Start drawing again on a new blank store aligned to the screen.
    recreate_store(view);
    // Transform the snapshot clean region to the new store.
    // Todo: Should really clip this to the new snapshot rect, only we can't because it's generally not aligned with the store's affine.
    _snapshot.drawn = shrink_region(region_affine_approxinwards(_snapshot.drawn, old_store_affine.inverse() * _store.affine, _store.rect), 4, -2);
};

void Stores::reset()
{
    _mode = Mode::None;
    _store.drawn.reset();
    _snapshot.drawn.reset();
}

// Handle transitions and actions in response to viewport changes.
auto Stores::update(Fragment const &view) -> Action
{
    switch (_mode) {
        
        case Mode::None: {
            // Not yet initialised or just reset - create store for first time.
            recreate_store(view);
            _mode = Mode::Normal;
            if (_prefs.debug_logging) std::cout << "Full reset" << std::endl;
            return Action::Recreated;
        }
        
        case Mode::Normal: {
            auto result = Action::None;
            // Enter decoupled mode if the affine has changed from what the store was drawn at.
            if (view.affine != _store.affine) {
                // Snapshot and reset the store.
                take_snapshot(view);
                // Enter decoupled mode.
                _mode = Mode::Decoupled;
                if (_prefs.debug_logging) std::cout << "Enter decoupled mode" << std::endl;
                result = Action::Recreated;
            } else {
                // Determine whether the view has moved sufficiently far that we need to shift the store.
                if (!_store.rect.contains(expandedBy(view.rect, _prefs.prerender))) {
                    // The visible region + prerender margin has reached the edge of the store.
                    if (!(cairo_to_geom(_store.drawn->get_extents()) & expandedBy(view.rect, _prefs.prerender + _prefs.padding)).regularized()) {
                        // If the store contains no reusable content at all, recreate it.
                        recreate_store(view);
                        if (_prefs.debug_logging) std::cout << "Recreate store" << std::endl;
                        result = Action::Recreated;
                    } else {
                        // Otherwise shift it.
                        shift_store(view);
                        if (_prefs.debug_logging) std::cout << "Shift store" << std::endl;
                        result = Action::Shifted;
                    }
                }
            }
            // After these operations, the store should now contain the visible region + prerender margin.
            assert(_store.rect.contains(expandedBy(view.rect, _prefs.prerender)));
            return result;
        }
        
        case Mode::Decoupled: {
            // Completely cancel the previous redraw and start again if the viewing parameters have changed too much.
            auto check_restart_redraw = [&, this] {
                // With this debug feature on, redraws should never be restarted.
                if (_prefs.debug_sticky_decoupled) return false;

                // Restart if the store is no longer covering the middle 50% of the screen. (Usually triggered by rotating or zooming out.)
                auto pl = Geom::Parallelogram(view.rect);
                pl *= Geom::Translate(-pl.midpoint()) * Geom::Scale(0.5) * Geom::Translate(pl.midpoint());
                pl *= view.affine.inverse() * _store.affine;
                if (!Geom::Parallelogram(_store.rect).contains(pl)) {
                    if (_prefs.debug_logging) std::cout << "Restart redraw (store not fully covering screen)" << std::endl;
                    return true;
                }

                // Also restart if zoomed in or out too much.
                auto scale_ratio = std::abs(view.affine.det() / _store.affine.det());
                if (scale_ratio > 3.0 || scale_ratio < 0.7) {
                    // Todo: Un-hard-code these thresholds.
                    //  * The threshold 3.0 is for zooming in. It says that if the quality of what is being redrawn is more than 3x worse than that of the screen, restart. This is necessary to ensure acceptably high resolution is kept as you zoom in.
                    //  * The threshold 0.7 is for zooming out. It says that if the quality of what is being redrawn is too high compared to the screen, restart. This prevents wasting time redrawing the screen slowly, at too high a quality that will probably not ever be seen.
                    if (_prefs.debug_logging) std::cout << "Restart redraw (zoomed changed too much)" << std::endl;
                    return true;
                }

                // Don't restart.
                return false;
            };

            if (check_restart_redraw()) {
                // Re-use as much content as possible from the store and the snapshot, and set as the new snapshot.
                snapshot_combine(view);
                return Action::Recreated;
            }

            return Action::None;
        }
        
        default: {
            assert(false);
            return Action::None;
        }
    }
}

auto Stores::finished_draw(Fragment const &view) -> Action
{
    // Finished drawing. Handle transitions out of decoupled mode, by checking if we need to reset the store to the correct affine.
    if (_mode == Mode::Decoupled) {
        if (_prefs.debug_sticky_decoupled) {
            // Debug feature: stop redrawing, but stay in decoupled mode.
        } else if (_store.affine == view.affine) {
            // Store is at the correct affine - exit decoupled mode.
            if (_prefs.debug_logging) std::cout << "Exit decoupled mode" << std::endl;
            // Exit decoupled mode.
            _mode = Mode::Normal;
            _graphics->invalidate_snapshot();
        } else {
            // Content is rendered at the wrong affine - take a new snapshot and continue idle process to continue rendering at the new affine.
            // Snapshot and reset the backing store.
            take_snapshot(view);
            if (_prefs.debug_logging) std::cout << "Remain in decoupled mode" << std::endl;
            return Action::Recreated;
        }
    }

    return Action::None;
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
