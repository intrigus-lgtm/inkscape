// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * LPE "Transform through 2 points" implementation
 */
/*
 * Authors:
 *   Jabier Arraiza Cenoz<jabier.arraiza@marker.es>
 *
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "lpe-transform_2pts.h"

#include <gtkmm/box.h>
#include <gtkmm/entry.h>

#include "display/curve.h"
#include "helper/geom.h"
#include "object/sp-path.h"
#include "svg/svg.h"
#include "ui/icon-names.h"
#include "ui/pack.h"
#include "ui/util.h"

// TODO due to internal breakage in glibmm headers, this must be last:
#include <glibmm/i18n.h>

namespace Inkscape::LivePathEffect {

LPETransform2Pts::LPETransform2Pts(LivePathEffectObject *lpeobject) :
    Effect(lpeobject),
    elastic(_("Elastic"), _("Elastic transform mode"), "elastic", &wr, this, false,"", INKSCAPE_ICON("on-outline"), INKSCAPE_ICON("off-outline")),
    from_original_width(_("From original width"), _("From original width"), "from_original_width", &wr, this, false,"", INKSCAPE_ICON("on-outline"), INKSCAPE_ICON("off-outline")),
    lock_length(_("Lock length"), _("Lock length to current distance"), "lock_length", &wr, this, false,"", INKSCAPE_ICON("on-outline"), INKSCAPE_ICON("off-outline")),
    lock_angle(_("Lock angle"), _("Lock angle"), "lock_angle", &wr, this, false,"", INKSCAPE_ICON("on-outline"), INKSCAPE_ICON("off-outline")),
    flip_horizontal(_("Flip horizontal"), _("Flip horizontal"), "flip_horizontal", &wr, this, false,"", INKSCAPE_ICON("on-outline"), INKSCAPE_ICON("off-outline")),
    flip_vertical(_("Flip vertical"), _("Flip vertical"), "flip_vertical", &wr, this, false,"", INKSCAPE_ICON("on-outline"), INKSCAPE_ICON("off-outline")),
    start(_("Start"), _("Start point"), "start", &wr, this, "Start point"),
    end(_("End"), _("End point"), "end", &wr, this, "End point"),
    stretch(_("Stretch"), _("Stretch the result"), "stretch", &wr, this, 1),
    offset(_("Offset"), _("Offset from knots"), "offset", &wr, this, 0),
    first_knot(_("First Knot"), _("First Knot"), "first_knot", &wr, this, 1),
    last_knot(_("Last Knot"), _("Last Knot"), "last_knot", &wr, this, 1),
    helper_size(_("Helper size:"), _("Rotation helper size"), "helper_size", &wr, this, 3),
    from_original_width_toggler(false),
    point_a(Geom::Point()),
    point_b(Geom::Point()),
    pathvector(),
    append_path(false),
    previous_angle(Geom::rad_from_deg(0)),
    previous_start(Geom::Point()),
    previous_length(-1)
{
    registerParameter(&first_knot);
    registerParameter(&last_knot);
    registerParameter(&helper_size);
    registerParameter(&stretch);
    registerParameter(&offset);
    registerParameter(&start);
    registerParameter(&end);
    registerParameter(&elastic);
    registerParameter(&from_original_width);
    registerParameter(&flip_vertical);
    registerParameter(&flip_horizontal);
    registerParameter(&lock_length);
    registerParameter(&lock_angle);

    first_knot.param_make_integer();
    last_knot.param_make_integer();
    helper_size.param_set_range(0, 999);
    helper_size.param_set_increments(1, 1);
    helper_size.param_set_digits(0);
    offset.param_set_range(std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max());
    offset.param_set_increments(1, 1);
    offset.param_set_digits(2);
    stretch.param_set_range(0, 999.0);
    stretch.param_set_increments(0.01, 0.01);
    stretch.param_set_digits(4);
    apply_to_clippath_and_mask = true;
}

LPETransform2Pts::~LPETransform2Pts() = default;

void
LPETransform2Pts::doOnApply(SPLPEItem const* lpeitem)
{
    using namespace Geom;
    original_bbox(lpeitem, false, true);
    point_a = Point(boundingbox_X.min(), boundingbox_Y.middle());
    point_b = Point(boundingbox_X.max(), boundingbox_Y.middle());
    SPLPEItem * splpeitem = const_cast<SPLPEItem *>(lpeitem);
    auto sp_path = cast<SPPath>(splpeitem);
    if (sp_path) {
        pathvector = sp_path->curveForEdit()->get_pathvector();
    }
    if(!pathvector.empty()) {
        point_a = pathvector.initialPoint();
        point_b = pathvector.finalPoint();
        if(are_near(point_a,point_b)) {
            point_b = pathvector.back().finalCurve().initialPoint();
        }
        size_t nnodes = nodeCount(pathvector);
        // this is need to fix undo on apply
        first_knot.param_set_value(1);
        last_knot.param_set_value(nnodes);
        first_knot.write_to_SVG();
        last_knot.write_to_SVG();
    }

    previous_length = Geom::distance(point_a,point_b);
    Geom::Ray transformed(point_a,point_b);
    previous_angle = transformed.angle();
    start.param_update_default(point_a);
    start.param_set_default();
    end.param_update_default(point_b);
    end.param_set_default();
}

void LPETransform2Pts::transform_multiply(Geom::Affine const &postmul, bool /*set*/)
{
    if (sp_lpe_item && sp_lpe_item->pathEffectsEnabled() && sp_lpe_item->optimizeTransforms()) {
        start.param_transform_multiply(postmul, false);
        end.param_transform_multiply(postmul, false);
    }
}

