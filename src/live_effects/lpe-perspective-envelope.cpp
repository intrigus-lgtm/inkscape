// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * LPE <perspective-envelope> implementation
 */
/*
 * Authors:
 *   Jabiertxof Code migration from python extensions envelope and perspective
 *   Aaron Spike, aaron@ekips.org from envelope and perspective python code
 *   Dmitry Platonov, shadowjack@mail.ru, 2006 perspective approach & math
 *   Jose Hevia (freon) Transform algorithm from envelope
 *
 * Copyright (C) 2007-2014 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "lpe-perspective-envelope.h"

#include <glibmm/i18n.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <gtkmm/separator.h>
#include <gtkmm/widget.h>
#include <gsl/gsl_linalg.h>

#include "display/curve.h"
#include "helper/geom.h"
#include "object/sp-lpe-item.h"
#include "ui/pack.h"
#include "ui/util.h"

using namespace Geom;

namespace Inkscape::LivePathEffect {

enum DeformationType {
    DEFORMATION_PERSPECTIVE,
    DEFORMATION_ENVELOPE
};

static const Util::EnumData<unsigned> DeformationTypeData[] = {
    {DEFORMATION_PERSPECTIVE          , N_("Perspective"), "perspective"},
    {DEFORMATION_ENVELOPE          , N_("Envelope deformation"), "envelope_deformation"}
};

static const Util::EnumDataConverter<unsigned> DeformationTypeConverter(DeformationTypeData, sizeof(DeformationTypeData)/sizeof(*DeformationTypeData));

LPEPerspectiveEnvelope::LPEPerspectiveEnvelope(LivePathEffectObject *lpeobject) :
    Effect(lpeobject),
    horizontal_mirror(_("Mirror movements in horizontal"), _("Mirror movements in horizontal"), "horizontal_mirror", &wr, this, false),
    vertical_mirror(_("Mirror movements in vertical"), _("Mirror movements in vertical"), "vertical_mirror", &wr, this, false),
    overflow_perspective(_("Overflow perspective"), _("Overflow perspective"), "overflow_perspective", &wr, this, false),
    deform_type(_("Type"), _("Select the type of deformation"), "deform_type", DeformationTypeConverter, &wr, this, DEFORMATION_PERSPECTIVE),
    up_left_point(_("Top Left"), _("Top Left - <b>Ctrl+Alt+Click</b>: reset, <b>Ctrl</b>: move along axes"), "up_left_point", &wr, this),
    up_right_point(_("Top Right"), _("Top Right - <b>Ctrl+Alt+Click</b>: reset, <b>Ctrl</b>: move along axes"), "up_right_point", &wr, this),
    down_left_point(_("Down Left"), _("Down Left - <b>Ctrl+Alt+Click</b>: reset, <b>Ctrl</b>: move along axes"), "down_left_point", &wr, this),
    down_right_point(_("Down Right"), _("Down Right - <b>Ctrl+Alt+Click</b>: reset, <b>Ctrl</b>: move along axes"), "down_right_point", &wr, this)
{
    // register all your parameters here, so Inkscape knows which parameters this effect has:
    registerParameter(&deform_type);
    registerParameter(&horizontal_mirror);
    registerParameter(&vertical_mirror);
    registerParameter(&overflow_perspective);
    registerParameter(&up_left_point);
    registerParameter(&up_right_point);
    registerParameter(&down_left_point);
    registerParameter(&down_right_point);
    apply_to_clippath_and_mask = true;
}

LPEPerspectiveEnvelope::~LPEPerspectiveEnvelope() = default;

void LPEPerspectiveEnvelope::transform_multiply(Geom::Affine const &postmul, bool /*set*/)
{
    if (sp_lpe_item && sp_lpe_item->pathEffectsEnabled() && sp_lpe_item->optimizeTransforms()) {
        up_left_point.param_transform_multiply(postmul, false);
        up_right_point.param_transform_multiply(postmul, false);
        down_left_point.param_transform_multiply(postmul, false);
        down_right_point.param_transform_multiply(postmul, false);
    }
}

