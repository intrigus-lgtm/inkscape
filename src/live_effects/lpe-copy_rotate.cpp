// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * LPE <copy_rotate> implementation
 */
/*
 * Authors:
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *   Johan Engelen <j.b.c.engelen@alumnus.utwente.nl>
 *   Jabiertxo Arraiza Cenoz <jabier.arraiza@marker.es>
 * Copyright (C) Authors 2007-2012
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "lpe-copy_rotate.h"

#include <gdk/gdk.h>
#include <gtkmm/widget.h>
#include <gtkmm/box.h>

#include <2geom/intersection-graph.h>
#include <2geom/path-intersection.h>
#include <2geom/sbasis-to-bezier.h>

#include "inkscape.h"
#include "style.h"
#include "display/curve.h"
#include "helper/geom.h"
#include "live_effects/lpeobject.h"
#include "live_effects/parameter/satellite-reference.h"
#include "object/sp-item-group.h"
#include "object/sp-object.h"
#include "object/sp-path.h"
#include "object/sp-shape.h"
#include "object/sp-text.h"
#include "path/path-boolop.h"
#include "svg/path-string.h"
#include "svg/svg.h"
#include "ui/pack.h"
#include "xml/sp-css-attr.h"

// TODO due to internal breakage in glibmm headers, this must be last:
#include <glibmm/i18n.h>

namespace Inkscape {
namespace LivePathEffect {

static const Util::EnumData<RotateMethod> RotateMethodData[RM_END] = {
    { RM_NORMAL, N_("Normal"), "normal" },
    { RM_KALEIDOSCOPE, N_("Kaleidoscope"), "kaleidoskope" },
    { RM_FUSE, N_("Fuse paths"), "fuse_paths" }
};
static const Util::EnumDataConverter<RotateMethod>
RMConverter(RotateMethodData, RM_END);

LPECopyRotate::LPECopyRotate(LivePathEffectObject *lpeobject) :
    Effect(lpeobject),
    // do not change name of this parameter us used in oncommit
    lpesatellites(_("lpesatellites"), _("Items satellites"), "lpesatellites", &wr, this, false),
    method(_("Method:"), _("Rotate methods"), "method", RMConverter, &wr, this, RM_NORMAL),
    origin(_("Origin"), _("Adjust origin of the rotation"), "origin", &wr, this,  _("Adjust origin of the rotation")),
    starting_point(_("Start point"), _("Starting point to define start angle"), "starting_point", &wr, this, _("Adjust starting point to define start angle")),
    starting_angle(_("Starting angle"), _("Angle of the first copy"), "starting_angle", &wr, this, 0.0),
    rotation_angle(_("Rotation angle"), _("Angle between two successive copies"), "rotation_angle", &wr, this, 60.0),
    num_copies(_("Number of copies"), _("Number of copies of the original path"), "num_copies", &wr, this, 6),
    gap(_("Gap"), _("Gap space between copies, use small negative gaps to fix some joins"), "gap", &wr, this, -0.01),
    copies_to_360(_("Distribute evenly"), _("Angle between copies is 360°/number of copies (ignores rotation angle setting)"), "copies_to_360", &wr, this, true),
    mirror_copies(_("Mirror copies"), _("Mirror between copies"), "mirror_copies", &wr, this, false),
    split_items(_("Split elements"), _("Split elements, so each can have its own style"), "split_items", &wr, this, false),
    link_styles(_("Link styles"), _("Link styles on split mode"), "link_styles", &wr, this, false),
    dist_angle_handle(100.0)
{
    show_orig_path = true;
    _provides_knotholder_entities = true;
    //0.92 compatibility
    if (this->getRepr()->attribute("fuse_paths") && strcmp(this->getRepr()->attribute("fuse_paths"), "true") == 0){
        this->getRepr()->removeAttribute("fuse_paths");
        this->getRepr()->setAttribute("method", "kaleidoskope");
        this->getRepr()->setAttribute("mirror_copies", "true");
    };
    // register all your parameters here, so Inkscape knows which parameters this effect has:
    registerParameter(&lpesatellites);
    registerParameter(&method);
    registerParameter(&num_copies);
    registerParameter(&starting_angle);
    registerParameter(&starting_point);
    registerParameter(&rotation_angle);
    registerParameter(&gap);
    registerParameter(&origin);
    registerParameter(&copies_to_360);
    registerParameter(&mirror_copies);
    registerParameter(&split_items);
    registerParameter(&link_styles);
    gap.param_set_range(std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max());
    gap.param_set_increments(0.01, 0.01);
    gap.param_set_digits(5);
    rotation_angle.param_set_digits(4);
    num_copies.param_set_range(1, std::numeric_limits<gint>::max());
    num_copies.param_make_integer();
    apply_to_clippath_and_mask = true;
    previous_num_copies = num_copies;
    previous_origin = Geom::Point(0,0);
    previous_start_point = Geom::Point(0,0);
    starting_point.param_widget_is_visible(false);
    reset = link_styles;
}

LPECopyRotate::~LPECopyRotate() = default;

bool LPECopyRotate::doOnOpen(SPLPEItem const *lpeitem)
{
    bool fixed = false;
    if (!is_load || is_applied) {
        return fixed;
    }
    legacytest_livarotonly = false;
    Glib::ustring version = lpeversion.param_getSVGValue();
    if (version < "1.2") {
        if (!SP_ACTIVE_DESKTOP) {
            legacytest_livarotonly = true;
        }
        if (!split_items) {
            return fixed;
        }
        lpesatellites.clear();
        for (size_t i = 0; i < num_copies - 1; i++) {
            Glib::ustring id = Glib::ustring("rotated-");
            id += std::to_string(i);
            id += "-";
            id += getLPEObj()->getId();
            SPObject *elemref = getSPDoc()->getObjectById(id.c_str());
            if (elemref) {
                lpesatellites.link(elemref, i);
            }
        }
        lpeversion.param_setValue("1.2", true);
        fixed = true;
        lpesatellites.write_to_SVG();
    }
    if (!split_items) {
        return fixed;
    }
    lpesatellites.start_listening();
    lpesatellites.connect_selection_changed();
    container = lpeitem->parent;
    return fixed;
}

void
LPECopyRotate::doAfterEffect (SPLPEItem const* lpeitem, SPCurve *curve)
{
    if (split_items) {
        SPDocument *document = getSPDoc();
        if (!document) {
            return;
        }
        bool write = false;
        bool active = !lpesatellites.data().size();
        for (auto lpereference : lpesatellites.data()) {
            if (lpereference && lpereference->isAttached() && lpereference.get()->getObject() != nullptr) {
                active = true;
            }
        }
        if (!active && !is_load && previous_split) {
            lpesatellites.clear();
            previous_num_copies = 0;
            return;
        }

        container = sp_lpe_item->parent;
        if (previous_num_copies != num_copies) {
            write = true;
            size_t pos = 0;
            for (auto lpereference : lpesatellites.data()) {
                if (lpereference && lpereference->isAttached()) {
                    auto copies = cast<SPItem>(lpereference->getObject());
                    if (copies) {
                        if (pos > num_copies - 2) {
                            copies->setHidden(true);
                        } else if (copies->isHidden()) {
                            copies->setHidden(false);
                        }
                    }
                }
                pos++;
            }
            previous_num_copies = num_copies;
        }
        bool forcewrite = write;
        Geom::Affine m = Geom::Translate(-origin) * Geom::Rotate(-(Geom::rad_from_deg(starting_angle)));
        for (size_t i = 1; i < num_copies; ++i) {
            Geom::Affine r = Geom::identity();
            if(mirror_copies && i%2 != 0) {
                r *= Geom::Rotate(Geom::Angle(half_dir)).inverse();
                r *= Geom::Scale(1, -1);
                r *= Geom::Rotate(Geom::Angle(half_dir));
            }

            Geom::Rotate rot(-(Geom::rad_from_deg(rotation_angle * i)));
            Geom::Affine t = m * r * rot * Geom::Rotate(Geom::rad_from_deg(starting_angle)) * Geom::Translate(origin);
            if (method != RM_NORMAL) {
                if(mirror_copies && i%2 != 0) {
                    t = m *  r * rot * Geom::Rotate(-Geom::rad_from_deg(starting_angle)) * Geom::Translate(origin);
                }
            } else {
                if(mirror_copies && i%2 != 0) {
                    t = m * Geom::Rotate(Geom::rad_from_deg(-rotation_angle)) * r * rot * Geom::Rotate(-Geom::rad_from_deg(starting_angle)) * Geom::Translate(origin);
                }
            }
            t *= sp_lpe_item->transform;
            toItem(t, i-1, reset, write);
            forcewrite = forcewrite || write;
        }
        //we keep satellites connected and active if write needed
        bool connected = lpesatellites.is_connected();
        if (forcewrite || !connected) {
            lpesatellites.write_to_SVG();
            lpesatellites.start_listening();
            if (!connected) {
                sp_lpe_item_update_patheffect(sp_lpe_item, false, false, true);
            } else {
                lpesatellites.update_satellites();
            }
        }
        reset = link_styles;
    }
    previous_split = split_items;
}

void LPECopyRotate::cloneStyle(SPObject *orig, SPObject *dest)
{
    dest->setAttribute("transform", orig->getAttribute("transform"));
    dest->setAttribute("style", orig->getAttribute("style"));
    dest->setAttribute("mask", orig->getAttribute("mask"));
    dest->setAttribute("clip-path", orig->getAttribute("clip-path"));
    dest->setAttribute("class", orig->getAttribute("class"));
    for (auto iter : orig->style->properties()) {
        if (iter->style_src != SPStyleSrc::UNSET) {
            auto key = iter->id();
            if (key != SPAttr::FONT && key != SPAttr::D && key != SPAttr::MARKER) {
                const gchar *attr = orig->getAttribute(iter->name().c_str());
                if (attr) {
                    dest->setAttribute(iter->name(), attr);
                }
            }
        }
    }
}

void LPECopyRotate::cloneD(SPObject *orig, SPObject *dest)
{
    SPDocument *document = getSPDoc();
    if (!document) {
        return;
    }
    dest->setAttribute("transform", orig->getAttribute("transform"));
    if (is<SPGroup>(orig) && is<SPGroup>(dest) && cast_unsafe<SPGroup>(orig)->getItemCount() == cast_unsafe<SPGroup>(dest)->getItemCount()) {
        if (reset) {
            cloneStyle(orig, dest);
        }
        std::vector< SPObject * > childs = orig->childList(true);
        size_t index = 0;
        for (auto & child : childs) {
            SPObject *dest_child = dest->nthChild(index);
            cloneD(child, dest_child);
            index++;
        }
        return;
    } else if( is<SPGroup>(orig) && is<SPGroup>(dest) && cast<SPGroup>(orig)->getItemCount() != cast<SPGroup>(dest)->getItemCount()) {
        split_items.param_setValue(false);
        return;
    }

    if ( is<SPText>(orig) && is<SPText>(dest) && cast<SPText>(orig)->children.size() == cast<SPText>(dest)->children.size()) {
        if (reset) {
            cloneStyle(orig, dest);
        }
        size_t index = 0;
        for (auto & child : cast<SPText>(orig)->children) {
            SPObject *dest_child = dest->nthChild(index);
            cloneD(&child, dest_child);
            index++;
        }
    }
    
    auto shape = cast<SPShape>(orig);
    auto path = cast<SPPath>(dest);
    if (shape) {
        SPCurve const *c = shape->curve();
        if (c) {
            auto str = sp_svg_write_path(c->get_pathvector());
            if (shape && !path) {
                const char *id = dest->getAttribute("id");
                const char *style = dest->getAttribute("style");
                Inkscape::XML::Document *xml_doc = dest->document->getReprDoc();
                Inkscape::XML::Node *dest_node = xml_doc->createElement("svg:path");
                dest_node->setAttribute("id", id);
                dest_node->setAttribute("style", style);
                dest->updateRepr(xml_doc, dest_node, SP_OBJECT_WRITE_ALL);
                path =  cast<SPPath>(dest);
            }
            path->setAttribute("d", str);
        } else {
            path->removeAttribute("d");
        }
        
    }
    if (reset) {
        cloneStyle(orig, dest);
    }
}

Inkscape::XML::Node *
LPECopyRotate::createPathBase(SPObject *elemref) {
    SPDocument *document = getSPDoc();
    if (!document) {
        return nullptr;
    }
    Inkscape::XML::Document *xml_doc = document->getReprDoc();
    Inkscape::XML::Node *prev = elemref->getRepr();
    auto group = cast<SPGroup>(elemref);
    if (group) {
        Inkscape::XML::Node *container = xml_doc->createElement("svg:g");
        container->setAttribute("transform", prev->attribute("transform"));
        container->setAttribute("mask", prev->attribute("mask"));
        container->setAttribute("clip-path", prev->attribute("clip-path"));
        container->setAttribute("class", prev->attribute("class"));
        container->setAttribute("style", prev->attribute("style"));
        std::vector<SPItem*> const item_list = group->item_list();
        Inkscape::XML::Node *previous = nullptr;
        for (auto sub_item : item_list) {
            Inkscape::XML::Node *resultnode = createPathBase(sub_item);

            container->addChild(resultnode, previous);
            previous = resultnode;
        }
        return container;
    }
    Inkscape::XML::Node *resultnode = xml_doc->createElement("svg:path");
    resultnode->setAttribute("transform", prev->attribute("transform"));
    resultnode->setAttribute("style", prev->attribute("style"));
    resultnode->setAttribute("mask", prev->attribute("mask"));
    resultnode->setAttribute("clip-path", prev->attribute("clip-path"));
    resultnode->setAttribute("class", prev->attribute("class"));
    return resultnode;
}

void
LPECopyRotate::toItem(Geom::Affine transform, size_t i, bool reset, bool &write)
{
    SPDocument *document = getSPDoc();
    if (!document) {
        return;
    }
    //Inkscape::XML::Document *xml_doc = document->getReprDoc();

    SPObject *elemref = nullptr;
    if (container != sp_lpe_item->parent) {
        lpesatellites.read_from_SVG();
        return;
    }
    if (lpesatellites.data().size() > i && lpesatellites.data()[i]) {
        elemref = lpesatellites.data()[i]->getObject();
    }
    Inkscape::XML::Node *phantom = nullptr;
    bool creation = false;
    if (elemref) {
        phantom = elemref->getRepr();
    } else {
        creation = true;
        phantom = createPathBase(sp_lpe_item);
        reset = true;
        elemref = container->appendChildRepr(phantom);

        Inkscape::GC::release(phantom);
    }
    cloneD(sp_lpe_item, elemref);
    elemref->setAttributeOrRemoveIfEmpty("transform", sp_svg_transform_write(transform));
    reset = link_styles;
    // allow use on clones even in different parent
    /* if (elemref->parent != container) {
        if (!creation) {
            lpesatellites.unlink(elemref);
        }
        Inkscape::XML::Node *copy = phantom->duplicate(xml_doc);
        copy->setAttribute("id", elemref->getId());
        lpesatellites.link(container->appendChildRepr(copy), i);
        Inkscape::GC::release(copy);
        elemref->deleteObject();
    } else  */
    if (creation) {
        write = true;
        lpesatellites.link(elemref, i);
    }
}

