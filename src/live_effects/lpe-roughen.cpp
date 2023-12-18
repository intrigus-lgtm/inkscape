// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Roughen LPE implementation. Creates roughen paths.
 */
/* Authors:
 *   Jabier Arraiza Cenoz <jabier.arraiza@marker.es>
 *
 * Thanks to all people involved specially to Josh Andler for the idea and to the
 * original extensions authors.
 *
 * Copyright (C) 2014 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "lpe-roughen.h"

#include <boost/functional/hash.hpp>

#include <glibmm/i18n.h>
#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/separator.h>

#include "display/curve.h"
#include "helper/geom.h"
#include "object/sp-lpe-item.h"
#include "ui/pack.h"
#include "util-string/ustring-format.h"

namespace Inkscape {
namespace LivePathEffect {

static const Util::EnumData<DivisionMethod> DivisionMethodData[] = { 
    { DM_SEGMENTS, N_("Number of segments"), "segments" }, 
    { DM_SIZE, N_("Segment size"), "size" } 
};
static const Util::EnumDataConverter<DivisionMethod> DMConverter(DivisionMethodData, DM_END);

static const Util::EnumData<HandlesMethod> HandlesMethodData[] = {
    { HM_ALONG_NODES, N_("Along nodes"), "along" },
    { HM_RAND, N_("Random"), "rand" },
    { HM_RETRACT, N_("Retract"), "retract" },
    { HM_SMOOTH, N_("Smooth"), "smooth" } 
};
static const Util::EnumDataConverter<HandlesMethod> HMConverter(HandlesMethodData, HM_END);

LPERoughen::LPERoughen(LivePathEffectObject *lpeobject)
    : Effect(lpeobject)
    , method(_("Method"), _("<b>Segment size:</b> add nodes to path evenly; <b>Number of segments:</b> add nodes between existing nodes"), "method", DMConverter, &wr, this, DM_SIZE)
    , max_segment_size(_("Segment size"), _("Add nodes to path evenly. Choose <b>Segment size</b> method from the dropdown to use this subdivision method."), "max_segment_size", &wr, this, 10)
    , segments(_("Number of segments"), _("Add nodes between existing nodes. Choose <b>Number of segments</b> method from the dropdown to use this subdivision method."), "segments", &wr, this, 2)
    , displace_x(_("Displace ←→"), _("Maximal displacement in x direction"), "displace_x", &wr, this, 10.)
    , displace_y(_("Displace ↑↓"), _("Maximal displacement in y direction"), "displace_y", &wr, this, 10.)
    , global_randomize(_("Global randomize"), _("Global displacement in all directions"), "global_randomize", &wr, this, 1.)
    , handles(_("Direction"), _("Options for handle direction"), "handles", HMConverter, &wr, this, HM_ALONG_NODES)
    , shift_nodes(_("Apply displacement"), _("Uncheck to use this LPE for just adding nodes, without roughening; useful for further interactive processing."), "shift_nodes", &wr, this, true)
    , fixed_displacement(_("Fixed displacement"), _("Fixed displacement, 1/3 of segment length"), "fixed_displacement",
                         &wr, this, false)
    , spray_tool_friendly(_("Spray Tool friendly"), _("For use with Spray Tool in copy mode"), "spray_tool_friendly",
                          &wr, this, false)
{
    registerParameter(&global_randomize);
    registerParameter(&displace_x);
    registerParameter(&displace_y);
    registerParameter(&method);
    registerParameter(&max_segment_size);
    registerParameter(&segments);
    registerParameter(&handles);
    registerParameter(&shift_nodes);
    registerParameter(&fixed_displacement);
    registerParameter(&spray_tool_friendly);
    
    displace_x.param_set_range(0., std::numeric_limits<double>::max());
    displace_y.param_set_range(0., std::numeric_limits<double>::max());
    global_randomize.param_set_range(0., std::numeric_limits<double>::max());

    max_segment_size.param_set_range(0., std::numeric_limits<double>::max());
    max_segment_size.param_set_increments(1, 1);
    max_segment_size.param_set_digits(3);

    segments.param_make_integer();
    segments.param_set_range(1, 9999);
    segments.param_set_increments(1, 1);
    seed = 0;
    apply_to_clippath_and_mask = true;
}

LPERoughen::~LPERoughen() = default;

void LPERoughen::doOnApply(SPLPEItem const *lpeitem)
{
    Geom::OptRect bbox = lpeitem->bounds(SPItem::GEOMETRIC_BBOX);
    if (bbox) {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();

        for (auto const param: param_vector) {
            auto const pref_path = Glib::ustring::compose("/live_effects/%1/%2",
                                                          LPETypeConverter.get_key(effectType()),
                                                          param->param_key);
            if (prefs->getEntry(pref_path).isValid()) continue;

            if (param->param_key == "max_segment_size") {
                auto const minor = std::min(bbox->width(), bbox->height());
                auto const max_segment_size_str = Inkscape::ustring::format_classic(minor / 50.0);
                param->param_readSVGValue(max_segment_size_str.c_str());
            } else if (param->param_key == "displace_x") {
                auto const displace_x_str = Inkscape::ustring::format_classic(bbox->width() / 150.0);
                param->param_readSVGValue(displace_x_str.c_str());
            } else if (param->param_key == "displace_y") {
                auto const displace_y_str = Inkscape::ustring::format_classic(bbox->height() / 150.0);
                param->param_readSVGValue(displace_y_str.c_str());
            }
        }
        writeParamsToSVG();
    }
    lpeversion.param_setValue("1.2", true);
}

void LPERoughen::doBeforeEffect(SPLPEItem const *lpeitem)
{
    if (spray_tool_friendly && seed == 0 && lpeitem->getId()) {
        std::string id_item(lpeitem->getId());
        long seed = static_cast<long>(boost::hash_value(id_item));
        global_randomize.param_set_value(global_randomize.get_value(), seed);
    }
    displace_x.resetRandomizer();
    displace_y.resetRandomizer();
    global_randomize.resetRandomizer();
    if (lpeversion.param_getSVGValue() < "1.1") {
        srand(1);
    } else {
        displace_x.param_set_randomsign(true);
        displace_y.param_set_randomsign(true);
    }
}

Gtk::Widget *LPERoughen::newWidget()
{
    auto const vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    vbox->set_margin(5);

    std::vector<Parameter *>::iterator it = param_vector.begin();
    while (it != param_vector.end()) {
        if ((*it)->widget_is_visible) {
            Parameter *param = *it;
            auto widg = param->param_newWidget();
            if (param->param_key == "method") {
                auto const method_label = Gtk::make_managed<Gtk::Label>(
                    Glib::ustring(_("<b>Resolution</b>")), Gtk::Align::START);
                method_label->set_use_markup(true);
                UI::pack_start(*vbox, *method_label, false, false, 2);
                UI::pack_start(*vbox, *Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL),
                               UI::PackOptions::expand_widget);
            }
            if (param->param_key == "handles") {
                auto const options = Gtk::make_managed<Gtk::Label>(
                    Glib::ustring(_("<b>Options</b>")), Gtk::Align::START);
                options->set_use_markup(true);
                UI::pack_start(*vbox, *options, false, false, 2);
                UI::pack_start(*vbox, *Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL),
                               UI::PackOptions::expand_widget);
            }

            if (widg) {
                UI::pack_start(*vbox, *widg, true, true, 2);

                if (auto const tip = param->param_getTooltip()) {
                    widg->set_tooltip_markup(*tip);
                } else {
                    widg->set_tooltip_text("");
                    widg->set_has_tooltip(false);
                }
            }
        }
        ++it;
    }

    return vbox;
}

double LPERoughen::sign(double random_number)
{
    if (lpeversion.param_getSVGValue() < "1.1") {
        if (rand() % 100 < 49) {
            random_number *= -1.;
        }
    }
    return random_number;
}

Geom::Point LPERoughen::randomize(double max_length, bool is_node)
{
    double factor = 1.0 / 3.0;
    if (is_node) {
        factor = 1.0;
    }
    double displace_x_parsed = displace_x * global_randomize * factor;
    double displace_y_parsed = displace_y * global_randomize * factor;
    Geom::Point output = Geom::Point(sign(displace_x_parsed), sign(displace_y_parsed));
    if (fixed_displacement) {
        Geom::Ray ray(Geom::Point(0, 0), output);
        output = Geom::Point::polar(ray.angle(), max_length);
    }
    return output;
}

void LPERoughen::doEffect(SPCurve *curve)
{
    Geom::PathVector const original_pathv = pathv_to_linear_and_cubic_beziers(curve->get_pathvector());
    curve->reset();
    for (const auto &path_it : original_pathv) {
        if (path_it.empty())
            continue;

        Geom::Path::const_iterator curve_it1 = path_it.begin();
        Geom::Path::const_iterator curve_it2 = ++(path_it.begin());
        Geom::Path::const_iterator curve_endit = path_it.end_default();
        SPCurve nCurve;
        Geom::Point prev(0, 0);
        Geom::Point last_move(0, 0);
        nCurve.moveto(curve_it1->initialPoint());
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
            Geom::CubicBezier const *cubic = nullptr;
            cubic = dynamic_cast<Geom::CubicBezier const *>(&*curve_it1);
            if (cubic) {
                nCurve.curveto((*cubic)[1] + last_move, (*cubic)[2], curve_it1->finalPoint());
            } else {
                nCurve.lineto(curve_it1->finalPoint());
            }
            last_move = Geom::Point(0, 0);
            double length = curve_it1->length(0.01);
            std::size_t splits = 0;
            if (method == DM_SEGMENTS) {
                splits = segments;
            } else {
                splits = ceil(length / max_segment_size);
            }
            auto const original = std::unique_ptr<Geom::Curve>(nCurve.last_segment()->duplicate());
            for (unsigned int t = 1; t <= splits; t++) {
                if (t == splits && splits != 1) {
                    continue;
                }
                SPCurve tmp;
                if (splits == 1) {
                    tmp = jitter(nCurve.last_segment(), prev, last_move);
                } else {
                    bool last = false;
                    if (t == splits - 1) {
                        last = true;
                    }
                    double time =
                        Geom::nearest_time(original->pointAt((1. / (double)splits) * t), *nCurve.last_segment());
                    tmp = addNodesAndJitter(nCurve.last_segment(), prev, last_move, time, last);
                }
                if (nCurve.get_segment_count() > 1) {
                    nCurve.backspace();
                    nCurve.append_continuous(std::move(tmp), 0.001);
                } else {
                    nCurve = std::move(tmp);
                }
            }
            ++curve_it1;
            ++curve_it2;
        }
        if (path_it.closed()) {
            if (handles == HM_SMOOTH && curve_it1 == curve_endit) {
                SPCurve *out = new SPCurve();
                nCurve.reverse();
                Geom::CubicBezier const *cubic_start = dynamic_cast<Geom::CubicBezier const *>(nCurve.first_segment());
                Geom::CubicBezier const *cubic = dynamic_cast<Geom::CubicBezier const *>(nCurve.last_segment());
                Geom::Point oposite = nCurve.first_segment()->pointAt(1.0 / 3.0);
                if (cubic_start) {
                    Geom::Ray ray((*cubic_start)[1], (*cubic_start)[0]);
                    double dist = Geom::distance((*cubic_start)[1], (*cubic_start)[0]);
                    oposite = Geom::Point::polar(ray.angle(), dist) + (*cubic_start)[0];
                }
                if (cubic) {
                    out->moveto((*cubic)[0]);
                    out->curveto((*cubic)[1], oposite, (*cubic)[3]);
                } else {
                    out->moveto(nCurve.last_segment()->initialPoint());
                    out->curveto(nCurve.last_segment()->initialPoint(), oposite, nCurve.last_segment()->finalPoint());
                }
                nCurve.backspace();
                nCurve.append_continuous(*out, 0.001);
                nCurve.reverse();
            }
            if (handles == HM_ALONG_NODES && curve_it1 == curve_endit) {
                SPCurve out;
                nCurve.reverse();
                Geom::CubicBezier const *cubic = dynamic_cast<Geom::CubicBezier const *>(nCurve.last_segment());
                if (cubic) {
                    out.moveto((*cubic)[0]);
                    out.curveto((*cubic)[1], (*cubic)[2] - ((*cubic)[3] - nCurve.first_segment()->initialPoint()), (*cubic)[3]);
                    nCurve.backspace();
                    nCurve.append_continuous(std::move(out), 0.001);
                }
                nCurve.reverse();
            }
            nCurve.move_endpoints(nCurve.last_segment()->finalPoint(), nCurve.last_segment()->finalPoint());
            nCurve.closepath_current();
        }
        curve->append(std::move(nCurve));
    }
}

SPCurve LPERoughen::addNodesAndJitter(Geom::Curve const *A, Geom::Point &prev, Geom::Point &last_move, double t, bool last)
{
    SPCurve out;
    Geom::CubicBezier const *cubic = dynamic_cast<Geom::CubicBezier const *>(&*A);
    double max_length = Geom::distance(A->initialPoint(), A->pointAt(t)) / 3.0;
    Geom::Point point_a1(0, 0);
    Geom::Point point_a2(0, 0);
    Geom::Point point_a3(0, 0);
    Geom::Point point_b1(0, 0);
    Geom::Point point_b2(0, 0);
    Geom::Point point_b3(0, 0);
    if (shift_nodes) {
        point_a3 = randomize(max_length, true);
        if (last) {
            point_b3 = randomize(max_length, true);
        }
    }
    if (handles == HM_RAND || handles == HM_SMOOTH) {
        point_a1 = randomize(max_length);
        point_a2 = randomize(max_length);
        point_b1 = randomize(max_length);
        if (last) {
            point_b2 = randomize(max_length);
        }
    } else {
        point_a2 = point_a3;
        point_b1 = point_a3;
        if (last) {
            point_b2 = point_b3;
        }
    }
    if (handles == HM_SMOOTH) {
        if (cubic) {
            std::pair<Geom::CubicBezier, Geom::CubicBezier> div = cubic->subdivide(t);
            std::vector<Geom::Point> seg1 = div.first.controlPoints(), seg2 = div.second.controlPoints();
            Geom::Ray ray(seg1[3] + point_a3, seg2[1] + point_a3);
            double length = max_length;
            if (!fixed_displacement) {
                length = Geom::distance(seg1[3] + point_a3, seg2[1] + point_a3);
            }
            point_b1 = seg1[3] + point_a3 + Geom::Point::polar(ray.angle(), length);
            point_b2 = seg2[2];
            point_b3 = seg2[3] + point_b3;
            point_a3 = seg1[3] + point_a3;
            ray.setPoints(prev, A->initialPoint());
            point_a1 = A->initialPoint() + Geom::Point::polar(ray.angle(), max_length);
            if (last) {
                Geom::Path b2(point_b3);
                b2.appendNew<Geom::LineSegment>(point_a3);
                length = max_length;
                ray.setPoints(point_b3, point_b2);
                if (!fixed_displacement) {
                    length = Geom::distance(b2.pointAt(1.0 / 3.0), point_b3);
                }
                point_b2 = point_b3 + Geom::Point::polar(ray.angle(), length);
            }
            ray.setPoints(point_b1, point_a3);
            point_a2 = point_a3 + Geom::Point::polar(ray.angle(), max_length);
            if (last) {
                prev = point_b2;
            } else {
                prev = point_a2;
            }
            out.moveto(seg1[0]);
            out.curveto(point_a1, point_a2, point_a3);
            out.curveto(point_b1, point_b2, point_b3);
        } else {
            Geom::Ray ray(A->pointAt(t) + point_a3, A->pointAt(t + (t / 3)));
            double length = max_length;
            if (!fixed_displacement) {
                length = Geom::distance(A->pointAt(t) + point_a3, A->pointAt(t + (t / 3)));
            }
            point_b1 = A->pointAt(t) + point_a3 + Geom::Point::polar(ray.angle(), length);
            point_b2 = A->pointAt(t + ((t / 3) * 2));
            point_b3 = A->finalPoint() + point_b3;
            point_a3 = A->pointAt(t) + point_a3;
            ray.setPoints(prev, A->initialPoint());
            point_a1 = A->initialPoint() + Geom::Point::polar(ray.angle(), max_length);
            if (prev == Geom::Point(0, 0)) {
                point_a1 = randomize(max_length);
            }
            if (last) {
                Geom::Path b2(point_b3);
                b2.appendNew<Geom::LineSegment>(point_a3);
                length = max_length;
                ray.setPoints(point_b3, point_b2);
                if (!fixed_displacement) {
                    length = Geom::distance(b2.pointAt(1.0 / 3.0), point_b3);
                }
                point_b2 = point_b3 + Geom::Point::polar(ray.angle(), length);
            }
            ray.setPoints(point_b1, point_a3);
            point_a2 = point_a3 + Geom::Point::polar(ray.angle(), max_length);
            if (last) {
                prev = point_b2;
            } else {
                prev = point_a2;
            }
            out.moveto(A->initialPoint());
            out.curveto(point_a1, point_a2, point_a3);
            out.curveto(point_b1, point_b2, point_b3);
        }
    } else if (handles == HM_RETRACT) {
        out.moveto(A->initialPoint());
        out.lineto(A->pointAt(t) + point_a3);
        if (cubic && !last) {
            std::pair<Geom::CubicBezier, Geom::CubicBezier> div = cubic->subdivide(t);
            std::vector<Geom::Point> seg2 = div.second.controlPoints();
            out.curveto(seg2[1], seg2[2], seg2[3]);
        } else {
            out.lineto(A->finalPoint() + point_b3);
        }
    } else if (handles == HM_ALONG_NODES) {
        if (cubic) {
            std::pair<Geom::CubicBezier, Geom::CubicBezier> div = cubic->subdivide(t);
            std::vector<Geom::Point> seg1 = div.first.controlPoints(), seg2 = div.second.controlPoints();
            out.moveto(seg1[0]);
            out.curveto(seg1[1] + last_move, seg1[2] + point_a3, seg1[3] + point_a3);
            last_move = point_a3;
            if (last) {
                last_move = point_b3;
            }
            out.curveto(seg2[1] + point_a3, seg2[2] + point_b3, seg2[3] + point_b3);
        } else {
            out.moveto(A->initialPoint());
            out.lineto(A->pointAt(t) + point_a3);
            out.lineto(A->finalPoint() + point_b3);
        }
    } else if (handles == HM_RAND) {
        if (cubic) {
            std::pair<Geom::CubicBezier, Geom::CubicBezier> div = cubic->subdivide(t);
            std::vector<Geom::Point> seg1 = div.first.controlPoints(), seg2 = div.second.controlPoints();
            out.moveto(seg1[0]);
            out.curveto(seg1[1] + point_a1, seg1[2] + point_a2 + point_a3, seg1[3] + point_a3);
            out.curveto(seg2[1] + point_a3 + point_b1, seg2[2] + point_b2 + point_b3, seg2[3] + point_b3);
        } else {
            out.moveto(A->initialPoint());
            out.lineto(A->pointAt(t) + point_a3);
            out.lineto(A->finalPoint() + point_b3);
        }
    }
    return out;
}

SPCurve LPERoughen::jitter(Geom::Curve const *A, Geom::Point &prev, Geom::Point &last_move)
{
    SPCurve out;
    Geom::CubicBezier const *cubic = dynamic_cast<Geom::CubicBezier const *>(&*A);
    double max_length = Geom::distance(A->initialPoint(), A->finalPoint()) / 3.0;
    Geom::Point point_a1(0, 0);
    Geom::Point point_a2(0, 0);
    Geom::Point point_a3(0, 0);
    if (shift_nodes) {
        point_a3 = randomize(max_length, true);
    }
    if (handles == HM_RAND || handles == HM_SMOOTH) {
        point_a1 = randomize(max_length);
        point_a2 = randomize(max_length);
    }
    if (handles == HM_SMOOTH) {
        if (cubic) {
            Geom::Ray ray(prev, A->initialPoint());
            point_a1 = Geom::Point::polar(ray.angle(), max_length);
            if (prev == Geom::Point(0, 0)) {
                point_a1 = A->pointAt(1.0 / 3.0) + randomize(max_length);
            }
            ray.setPoints((*cubic)[3] + point_a3, (*cubic)[2] + point_a3);
            if (lpeversion.param_getSVGValue() < "1.1") {
                point_a2 = randomize(max_length, ray.angle());
            } else {
                point_a2 = randomize(max_length, false);
            }
            prev = (*cubic)[2] + point_a2;
            out.moveto((*cubic)[0]);
            out.curveto((*cubic)[0] + point_a1, (*cubic)[2] + point_a2 + point_a3, (*cubic)[3] + point_a3);
        } else {
            Geom::Ray ray(prev, A->initialPoint());
            point_a1 = Geom::Point::polar(ray.angle(), max_length);
            if (prev == Geom::Point(0, 0)) {
                point_a1 = A->pointAt(1.0 / 3.0) + randomize(max_length);
            }
            ray.setPoints(A->finalPoint() + point_a3, A->pointAt((1.0 / 3.0) * 2) + point_a3);
            if (lpeversion.param_getSVGValue() < "1.1") {
                point_a2 = randomize(max_length, ray.angle());
            } else {
                point_a2 = randomize(max_length, false);
            }
            prev = A->pointAt((1.0 / 3.0) * 2) + point_a2 + point_a3;
            out.moveto(A->initialPoint());
            out.curveto(A->initialPoint() + point_a1, A->pointAt((1.0 / 3.0) * 2) + point_a2 + point_a3,
                         A->finalPoint() + point_a3);
        }
    } else if (handles == HM_RETRACT) {
        out.moveto(A->initialPoint());
        out.lineto(A->finalPoint() + point_a3);
    } else if (handles == HM_ALONG_NODES) {
        if (cubic) {
            out.moveto((*cubic)[0]);
            out.curveto((*cubic)[1] + last_move, (*cubic)[2] + point_a3, (*cubic)[3] + point_a3);
            last_move = point_a3;
        } else {
            out.moveto(A->initialPoint());
            out.lineto(A->finalPoint() + point_a3);
        }
    } else if (handles == HM_RAND) {
        out.moveto(A->initialPoint());
        out.curveto(A->pointAt(0.3333) + point_a1, A->pointAt(0.6666) + point_a2 + point_a3,
                     A->finalPoint() + point_a3);
    }
    return out;
}

Geom::Point LPERoughen::tPoint(Geom::Point A, Geom::Point B, double t)
{
    using Geom::X;
    using Geom::Y;
    return Geom::Point(A[X] + t * (B[X] - A[X]), A[Y] + t * (B[Y] - A[Y]));
}

}; // namespace LivePathEffect
}; /* namespace Inkscape */

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
