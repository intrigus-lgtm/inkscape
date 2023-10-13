// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "lpe-simplify.h"

#include <2geom/svg-path-parser.h>
#include <glibmm/i18n.h>
#include <gtkmm/box.h>
#include <gtkmm/entry.h>

#include "display/curve.h"
#include "helper/geom.h"
#include "object/sp-lpe-item.h"
#include "path/path-util.h"
#include "svg/svg.h"
#include "ui/icon-names.h"
#include "ui/pack.h"
#include "ui/tools/node-tool.h"
#include "ui/util.h"

namespace Inkscape::LivePathEffect {

LPESimplify::LPESimplify(LivePathEffectObject *lpeobject)
    : Effect(lpeobject)
    , steps(_("Repeat"), _("Change number of repeats of simplifying operation. Useful for complex paths that need to be significantly simplified. "), "steps", &wr, this, 1)
    , threshold(_("Complexity"), _("Drag slider to set the amount of simplification"), "threshold", &wr, this, 5)
    , smooth_angles(_("Smoothness"), _("Max degree difference on handles to perform smoothing"), "smooth_angles",
                    &wr, this, 360.)
    , helper_size(_("Handle size"), _("Size of the handles in the effect visualization (not editable)"), "helper_size", &wr, this, 10)
    , simplify_individual_paths(_("Paths separately"), _("When there are multiple paths in the selection, simplify each one separately."), "simplify_individual_paths",
                                &wr, this, false, "", INKSCAPE_ICON("on-outline"), INKSCAPE_ICON("off-outline"))
    , simplify_just_coalesce(_("Just coalesce"), _("Simplify just coalesce"), "simplify_just_coalesce", &wr, this,
                             false, "", INKSCAPE_ICON("on-outline"), INKSCAPE_ICON("off-outline"))
{
    registerParameter(&threshold);
    registerParameter(&steps);
    registerParameter(&smooth_angles);
    registerParameter(&helper_size);
    registerParameter(&simplify_individual_paths);
    registerParameter(&simplify_just_coalesce);

    threshold.addSlider(true);
    spinbutton_width_chars = 5;
    steps.addSlider(true);
    steps.param_set_range(1, 50);
    steps.param_set_increments(1, 1);
    steps.param_set_digits(0);

    smooth_angles.addSlider(true);
    smooth_angles.param_set_range(0.0, 360.0);
    smooth_angles.param_set_increments(1, 1);

    helper_size.addSlider(true);
    helper_size.param_set_range(0.0, 30);
    helper_size.param_set_increments(1, 1);
    helper_size.param_set_digits(2);
    setVersioningData();
    radius_helper_nodes = 6.0;
    apply_to_clippath_and_mask = true;
}

LPESimplify::~LPESimplify() = default;

void
LPESimplify::doBeforeEffect (SPLPEItem const* lpeitem)
{
    if(!hp.empty()) {
        hp.clear();
    }
    bbox = lpeitem->visualBounds();
    radius_helper_nodes = helper_size;
}

void
LPESimplify::setVersioningData()
{
    Glib::ustring version = lpeversion.param_getSVGValue();
    if (version < "1.3") {
        threshold.param_set_range(0.0001, Geom::infinity());
        threshold.param_set_increments(0.0001, 0.0001);
        threshold.param_set_digits(6);
        smooth_angles.param_set_digits(2);
    } else {
        threshold.param_set_range(0.01, 100.00);
        threshold.param_set_increments(0.1, 0.1);
        threshold.param_set_digits(2);
        smooth_angles.param_set_digits(0);
       
        threshold.param_set_no_leading_zeros();
        helper_size.param_set_no_leading_zeros();
        smooth_angles.param_set_no_leading_zeros();
    }
}

void
LPESimplify::doOnApply(SPLPEItem const* lpeitem)
{
    lpeversion.param_setValue("1.3", true);
    setVersioningData();
}

Gtk::Widget *
LPESimplify::newWidget()
{
    // use manage here, because after deletion of Effect object, others might still be pointing to this widget.
    auto const vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    vbox->property_margin().set_value(5);

    auto const buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);

