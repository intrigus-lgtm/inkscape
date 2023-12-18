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

#include "live_effects/lpe-tiling.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <2geom/intersection-graph.h>
#include <2geom/path-intersection.h>
#include <2geom/sbasis-to-bezier.h>
#include <gdk/gdk.h>
#include <glibmm/i18n.h>
#include <glibmm/ustring.h>
#include <gtkmm/box.h>
#include <gtkmm/combobox.h>
#include <gtkmm/frame.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/object.h>
#include <gtkmm/spinbutton.h>

#include "inkscape.h"
#include "display/curve.h"
#include "helper/geom.h"
#include "live_effects/lpeobject.h"
#include "live_effects/parameter/satellite-reference.h"
#include "object/sp-item-group.h"
#include "object/sp-object.h"
#include "object/sp-path.h"
#include "object/sp-shape.h"
#include "object/sp-text.h"
#include "path-chemistry.h"
#include "path/path-boolop.h"
#include "style.h"
#include "svg/path-string.h"
#include "svg/svg.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/knot/knot-holder-entity.h"
#include "ui/knot/knot-holder.h"
#include "ui/pack.h"
#include "ui/util.h"
#include "util-string/ustring-format.h"
#include "xml/sp-css-attr.h"

namespace Inkscape::LivePathEffect {

namespace CoS {

class KnotHolderEntityCopyGapX final : public LPEKnotHolderEntity {
public:
    KnotHolderEntityCopyGapX(LPETiling * effect) : LPEKnotHolderEntity(effect) {};
    ~KnotHolderEntityCopyGapX() final;

    void knot_set(Geom::Point const &p, Geom::Point const &origin, guint state) final;
    void knot_click(guint state) final;
    Geom::Point knot_get() const final;

    double startpos = dynamic_cast<LPETiling const*> (_effect)->gapx_unit;
};

class KnotHolderEntityCopyGapY final : public LPEKnotHolderEntity {
public:
    KnotHolderEntityCopyGapY(LPETiling * effect) : LPEKnotHolderEntity(effect) {};
    ~KnotHolderEntityCopyGapY() final;

    void knot_set(Geom::Point const &p, Geom::Point const &origin, guint state) final;
    void knot_click(guint state) final;
    Geom::Point knot_get() const final;

    double startpos = dynamic_cast<LPETiling const*> (_effect)->gapy_unit;
};

} // namespace CoS

LPETiling::LPETiling(LivePathEffectObject *lpeobject) :
    Effect(lpeobject),
    // do not change name of this parameter is used in oncommit
    unit(_("Unit:"), _("Unit"), "unit", &wr, this, "px"),
    lpesatellites(_("lpesatellites"), _("Items satellites"), "lpesatellites", &wr, this, false),
    num_cols(_("Columns"), _("Number of columns"), "num_cols", &wr, this, 3),
    num_rows(_("Rows"), _("Number of rows"), "num_rows", &wr, this, 3),
    gapx(_("Gap X"), _("Horizontal gap between tiles (uses selected unit)"), "gapx", &wr, this, 0.0),
    gapy(_("Gap Y"), _("Vertical gap between tiles (uses selected unit)"), "gapy", &wr, this, 0.0),
    scale(_("Scale %"), _("Scale tiles by this percentage"), "scale", &wr, this, 0.0),
    rotate(_("Rotate °"), _("Rotate tiles by this amount of degrees"), "rotate", &wr, this, 0.0),
    offset(_("Offset %"), _("Offset tiles by this percentage of width/height"), "offset", &wr, this, 0.0),
    offset_type(_("Offset type"), _("Choose whether to offset rows or columns"), "offset_type", &wr, this, false),
    interpolate_scalex(_("Interpolate scale X"), _("Interpolate tile size in each row"), "interpolate_scalex", &wr, this, false),
    interpolate_scaley(_("Interpolate scale Y"), _("Interpolate tile size in each column"), "interpolate_scaley", &wr, this, true),
    shrink_interp(_("Minimize gaps"), _("Minimize gaps between scaled objects (does not work with rotation/diagonal mode)"), "shrink_interp", &wr, this, false),
    interpolate_rotatex(_("Interpolate rotation X"), _("Interpolate tile rotation in row"), "interpolate_rotatex", &wr, this, false),
    interpolate_rotatey(_("Interpolate rotation Y"), _("Interpolate tile rotation in column"), "interpolate_rotatey", &wr, this, true),
    split_items(_("Split elements"), _("Split elements, so they can be selected, styled, and moved (if grouped) independently"), "split_items", &wr, this, false),
    mirrorrowsx(_("Mirror rows in X"), _("Mirror rows horizontally"), "mirrorrowsx", &wr, this, false),
    mirrorrowsy(_("Mirror rows in Y"), _("Mirror rows vertically"), "mirrorrowsy", &wr, this, false),
    mirrorcolsx(_("Mirror cols in X"), _("Mirror columns horizontally"), "mirrorcolsx", &wr, this, false),
    mirrorcolsy(_("Mirror cols in Y"), _("Mirror columns vertically"), "mirrorcolsy", &wr, this, false),
    mirrortrans(_("Mirror transforms"), _("Mirror transformations"), "mirrortrans", &wr, this, false),
    link_styles(_("Link styles"), _("Link styles in split mode, can also be used to reset style of copies"), "link_styles", &wr, this, false),
    random_gap_x(_("Random gaps X"), _("Randomize horizontal gaps"), "random_gap_x", &wr, this, false),
    random_gap_y(_("Random gaps Y"), _("Randomize vertical gaps"), "random_gap_y", &wr, this, false),
    random_rotate(_("Random rotation"), _("Randomize tile rotation"), "random_rotate", &wr, this, false),
    random_scale(_("Random scale"), _("Randomize scale"), "random_scale", &wr, this, false),
    seed(_("Seed"), _("Randomization seed"), "seed", &wr, this, 1.),
    transformorigin("transformorigin:", "transformorigin","transformorigin", &wr, this, "", true)    
{
    show_orig_path = true;
    _provides_knotholder_entities = true;

    // register all your parameters here, so Inkscape knows which parameters this effect has:
    // please intense work on this widget and is important reorder parameters very carefully
    registerParameter(&unit);
    registerParameter(&seed);
    registerParameter(&lpesatellites);
    registerParameter(&num_rows);
    registerParameter(&num_cols);
    registerParameter(&gapx);
    registerParameter(&gapy);
    registerParameter(&offset);
    registerParameter(&offset_type);
    registerParameter(&scale);
    registerParameter(&rotate);
    registerParameter(&mirrorrowsx);
    registerParameter(&mirrorrowsy);
    registerParameter(&mirrorcolsx);
    registerParameter(&mirrorcolsy);
    registerParameter(&mirrortrans);
    registerParameter(&shrink_interp);
    registerParameter(&split_items);
    registerParameter(&link_styles);
    registerParameter(&interpolate_scalex);
    registerParameter(&interpolate_scaley);
    registerParameter(&interpolate_rotatex);
    registerParameter(&interpolate_rotatey);
    registerParameter(&random_scale);
    registerParameter(&random_rotate);
    registerParameter(&random_gap_y);
    registerParameter(&random_gap_x);
    registerParameter(&transformorigin);
    
    num_cols.param_set_range(1, 9999);// we need the input a bit tiny so this seems enough
    num_cols.param_make_integer();
    num_cols.param_set_increments(1, 10);
    num_rows.param_set_range(1, 9999);
    num_rows.param_make_integer();
    num_rows.param_set_increments(1, 10);
    scale.param_set_range(-9999.99,9999.99); 
    scale.param_set_increments(1, 10);
    gapx.param_set_range(-99999,99999); 
    gapx.param_set_increments(1.0, 10.0);
    gapy.param_set_range(-99999,99999); 
    gapy.param_set_increments(1.0, 10.0);
    rotate.param_set_increments(1.0, 10.0);
    rotate.param_set_range(-900, 900);
    offset.param_set_range(-300, 300);
    offset.param_set_increments(1.0, 10.0);
    seed.param_set_range(1.0, 1.0);
    seed.param_set_randomsign(true);
    apply_to_clippath_and_mask = true;
    _provides_knotholder_entities = true;
    prev_num_cols = num_cols;
    prev_num_rows = num_rows;
    _knotholder = nullptr;
    reset = link_styles;
    display_unit = getSPDoc()->getWidth().unit->abbr;
}

LPETiling::~LPETiling()
{
    if (_knotholder) {
        _knotholder->clear();
        _knotholder = nullptr;
    }
}

bool LPETiling::doOnOpen(SPLPEItem const *lpeitem)
{
    bool fixed = false;
    if (!is_load || is_applied) {
        return fixed;
    }
    if (!split_items) {
        return fixed;
    }
    lpesatellites.update_satellites();
    container = lpeitem->parent;
    return fixed;
}

void
LPETiling::doAfterEffect (SPLPEItem const* lpeitem, SPCurve *curve)
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
        if (!active && !is_load && prev_split) {
            lpesatellites.clear();
            prev_num_cols = 0;
            prev_num_rows = 0;
        }
        prev_split = split_items;

