// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * LPE <mirror_symmetry> implementation: mirrors a path with respect to a given line.
 */
/*
 * Authors:
 *   Maximilian Albert
 *   Johan Engelen
 *   Abhishek Sharma
 *   Jabiertxof
 *
 * Copyright (C) Johan Engelen 2007 <j.b.c.engelen@utwente.nl>
 * Copyright (C) Maximilin Albert 2008 <maximilian.albert@gmail.com>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "live_effects/lpe-mirror_symmetry.h"

#include <glibmm/i18n.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>

#include "path-chemistry.h"
#include "style.h"

#include "2geom/affine.h"
#include "2geom/path-intersection.h"

#include "display/curve.h"
#include "helper/geom.h"
#include "live_effects/parameter/satellite-reference.h"
#include "object/sp-defs.h"
#include "object/sp-item-group.h"
#include "object/sp-lpe-item.h"
#include "object/sp-path.h"
#include "object/sp-text.h"
#include "path/path-boolop.h"
#include "svg/path-string.h"
#include "svg/svg.h"
#include "ui/pack.h"
#include "xml/sp-css-attr.h"


namespace Inkscape {
namespace LivePathEffect {

static const Util::EnumData<ModeType> ModeTypeData[] = {
    { MT_V, N_("Vertical page center"), "vertical" },
    { MT_H, N_("Horizontal page center"), "horizontal" },
    { MT_FREE, N_("Freely defined mirror line"), "free" },
    { MT_X, N_("X coordinate of mirror line midpoint"), "X" },
    { MT_Y, N_("Y coordinate of mirror line midpoint"), "Y" }
};
static const Util::EnumDataConverter<ModeType>
MTConverter(ModeTypeData, MT_END);


LPEMirrorSymmetry::LPEMirrorSymmetry(LivePathEffectObject *lpeobject) :
    Effect(lpeobject),
    // do not change name of this parameter us used in oncommit
    lpesatellites(_("lpesatellites"), _("Items satellites"), "lpesatellites", &wr, this, false),
    mode(_("Mode"), _("Set mode of transformation. Either freely defined by mirror line or constrained to certain symmetry points."), "mode", MTConverter, &wr, this, MT_FREE),
    discard_orig_path(_("Discard original path"), _("Only keep mirrored part of the path, remove the original."), "discard_orig_path", &wr, this, false),
    fuse_paths(_("Fuse paths"), _("Fuse original path and mirror image into a single path"), "fuse_paths", &wr, this, false),
    oposite_fuse(_("Fuse opposite sides"), _("Picks the part on the other side of the mirror line as the original."), "oposite_fuse", &wr, this, false),
    split_items(_("Split elements"), _("Split original and mirror image into separate paths, so each can have its own style."), "split_items", &wr, this, false),
    split_open(_("Keep open paths on split"), _("Do not automatically close paths along the split line."), "split_open", &wr, this, false),
    start_point(_("Mirror line start"), _("Start point of mirror line"), "start_point", &wr, this, _("Adjust start point of mirror line")),
    end_point(_("Mirror line end"), _("End point of mirror line"), "end_point", &wr, this, _("Adjust end point of mirror line")),
    center_point(_("Mirror line mid"), _("Center point of mirror line"), "center_point", &wr, this, _("Adjust center point of mirror line")),
    link_styles(_("Link styles"), _("Link styles on split mode"), "link_styles", &wr, this, false)
{
    registerParameter(&lpesatellites);
    registerParameter(&mode);
    registerParameter(&discard_orig_path);
    registerParameter(&fuse_paths);
    registerParameter(&oposite_fuse);
    registerParameter(&split_items);
    registerParameter(&split_open);
    registerParameter(&link_styles);
    registerParameter(&start_point);
    registerParameter(&end_point);
    registerParameter(&center_point);
    show_orig_path = true;
    apply_to_clippath_and_mask = true;
    previous_center = Geom::Point(0,0);
    center_point.param_widget_is_visible(false);
    reset = link_styles;
    center_horiz = false;
    center_vert = false;
    satellitestoclipboard = true;
}

LPEMirrorSymmetry::~LPEMirrorSymmetry() = default;

bool LPEMirrorSymmetry::doOnOpen(SPLPEItem const *lpeitem)
{
    bool fixed = false;
    if (!is_load || is_applied || !split_items) {
        return fixed;
    }

    Glib::ustring version = lpeversion.param_getSVGValue();
    if (version < "1.2") {
        lpesatellites.clear();
        Glib::ustring id = Glib::ustring("mirror-");
        id += getLPEObj()->getId();
        SPObject *elemref = getSPDoc()->getObjectById(id.c_str());
        if (elemref) {
            lpesatellites.link(elemref, 0);
        }
        lpeversion.param_setValue("1.2", true);
        fixed = true;
        lpesatellites.write_to_SVG();
    }
    lpesatellites.start_listening();
    lpesatellites.connect_selection_changed();
    container = lpeitem->parent;
    return fixed;
}

void
LPEMirrorSymmetry::doAfterEffect (SPLPEItem const* lpeitem, SPCurve *curve)
{
    SPDocument *document = getSPDoc();
    if (!document) {
        return;
    }
    container = sp_lpe_item->parent;
    
    if (split_items && !discard_orig_path) {
        bool active = !lpesatellites.data().size() || is_load;
        for (auto lpereference : lpesatellites.data()) {
            if (lpereference && lpereference->isAttached() && lpereference.get()->getObject() != nullptr) {
                active = true;
            }
        }
        // we need to call this when the LPE is "mirrored 1 or + times in split mode"
        // to prevent satellite hidden as in prev status
        if (!active && !is_load && prev_split && !prev_discard_orig_path) {
            lpesatellites.clear();
            return;
        }
        Geom::Line ls((Geom::Point)start_point, (Geom::Point)end_point);
        Geom::Affine m = Geom::reflection (ls.vector(), (Geom::Point)start_point);
        m *= sp_lpe_item->transform;
        toMirror(m);
    }
    prev_split = split_items;
    prev_discard_orig_path = discard_orig_path;
}

Gtk::Widget *
LPEMirrorSymmetry::newWidget()
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

            if (widg && param->param_key != "split_open") {
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
    auto const hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    auto const center_vert_button = Gtk::make_managed<Gtk::Button>(Glib::ustring(_("Vertical center")));
    center_vert_button->signal_clicked().connect(sigc::mem_fun (*this,&LPEMirrorSymmetry::centerVert));
    center_vert_button->set_size_request(110,20);
    auto const center_horiz_button = Gtk::make_managed<Gtk::Button>(Glib::ustring(_("Horizontal center")));
    center_horiz_button->signal_clicked().connect(sigc::mem_fun (*this,&LPEMirrorSymmetry::centerHoriz));
    center_horiz_button->set_size_request(110,20);
    UI::pack_start(*vbox, *hbox, true,true,2);
    UI::pack_start(*hbox, *center_vert_button, false, false,2);
    UI::pack_start(*hbox, *center_horiz_button, false, false,2);
    return vbox;
}

void
LPEMirrorSymmetry::centerVert(){
    center_vert = true;
    sp_lpe_item_update_patheffect(sp_lpe_item, false, false, true);
    makeUndoDone(_("Center Vertical"));
}

void
LPEMirrorSymmetry::centerHoriz(){
    center_horiz = true;
    sp_lpe_item_update_patheffect(sp_lpe_item, false, false, true);
    makeUndoDone(_("Center Horizontal"));
}

void
LPEMirrorSymmetry::doBeforeEffect (SPLPEItem const* lpeitem)
{
    using namespace Geom;
    if ((!split_items || discard_orig_path) && lpesatellites.data().size()) {
        processObjects(LPE_ERASE);
    }
    if (link_styles) {
        reset = true;
    }
    if (!lpesatellites.data().size()) {
        lpesatellites.read_from_SVG();
        if (lpesatellites.data().size()) {
            lpesatellites.update_satellites();
        }
    }
    original_bbox(lpeitem, false, true);
    Point point_a(boundingbox_X.max(), boundingbox_Y.min());
    Point point_b(boundingbox_X.max(), boundingbox_Y.max());
    Point point_c(boundingbox_X.middle(), boundingbox_Y.middle());
    if (center_vert) {
        center_point.param_setValue(point_c, true);
        end_point.param_setValue(Geom::Point(boundingbox_X.middle(), boundingbox_Y.min()),true);
        //force update
        start_point.param_setValue(Geom::Point(boundingbox_X.middle(), boundingbox_Y.max()),true);
        center_vert = false;
    } else if (center_horiz) {
        center_point.param_setValue(point_c, true);
        end_point.param_setValue(Geom::Point(boundingbox_X.max(), boundingbox_Y.middle()),true);
        start_point.param_setValue(Geom::Point(boundingbox_X.min(), boundingbox_Y.middle()),true);
        //force update
        center_horiz = false;
    } else {

        if (mode == MT_Y) {
            point_a = Geom::Point(boundingbox_X.min(),center_point[Y]);
            point_b = Geom::Point(boundingbox_X.max(),center_point[Y]);
            center_point.param_setValue(Geom::middle_point((Geom::Point)point_a, (Geom::Point)point_b), true);
        }
        if (mode == MT_X) {
            point_a = Geom::Point(center_point[X],boundingbox_Y.min());
            point_b = Geom::Point(center_point[X],boundingbox_Y.max());
            center_point.param_setValue(Geom::middle_point((Geom::Point)point_a, (Geom::Point)point_b), true);
        }
        if ((Geom::Point)start_point == (Geom::Point)end_point) {
            start_point.param_setValue(point_a, true);
            end_point.param_setValue(point_b, true);
            previous_center = Geom::middle_point((Geom::Point)start_point, (Geom::Point)end_point);
            center_point.param_setValue(previous_center, true);
            return;
        }
        if ( mode == MT_X || mode == MT_Y ) {
            if (!are_near(previous_center, (Geom::Point)center_point, 0.01)) {
                center_point.param_setValue(Geom::middle_point(point_a, point_b), true);
                end_point.param_setValue(point_b, true);
                start_point.param_setValue(point_a, true);
            } else {
                if ( mode == MT_X ) {
                    if (!are_near(start_point[X], point_a[X], 0.01)) {
                        start_point.param_setValue(point_a, true);
                    }
                    if (!are_near(end_point[X], point_b[X], 0.01)) {
                        end_point.param_setValue(point_b, true);
                    }
                } else {  //MT_Y
                    if (!are_near(start_point[Y], point_a[Y], 0.01)) {
                        start_point.param_setValue(point_a, true);
                    }
                    if (!are_near(end_point[Y], point_b[Y], 0.01)) {
                        end_point.param_setValue(point_b, true);
                    }
                }
            }
        } else if ( mode == MT_FREE) {
            if ((Geom::Point)start_point == (Geom::Point)end_point) {
                start_point.param_setValue(point_a, true);
                end_point.param_setValue(point_b, true);
                previous_center = Geom::middle_point((Geom::Point)start_point, (Geom::Point)end_point);
                center_point.param_setValue(previous_center, true);
                return;
            }
            if (!are_near(previous_center, (Geom::Point)center_point, 0.001)) {
                Geom::Point trans = center_point - Geom::middle_point((Geom::Point)start_point, (Geom::Point)end_point);
                start_point.param_setValue(start_point + trans, true);
                end_point.param_setValue(end_point + trans, true);
            }
            center_point.param_setValue(Geom::middle_point((Geom::Point)start_point, (Geom::Point)end_point), true);
            previous_center = Geom::middle_point((Geom::Point)start_point, (Geom::Point)end_point);
        } else if ( mode == MT_V){
            SPDocument *document = getSPDoc();
            if (document) {
                Geom::Affine transform = i2anc_affine(lpeitem, nullptr).inverse();
                Geom::Point sp = Geom::Point(document->getWidth().value("px")/2.0, 0) * transform;
                start_point.param_setValue(sp, true);
                Geom::Point ep = Geom::Point(document->getWidth().value("px")/2.0, document->getHeight().value("px")) * transform;
                end_point.param_setValue(ep, true);
                center_point.param_setValue(Geom::middle_point((Geom::Point)start_point, (Geom::Point)end_point), true);
            }
        } else { //horizontal page
            SPDocument *document = getSPDoc();
            if (document) {
                Geom::Affine transform = i2anc_affine(lpeitem, nullptr).inverse();
                Geom::Point sp = Geom::Point(0, document->getHeight().value("px")/2.0) * transform;
                start_point.param_setValue(sp, true);
                Geom::Point ep = Geom::Point(document->getWidth().value("px"), document->getHeight().value("px")/2.0) * transform;
                end_point.param_setValue(ep, true);
                center_point.param_setValue(Geom::middle_point((Geom::Point)start_point, (Geom::Point)end_point), true);
            }
        }
    }
    previous_center = center_point;
}

void LPEMirrorSymmetry::cloneStyle(SPObject *orig, SPObject *dest)
{
    dest->setAttribute("transform", orig->getAttribute("transform"));
    dest->setAttribute("mask", orig->getAttribute("mask"));
    dest->setAttribute("clip-path", orig->getAttribute("clip-path"));
    dest->setAttribute("class", orig->getAttribute("class"));
    dest->setAttribute("style", orig->getAttribute("style"));
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

void LPEMirrorSymmetry::cloneD(SPObject *orig, SPObject *dest)
{
    SPDocument *document = getSPDoc();
    if (!document) {
        return;
    }
    if ( is<SPGroup>(orig) && is<SPGroup>(dest) && cast<SPGroup>(orig)->getItemCount() == cast<SPGroup>(dest)->getItemCount() ) {
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

    if (is<SPText>(orig) && is<SPText>(dest) && cast<SPText>(orig)->children.size() == cast<SPText>(dest)->children.size()) {
        if (reset) {
            cloneStyle(orig, dest);
        }
        size_t index = 0;
        for (auto &child : cast<SPText>(orig)->children) {
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
                path = cast<SPPath>(dest);
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
LPEMirrorSymmetry::createPathBase(SPObject *elemref) {
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
    resultnode->setAttribute("mask", prev->attribute("mask"));
    resultnode->setAttribute("clip-path", prev->attribute("clip-path"));
    resultnode->setAttribute("class", prev->attribute("class"));
    return resultnode;
}

void
LPEMirrorSymmetry::toMirror(Geom::Affine transform)
{
    SPDocument *document = getSPDoc();
    if (!document) {
        return;
    }
    //Inkscape::XML::Document *xml_doc = document->getReprDoc();
    SPObject *elemref = nullptr;
    if (!is_load && container != sp_lpe_item->parent) {
        lpesatellites.read_from_SVG();
        return;
    }
    if (lpesatellites.data().size() && lpesatellites.data()[0]) {
        elemref = lpesatellites.data()[0]->getObject();
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
    reset = link_styles;
    elemref->getRepr()->setAttributeOrRemoveIfEmpty("transform", sp_svg_transform_write(transform));
    // Alow work in clones
    /* if (elemref->parent != container) {
        if (!creation) {
            lpesatellites.unlink(elemref);
        }
        Inkscape::XML::Node *copy = phantom->duplicate(xml_doc);
        copy->setAttribute("id", elemref->getId());
        lpesatellites.link(container->appendChildRepr(copy), 0);
        Inkscape::GC::release(copy);
        elemref->deleteObject();
        lpesatellites.write_to_SVG();
        lpesatellites.update_satellites();
    } else  */
    if (creation) {
        lpesatellites.clear();
        lpesatellites.link(elemref, 0);
        lpesatellites.write_to_SVG();
        if (lpesatellites.is_connected()) {
            lpesatellites.update_satellites();
        }
    }
    if (!lpesatellites.is_connected()) {
        if (!creation) {
            lpesatellites.write_to_SVG();
        }
        lpesatellites.start_listening();
        sp_lpe_item_update_patheffect(sp_lpe_item, false, false, true);
    }
}