    for (auto const param: param_vector) {
        if (!param->widget_is_visible) continue;

        auto const widg = param->param_newWidget();
        if (!widg) continue;

        if (param->param_key == "simplify_just_coalesce") {
            continue;
        }

        if (param->param_key == "simplify_individual_paths") {
            UI::pack_start(*buttons, *widg, true, true, 2);
        } else {
            auto &horizontal_box = dynamic_cast<Gtk::Box &>(*widg);
            auto const child_list = UI::get_children(horizontal_box);
            auto &entry = dynamic_cast<Gtk::Entry &>(*child_list.at(1));
            entry.set_width_chars(8);

            UI::pack_start(*vbox, *widg, true, true, 2);
        }

        if (auto const tip = param->param_getTooltip()) {
            widg->set_tooltip_markup(*tip);
        } else {
            widg->set_tooltip_text({});
            widg->set_has_tooltip(false);
        }
    }

    buttons->set_halign(Gtk::Align::START);
    UI::pack_start(*vbox, *buttons, true, true, 2);
    return vbox;
}

void
LPESimplify::doEffect(SPCurve *curve)
{
    Geom::PathVector const original_pathv = pathv_to_linear_and_cubic_beziers(curve->get_pathvector());
    auto pathliv = Path_for_pathvector(original_pathv);

    double size = Geom::L2(bbox->dimensions());
    if (simplify_individual_paths) {
        size = Geom::L2(Geom::bounds_fast(original_pathv)->dimensions());
    }
    size /= sp_lpe_item->i2doc_affine().descrim();

    auto const &version = lpeversion.param_getSVGValue();
    int const factor = version < "1.3" ? 1 : 10000;

    for (auto i = 0u; i < steps; ++i) {
        if (simplify_just_coalesce) {
            pathliv->Coalesce((threshold / factor) * size);
        } else {
            pathliv->ConvertEvenLines((threshold / factor) * size);
            pathliv->Simplify((threshold / factor) * size);
        }
    }

    auto result = pathliv->MakePathVector();
    generateHelperPathAndSmooth(result);
    curve->set_pathvector(result);
    update_helperpath();
}

void
LPESimplify::generateHelperPathAndSmooth(Geom::PathVector &result)
{
    if(steps < 1) {
        return;
    }
    Geom::PathVector tmp_path;
    Geom::CubicBezier const *cubic = nullptr;
    for (auto & path_it : result) {
        if (path_it.empty()) {
            continue;
        }

        Geom::Path::iterator curve_it1 = path_it.begin(); // incoming curve
        Geom::Path::iterator curve_it2 = ++(path_it.begin());// outgoing curve
        Geom::Path::iterator curve_endit = path_it.end_default(); // this determines when the loop has to stop
        SPCurve *nCurve = new SPCurve();
        if (path_it.closed()) {
            // if the path is closed, maybe we have to stop a bit earlier because the
            // closing line segment has zerolength.
            const Geom::Curve &closingline =
                path_it.back_closed(); // the closing line segment is always of type
            // Geom::LineSegment.
            if (are_near(closingline.initialPoint(), closingline.finalPoint())) {
                // closingline.isDegenerate() did not work, because it only checks for
                // *exact* zero length, which goes wrong for relative coordinates and
                // rounding errors...
                // the closing line segment has zero-length. So stop before that one!
                curve_endit = path_it.end_open();
            }
        }
        if(helper_size > 0) {
            drawNode(curve_it1->initialPoint());
        }
        nCurve->moveto(curve_it1->initialPoint());
        Geom::Point start = Geom::Point(0,0);
        while (curve_it1 != curve_endit) {
            cubic = dynamic_cast<Geom::CubicBezier const *>(&*curve_it1);
            Geom::Point point_at1 = curve_it1->initialPoint();
            Geom::Point point_at2 = curve_it1->finalPoint();
            Geom::Point point_at3 = curve_it1->finalPoint();
            Geom::Point point_at4 = curve_it1->finalPoint();

            if(start == Geom::Point(0,0)) {
                start = point_at1;
            }

            if (cubic) {
                point_at1 = (*cubic)[1];
                point_at2 = (*cubic)[2];
            }

            if(path_it.closed() && curve_it2 == curve_endit) {
                point_at4 = start;
            }
            if(curve_it2 != curve_endit) {
                cubic = dynamic_cast<Geom::CubicBezier const *>(&*curve_it2);
                if (cubic) {
                    point_at4 = (*cubic)[1];
                }
            }
            Geom::Ray ray1(point_at2, point_at3);
            Geom::Ray ray2(point_at3, point_at4);
            double angle1 = Geom::deg_from_rad(ray1.angle());
            double angle2 = Geom::deg_from_rad(ray2.angle());
            if((smooth_angles  >= std::abs(angle2 - angle1)) && !are_near(point_at4,point_at3) && !are_near(point_at2,point_at3)) {
                double dist = Geom::distance(point_at2,point_at3);
                Geom::Angle angleFixed = ray2.angle();
                angleFixed -= Geom::Angle::from_degrees(180.0);
                point_at2 =  Geom::Point::polar(angleFixed, dist) + point_at3;
            }
            nCurve->curveto(point_at1, point_at2, curve_it1->finalPoint());
            cubic = dynamic_cast<Geom::CubicBezier const *>(nCurve->last_segment());
            if (cubic) {
                point_at1 = (*cubic)[1];
                point_at2 = (*cubic)[2];
                if(helper_size > 0) {
                    if(!are_near((*cubic)[0],(*cubic)[1])) {
                        drawHandle((*cubic)[1]);
                        drawHandleLine((*cubic)[0],(*cubic)[1]);
                    }
                    if(!are_near((*cubic)[3],(*cubic)[2])) {
                        drawHandle((*cubic)[2]);
                        drawHandleLine((*cubic)[3],(*cubic)[2]);
                    }
                }
            }
            if(helper_size > 0) {
                drawNode(curve_it1->finalPoint());
            }
            ++curve_it1;
            ++curve_it2;
        }
        if (path_it.closed()) {
            nCurve->closepath_current();
        }
        tmp_path.push_back(nCurve->get_pathvector()[0]);
        nCurve->reset();
        delete nCurve;
    }
    result = tmp_path;
}

