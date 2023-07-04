// SPDX-License-Identifier: GPL-2.0-or-later
#include "updaters.h"
#include "ui/util.h"

namespace Inkscape {
namespace UI {
namespace Widget {

class ResponsiveUpdater : public Updater
{
public:
    Strategy get_strategy() const override { return Strategy::Responsive; }

    void reset()                                             override { clean_region = Cairo::Region::create(); }
    void intersect (Geom::IntRect const &rect)               override { clean_region->intersect(geom_to_cairo(rect)); }
    void mark_dirty(Geom::IntRect const &rect)               override { clean_region->subtract(geom_to_cairo(rect)); }
    void mark_dirty(Cairo::RefPtr<Cairo::Region> const &reg) override { clean_region->subtract(reg); }
    void mark_clean(Geom::IntRect const &rect)               override { clean_region->do_union(geom_to_cairo(rect)); }

    Cairo::RefPtr<Cairo::Region> get_next_clean_region() override { return clean_region; }
    bool                         report_finished      () override { return false; }
    void                         next_frame           () override {}
};

class FullRedrawUpdater : public ResponsiveUpdater
{
    // Whether we are currently in the middle of a redraw.
    bool inprogress = false;

    // Contains a copy of the old clean region if damage events occurred during the current redraw, otherwise null.
    Cairo::RefPtr<Cairo::Region> old_clean_region;

public:
    Strategy get_strategy() const override { return Strategy::FullRedraw; }

    void reset() override
    {
        ResponsiveUpdater::reset();
        inprogress = false;
        old_clean_region.reset();
    }

    void intersect(const Geom::IntRect &rect) override
    {
        ResponsiveUpdater::intersect(rect);
        if (old_clean_region) old_clean_region->intersect(geom_to_cairo(rect));
    }

    void mark_dirty(Geom::IntRect const &rect) override
    {
        if (inprogress && !old_clean_region) old_clean_region = clean_region->copy();
        ResponsiveUpdater::mark_dirty(rect);
    }

    void mark_dirty(const Cairo::RefPtr<Cairo::Region> &reg) override
    {
        if (inprogress && !old_clean_region) old_clean_region = clean_region->copy();
        ResponsiveUpdater::mark_dirty(reg);
    }

    void mark_clean(const Geom::IntRect &rect) override
    {
        ResponsiveUpdater::mark_clean(rect);
        if (old_clean_region) old_clean_region->do_union(geom_to_cairo(rect));
    }

    Cairo::RefPtr<Cairo::Region> get_next_clean_region() override
    {
        inprogress = true;
        if (!old_clean_region) {
            return clean_region;
        } else {
            return old_clean_region;
        }
    }

    bool report_finished() override
    {
        assert(inprogress);
        if (!old_clean_region) {
            // Completed redraw without being damaged => finished.
            inprogress = false;
            return false;
        } else {
            // Completed redraw but damage events arrived => ask for another redraw, using the up-to-date clean region.
            old_clean_region.reset();
            return true;
        }
    }
};

class MultiscaleUpdater : public ResponsiveUpdater
{
    // Whether we are currently in the middle of a redraw.
    bool inprogress = false;

    // Whether damage events occurred during the current redraw.
    bool activated = false;

    int counter; // A steadily incrementing counter from which the current scale is derived.
    int scale; // The current scale to process updates at.
    int elapsed; // How much time has been spent at the current scale.
    std::vector<Cairo::RefPtr<Cairo::Region>> blocked; // The region blocked from being updated at each scale.

public:
    Strategy get_strategy() const override { return Strategy::Multiscale; }

    void reset() override
    {
        ResponsiveUpdater::reset();
        inprogress = activated = false;
    }

    void intersect(const Geom::IntRect &rect) override
    {
        ResponsiveUpdater::intersect(rect);
        if (activated) {
            for (auto &reg : blocked) {
                reg->intersect(geom_to_cairo(rect));
            }
        }
    }

    void mark_dirty(Geom::IntRect const &rect) override
    {
        ResponsiveUpdater::mark_dirty(rect);
        post_mark_dirty();
    }

    void mark_dirty(const Cairo::RefPtr<Cairo::Region> &reg) override
    {
        ResponsiveUpdater::mark_dirty(reg);
        post_mark_dirty();
    }

    void post_mark_dirty()
    {
        if (inprogress && !activated) {
            counter = scale = elapsed = 0;
            blocked = { Cairo::Region::create() };
            activated = true;
        }
    }

    void mark_clean(const Geom::IntRect &rect) override
    {
        ResponsiveUpdater::mark_clean(rect);
        if (activated) blocked[scale]->do_union(geom_to_cairo(rect));
    }

    Cairo::RefPtr<Cairo::Region> get_next_clean_region() override
    {
        inprogress = true;
        if (!activated) {
            return clean_region;
        } else {
            auto result = clean_region->copy();
            result->do_union(blocked[scale]);
            return result;
        }
    }

    bool report_finished() override
    {
        assert(inprogress);
        if (!activated) {
            // Completed redraw without damage => finished.
            inprogress = false;
            return false;
        } else {
            // Completed redraw but damage events arrived => begin updating any remaining damaged regions.
            activated = false;
            blocked.clear();
            return true;
        }
    }

    void next_frame() override
    {
        if (!activated) return;

        // Stay at the current scale for 2^scale frames.
        elapsed++;
        if (elapsed < (1 << scale)) return;
        elapsed = 0;

        // Adjust the counter, which causes scale to hop around the values 0, 1, 2... spending half as much time at each subsequent scale.
        counter++;
        scale = 0;
        for (int tmp = counter; tmp % 2 == 1; tmp /= 2) {
            scale++;
        }

        // Ensure sufficiently many blocked zones exist.
        if (scale == blocked.size()) {
            blocked.emplace_back();
        }

        // Recreate the current blocked zone as the union of the clean region and lower-scale blocked zones.
        blocked[scale] = clean_region->copy();
        for (int i = 0; i < scale; i++) {
            blocked[scale]->do_union(blocked[i]);
        }
    }
};

template<> std::unique_ptr<Updater> Updater::create<Updater::Strategy::Responsive>() {return std::make_unique<ResponsiveUpdater>();}
template<> std::unique_ptr<Updater> Updater::create<Updater::Strategy::FullRedraw>() {return std::make_unique<FullRedrawUpdater>();}
template<> std::unique_ptr<Updater> Updater::create<Updater::Strategy::Multiscale>() {return std::make_unique<MultiscaleUpdater>();}

std::unique_ptr<Updater> Updater::create(Strategy strategy)
{
    switch (strategy)
    {
        case Strategy::Responsive: return create<Strategy::Responsive>();
        case Strategy::FullRedraw: return create<Strategy::FullRedraw>();
        case Strategy::Multiscale: return create<Strategy::Multiscale>();
        default: return nullptr; // Never triggered, but GCC errors out on build without.
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