bool pointInTriangle(Geom::Point const &p, std::vector<Geom::Point> points)
{
    if (points.size() != 3) {
        g_warning("Incorrect number of points in pointInTriangle\n");
        return false;
    }
    Geom::Point p1 = points[0];
    Geom::Point p2 = points[1];
    Geom::Point p3 = points[2];
    // http://totologic.blogspot.com.es/2014/01/accurate-point-in-triangle-test.html
    using Geom::X;
    using Geom::Y;
    double denominator = (p1[X] * (p2[Y] - p3[Y]) + p1[Y] * (p3[X] - p2[X]) + p2[X] * p3[Y] - p2[Y] * p3[X]);
    double t1 = (p[X] * (p3[Y] - p1[Y]) + p[Y] * (p1[X] - p3[X]) - p1[X] * p3[Y] + p1[Y] * p3[X]) / denominator;
    double t2 = (p[X] * (p2[Y] - p1[Y]) + p[Y] * (p1[X] - p2[X]) - p1[X] * p2[Y] + p1[Y] * p2[X]) / -denominator;
    double s = t1 + t2;

    return 0 <= t1 && t1 <= 1 && 0 <= t2 && t2 <= 1 && s <= 1;
}

void LPEPerspectiveEnvelope::doEffect(SPCurve *curve)
{
    double projmatrix[3][3];
    if(deform_type == DEFORMATION_PERSPECTIVE) {
        using Geom::X;
        using Geom::Y;
        std::vector<Geom::Point> source_handles(4);
        source_handles[0] = Geom::Point(boundingbox_X.min(), boundingbox_Y.max());
        source_handles[1] = Geom::Point(boundingbox_X.min(), boundingbox_Y.min());
        source_handles[2] = Geom::Point(boundingbox_X.max(), boundingbox_Y.min());
        source_handles[3] = Geom::Point(boundingbox_X.max(), boundingbox_Y.max());
        double solmatrix[8][8] = {{0}};
        double free_term[8] = {0};
        double gslSolmatrix[64];
        for(unsigned int i = 0; i < 4; ++i) {
            solmatrix[i][0] = source_handles[i][X];
            solmatrix[i][1] = source_handles[i][Y];
            solmatrix[i][2] = 1;
            solmatrix[i][6] = -handles[i][X] * source_handles[i][X];
            solmatrix[i][7] = -handles[i][X] * source_handles[i][Y];
            solmatrix[i+4][3] = source_handles[i][X];
            solmatrix[i+4][4] = source_handles[i][Y];
            solmatrix[i+4][5] = 1;
            solmatrix[i+4][6] = -handles[i][Y] * source_handles[i][X];
            solmatrix[i+4][7] = -handles[i][Y] * source_handles[i][Y];
            free_term[i] = handles[i][X];
            free_term[i+4] = handles[i][Y];
        }
        int h = 0;
        for(auto & i : solmatrix) {
            for(double j : i) {
                gslSolmatrix[h] = j;
                h++;
            }
        }
        //this is get by this page:
        //http://www.gnu.org/software/gsl/manual/html_node/Linear-Algebra-Examples.html#Linear-Algebra-Examples
        gsl_matrix_view m = gsl_matrix_view_array (gslSolmatrix, 8, 8);
        gsl_vector_view b = gsl_vector_view_array (free_term, 8);
        gsl_vector *x = gsl_vector_alloc (8);
        int s;
        gsl_permutation * p = gsl_permutation_alloc (8);
        gsl_linalg_LU_decomp (&m.matrix, p, &s);
        gsl_linalg_LU_solve (&m.matrix, p, &b.vector, x);
        h = 0;
        for(auto & i : projmatrix) {
            for(double & j : i) {
                if(h==8) {
                    projmatrix[2][2] = 1.0;
                    continue;
                }
                j = gsl_vector_get(x, h);
                h++;
            }
        }
        gsl_permutation_free (p);
        gsl_vector_free (x);
    }
    Geom::PathVector const original_pathv = pathv_to_linear_and_cubic_beziers(curve->get_pathvector());
    curve->reset();
    Geom::CubicBezier const *cubic = nullptr;
    Geom::Point point_at1(0, 0);
    Geom::Point point_at2(0, 0);
    Geom::Point point_at3(0, 0);
    for (const auto & path_it : original_pathv) {
        //Si está vacío...
        if (path_it.empty())
            continue;
        //Itreadores
        SPCurve nCurve;
        Geom::Path::const_iterator curve_it1 = path_it.begin();
        Geom::Path::const_iterator curve_endit = path_it.end_default();

        if (path_it.closed()) {
            const Geom::Curve &closingline = path_it.back_closed();
            if (are_near(closingline.initialPoint(), closingline.finalPoint())) {
                curve_endit = path_it.end_open();
            }
        }
        if(deform_type == DEFORMATION_PERSPECTIVE) {
            nCurve.moveto(projectPoint(curve_it1->initialPoint(), projmatrix));
        } else {
            nCurve.moveto(projectPoint(curve_it1->initialPoint()));
        }
        while (curve_it1 != curve_endit) {
            cubic = dynamic_cast<Geom::CubicBezier const *>(&*curve_it1);
            if (cubic) {
                point_at1 = (*cubic)[1];
                point_at2 = (*cubic)[2];
            } else {
                point_at1 = curve_it1->initialPoint();
                point_at2 = curve_it1->finalPoint();
            }
            point_at3 = curve_it1->finalPoint();
            if(deform_type == DEFORMATION_PERSPECTIVE) {
                point_at1 = projectPoint(point_at1, projmatrix);
                point_at2 = projectPoint(point_at2, projmatrix);
                point_at3 = projectPoint(point_at3, projmatrix);
            } else {
                point_at1 = projectPoint(point_at1);
                point_at2 = projectPoint(point_at2);
                point_at3 = projectPoint(point_at3);
            }
            if (cubic) {
                nCurve.curveto(point_at1, point_at2, point_at3);
            } else {
                nCurve.lineto(point_at3);
            }
            ++curve_it1;
        }
        //y cerramos la curva
        if (path_it.closed()) {
            nCurve.move_endpoints(point_at3, point_at3);
            nCurve.closepath_current();
        }
        curve->append(std::move(nCurve));
    }
}