        container = sp_lpe_item->parent;
        if (prev_num_cols * prev_num_rows != num_cols * num_rows) {
            write = true;
            size_t pos = 0;
            for (auto lpereference : lpesatellites.data()) {
                if (lpereference && lpereference->isAttached()) {
                    auto copies = cast<SPItem>(lpereference->getObject());
                    if (copies) {
                        if (pos > num_cols * num_rows - 2) {
                            copies->setHidden(true);
                        } else if (copies->isHidden()) {
                            copies->setHidden(false);
                        }
                    }
                }
                pos++;
            }
            prev_num_cols = num_cols;
            prev_num_rows = num_rows;
        }
        if (!gap_bbox) {
            return;
        }
        Geom::Point center = (*gap_bbox).midpoint() * transformoriginal.inverse();
        bool forcewrite = false;
        Geom::Affine origin = Geom::Translate(center).inverse();
        if (!interpolate_rotatex && !interpolate_rotatey && !random_rotate) {
            origin *= Geom::Rotate::from_degrees(rotate);
        }
        if (!interpolate_scalex && !interpolate_scaley && !random_scale) {
            origin *= Geom::Scale(scaleok, scaleok);
        }
        origin *= Geom::Translate(center);
        origin = origin.inverse();
        size_t counter = 0;
        double gapscalex = 0;
        double maxheight = 0;
        double maxwidth = 0;
        double minheight = std::numeric_limits<double>::max();
        double y[(int)num_cols]; 
        double ygap[(int)num_cols]; 
        double yset = 0;
        Geom::OptRect prev_bbox;
        Geom::OptRect bbox = sp_lpe_item->geometricBounds();

        Geom::Affine base_transform = sp_item_transform_repr(sp_lpe_item);
        Geom::Affine gapp = base_transform.inverse() * transformoriginal;
        Geom::Point spcenter_base = (*sp_lpe_item->geometricBounds(transformoriginal)).midpoint();
        Geom::Point spcenter = (*sp_lpe_item->geometricBounds(base_transform)).midpoint();
        Geom::Affine gap = gapp.withoutTranslation();
        if (!bbox) {
            return;
        }
        (*bbox) *= transformoriginal;
        for (int i = 0; i < num_rows; ++i) {
            double fracy = 1;
            if (num_rows != 1) {
                fracy = i/(double)(num_rows - 1);
            }
            for (int j = 0; j < num_cols; ++j) {
                double x = 0;
                double fracx = 1;
                if (num_cols != 1) {
                    fracx = j/(double)(num_cols - 1);
                }
                Geom::Affine r = Geom::identity();
                Geom::Scale mirror = Geom::Scale(1,1);
                if(mirrorrowsx || mirrorrowsy || mirrorcolsx || mirrorcolsy) {
                    gint mx = 1;
                    gint my = 1;
                    if (mirrorrowsx && mirrorcolsx) {
                        mx = (j+i)%2 != 0 ? -1 : 1;
                    } else {
                        if (mirrorrowsx) {
                            mx = i%2 != 0 ? -1 : 1;
                        } else if (mirrorcolsx) {
                            mx = j%2 != 0 ? -1 : 1;
                        }
                    }
                    if (mirrorrowsy && mirrorcolsy) {
                        my = (j+i)%2 != 0 ? -1 : 1;
                    } else {                     
                        if (mirrorrowsy) {
                            my = i%2 != 0 ? -1 : 1; 
                        } else if (mirrorcolsy) {
                            my = j%2 != 0 ? -1 : 1;
                        }
                    }
                    mirror = Geom::Scale(mx, my);
                }
                if (mirrortrans && interpolate_scalex && i%2 != 0) {
                    fracx = 1-fracx;
                }
                double fracyin = fracy;
                if (mirrortrans && interpolate_scaley && j%2 != 0) {
                    fracyin = 1-fracyin;
                }
                double rotatein = rotate;
                if (interpolate_rotatex && interpolate_rotatey) {
                    rotatein = rotatein * (i + j);
                } else if (interpolate_rotatex) {
                    rotatein = rotatein  * j;
                } else if (interpolate_rotatey) {
                    rotatein = rotatein * i;
                }
                if (mirrortrans && 
                    ((interpolate_rotatex && i%2 != 0) ||
                    (interpolate_rotatey && j%2 != 0) ||
                    (interpolate_rotatex && interpolate_rotatey))) 
                {
                    rotatein *=-1;
                }
                double scalein = 1;
                double scalegap = scaleok - scalein;
                if (interpolate_scalex && interpolate_scaley) {
                    scalein = (scalegap * (i + j)) + 1;
                } else if (interpolate_scalex) {
                    scalein = (scalegap * j) + 1;
                } else if (interpolate_scaley) {
                    scalein = (scalegap * i) + 1;
                } else {
                    scalein = scaleok;
                }
                if (!interpolate_rotatex && !interpolate_rotatey && !random_rotate) {
                    r *= Geom::Rotate::from_degrees(rotatein).inverse();
                }
                if (random_scale && scaleok != 1.0) {
                    if (random_s.size() == counter) {
                        double max = std::max(1.0,scaleok);
                        double min = std::min(1.0,scaleok);
                        random_s.emplace_back(seed.param_get_random_number()  * (max - min) + min);
                    }
                    scalein = random_s[counter];
                }
                if (random_rotate && rotate) {
                    if (random_r.size() == counter) {
                        random_r.emplace_back((seed.param_get_random_number() - seed.param_get_random_number()) * rotate);
                    }
                    rotatein = random_r[counter];
                }
                if (random_x.size() == counter) {
                    if (random_gap_x && gapx_unit) {
                        random_x.emplace_back((seed.param_get_random_number() * gapx_unit)); // avoid overlapping
                    } else {
                        random_x.emplace_back(0);
                    }
                }
                if (random_y.size() == counter) {
                    if (random_gap_y && gapy_unit) {
                        random_y.emplace_back((seed.param_get_random_number() * gapy_unit)); // avoid overlapping
                    } else {
                        random_y.emplace_back(0);
                    }
                }
                r *= Geom::Rotate::from_degrees(rotatein);
                r *= Geom::Scale(scalein, scalein);
                double scale_fix = end_scale(scaleok, true);
                double heightrows = original_height * scale_fix;
                double widthcols = original_width * scale_fix;
                double fixed_heightrows = heightrows;
                double fixed_widthcols = widthcols;
                bool shrink_interpove = shrink_interp;
                if (rotatein) {
                    shrink_interpove = false;
                }
                if (scaleok != 1.0 && (interpolate_scalex || interpolate_scaley)) {
                    maxheight = std::max(maxheight,(*bbox).height() * scalein);
                    maxwidth = std::max(maxwidth,(*bbox).width() * scalein);
                    minheight = std::min(minheight,(*bbox).height() * scalein);
                    widthcols = std::max(original_width * end_scale(scaleok, false), original_width);
                    heightrows = std::max(original_height * end_scale(scaleok, false), original_height);
                    fixed_widthcols = widthcols;
                    fixed_heightrows = heightrows;
                    double cx = (*bbox).width() * scalein;
                    double cy = (*bbox).height() * scalein; 
                    cx += gapx_unit;
                    cy += gapy_unit;
                    if (shrink_interpove && (!interpolate_scalex || !interpolate_scaley)) {
                        double px = 0;
                        double py = 0; 
                        if (prev_bbox) {                    
                            px = (*prev_bbox).width();
                            py = (*prev_bbox).height();
                            px += gapx_unit;
                            py += gapy_unit;
                        }
                        if (interpolate_scalex) {
                            if (j) {
                                x = cx - ((cx-px)/2.0);
                                gapscalex += x;
                                x = gapscalex;
                            } else {
                                x = 0;
                                gapscalex = 0;
                            }
                            widthcols = 0;
                        } else if (interpolate_scaley) { 
                            x = 0;
                            if (i == 1) {
                                ygap[j] = ((cy-y[j])/2.0);
                                y[j] += ygap[j];
                            }
                            yset = y[j];
                            y[j] += cy + ygap[j];
                            heightrows = 0;
                        }                        
                    }
                    prev_bbox = bbox;
                } else {
                    y[j] = 0;
                }
                if (!counter) {
                    counter++;
                    continue;
                }
                double xset = x;
                xset += widthcols * j;
                if (heightrows) {
                    yset = heightrows * i; 
                }
                SPItem * item = toItem(counter - 1, reset, write); 
                if (item) {
                    if (!(lpesatellites.data().size() > counter - 1 && lpesatellites.data()[counter - 1])) {
                        item->deleteObject(true);
                        return;
                    }
                    prev_bbox = item->geometricBounds();
                    (*prev_bbox) *= r;
                    double offset_x = 0;
                    double offset_y = 0;
                    if (offset != 0) {
                        if (offset_type && j%2) {
                            offset_y = fixed_heightrows/(100.0/(double)offset);
                        }
                        if (!offset_type && i%2) {
                            offset_x = fixed_widthcols/(100.0/(double)offset);
                        }
                    }
                    
                    
                    auto p = Geom::Point(xset + offset_x - random_x[counter], yset + offset_y - random_y[counter]);
                    auto translate = p * gap.inverse();
                    Geom::Affine finalit = (transformoriginal * Geom::Translate(spcenter_base).inverse() * mirror * Geom::Translate(spcenter_base));
                    finalit *= gapp.inverse() * Geom::Translate(spcenter).inverse() * originatrans.withoutTranslation().inverse() * r * Geom::Translate(translate) * Geom::Translate(spcenter);
                    item->doWriteTransform(finalit);
                    item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
                    forcewrite = forcewrite || write;
                }
                counter++;
            }
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
}

void LPETiling::cloneStyle(SPObject *orig, SPObject *dest)
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
                if (auto const attr = orig->getAttribute(iter->name().c_str())) {
                    dest->setAttribute(iter->name(), attr);
                }
            }
        }
    } 
}