void
LPETransform2Pts::doBeforeEffect (SPLPEItem const* lpeitem)
{
    using namespace Geom;
    original_bbox(lpeitem, false, true);
    point_a = Point(boundingbox_X.min(), boundingbox_Y.middle());
    point_b = Point(boundingbox_X.max(), boundingbox_Y.middle());

    SPLPEItem * splpeitem = const_cast<SPLPEItem *>(lpeitem);
    auto sp_path = cast<SPPath>(splpeitem);
    if (sp_path) {
        pathvector = sp_path->curveForEdit()->get_pathvector();
    }
    if(from_original_width_toggler != from_original_width) {
        from_original_width_toggler = from_original_width;
        reset();
    }
    if(!pathvector.empty() && !from_original_width) {
        append_path = false;
        point_a = pointAtNodeIndex(pathvector,(size_t)first_knot-1);
        point_b = pointAtNodeIndex(pathvector,(size_t)last_knot-1);
        size_t nnodes = nodeCount(pathvector);
        first_knot.param_set_range(1, last_knot-1);
        last_knot.param_set_range(first_knot+1, nnodes);
        if (from_original_width){
            from_original_width.param_setValue(false);
        }
    } else {
        if (first_knot != 1){
            first_knot.param_set_value(1);
        }
        if (last_knot != 2){
            last_knot.param_set_value(2);
        }
        first_knot.param_set_range(1,1);
        last_knot.param_set_range(2,2);
        append_path = false;
        if (!from_original_width){
            from_original_width.param_setValue(true);
        }
    }
    if(lock_length && !lock_angle && previous_length != -1) {
        Geom::Ray transformed((Geom::Point)start,(Geom::Point)end);
        if(previous_start == start || previous_angle == Geom::rad_from_deg(0)) {
            previous_angle = transformed.angle();
        }
    } else if(lock_angle && !lock_length && previous_angle != Geom::rad_from_deg(0)) {
        if(previous_start == start){
            previous_length = Geom::distance((Geom::Point)start, (Geom::Point)end);
        }
    }
    if(lock_length || lock_angle ) {
        Geom::Point end_point = Geom::Point::polar(previous_angle, previous_length) + (Geom::Point)start;
        end.param_setValue(end_point);
    }
    Geom::Ray transformed((Geom::Point)start,(Geom::Point)end);
    previous_angle = transformed.angle();
    previous_length = Geom::distance((Geom::Point)start, (Geom::Point)end);
    previous_start = start;
}

