// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Spray Tool
 *
 * Authors:
 *   Pierre-Antoine MARC
 *   Pierre CACLIN
 *   Aurel-Aimé MARMION
 *   Julien LERAY
 *   Benoît LAVORATA
 *   Vincent MONTAGNE
 *   Pierre BARBRY-BLOT
 *   Steren GIANNINI (steren.giannini@gmail.com)
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *   Jabiertxo Arraiza <jabier.arraiza@marker.es>
 *   Adrian Boguszewski
 *
 * Copyright (C) 2009 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "spray-tool.h"

#include <vector>

#include <gdk/gdkkeysyms.h>
#include <glibmm/i18n.h>

#include <2geom/circle.h>

#include "context-fns.h"
#include "desktop-style.h"
#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "message-context.h"
#include "selection.h"

#include "display/curve.h"
#include "display/drawing.h"
#include "display/control/canvas-item-bpath.h"
#include "display/control/canvas-item-drawing.h"

#include "object/box3d.h"
#include "object/sp-shape.h"
#include "object/sp-use.h"

#include "svg/svg-color.h"

#include "ui/icon-names.h"
#include "ui/toolbar/spray-toolbar.h"
#include "ui/widget/events/canvas-event.h"

using Inkscape::DocumentUndo;

#define DDC_RED_RGBA 0xff0000ff
#define DYNA_MIN_WIDTH 1.0e-6

// Disabled in 0.91 because of Bug #1274831 (crash, spraying an object
// with the mode: spray object in single path)
// Please enable again when working on 1.0
#define ENABLE_SPRAY_MODE_SINGLE_PATH

