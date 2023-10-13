// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Generic drawing context
 *
 * Author:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Abhishek Sharma
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright (C) 2000 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 * Copyright (C) 2002 Lauris Kaplinski
 * Copyright (C) 2012 Johan Engelen
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "freehand-base.h"

#include "desktop-style.h"
#include "id-clash.h"
#include "message-stack.h"
#include "selection.h"
#include "selection-chemistry.h"
#include "style.h"

#include "display/curve.h"
#include "display/control/canvas-item-bpath.h"

#include "live_effects/lpe-bendpath.h"
#include "live_effects/lpe-patternalongpath.h"
#include "live_effects/lpe-powerstroke.h"

#include "object/sp-item-group.h"
#include "object/sp-path.h"
#include "object/sp-rect.h"
#include "object/sp-use.h"

#include "svg/svg-color.h"
#include "svg/svg.h"

#include "ui/clipboard.h"
#include "ui/draw-anchor.h"
#include "ui/icon-names.h"
#include "ui/tools/lpe-tool.h" // TODO: Remove in the future
#include "ui/tools/pencil-tool.h" // TODO: Remove in the future
#include "ui/widget/events/canvas-event.h"

using Inkscape::DocumentUndo;

namespace Inkscape::UI::Tools {

/**
 * Flushes white curve(s) and additional curve into object.
 *
 * No cleaning of colored curves - this has to be done by caller
 * No rereading of white data, so if you cannot rely on ::modified, do it in caller
 */
static void spdc_flush_white(FreehandBase *dc, std::shared_ptr<SPCurve> gc);

static void spdc_free_colors(FreehandBase *dc);

FreehandBase::FreehandBase(SPDesktop *desktop, std::string &&prefs_path, std::string &&cursor_filename)
    : ToolBase(desktop, std::move(prefs_path), std::move(cursor_filename))
{
    selection = desktop->getSelection();

    // Connect signals to track selection changes
    sel_changed_connection = selection->connectChanged([=](Selection *) { _attachSelection(); });
    sel_modified_connection = selection->connectModified([=](Selection *, unsigned) { onSelectionModified(); });

    // Create red bpath
    red_bpath = make_canvasitem<CanvasItemBpath>(desktop->getCanvasSketch());
    red_bpath->set_stroke(red_color);
    red_bpath->set_fill(0x0, SP_WIND_RULE_NONZERO);

    // Create blue bpath
    blue_bpath = make_canvasitem<CanvasItemBpath>(desktop->getCanvasSketch());
    blue_bpath->set_stroke(blue_color);
    blue_bpath->set_fill(0x0, SP_WIND_RULE_NONZERO);

    // Create green curve
    green_curve = std::make_shared<SPCurve>();

    // Create start anchor alternative curve
    sa_overwrited = std::make_shared<SPCurve>();

    _attachSelection();
}

FreehandBase::~FreehandBase()
{
    sel_changed_connection.disconnect();
    sel_modified_connection.disconnect();

    ungrabCanvasEvents();

    if (selection) {
        selection = nullptr;
    }

    spdc_free_colors(this);
}

bool FreehandBase::root_handler(CanvasEvent const &event)
{
    bool ret = false;

    inspect_event(event,
        [&] (KeyPressEvent const &event) {
            switch (get_latin_keyval(event)) {
                case GDK_KEY_Up:
                case GDK_KEY_Down:
                case GDK_KEY_KP_Up:
                case GDK_KEY_KP_Down:
                    // prevent the zoom field from activation
                    if (!mod_ctrl_only(event)) {
                        ret = true;
                    }
                    break;
                default:
                    break;
            }
        },
        [&] (CanvasEvent const &event) {}
    );

    return ret || ToolBase::root_handler(event);
}

std::optional<Geom::Point> FreehandBase::red_curve_get_last_point() const
{
    if (!red_curve.is_empty()) {
        return red_curve.last_point();
    }
    return {};
}

static void spdc_paste_curve_as_freehand_shape(Geom::PathVector const &newpath, FreehandBase *dc, SPItem *item)
{
    using namespace Inkscape::LivePathEffect;

    // TODO: Don't paste path if nothing is on the clipboard
    SPDocument *document = dc->getDesktop()->doc();
    Effect::createAndApply(PATTERN_ALONG_PATH, document, item);
    Effect* lpe = cast<SPLPEItem>(item)->getCurrentLPE();
    static_cast<LPEPatternAlongPath*>(lpe)->pattern.set_new_value(newpath,true);
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    double scale = prefs->getDouble("/live_effects/skeletal/width", 1);
    if (!scale) {
        scale = 1;
    }
    Inkscape::SVGOStringStream os;
    os << scale;
    lpe->getRepr()->setAttribute("prop_scale", os.str());
}

void spdc_apply_style(SPObject *obj)
{
    SPCSSAttr *css = sp_repr_css_attr_new();
    if (obj->style) {
        if (obj->style->stroke.isPaintserver()) {
            SPPaintServer *server = obj->style->getStrokePaintServer();
            if (server) {
                Glib::ustring str;
                str += "url(#";
                str += server->getId();
                str += ")";
                sp_repr_css_set_property(css, "fill", str.c_str());
            }
        } else if (obj->style->stroke.isColor()) {
            gchar c[64];
            sp_svg_write_color(
                c, sizeof(c),
                obj->style->stroke.value.color.toRGBA32(SP_SCALE24_TO_FLOAT(obj->style->stroke_opacity.value)));
            sp_repr_css_set_property(css, "fill", c);
        } else {
            sp_repr_css_set_property(css, "fill", "none");
        }
    } else {
        sp_repr_css_unset_property(css, "fill");
    }

    sp_repr_css_set_property(css, "fill-rule", "nonzero");
    sp_repr_css_set_property(css, "stroke", "none");

    sp_desktop_apply_css_recursive(obj, css, true);
    sp_repr_css_attr_unref(css);
}

static void spdc_apply_powerstroke_shape(std::vector<Geom::Point> const &points, FreehandBase *dc, SPItem *item)
{
    using namespace Inkscape::LivePathEffect;
    SPDesktop *desktop = dc->getDesktop();
    SPDocument *document = desktop->getDocument();
    if (!document || !desktop) {
        return;
    }
    if (SP_IS_PENCIL_CONTEXT(dc)) {
        if (dc->tablet_enabled) {
            SPObject *elemref = nullptr;
            if ((elemref = document->getObjectById("power_stroke_preview"))) {
                elemref->getRepr()->removeAttribute("style");
                auto successor = cast<SPItem>(elemref);
                sp_desktop_apply_style_tool(desktop, successor->getRepr(),
                                            Glib::ustring("/tools/freehand/pencil").data(), false);
                spdc_apply_style(successor);
                sp_object_ref(item);
                item->deleteObject(false);
                item->setSuccessor(successor);
                sp_object_unref(item);
                item = successor;
                dc->selection->set(item);
                item->setLocked(false);
                dc->white_item = item;
                rename_id(item, "path-1");
            }
            return;
        }
    }
    Effect::createAndApply(POWERSTROKE, document, item);
    Effect* lpe = cast<SPLPEItem>(item)->getCurrentLPE();

    static_cast<LPEPowerStroke*>(lpe)->offset_points.param_set_and_write_new_value(points);

    // write powerstroke parameters:
    lpe->getRepr()->setAttribute("start_linecap_type", "zerowidth");
    lpe->getRepr()->setAttribute("end_linecap_type", "zerowidth");
    lpe->getRepr()->setAttribute("sort_points", "true");
    lpe->getRepr()->setAttribute("not_jump", "false");
    lpe->getRepr()->setAttribute("interpolator_type", "CubicBezierJohan");
    lpe->getRepr()->setAttribute("interpolator_beta", "0.2");
    lpe->getRepr()->setAttribute("miter_limit", "4");
    lpe->getRepr()->setAttribute("scale_width", "1");
    lpe->getRepr()->setAttribute("linejoin_type", "extrp_arc");
}

static void spdc_apply_bend_shape(gchar const *svgd, FreehandBase *dc, SPItem *item)
{
    using namespace Inkscape::LivePathEffect;
    if (is<SPUse>(item)) {
        return;
    }
    SPDesktop *desktop = dc->getDesktop();
    SPDocument *document = desktop->getDocument();
    if (!document || !desktop) {
        return;
    }
    auto lpeitem = cast<SPLPEItem>(item);
    if (!lpeitem) {
        return;
    }
    if (!lpeitem->hasPathEffectOfType(BEND_PATH)){
        Effect::createAndApply(BEND_PATH, document, item);
    }
    auto lpe = lpeitem->getCurrentLPE();

    // write bend parameters:
    auto prefs = Preferences::get();
    double scale = prefs->getDouble("/live_effects/bend_path/width", 1);
    if (!scale) {
        scale = 1;
    }
    Inkscape::SVGOStringStream os;
    os << scale;
    lpe->getRepr()->setAttribute("prop_scale", os.str());
    lpe->getRepr()->setAttribute("scale_y_rel", "false");
    lpe->getRepr()->setAttribute("vertical", "false");
    static_cast<LPEBendPath*>(lpe)->bend_path.paste_param_path(svgd);
}

static void spdc_apply_simplify(double threshold, FreehandBase *dc, SPItem *item)
{
    const SPDesktop *desktop = dc->getDesktop();
    SPDocument *document = desktop->getDocument();
    if (!document || !desktop) {
        return;
    }
    using namespace Inkscape::LivePathEffect;

    Effect::createAndApply(SIMPLIFY, document, item);
    Effect* lpe = cast<SPLPEItem>(item)->getCurrentLPE();
    // write simplify parameters:
    lpe->getRepr()->setAttribute("steps", "1");
    lpe->getRepr()->setAttributeOrRemoveIfEmpty("threshold", std::to_string(threshold).c_str());
    lpe->getRepr()->setAttribute("smooth_angles", "360");
    lpe->getRepr()->setAttribute("helper_size", "0");
    lpe->getRepr()->setAttribute("simplify_individual_paths", "false");
    lpe->getRepr()->setAttribute("simplify_just_coalesce", "false");
}

static ShapeType previous_shape_type = NONE;

static void spdc_check_for_and_apply_waiting_LPE(FreehandBase *dc, SPItem *item, SPCurve const *curve, bool is_bend)
{
    using namespace Inkscape::LivePathEffect;
    auto prefs = Preferences::get();

    auto desktop = dc->getDesktop();

    if (is<SPLPEItem>(item)) {
        double const defsize = 10 / (0.265 * dc->getDesktop()->getDocument()->getDocumentScale()[0]);
        auto const SHAPE_LENGTH = defsize;
        auto const SHAPE_HEIGHT = defsize;
        //Store the clipboard path to apply in the future without the use of clipboard
        static Geom::PathVector previous_shape_pathv;
        static SPItem *bend_item = nullptr;
        auto shape = static_cast<ShapeType>(prefs->getInt(dc->getPrefsPath() + "/shape", 0));
        if (previous_shape_type == NONE) {
            previous_shape_type = shape;
        }
        if(shape == LAST_APPLIED){
            shape = previous_shape_type;
            if(shape == CLIPBOARD || shape == BEND_CLIPBOARD){
                shape = LAST_APPLIED;
            }
        }
        Inkscape::UI::ClipboardManager *cm = Inkscape::UI::ClipboardManager::get();
        if (is_bend && 
           (shape == BEND_CLIPBOARD || (shape == LAST_APPLIED && previous_shape_type != CLIPBOARD)) && 
            cm->paste(desktop, true))
        {
            bend_item = dc->selection->singleItem();
            if(!bend_item || (!is<SPShape>(bend_item) && !is<SPGroup>(bend_item))){
                previous_shape_type = NONE;
                return;
            }
        } else if(is_bend) {
            return;
        }
        if (!is_bend && previous_shape_type == BEND_CLIPBOARD && shape == BEND_CLIPBOARD) {
            return;
        }
        bool shape_applied = false;
        bool simplify = prefs->getInt(dc->getPrefsPath() + "/simplify", 0);
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        guint mode = prefs->getInt("/tools/freehand/pencil/freehand-mode", 0);
        if(simplify && mode != 2){
            double tol = prefs->getDoubleLimited("/tools/freehand/pencil/tolerance", 10.0, 1.0, 100.0);
            tol = tol/(100.0*(102.0-tol));
            tol *= 10000;
            spdc_apply_simplify(tol, dc, item);
            sp_lpe_item_update_patheffect(cast<SPLPEItem>(item), true, false);
        }
        if (prefs->getInt(dc->getPrefsPath() + "/freehand-mode", 0) == 1) {
            Effect::createAndApply(SPIRO, dc->getDesktop()->getDocument(), item);
        }

        if (prefs->getInt(dc->getPrefsPath() + "/freehand-mode", 0) == 2) {
            Effect::createAndApply(BSPLINE, dc->getDesktop()->getDocument(), item);
        }
        if (auto sp_shape = cast<SPShape>(item)) {
            curve = sp_shape->curve();
        }
        SPCSSAttr *css_item = sp_css_attr_from_object(item, SP_STYLE_FLAG_ALWAYS);
        const char *cstroke = sp_repr_css_property(css_item, "stroke", "none");
        const char *cfill = sp_repr_css_property(css_item, "fill", "none");
        const char *stroke_width = sp_repr_css_property(css_item, "stroke-width", "0");
        double swidth;
        sp_svg_number_read_d(stroke_width, &swidth);
        swidth = prefs->getDouble("/live_effects/powerstroke/width", SHAPE_HEIGHT / 2);
        if (!swidth) {
            swidth = swidth/2;
        }
        swidth = std::abs(swidth);
        guint curve_length = curve->get_segment_count();
        if (SP_IS_PENCIL_CONTEXT(dc)) {
            if (dc->tablet_enabled) {
                spdc_apply_powerstroke_shape({}, dc, item);
                shape_applied = true;
                shape = NONE;
                previous_shape_type = NONE;
            }
        }

        switch (shape) {
            case NONE:
                // don't apply any shape
                break;
            case TRIANGLE_IN:
                // "triangle in"
                spdc_apply_powerstroke_shape({{0, swidth}}, dc, item);
                shape_applied = false;
                break;
            case TRIANGLE_OUT:
                // "triangle out"
                spdc_apply_powerstroke_shape({{(double)curve_length, swidth}}, dc, item);
                shape_applied = false;
                break;
            case ELLIPSE:
            {
                // "ellipse"
                SPCurve c;
                constexpr double C1 = 0.552;
                c.moveto(0, SHAPE_HEIGHT/2);
                c.curveto(0, (1 - C1) * SHAPE_HEIGHT/2, (1 - C1) * SHAPE_LENGTH/2, 0, SHAPE_LENGTH/2, 0);
                c.curveto((1 + C1) * SHAPE_LENGTH/2, 0, SHAPE_LENGTH, (1 - C1) * SHAPE_HEIGHT/2, SHAPE_LENGTH, SHAPE_HEIGHT/2);
                c.curveto(SHAPE_LENGTH, (1 + C1) * SHAPE_HEIGHT/2, (1 + C1) * SHAPE_LENGTH/2, SHAPE_HEIGHT, SHAPE_LENGTH/2, SHAPE_HEIGHT);
                c.curveto((1 - C1) * SHAPE_LENGTH/2, SHAPE_HEIGHT, 0, (1 + C1) * SHAPE_HEIGHT/2, 0, SHAPE_HEIGHT/2);
                c.closepath();
                spdc_paste_curve_as_freehand_shape(c.get_pathvector(), dc, item);

                shape_applied = true;
                break;
            }
            case CLIPBOARD:
            {
                // take shape from clipboard;
                Inkscape::UI::ClipboardManager *cm = Inkscape::UI::ClipboardManager::get();
                if(cm->paste(desktop,true)){
                    dc->selection->toCurves(true);
                    if (auto pasted_clipboard = dc->selection->singleItem()){
                        Inkscape::XML::Node *pasted_clipboard_root = pasted_clipboard->getRepr();
                        Inkscape::XML::Node *path = sp_repr_lookup_name(pasted_clipboard_root, "svg:path", -1); // unlimited search depth
                        if ( path != nullptr ) {
                            gchar const *svgd = path->attribute("d");
                            dc->selection->remove(pasted_clipboard);
                            previous_shape_pathv =  sp_svg_read_pathv(svgd);
                            previous_shape_pathv *= pasted_clipboard->transform;
                            spdc_paste_curve_as_freehand_shape(previous_shape_pathv, dc, item);

                            shape = CLIPBOARD;
                            shape_applied = true;
                            pasted_clipboard->deleteObject();
                        } else {
                            shape = NONE;
                        }
                    } else {
                        shape = NONE;
                    }
                } else {
                    shape = NONE;
                }
                break;
            }
            case BEND_CLIPBOARD:
            {
                gchar const *svgd = item->getRepr()->attribute("d");
                if(bend_item && (is<SPShape>(bend_item) || is<SPGroup>(bend_item))){
                    // If item is a SPRect, convert it to path first:
                    if (is<SPRect>(bend_item) ) {
                        if (desktop) {
                            Inkscape::Selection *sel = desktop->getSelection();
                            if ( sel && !sel->isEmpty() ) {
                                sel->clear();
                                sel->add(bend_item);
                                sel->toCurves();
                                bend_item = sel->singleItem();
                            }
                        }
                    }
                    bend_item->moveTo(item,false);
                    bend_item->transform.setTranslation(Geom::Point());
                    spdc_apply_bend_shape(svgd, dc, bend_item);
                    dc->selection->add(bend_item);

                    shape = BEND_CLIPBOARD;
                } else {
                    bend_item = nullptr;
                    shape = NONE;
                }
                break;
            }
            case LAST_APPLIED:
            {
                if(previous_shape_type == CLIPBOARD){
                    if(previous_shape_pathv.size() != 0){
                        spdc_paste_curve_as_freehand_shape(previous_shape_pathv, dc, item);
                        shape_applied = true;
                        shape = CLIPBOARD;
                    } else{
                        shape = NONE;
                    }
                } else {
                    if(bend_item != nullptr && bend_item->getRepr() != nullptr){
                        gchar const *svgd = item->getRepr()->attribute("d");
                        dc->selection->add(bend_item);
                        dc->selection->duplicate();
                        dc->selection->remove(bend_item);
                        bend_item = dc->selection->singleItem();
                        if(bend_item){
                            bend_item->moveTo(item,false);
                            Geom::Coord expansion_X = bend_item->transform.expansionX();
                            Geom::Coord expansion_Y = bend_item->transform.expansionY();
                            bend_item->transform = Geom::Affine(1,0,0,1,0,0);
                            bend_item->transform.setExpansionX(expansion_X);
                            bend_item->transform.setExpansionY(expansion_Y);
                            spdc_apply_bend_shape(svgd, dc, bend_item);
                            dc->selection->add(bend_item);

                            shape = BEND_CLIPBOARD;
                        } else {
                            shape = NONE;
                        }
                    } else {
                        shape = NONE;
                    }
                }
                break;
            }
            default:
                break;
        }
        previous_shape_type = shape;

        if (shape_applied) {
            // apply original stroke color as fill and unset stroke; then return
            SPCSSAttr *css = sp_repr_css_attr_new();
            if (!strcmp(cfill, "none")) {
                sp_repr_css_set_property (css, "fill", cstroke);
            } else {
                sp_repr_css_set_property (css, "fill", cfill);
            }
            sp_repr_css_set_property (css, "stroke", "none");
            sp_desktop_apply_css_recursive(dc->white_item, css, true);
            sp_repr_css_attr_unref(css);
            return;
        }
        if (dc->waiting_LPE_type != INVALID_LPE) {
            Effect::createAndApply(dc->waiting_LPE_type, dc->getDesktop()->getDocument(), item);
            dc->waiting_LPE_type = INVALID_LPE;

            if (auto lc = SP_LPETOOL_CONTEXT(dc)) {
                // since a geometric LPE was applied, we switch back to "inactive" mode
                lc->switch_mode(INVALID_LPE);
            }
        }
        if (SP_IS_PEN_CONTEXT(dc)) {
            SP_PEN_CONTEXT(dc)->setPolylineMode();
        }
    }
}

/*
 * Selection handlers
 */

/* fixme: We have to ensure this is not delayed (Lauris) */
void FreehandBase::onSelectionModified()
{
    _attachSelection();
}

void FreehandBase::_attachSelection()
{
    // We reset white and forget white/start/end anchors
    white_curves.clear();
    white_anchors.clear();
    white_item = nullptr;
    sa = nullptr;
    ea = nullptr;

    SPItem *item = selection ? selection->singleItem() : nullptr;

    if (is<SPPath>(item)) {
        // Create new white data
        // Item
        white_item = item;

        // Curve list
        // We keep it in desktop coordinates to eliminate calculation errors
        auto path = static_cast<SPPath *>(item);
        if (!path->curveForEdit()) {
            return;
        }

        auto tmp = path->curveForEdit()->transformed(white_item->i2dt_affine()).split();
        white_curves.clear();
        white_curves.reserve(tmp.size());
        for (auto &t : tmp) {
            white_curves.emplace_back(std::make_shared<SPCurve>(std::move(t)));
        }

        // Anchor list
        for (auto const &c : white_curves) {
            g_return_if_fail( c->get_segment_count() > 0 );
            if ( !c->is_closed() ) {
                white_anchors.emplace_back(std::make_unique<SPDrawAnchor>(this, c, true , *c->first_point()));
                white_anchors.emplace_back(std::make_unique<SPDrawAnchor>(this, c, false, *c->last_point()));
            }
        }
        // fixme: recalculate active anchor?
    }
}

void spdc_endpoint_snap_rotation(ToolBase *tool, Geom::Point &p, Geom::Point const &o, unsigned state)
{
    auto prefs = Preferences::get();
    unsigned const snaps = abs(prefs->getInt("/options/rotationsnapsperpi/value", 12));

    SnapManager &m = tool->getDesktop()->getNamedView()->snap_manager;
    m.setup(tool->getDesktop());

    bool snap_enabled = m.snapprefs.getSnapEnabledGlobally();
    if (state & GDK_SHIFT_MASK) {
        // SHIFT disables all snapping, except the angular snapping. After all, the user explicitly asked for angular
        // snapping by pressing CTRL, otherwise we wouldn't have arrived here. But although we temporarily disable
        // the snapping here, we must still call for a constrained snap in order to apply the constraints (i.e. round
        // to the nearest angle increment)
        m.snapprefs.setSnapEnabledGlobally(false);
    }

    Inkscape::SnappedPoint dummy = m.constrainedAngularSnap(Inkscape::SnapCandidatePoint(p, Inkscape::SNAPSOURCE_NODE_HANDLE), std::optional<Geom::Point>(), o, snaps);
    p = dummy.getPoint();

    if (state & GDK_SHIFT_MASK) {
        m.snapprefs.setSnapEnabledGlobally(snap_enabled); // restore the original setting
    }

    m.unSetup();
}

void spdc_endpoint_snap_free(ToolBase *tool, Geom::Point &p, std::optional<Geom::Point> &start_of_line)
{
    const SPDesktop *dt = tool->getDesktop();
    SnapManager &m = dt->getNamedView()->snap_manager;
    Inkscape::Selection *selection = dt->getSelection();

    // selection->singleItem() is the item that is currently being drawn. This item will not be snapped to (to avoid self-snapping)
    // TODO: Allow snapping to the stationary parts of the item, and only ignore the last segment

    m.setup(dt, true, selection->singleItem());
    Inkscape::SnapCandidatePoint scp(p, Inkscape::SNAPSOURCE_NODE_HANDLE);
    if (start_of_line) {
        scp.addOrigin(*start_of_line);
    }

    Inkscape::SnappedPoint sp = m.freeSnap(scp);
    p = sp.getPoint();

    m.unSetup();
}

void spdc_concat_colors_and_flush(FreehandBase *dc, bool forceclosed)
{
    // Concat RBG
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    // Green
    auto c = std::make_shared<SPCurve>();
    std::swap(c, dc->green_curve);
    dc->green_bpaths.clear();

    // Blue
    c->append_continuous(std::move(dc->blue_curve));
    dc->blue_curve.reset();
    dc->blue_bpath->set_bpath(nullptr);

    // Red
    if (dc->red_curve_is_valid) {
        c->append_continuous(dc->red_curve);
    }
    dc->red_curve.reset();
    dc->red_bpath->set_bpath(nullptr);

    if (c->is_empty()) {
        return;
    }

    // Step A - test, whether we ended on green anchor
    if ( (forceclosed && 
         (!dc->sa || (dc->sa && dc->sa->curve->is_empty()))) || 
         ( dc->green_anchor && dc->green_anchor->active)) 
    {
        // We hit green anchor, closing Green-Blue-Red
        dc->getDesktop()->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("Path is closed."));
        c->closepath_current();
        // Closed path, just flush
        spdc_flush_white(dc, std::move(c));
        return;
    }

