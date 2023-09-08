// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <glibmm/i18n.h>
#include <glibmm/ustring.h>                 // for operator==, ustring
#include <gtkmm/box.h>                      // for Box
#include <gtkmm/button.h>                   // for Button
#include <gtkmm/entry.h>                    // for Entry
#include <gtkmm/enums.h>                    // for Orientation
#include <gtkmm/widget.h>                   // for Widget

#include "display/curve.h"
#include "live_effects/lpe-bspline.h"
#include "object/sp-path.h"
#include "preferences.h"
#include "svg/svg.h"
#include "ui/pack.h"
#include "ui/util.h"
#include "ui/widget/scalar.h"

namespace Inkscape::LivePathEffect {

static constexpr double BSPLINE_TOL = 0.001;
static constexpr double NO_POWER = 0.0;
static constexpr double DEFAULT_START_POWER = 1.0 / 3.0;
static constexpr double DEFAULT_END_POWER = 2.0 / 3.0;

Geom::Path sp_bspline_drawHandle(Geom::Point p, double helper_size);

LPEBSpline::LPEBSpline(LivePathEffectObject *lpeobject)
    : Effect(lpeobject),
      steps(_("Steps with CTRL:"), _("Change number of steps with CTRL pressed"), "steps", &wr, this, 2),
      helper_size(_("Helper size:"), _("Helper size"), "helper_size", &wr, this, 0),
      apply_no_weight(_("Apply changes if weight = 0%"), _("Apply changes if weight = 0%"), "apply_no_weight", &wr, this, true),
      apply_with_weight(_("Apply changes if weight > 0%"), _("Apply changes if weight > 0%"), "apply_with_weight", &wr, this, true),
      only_selected(_("Change only selected nodes"), _("Change only selected nodes"), "only_selected", &wr, this, false),
      uniform(_("Uniform BSpline"), _("Uniform bspline"), "uniform", &wr, this, false),
      weight(_("Change weight %:"), _("Change weight percent of the effect"), "weight", &wr, this, DEFAULT_START_POWER * 100)
{
    registerParameter(&weight);
    registerParameter(&steps);
    registerParameter(&helper_size);
    registerParameter(&apply_no_weight);
    registerParameter(&apply_with_weight);
    registerParameter(&only_selected);
    registerParameter(&uniform);

    weight.param_set_range(NO_POWER, 100.0);
    weight.param_set_increments(0.1, 0.1);
    weight.param_set_digits(4);

    steps.param_set_range(1, 10);
    steps.param_set_increments(1, 1);
    steps.param_set_digits(0);

    helper_size.param_set_range(0.0, 999.0);
    helper_size.param_set_increments(1, 1);
    helper_size.param_set_digits(2);
}

LPEBSpline::~LPEBSpline() = default;

void LPEBSpline::doBeforeEffect (SPLPEItem const* /*lpeitem*/)
{
    if(!hp.empty()) {
        hp.clear();
    }
}

void LPEBSpline::doOnApply(SPLPEItem const* lpeitem)
{
    if (!is<SPShape>(lpeitem)) {
        g_warning("LPE BSpline can only be applied to shapes (not groups).");
        SPLPEItem * item = const_cast<SPLPEItem*>(lpeitem);
        item->removeCurrentPathEffect(false);
    }
    lpeversion.param_setValue("1.3", true);
}

void
LPEBSpline::addCanvasIndicators(SPLPEItem const */*lpeitem*/, std::vector<Geom::PathVector> &hp_vec)
{
    hp_vec.push_back(hp);
}

Gtk::Widget *LPEBSpline::newWidget()
{
    // use manage here, because after deletion of Effect object, others might
    // still be pointing to this widget.
    auto const vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    vbox->set_margin(5);

    for (auto const param: param_vector) {
        if (!param->widget_is_visible) continue;

        auto const widg = param->param_newWidget();
        if (!widg) continue;

        if (param->param_key == "weight") {
            auto const buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);

            auto const default_weight = Gtk::make_managed<Gtk::Button>(_("Default weight"));
            default_weight->signal_clicked()
                .connect(sigc::mem_fun(*this, &LPEBSpline::toDefaultWeight));
            UI::pack_start(*buttons, *default_weight, true, true, 2);

            auto const make_cusp = Gtk::make_managed<Gtk::Button>(_("Make cusp"));
            make_cusp->signal_clicked()
                .connect(sigc::mem_fun(*this, &LPEBSpline::toMakeCusp));
            UI::pack_start(*buttons, *make_cusp, true, true, 2);

            UI::pack_start(*vbox, *buttons, true, true, 2);
        }
        if (param->param_key == "weight" || param->param_key == "steps") {
            auto &scalar = dynamic_cast<UI::Widget::Scalar &>(*widg);
            scalar.signal_value_changed().connect(sigc::mem_fun(*this, &LPEBSpline::toWeight));

            auto const childList = UI::get_children(scalar);
            auto &entry = dynamic_cast<Gtk::Entry &>(*childList.at(1));
            entry.set_width_chars(9);
        }

        UI::pack_start(*vbox, *widg, true, true, 2);

        if (auto const tip = param->param_getTooltip()) {
            widg->set_tooltip_markup(*tip);
        } else {
            widg->set_tooltip_text({});
            widg->set_has_tooltip(false);
        }
    }

