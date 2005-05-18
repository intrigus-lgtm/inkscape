#define __SP_ARC_CONTEXT_C__

/** \file Ellipse drawing context. */

/*
 * Authors:
 *   Mitsuru Oka
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *
 * Copyright (C) 2000-2002 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 * Copyright (C) 2002 Mitsuru Oka
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <math.h>
#include <gdk/gdkkeysyms.h>

#include "macros.h"
#include <glibmm/i18n.h>
#include "display/sp-canvas.h"
#include "inkscape.h"
#include "sp-ellipse.h"
#include "document.h"
#include "selection.h"
#include "desktop-handles.h"
#include "desktop-affine.h"
#include "snap.h"
#include "pixmaps/cursor-arc.xpm"
#include "desktop-widget.h"
#include "sp-metrics.h"
#include "knotholder.h"
#include "xml/repr.h"
#include "xml/node-event-vector.h"
#include "object-edit.h"
#include "prefs-utils.h"
#include "widgets/spw-utilities.h"
#include "message-context.h"
#include "desktop.h"
#include "desktop-style.h"

#include "arc-context.h"

static void sp_arc_context_class_init(SPArcContextClass *klass);
static void sp_arc_context_init(SPArcContext *arc_context);
static void sp_arc_context_dispose(GObject *object);

static void sp_arc_context_setup(SPEventContext *ec);
static gint sp_arc_context_root_handler(SPEventContext *event_context, GdkEvent *event);
static gint sp_arc_context_item_handler(SPEventContext *event_context, SPItem *item, GdkEvent *event);

static void sp_arc_drag(SPArcContext *ec, NR::Point pt, guint state);
static void sp_arc_finish(SPArcContext *ec);

static SPEventContextClass *parent_class;

GtkType
sp_arc_context_get_type(void)
{
    static GType type = 0;
    if (!type) {
        GTypeInfo info = {
            sizeof(SPArcContextClass),
            NULL, NULL,
            (GClassInitFunc) sp_arc_context_class_init,
            NULL, NULL,
            sizeof(SPArcContext),
            4,
            (GInstanceInitFunc) sp_arc_context_init,
            NULL,   /* value_table */
        };
        type = g_type_register_static(SP_TYPE_EVENT_CONTEXT, "SPArcContext", &info, (GTypeFlags)0);
    }
    return type;
}

static void
sp_arc_context_class_init(SPArcContextClass *klass)
{
    GObjectClass *object_class;
    SPEventContextClass *event_context_class;

    object_class = (GObjectClass *) klass;
    event_context_class = (SPEventContextClass *) klass;

    parent_class = (SPEventContextClass*)g_type_class_peek_parent(klass);

    object_class->dispose = sp_arc_context_dispose;

    event_context_class->setup = sp_arc_context_setup;
    event_context_class->root_handler = sp_arc_context_root_handler;
    event_context_class->item_handler = sp_arc_context_item_handler;
}

static void sp_arc_context_init(SPArcContext *arc_context)
{
    SPEventContext *event_context = SP_EVENT_CONTEXT(arc_context);

    event_context->cursor_shape = cursor_arc_xpm;
    event_context->hot_x = 4;
    event_context->hot_y = 4;
    event_context->xp = 0;
    event_context->yp = 0;
    event_context->tolerance = 0;
    event_context->within_tolerance = false;
    event_context->item_to_select = NULL;

    event_context->shape_repr = NULL;
    event_context->shape_knot_holder = NULL;

    arc_context->item = NULL;

    new (&arc_context->sel_changed_connection) sigc::connection();
}

static void sp_arc_context_dispose(GObject *object)
{
    SPEventContext *ec = SP_EVENT_CONTEXT(object);
    SPArcContext *ac = SP_ARC_CONTEXT(object);

    ec->enableGrDrag(false);

    ac->sel_changed_connection.disconnect();
    ac->sel_changed_connection.~connection();

    if (ec->shape_knot_holder) {
        sp_knot_holder_destroy(ec->shape_knot_holder);
        ec->shape_knot_holder = NULL;
    }

    if (ec->shape_repr) { // remove old listener
        sp_repr_remove_listener_by_data(ec->shape_repr, ec);
        sp_repr_unref(ec->shape_repr);
        ec->shape_repr = 0;
    }

    /* fixme: This is necessary because we do not grab */
    if (ac->item) sp_arc_finish(ac);

    if (ac->_message_context) {
        delete ac->_message_context;
    }

    G_OBJECT_CLASS(parent_class)->dispose(object);
}

static Inkscape::XML::NodeEventVector ec_shape_repr_events = {
    NULL, /* child_added */
    NULL, /* child_removed */
    ec_shape_event_attr_changed,
    NULL, /* content_changed */
    NULL  /* order_changed */
};