void
LPETransform2Pts::updateIndex()
{
    SPLPEItem * splpeitem = const_cast<SPLPEItem *>(sp_lpe_item);
    auto sp_path = cast<SPPath>(splpeitem);
    if (sp_path) {
        pathvector = sp_path->curveForEdit()->get_pathvector();
    }
    if(pathvector.empty()) {
        return;
    }
    if(!from_original_width) {
        point_a = pointAtNodeIndex(pathvector,(size_t)first_knot-1);
        point_b = pointAtNodeIndex(pathvector,(size_t)last_knot-1);
        start.param_update_default(point_a);
        start.param_set_default();
        end.param_update_default(point_b);
        end.param_set_default();
        start.param_update_default(point_a);
        end.param_update_default(point_b);
        start.param_set_default();
        end.param_set_default();
        // seems silly but fix undo resetting value on it
        first_knot.param_set_value(first_knot);
        last_knot.param_set_value(last_knot);
    }
    refresh_widgets = true;
}
//todo migrate to PathVector class?
size_t
LPETransform2Pts::nodeCount(Geom::PathVector pathvector) const
{
    size_t n = 0;
    for (auto & it : pathvector) {
        n += count_path_nodes(it);
    }
    return n;
}
//todo migrate to PathVector class?
Geom::Point
LPETransform2Pts::pointAtNodeIndex(Geom::PathVector pathvector, size_t index) const
{
    size_t n = 0;
    for (auto & pv_it : pathvector) {
        for (Geom::Path::iterator curve_it = pv_it.begin(); curve_it != pv_it.end_closed(); ++curve_it) {
            if(index == n) {
                return curve_it->initialPoint();
            }
            n++;
        }
    }
    return Geom::Point();
}
//todo migrate to PathVector class? Not used
Geom::Path
LPETransform2Pts::pathAtNodeIndex(Geom::PathVector pathvector, size_t index) const
{
    size_t n = 0;
    for (auto & pv_it : pathvector) {
        for (Geom::Path::iterator curve_it = pv_it.begin(); curve_it != pv_it.end_closed(); ++curve_it) {
            if(index == n) {
                return pv_it;
            }
            n++;
        }
    }
    return Geom::Path();
}

void
LPETransform2Pts::reset()
{
    point_a = Geom::Point(boundingbox_X.min(), boundingbox_Y.middle());
    point_b = Geom::Point(boundingbox_X.max(), boundingbox_Y.middle());
    if(!pathvector.empty() && !from_original_width) {
        size_t nnodes = nodeCount(pathvector);
        first_knot.param_set_range(1, last_knot-1);
        last_knot.param_set_range(first_knot+1, nnodes);
        first_knot.param_set_value(1);
        last_knot.param_set_value(nnodes);
        point_a = pathvector.initialPoint();
        point_b = pathvector.finalPoint();
    } else {
        first_knot.param_set_value(1);
        last_knot.param_set_value(2);
    }
    refresh_widgets = true;
    offset.param_set_value(0.0);
    stretch.param_set_value(1.0);
    Geom::Ray transformed(point_a, point_b);
    previous_angle = transformed.angle();
    previous_length = Geom::distance(point_a, point_b);
    start.param_update_default(point_a);
    end.param_update_default(point_b);
    start.param_set_default();
    end.param_set_default();
}

Gtk::Widget *LPETransform2Pts::newWidget()
{
    // use manage here, because after deletion of Effect object, others might
    // still be pointing to this widget.
    auto const vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    vbox->set_margin(5);

    auto const button1 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);
    auto const button2 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);
    auto const button3 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);
    auto const button4 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);

    for (auto const param: param_vector) {
        if (!param->widget_is_visible) continue;

        auto const widg = param->param_newWidget();
        if (!widg) continue;

        auto parent = vbox;

        if (param->param_key == "first_knot" || param->param_key == "last_knot") {
            auto &scalar = dynamic_cast<UI::Widget::Scalar &>(*widg);
            Gtk::manage(&scalar);
            scalar.signal_value_changed().connect(sigc::mem_fun(*this, &LPETransform2Pts::updateIndex));

            auto const child_list = UI::get_children(scalar);
            auto &entry = dynamic_cast<Gtk::Entry &>(*child_list.at(1));
            entry.set_width_chars(3);
        } else if (param->param_key == "from_original_width" || param->param_key == "elastic") {
            parent = button1;
        } else if (param->param_key == "flip_horizontal" || param->param_key == "flip_vertical") {
            parent = button2;
        } else if (param->param_key == "lock_angle" || param->param_key == "lock_length") {
            parent = button3;
        }

        g_assert(parent);
        UI::pack_start(*parent, *widg, true, true, 2);

        if (auto const tip = param->param_getTooltip()) {
            widg->set_tooltip_markup(*tip);
        } else {
            widg->set_tooltip_text({});
            widg->set_has_tooltip(false);
        }
    }

    auto const reset = Gtk::make_managed<Gtk::Button>(Glib::ustring(_("Reset")));
    reset->signal_clicked().connect(sigc::mem_fun(*this, &LPETransform2Pts::reset));
    UI::pack_start(*button4, *reset, true, true, 2);

    UI::pack_start(*vbox, *button1, true, true, 2);
    UI::pack_start(*vbox, *button2, true, true, 2);
    UI::pack_start(*vbox, *button3, true, true, 2);
    UI::pack_start(*vbox, *button4, true, true, 2);
    return vbox;
}