Geom::Point
LPEPerspectiveEnvelope::projectPoint(Geom::Point p)
{
    double width = boundingbox_X.extent();
    double height = boundingbox_Y.extent();
    double delta_x = boundingbox_X.min() - p[X];
    double delta_y = boundingbox_Y.max() - p[Y];
    Geom::Coord x_ratio = (delta_x * -1) / width;
    Geom::Coord y_ratio = delta_y / height;
    Geom::Line horiz;
    Geom::Line vert;
    vert.setPoints (pointAtRatio(y_ratio,down_left_point,up_left_point),pointAtRatio(y_ratio,down_right_point,up_right_point));
    horiz.setPoints (pointAtRatio(x_ratio,down_left_point,down_right_point),pointAtRatio(x_ratio,up_left_point,up_right_point));

    OptCrossing crossPoint = intersection(horiz,vert);
    if(crossPoint) {
        return horiz.pointAt(Geom::Coord(crossPoint->ta));
    } else {
        return p;
    }
}

Geom::Point
LPEPerspectiveEnvelope::projectPoint(Geom::Point p, double m[][3])
{
    Geom::Coord x = p[0];
    Geom::Coord y = p[1];
    return Geom::Point(
               Geom::Coord((x*m[0][0] + y*m[0][1] + m[0][2])/(x*m[2][0]+y*m[2][1]+m[2][2])),
               Geom::Coord((x*m[1][0] + y*m[1][1] + m[1][2])/(x*m[2][0]+y*m[2][1]+m[2][2])));
}

Geom::Point
LPEPerspectiveEnvelope::pointAtRatio(Geom::Coord ratio,Geom::Point A, Geom::Point B)
{
    Geom::Coord x = A[X] + (ratio * (B[X]-A[X]));
    Geom::Coord y = A[Y]+ (ratio * (B[Y]-A[Y]));
    return Point(x, y);
}