    return vbox;
}

void LPEBSpline::toDefaultWeight()
{
    changeWeight(DEFAULT_START_POWER * 100);
    sp_lpe_item_update_patheffect(sp_lpe_item, false, false);
    makeUndoDone(_("Change to default weight"));
}

void LPEBSpline::toMakeCusp()
{
    changeWeight(NO_POWER);
    sp_lpe_item_update_patheffect(sp_lpe_item, false, false);
    makeUndoDone(_("Change to 0 weight"));
}

void LPEBSpline::toWeight()
{
    changeWeight(weight);
    sp_lpe_item_update_patheffect(sp_lpe_item, false, false);
    makeUndoDone(_("Change scalar parameter"));
}

void LPEBSpline::changeWeight(double weight_ammount)
{
    auto path = cast<SPPath>(sp_lpe_item);
    if (path) {
        auto curve = *path->curveForEdit();
        doBSplineFromWidget(&curve, weight_ammount / 100.0);
        path->setAttribute("inkscape:original-d", sp_svg_write_path(curve.get_pathvector()));
    }
}

void LPEBSpline::doEffect(SPCurve *curve)
{
    sp_bspline_do_effect(*curve, helper_size, hp, uniform);
}

void sp_bspline_do_effect(SPCurve &curve, double helper_size, Geom::PathVector &hp, bool uniform)
{
    if (curve.get_segment_count() < 1) {
        return;
    }
    Geom::PathVector original_pathv = curve.get_pathvector();
    curve.reset();
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    for (auto & path_it : original_pathv) {
        if (path_it.empty()) {
            continue;
        }
        if (!prefs->getBool("/tools/nodes/show_outline", true)){
            hp.push_back(path_it);
        }
        Geom::CubicBezier const *cubic = nullptr;
        // BSplines has special tratment for start/end on uniform cubic bsplines
        // we need to change power from 1/3 to 1/2 and apply the factor of current power
        if (uniform && !path_it.closed() && path_it.size_open() > 1) {
            cubic = dynamic_cast<Geom::CubicBezier const *>(&path_it.front());
            if (cubic) {
                double factor = Geom::nearest_time((*cubic)[2], path_it.front()) / DEFAULT_END_POWER;
                Geom::Path newp((*cubic)[0]);
                newp.appendNew<Geom::CubicBezier>((*cubic)[0], path_it.front().pointAt(0.5 + (factor - 1)), (*cubic)[3]);
                path_it.erase(path_it.begin());
                cubic = dynamic_cast<Geom::CubicBezier const *>(&path_it.front());
                if (cubic) {
                    double factor = Geom::nearest_time((*cubic)[2], path_it.front()) / DEFAULT_END_POWER;
                    Geom::Path newp2((*cubic)[0]);
                    newp2.appendNew<Geom::CubicBezier>((*cubic)[1], path_it.front().pointAt(0.5 + (factor - 1)), (*cubic)[3]);
                    path_it.erase(path_it.begin());
                    newp.setFinal(newp2.back_open().initialPoint());
                    newp.append(newp2);
                }
                path_it.setInitial(newp.back_open().finalPoint());
                newp.append(path_it);
                path_it = newp;
            }
            cubic = dynamic_cast<Geom::CubicBezier const *>(&path_it.back_open());
            if (cubic && path_it.size_open() > 2) {
                double factor = (Geom::nearest_time((*cubic)[1], path_it.back_open()) * 0.5) / DEFAULT_START_POWER;
                Geom::Path newp((*cubic)[0]);
                newp.appendNew<Geom::CubicBezier>(path_it.back_open().pointAt(factor), (*cubic)[3], (*cubic)[3]);
                path_it.erase_last();
                cubic = dynamic_cast<Geom::CubicBezier const *>(&path_it.back_open());
                if (cubic && path_it.size_open() > 3) {
                    double factor = (Geom::nearest_time((*cubic)[1], path_it.back_open()) * 0.5) / DEFAULT_START_POWER;
                    Geom::Path newp2((*cubic)[0]);
                    newp2.appendNew<Geom::CubicBezier>(path_it.back_open().pointAt(factor), (*cubic)[2], (*cubic)[3]);
                    path_it.erase_last();
                    newp2.setFinal(newp.back_open().initialPoint());
                    newp2.append(newp);
                    newp = newp2;
                }
                path_it.setFinal(newp.front().initialPoint());
                path_it.append(newp);
            }
        }
        Geom::Path::iterator curve_it1 = path_it.begin();
        Geom::Path::iterator curve_it2 = ++(path_it.begin());
        Geom::Path::iterator curve_endit = path_it.end_default();
        SPCurve curve_n;
        Geom::Point previousNode(0, 0);
        Geom::Point node(0, 0);
        Geom::Point point_at1(0, 0);
        Geom::Point point_at2(0, 0);
        Geom::Point next_point_at1(0, 0);
        Geom::D2<Geom::SBasis> sbasis_in;
        Geom::D2<Geom::SBasis> sbasis_out;
        Geom::D2<Geom::SBasis> sbasis_helper;
        curve_n.moveto(curve_it1->initialPoint());
        if (path_it.closed()) {
          const Geom::Curve &closingline = path_it.back_closed(); 
          // the closing line segment is always of type 
          // Geom::LineSegment.
          if (are_near(closingline.initialPoint(), closingline.finalPoint())) {
            // closingline.isDegenerate() did not work, because it only checks for
            // *exact* zero length, which goes wrong for relative coordinates and
            // rounding errors...
            // the closing line segment has zero-length. So stop before that one!
            curve_endit = path_it.end_open();
          }
        }
        while (curve_it1 != curve_endit) {
            SPCurve in;
            in.moveto(curve_it1->initialPoint());
            in.lineto(curve_it1->finalPoint());
            cubic = dynamic_cast<Geom::CubicBezier const *>(&*curve_it1);
            if (cubic) {
                sbasis_in = in.first_segment()->toSBasis();
                if (are_near((*cubic)[1],(*cubic)[0]) && !are_near((*cubic)[2],(*cubic)[3])) {
                    point_at1 = sbasis_in.valueAt(DEFAULT_START_POWER);
                } else {
                    point_at1 = sbasis_in.valueAt(Geom::nearest_time((*cubic)[1], *in.first_segment()));
                }
                if (uniform && curve_n.is_unset()) {
                    point_at1 = curve_it1->initialPoint();
                }
                if (are_near((*cubic)[2],(*cubic)[3]) && !are_near((*cubic)[1],(*cubic)[0])) {
                    point_at2 = sbasis_in.valueAt(DEFAULT_END_POWER);
                } else {
                    point_at2 = sbasis_in.valueAt(Geom::nearest_time((*cubic)[2], *in.first_segment()));
                }
            } else {
                point_at1 = in.first_segment()->initialPoint();
                point_at2 = in.first_segment()->finalPoint();
            }
            if (curve_it2 != curve_endit) {
                SPCurve out;
                out.moveto(curve_it2->initialPoint());
                out.lineto(curve_it2->finalPoint());
                cubic = dynamic_cast<Geom::CubicBezier const *>(&*curve_it2);
                if (cubic) {
                    sbasis_out = out.first_segment()->toSBasis();
                    if (are_near((*cubic)[1],(*cubic)[0]) && !are_near((*cubic)[2],(*cubic)[3])) {
                        next_point_at1 = sbasis_in.valueAt(DEFAULT_START_POWER);
                    } else {
                        next_point_at1 = sbasis_out.valueAt(Geom::nearest_time((*cubic)[1], *out.first_segment()));
                    }
                } else {
                    next_point_at1 = out.first_segment()->initialPoint();
                }
            }
            if (path_it.closed() && curve_it2 == curve_endit) {
                SPCurve start;
                start.moveto(path_it.begin()->initialPoint());
                start.lineto(path_it.begin()->finalPoint());
                Geom::D2<Geom::SBasis> sbasis_start = start.first_segment()->toSBasis();
                SPCurve line_helper;
                cubic = dynamic_cast<Geom::CubicBezier const *>(&*path_it.begin());
                if (cubic) {
                    line_helper.moveto(sbasis_start.valueAt(Geom::nearest_time((*cubic)[1], *start.first_segment())));
                } else {
                    line_helper.moveto(start.first_segment()->initialPoint());
                }

                SPCurve end;
                end.moveto(curve_it1->initialPoint());
                end.lineto(curve_it1->finalPoint());
                Geom::D2<Geom::SBasis> sbasis_end = end.first_segment()->toSBasis();
                cubic = dynamic_cast<Geom::CubicBezier const *>(&*curve_it1);
                if (cubic) {
                    line_helper.lineto(sbasis_end.valueAt(Geom::nearest_time((*cubic)[2], *end.first_segment())));
                } else {
                    line_helper.lineto(end.first_segment()->finalPoint());
                }
                sbasis_helper = line_helper.first_segment()->toSBasis();
                node = sbasis_helper.valueAt(0.5);
                curve_n.curveto(point_at1, point_at2, node);
                curve_n.move_endpoints(node, node);
            } else if ( curve_it2 == curve_endit) {
                if (uniform) {
                    curve_n.curveto(point_at1, curve_it1->finalPoint(), curve_it1->finalPoint());
                } else {
                    curve_n.curveto(point_at1, point_at2, curve_it1->finalPoint());
                }
                curve_n.move_endpoints(path_it.begin()->initialPoint(), curve_it1->finalPoint());
            } else {
                SPCurve line_helper;
                line_helper.moveto(point_at2);
                line_helper.lineto(next_point_at1);
                sbasis_helper = line_helper.first_segment()->toSBasis();
                previousNode = node;
                node = sbasis_helper.valueAt(0.5);
                Geom::CubicBezier const *cubic2 = dynamic_cast<Geom::CubicBezier const *>(&*curve_it1);
                if((cubic && are_near((*cubic)[0],(*cubic)[1])) || (cubic2 && are_near((*cubic2)[2],(*cubic2)[3]))) {
                    node = curve_it1->finalPoint();
                }
                curve_n.curveto(point_at1, point_at2, node);
            }
            if(!are_near(node,curve_it1->finalPoint()) && helper_size > 0.0) {
                hp.push_back(sp_bspline_drawHandle(node, helper_size));
            }
            ++curve_it1;
            ++curve_it2;
        }
        if (path_it.closed()) {
            curve_n.closepath_current();
        }
        curve.append(std::move(curve_n), false);
    }
    if(helper_size > 0.0) {
        hp.push_back(curve.get_pathvector()[0]);
    }
}