Gtk::Widget * LPECopyRotate::newWidget()
{
    // use manage here, because after deletion of Effect object, others might
    // still be pointing to this widget.
    auto const vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    vbox->set_margin(5);

    std::vector<Parameter *>::iterator it = param_vector.begin();
    while (it != param_vector.end()) {
        if ((*it)->widget_is_visible) {
            Parameter *param = *it;
            auto const widg = param->param_newWidget();

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


void
LPECopyRotate::doOnApply(SPLPEItem const* lpeitem)
{
    using namespace Geom;
    original_bbox(lpeitem, false, true);

    A = Point(boundingbox_X.min(), boundingbox_Y.middle());
    B = Point(boundingbox_X.middle(), boundingbox_Y.middle());
    origin.param_setValue(A, true);
    origin.param_update_default(A);
    dist_angle_handle = L2(B - A);
    dir = unit_vector(B - A);
    lpeversion.param_setValue("1.2", true);
}

void
LPECopyRotate::doBeforeEffect (SPLPEItem const* lpeitem)
{
    using namespace Geom;
    if (!split_items && lpesatellites.data().size()) {
        processObjects(LPE_ERASE);
    }
    if (link_styles) {
        reset = true;
    }
    if (split_items && !lpesatellites.data().size()) {
        lpesatellites.read_from_SVG();
        if (lpesatellites.data().size()) {
            lpesatellites.update_satellites();
        }
    }
    original_bbox(lpeitem, false, true);
    if (copies_to_360 && num_copies > 2) {
        rotation_angle.param_set_value(360.0/(double)num_copies);
    }
    if (method != RM_NORMAL && rotation_angle * num_copies > 360 && rotation_angle > 0 && copies_to_360) {
        num_copies.param_set_value(floor(360/rotation_angle));
    }
    if (method != RM_NORMAL  && mirror_copies && copies_to_360) {
        num_copies.param_set_increments(2.0,10.0);
        if ((int)num_copies%2 !=0) {
            num_copies.param_set_value(num_copies+1);
            rotation_angle.param_set_value(360.0/(double)num_copies);
        }
    } else {
        num_copies.param_set_increments(1.0, 10.0);
    }

    A = Point(boundingbox_X.min(), boundingbox_Y.middle());
    B = Point(boundingbox_X.middle(), boundingbox_Y.middle());
    if (Geom::are_near(A, B, 0.01)) {
        B += Geom::Point(1.0, 0.0);
    }
    dir = unit_vector(B - A);
    // I first suspected the minus sign to be a bug in 2geom but it is
    // likely due to SVG's choice of coordinate system orientation (max)
    bool near_start_point = Geom::are_near(previous_start_point, (Geom::Point)starting_point, 0.01);
    bool near_origin = Geom::are_near(previous_origin, (Geom::Point)origin, 0.01);
    if (!near_start_point && !is_load) {
        if (lpeitem->document->isSensitive()) {
            starting_angle.param_set_value(deg_from_rad(-angle_between(dir, starting_point - origin)));
        }
        // FIXME: This will always be true! Did we mean to check if some state contains Shift flag?
        if (GDK_SHIFT_MASK) {
            dist_angle_handle = L2(B - A);
        } else {
            dist_angle_handle = L2(starting_point - origin);
        }
    }
    if (dist_angle_handle < 1.0) {
        dist_angle_handle = 1.0;
    }
    double distance = dist_angle_handle;
    if (previous_start_point != Geom::Point(0,0) || previous_origin != Geom::Point(0,0)) {
        distance = Geom::distance(previous_origin, starting_point);
    }
    start_pos = origin + dir * Rotate(-rad_from_deg(starting_angle)) * distance;
    if (!near_start_point || !near_origin || split_items) {
        starting_point.param_setValue(start_pos);
    }

    previous_origin = (Geom::Point)origin;
    previous_start_point = (Geom::Point)starting_point;
}

void
LPECopyRotate::split(Geom::PathVector &path_on, Geom::Path const &divider)
{
    Geom::PathVector tmp_path;
    double time_start = 0.0;
    Geom::Path original = path_on[0];
    int position = 0;
    Geom::Crossings cs = crossings(original,divider);
    std::vector<double> crossed;
    for(auto & c : cs) {
        crossed.push_back(c.ta);
    }
    std::sort(crossed.begin(), crossed.end());
    for (double time_end : crossed) {
        if (time_start == time_end || time_end - time_start < Geom::EPSILON) {
            continue;
        }
        Geom::Path portion_original = original.portion(time_start,time_end);
        if (!portion_original.empty()) {
            Geom::Point side_checker = portion_original.pointAt(0.0001);
            position = Geom::sgn(Geom::cross(divider[1].finalPoint() - divider[0].finalPoint(), side_checker - divider[0].finalPoint()));
            if (rotation_angle != 180) {
                position = pointInTriangle(side_checker, divider.initialPoint(), divider[0].finalPoint(), divider[1].finalPoint());
            }
            if (position == 1) {
                tmp_path.push_back(portion_original);
            }
            portion_original.clear();
            time_start = time_end;
        }
    }
    position = Geom::sgn(Geom::cross(divider[1].finalPoint() - divider[0].finalPoint(), original.finalPoint() - divider[0].finalPoint()));
    if (rotation_angle != 180) {
        position = pointInTriangle(original.finalPoint(), divider.initialPoint(), divider[0].finalPoint(), divider[1].finalPoint());
    }
    if (cs.size() > 0 && position == 1) {
        Geom::Path portion_original = original.portion(time_start, original.size());
        if(!portion_original.empty()){
            if (!original.closed()) {
                tmp_path.push_back(portion_original);
            } else {
                if (tmp_path.size() > 0 && tmp_path[0].size() > 0 ) {
                    portion_original.setFinal(tmp_path[0].initialPoint());
                    portion_original.append(tmp_path[0]);
                    tmp_path[0] = portion_original;
                } else {
                    tmp_path.push_back(portion_original);
                }
            }
            portion_original.clear();
        }
    }
    if (cs.size()==0  && position == 1) {
        tmp_path.push_back(original);
    }
    path_on = tmp_path;
}

Geom::PathVector
LPECopyRotate::doEffect_path (Geom::PathVector const & path_in)
{
    Geom::PathVector path_out;
    double diagonal = Geom::distance(Geom::Point(boundingbox_X.min(),boundingbox_Y.min()),Geom::Point(boundingbox_X.max(),boundingbox_Y.max()));
    Geom::OptRect bbox = sp_lpe_item->geometricBounds();
    size_divider = Geom::distance(origin,bbox) + (diagonal * 6);
    Geom::Point line_start  = origin + dir * Geom::Rotate(-(Geom::rad_from_deg(starting_angle))) * size_divider;
    Geom::Point line_end = origin + dir * Geom::Rotate(-(Geom::rad_from_deg(rotation_angle + starting_angle))) * size_divider;
    divider = Geom::Path(line_start);
    divider.appendNew<Geom::LineSegment>((Geom::Point)origin);
    divider.appendNew<Geom::LineSegment>(line_end);
    divider.close();
    half_dir = unit_vector(Geom::middle_point(line_start,line_end) - (Geom::Point)origin);
    FillRuleBool fillrule = fill_nonZero;
    if (current_shape->style && 
        current_shape->style->fill_rule.set &&
        current_shape->style->fill_rule.computed == SP_WIND_RULE_EVENODD) 
    {
        fillrule = (FillRuleBool)fill_oddEven;
    }
    if (method != RM_NORMAL) {
        if (method != RM_KALEIDOSCOPE) {
            path_out = doEffect_path_post(path_in, fillrule);
        } else {
            path_out = pathv_to_linear_and_cubic_beziers(path_in);
        }
        if (num_copies == 0) {
            return path_out;
        }
        Geom::PathVector triangle;
        triangle.push_back(divider);
        path_out = sp_pathvector_boolop(path_out, triangle, bool_op_inters, fillrule, fillrule, legacytest_livarotonly);
        if ( !split_items ) {
            path_out = doEffect_path_post(path_out, fillrule);
        } else {
            path_out *= Geom::Translate(half_dir * gap);
        }
    } else {
        path_out = doEffect_path_post(path_in, fillrule);
    }
    if (!split_items && method != RM_NORMAL) {
        Geom::PathVector path_out_tmp;
        for (const auto & path_it : path_out) {
            if (path_it.empty()) {
                continue;
            }
            Geom::Path::const_iterator curve_it1 = path_it.begin();
            Geom::Path::const_iterator curve_endit = path_it.end_default();
            Geom::Path res;
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
                if (!Geom::are_near(curve_it1->initialPoint(), curve_it1->pointAt(0.5), 0.05)) {
                    if (!res.empty()) {
                        res.setFinal(curve_it1->initialPoint());
                    }
                    Geom::Curve *c = curve_it1->duplicate();
                    res.append(c);
                }
                ++curve_it1;
            }
            if (path_it.closed()) {
                res.close();
            }
            path_out_tmp.push_back(res);
        }
        path_out = path_out_tmp;
    }
    return pathv_to_linear_and_cubic_beziers(path_out);
}

Geom::PathVector
LPECopyRotate::doEffect_path_post (Geom::PathVector const & path_in, FillRuleBool fillrule)
{
    if ((split_items || num_copies == 1) && method == RM_NORMAL) {
        if (split_items) {
            Geom::PathVector path_out = pathv_to_linear_and_cubic_beziers(path_in);
            Geom::Affine m = Geom::Translate(-origin) * Geom::Rotate(-(Geom::rad_from_deg(starting_angle)));
            Geom::Affine t = m * Geom::Rotate(-Geom::rad_from_deg(starting_angle)) *
                             Geom::Rotate(Geom::rad_from_deg(starting_angle)) * Geom::Translate(origin);
            return path_out * t;
        }
        return path_in;
    }

    Geom::Affine pre = Geom::Translate(-origin) * Geom::Rotate(-Geom::rad_from_deg(starting_angle));
    Geom::PathVector original_pathv = pathv_to_linear_and_cubic_beziers(path_in);
    Geom::PathVector output_pv;
    Geom::PathVector output;
    for (int i = 0; i < num_copies; ++i) {
        Geom::Rotate rot(-Geom::rad_from_deg(rotation_angle * i));
        Geom::Affine r = Geom::identity();
        if( i%2 != 0 && mirror_copies) {
            r *= Geom::Rotate(Geom::Angle(half_dir)).inverse();
            r *= Geom::Scale(1, -1);
            r *= Geom::Rotate(Geom::Angle(half_dir));
        }
        Geom::Affine t = pre * r * rot * Geom::Rotate(Geom::rad_from_deg(starting_angle)) * Geom::Translate(origin);
        if(mirror_copies && i%2 != 0) {
            t = pre * r * rot * Geom::Rotate(Geom::rad_from_deg(starting_angle)).inverse() * Geom::Translate(origin);
        }
        if (method != RM_NORMAL) {
            //we use safest way to union
            Geom::PathVector join_pv = original_pathv * t;
            join_pv *= Geom::Translate(half_dir * rot * gap);
            if (!output_pv.empty()) {
                output_pv = sp_pathvector_boolop(output_pv, join_pv, bool_op_union, fillrule, fillrule, legacytest_livarotonly);
            } else {
                output_pv = join_pv;
            }
        } else {
            t = pre * Geom::Rotate(-Geom::rad_from_deg(starting_angle)) * r * rot * Geom::Rotate(Geom::rad_from_deg(starting_angle)) * Geom::Translate(origin);
            if(mirror_copies && i%2 != 0) {
                t = pre * Geom::Rotate(Geom::rad_from_deg(-starting_angle-rotation_angle)) * r * rot * Geom::Rotate(-Geom::rad_from_deg(starting_angle)) * Geom::Translate(origin);
            }
            output_pv = path_in * t;
            output.insert(output.end(), output_pv.begin(), output_pv.end());
        }
    }
    if (method != RM_NORMAL) {
        output = output_pv;
    }
    return output;
}

void
LPECopyRotate::addCanvasIndicators(SPLPEItem const */*lpeitem*/, std::vector<Geom::PathVector> &hp_vec)
{
    using namespace Geom;
    hp_vec.clear();
    Geom::Path hp;
    hp.start(start_pos);
    hp.appendNew<Geom::LineSegment>((Geom::Point)origin);
    hp.appendNew<Geom::LineSegment>(origin + dir * Rotate(-rad_from_deg(rotation_angle+starting_angle)) * Geom::distance(origin,starting_point));
    Geom::PathVector pathv;
    pathv.push_back(hp);
    hp_vec.push_back(pathv);
}

void
LPECopyRotate::resetDefaults(SPItem const* item)
{
    Effect::resetDefaults(item);
    original_bbox(cast<SPLPEItem>(item), false, true);
}

void
LPECopyRotate::doOnVisibilityToggled(SPLPEItem const* /*lpeitem*/)
{
    processObjects(LPE_VISIBILITY);
}

void 
LPECopyRotate::doOnRemove (SPLPEItem const* lpeitem)
{
    if (keep_paths) {
        processObjects(LPE_TO_OBJECTS);
        return;
    }
    processObjects(LPE_ERASE);
}

} //namespace LivePathEffect
} /* namespace Inkscape */

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