    // Step B - both start and end anchored to same curve
    if ( dc->sa && dc->ea
         && ( dc->sa->curve == dc->ea->curve )
         && ( ( dc->sa != dc->ea )
              || dc->sa->curve->is_closed() ) )
    {
        // We hit bot start and end of single curve, closing paths
        dc->getDesktop()->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("Closing path."));
        dc->sa_overwrited->append_continuous(*c);
        dc->sa_overwrited->closepath_current();
        if (!dc->white_curves.empty()) {
            dc->white_curves.erase(std::find(dc->white_curves.begin(),dc->white_curves.end(), dc->sa->curve));
        }
        dc->white_curves.push_back(std::move(dc->sa_overwrited));
        spdc_flush_white(dc, nullptr);
        return;
    }
    // Step C - test start
    if (dc->sa) {
        if (!dc->white_curves.empty()) {
            dc->white_curves.erase(std::find(dc->white_curves.begin(),dc->white_curves.end(), dc->sa->curve));
        }
        dc->sa_overwrited->append_continuous(*c);
        c = std::move(dc->sa_overwrited);
    } else /* Step D - test end */ if (dc->ea) {
        auto e = std::move(dc->ea->curve);
        if (!dc->white_curves.empty()) {
            dc->white_curves.erase(std::find(dc->white_curves.begin(),dc->white_curves.end(), e));
        }
        if (!dc->ea->start) {
            e = std::make_shared<SPCurve>(e->reversed());
        }
        if(prefs->getInt(dc->getPrefsPath() + "/freehand-mode", 0) == 1 ||
            prefs->getInt(dc->getPrefsPath() + "/freehand-mode", 0) == 2)
        {
                e = std::make_shared<SPCurve>(e->reversed());
                Geom::CubicBezier const * cubic = dynamic_cast<Geom::CubicBezier const*>(&*e->last_segment());
                if(cubic){
                    auto lastSeg = std::make_shared<SPCurve>();
                    lastSeg->moveto((*cubic)[0]);
                    lastSeg->curveto((*cubic)[1],(*cubic)[3],(*cubic)[3]);
                    if ( e->get_segment_count() == 1) {
                        e = std::move(lastSeg);
                    } else {
                        //we eliminate the last segment
                        e->backspace();
                        //and we add it again with the recreation
                        e->append_continuous(*lastSeg);
                    }
                }
                e = std::make_shared<SPCurve>(e->reversed());
        }
        c->append_continuous(*e);
    }
    if (forceclosed) 
    {
        dc->getDesktop()->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("Path is closed."));
        c->closepath_current();
    }
    spdc_flush_white(dc, std::move(c));
}