void LPETiling::cloneD(SPObject *orig, SPObject *dest)
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
    }  else if( is<SPGroup>(orig) && is<SPGroup>(dest) && cast<SPGroup>(orig)->getItemCount() != cast<SPGroup>(dest)->getItemCount()) {
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
LPETiling::createPathBase(SPObject *elemref) {
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


SPItem *
LPETiling::toItem(size_t i, bool reset, bool &write)
{
    SPDocument *document = getSPDoc();
    if (!document) {
        return nullptr;
    }
    
    SPObject *elemref = nullptr;
    if (container != sp_lpe_item->parent) {
        lpesatellites.read_from_SVG();
        return nullptr;
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
    reset = link_styles;
    if (creation) {
        write = true;
        lpesatellites.link(elemref, i);
    }
    return cast<SPItem>(elemref);
}

Gtk::ToggleButton* create_radio_button(Gtk::ToggleButton *&group, const Glib::ustring& tooltip, const Glib::ustring& icon_name) {
    auto const button = Gtk::make_managed<Gtk::ToggleButton>();
    if (group) {
        button->set_group(*group);
    } else {
        group = button;
    }
    button->set_tooltip_text(tooltip);
    button->set_image_from_icon_name(icon_name, Gtk::IconSize::NORMAL);
    button->set_halign(Gtk::Align::CENTER);
    button->set_valign(Gtk::Align::CENTER);
    button->add_css_class("lpe-square-button");
    return button;
}

void align_widgets(const std::vector<Gtk::Widget*>& widgets, int spinbutton_chars = 7) {
    // traverse container, locate n-th child in each row
    auto const for_child_n = [&widgets](int const child_index,
                                        std::function<void (Gtk::Widget *)> const &action)
    {
        for (auto child : widgets) {
            auto container = dynamic_cast<Gtk::Box *>(child);
            if (!container) continue;

            auto const children = UI::get_children(*container);
            if (child_index < children.size()) {
                action(children[child_index]);
            }
        }
    };

    auto const get_natural_width = [](Gtk::Widget const &widget)
    {
        g_assert(widget.get_visible());
        int natural{}, ignore{};
        widget.measure(Gtk::Orientation::HORIZONTAL, -1, ignore, natural, ignore, ignore);
        return natural;
    };

    // column 0 - labels
    int max_width = 0;
    for_child_n(0, [&](Gtk::Widget* child){
        if (auto label = dynamic_cast<Gtk::Label*>(child)) {
            label->set_xalign(0); // left-align
            max_width = std::max(max_width, get_natural_width(*label));
        }
    });
    // align
    for_child_n(0, [=](Gtk::Widget* child) {
        if (auto label = dynamic_cast<Gtk::Label*>(child)) {
            label->set_size_request(max_width);
        }
    });

    // column 1 - align spin buttons, if any
    int button_width = 0;
    for_child_n(1, [&](Gtk::Widget* child) {
        if (auto spin = dynamic_cast<Gtk::SpinButton*>(child)) {
            // arbitrarily selected spinbutton size
            spin->set_width_chars(spinbutton_chars);
            button_width = std::max(button_width, get_natural_width(*spin));
        } 
    });
    // set min size for comboboxes, if any
    int combo_size = button_width > 0 ? button_width : 50; // match with spinbuttons, or just min of 50px
    for_child_n(1, [=](Gtk::Widget* child) {
        if (auto combo = dynamic_cast<Gtk::ComboBox*>(child)) {
            combo->set_size_request(combo_size);
        }
    });
}

Gtk::Widget * LPETiling::newWidget()
{
    // use manage here, because after deletion of Effect object, others might
    // still be pointing to this widget.
    auto const vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    vbox->set_margin(5);

    Gtk::Widget *combo = nullptr;
    Gtk::Widget *randbutton = nullptr;
    Gtk::Box *containerstart = nullptr;
    Gtk::Box *containerend = nullptr;
    Gtk::Box *movestart = nullptr;
    Gtk::Box *moveend = nullptr;
    Gtk::Box *rowcols = nullptr;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool usemirroricons = prefs->getBool("/live_effects/copy/mirroricons",true);
    std::vector<Gtk::Widget*> scalars;

    for (auto const param: param_vector) {
        if (!param->widget_is_visible) continue;

        auto const widg = param->param_newWidget();
        if (!widg) continue;

        if (param->param_key == "unit") {
            prev_unit = unit.get_abbreviation();

            auto const widgcombo = dynamic_cast<UI::Widget::RegisteredUnitMenu *>(widg);

            auto const destroy_child = widgcombo->get_first_child();
            widgcombo->remove(*destroy_child);
            delete destroy_child;

            combo = widgcombo;

            if (!usemirroricons) continue;

            Gtk::ToggleButton *group = nullptr;
            auto const frame  = Gtk::make_managed<Gtk::Frame>(_("Mirroring mode"));
            frame->set_halign(Gtk::Align::START);
            auto const cbox  = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);
            auto const vbox1 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL,0);
            auto const hbox1 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);
            auto const hbox2 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);
            auto const vbox2 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL,0);
            auto const hbox3 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);
            auto const hbox4 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);
            vbox2->set_margin_start(5);
            vbox1->set_margin_bottom(3);
            UI::pack_start(*cbox, *vbox1, false, false);
            UI::pack_start(*cbox, *vbox2, false, false);
            cbox->set_margin_start(6);
            cbox->set_margin_end(6);
            cbox->set_margin_bottom(3);
            cbox->set_halign(Gtk::Align::START);
            hbox1->set_margin_bottom(3);
            hbox3->set_margin_bottom(3);
            frame->set_child(*cbox);
            UI::pack_start(*vbox, *frame, false, false, 1);
            UI::pack_start(*vbox1, *hbox1, false, false);
            UI::pack_start(*vbox1, *hbox2, false, false);
            UI::pack_start(*vbox2, *hbox3, false, false);
            UI::pack_start(*vbox2, *hbox4, false, false);
            generate_buttons(hbox1, group, 0);
            generate_buttons(hbox2, group, 1);
            generate_buttons(hbox3, group, 2);
            generate_buttons(hbox4, group, 3);

            continue;
        } else if (param->param_key == "seed"){
            auto const widgrand = dynamic_cast<UI::Widget::RegisteredRandom *>(widg);

            auto const destroy_child = widgrand->get_first_child();
            widgrand->remove(*destroy_child);
            delete destroy_child;

            auto const first = widgrand->get_first_child();
            first->set_visible(false);

            auto const button = dynamic_cast<Gtk::Button *>(first->get_next_sibling());
            g_assert(button);

            auto const box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
            box->append(*Gtk::manage(sp_get_icon_image(INKSCAPE_ICON("randomize"), Gtk::IconSize::NORMAL)));
            box->append(*Gtk::make_managed<Gtk::Label>(_("Randomize")));
            button->set_child(*box);

            button->set_tooltip_markup(_("Randomization seed for random mode for scaling, rotation and gaps"));
            button->set_has_frame(true);
            button->set_valign(Gtk::Align::START);

            widgrand->set_vexpand(false);
            widgrand->set_hexpand(false);
            widgrand->set_valign(Gtk::Align::START);
            widgrand->set_halign(Gtk::Align::START);
            randbutton = Gtk::manage(widgrand);

            continue;
        } else if (param->param_key == "offset_type" || 
            param->param_key == "mirrorrowsx" && usemirroricons ||
            param->param_key == "mirrorrowsy" && usemirroricons ||
            param->param_key == "mirrorcolsx" && usemirroricons ||
            param->param_key == "mirrorcolsy" && usemirroricons ||
            param->param_key == "interpolate_rotatex" ||
            param->param_key == "interpolate_rotatey" ||
            param->param_key == "interpolate_scalex" ||
            param->param_key == "interpolate_scaley" ||
            param->param_key == "random_scale" ||
            param->param_key == "random_rotate" ||
            param->param_key == "random_gap_x" ||
            param->param_key == "random_gap_y")
        {
            continue;
        } else if (param->param_key == "offset") {
            UI::pack_start(*movestart, *widg, false, false, 2);
            auto const container = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);
            Gtk::ToggleButton *group = nullptr;
            auto rows = create_radio_button(group, _("Offset rows"), INKSCAPE_ICON("rows"));
            auto cols = create_radio_button(group, _("Offset columns"), INKSCAPE_ICON("cols"));
            rows->set_tooltip_markup(_("Offset alternate rows"));
            cols->set_tooltip_markup(_("Offset alternate cols"));
            if (offset_type) {
                cols->set_active();
            } else {
                rows->set_active();
            }
            UI::pack_start(*container, *rows, false, false, 1);
            UI::pack_start(*container, *cols, false, false, 1);
            cols->signal_clicked().connect(sigc::mem_fun (*this, &LPETiling::setOffsetCols));
            rows->signal_clicked().connect(sigc::mem_fun (*this, &LPETiling::setOffsetRows));
            UI::pack_start(*moveend, *container, false, false, 2);
        } else if (param->param_key == "scale") {
            auto const container = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);
            Gtk::ToggleButton *group = nullptr;
            auto cols = create_radio_button(group, _("Interpolate X"), INKSCAPE_ICON("interpolate-scale-x"));
            auto rows = create_radio_button(group, _("Interpolate Y"), INKSCAPE_ICON("interpolate-scale-y"));
            auto both = create_radio_button(group, _("Interpolate both"), INKSCAPE_ICON("interpolate-scale-both"));
            auto none = create_radio_button(group, _("No interpolation"), INKSCAPE_ICON("interpolate-scale-none"));
            auto rand = create_radio_button(group, _("Interpolate random"), INKSCAPE_ICON("scale-random"));
            if (interpolate_scalex && interpolate_scaley) {
                both->set_active();
            } else if (interpolate_scalex) {
                cols->set_active();
            } else if (interpolate_scaley) {
                rows->set_active();
            } else if (random_scale) {
                rand->set_active();
            } else {
                none->set_active();
            }
            cols->set_tooltip_markup(_("Blend scale from <b>left to right</b> (left column uses original scale, right column uses new scale)"));
            rows->set_tooltip_markup(_("Blend scale from <b>top to bottom</b> (top row uses original scale, bottom row uses new scale)"));
            both->set_tooltip_markup(_("Blend scale <b>diagonally</b> (top left tile uses original scale, bottom right tile uses new scale)"));
            none->set_tooltip_markup(_("Uniform scale"));
            rand->set_tooltip_markup(_("Random scale (hit <b>Randomize</b> button to shuffle)"));
            UI::pack_start(*container, *rows, false, false, 1);
            UI::pack_start(*container, *cols, false, false, 1);
            UI::pack_start(*container, *both, false, false, 1);
            UI::pack_start(*container, *none, false, false, 1);
            UI::pack_start(*container, *rand, false, false, 1);
            rand->signal_clicked().connect(sigc::mem_fun(*this, &LPETiling::setScaleRandom));
            none->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &LPETiling::setScaleInterpolate), false, false));
            cols->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &LPETiling::setScaleInterpolate), true, false));
            rows->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &LPETiling::setScaleInterpolate), false, true));
            both->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &LPETiling::setScaleInterpolate), true, true));
            UI::pack_start(*movestart, *widg, false, false, 2);
            UI::pack_start(*moveend, *container, false, false, 2);
        } else if (param->param_key == "rotate") {
            UI::pack_start(*movestart, *widg, false, false, 2);
            auto const container = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);
            Gtk::ToggleButton *group = nullptr;
            auto cols = create_radio_button(group, _("Interpolate X"), INKSCAPE_ICON("interpolate-rotate-x"));
            auto rows = create_radio_button(group, _("Interpolate Y"), INKSCAPE_ICON("interpolate-rotate-y"));
            auto both = create_radio_button(group, _("Interpolate both"), INKSCAPE_ICON("interpolate-rotate-both"));
            auto none = create_radio_button(group, _("No interpolation"), INKSCAPE_ICON("interpolate-rotate-none"));
            auto rand = create_radio_button(group, _("Interpolate random"), INKSCAPE_ICON("rotate-random"));
            if (interpolate_rotatex && interpolate_rotatey) {
                both->set_active();
            } else if (interpolate_rotatex) {
                cols->set_active();
            } else if (interpolate_rotatey) {
                rows->set_active();
            } else if (random_rotate) {
                rand->set_active();
            } else {
                none->set_active();
            }
            cols->set_tooltip_markup(_("Blend rotation from <b>left to right</b> (left column uses original rotation, right column uses new rotation)"));
            rows->set_tooltip_markup(_("Blend rotation from <b>top to bottom</b> (top row uses original rotation, bottom row uses new rotation)"));
            both->set_tooltip_markup(_("Blend rotation <b>diagonally</b> (top left tile uses original rotation, bottom right tile uses new rotation)"));
            none->set_tooltip_markup(_("Uniform rotation"));
            rand->set_tooltip_markup(_("Random rotation (hit <b>Randomize</b> button to shuffle)"));
            UI::pack_start(*container, *rows, false, false, 1);
            UI::pack_start(*container, *cols, false, false, 1);
            UI::pack_start(*container, *both, false, false, 1);
            UI::pack_start(*container, *none, false, false, 1);
            UI::pack_start(*container, *rand, false, false, 1);
            rand->signal_clicked().connect(sigc::mem_fun(*this, &LPETiling::setRotateRandom));
            none->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &LPETiling::setRotateInterpolate), false, false));
            cols->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &LPETiling::setRotateInterpolate), true, false));
            rows->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &LPETiling::setRotateInterpolate), false, true));
            both->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &LPETiling::setRotateInterpolate), true, true));
            UI::pack_start(*moveend, *container, false, false, 2);
         } else if (param->param_key == "gapx") {
            auto const wrapper = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);
            movestart = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL,0);
            moveend = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL,0);
            moveend->set_homogeneous();
            moveend->set_valign(Gtk::Align::FILL);
            auto const container = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);
            Gtk::ToggleButton *group = nullptr;
            auto normal = create_radio_button(group, _("Normal"), INKSCAPE_ICON("interpolate-scale-none"));
            auto randx = create_radio_button(group, _("Random"), INKSCAPE_ICON("gap-random-x"));
            if (random_gap_x) {
                randx->set_active();
            } else {
                normal->set_active();
            }
            normal->set_tooltip_markup(_("All horizontal gaps have the same width"));
            randx->set_tooltip_markup(_("Random horizontal gaps (hit <b>Randomize</b> button to shuffle)"));
            normal->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &LPETiling::setGapXMode), false));
            randx->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &LPETiling::setGapXMode), true));
            UI::pack_start(*container, *normal, false, false, 1);
            UI::pack_start(*container, *randx, false, false, 1);
            combo->set_margin_end(0);
            UI::pack_end(*container, *combo, false, false, 1);
            UI::pack_start(*movestart, *widg, false, false, 2);
            UI::pack_start(*moveend, *container, false, false, 2);
            UI::pack_start(*wrapper, *movestart, false, false);
            UI::pack_start(*wrapper, *moveend, false, false);
            //bwidg->set_hexpand(true);
            combo->set_halign(Gtk::Align::END);
            widg->set_halign(Gtk::Align::START);
            UI::pack_start(*vbox, *wrapper, true, true);
        } else if (param->param_key == "gapy") {
            UI::pack_start(*movestart, *widg, true, true, 2);
            auto const container = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);
            Gtk::ToggleButton *group = nullptr;
            auto normal = create_radio_button(group, _("Normal"), INKSCAPE_ICON("interpolate-scale-none"));
            auto randy = create_radio_button(group, _("Random"), INKSCAPE_ICON("gap-random-y"));
            if (random_gap_y) {
                randy->set_active();
            } else {
                normal->set_active();
            }
            normal->set_tooltip_markup(_("All vertical gaps have the same height"));
            randy->set_tooltip_markup(_("Random vertical gaps (hit <b>Randomize</b> button to shuffle)"));
            normal->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &LPETiling::setGapYMode), false));
            randy->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &LPETiling::setGapYMode), true));
            UI::pack_start(*container, *normal, false, false, 1);
            UI::pack_start(*container, *randy, false, false, 1);
            widg->set_halign(Gtk::Align::START);
            UI::pack_start(*moveend, *container, false, false, 2);
        } else if (param->param_key == "mirrortrans"){
            auto const container = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL,0);
            auto const containerwraper = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);
            containerend = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL,0);
            containerstart = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL,0);
            UI::pack_start(*container, *containerwraper, false, true);
            UI::pack_start(*containerwraper, *containerstart, false, true);
            UI::pack_start(*containerwraper, *containerend, true, true);
            UI::pack_end(*containerend, *randbutton, true, true, 2);
            containerend->set_margin_start(8);
            UI::pack_start(*containerstart, *widg, false, true, 2);
            container->set_hexpand(false);
            containerwraper->set_hexpand(false);
            containerend->set_hexpand(false);
            containerstart->set_hexpand(false);
            UI::pack_start(*vbox, *container, false, true, 1);
        } else if (
            param->param_key == "split_items" ||
            param->param_key == "link_styles" ||
            param->param_key == "shrink_interp")
        { 
            UI::pack_start(*containerstart, *widg, true, true, 2);
            widg->set_vexpand(false);
            widg->set_hexpand(false);
            widg->set_valign(Gtk::Align::START);
            widg->set_halign(Gtk::Align::START);
        } else if (param->param_key == "num_rows") { 
            rowcols = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,0);
            UI::pack_start(*rowcols, *widg, false, false);
            UI::pack_start(*vbox, *rowcols, false, true, 2);
        } else if (param->param_key == "num_cols") { 
            UI::pack_start(*rowcols, *widg, false, false);
        } else {
            UI::pack_start(*vbox, *widg, false, true, 2);
        }

        if (auto const tip = param->param_getTooltip()) {
            widg->set_tooltip_markup(*tip);
        } else {
            widg->set_tooltip_markup({});
            widg->set_has_tooltip(false);
        }                
    
        if (dynamic_cast<ScalarParam *>(param)) {
            scalars.push_back(widg);
        }
    }

    align_widgets(scalars, 5);

    return vbox;
}

