// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) Johan Engelen 2012 <j.b.c.engelen@alumnus.utwente.nl>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "lpe-clone-original.h"

#include <cstddef>

#include <glibmm/i18n.h>

#include "lpe-bspline.h"
#include "lpeobject.h"
#include "lpeobject-reference.h"
#include "lpe-spiro.h"

#include "inkscape.h"
#include "selection.h"

#include "actions/actions-tools.h"
#include "display/curve.h"
#include "live_effects/parameter/satellite-reference.h"
#include "object/sp-clippath.h"
#include "object/sp-item-group.h"
#include "object/sp-mask.h"
#include "object/sp-path.h"
#include "object/sp-shape.h"
#include "object/sp-text.h"
#include "object/sp-use.h"
#include "svg/path-string.h"
#include "svg/svg.h"
#include "ui/pack.h"
#include "ui/tools/node-tool.h"
#include "util/optstr.h"
#include "xml/sp-css-attr.h"

namespace Inkscape {
namespace LivePathEffect {

static const Util::EnumData<Clonelpemethod> ClonelpemethodData[] = {
    { CLM_NONE, N_("No Shape"), "none" },
    { CLM_D, N_("With LPE's"), "d" },
    { CLM_ORIGINALD, N_("Without LPE's"), "originald" },
    { CLM_BSPLINESPIRO, N_("Spiro or BSpline Only"), "bsplinespiro" },
};
static const Util::EnumDataConverter<Clonelpemethod> CLMConverter(ClonelpemethodData, CLM_END);

LPECloneOriginal::LPECloneOriginal(LivePathEffectObject *lpeobject)
    : Effect(lpeobject)
    , linkeditem(_("Linked Item:"), _("Item from which to take the original data"), "linkeditem", &wr, this)
    , method(_("Shape"), _("Linked shape"), "method", CLMConverter, &wr, this, CLM_D)
    , attributes(_("Attributes"), _("Attributes of the original that the clone should copy, written as a comma-separated list; e.g. 'transform, style, clip-path, X, Y'."),
                 "attributes", &wr, this, "")
    , css_properties(_("CSS Properties"),
                       _("CSS properties of the original that the clone should copy, written as a comma-separated list; e.g. 'fill, filter, opacity'."),
                       "css_properties", &wr, this, "")
    , allow_transforms(_("Allow Transforms"), _("Allow transforms"), "allow_transforms", &wr, this, true)
{
    //0.92 compatibility
    const gchar *linkedpath = getLPEObj()->getAttribute("linkedpath");
    if (linkedpath && strcmp(linkedpath, "") != 0){
        getLPEObj()->setAttribute("linkeditem", linkedpath);
        getLPEObj()->removeAttribute("linkedpath");
        getLPEObj()->setAttribute("method", "bsplinespiro");
        getLPEObj()->setAttribute("allow_transforms", "false");
    };

    sync = false;
    linked = "";
    if (getLPEObj()->getAttribute("linkeditem")) {
        linked = getLPEObj()->getAttribute("linkeditem");
    }
    registerParameter(&linkeditem);
    registerParameter(&method);
    registerParameter(&attributes);
    registerParameter(&css_properties);
    registerParameter(&allow_transforms);
    attributes.param_hide_canvas_text();
    css_properties.param_hide_canvas_text();
}

LPECloneOriginal::~LPECloneOriginal() = default;

bool LPECloneOriginal::doOnOpen(SPLPEItem const *lpeitem)
{
    // we need to inform when all items are ready to read svg relink clones
    // previously couldn't be because clones are not ready (load later)
    linkeditem.start_listening(linkeditem.getObject());
    linkeditem.connect_selection_changed();
    return false;
}

void
LPECloneOriginal::syncOriginal()
{
    if (method != CLM_NONE) {
        sync = true;
        sp_lpe_item_update_patheffect (sp_lpe_item, false, true);
        method.param_set_value(CLM_NONE);
        refresh_widgets = true;
        SPDesktop *desktop = SP_ACTIVE_DESKTOP;
        sp_lpe_item_update_patheffect (sp_lpe_item, false, true);
        if (desktop && dynamic_cast<Inkscape::UI::Tools::NodeTool *>(desktop->getTool())) {
            // Why is this switching tools twice? Probably to reinitialize Node Tool.
            set_active_tool(desktop, "Select");
            set_active_tool(desktop, "Node");
        }
    }
}

Gtk::Widget *
LPECloneOriginal::newWidget()
{
    // use manage here, because after deletion of Effect object, others might still be pointing to this widget.
    auto const vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    vbox->property_margin().set_value(5);

    std::vector<Parameter *>::iterator it = param_vector.begin();
    while (it != param_vector.end()) {
        if ((*it)->widget_is_visible) {
            Parameter * param = *it;
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
    auto const sync_button = Gtk::make_managed<Gtk::Button>(Glib::ustring(_("No Shape Sync to Current")));
    sync_button->signal_clicked().connect(sigc::mem_fun (*this,&LPECloneOriginal::syncOriginal));
    UI::pack_start(*vbox, *sync_button, true, true, 2);
    return vbox;
}

void
LPECloneOriginal::cloneAttributes(SPObject *origin, SPObject *dest, const gchar * attributes, const gchar * css_properties, bool init) 
{
    SPDocument *document = getSPDoc();
    if (!document || !origin || !dest) {
        return;
    }
    bool root = dest == sp_lpe_item;
    auto group_origin = cast<SPGroup>(origin);
    auto group_dest = cast<SPGroup>(dest);
    if (group_origin && group_dest && group_origin->getItemCount() == group_dest->getItemCount()) {
        std::vector< SPObject * > childs = group_origin->childList(true);
        size_t index = 0;
        for (auto & child : childs) {
            SPObject *dest_child = group_dest->nthChild(index);
            cloneAttributes(child, dest_child, attributes, css_properties, init);
            index++;
        }
    } else if ((!group_origin &&  group_dest) ||
               ( group_origin && !group_dest)) 
    {
        g_warning("LPE Clone Original: for this path effect to work properly, the same type and the same number of children are required");
        return;
    }
    //Attributes
    auto shape_origin = cast<SPShape>(origin);
    auto shape_dest   = cast<SPShape>(dest);
    auto path_dest   = cast<SPPath>(dest);

    gchar ** attarray = g_strsplit(old_attributes.c_str(), ",", 0);
    gchar ** iter = attarray;
    while (*iter) {
        const char *attribute = g_strstrip(*iter);
        if (strlen(attribute)) {
            dest->removeAttribute(attribute);
        }
        iter++;
    }
    g_strfreev(attarray);

    attarray = g_strsplit(attributes, ",", 0);
    iter = attarray;
    while (*iter) {
        const char *attribute = g_strstrip(*iter);
        if (strlen(attribute) && shape_dest && shape_origin) {
            if (std::strcmp(attribute, "d") == 0) {
                std::optional<SPCurve> c;
                if (method == CLM_BSPLINESPIRO) {
                    c = SPCurve::ptr_to_opt(shape_origin->curveForEdit());
                    auto lpe_item = cast<SPLPEItem>(origin);
                    if (lpe_item) {
                        PathEffectList lpelist = lpe_item->getEffectList();
                        PathEffectList::iterator i;
                        for (i = lpelist.begin(); i != lpelist.end(); ++i) {
                            LivePathEffectObject *lpeobj = (*i)->lpeobject;
                            if (lpeobj) {
                                Inkscape::LivePathEffect::Effect *lpe = lpeobj->get_lpe();
                                if (auto bspline = dynamic_cast<Inkscape::LivePathEffect::LPEBSpline *>(lpe)) {
                                    Geom::PathVector hp;
                                    LivePathEffect::sp_bspline_do_effect(*c, 0, hp, bspline->uniform);
                                } else if (dynamic_cast<Inkscape::LivePathEffect::LPESpiro *>(lpe)) {
                                    LivePathEffect::sp_spiro_do_effect(*c);
                                }
                            }
                        }
                    }
                } else if (method == CLM_ORIGINALD) {
                    c = SPCurve::ptr_to_opt(shape_origin->curveForEdit());
                } else if(method == CLM_D){
                    c = SPCurve::ptr_to_opt(shape_origin->curve());
                }
                if (c && method != CLM_NONE) {
                    Geom::PathVector c_pv = c->get_pathvector();
                    c->set_pathvector(c_pv);
                    auto str = sp_svg_write_path(c_pv);
                    if (sync){
                        if (path_dest) {
                            dest->setAttribute("inkscape:original-d", str);
                        } else {
                            dest->setAttribute("d", str);
                        }
                    }
                    shape_dest->setCurveInsync(std::move(*c));
                    dest->setAttribute("d", str);
                } else if (method != CLM_NONE) {
                    dest->removeAttribute(attribute);
                }
            } else {
                dest->setAttribute(attribute, origin->getAttribute(attribute));
            }
        } else if (strlen(attribute)) {
            dest->setAttribute(attribute, origin->getAttribute(attribute));
        }
        iter++;
    }
    if (!allow_transforms || !root) {
        dest->setAttribute("transform", origin->getAttribute("transform"));
        dest->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
    }
    g_strfreev(attarray);

    SPCSSAttr *css_origin = sp_repr_css_attr_new();
    sp_repr_css_attr_add_from_string(css_origin, origin->getAttribute("style"));
    SPCSSAttr *css_dest = sp_repr_css_attr_new();
    sp_repr_css_attr_add_from_string(css_dest, dest->getAttribute("style"));
    if (init) {
        css_dest = css_origin;
    }
    gchar ** styleattarray = g_strsplit(old_css_properties.c_str(), ",", 0);
    gchar ** styleiter = styleattarray;
    while (*styleiter) {
        const char *attribute = g_strstrip(*styleiter);
        if (strlen(attribute)) {
            sp_repr_css_set_property (css_dest, attribute, nullptr);
        }
        styleiter++;
    }
    g_strfreev(styleattarray);

    styleattarray = g_strsplit(css_properties, ",", 0);
    styleiter = styleattarray;
    while (*styleiter) {
        const char *attribute = g_strstrip(*styleiter);
        if (strlen(attribute)) {
            const char* origin_attribute = sp_repr_css_property(css_origin, attribute, "");
            if (!strlen(origin_attribute)) { //==0
                sp_repr_css_set_property (css_dest, attribute, nullptr);
            } else {
                sp_repr_css_set_property (css_dest, attribute, origin_attribute);
            }
        }
        styleiter++;
    }
    g_strfreev(styleattarray);

    Glib::ustring css_str;
    sp_repr_css_write_string(css_dest,css_str);
    dest->setAttributeOrRemoveIfEmpty("style", css_str);
}

void
LPECloneOriginal::doBeforeEffect (SPLPEItem const* lpeitem){
    SPDocument *document = getSPDoc();
    if (!document) {
        return;
    }
    if (!is_load && !isOnClipboard() && linkeditem.lperef && 
        linkeditem.lperef->isAttached() && linkeditem.lperef.get()->getObject() == nullptr) 
    {
        linkeditem.unlink();
        return;
    }
    bool init = false;
    if (!linkeditem.linksToItem() || isOnClipboard()) {
        linkeditem.read_from_SVG();
        init = true;
    } 
    if (linkeditem.linksToItem()) {
        if (!linkeditem.isConnected() && linkeditem.getObject()) {
            linkeditem.start_listening(linkeditem.getObject());
            sp_lpe_item_update_patheffect(sp_lpe_item, false, false, true);
            return;
        }
        auto orig = cast<SPItem>(linkeditem.getObject());
        if(!orig) {
            return;
        }
        auto text_origin = cast<SPText>(orig);
        auto *dest = sp_lpe_item;
        auto *dest_path = cast<SPPath>(sp_lpe_item);
        auto *dest_shape = cast<SPShape>(sp_lpe_item);
        const gchar * id = getLPEObj()->getAttribute("linkeditem");
        init = init || linked == "" || g_strcmp0(id, linked.c_str()) != 0;
        /* if (sp_lpe_item->getRepr()->attribute("style")) {
            init = false;
        } */
        Glib::ustring attr = "d,";
        if (text_origin && dest_shape) {
            auto curve = text_origin->getNormalizedBpath();
            if (dest_path) {
                dest->setAttribute("inkscape:original-d", sp_svg_write_path(curve.get_pathvector()));
            }
            dest_shape->setCurveInsync(curve);
            dest_shape->setAttribute("d", sp_svg_write_path(curve.get_pathvector()));
            attr = "";
        }
        if (g_strcmp0(linked.c_str(), id) && !is_load) {
            dest->setAttribute("transform", nullptr);
        }
        original_bbox(lpeitem, false, true);
        auto attributes_str = attributes.param_getSVGValue();
        attr += attributes_str + ",";
        if (attr.size()  && attributes_str.empty()) {
            attr.erase (attr.size()-1, 1);
        }
        auto css_properties_str = css_properties.param_getSVGValue();
        Glib::ustring style_attr = "";
        if (style_attr.size() && css_properties_str.empty()) {
            style_attr.erase (style_attr.size()-1, 1);
        }
        style_attr += css_properties_str + ",";
        cloneAttributes(orig, dest, attr.c_str(), style_attr.c_str(), init);
        old_css_properties = css_properties.param_getSVGValue();
        old_attributes = attributes.param_getSVGValue();
        sync = false;
        linked = id;
    } else {
        linked = "";
    }
}

bool LPECloneOriginal::getHolderRemove() {
    // this leave a empty path item but keep clone
    std::vector<SPLPEItem *> lpeitems = getCurrrentLPEItems();
    if (!holderRemove && lpeitems.size() == 1 && !keep_paths && !on_remove_all) {
        if (lpeitems[0] && lpeitems[0]->getAttribute("class")) {
            Glib::ustring fromclone = sp_lpe_item->getAttribute("class");
            size_t pos = fromclone.find("fromclone");
            if (pos != Glib::ustring::npos && !lpeitems[0]->document->isSeeking()) {
                if (linkeditem.lperef->getObject() && SP_ACTIVE_DESKTOP) {
                    holderRemove = true;
                    return holderRemove;
                }
            }
        }
    }
    return false;
}

void LPECloneOriginal::doOnRemove(SPLPEItem const *lpeitem)
{
    // this leave a empty path item but keep clone
    if (holderRemove && lpeitem) {
        if (lpeitem->getAttribute("class")) {
            Glib::ustring fromclone = lpeitem->getAttribute("class");
            size_t pos = fromclone.find("fromclone");
            if (pos != Glib::ustring::npos && !lpeitem->document->isSeeking()) {
                auto transform_copy = Util::to_opt(sp_lpe_item->getAttribute("transform"));
                if (SPObject *owner = linkeditem.lperef->getObject()) {
                    auto oset = ObjectSet(lpeitem->document);
                    oset.add(owner);
                    oset.clone(true);
                    if (SPUse *clone = cast<SPUse>(oset.singleItem())) {
                        auto transform_use = clone->get_root_transform();
                        clone->transform *= transform_use.inverse();
                        if (Util::to_cstr(transform_copy)) {
                            Geom::Affine item_t(Geom::identity());
                            sp_svg_transform_read(Util::to_cstr(transform_copy), &item_t);
                            clone->transform *= item_t;
                        }
                        // update use real transform
                        clone->doWriteTransform(clone->transform);
                        clone->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
                    }
                }
            }
        }
    }
    linkeditem.quit_listening();
    linkeditem.unlink();
}

void
LPECloneOriginal::doEffect (SPCurve * curve)
{
    SPCurve const *current_curve_before = current_shape->curveBeforeLPE();
    if (!current_curve_before || current_curve_before->get_pathvector() == sp_svg_read_pathv("M 0 0")) {
        syncOriginal();
    }
    if (method != CLM_NONE) {
        SPCurve const *current_curve = current_shape->curve();
        if (current_curve != nullptr) {
            curve->set_pathvector(current_curve->get_pathvector());
        }
    }
}

} // namespace LivePathEffect
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