void
LPESimplify::drawNode(Geom::Point p)
{
    double r = radius_helper_nodes;
    char const * svgd;
    svgd = "M 0.55,0.5 A 0.05,0.05 0 0 1 0.5,0.55 0.05,0.05 0 0 1 0.45,0.5 0.05,0.05 0 0 1 0.5,0.45 0.05,0.05 0 0 1 0.55,0.5 Z M 0,0 1,0 1,1 0,1 Z";
    Geom::PathVector pathv = sp_svg_read_pathv(svgd);
    pathv *= Geom::Scale(r) * Geom::Translate(p - Geom::Point(0.5*r,0.5*r));
    hp.push_back(pathv[0]);
    hp.push_back(pathv[1]);
}

void
LPESimplify::drawHandle(Geom::Point p)
{
    double r = radius_helper_nodes;
    char const * svgd;
    svgd = "M 0.7,0.35 A 0.35,0.35 0 0 1 0.35,0.7 0.35,0.35 0 0 1 0,0.35 0.35,0.35 0 0 1 0.35,0 0.35,0.35 0 0 1 0.7,0.35 Z";
    Geom::PathVector pathv = sp_svg_read_pathv(svgd);
    pathv *= Geom::Scale(r) * Geom::Translate(p - Geom::Point(0.35*r,0.35*r));
    hp.push_back(pathv[0]);
}

void
LPESimplify::drawHandleLine(Geom::Point p,Geom::Point p2)
{
    Geom::Path path;
    path.start( p );
    double diameter = radius_helper_nodes;
    if(helper_size > 0 && Geom::distance(p,p2) > (diameter * 0.35)) {
        Geom::Ray ray2(p, p2);
        p2 =  p2 - Geom::Point::polar(ray2.angle(),(diameter * 0.35));
    }
    path.appendNew<Geom::LineSegment>( p2 );
    hp.push_back(path);
}

void
LPESimplify::addCanvasIndicators(SPLPEItem const */*lpeitem*/, std::vector<Geom::PathVector> &hp_vec)
{
    hp_vec.push_back(hp);
}

} // namespace Inkscape::LivePathEffect

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