void
LPETiling::generate_buttons(Gtk::Box * const container, Gtk::ToggleButton *&group, int const pos)
{
    for (int i = 0; i < 4; i++) {
        int const position = (pos * 4) + i;
        auto const &result = getMirrorMap(position);
        auto const iconname = Glib::ustring::compose("mirroring-%1", result);
        auto button = create_radio_button(group, Glib::ustring(), iconname);
        if (getActiveMirror(position)) {
            _updating = true;
            button->set_active();
            _updating = false;
        }
        button->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &LPETiling::setMirroring),position));
        static constexpr int zero = static_cast<gunichar>('0');
        Glib::ustring tooltip = result[0] == zero ? "" : "rx+";
        tooltip += result[1] == zero ? "" : "ry+";
        tooltip += result[2] == zero ? "" : "cx+";
        tooltip += result[3] == zero ? "" : "cy+";
        if (tooltip.size()) {
            tooltip.erase(tooltip.size()-1);
        }
        button->set_tooltip_markup(tooltip);
        button->set_margin_start(1);
        UI::pack_start(*container, *button, false, false, 1);
    }
}

Glib::ustring 
LPETiling::getMirrorMap(int const index)
{
    Glib::ustring result = "0000";
    if (index == 1) {
        result = "1000";
    } else if (index == 2) {
        result = "1100";
    } else if (index == 3) {
        result = "0100";
    } else if (index == 4) {
        result = "0011";
    } else if (index == 5) {
        result = "1011";
    } else if (index == 6) {
        result = "1111";
    } else if (index == 7) {
        result = "0111";
    } else if (index == 8) {
        result = "0010";
    } else if (index == 9) {
        result = "1010";
    } else if (index == 10) {
        result = "1110";
    } else if (index == 11) {
        result = "0110";
    } else if (index == 12) {
        result = "0001";
    } else if (index == 13) {
        result = "1001";
    } else if (index == 14) {
        result = "1101";
    } else if (index == 15) {
        result = "0101";
    }
    return result;
}