/**
\brief  Callback that processes the "changed" signal on the selection;
destroys old and creates new knotholder.
*/
void
sp_arc_context_selection_changed(Inkscape::Selection * selection, gpointer data)
{
    SPArcContext *ac = SP_ARC_CONTEXT(data);
    SPEventContext *ec = SP_EVENT_CONTEXT(ac);

    if (ec->shape_knot_holder) { // desktroy knotholder
        sp_knot_holder_destroy(ec->shape_knot_holder);
        ec->shape_knot_holder = NULL;
    }

    if (ec->shape_repr) { // remove old listener
        sp_repr_remove_listener_by_data(ec->shape_repr, ec);
        sp_repr_unref(ec->shape_repr);
        ec->shape_repr = 0;
    }

    SPItem *item = selection ? selection->singleItem() : NULL;
    if (item) {
        ec->shape_knot_holder = sp_item_knot_holder(item, ec->desktop);
        Inkscape::XML::Node *shape_repr = SP_OBJECT_REPR(item);
        if (shape_repr) {
            ec->shape_repr = shape_repr;
            sp_repr_ref(shape_repr);
            sp_repr_add_listener(shape_repr, &ec_shape_repr_events, ec);
            sp_repr_synthesize_events(shape_repr, &ec_shape_repr_events, ec);
        }


    }
}

static void
sp_arc_context_setup(SPEventContext *ec)
{
    SPArcContext *ac = SP_ARC_CONTEXT(ec);

    if (((SPEventContextClass *) parent_class)->setup)
        ((SPEventContextClass *) parent_class)->setup(ec);

    SPItem *item = SP_DT_SELECTION(ec->desktop)->singleItem();

    if (item) {
        ec->shape_knot_holder = sp_item_knot_holder(item, ec->desktop);
        Inkscape::XML::Node *shape_repr = SP_OBJECT_REPR(item);
        if (shape_repr) {
            ec->shape_repr = shape_repr;
            sp_repr_ref(shape_repr);
            sp_repr_add_listener(shape_repr, &ec_shape_repr_events, ec);
            sp_repr_synthesize_events(shape_repr, &ec_shape_repr_events, ec);
        }
    }

    ac->sel_changed_connection.disconnect();
    ac->sel_changed_connection = SP_DT_SELECTION(ec->desktop)->connectChanged(sigc::bind(sigc::ptr_fun(&sp_arc_context_selection_changed), (gpointer)ac));

    if (prefs_get_int_attribute("tools.shapes", "selcue", 0) != 0) {
        ec->enableSelectionCue();
    }

    if (prefs_get_int_attribute("tools.shapes", "gradientdrag", 0) != 0) {
        ec->enableGrDrag();
    }

    ac->_message_context = new Inkscape::MessageContext(SP_VIEW(ec->desktop)->messageStack());
}

static gint sp_arc_context_item_handler(SPEventContext *event_context, SPItem *item, GdkEvent *event)
{
    SPDesktop *desktop = event_context->desktop;
    gint ret;

    ret = FALSE;

    switch (event->type) {
        case GDK_BUTTON_PRESS:
            if (event->button.button == 1) {

                // save drag origin
                event_context->xp = (gint) event->button.x;
                event_context->yp = (gint) event->button.y;
                event_context->within_tolerance = true;

                // remember clicked item, disregarding groups, honoring Alt
                event_context->item_to_select = sp_event_context_find_item (desktop, NR::Point(event->button.x, event->button.y), event->button.state, TRUE);

                ret = TRUE;
            }
            break;
            // motion and release are always on root (why?)
        default:
            break;
    }

    if (((SPEventContextClass *) parent_class)->item_handler)
        ret = ((SPEventContextClass *) parent_class)->item_handler(event_context, item, event);

    return ret;
}