Gtk::Widget *
LPEPerspectiveEnvelope::newWidget()
{
    // use manage here, because after deletion of Effect object, others might still be pointing to this widget.
    auto const vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    vbox->set_margin(5);

    auto const hbox_up_handles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);
    auto const hbox_down_handles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);

    for (auto const param: param_vector) {
        if (!param->widget_is_visible) continue;

        auto const widg = param->param_newWidget();
        if (!widg) continue;

        if (param->param_key == "up_left_point" ||
            param->param_key == "up_right_point" ||
            param->param_key == "down_left_point" ||
            param->param_key == "down_right_point")
        {
            auto &point_hbox = dynamic_cast<Gtk::Box &>(*widg);
            auto const child_list = UI::get_children(point_hbox);
            auto &point_hboxHBox = dynamic_cast<Gtk::Box &>(*child_list.at(0));
            auto const child_list2 = UI::get_children(point_hboxHBox);
            point_hboxHBox.remove(*child_list2.at(0));

            if (param->param_key == "up_left_point") {
                auto const handles = Gtk::make_managed<Gtk::Label>(Glib::ustring(_("Handles:")),Gtk::Align::START);
                UI::pack_start(*vbox, *handles, false, false, 2);
                UI::pack_start(*hbox_up_handles, *widg, true, true, 2);
                UI::pack_start(*hbox_up_handles, *Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL),
                               UI::PackOptions::expand_padding);
            } else if (param->param_key == "up_right_point") {
                UI::pack_start(*hbox_up_handles, *widg, true, true, 2);
            } else if (param->param_key == "down_left_point") {
                UI::pack_start(*hbox_down_handles, *widg, true, true, 2);
                UI::pack_start(*hbox_down_handles, *Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL),
                               UI::PackOptions::expand_padding);
            } else {
                UI::pack_start(*hbox_down_handles, *widg, true, true, 2);
            }

            if (auto const tip = param->param_getTooltip()) {
                widg->set_tooltip_markup(*tip);
            } else {
                widg->set_tooltip_text({});
                widg->set_has_tooltip(false);
            }
        }
    }

    UI::pack_start(*vbox, *hbox_up_handles,true, true, 2);
    UI::pack_start(*vbox, *Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL), UI::PackOptions::expand_widget);
    UI::pack_start(*vbox, *hbox_down_handles, true, true, 2);
    auto const reset_button = Gtk::make_managed<Gtk::Button>(_("_Clear"), true);
    reset_button->set_image_from_icon_name("edit-clear");
    reset_button->signal_clicked().connect(sigc::mem_fun (*this,&LPEPerspectiveEnvelope::resetGrid));
    reset_button->set_size_request(140,30);
    reset_button->set_halign(Gtk::Align::START);
    UI::pack_start(*vbox, *reset_button, false, false, 2);
    return vbox;
}

void
LPEPerspectiveEnvelope::vertical(PointParam &param_one, PointParam &param_two, Geom::Line vert)
{
    Geom::Point A = param_one;
    Geom::Point B = param_two;
    double Y = (A[Geom::Y] + B[Geom::Y])/2;
    A[Geom::Y] = Y;
    B[Geom::Y] = Y;
    Geom::Point nearest = vert.pointAt(vert.nearestTime(A));
    double distance_one = Geom::distance(A,nearest);
    double distance_two = Geom::distance(B,nearest);
    double distance_middle = (distance_one + distance_two)/2;
    if(A[Geom::X] > B[Geom::X]) {
        distance_middle *= -1;
    }
    A[Geom::X] = nearest[Geom::X] - distance_middle;
    B[Geom::X] = nearest[Geom::X] + distance_middle;
    param_one.param_setValue(A);
    param_two.param_setValue(B);
}

void
LPEPerspectiveEnvelope::horizontal(PointParam &param_one, PointParam &param_two, Geom::Line horiz)
{
    Geom::Point A = param_one;
    Geom::Point B = param_two;
    double X = (A[Geom::X] + B[Geom::X])/2;
    A[Geom::X] = X;
    B[Geom::X] = X;
    Geom::Point nearest = horiz.pointAt(horiz.nearestTime(A));
    double distance_one = Geom::distance(A,nearest);
    double distance_two = Geom::distance(B,nearest);
    double distance_middle = (distance_one + distance_two)/2;
    if(A[Geom::Y] > B[Geom::Y]) {
        distance_middle *= -1;
    }
    A[Geom::Y] = nearest[Geom::Y] - distance_middle;
    B[Geom::Y] = nearest[Geom::Y] + distance_middle;
    param_one.param_setValue(A);
    param_two.param_setValue(B);
}