bool
LPETiling::getActiveMirror(int const index)
{
    auto const &result = getMirrorMap(index);
    return result[0] == Inkscape::ustring::format_classic(mirrorrowsx)[0] &&
           result[1] == Inkscape::ustring::format_classic(mirrorrowsy)[0] &&
           result[2] == Inkscape::ustring::format_classic(mirrorcolsx)[0] &&
           result[3] == Inkscape::ustring::format_classic(mirrorcolsy)[0];
}

void 
LPETiling::setMirroring(int const index)
{
    if (_updating) {
        return;
    }
    _updating = true;
    auto const &result = getMirrorMap(index);
    static constexpr int zero = static_cast<gunichar>('0');
    mirrorrowsx.param_setValue(result[0] == zero ? false : true);
    mirrorrowsy.param_setValue(result[1] == zero ? false : true);
    mirrorcolsx.param_setValue(result[2] == zero ? false : true);
    mirrorcolsy.param_setValue(result[3] == zero ? false : true);
    writeParamsToSVG();
    _updating = false;
}

void
LPETiling::setOffsetCols(){
    offset_type.param_setValue(true);
    offset_type.write_to_SVG();
}
void
LPETiling::setOffsetRows(){
    offset_type.param_setValue(false);
    offset_type.write_to_SVG();
}

void
LPETiling::setRotateInterpolate(bool x, bool y){
    interpolate_rotatex.param_setValue(x);
    interpolate_rotatey.param_setValue(y);
    random_rotate.param_setValue(false);
    writeParamsToSVG();
}

void
LPETiling::setScaleInterpolate(bool x, bool y){
    interpolate_scalex.param_setValue(x);
    interpolate_scaley.param_setValue(y);
    random_scale.param_setValue(false);
    writeParamsToSVG();
}