static void spdc_flush_white(FreehandBase *dc, std::shared_ptr<SPCurve> gc)
{
    std::shared_ptr<SPCurve> c;

    if (!dc->white_curves.empty()) {
        g_assert(dc->white_item);

        c = std::make_shared<SPCurve>();
        for (auto const &wc : dc->white_curves) {
            c->append(*wc);
        }

        dc->white_curves.clear();
        if (gc) {
            c->append(*gc);
        }
    } else if (gc) {
        c = std::move(gc);
    } else {
        return;
    }

    SPDesktop *desktop = dc->getDesktop();
    SPDocument *doc = desktop->getDocument();
    Inkscape::XML::Document *xml_doc = doc->getReprDoc();

    // Now we have to go back to item coordinates at last
    c->transform( dc->white_item
               ? (dc->white_item)->dt2i_affine()
               :  desktop->dt2doc() );

    if (!c->is_empty()) {
        // We actually have something to write

        bool has_lpe = false;
        Inkscape::XML::Node *repr;

        if (dc->white_item) {
            repr = dc->white_item->getRepr();
            has_lpe = cast<SPLPEItem>(dc->white_item)->hasPathEffectRecursive();
        } else {
            repr = xml_doc->createElement("svg:path");
            // Set style
            sp_desktop_apply_style_tool(desktop, repr, dc->getPrefsPath(), false);
        }

        auto str = sp_svg_write_path(c->get_pathvector());
        if (has_lpe)
            repr->setAttribute("inkscape:original-d", str);
        else
            repr->setAttribute("d", str);

        auto layer = dc->currentLayer();
        if (SP_IS_PENCIL_CONTEXT(dc) && dc->tablet_enabled) {
            if (!dc->white_item) {
                dc->white_item = cast<SPItem>(layer->appendChildRepr(repr));
            }
            spdc_check_for_and_apply_waiting_LPE(dc, dc->white_item, c.get(), false);
        }
        if (!dc->white_item) {
            // Attach repr
            auto item = cast<SPItem>(layer->appendChildRepr(repr));
            dc->white_item = item;
            //Bend needs the transforms applied after, Other effects best before
            spdc_check_for_and_apply_waiting_LPE(dc, item, c.get(), true);
            Inkscape::GC::release(repr);
            item->transform = layer->i2doc_affine().inverse();
            item->updateRepr();
            item->doWriteTransform(item->transform, nullptr, true);
            spdc_check_for_and_apply_waiting_LPE(dc, item, c.get(), false);
            if(previous_shape_type == BEND_CLIPBOARD){
                repr->parent()->removeChild(repr);
                dc->white_item = nullptr;
            } else {
                dc->selection->set(repr);
            }
        }
        auto lpeitem = cast<SPLPEItem>(dc->white_item);
        if (lpeitem && lpeitem->hasPathEffectRecursive()) {
            sp_lpe_item_update_patheffect(lpeitem, true, false);
        }
        DocumentUndo::done(doc, _("Draw path"), SP_IS_PEN_CONTEXT(dc)? INKSCAPE_ICON("draw-path") : INKSCAPE_ICON("draw-freehand"));

        // When quickly drawing several subpaths with Shift, the next subpath may be finished and
        // flushed before the selection_modified signal is fired by the previous change, which
        // results in the tool losing all of the selected path's curve except that last subpath. To
        // fix this, we force the selection_modified callback now, to make sure the tool's curve is
        // in sync immediately.
        dc->onSelectionModified();
    }

    // Flush pending updates
    doc->ensureUpToDate();
}