void
LPEPerspectiveEnvelope::doBeforeEffect (SPLPEItem const* lpeitem)
{
    original_bbox(lpeitem, false, true);
    if (Geom::are_near(boundingbox_X.min(),boundingbox_X.max()) || 
        Geom::are_near(boundingbox_Y.min(),boundingbox_Y.max())) 
    {
        g_warning("Couldn`t apply perspective/envelope to a element with geometric width or height equal 0 we add a temporary bounding box to allow handle");
        if (Geom::are_near(boundingbox_X.min(), boundingbox_X.max())) {
            boundingbox_X = Geom::Interval(boundingbox_X.min() - 3, boundingbox_X.max() + 3);
        }
        if (Geom::are_near(boundingbox_Y.min(), boundingbox_Y.max())) {
            boundingbox_Y = Geom::Interval(boundingbox_Y.min() - 3, boundingbox_Y.max() + 3);
        }
    }
    Geom::Line vert(Geom::Point(boundingbox_X.middle(),boundingbox_Y.max()), Geom::Point(boundingbox_X.middle(), boundingbox_Y.min()));
    Geom::Line horiz(Geom::Point(boundingbox_X.min(),boundingbox_Y.middle()), Geom::Point(boundingbox_X.max(), boundingbox_Y.middle()));
    if(vertical_mirror) {
        vertical(up_left_point, up_right_point,vert);
        vertical(down_left_point, down_right_point,vert);
    }
    if(horizontal_mirror) {
        horizontal(up_left_point, down_left_point,horiz);
        horizontal(up_right_point, down_right_point,horiz);
    }
    setDefaults();
    if (are_near(up_left_point, up_right_point) && are_near(up_right_point, down_left_point) &&
        are_near(down_left_point, down_right_point)) {
        g_warning(
            "Perspective/Envelope LPE::doBeforeEffect - lpeobj with invalid parameter, the same value in 4 handles!");
        resetGrid();
        return;
    }
    if (deform_type == DEFORMATION_PERSPECTIVE) {
        if (!overflow_perspective && handles.size() == 4) {
            bool move0 = false;
            if (handles[0] != down_left_point) {
                move0 = true;
            }
            bool move1 = false;
            if (handles[1] != up_left_point) {
                move1 = true;
            }
            bool move2 = false;
            if (handles[2] != up_right_point) {
                move2 = true;
            }
            bool move3 = false;
            if (handles[3] != down_right_point) {
                move3 = true;
            }
            handles.resize(4);
            handles[0] = down_left_point;
            handles[1] = up_left_point;
            handles[2] = up_right_point;
            handles[3] = down_right_point;
            Geom::Line line_a(handles[3], handles[1]);
            Geom::Line line_b(handles[1], handles[2]);
            Geom::Line line_c(handles[2], handles[3]);
            int position_a = Geom::sgn(Geom::cross(handles[3] - handles[1], handles[0] - handles[1]));
            int position_b = Geom::sgn(Geom::cross(handles[1] - handles[2], handles[0] - handles[2]));
            int position_c = Geom::sgn(Geom::cross(handles[2] - handles[3], handles[0] - handles[3]));
            if (position_a != 1 && move0) {
                Geom::Point point_a = line_a.pointAt(line_a.nearestTime(handles[0]));
                down_left_point.param_setValue(point_a, true);
            }
            if (position_b == 1 && move0) {
                Geom::Point point_b = line_b.pointAt(line_b.nearestTime(handles[0]));
                down_left_point.param_setValue(point_b, true);
            }
            if (position_c == 1 && move0) {
                Geom::Point point_c = line_c.pointAt(line_c.nearestTime(handles[0]));
                down_left_point.param_setValue(point_c, true);
            }
            line_a.setPoints(handles[0], handles[2]);
            line_b.setPoints(handles[2], handles[3]);
            line_c.setPoints(handles[3], handles[0]);
            position_a = Geom::sgn(Geom::cross(handles[0] - handles[2], handles[1] - handles[2]));
            position_b = Geom::sgn(Geom::cross(handles[2] - handles[3], handles[1] - handles[3]));
            position_c = Geom::sgn(Geom::cross(handles[3] - handles[0], handles[1] - handles[0]));
            if (position_a != 1 && move1) {
                Geom::Point point_a = line_a.pointAt(line_a.nearestTime(handles[1]));
                up_left_point.param_setValue(point_a, true);
            }
            if (position_b == 1 && move1) {
                Geom::Point point_b = line_b.pointAt(line_b.nearestTime(handles[1]));
                up_left_point.param_setValue(point_b, true);
            }
            if (position_c == 1 && move1) {
                Geom::Point point_c = line_c.pointAt(line_c.nearestTime(handles[1]));
                up_left_point.param_setValue(point_c, true);
            }
            line_a.setPoints(handles[1], handles[3]);
            line_b.setPoints(handles[3], handles[0]);
            line_c.setPoints(handles[0], handles[1]);
            position_a = Geom::sgn(Geom::cross(handles[1] - handles[3], handles[2] - handles[3]));
            position_b = Geom::sgn(Geom::cross(handles[3] - handles[0], handles[2] - handles[0]));
            position_c = Geom::sgn(Geom::cross(handles[0] - handles[1], handles[2] - handles[1]));
            if (position_a != 1 && move2) {
                Geom::Point point_a = line_a.pointAt(line_a.nearestTime(handles[2]));
                up_right_point.param_setValue(point_a, true);
            }
            if (position_b == 1 && move2) {
                Geom::Point point_b = line_b.pointAt(line_b.nearestTime(handles[2]));
                up_right_point.param_setValue(point_b, true);
            }
            if (position_c == 1 && move2) {
                Geom::Point point_c = line_c.pointAt(line_c.nearestTime(handles[2]));
                up_right_point.param_setValue(point_c, true);
            }
            line_a.setPoints(handles[2], handles[0]);
            line_b.setPoints(handles[0], handles[1]);
            line_c.setPoints(handles[1], handles[2]);
            position_a = Geom::sgn(Geom::cross(handles[2] - handles[0], handles[3] - handles[0]));
            position_b = Geom::sgn(Geom::cross(handles[0] - handles[1], handles[3] - handles[1]));
            position_c = Geom::sgn(Geom::cross(handles[1] - handles[2], handles[3] - handles[2]));
            if (position_a != 1 && move3) {
                Geom::Point point_a = line_a.pointAt(line_a.nearestTime(handles[3]));
                down_right_point.param_setValue(point_a, true);
            }
            if (position_b == 1 && move3) {
                Geom::Point point_b = line_b.pointAt(line_b.nearestTime(handles[3]));
                down_right_point.param_setValue(point_b, true);
            }
            if (position_c == 1 && move3) {
                Geom::Point point_c = line_c.pointAt(line_c.nearestTime(handles[3]));
                down_right_point.param_setValue(point_c, true);
            }
        } else {
            handles.resize(4);
            handles[0] = down_left_point;
            handles[1] = up_left_point;
            handles[2] = up_right_point;
            handles[3] = down_right_point;
        }
    }
}