void
LPETiling::setRotateRandom() {
    interpolate_rotatex.param_setValue(false);
    interpolate_rotatey.param_setValue(false);
    random_rotate.param_setValue(true);
    writeParamsToSVG();
}

void
LPETiling::setScaleRandom() {
    interpolate_scalex.param_setValue(false);
    interpolate_scaley.param_setValue(false);
    random_scale.param_setValue(true);
    writeParamsToSVG();
}

void
LPETiling::setGapXMode(bool random) {
    random_gap_x.param_setValue(random);
    writeParamsToSVG();
}

void
LPETiling::setGapYMode(bool random) {
    random_gap_y.param_setValue(random);
    writeParamsToSVG();
}

void
LPETiling::doOnApply(SPLPEItem const* lpeitem)
{
    if (lpeitem->getAttribute("transform")) {
        transformorigin.param_setValue(lpeitem->getAttribute("transform"), true);
    } else {
        transformorigin.param_setValue("", true);
    }
    lpeversion.param_setValue("1.3.1", true);
    legacy = false;
    doBeforeEffect(lpeitem);
}

void
LPETiling::doBeforeEffect (SPLPEItem const* lpeitem)
{
    if (is_load) {
        legacy = lpeversion.param_getSVGValue() < "1.3.1";
    }
    auto transformorigin_str = lpeitem->getAttribute("transform");
    if (transformorigin_str) {
        transformorigin.read_from_SVG();
        auto transformorigin_str = transformorigin.param_getSVGValue();
        transformoriginal = Geom::identity();
        if (!transformorigin_str.empty()) {
            sp_svg_transform_read(transformorigin_str.c_str(), &transformoriginal);
        }
    } else {
        transformorigin.param_setValue("", true);
        transformoriginal = Geom::identity();
    }
    //transformoriginal = transformoriginal.withoutTranslation();
    using namespace Geom;
    seed.resetRandomizer();
    random_x.clear();
    random_y.clear();
    random_s.clear();
    random_r.clear();
    if (prev_unit != unit.get_abbreviation()) {
        double newgapx = Inkscape::Util::Quantity::convert(gapx, prev_unit, unit.get_abbreviation());
        double newgapy = Inkscape::Util::Quantity::convert(gapy, prev_unit, unit.get_abbreviation());
        gapx.param_set_value(newgapx);
        gapy.param_set_value(newgapy);
        prev_unit = unit.get_abbreviation();
        writeParamsToSVG();
    }
    scaleok = (scale + 100) / 100.0;
    double seedset = seed.param_get_random_number() - seed.param_get_random_number();
    affinebase = Geom::identity();
    if (random_rotate && rotate) {
        affinebase *= Geom::Rotate::from_degrees(seedset * rotate);
    }
    if (random_scale && scaleok != 1) {
        affinebase *= Geom::Scale(seed.param_get_random_number() * (std::max(scaleok,1.0) - std::min(scaleok,1.0)) + std::min(scaleok,1.0));
    }
    if (random_gap_x && gapx_unit) {
        affinebase *= Geom::Translate(seed.param_get_random_number() * gapx_unit * -1, 0);
    }
    if (random_gap_y && gapy_unit) {
        affinebase *= Geom::Translate(0,seed.param_get_random_number() * gapy_unit * -1);
    }
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
    if (legacy) {
        auto const prev_display_unit = std::move(display_unit);
        display_unit = getSPDoc()->getDisplayUnit()->abbr;
        if (!display_unit.empty() && display_unit != prev_display_unit) {
            //_document->getDocumentScale().inverse()
            gapx.param_set_value(Inkscape::Util::Quantity::convert(gapx, display_unit.c_str(), prev_display_unit.c_str()));
            gapy.param_set_value(Inkscape::Util::Quantity::convert(gapy, display_unit.c_str(), prev_display_unit.c_str()));
            gapx.write_to_SVG();
            gapy.write_to_SVG();
        }
        gapx_unit = Inkscape::Util::Quantity::convert(gapx, unit.get_abbreviation(), display_unit.c_str());
        gapy_unit = Inkscape::Util::Quantity::convert(gapy, unit.get_abbreviation(), display_unit.c_str());
    } else {
        gapx_unit = Inkscape::Util::Quantity::convert(gapx, unit.get_abbreviation(), "px") / getSPDoc()->getDocumentScale()[Geom::X];
        gapy_unit = Inkscape::Util::Quantity::convert(gapy, unit.get_abbreviation(), "px") / getSPDoc()->getDocumentScale()[Geom::X];
    }
    original_bbox(sp_lpe_item, false, true, transformoriginal);
    originalbbox = Geom::OptRect(boundingbox_X,boundingbox_Y);
    Geom::Point A = Point(boundingbox_X.min() - (gapx_unit / 2.0), boundingbox_Y.min() - (gapy_unit / 2.0));
    Geom::Point B = Point(boundingbox_X.max() + (gapx_unit / 2.0), boundingbox_Y.max() + (gapy_unit / 2.0));
    gap_bbox = Geom::OptRect(A,B);
    if (!gap_bbox) {
        return;
    }
    
    double scale_fix = end_scale(scaleok, true);
    (*originalbbox) *= Geom::Translate((*originalbbox).midpoint()).inverse() * Geom::Scale(scale_fix) * Geom::Translate((*originalbbox).midpoint());
    if (!interpolate_scalex && !interpolate_scaley && !random_scale) {
        (*gap_bbox) *= Geom::Translate((*gap_bbox).midpoint()).inverse() * Geom::Scale(scaleok,scaleok) * Geom::Translate((*gap_bbox).midpoint());
        (*originalbbox) *= Geom::Translate((*originalbbox).midpoint()).inverse() * Geom::Scale(scaleok,scaleok) * Geom::Translate((*originalbbox).midpoint());
    }
    original_width = (*gap_bbox).width();
    original_height = (*gap_bbox).height();
}

double
LPETiling::end_scale(double scale_fix, bool tomax) const {
    if (interpolate_scalex && interpolate_scaley) {
        scale_fix = 1 + ((scale_fix - 1) * (num_rows + num_cols -1)); 
    } else if (interpolate_scalex) {
        scale_fix = 1 + ((scale_fix - 1) * (num_cols -1)); 
    } else if (interpolate_scaley) {
        scale_fix = 1 + ((scale_fix - 1) * (num_rows -1)); 
    }
    if (tomax && (random_scale || interpolate_scalex || interpolate_scaley)) {
        scale_fix = std::max(scale_fix, 1.0);
    }
    return scale_fix;
}

Geom::PathVector
LPETiling::doEffect_path (Geom::PathVector const & path_in)
{    
    Geom::PathVector path_out;
    FillRuleBool fillrule = fill_nonZero;
    if (current_shape->style && 
        current_shape->style->fill_rule.set &&
        current_shape->style->fill_rule.computed == SP_WIND_RULE_EVENODD) 
    {
        fillrule = (FillRuleBool)fill_oddEven;
    }
    path_out = doEffect_path_post(path_in, fillrule);
    if (_knotholder) {
        _knotholder->update_knots();
    }
    if (split_items) {
        return path_out;
    } else {
        return path_out * transformoriginal.inverse();
    }
}