//TODO: Migrate the tree next function to effect.cpp/h to avoid duplication
void
LPEMirrorSymmetry::doOnVisibilityToggled(SPLPEItem const* /*lpeitem*/)
{
    processObjects(LPE_VISIBILITY);
}

void
LPEMirrorSymmetry::doOnRemove (SPLPEItem const* /*lpeitem*/)
{
    if (keep_paths) {
        processObjects(LPE_TO_OBJECTS);
        return;
    }
    processObjects(LPE_ERASE);
}

void
LPEMirrorSymmetry::doOnApply (SPLPEItem const* lpeitem)
{
    using namespace Geom;

    original_bbox(lpeitem, false, true);

    Point point_a(boundingbox_X.max(), boundingbox_Y.min());
    Point point_b(boundingbox_X.max(), boundingbox_Y.max());
    Point point_c(boundingbox_X.max(), boundingbox_Y.middle());
    start_point.param_setValue(point_a, true);
    start_point.param_update_default(point_a);
    end_point.param_setValue(point_b, true);
    end_point.param_update_default(point_b);
    center_point.param_setValue(point_c, true);
    previous_center = center_point;
    //we bump to 1.1 because previous 1.0.2 take no effect because a bug on 1.0.2
    lpeversion.param_setValue("1.2", true);
    lpesatellites.update_satellites();
}