Geom::Piecewise<Geom::D2<Geom::SBasis> >
LPETransform2Pts::doEffect_pwd2 (Geom::Piecewise<Geom::D2<Geom::SBasis> > const & pwd2_in)
{
    Geom::Piecewise<Geom::D2<Geom::SBasis> > output;
    double sca = Geom::distance((Geom::Point)start,(Geom::Point)end)/Geom::distance(point_a,point_b);
    Geom::Ray original(point_a,point_b);
    Geom::Ray transformed((Geom::Point)start,(Geom::Point)end);
    double rot = transformed.angle() - original.angle();
    Geom::Path helper;
    helper.start(point_a);
    helper.appendNew<Geom::LineSegment>(point_b);
    Geom::Affine m;
    Geom::Angle original_angle = original.angle();
    if(flip_horizontal && flip_vertical){
        m *= Geom::Rotate(-original_angle);
        m *= Geom::Scale(-1,-1);
        m *= Geom::Rotate(original_angle);
    } else if(flip_vertical){
        m *= Geom::Rotate(-original_angle);
        m *= Geom::Scale(1,-1);
        m *= Geom::Rotate(original_angle);
    } else if(flip_horizontal){
        m *= Geom::Rotate(-original_angle);
        m *= Geom::Scale(-1,1);
        m *= Geom::Rotate(original_angle);
    }
    if(stretch != 1){
        m *= Geom::Rotate(-original_angle);
        m *= Geom::Scale(1,stretch);
        m *= Geom::Rotate(original_angle);
    }
    if(elastic) {
        m *= Geom::Rotate(-original_angle);
        if(sca > 1){
            m *= Geom::Scale(sca, 1.0);
        } else {
            m *= Geom::Scale(sca, 1.0-((1.0-sca)/2.0));
        }
        m *= Geom::Rotate(transformed.angle());
    } else {
        m *= Geom::Scale(sca);
        m *= Geom::Rotate(rot);
    }
    helper *= m;
    Geom::Point trans = (Geom::Point)start - helper.initialPoint();
    if(flip_horizontal){
        trans = (Geom::Point)end - helper.initialPoint();
    }
    if(offset != 0){
        trans = Geom::Point::polar(transformed.angle() + Geom::rad_from_deg(-90),offset) + trans;
    }
    m *= Geom::Translate(trans);

    output.concat(pwd2_in * m);

    return output;
}

void
LPETransform2Pts::addCanvasIndicators(SPLPEItem const */*lpeitem*/, std::vector<Geom::PathVector> &hp_vec)
{
    using namespace Geom;
    hp_vec.clear();
    Geom::Path hp;
    hp.start((Geom::Point)start);
    hp.appendNew<Geom::LineSegment>((Geom::Point)end);
    Geom::PathVector pathv;
    pathv.push_back(hp);
    double r = helper_size*.1;
    if(lock_length || lock_angle ) {
        char const * svgd;
        svgd = "M -5.39,8.78 -9.13,5.29 -10.38,10.28 Z M -7.22,7.07 -3.43,3.37 m -1.95,-12.16 -3.74,3.5 -1.26,-5 z m -1.83,1.71 3.78,3.7 M 5.24,8.78 8.98,5.29 10.24,10.28 Z M 7.07,7.07 3.29,3.37 M 5.24,-8.78 l 3.74,3.5 1.26,-5 z M 7.07,-7.07 3.29,-3.37";
        PathVector pathv_move = sp_svg_read_pathv(svgd);
        pathv_move *= Affine(r,0,0,r,0,0) * Translate(Geom::Point(start));
        hp_vec.push_back(pathv_move);
    }
    if(!lock_angle && lock_length) {
        char const * svgd;
        svgd = "M 0,9.94 C -2.56,9.91 -5.17,8.98 -7.07,7.07 c -3.91,-3.9 -3.91,-10.24 0,-14.14 1.97,-1.97 4.51,-3.02 7.07,-3.04 2.56,0.02 5.1,1.07 7.07,3.04 3.91,3.9 3.91,10.24 0,14.14 C 5.17,8.98 2.56,9.91 0,9.94 Z";
        PathVector pathv_turn = sp_svg_read_pathv(svgd);
        pathv_turn *= Geom::Rotate(previous_angle);
        pathv_turn *= Affine(r,0,0,r,0,0) * Translate(Geom::Point(end));
        hp_vec.push_back(pathv_turn);
    }
    hp_vec.push_back(pathv);
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