Geom::PathVector
LPETiling::doEffect_path_post (Geom::PathVector const & path_in, FillRuleBool fillrule)
{
    if (!gap_bbox) {
        return path_in;
    }
    Geom::Point center = (*gap_bbox).midpoint() * transformoriginal.inverse();
    Geom::PathVector output;
    gint counter = 0;
    Geom::OptRect prev_bbox;
    double gapscalex = 0;
    double maxheight = 0;
    double maxwidth = 0;
    double minheight = std::numeric_limits<double>::max();
    Geom::OptRect bbox = path_in.boundsFast();
    if (!bbox) {
        return path_in;
    }
    (*bbox) *= transformoriginal;

    double posx = ((*gap_bbox).left() - (*bbox).left()) / (*gap_bbox).width();
    double factorx = original_width/(*bbox).width();
    double factory = original_height/(*bbox).height();
    double y[(int)num_cols]; 
    double yset = 0;
    double gap[(int)num_cols]; 
    for (int i = 0; i < num_rows; ++i) {
        double fracy = 1;
        if (num_rows != 1) {
            fracy = i/(double)(num_rows - 1);
        }
        for (int j = 0; j < num_cols; ++j) {
            double x = 0;
            double fracx = 1;
            if (num_cols != 1) {
                fracx = j/(double)(num_cols - 1);
            }
            Geom::Affine r = Geom::identity();
            r = Geom::identity();
            Geom::Scale mirror = Geom::Scale(1,1);
            bool reverse_pv = false;
            if(mirrorrowsx || mirrorrowsy || mirrorcolsx || mirrorcolsy) {
                gint mx = 1;
                gint my = 1;
                if (mirrorrowsx && mirrorcolsx) {
                    mx = (j+i)%2 != 0 ? -1 : 1;
                } else {
                    if (mirrorrowsx) {
                        mx = i%2 != 0 ? -1 : 1;
                    } else if (mirrorcolsx) {
                        mx = j%2 != 0 ? -1 : 1;
                    }
                }
                if (mirrorrowsy && mirrorcolsy) {
                    my = (j+i)%2 != 0 ? -1 : 1;
                } else {                     
                    if (mirrorrowsy) {
                        my = i%2 != 0 ? -1 : 1; 
                    } else if (mirrorcolsy) {
                        my = j%2 != 0 ? -1 : 1;
                    }
                }
                mirror = Geom::Scale(mx, my);
                reverse_pv = mx * my == -1;
            }
            if (mirrortrans && interpolate_scalex && i%2 != 0) {
                fracx = 1-fracx;
            }
            double fracyin = fracy;
            if (mirrortrans && interpolate_scaley && j%2 != 0) {
                fracyin = 1-fracyin;
            }
            /* if (mirrortrans && interpolate_scaley && interpolate_scalex) {
                fract = 1-fract;
            } */
            double rotatein = rotate;
            if (interpolate_rotatex && interpolate_rotatey) {
                rotatein = rotatein * (i + j);
            } else if (interpolate_rotatex) {
                rotatein = rotatein * j;
            } else if (interpolate_rotatey) {
                rotatein = rotatein * i;
            }
            if (mirrortrans && 
                ((interpolate_rotatex && i%2 != 0) ||
                 (interpolate_rotatey && j%2 != 0) ||
                 (interpolate_rotatex && interpolate_rotatey))) 
            {
                rotatein *=-1;
            }
            double scalein = 1;
            double scalegap = scaleok - scalein;
            if (interpolate_scalex && interpolate_scaley) {
                scalein = (scalegap * (i + j)) + 1;
            } else if (interpolate_scalex) {
                scalein = (scalegap * j) + 1;
            } else if (interpolate_scaley) {
                scalein = (scalegap * i) + 1;
            } else {
                scalein = scaleok;
            }
            
            if (random_scale && scaleok != 1.0) {
                if (random_s.size() == counter) {
                    double max = std::max(1.0,scaleok);
                    double min = std::min(1.0,scaleok);
                    random_s.emplace_back(seed.param_get_random_number()  * (max - min) + min);
                }
                scalein = random_s[counter];
            }
            if (random_rotate && rotate) {
                if (random_r.size() == counter) {
                    random_r.emplace_back((seed.param_get_random_number() - seed.param_get_random_number()) * rotate);
                }
                rotatein = random_r[counter];
            }
            if (random_x.size() == counter) {
                if (random_gap_x && gapx_unit && (j || i)) {
                    random_x.emplace_back((seed.param_get_random_number() * gapx_unit)); // avoid overlapping
                } else {
                    random_x.emplace_back(0);
                }
            }
            if (random_y.size() == counter) {
                if (random_gap_y && gapy_unit && (j || i)) {
                    random_y.emplace_back((seed.param_get_random_number() * gapy_unit)); // avoid overlapping
                } else {
                    random_y.emplace_back(0);
                }
            }
            r *= Geom::Scale(scalein, scalein);
            r *= Geom::Rotate::from_degrees(rotatein);
            
            Geom::PathVector output_pv = pathv_to_linear_and_cubic_beziers(path_in);
            if (reverse_pv) {
                output_pv.reverse();
            }
            
            output_pv *= Geom::Translate(center).inverse();
            output_pv *= r;
            if (!interpolate_rotatex && !interpolate_rotatey && !random_rotate) {
                output_pv *= Geom::Rotate::from_degrees(rotate);
            }
            if (!interpolate_scalex && !interpolate_scaley && !random_scale) {
                output_pv *= Geom::Scale(scaleok, scaleok);
            }
            originatrans = r;
            output_pv *= Geom::Translate(center); 
            if (split_items) {
                return output_pv;
            }
            double scale_fix = end_scale(scaleok, true);
            double heightrows = original_height * scale_fix;
            double widthcols = original_width * scale_fix;
            double fixed_heightrows = heightrows;
            double fixed_widthcols = widthcols;

            if (rotatein && shrink_interp) {
                shrink_interp.param_setValue(false);
                shrink_interp.write_to_SVG();
                return path_in;
            }
            if (scaleok != 1.0 && (interpolate_scalex || interpolate_scaley )) {
                Geom::OptRect bbox = output_pv.boundsFast();
                if (bbox) {
                    maxheight = std::max(maxheight,(*bbox).height());
                    maxwidth = std::max(maxwidth,(*bbox).width());
                    minheight = std::min(minheight,(*bbox).height());
                    widthcols = std::max(original_width * end_scale(scaleok, false),original_width);
                    heightrows = std::max(original_height * end_scale(scaleok, false),original_height);
                    fixed_widthcols = widthcols;
                    fixed_heightrows = heightrows;
                    double cx = (*bbox).width();
                    double cy = (*bbox).height(); 
                    if (shrink_interp && (!interpolate_scalex || !interpolate_scaley)) {
                        heightrows = 0;
                        widthcols = 0;
                        double px = 0;
                        double py = 0; 
                        if (prev_bbox) {                    
                            px = (*prev_bbox).width();
                            py = (*prev_bbox).height();
                        }
                        if (interpolate_scalex) {
                            if (j) {
                                x = ((cx - ((cx - px) / 2.0))) * factorx;
                                gapscalex += x;
                                x = gapscalex;
                            } else {
                                x = 0;
                                gapscalex = 0;
                            }
                        } else {
                            x = (std::max(original_width * end_scale(scaleok, false), original_width) + posx) * j;
                        }
                        if (interpolate_scalex && i == 1) {
                            y[j] = maxheight * factory;
                        } else if(i == 0) {
                            y[j] = 0;
                        }
                        if (i == 1 && !interpolate_scalex) {
                            gap[j] = ((cy * factory) - y[j])/2.0;
                        } else if (i == 0) {
                            gap[j] = 0;
                        }
                        yset = y[j] + (gap[j] * i);
                        if (interpolate_scaley) {
                            y[j] += cy * factory;
                        } else {
                            y[j] += maxheight * factory;
                        }
                    }
                    prev_bbox = bbox;
                }
            } else {
                y[j] = 0;
            }
            double xset = x;
            xset += widthcols * j;
            if (heightrows) {
                yset = heightrows * i; 
            }
            double offset_x = 0;
            double offset_y = 0;
            if (offset != 0) {
                if (offset_type && j%2) {
                    offset_y = fixed_heightrows/(100.0/(double)offset);
                }
                if (!offset_type && i%2) {
                    offset_x = fixed_widthcols/(100.0/(double)offset);
                    
                }
            }
            output_pv *= Geom::Translate(center).inverse() * mirror * Geom::Translate(center);
            output_pv *= transformoriginal;
            output_pv *= Geom::Translate(Geom::Point(xset + offset_x - random_x[counter],yset + offset_y - random_y[counter]));
            output.insert(output.end(), output_pv.begin(), output_pv.end());
            counter++;
        }
    }
    return output;
}

void
LPETiling::addCanvasIndicators(SPLPEItem const *lpeitem, std::vector<Geom::PathVector> &hp_vec)
{
    if (!gap_bbox) {
        return;
    }
    using namespace Geom;
    hp_vec.clear();
    Geom::Path hp = Geom::Path(*gap_bbox);
    double scale_fix = end_scale(scaleok, true);
    hp *= Geom::Translate((*gap_bbox).midpoint()).inverse() *  Geom::Scale(scale_fix) * Geom::Translate((*gap_bbox).midpoint());
    hp *= transformoriginal.inverse();
    Geom::PathVector pathv;
    pathv.push_back(hp);
    hp_vec.push_back(pathv);
}