Geom::Path sp_bspline_drawHandle(Geom::Point p, double helper_size)
{
    char const * svgd = "M 1,0.5 A 0.5,0.5 0 0 1 0.5,1 0.5,0.5 0 0 1 0,0.5 0.5,0.5 0 0 1 0.5,0 0.5,0.5 0 0 1 1,0.5 Z";
    Geom::PathVector pathv = sp_svg_read_pathv(svgd);
    Geom::Affine aff = Geom::Affine();
    aff *= Geom::Scale(helper_size);
    pathv *= aff;
    pathv *= Geom::Translate(p - Geom::Point(0.5*helper_size, 0.5*helper_size));
    return pathv[0];
}

void LPEBSpline::doBSplineFromWidget(SPCurve *curve, double weight_ammount)
{
    using Geom::X;
    using Geom::Y;

    if (curve->get_segment_count() < 1)
        return;
    // Make copy of old path as it is changed during processing
    Geom::PathVector const original_pathv = curve->get_pathvector();
    curve->reset();

    for (const auto & path_it : original_pathv) {
        if (path_it.empty()) {
            continue;
        }
        Geom::Path::const_iterator curve_it1 = path_it.begin();
        Geom::Path::const_iterator curve_it2 = ++(path_it.begin());
        Geom::Path::const_iterator curve_endit = path_it.end_default();

        SPCurve curve_n;
        Geom::Point point_at0(0, 0);
        Geom::Point point_at1(0, 0);
        Geom::Point point_at2(0, 0);
        Geom::Point point_at3(0, 0);
        Geom::D2<Geom::SBasis> sbasis_in;
        Geom::D2<Geom::SBasis> sbasis_out;
        Geom::CubicBezier const *cubic = nullptr;
        curve_n.moveto(curve_it1->initialPoint());
        if (path_it.closed()) {
          const Geom::Curve &closingline = path_it.back_closed(); 
          // the closing line segment is always of type 
          // Geom::LineSegment.
          if (are_near(closingline.initialPoint(), closingline.finalPoint())) {
            // closingline.isDegenerate() did not work, because it only checks for
            // *exact* zero length, which goes wrong for relative coordinates and
            // rounding errors...
            // the closing line segment has zero-length. So stop before that one!
            curve_endit = path_it.end_open();
          }
        }
        while (curve_it1 != curve_endit) {
            SPCurve in;
            in.moveto(curve_it1->initialPoint());
            in.lineto(curve_it1->finalPoint());
            cubic = dynamic_cast<Geom::CubicBezier const *>(&*curve_it1);
            point_at0 = in.first_segment()->initialPoint();
            point_at3 = in.first_segment()->finalPoint();
            sbasis_in = in.first_segment()->toSBasis();
            if (cubic) {
                if ((apply_no_weight && apply_with_weight) ||
                    (apply_no_weight && Geom::are_near((*cubic)[1], point_at0)) ||
                    (apply_with_weight && !Geom::are_near((*cubic)[1], point_at0)))
                {
                    if (isNodePointSelected(point_at0) || !only_selected) {
                        point_at1 = sbasis_in.valueAt(weight_ammount);
                    } else {
                        point_at1 = (*cubic)[1];
                    }
                } else {
                    point_at1 = (*cubic)[1];
                }
                if ((apply_no_weight && apply_with_weight) ||
                    (apply_no_weight && Geom::are_near((*cubic)[2], point_at3)) ||
                    (apply_with_weight && !Geom::are_near((*cubic)[2], point_at3)))
                {
                    if (isNodePointSelected(point_at3) || !only_selected) {
                        point_at2 = sbasis_in.valueAt(1 - weight_ammount);
                        if (!Geom::are_near(weight_ammount, NO_POWER, BSPLINE_TOL)) {
                            point_at2 =
                                Geom::Point(point_at2[X], point_at2[Y]);
                        }
                    } else {
                        point_at2 = (*cubic)[2];
                    }
                } else {
                    point_at2 = (*cubic)[2];
                }
            } else {
                if ((apply_no_weight && apply_with_weight) || 
                    (apply_no_weight && Geom::are_near(weight_ammount, NO_POWER, BSPLINE_TOL)) ||
                    (apply_with_weight && !Geom::are_near(weight_ammount, NO_POWER, BSPLINE_TOL)))
                {
                    if (isNodePointSelected(point_at0) || !only_selected) {
                        point_at1 = sbasis_in.valueAt(weight_ammount);
                    } else {
                        point_at1 = in.first_segment()->initialPoint();
                    }
                    if (isNodePointSelected(point_at3) || !only_selected) {
                        point_at2 = sbasis_in.valueAt(1 - weight_ammount);
                    } else {
                        point_at2 = in.first_segment()->finalPoint();
                    }
                } else {
                    point_at1 = in.first_segment()->initialPoint();
                    point_at2 = in.first_segment()->finalPoint();
                }
            }
            curve_n.curveto(point_at1, point_at2, point_at3);
            ++curve_it1;
            ++curve_it2;
        }
        if (path_it.closed()) {
            curve_n.move_endpoints(path_it.begin()->initialPoint(),
                                   path_it.begin()->initialPoint());
        } else {
            curve_n.move_endpoints(path_it.begin()->initialPoint(), point_at3);
        }
        if (path_it.closed()) {
            curve_n.closepath_current();
        }
        curve->append(std::move(curve_n), false);
    }
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