static gint sp_arc_context_root_handler(SPEventContext *event_context, GdkEvent *event)
{
    static gboolean dragging;

    SPDesktop *desktop = event_context->desktop;
    Inkscape::Selection *selection = SP_DT_SELECTION (desktop);

    SPArcContext *ac = SP_ARC_CONTEXT(event_context);

    event_context->tolerance = prefs_get_int_attribute_limited("options.dragtolerance", "value", 0, 0, 100);

    gint ret = FALSE;

    switch (event->type) {
        case GDK_BUTTON_PRESS:
            if (event->button.button == 1) {
                // save drag origin
                event_context->xp = (gint) event->button.x;
                event_context->yp = (gint) event->button.y;
                event_context->within_tolerance = true;

                // remember clicked item, disregarding groups, honoring Alt
                event_context->item_to_select = sp_event_context_find_item (desktop, NR::Point(event->button.x, event->button.y), event->button.state, TRUE);

                dragging = TRUE;
                /* Position center */
                ac->center = sp_desktop_w2d_xy_point(event_context->desktop,
                                                     NR::Point(event->button.x, event->button.y));
                /* Snap center to nearest magnetic point */
                namedview_free_snap(event_context->desktop->namedview, Snapper::SNAP_POINT, ac->center);
                sp_canvas_item_grab(SP_CANVAS_ITEM(desktop->acetate),
                                    GDK_KEY_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK,
                                    NULL, event->button.time);
                ret = TRUE;
            }
            break;
        case GDK_MOTION_NOTIFY:
            if (dragging && event->motion.state && GDK_BUTTON1_MASK) {

                if ( event_context->within_tolerance
                     && ( abs( (gint) event->motion.x - event_context->xp ) < event_context->tolerance )
                     && ( abs( (gint) event->motion.y - event_context->yp ) < event_context->tolerance ) ) {
                    break; // do not drag if we're within tolerance from origin
                }
                // Once the user has moved farther than tolerance from the original location
                // (indicating they intend to draw, not click), then always process the
                // motion notify coordinates as given (no snapping back to origin)
                event_context->within_tolerance = false;

                NR::Point const motion_w(event->motion.x, event->motion.y);
                NR::Point const motion_dt(sp_desktop_w2d_xy_point(event_context->desktop, motion_w));
                sp_arc_drag(ac, motion_dt, event->motion.state);
                ret = TRUE;
            }
            break;
        case GDK_BUTTON_RELEASE:
            event_context->xp = event_context->yp = 0;
            if (event->button.button == 1) {
                dragging = FALSE;
                if (!event_context->within_tolerance) {
                    // we've been dragging, finish the arc
                    sp_arc_finish(ac);
                } else if (event_context->item_to_select) {
                    // no dragging, select clicked item if any
                    if (event->button.state & GDK_SHIFT_MASK) {
                        selection->toggle(event_context->item_to_select);
                    } else {
                        selection->set(event_context->item_to_select);
                    }
                } else {
                    // click in an empty space
                    selection->clear();
                }
                event_context->xp = 0;
                event_context->yp = 0;
                event_context->item_to_select = NULL;
                ret = TRUE;
            }
            sp_canvas_item_ungrab(SP_CANVAS_ITEM(desktop->acetate), event->button.time);
            break;
        case GDK_KEY_PRESS:
            switch (get_group0_keyval (&event->key)) {
                case GDK_Alt_L:
                case GDK_Alt_R:
                case GDK_Control_L:
                case GDK_Control_R:
                case GDK_Shift_L:
                case GDK_Shift_R:
                case GDK_Meta_L:  // Meta is when you press Shift+Alt (at least on my machine)
                case GDK_Meta_R:
                    sp_event_show_modifier_tip(event_context->defaultMessageContext(), event,
                                               _("<b>Ctrl</b>: make circle or integer-ratio ellipse, snap arc/segment angle"),
                                               _("<b>Shift</b>: draw around the starting point"),
                                               NULL);
                    break;
                case GDK_Up:
                case GDK_Down:
                case GDK_KP_Up:
                case GDK_KP_Down:
                    // prevent the zoom field from activation
                    if (!MOD__CTRL_ONLY)
                        ret = TRUE;
                    break;
                case GDK_x:
                case GDK_X:
                    if (MOD__ALT_ONLY) {
                        gpointer hb = sp_search_by_data_recursive(desktop->owner->aux_toolbox, (gpointer) "altx-arc");
                        if (hb && GTK_IS_WIDGET(hb)) {
                            gtk_widget_grab_focus(GTK_WIDGET(hb));
                        }
                        ret = TRUE;
                    }
                    break;
                case GDK_Escape:
                    SP_DT_SELECTION(desktop)->clear();
                    //TODO: make dragging escapable by Esc
                default:
                    break;
            }
            break;
        case GDK_KEY_RELEASE:
            switch (event->key.keyval) {
                case GDK_Alt_L:
                case GDK_Alt_R:
                case GDK_Control_L:
                case GDK_Control_R:
                case GDK_Shift_L:
                case GDK_Shift_R:
                case GDK_Meta_L:  // Meta is when you press Shift+Alt
                case GDK_Meta_R:
                    event_context->defaultMessageContext()->clear();
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }

    if (!ret) {
        if (((SPEventContextClass *) parent_class)->root_handler)
            ret = ((SPEventContextClass *) parent_class)->root_handler(event_context, event);
    }

    return ret;
}

static void sp_arc_drag(SPArcContext *ac, NR::Point pt, guint state)
{
    SPDesktop *desktop = SP_EVENT_CONTEXT(ac)->desktop;

    if (!ac->item) {

        SPItem *layer=SP_ITEM(desktop->currentLayer());
        if ( !layer || desktop->itemIsHidden(layer)) {
            ac->_message_context->set(Inkscape::ERROR_MESSAGE, _("<b>Current layer is hidden</b>. Unhide it to be able to draw on it."));
            return;
        }
        if ( !layer || layer->isLocked()) {
            ac->_message_context->set(Inkscape::ERROR_MESSAGE, _("<b>Current layer is locked</b>. Unlock it to be able to draw on it."));
            return;
        }

        /* Create object */
        Inkscape::XML::Node *repr = sp_repr_new("svg:path");
        sp_repr_set_attr(repr, "sodipodi:type", "arc");

        /* Set style */
        sp_desktop_apply_style_tool(desktop, repr, "tools.shapes.arc", false);

        ac->item = SP_ITEM(desktop->currentLayer()->appendChildRepr(repr));
        sp_repr_unref(repr);
        ac->item->transform = SP_ITEM(desktop->currentRoot())->getRelativeTransform(desktop->currentLayer());
        ac->item->updateRepr();
    }

    /* This is bit ugly, but so we are */

    NR::Point p0, p1;
    if (state & GDK_CONTROL_MASK) {
        NR::Point delta = pt - ac->center;
        /* fixme: Snapping */
        if ((fabs(delta[0]) > fabs(delta[1])) && (delta[1] != 0.0)) {
            delta[0] = floor(delta[0]/delta[1] + 0.5) * delta[1];
        } else if (delta[0] != 0.0) {
            delta[1] = floor(delta[1]/delta[0] + 0.5) * delta[0];
        }
        p1 = ac->center + delta;
        if (state & GDK_SHIFT_MASK) {
            p0 = ac->center - delta;
            const NR::Coord l0 = namedview_vector_snap(desktop->namedview, Snapper::SNAP_POINT, p0, p0 - p1);
            const NR::Coord l1 = namedview_vector_snap(desktop->namedview, Snapper::SNAP_POINT, p1, p1 - p0);

            if (l0 < l1) {
                p1 = 2 * ac->center - p0;
            } else {
                p0 = 2 * ac->center - p1;
            }
        } else {
            p0 = ac->center;
            namedview_vector_snap(desktop->namedview, Snapper::SNAP_POINT, p1, p1 - p0);
        }
    } else if (state & GDK_SHIFT_MASK) {
        /* Corner point movements are bound */
        p1 = pt;
        p0 = 2 * ac->center - p1;
        for (int d = 0 ; d < 2 ; ++d) {
            NR::Coord snap_movement[2];
            snap_movement[0] = namedview_dim_snap(desktop->namedview, Snapper::SNAP_POINT, p0, NR::Dim2(d));
            snap_movement[1] = namedview_dim_snap(desktop->namedview, Snapper::SNAP_POINT, p1, NR::Dim2(d));
            if ( snap_movement[0] <
                 snap_movement[1] ) {
                /* Use point 0 position. */
                p1[d] = 2 * ac->center[d] - p0[d];
            } else {
                p0[d] = 2 * ac->center[d] - p1[d];
            }
        }
    } else {
        /* Free movement for corner point */
        p0 = ac->center;
        p1 = pt;
        namedview_free_snap(desktop->namedview, Snapper::SNAP_POINT, p1);
    }

    p0 = sp_desktop_dt2root_xy_point(desktop, p0);
    p1 = sp_desktop_dt2root_xy_point(desktop, p1);

    // FIXME: use NR::Rect
    const NR::Coord x0 = MIN(p0[NR::X], p1[NR::X]);
    const NR::Coord y0 = MIN(p0[NR::Y], p1[NR::Y]);
    const NR::Coord x1 = MAX(p0[NR::X], p1[NR::X]);
    const NR::Coord y1 = MAX(p0[NR::Y], p1[NR::Y]);

    sp_arc_position_set(SP_ARC(ac->item), (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2);

    GString *xs = SP_PX_TO_METRIC_STRING(fabs(x1-x0), sp_desktop_get_default_metric(desktop));
    GString *ys = SP_PX_TO_METRIC_STRING(fabs(y1-y0), sp_desktop_get_default_metric(desktop));
    ac->_message_context->setF(Inkscape::NORMAL_MESSAGE, _("<b>Ellipse</b>: %s x %s; with <b>Ctrl</b> to make circle or integer-ratio ellipse; with <b>Shift</b> to draw around the starting point"), xs->str, ys->str);
    g_string_free(xs, FALSE);
    g_string_free(ys, FALSE);
}

static void sp_arc_finish(SPArcContext *ac)
{
    ac->_message_context->clear();

    if (ac->item != NULL) {
        SPDesktop *desktop = SP_EVENT_CONTEXT(ac)->desktop;

        SP_OBJECT(ac->item)->updateRepr();

        SP_DT_SELECTION(desktop)->set(ac->item);
        sp_document_done(SP_DT_DOCUMENT(desktop));

        ac->item = NULL;
    }
}


/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=99 :