Geom::PathVector
LPEMirrorSymmetry::doEffect_path (Geom::PathVector const & path_in)
{
    if (split_items && !fuse_paths) {
        return path_in;
    }
    Geom::PathVector const original_pathv = pathv_to_linear_and_cubic_beziers(path_in);
    Geom::PathVector path_out;

    if (!discard_orig_path && !fuse_paths) {
        path_out = pathv_to_linear_and_cubic_beziers(path_in);
    }

    Geom::Line line_separation((Geom::Point)start_point, (Geom::Point)end_point);
    Geom::Affine m = Geom::reflection (line_separation.vector(), (Geom::Point)start_point);
    if (fuse_paths && !discard_orig_path) {
        for (const auto & path_it : original_pathv)
        {
            if (path_it.empty()) {
                continue;
            }
            Geom::PathVector tmp_pathvector;
            double time_start = 0.0;
            int position = 0;
            bool end_open = false;
            if (path_it.closed()) {
                const Geom::Curve &closingline = path_it.back_closed();
                if (!are_near(closingline.initialPoint(), closingline.finalPoint())) {
                    end_open = true;
                }
            }
            Geom::Path original = path_it;
            if (end_open && path_it.closed()) {
                original.close(false);
                original.appendNew<Geom::LineSegment>( original.initialPoint() );
                original.close(true);
            }
            Geom::Point s = start_point;
            Geom::Point e = end_point;
            double dir = line_separation.angle();
            double diagonal = Geom::distance(Geom::Point(boundingbox_X.min(),boundingbox_Y.min()),Geom::Point(boundingbox_X.max(),boundingbox_Y.max()));
            Geom::Rect bbox(Geom::Point(boundingbox_X.min(),boundingbox_Y.min()),Geom::Point(boundingbox_X.max(),boundingbox_Y.max()));
            double size_divider = Geom::distance(center_point, bbox) + diagonal;
            s = Geom::Point::polar(dir,size_divider) + center_point;
            e = Geom::Point::polar(dir + Geom::rad_from_deg(180),size_divider) + center_point;
            Geom::Path divider = Geom::Path(s);
            divider.appendNew<Geom::LineSegment>(e);
            Geom::Crossings cs = crossings(original, divider);
            std::vector<double> crossed;
            for(auto & c : cs) {
                crossed.push_back(c.ta);
            }
            std::sort(crossed.begin(), crossed.end());
            for (unsigned int i = 0; i < crossed.size(); i++) {
                double time_end = crossed[i];
                if (time_start != time_end && time_end - time_start > Geom::EPSILON) {
                    Geom::Path portion = original.portion(time_start, time_end);
                    if (!portion.empty()) {
                        Geom::Point middle = portion.pointAt((double)portion.size()/2.0);
                        position = Geom::sgn(Geom::cross(e - s, middle - s));
                        if (!oposite_fuse) {
                            position *= -1;
                        }
                        if (position == 1) {
                            if (!split_items) {
                                Geom::Path mirror = portion.reversed() * m;
                                mirror.setInitial(portion.finalPoint());
                                portion.append(mirror);
                                if(i != 0) {
                                    portion.setFinal(portion.initialPoint());
                                    portion.close();
                                }
                            }
                            tmp_pathvector.push_back(portion);
                        }
                        portion.clear();
                    }
                }
                time_start = time_end;
            }
            position = Geom::sgn(Geom::cross(e - s, original.finalPoint() - s));
            if (!oposite_fuse) {
                position *= -1;
            }
            if (cs.size()!=0 && (position == 1)) {
                if (time_start != original.size() && original.size() - time_start > Geom::EPSILON) {
                    Geom::Path portion = original.portion(time_start, original.size());
                    if (!portion.empty()) {
                        portion = portion.reversed();
                        if (!split_items) {
                            Geom::Path mirror = portion.reversed() * m;
                            mirror.setInitial(portion.finalPoint());
                            portion.append(mirror);
                        }
                        portion = portion.reversed();
                        if (!original.closed()) {
                            tmp_pathvector.push_back(portion);
                        } else {
                            if (cs.size() > 1 && tmp_pathvector.size() > 0 && tmp_pathvector[0].size() > 0 ) {
                                if (!split_items) {
                                    portion.setFinal(tmp_pathvector[0].initialPoint());
                                    portion.setInitial(tmp_pathvector[0].finalPoint());
                                } else {
                                    tmp_pathvector[0] = tmp_pathvector[0].reversed();
                                    portion = portion.reversed();
                                    portion.setInitial(tmp_pathvector[0].finalPoint());
                                }
                                tmp_pathvector[0].append(portion);
                            } else {
                                tmp_pathvector.push_back(portion);
                            }
                            if (lpeversion.param_getSVGValue() < "1.1") {
                                tmp_pathvector[0].close();
                            }
                        }
                        portion.clear();
                    }
                }
            }
            if (!split_open && lpeversion.param_getSVGValue() >= "1.1" && original.closed()) {
                for (auto &path : tmp_pathvector) {
                    if (!path.closed()) {
                        path.close();
                    }
                }
                sp_flatten(tmp_pathvector, fill_oddEven);
            }
            if (cs.size() == 0 && position == 1) {
                tmp_pathvector.push_back(original);
                if (!split_items) {
                    tmp_pathvector.push_back(original * m);
                }
            }
            path_out.insert(path_out.end(), tmp_pathvector.begin(), tmp_pathvector.end());
            tmp_pathvector.clear();
        }
    } else if (!fuse_paths || discard_orig_path) {
        for (const auto & i : original_pathv) {
            path_out.push_back(i * m);
        }
    }
    return path_out;
}

void
LPEMirrorSymmetry::addCanvasIndicators(SPLPEItem const */*lpeitem*/, std::vector<Geom::PathVector> &hp_vec)
{
    using namespace Geom;
    hp_vec.clear();
    Geom::Path path;
    Geom::Point s = start_point;
    Geom::Point e = end_point;
    path.start( s );
    path.appendNew<Geom::LineSegment>( e );
    Geom::PathVector helper;
    helper.push_back(path);
    hp_vec.push_back(helper);
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