SPDrawAnchor *spdc_test_inside(FreehandBase *dc, Geom::Point const &p)
{
    SPDrawAnchor *active = nullptr;

    // Test green anchor
    if (dc->green_anchor) {
        active = dc->green_anchor->anchorTest(p, true);
    }

    for (auto &i : dc->white_anchors) {
        auto na = i->anchorTest(p, !active);
        if (!active && na) {
            active = na;
        }
    }

    return active;
}

static void spdc_free_colors(FreehandBase *dc)
{
    // Red
    dc->red_bpath.reset();

    // Blue
    dc->blue_bpath.reset();
    dc->blue_curve.reset();

    // Overwrite start anchor curve
    dc->sa_overwrited.reset();
    // Green
    dc->green_bpaths.clear();
    dc->green_curve.reset();
    dc->green_anchor.reset();

    // White
    if (dc->white_item) {
        // We do not hold refcount
        dc->white_item = nullptr;
    }
    dc->white_curves.clear();
    dc->white_anchors.clear();
}

void spdc_create_single_dot(ToolBase *tool, Geom::Point const &pt, char const *path, unsigned event_state)
{
    g_return_if_fail(!strcmp(path, "/tools/freehand/pen") || !strcmp(path, "/tools/freehand/pencil")
            || !strcmp(path, "/tools/calligraphic") );
    Glib::ustring tool_path = path;

    SPDesktop *desktop = tool->getDesktop();
    Inkscape::XML::Document *xml_doc = desktop->doc()->getReprDoc();
    Inkscape::XML::Node *repr = xml_doc->createElement("svg:path");
    repr->setAttribute("sodipodi:type", "arc");
    auto layer = tool->currentLayer();
    auto item = cast<SPItem>(layer->appendChildRepr(repr));
    item->transform = layer->i2doc_affine().inverse();
    Inkscape::GC::release(repr);

    // apply the tool's current style
    sp_desktop_apply_style_tool(desktop, repr, path, false);

    // find out stroke width (TODO: is there an easier way??)
    double stroke_width = 3.0;
    gchar const *style_str = repr->attribute("style");
    if (style_str) {
        SPStyle style(desktop->doc());
        style.mergeString(style_str);
        stroke_width = style.stroke_width.computed;
    }

    // unset stroke and set fill color to former stroke color
    auto str = strcmp(path, "/tools/calligraphic")
             ? g_strdup_printf("fill:#%06x;stroke:none;", sp_desktop_get_color_tool(desktop, path, false) >> 8)
             : g_strdup_printf("fill:#%06x;stroke:#%06x;", sp_desktop_get_color_tool(desktop, path, true) >> 8, sp_desktop_get_color_tool(desktop, path, false) >> 8);
    repr->setAttribute("style", str);
    g_free(str);

    // put the circle where the mouse click occurred and set the diameter to the
    // current stroke width, multiplied by the amount specified in the preferences
    auto prefs = Preferences::get();

    Geom::Affine const i2d (item->i2dt_affine ());
    Geom::Point pp = pt * i2d.inverse();

    double rad = 0.5 * prefs->getDouble(tool_path + "/dot-size", 3.0);
    if (!strcmp(path, "/tools/calligraphic"))
        rad = 0.0333 * prefs->getDouble(tool_path + "/width", 3.0) / desktop->current_zoom() / desktop->getDocument()->getDocumentScale()[Geom::X];
    if (event_state & GDK_ALT_MASK) {
        // TODO: We vary the dot size between 0.5*rad and 1.5*rad, where rad is the dot size
        // as specified in prefs. Very simple, but it might be sufficient in practice. If not,
        // we need to devise something more sophisticated.
        double s = g_random_double_range(-0.5, 0.5);
        rad *= (1 + s);
    }
    if (event_state & GDK_SHIFT_MASK) {
        // double the point size
        rad *= 2;
    }

    repr->setAttributeSvgDouble("sodipodi:cx", pp[Geom::X]);
    repr->setAttributeSvgDouble("sodipodi:cy", pp[Geom::Y]);
    repr->setAttributeSvgDouble("sodipodi:rx", rad * stroke_width);
    repr->setAttributeSvgDouble("sodipodi:ry", rad * stroke_width);
    item->updateRepr();
    item->doWriteTransform(item->transform, nullptr, true);

    desktop->getSelection()->set(item);

    desktop->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("Creating single dot"));
    DocumentUndo::done(desktop->getDocument(), _("Create single dot"), "");
}

} // namespace Inkscape::UI::Tools

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