namespace Inkscape::UI::Tools {

enum {
    PICK_COLOR,
    PICK_OPACITY,
    PICK_R,
    PICK_G,
    PICK_B,
    PICK_H,
    PICK_S,
    PICK_L
};

/**
 * This function returns pseudo-random numbers from a normal distribution
 * @param mu : mean
 * @param sigma : standard deviation ( > 0 )
 */
inline double NormalDistribution(double mu, double sigma)
{
  // use Box Muller's algorithm
  return mu + sigma * sqrt( -2.0 * log(g_random_double_range(0, 1)) ) * cos( 2.0*M_PI*g_random_double_range(0, 1) );
}

/* Method to rotate items */
static void sp_spray_rotate_rel(Geom::Point c, Geom::Point &d, SPDesktop */*desktop*/, SPItem *item, Geom::Rotate const &rotation)
{
    Geom::Translate const s(c);
    Geom::Affine affine = s.inverse() * rotation * s;
    d *= s.inverse() * rotation * s;
    // Rotate item.
    item->set_i2d_affine(item->i2dt_affine() * affine);
    // Use each item's own transform writer, consistent with sp_selection_apply_affine()
    item->doWriteTransform(item->transform);
    // Restore the center position (it's changed because the bbox center changed)
    if (item->isCenterSet()) {
        item->setCenter(c);
        item->updateRepr();
    }
}

/* Method to scale items */
static void sp_spray_scale_rel(Geom::Point c, Geom::Point &d, SPDesktop */*desktop*/, SPItem *item, Geom::Scale const &scale)
{
    Geom::Translate const s(c);
    d *= s.inverse() * scale * s;
    item->set_i2d_affine(item->i2dt_affine() * s.inverse() * scale * s);
    item->doWriteTransform(item->transform);
}

static void get_paths(SPItem * item, Geom::PathVector &res, bool root = true) {
    if (auto bbox = item->documentVisualBounds()) {
        auto clone = cast<SPUse>(item);
        if (auto grp = cast<SPGroup>(item)) {
            for (auto ig : grp->item_list()) {
                get_paths(cast<SPItem>(ig), res, false);
            }
        } else if (auto shape = cast<SPShape>(item)) {
            Geom::Affine trans = item->i2doc_affine();
            for (auto path : shape->curve()->get_pathvector()) {
                path *= trans;
                res.insert(res.end(), path);
            }
        } else if (clone && !root) {
            get_paths(clone->trueOriginal(), res, false);
        }
        if (root) {
            if (clone) {
                get_paths(clone->trueOriginal(), res, false);
                res *= clone->trueOriginal()->transform.inverse();
                res *= clone->get_root_transform();
                bbox = res.boundsFast();
            }
            if (bbox) {
                res *= Geom::Translate(bbox->midpoint()).inverse();
            }
        }
    }
}

SprayTool::SprayTool(SPDesktop *desktop)
    : ToolBase(desktop, "/tools/spray", "spray.svg", false)
    , pressure(TC_DEFAULT_PRESSURE)
{
    dilate_area = make_canvasitem<CanvasItemBpath>(desktop->getCanvasControls());
    dilate_area->set_stroke(0xff9900ff);
    dilate_area->set_fill(0x0, SP_WIND_RULE_EVENODD);
    dilate_area->set_visible(false);

    shapes_area = make_canvasitem<CanvasItemBpath>(desktop->getCanvasControls());
    shapes_area->set_stroke(0x333333ff);
    shapes_area->set_fill(0x0, SP_WIND_RULE_EVENODD);
    shapes_area->set_visible(false);

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setBool("/dialogs/clonetiler/dotrace", false);
    if (prefs->getBool("/tools/spray/selcue")) {
        this->enableSelectionCue();
    }
    if (prefs->getBool("/tools/spray/gradientdrag")) {
        this->enableGrDrag();
    }
    desktop->getSelection()->setBackup();
    sp_event_context_read(this, "distrib");
    sp_event_context_read(this, "width");
    sp_event_context_read(this, "ratio");
    sp_event_context_read(this, "tilt");
    sp_event_context_read(this, "rotation_variation");
    sp_event_context_read(this, "scale_variation");
    sp_event_context_read(this, "mode");
    sp_event_context_read(this, "population");
    sp_event_context_read(this, "mean");
    sp_event_context_read(this, "standard_deviation");
    sp_event_context_read(this, "usepressurewidth");
    sp_event_context_read(this, "usepressurepopulation");
    sp_event_context_read(this, "usepressurescale");
    sp_event_context_read(this, "Scale");
    sp_event_context_read(this, "offset");
    sp_event_context_read(this, "picker");
    sp_event_context_read(this, "pick_center");
    sp_event_context_read(this, "pick_inverse_value");
    sp_event_context_read(this, "pick_fill");
    sp_event_context_read(this, "pick_stroke");
    sp_event_context_read(this, "pick_no_overlap");
    sp_event_context_read(this, "over_no_transparent");
    sp_event_context_read(this, "over_transparent");
    sp_event_context_read(this, "no_overlap");
}

SprayTool::~SprayTool() {
    if (!object_set.isEmpty()) {
        object_set.clear();
    }
    _desktop->getSelection()->restoreBackup();
    this->enableGrDrag(false);
}

void SprayTool::update_cursor(bool /*with_shift*/) {
    guint num = 0;
    gchar *sel_message = nullptr;

    if (!_desktop->getSelection()->isEmpty()) {
        num = (guint)boost::distance(_desktop->getSelection()->items());
        sel_message = g_strdup_printf(ngettext("<b>%i</b> object selected","<b>%i</b> objects selected",num), num);
    } else {
        sel_message = g_strdup_printf("%s", _("<b>Nothing</b> selected"));
    }

    switch (this->mode) {
        case SPRAY_MODE_COPY:
            this->message_context->setF(Inkscape::NORMAL_MESSAGE, _("%s. Drag, click or click and scroll to spray <b>copies</b> of the initial selection. Right click + Move to update single click item."), sel_message);
            break;
        case SPRAY_MODE_CLONE:
            this->message_context->setF(Inkscape::NORMAL_MESSAGE, _("%s. Drag, click or click and scroll to spray <b>clones</b> of the initial selection. Right click + Move to update single click item."), sel_message);
            break;
        case SPRAY_MODE_SINGLE_PATH:
            this->message_context->setF(Inkscape::NORMAL_MESSAGE, _("%s. Drag, click or click and scroll to spray in a <b>single path</b> of the initial selection. Right click + Move to update single click item.."), sel_message);
            break;
        default:
            break;
    }
    g_free(sel_message);
}


void SprayTool::setCloneTilerPrefs() {
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    do_trace = prefs->getBool("/dialogs/clonetiler/dotrace", false);
    pick = prefs->getInt("/dialogs/clonetiler/pick");
    pick_to_size = prefs->getBool("/dialogs/clonetiler/pick_to_size", false);
    pick_to_presence = prefs->getBool("/dialogs/clonetiler/pick_to_presence", false);
    pick_to_color = prefs->getBool("/dialogs/clonetiler/pick_to_color", false);
    pick_to_opacity = prefs->getBool("/dialogs/clonetiler/pick_to_opacity", false);
    rand_picked = 0.01 * prefs->getDoubleLimited("/dialogs/clonetiler/rand_picked", 0, 0, 100);
    invert_picked = prefs->getBool("/dialogs/clonetiler/invert_picked", false);
    gamma_picked = prefs->getDoubleLimited("/dialogs/clonetiler/gamma_picked", 0, -10, 10);
}

void SprayTool::set(const Inkscape::Preferences::Entry& val) {
    Glib::ustring path = val.getEntryName();

    if (path == "mode") {
        this->mode = val.getInt();
        this->update_cursor(false);
    } else if (path == "width") {
        this->width = 0.01 * CLAMP(val.getInt(10), 1, 100);
    } else if (path == "usepressurewidth") {
        this->usepressurewidth = val.getBool();
    } else if (path == "usepressurepopulation") {
        this->usepressurepopulation = val.getBool();
    } else if (path == "usepressurescale") {
        this->usepressurescale = val.getBool();
    } else if (path == "population") {
        this->population = 0.01 * CLAMP(val.getInt(10), 1, 100);
    } else if (path == "rotation_variation") {
        this->rotation_variation = CLAMP(val.getDouble(0.0), 0, 100.0);
    } else if (path == "scale_variation") {
        this->scale_variation = CLAMP(val.getDouble(1.0), 0, 100.0);
    } else if (path == "standard_deviation") {
        this->standard_deviation = 0.01 * CLAMP(val.getInt(10), 1, 100);
    } else if (path == "mean") {
        this->mean = 0.01 * CLAMP(val.getInt(10), 1, 100);
// Not implemented in the toolbar and preferences yet
    } else if (path == "distribution") {
        this->distrib = val.getInt(1);
    } else if (path == "tilt") {
        this->tilt = CLAMP(val.getDouble(0.1), 0, 1000.0);
    } else if (path == "ratio") {
        this->ratio = CLAMP(val.getDouble(), 0.0, 0.9);
    } else if (path == "offset") {
        this->offset = val.getDoubleLimited(100.0, 0, 1000.0);
    } else if (path == "pick_center") {
        this->pick_center =  val.getBool(true);
    } else if (path == "pick_inverse_value") {
        this->pick_inverse_value =  val.getBool(false);
    } else if (path == "pick_fill") {
        this->pick_fill =  val.getBool(false);
    } else if (path == "pick_stroke") {
        this->pick_stroke =  val.getBool(false);
    } else if (path == "pick_no_overlap") {
        this->pick_no_overlap =  val.getBool(false);
    } else if (path == "over_no_transparent") {
        this->over_no_transparent =  val.getBool(true);
    } else if (path == "over_transparent") {
        this->over_transparent =  val.getBool(true);
    } else if (path == "no_overlap") {
        this->no_overlap = val.getBool(false);
    } else if (path == "picker") {
        this->picker =  val.getBool(false);
    }
}

static void sp_spray_extinput(SprayTool *tc, ExtendedInput const &ext)
{
    if (ext.pressure) {
        tc->pressure = CLAMP(*ext.pressure, TC_MIN_PRESSURE, TC_MAX_PRESSURE);
    } else {
        tc->pressure = TC_DEFAULT_PRESSURE;
    }
}

static double get_width(SprayTool *tc)
{
    double pressure = (tc->usepressurewidth? tc->pressure / TC_DEFAULT_PRESSURE : 1);
    return pressure * tc->width;
}

static double get_dilate_radius(SprayTool *tc)
{
    return 250 * get_width(tc) /tc->getDesktop()->current_zoom();
}

static double get_path_mean(SprayTool *tc)
{
    return tc->mean;
}

static double get_path_standard_deviation(SprayTool *tc)
{
    return tc->standard_deviation;
}

static double get_population(SprayTool *tc)
{
    double pressure = (tc->usepressurepopulation? tc->pressure / TC_DEFAULT_PRESSURE : 1);
    return pressure * tc->population;
}

static double get_pressure(SprayTool *tc)
{
    double pressure = tc->pressure / TC_DEFAULT_PRESSURE;
    return pressure;
}

static double get_move_mean(SprayTool *tc)
{
    return tc->mean;
}

static double get_move_standard_deviation(SprayTool *tc)
{
    return tc->standard_deviation;
}

/**
 * Method to handle the distribution of the items
 * @param[out]  radius : radius of the position of the sprayed object
 * @param[out]  angle : angle of the position of the sprayed object
 * @param[in]   a : mean
 * @param[in]   s : standard deviation
 * @param[in]   choice :

 */
static void random_position(double &radius, double &angle, double &a, double &s, int /*choice*/)
{
    // angle is taken from an uniform distribution
    angle = g_random_double_range(0, M_PI*2.0);

    // radius is taken from a Normal Distribution
    double radius_temp =-1;
    while(!((radius_temp >= 0) && (radius_temp <=1 )))
    {
        radius_temp = NormalDistribution(a, s);
    }
    // Because we are in polar coordinates, a special treatment has to be done to the radius.
    // Otherwise, positions taken from an uniform repartition on radius and angle will not seam to
    // be uniformily distributed on the disk (more at the center and less at the boundary).
    // We counter this effect with a 0.5 exponent. This is empiric.
    radius = pow(radius_temp, 0.5);

}

static void sp_spray_transform_path(SPItem * item, Geom::Path &path, Geom::Affine affine, Geom::Point center){
    path *= i2anc_affine(static_cast<SPItem *>(item->parent), nullptr).inverse();
    path *= item->transform.inverse();
    Geom::Affine dt2p;
    if (item->parent) {
        dt2p = static_cast<SPItem *>(item->parent)->i2dt_affine().inverse();
    } else {
        dt2p = item->document->dt2doc();
    }
    Geom::Affine i2dt = item->i2dt_affine() * Geom::Translate(center).inverse() * affine * Geom::Translate(center);
    path *= i2dt * dt2p;
    path *= i2anc_affine(static_cast<SPItem *>(item->parent), nullptr);
}

/**
Randomizes \a val by \a rand, with 0 < val < 1 and all values (including 0, 1) having the same
probability of being displaced.
 */
double randomize01(double val, double rand)
{
    double base = MIN (val - rand, 1 - 2*rand);
    if (base < 0) {
        base = 0;
    }
    val = base + g_random_double_range (0, MIN (2 * rand, 1 - base));
    return CLAMP(val, 0, 1); // this should be unnecessary with the above provisions, but just in case...
}

static guint32 getPickerData(Geom::IntRect area, SPDesktop *desktop)
{
    Inkscape::CanvasItemDrawing *canvas_item_drawing = desktop->getCanvasDrawing();
    Inkscape::Drawing *drawing = canvas_item_drawing->get_drawing();

    // Get average color.
    double R, G, B, A;
    drawing->averageColor(area, R, G, B, A);

    //this can fix the bug #1511998 if confirmed
    if ( A < 1e-6) {
        R = 1.0;
        G = 1.0;
        B = 1.0;
    }

    return SP_RGBA32_F_COMPOSE(R, G, B, A);
}

static void showHidden(std::vector<SPItem *> items_down){
    for (auto item_hidden : items_down) {
        item_hidden->setHidden(false);
        item_hidden->updateRepr();
    }
}
//todo: maybe move same parameter to preferences
static bool fit_item(SPDesktop *desktop,
                     SPItem *item,
                     Geom::OptRect bbox,
                     Geom::Point &move,
                     Geom::Point center,
                     gint mode,
                     double angle,
                     double &_scale,
                     double scale,
                     bool picker,
                     bool pick_center,
                     bool pick_inverse_value,
                     bool pick_fill,
                     bool pick_stroke,
                     bool pick_no_overlap,
                     bool over_no_transparent,
                     bool over_transparent,
                     bool no_overlap,
                     double offset,
                     SPCSSAttr *css,
                     bool trace_scale,
                     int pick,
                     bool do_trace,
                     bool single_click,
                     bool pick_to_size,
                     bool pick_to_presence,
                     bool pick_to_color,
                     bool pick_to_opacity,
                     bool invert_picked,
                     double gamma_picked ,
                     double rand_picked)
{
    SPDocument *doc = item->document;
    double width = bbox->width();
    double height = bbox->height();
    double offset_width = (offset * width)/100.0 - (width);
    if(offset_width < 0 ){
        offset_width = 0;
    }
    double offset_height = (offset * height)/100.0 - (height);
    if(offset_height < 0 ){
        offset_height = 0;
    }
    if(picker && pick_to_size && !trace_scale && do_trace){
        _scale = 0.1;
    }
    Geom::OptRect bbox_procesed = Geom::Rect(Geom::Point(bbox->left() - offset_width, bbox->top() - offset_height),Geom::Point(bbox->right() + offset_width, bbox->bottom() + offset_height));
    Geom::Path path;
    path.start(Geom::Point(bbox_procesed->left(), bbox_procesed->top()));
    path.appendNew<Geom::LineSegment>(Geom::Point(bbox_procesed->right(), bbox_procesed->top()));
    path.appendNew<Geom::LineSegment>(Geom::Point(bbox_procesed->right(), bbox_procesed->bottom()));
    path.appendNew<Geom::LineSegment>(Geom::Point(bbox_procesed->left(), bbox_procesed->bottom()));
    path.close(true);
    sp_spray_transform_path(item, path, Geom::Scale(_scale), center);
    sp_spray_transform_path(item, path, Geom::Scale(scale), center);
    sp_spray_transform_path(item, path, Geom::Rotate(angle), center);
    path *= Geom::Translate(move);
    path *= desktop->doc2dt();
    bbox_procesed = path.boundsFast();
    double bbox_left_main = bbox_procesed->left();
    double bbox_right_main = bbox_procesed->right();
    double bbox_top_main = bbox_procesed->top();
    double bbox_bottom_main = bbox_procesed->bottom();
    double width_transformed = bbox_procesed->width();
    double height_transformed = bbox_procesed->height();
    Geom::Point mid_point = desktop->d2w(bbox_procesed->midpoint());
    Geom::IntRect area = Geom::IntRect::from_xywh(floor(mid_point[Geom::X]), floor(mid_point[Geom::Y]), 1, 1);
    guint32 rgba = getPickerData(area, desktop);
    guint32 rgba2 = 0xffffff00;
    Geom::Rect rect_sprayed(desktop->d2w(Geom::Point(bbox_left_main,bbox_top_main)), desktop->d2w(Geom::Point(bbox_right_main,bbox_bottom_main)));
    if (!rect_sprayed.hasZeroArea()) {
        rgba2 = getPickerData(rect_sprayed.roundOutwards(), desktop);
    }
    if(pick_no_overlap) {
        if(rgba != rgba2) {
            if(mode != SPRAY_MODE_ERASER) {
                return false;
            }
        }
    }
    if(!pick_center) {
        rgba = rgba2;
    }
    if(!over_transparent && (SP_RGBA32_A_F(rgba) == 0 || SP_RGBA32_A_F(rgba) < 1e-6)) {
        if(mode != SPRAY_MODE_ERASER) {
            return false;
        }
    }
    if(!over_no_transparent && SP_RGBA32_A_F(rgba) > 0) {
        if(mode != SPRAY_MODE_ERASER) {
            return false;
        }
    }
    if(offset < 100 ) {
        offset_width = ((99.0 - offset) * width_transformed)/100.0 - width_transformed;
        offset_height = ((99.0 - offset) * height_transformed)/100.0 - height_transformed;
    } else {
        offset_width = 0;
        offset_height = 0;
    }
    std::vector<SPItem*> items_down = desktop->getDocument()->getItemsPartiallyInBox(desktop->dkey, *bbox_procesed);
    Inkscape::Selection *selection = desktop->getSelection();
    if (selection->isEmpty()) {
        return false;
    }
    std::vector<SPItem*> const items_selected(selection->items().begin(), selection->items().end());
    std::vector<SPItem*> items_down_erased;
    for (std::vector<SPItem*>::const_iterator i=items_down.begin(); i!=items_down.end(); ++i) {
        SPItem *item_down = *i;
        Geom::OptRect bbox_down = item_down->documentVisualBounds();
        double bbox_left = bbox_down->left();
        double bbox_top = bbox_down->top();
        gchar const * item_down_sharp = g_strdup_printf("#%s", item_down->getId());
        items_down_erased.push_back(item_down);
        for (auto item_selected : items_selected) {
            gchar const * spray_origin;
            if(!item_selected->getAttribute("inkscape:spray-origin")){
                spray_origin = g_strdup_printf("#%s", item_selected->getId());
            } else {
                spray_origin = item_selected->getAttribute("inkscape:spray-origin");
            }
            if(strcmp(item_down_sharp, spray_origin) == 0 ||
                (item_down->getAttribute("inkscape:spray-origin") &&
                strcmp(item_down->getAttribute("inkscape:spray-origin"),spray_origin) == 0 ))
            {
                if(mode == SPRAY_MODE_ERASER) {
                    if(strcmp(item_down_sharp, spray_origin) != 0 && !selection->includes(item_down) ){
                        item_down->deleteObject();
                        items_down_erased.pop_back();
                        break;
                    }
                } else if(no_overlap) {
                    if(!(offset_width < 0 && offset_height < 0 && std::abs(bbox_left - bbox_left_main) > std::abs(offset_width) &&
                std::abs(bbox_top - bbox_top_main) > std::abs(offset_height))){
                        if(!no_overlap && (picker || over_transparent || over_no_transparent)){
                            showHidden(items_down);
                        }
                        return false;
                    }
                } else if(picker || over_transparent || over_no_transparent) {
                    item_down->setHidden(true);
                    item_down->updateRepr();
                }
            }
        }
    }
    if(mode == SPRAY_MODE_ERASER){
        if(!no_overlap && (picker || over_transparent || over_no_transparent)){
            showHidden(items_down_erased);
        }
        return false;
    }
    if(picker || over_transparent || over_no_transparent){
        if(!no_overlap){
            doc->ensureUpToDate();
            rgba = getPickerData(area, desktop);
            if (!rect_sprayed.hasZeroArea()) {
                rgba2 = getPickerData(rect_sprayed.roundOutwards(), desktop);
            }
        }
        if(pick_no_overlap){
            if(rgba != rgba2){
                if(!no_overlap && (picker || over_transparent || over_no_transparent)){
                    showHidden(items_down);
                }
                return false;
            }
        }
        if(!pick_center){
            rgba = rgba2;
        }
        double opacity = 1.0;
        gchar color_string[32]; *color_string = 0;
        float r = SP_RGBA32_R_F(rgba);
        float g = SP_RGBA32_G_F(rgba);
        float b = SP_RGBA32_B_F(rgba);
        float a = SP_RGBA32_A_F(rgba);
        if(!over_transparent && (a == 0 || a < 1e-6)){
            if(!no_overlap && (picker || over_transparent || over_no_transparent)){
                showHidden(items_down);
            }
            return false;
        }
        if(!over_no_transparent && a > 0){
            if(!no_overlap && (picker || over_transparent || over_no_transparent)){
                showHidden(items_down);
            }
            return false;
        }

        if(picker && do_trace){
            float hsl[3];
            SPColor::rgb_to_hsl_floatv (hsl, r, g, b);

            gdouble val = 0;
            switch (pick) {
            case PICK_COLOR:
                val = 1 - hsl[2]; // inverse lightness; to match other picks where black = max
                break;
            case PICK_OPACITY:
                val = a;
                break;
            case PICK_R:
                val = r;
                break;
            case PICK_G:
                val = g;
                break;
            case PICK_B:
                val = b;
                break;
            case PICK_H:
                val = hsl[0];
                break;
            case PICK_S:
                val = hsl[1];
                break;
            case PICK_L:
                val = 1 - hsl[2];
                break;
            default:
                break;
            }

            if (rand_picked > 0) {
                val = randomize01 (val, rand_picked);
                r = randomize01 (r, rand_picked);
                g = randomize01 (g, rand_picked);
                b = randomize01 (b, rand_picked);
            }

            if (gamma_picked != 0) {
                double power;
                if (gamma_picked > 0)
                    power = 1/(1 + fabs(gamma_picked));
                else
                    power = 1 + fabs(gamma_picked);

                val = pow (val, power);
                r = pow ((double)r, (double)power);
                g = pow ((double)g, (double)power);
                b = pow ((double)b, (double)power);
            }

            if (invert_picked) {
                val = 1 - val;
                r = 1 - r;
                g = 1 - g;
                b = 1 - b;
            }

            val = CLAMP (val, 0, 1);
            r = CLAMP (r, 0, 1);
            g = CLAMP (g, 0, 1);
            b = CLAMP (b, 0, 1);

            // recompose tweaked color
            rgba = SP_RGBA32_F_COMPOSE(r, g, b, a);
            if (pick_to_size) {
                if(!trace_scale){
                    if(pick_inverse_value) {
                        _scale = 1.0 - val;
                    } else {
                        _scale = val;
                    }
                    if(_scale == 0.0) {
                        if(!no_overlap && (picker || over_transparent || over_no_transparent)){
                            showHidden(items_down);
                        }
                        return false;
                    }
                    if(!fit_item(desktop
                                 , item
                                 , bbox
                                 , move
                                 , center
                                 , mode
                                 , angle
                                 , _scale
                                 , scale
                                 , picker
                                 , pick_center
                                 , pick_inverse_value
                                 , pick_fill
                                 , pick_stroke
                                 , pick_no_overlap
                                 , over_no_transparent
                                 , over_transparent
                                 , no_overlap
                                 , offset
                                 , css
                                 , true
                                 , pick
                                 , do_trace
                                 , single_click
                                 , pick_to_size
                                 , pick_to_presence
                                 , pick_to_color
                                 , pick_to_opacity
                                 , invert_picked
                                 , gamma_picked
                                 , rand_picked)
                        )
                    {
                        if(!no_overlap && (picker || over_transparent || over_no_transparent)){
                            showHidden(items_down);
                        }
                        return false;
                    }
                }
            }

            if (pick_to_opacity) {
                if(pick_inverse_value) {
                    opacity *= 1.0 - val;
                } else {
                    opacity *= val;
                }
                std::stringstream opacity_str;
                opacity_str.imbue(std::locale::classic());
                opacity_str << opacity;
                sp_repr_css_set_property(css, "opacity", opacity_str.str().c_str());
            }
            if (pick_to_presence) {
                if (g_random_double_range (0, 1) > val) {
                    //Hiding the element is a way to retain original
                    //behaviour of tiled clones for presence option.
                    sp_repr_css_set_property(css, "opacity", "0");
                }
            }
            if (pick_to_color) {
                sp_svg_write_color(color_string, sizeof(color_string), rgba);
                if(pick_fill){
                    sp_repr_css_set_property(css, "fill", color_string);
                }
                if(pick_stroke){
                    sp_repr_css_set_property(css, "stroke", color_string);
                }
            }
            if (opacity < 1e-6) { // invisibly transparent, skip
                if(!no_overlap && (picker || over_transparent || over_no_transparent)){
                    showHidden(items_down);
                }
                return false;
            }
        }
        if(!do_trace){
            if(!pick_center){
                rgba = rgba2;
            }
            if (pick_inverse_value) {
                r = 1 - SP_RGBA32_R_F(rgba);
                g = 1 - SP_RGBA32_G_F(rgba);
                b = 1 - SP_RGBA32_B_F(rgba);
            } else {
                r = SP_RGBA32_R_F(rgba);
                g = SP_RGBA32_G_F(rgba);
                b = SP_RGBA32_B_F(rgba);
            }
            rgba = SP_RGBA32_F_COMPOSE(r, g, b, a);
            sp_svg_write_color(color_string, sizeof(color_string), rgba);
            if(pick_fill){
                sp_repr_css_set_property(css, "fill", color_string);
            }
            if(pick_stroke){
                sp_repr_css_set_property(css, "stroke", color_string);
            }
        }
        if(!no_overlap && (picker || over_transparent || over_no_transparent)){
            showHidden(items_down);
        }
    }
    return true;
}

static bool sp_spray_recursive(SPDesktop *desktop,
                               Inkscape::ObjectSet *set,
                               SPItem *item,
                               SPItem *&single_path_output,
                               Geom::Point p,
                               Geom::Point /*vector*/,
                               gint mode,
                               double radius,
                               double population,
                               double &scale,
                               double scale_variation,
                               bool /*reverse*/,
                               double mean,
                               double standard_deviation,
                               double ratio,
                               double tilt,
                               double rotation_variation,
                               gint _distrib,
                               bool no_overlap,
                               bool picker,
                               bool pick_center,
                               bool pick_inverse_value,
                               bool pick_fill,
                               bool pick_stroke,
                               bool pick_no_overlap,
                               bool over_no_transparent,
                               bool over_transparent,
                               double offset,
                               bool usepressurescale,
                               double pressure,
                               int pick,
                               bool do_trace,
                               bool single_click,
                               double single_angle,
                               double single_scale,
                               bool pick_to_size,
                               bool pick_to_presence,
                               bool pick_to_color,
                               bool pick_to_opacity,
                               bool invert_picked,
                               double gamma_picked ,
                               double rand_picked)
{
    bool did = false;

    {
        // convert 3D boxes to ordinary groups before spraying their shapes
        // TODO: ideally the original object is preserved.
        if (auto box = cast<SPBox3D>(item)) {
            desktop->getSelection()->remove(item);
            set->remove(item);
            item = box->convert_to_group();
            set->add(item);
            desktop->getSelection()->add(item);
        }
    }

    double _fid = single_click ? 0 : g_random_double_range(0, 1);
    double angle = single_click ? single_angle : g_random_double_range( - rotation_variation / 100.0 * M_PI , rotation_variation / 100.0 * M_PI );
    double _scale = single_click ? single_scale : g_random_double_range( 1.0 - scale_variation / 100.0, 1.0 + scale_variation / 100.0 );
    if(!single_click && usepressurescale){
        _scale = pressure;
    }
    double dr; double dp;
    random_position( dr, dp, mean, standard_deviation, _distrib );
    dr=dr*radius;

    if (mode != SPRAY_MODE_SINGLE_PATH) {
        if (auto bbox = item->documentVisualBounds()) {
            if(_fid <= population || no_overlap)
            {
                SPDocument *doc = item->document;
                gchar const * spray_origin;
                if(!item->getAttribute("inkscape:spray-origin")){
                    spray_origin = g_strdup_printf("#%s", item->getId());
                } else {
                    spray_origin = item->getAttribute("inkscape:spray-origin");
                }
                Geom::Point center = item->getCenter();
                Geom::Point move = (Geom::Point(cos(tilt)*cos(dp)*dr/(1-ratio)+sin(tilt)*sin(dp)*dr/(1+ratio), -sin(tilt)*cos(dp)*dr/(1-ratio)+cos(tilt)*sin(dp)*dr/(1+ratio)))+(p-bbox->midpoint());
                if (single_click) {
                    move = p-bbox->midpoint();
                }
                SPCSSAttr *css = sp_repr_css_attr_new();
                bool stop = false;
                
                if (mode == SPRAY_MODE_ERASER ||
                    pick_no_overlap || no_overlap || picker ||
                    !over_transparent || !over_no_transparent) 
                {
                    for (auto i : {0,1}) {
                        if (!fit_item(desktop
                                    , item
                                    , bbox
                                    , move
                                    , center
                                    , mode
                                    , angle
                                    , _scale
                                    , scale
                                    , picker
                                    , pick_center
                                    , pick_inverse_value
                                    , pick_fill
                                    , pick_stroke
                                    , pick_no_overlap
                                    , over_no_transparent
                                    , over_transparent
                                    , no_overlap
                                    , offset
                                    , css
                                    , false
                                    , pick
                                    , do_trace
                                    , single_click
                                    , pick_to_size
                                    , pick_to_presence
                                    , pick_to_color
                                    , pick_to_opacity
                                    , invert_picked
                                    , gamma_picked
                                    , rand_picked)) {
                            if (no_overlap && i == 0) {
                                move = p-bbox->midpoint() * desktop->doc2dt().withoutTranslation();
                                continue;
                            } else {                     
                                stop = true;
                                break;
                            }
                        }
                    }
                    if (stop) {
                        return false;
                    }
                }
                SPItem *item_copied;
                // Duplicate
                Inkscape::XML::Document* xml_doc = doc->getReprDoc();
                Inkscape::XML::Node *old_repr = item->getRepr();
                Inkscape::XML::Node *parent = old_repr->parent();
                Inkscape::XML::Node *clone = nullptr;
                if (mode == SPRAY_MODE_CLONE) {
                    // Creation of the clone
                    clone = xml_doc->createElement("svg:use");
                    // Ad the clone to the list of the parent's children
                    parent->appendChild(clone);
                    // Generates the link between parent and child attributes
                    if(!clone->attribute("inkscape:spray-origin")){
                        clone->setAttribute("inkscape:spray-origin", spray_origin);
                    }
                    gchar *href_str = g_strdup_printf("#%s", old_repr->attribute("id"));
                    clone->setAttribute("xlink:href", href_str);
                    g_free(href_str);

                    SPObject *clone_object = doc->getObjectByRepr(clone);
                    item_copied = cast<SPItem>(clone_object);
                } else {
                    Inkscape::XML::Node *copy = old_repr->duplicate(xml_doc);
                    if(!copy->attribute("inkscape:spray-origin")){
                        copy->setAttribute("inkscape:spray-origin", spray_origin);
                    }
                    parent->appendChild(copy);
                    SPObject *new_obj = doc->getObjectByRepr(copy);
                    item_copied = cast<SPItem>(new_obj);   // Conversion object->item
                }
                // Conversion object->item
                
                Geom::Point tcenter = center;
                if (single_click && item->isCenterSet()) {
                    item_copied->unsetCenter();
                    item_copied->updateRepr();
                    center = bbox->midpoint();
                }
                sp_spray_scale_rel(center, tcenter, desktop, item_copied, Geom::Scale(_scale));
                sp_spray_scale_rel(center, tcenter, desktop, item_copied, Geom::Scale(scale));
                sp_spray_rotate_rel(center,tcenter, desktop,item_copied, Geom::Rotate(angle));
                // Move the cursor p
                auto translate = Geom::Translate(move * desktop->doc2dt().withoutTranslation());
                item_copied->move_rel(translate);
                if (single_click && item->isCenterSet()) {
                    item_copied->setCenter(tcenter * translate);
                    item_copied->updateRepr();
                }
                if(picker){
                    sp_desktop_apply_css_recursive(item_copied, css, true);
                }
                if (mode == SPRAY_MODE_CLONE) {
                    Inkscape::GC::release(clone);
                }
                did = true;
            }
        }
    }
#ifdef ENABLE_SPRAY_MODE_SINGLE_PATH
    else if (mode == SPRAY_MODE_SINGLE_PATH) {
        if (item) {
            SPDocument *doc = item->document;
            Inkscape::XML::Document* xml_doc = doc->getReprDoc();
            Inkscape::XML::Node *old_repr = item->getRepr();
            Inkscape::XML::Node *parent = old_repr->parent();

            if (auto bbox = item->documentVisualBounds()) {
                if (_fid <= population) { // Rules the population of objects sprayed
                    // Duplicates the parent item
                    Inkscape::XML::Node *copy = old_repr->duplicate(xml_doc);
                    gchar const * spray_origin;
                    if(!copy->attribute("inkscape:spray-origin")){
                        spray_origin = g_strdup_printf("#%s", old_repr->attribute("id"));
                    } else {
                        spray_origin = copy->attribute("inkscape:spray-origin");
                    }
                    parent->appendChild(copy);
                    SPObject *new_obj = doc->getObjectByRepr(copy);
                    auto item_copied = cast<SPItem>(new_obj);

                    // Move around the cursor
                    Geom::Point move = (Geom::Point(cos(tilt)*cos(dp)*dr/(1-ratio)+sin(tilt)*sin(dp)*dr/(1+ratio), -sin(tilt)*cos(dp)*dr/(1-ratio)+cos(tilt)*sin(dp)*dr/(1+ratio)))+(p-bbox->midpoint());

                    Geom::Point center = item->getCenter();
                    Geom::Point tcenter = center; // dont use
                    sp_spray_scale_rel(center, tcenter, desktop, item_copied, Geom::Scale(_scale, _scale));
                    sp_spray_scale_rel(center, tcenter, desktop, item_copied, Geom::Scale(scale, scale));
                    sp_spray_rotate_rel(center, tcenter,desktop, item_copied, Geom::Rotate(angle));
                    item_copied->move_rel(Geom::Translate(move * desktop->doc2dt().withoutTranslation()));

                    // Union
                    // only works if no groups in selection
                    ObjectSet object_set_tmp = *desktop->getSelection();
                    object_set_tmp.clear();
                    object_set_tmp.add(item_copied);
                    object_set_tmp.removeLPESRecursive(true);
                    if (is<SPUse>(object_set_tmp.objects().front())) {
                        object_set_tmp.unlinkRecursive(true);
                    }
                    if (single_path_output) { // Previous result
                        object_set_tmp.add(single_path_output);
                    }
                    object_set_tmp.pathUnion(true);
                    single_path_output = object_set_tmp.items().front();
                    for (auto item : object_set_tmp.items()) {
                        auto repr = item->getRepr();
                        repr->setAttribute("inkscape:spray-origin", spray_origin);
                    }
                    object_set_tmp.clear();
                    Inkscape::GC::release(copy);
                    did = true;
                }
            }
        }
    }
#endif
    return did;
}

static bool sp_spray_dilate(SprayTool *tc, Geom::Point p, Geom::Point vector, bool reverse, bool force = false)
{
    SPDesktop *desktop = tc->getDesktop();
    Inkscape::ObjectSet *set = tc->objectSet();
    if (set->isEmpty()) {
        return false;
    }

    bool did = false;
    double radius = get_dilate_radius(tc);
    double population = get_population(tc);
    if (radius == 0 || (population == 0 && !force)) { 
        return false;
    }
    double path_mean = get_path_mean(tc);
    if (radius == 0 || path_mean == 0) {
        return false;
    }
    double path_standard_deviation = get_path_standard_deviation(tc);
    if (radius == 0 || path_standard_deviation == 0) {
        return false;
    }
    double move_mean = get_move_mean(tc);
    double move_standard_deviation = get_move_standard_deviation(tc);

    {
        for(auto item : tc->items){
            g_assert(item != nullptr);
            sp_object_ref(item);
        }

        for(auto item : tc->items){
            g_assert(item != nullptr);
            if (sp_spray_recursive(desktop
                                , set
                                , item
                                , tc->single_path_output
                                , p, vector
                                , tc->mode
                                , radius
                                , population
                                , tc->scale
                                , tc->scale_variation
                                , reverse
                                , move_mean
                                , move_standard_deviation
                                , tc->ratio
                                , tc->tilt
                                , tc->rotation_variation
                                , tc->distrib
                                , tc->no_overlap
                                , tc->picker
                                , tc->pick_center
                                , tc->pick_inverse_value
                                , tc->pick_fill
                                , tc->pick_stroke
                                , tc->pick_no_overlap
                                , tc->over_no_transparent
                                , tc->over_transparent
                                , tc->offset
                                , tc->usepressurescale
                                , get_pressure(tc)
                                , tc->pick
                                , tc->do_trace
                                , tc->single_click
                                , tc->single_angle
                                , tc->single_scale
                                , tc->pick_to_size
                                , tc->pick_to_presence
                                , tc->pick_to_color
                                , tc->pick_to_opacity
                                , tc->invert_picked
                                , tc->gamma_picked
                                , tc->rand_picked)) {
                did = true;
            }
        }

        for(auto item : tc->items){
            g_assert(item != nullptr);
            sp_object_unref(item);
        }
    }

    return did;
}

static void sp_spray_update_area(SprayTool *tc)
{
    double radius = get_dilate_radius(tc);
    Geom::Affine const sm ( Geom::Scale(radius/(1-tc->ratio), radius/(1+tc->ratio)) *
                            Geom::Rotate(tc->tilt) *
                            Geom::Translate(tc->getDesktop()->point()));

    Geom::PathVector path = Geom::Path(Geom::Circle(0,0,1)); // Unit circle centered at origin.
    path *= sm;
    tc->dilate_area->set_bpath(path);
    tc->dilate_area->set_visible(true);
    if (tc->single_click && tc->items.size() == 1 && tc->mode != SPRAY_MODE_SINGLE_PATH && tc->mode != SPRAY_MODE_ERASER) {
        Geom::PathVector shapes;
        get_paths(tc->items[0], shapes);
        shapes *= Geom::Translate(tc->getDesktop()->point());
        tc->shapes_area->set_bpath(shapes);
        tc->shapes_area->set_visible(true);
    } else {
        tc->shapes_area->set_visible(false);
    }
}

static void sp_spray_switch_mode(SprayTool *tc, gint mode, bool with_shift)
{
    // Select the button mode
    auto tb = dynamic_cast<UI::Toolbar::SprayToolbar*>(tc->getDesktop()->get_toolbar_by_name("SprayToolbar"));

    if(tb) {
        tb->set_mode(mode);
    } else {
        std::cerr << "Could not access Spray toolbar" << std::endl;
    }

    // Need to set explicitly, because the prefs may not have changed by the previous
    tc->mode = mode;
    tc->update_cursor(with_shift);
}

bool SprayTool::root_handler(CanvasEvent const &event)
{
    bool ret = false;

    inspect_event(event,
        [&] (EnterEvent const &event) {
            dilate_area->set_visible(true);
            shapes_area->set_visible(true);
        },
        [&] (LeaveEvent const &event) {
            dilate_area->set_visible(false);
            shapes_area->set_visible(false);
        },
        [&] (ButtonPressEvent const &event) {
            if (event.num_press == 1 && event.button == 1) {
                _desktop->getSelection()->restoreBackup();
                if (Inkscape::have_viable_layer(_desktop, defaultMessageContext())) {
                    xyp = event.pos.floor();
                    setCloneTilerPrefs();
                    Geom::Point const motion_dt(_desktop->w2d(event.pos));
                    last_push = _desktop->dt2doc(motion_dt);

                    sp_spray_extinput(this, event.extinput);

                    set_high_motion_precision();
                    is_dilating = true;
                    has_dilated = false;
                    is_drawing = false;
                    object_set = *_desktop->getSelection();
                    if (mode == SPRAY_MODE_SINGLE_PATH) {
                        single_path_output = nullptr;
                    }

                    ret = true;
                    within_tolerance = true;
                    single_click = true;
                }
            }
            if (event.num_press == 1 && event.button == 3) {
                //reset preview on right click
                items.clear();
                ret = true;
            }
        },
        [&] (MotionEvent const &event) {

            Geom::Point motion_dt(_desktop->w2d(event.pos));
            Geom::Point motion_doc(_desktop->dt2doc(motion_dt));
            if (!has_dilated && items.empty() && mode != SPRAY_MODE_SINGLE_PATH) {
                _desktop->getSelection()->restoreBackup();
                update_cursor(true);
                object_set = *_desktop->getSelection();
                if (!object_set.isEmpty()) {
                    _desktop->getSelection()->clear();
                    items.clear();
                    items.insert(items.end(), object_set.items().begin(), object_set.items().end());
                    // select a random item from the ones selected to spay to preview and apply on single click
                    int pos = g_random_int_range(0, items.size());
                    SPItem * randintem = items[pos];
                    release_connection = randintem->connectRelease([this] (auto) { items.clear(); });
                    items.clear();
                    items.push_back(randintem);
                    shapes.clear();
                    get_paths(randintem, shapes);
                    single_angle = g_random_double_range( - rotation_variation / 100.0 * M_PI , rotation_variation / 100.0 * M_PI );
                    single_scale = g_random_double_range( 1.0 - scale_variation / 100.0, 1.0 + scale_variation / 100.0 );
                    if (usepressurescale && last_pressure) {
                        single_scale = last_pressure;
                    }
                    Geom::OptRect a = shapes.boundsFast();
                    if (a) {
                        Geom::Translate const s(a->midpoint());
                        shapes *= s.inverse() * Geom::Scale(single_scale) * s;
                        shapes *= s.inverse() * Geom::Scale(scale) * s;
                        shapes *= s.inverse() * Geom::Rotate(single_angle) * s;
                    }
                }
            }
            // To fix https://bugs.launchpad.net/inkscape/+bug/1458200
            // we increase the tolerance because no sensible data for panning
            if (within_tolerance && Geom::LInfty(event.pos.floor() - xyp) < tolerance * 3) {
                // do not drag if we're within tolerance from origin
                return;
            }
            if (!is_drawing && is_dilating) {
                items.clear();
                items.insert(items.end(), object_set.items().begin(), object_set.items().end());
            }
            
            // Once the user has moved farther than tolerance from
            // the original location (indicating they intend to move
            // the object, not click), then always process the motion
            // notify coordinates as given (no snapping back to origin)
            within_tolerance = false;
            single_click = false;
            sp_spray_extinput(this, event.extinput);

            // Draw the dilating cursor
            double radius = get_dilate_radius(this);
            Geom::Affine const sm (Geom::Scale(radius/(1-ratio), radius/(1+ratio)) *
                                   Geom::Rotate(tilt) *
                                   Geom::Translate(motion_dt));

            Geom::PathVector path = Geom::Path(Geom::Circle(0, 0, 1)); // Unit circle centered at origin.
            path *= sm;
            dilate_area->set_bpath(path);
            dilate_area->set_visible(true);
            if (!has_dilated && items.size() == 1 && mode != SPRAY_MODE_SINGLE_PATH && mode != SPRAY_MODE_ERASER) {
                shapes *= Geom::Translate(getDesktop()->point());
                shapes_area->set_bpath(shapes);
                shapes *= Geom::Translate(getDesktop()->point()).inverse();
                shapes_area->set_visible(true);
            } else {
                shapes_area->set_visible(false);
            }
            unsigned num = items.size();
            if (num == 0) {
                this->message_context->flash(Inkscape::ERROR_MESSAGE, _("<b>Nothing selected!</b> Select objects to spray."));
            }

            // Dilating:
            if (is_dilating && ( event.modifiers & GDK_BUTTON1_MASK )) {
                sp_spray_dilate(this, motion_doc, motion_doc - last_push, event.modifiers & GDK_SHIFT_MASK ? true : false);
                //this->last_push = motion_doc;
                is_drawing = true;
                has_dilated = true;
                // It's slow, so prevent clogging up with events
                gobble_motion_events(GDK_BUTTON1_MASK);
                ret = true;
            }
        },
        [&] (ScrollEvent const &event) {
            if (event.modifiers == GDK_BUTTON1_MASK) {
                std::cout << "SCROLL" << std::endl;
                /* Spray with the scroll */
                double temp ;
                temp = population;
                population = 1.0;
                _desktop->setToolboxAdjustmentValue("spray-population", population * 100);
                Geom::Point const scroll_dt = _desktop->point();;

                switch (event.direction) {
                    case Gdk::ScrollDirection::DOWN:
                    case Gdk::ScrollDirection::UP:
                    case Gdk::ScrollDirection::SMOOTH:
                        if (Inkscape::have_viable_layer(_desktop, defaultMessageContext())) {
                            last_push = _desktop->dt2doc(scroll_dt);
                            sp_spray_extinput(this, event.extinput);
                            if(is_dilating) {
                                sp_spray_dilate(this, _desktop->dt2doc(scroll_dt), Geom::Point(0, 0), false);
                            }
                            population = temp;
                            _desktop->setToolboxAdjustmentValue("spray-population", population * 100);

                            ret = true;
                        }
                        break;
                    case Gdk::ScrollDirection::RIGHT:
                       break;
                    case Gdk::ScrollDirection::LEFT:
                       break;
                }
            }
        },
        [&] (ButtonReleaseEvent const &event) {

            Geom::Point const motion_dt(_desktop->w2d(event.pos));
            Geom::Point motion_doc(_desktop->dt2doc(motion_dt));

            set_high_motion_precision(false);
            is_drawing = false;
            
            if ((single_click || this->is_dilating) && event.button == 1) {
                if (single_click) {
                    sp_spray_dilate(this, _desktop->dt2doc(motion_dt), motion_doc - this->last_push, event.modifiers & GDK_SHIFT_MASK? true : false, true);
                } else if (!this->has_dilated) {
                    // If we did not rub, do a light tap
                    pressure = 0.03;
                    sp_spray_dilate(this, _desktop->dt2doc(motion_dt), Geom::Point(0,0), event.modifiers & GDK_SHIFT_MASK);
                }
                last_pressure = pressure;
                items.clear();
                is_dilating = false;
                is_drawing = false;
                has_dilated = false;
                single_click = false;
                switch (mode) {
                    case SPRAY_MODE_COPY:
                        DocumentUndo::done(_desktop->getDocument(), _("Spray with copies"), INKSCAPE_ICON("tool-spray"));
                        break;
                    case SPRAY_MODE_CLONE:
                        DocumentUndo::done(_desktop->getDocument(), _("Spray with clones"), INKSCAPE_ICON("tool-spray"));
                        break;
                    case SPRAY_MODE_SINGLE_PATH:
                        DocumentUndo::done(_desktop->getDocument(), _("Spray in single path"), INKSCAPE_ICON("tool-spray"));
                        break;
                }
            }
            _desktop->getSelection()->clear();
            object_set.clear();
        },
        [&] (KeyPressEvent const &event) {
            switch (get_latin_keyval (event)) {
                case GDK_KEY_j:
                case GDK_KEY_J:
                    if (mod_shift_only(event)) {
                        sp_spray_switch_mode(this, SPRAY_MODE_COPY, mod_shift(event));
                        ret = true;
                    }
                    break;
                case GDK_KEY_k:
                case GDK_KEY_K:
                    if (mod_shift_only(event)) {
                        sp_spray_switch_mode(this, SPRAY_MODE_CLONE, mod_shift(event));
                        ret = true;
                    }
                    break;
#ifdef ENABLE_SPRAY_MODE_SINGLE_PATH
                case GDK_KEY_l:
                case GDK_KEY_L:
                    if (mod_shift_only(event)) {
                        sp_spray_switch_mode(this, SPRAY_MODE_SINGLE_PATH, mod_shift(event));
                        ret = true;
                    }
                    break;
#endif
                case GDK_KEY_Up:
                case GDK_KEY_KP_Up:
                    if (!mod_ctrl_only(event)) {
                        population += 0.01;
                        if (population > 1.0) {
                            population = 1.0;
                        }
                        _desktop->setToolboxAdjustmentValue("spray-population", population * 100);
                        ret = true;
                    }
                    break;
                case GDK_KEY_Down:
                case GDK_KEY_KP_Down:
                    if (!mod_ctrl_only(event)) {
                        population -= 0.01;
                        if (population < 0.0) {
                            population = 0.0;
                        }
                        _desktop->setToolboxAdjustmentValue("spray-population", population * 100);
                        ret = true;
                    }
                    break;
                case GDK_KEY_Right:
                case GDK_KEY_KP_Right:
                    if (!mod_ctrl_only(event)) {
                        width += 0.01;
                        if (width > 1.0) {
                            width = 1.0;
                        }
                        // The same spinbutton is for alt+x
                        _desktop->setToolboxAdjustmentValue("spray-width", width * 100);
                        sp_spray_update_area(this);
                        ret = true;
                    }
                    break;
                case GDK_KEY_Left:
                case GDK_KEY_KP_Left:
                    if (!mod_ctrl_only(event)) {
                        width -= 0.01;
                        if (width < 0.01) {
                            width = 0.01;
                        }
                        _desktop->setToolboxAdjustmentValue("spray-width", width * 100);
                        sp_spray_update_area(this);
                        ret = true;
                    }
                    break;
                case GDK_KEY_Home:
                case GDK_KEY_KP_Home:
                    width = 0.01;
                    _desktop->setToolboxAdjustmentValue("spray-width", width * 100);
                    sp_spray_update_area(this);
                    ret = true;
                    break;
                case GDK_KEY_End:
                case GDK_KEY_KP_End:
                    width = 1.0;
                    _desktop->setToolboxAdjustmentValue("spray-width", width * 100);
                    sp_spray_update_area(this);
                    ret = true;
                    break;
                case GDK_KEY_x:
                case GDK_KEY_X:
                    if (mod_alt_only(event)) {
                        _desktop->setToolboxFocusTo("spray-width");
                        ret = true;
                    }
                    break;
                case GDK_KEY_Shift_L:
                case GDK_KEY_Shift_R:
                    update_cursor(true);
                    break;
                case GDK_KEY_Control_L:
                case GDK_KEY_Control_R:
                    break;
                case GDK_KEY_Delete:
                case GDK_KEY_KP_Delete:
                case GDK_KEY_BackSpace:
                    ret = deleteSelectedDrag(mod_ctrl_only(event));
                    break;

                default:
                    break;
            }
        },
        [&] (KeyReleaseEvent const &event) {
            Inkscape::Preferences *prefs = Inkscape::Preferences::get();
            switch (get_latin_keyval(event)) {
                case GDK_KEY_Shift_L:
                case GDK_KEY_Shift_R:
                    update_cursor(false);
                    break;
                case GDK_KEY_Control_L:
                case GDK_KEY_Control_R:
                    sp_spray_switch_mode (this, prefs->getInt("/tools/spray/mode"), mod_shift(event));
                    message_context->clear();
                    break;
                default:
                    sp_spray_switch_mode (this, prefs->getInt("/tools/spray/mode"), mod_shift(event));
                    break;
            }
        },
        [&] (CanvasEvent const &event) {}
    );

    return ret || ToolBase::root_handler(event);
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
