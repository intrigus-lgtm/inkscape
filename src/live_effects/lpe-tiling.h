// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * LPE <tiling> implementation
 */
/*
 * Authors:
 *   Jabiertxo Arraiza Cenoz <jabier.arraiza@marker.es>
 *   Adam Belis <>
 * Copyright (C) Authors 2022-2022
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_LPE_TILING_H
#define INKSCAPE_LPE_TILING_H

#include <vector>

#include "live_effects/effect.h"
#include "live_effects/lpegroupbbox.h"
#include "live_effects/parameter/enum.h"
#include "live_effects/parameter/parameter.h"
#include "live_effects/parameter/point.h"
#include "live_effects/parameter/satellitearray.h"
#include "live_effects/parameter/text.h"
#include "live_effects/parameter/unit.h"
#include "live_effects/parameter/random.h"
// this is only to fillrule
#include "livarot/Shape.h"

namespace Gtk {
class Box;
class Widget;
} // namespace Gtk

class KnotHolder;

namespace Inkscape::LivePathEffect {

namespace CoS {
// we need a separate namespace to avoid clashes with other LPEs
class KnotHolderEntityCopyGapX;
class KnotHolderEntityCopyGapY;
} // namespace CoS

typedef FillRule FillRuleBool;

class LPETiling final : public Effect, GroupBBoxEffect {
public:
    LPETiling(LivePathEffectObject *lpeobject);
    ~LPETiling() final;
    void doOnApply (SPLPEItem const* lpeitem) final;
    Geom::PathVector doEffect_path (Geom::PathVector const & path_in) final;
    void doBeforeEffect (SPLPEItem const* lpeitem) final;
    void doAfterEffect (SPLPEItem const* lpeitem, SPCurve *curve) final;
    void split(Geom::PathVector &path_in, Geom::Path const &divider);
    void resetDefaults(SPItem const* item) final;
    void doOnRemove(SPLPEItem const* /*lpeitem*/) final;
    bool doOnOpen(SPLPEItem const * /*lpeitem*/) final;
    void doOnVisibilityToggled(SPLPEItem const* /*lpeitem*/) final;
    Gtk::Widget * newWidget() final;
    void cloneStyle(SPObject *orig, SPObject *dest);
    Geom::PathVector doEffect_path_post (Geom::PathVector const & path_in, FillRuleBool fillrule);
    SPItem * toItem(size_t i, bool reset, bool &write);
    void cloneD(SPObject *orig, SPObject *dest);
    Inkscape::XML::Node * createPathBase(SPObject *elemref);
    friend class CoS::KnotHolderEntityCopyGapX;
    friend class CoS::KnotHolderEntityCopyGapY;
    void addKnotHolderEntities(KnotHolder * knotholder, SPItem * item) final;

protected:
    void addCanvasIndicators(SPLPEItem const *lpeitem, std::vector<Geom::PathVector> &hp_vec) final;
    KnotHolder *_knotholder;
    double gapx_unit = 0;
    double gapy_unit = 0;
    double offset_unit = 0;

private:
    void setOffsetCols();
    void setOffsetRows();
    void setScaleInterpolate(bool x, bool y);
    void setRotateInterpolate(bool x, bool y);
    void setScaleRandom();
    void setRotateRandom();
    void setGapXMode(bool random);
    void setGapYMode(bool random);
    bool getActiveMirror(int index);
    double end_scale(double scale_fix, bool tomax) const;
    bool _updating = false;
    void setMirroring(int index);
    Glib::ustring getMirrorMap(int index);
    void generate_buttons(Gtk::Box *container, Gtk::ToggleButton *&group, int pos);
    UnitParam unit;
    SatelliteArrayParam lpesatellites;
    ScalarParam gapx;
    ScalarParam gapy;
    ScalarParam num_rows;
    ScalarParam num_cols;
    ScalarParam rotate;
    ScalarParam scale;
    ScalarParam offset;
    BoolParam offset_type;
    BoolParam random_scale;
    BoolParam random_rotate;
    BoolParam random_gap_x;
    BoolParam random_gap_y;
    RandomParam seed;
    BoolParam interpolate_scalex;
    BoolParam interpolate_rotatex;
    BoolParam interpolate_scaley;
    BoolParam interpolate_rotatey;
    BoolParam mirrorrowsx;
    BoolParam mirrorrowsy;
    BoolParam mirrorcolsx;
    BoolParam mirrorcolsy;
    BoolParam mirrortrans;
    BoolParam split_items;
    BoolParam link_styles;
    BoolParam shrink_interp;
    HiddenParam transformorigin;
    double original_width = 0;
    double original_height = 0;
    Geom::OptRect gap_bbox;
    Geom::OptRect originalbbox;
    double prev_num_cols;
    double prev_num_rows;
    bool reset;
    gdouble scaleok = 1.0;
    Glib::ustring display_unit;
    Glib::ustring prev_unit = "px";
    bool legacy = false;
    std::vector<double> random_x;
    std::vector<double> random_y;
    std::vector<double> random_s;
    std::vector<double> random_r;
    Geom::Affine affinebase = Geom::identity();
    Geom::Affine transformoriginal = Geom::identity();
    Geom::Affine hideaffine = Geom::identity();
    Geom::Affine originatrans = Geom::identity();
    bool prev_split = false;
    SPObject *container;
    LPETiling(const LPETiling&) = delete;
    LPETiling& operator=(const LPETiling&) = delete;
};

} // namespace Inkscape::LivePathEffect

#endif // INKSCAPE_LPE_TILING_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-gaps:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