void
LPEPerspectiveEnvelope::setDefaults()
{
    if (Geom::are_near(boundingbox_X.min(),boundingbox_X.max()) || 
        Geom::are_near(boundingbox_Y.min(),boundingbox_Y.max())) 
    {
        if (Geom::are_near(boundingbox_X.min(), boundingbox_X.max())) {
            boundingbox_X = Geom::Interval(boundingbox_X.min() - 3, boundingbox_X.max() + 3);
        }
        if (Geom::are_near(boundingbox_Y.min(), boundingbox_Y.max())) {
            boundingbox_Y = Geom::Interval(boundingbox_Y.min() - 3, boundingbox_Y.max() + 3);
        }
    }
    Geom::Point up_left(boundingbox_X.min(), boundingbox_Y.min());
    Geom::Point up_right(boundingbox_X.max(), boundingbox_Y.min());
    Geom::Point down_left(boundingbox_X.min(), boundingbox_Y.max());
    Geom::Point down_right(boundingbox_X.max(), boundingbox_Y.max());

    up_left_point.param_update_default(up_left);
    up_right_point.param_update_default(up_right);
    down_right_point.param_update_default(down_right);
    down_left_point.param_update_default(down_left);
}

void
LPEPerspectiveEnvelope::resetGrid()
{
    up_left_point.param_set_default();
    up_right_point.param_set_default();
    down_right_point.param_set_default();
    down_left_point.param_set_default();
}

void
LPEPerspectiveEnvelope::resetDefaults(SPItem const* item)
{
    Effect::resetDefaults(item);
    original_bbox(cast<SPLPEItem>(item), false, true);
    setDefaults();
    resetGrid();
}

void
LPEPerspectiveEnvelope::addCanvasIndicators(SPLPEItem const */*lpeitem*/, std::vector<Geom::PathVector> &hp_vec)
{
    hp_vec.clear();

    SPCurve c;
    c.moveto(up_left_point);
    c.lineto(up_right_point);
    c.lineto(down_right_point);
    c.lineto(down_left_point);
    c.lineto(up_left_point);
    hp_vec.push_back(c.get_pathvector());
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