void
LPETiling::resetDefaults(SPItem const* item)
{
    Effect::resetDefaults(item);
    original_bbox(cast<SPLPEItem>(item), false, true);
}

void
LPETiling::doOnVisibilityToggled(SPLPEItem const* lpeitem)
{   
    auto transformorigin_str = lpeitem->getAttribute("transform");
    Geom::Affine ontoggle = Geom::identity();
    if (transformorigin_str) {
        sp_svg_transform_read(transformorigin_str, &ontoggle);
    }
    ontoggle = ontoggle;
    if (is_visible) {
        if ( ontoggle == Geom::identity()) {
            transformorigin.param_setValue("", true);
        } else {
            ontoggle = ontoggle * hideaffine.inverse() * transformoriginal;
            transformorigin.param_setValue(sp_svg_transform_write(ontoggle), true);
        }
    } else {
        hideaffine = ontoggle;
    }
    processObjects(LPE_VISIBILITY);
}


void 
LPETiling::doOnRemove (SPLPEItem const* lpeitem)
{
    if (keep_paths) {
        processObjects(LPE_TO_OBJECTS);
        return;
    }
    processObjects(LPE_ERASE);
}

void LPETiling::addKnotHolderEntities(KnotHolder *knotholder, SPItem *item)
{
    _knotholder = knotholder;
    KnotHolderEntity *e = new CoS::KnotHolderEntityCopyGapX(this);
    e->create(nullptr, item, knotholder, Inkscape::CANVAS_ITEM_CTRL_TYPE_LPE, "LPE:CopiesGapX",
              _("<b>Horizontal gaps between tiles</b>: drag to adjust, <b>Shift+click</b> to reset"));
    knotholder->add(e);

    KnotHolderEntity *f = new CoS::KnotHolderEntityCopyGapY(this);
    f->create(nullptr, item, knotholder, Inkscape::CANVAS_ITEM_CTRL_TYPE_LPE, "LPE:CopiesGapY",
              _("<b>Vertical gaps between tiles</b>: drag to adjust, <b>Shift+click</b> to reset"));
    knotholder->add(f);
}

namespace CoS {

KnotHolderEntityCopyGapX::~KnotHolderEntityCopyGapX()
{
    LPETiling* lpe = dynamic_cast<LPETiling *>(_effect);
    if (lpe) {
        lpe->_knotholder = nullptr;
    }
}

KnotHolderEntityCopyGapY::~KnotHolderEntityCopyGapY()
{
    LPETiling* lpe = dynamic_cast<LPETiling *>(_effect);
    if (lpe) {
        lpe->_knotholder = nullptr;
    }
}

void KnotHolderEntityCopyGapX::knot_click(guint state)
{
    if (!(state & GDK_SHIFT_MASK)) {
        return;
    }

    LPETiling* lpe = dynamic_cast<LPETiling *>(_effect);

    lpe->gapx.param_set_value(0);
    startpos = 0;
    sp_lpe_item_update_patheffect(cast<SPLPEItem>(item), false, false);
}

void KnotHolderEntityCopyGapY::knot_click(guint state)
{
    if (!(state & GDK_SHIFT_MASK)) {
        return;
    }

    LPETiling* lpe = dynamic_cast<LPETiling *>(_effect);

    lpe->gapy.param_set_value(0);
    startpos = 0;
    sp_lpe_item_update_patheffect(cast<SPLPEItem>(item), false, false);
}

void KnotHolderEntityCopyGapX::knot_set(Geom::Point const &p, Geom::Point const&/*origin*/, guint state)
{
    LPETiling* lpe = dynamic_cast<LPETiling *>(_effect);

    Geom::Point const s = snap_knot_position(p, state);
    if (lpe->originalbbox) {
        Geom::Point point = (*lpe->originalbbox).corner(1);
        point *= lpe->transformoriginal.inverse();
        double value = s[Geom::X] - point[Geom::X];
        if (lpe->legacy) {
            Glib::ustring doc_unit = SP_ACTIVE_DOCUMENT->getWidth().unit->abbr.c_str();
            value = Inkscape::Util::Quantity::convert((value/lpe->end_scale(lpe->scaleok, false)) * 2, doc_unit.c_str(),lpe->unit.get_abbreviation());
        } else {
            value = Inkscape::Util::Quantity::convert((value/lpe->end_scale(lpe->scaleok, false)) * 2, "px", lpe->unit.get_abbreviation()) * SP_ACTIVE_DOCUMENT->getDocumentScale()[Geom::X];
        }
        lpe->gapx.param_set_value(value);
        lpe->gapx.write_to_SVG();
    }
}

void KnotHolderEntityCopyGapY::knot_set(Geom::Point const &p, Geom::Point const& /*origin*/, guint state)
{
    LPETiling* lpe = dynamic_cast<LPETiling *>(_effect);

    Geom::Point const s = snap_knot_position(p, state);
    if (lpe->originalbbox) {
        Geom::Point point = (*lpe->originalbbox).corner(3);
        point *= lpe->transformoriginal.inverse();
        double value = s[Geom::Y] - point[Geom::Y];
        if (lpe->legacy) {
            Glib::ustring doc_unit = SP_ACTIVE_DOCUMENT->getWidth().unit->abbr.c_str();
            value = Inkscape::Util::Quantity::convert((value/lpe->end_scale(lpe->scaleok, false)) * 2, doc_unit.c_str(),lpe->unit.get_abbreviation());
        } else {
            value = Inkscape::Util::Quantity::convert((value/lpe->end_scale(lpe->scaleok, false)) * 2, "px", lpe->unit.get_abbreviation()) * SP_ACTIVE_DOCUMENT->getDocumentScale()[Geom::X];
        }
        lpe->gapy.param_set_value(value);
        lpe->gapy.write_to_SVG();
    }
}

Geom::Point KnotHolderEntityCopyGapX::knot_get() const
{
    LPETiling const * lpe = dynamic_cast<LPETiling const*> (_effect);
    Geom::Point ret = Geom::Point(Geom::infinity(),Geom::infinity());
    if (lpe->originalbbox) {
        auto bbox = (*lpe->originalbbox);
        double value;
        if (lpe->legacy) {
            Glib::ustring prev_unit = SP_ACTIVE_DOCUMENT->getDisplayUnit()->abbr.c_str();
            value = Inkscape::Util::Quantity::convert(lpe->gapx, lpe->unit.get_abbreviation(), prev_unit.c_str());
        } else {
            value = Inkscape::Util::Quantity::convert(lpe->gapx, lpe->unit.get_abbreviation(), "px") / SP_ACTIVE_DOCUMENT->getDocumentScale()[Geom::X];
        }
        double scale = lpe->scaleok;
        ret = (bbox).corner(1) + Geom::Point((value * lpe->end_scale(scale, false))/2.0,0);
        ret *= lpe->transformoriginal.inverse();
    }
    return ret;
}

Geom::Point KnotHolderEntityCopyGapY::knot_get() const
{
    LPETiling const * lpe = dynamic_cast<LPETiling const*> (_effect);
    Geom::Point ret = Geom::Point(Geom::infinity(),Geom::infinity());
    if (lpe->originalbbox) {
        auto bbox = (*lpe->originalbbox);
        double value;
        if (lpe->legacy) {
            Glib::ustring prev_unit = SP_ACTIVE_DOCUMENT->getDisplayUnit()->abbr.c_str();
            value = Inkscape::Util::Quantity::convert(lpe->gapy, lpe->unit.get_abbreviation(), prev_unit.c_str());
        } else {
            value = Inkscape::Util::Quantity::convert(lpe->gapy, lpe->unit.get_abbreviation(), "px") / SP_ACTIVE_DOCUMENT->getDocumentScale()[Geom::X];
        }
        double scale = lpe->scaleok;
        ret = (bbox).corner(3) + Geom::Point(0,(value * lpe->end_scale(scale, false))/2.0);
        ret *= lpe->transformoriginal.inverse();
    }
    return ret;
}

} // namespace CoS

} // namespace Inkscape::LivePathEffect

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
