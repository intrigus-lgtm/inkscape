// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Connector creation tool
 *
 * Authors:
 *   Michael Wybrow <mjwybrow@users.sourceforge.net>
 *   Abhishek Sharma
 *   Jon A. Cruz <jon@joncruz.org>
 *   Martin Owens <doctormo@gmail.com>
 *
 * Copyright (C) 2005-2008  Michael Wybrow
 * Copyright (C) 2009  Monash University
 * Copyright (C) 2012  Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 *
 * TODO:
 *  o  Show a visual indicator for objects with the 'avoid' property set.
 *  o  Allow user to change a object between a path and connector through
 *     the interface.
 *  o  Create an interface for setting markers (arrow heads).
 *  o  Better distinguish between paths and connectors to prevent problems
 *     in the node tool and paths accidentally being turned into connectors
 *     in the connector tool.  Perhaps have a way to convert between.
 *  o  Only call libavoid's updateEndPoint as required.  Currently we do it
 *     for both endpoints, even if only one is moving.
 *  o  Deal sanely with connectors with both endpoints attached to the
 *     same connection point, and drawing of connectors attaching
 *     overlapping shapes (currently tries to adjust connector to be
 *     outside both bounding boxes).
 *  o  Fix many special cases related to connectors updating,
 *     e.g., copying a couple of shapes and a connector that are
 *           attached to each other.
 *     e.g., detach connector when it is moved or transformed in
 *           one of the other contexts.
 *  o  Cope with shapes whose ids change when they have attached
 *     connectors.
 *  o  During dragging motion, gobble up to and use the final motion event.
 *     Gobbling away all duplicates after the current can occasionally result
 *     in the path lagging behind the mouse cursor if it is no longer being
 *     dragged.
 *  o  Fix up libavoid's representation after undo actions.  It doesn't see
 *     any transform signals and hence doesn't know shapes have moved back to
 *     there earlier positions.
 *
 * ----------------------------------------------------------------------------
 *
 * Notes:
 *
 *  Much of the way connectors work for user-defined points has been
 *  changed so that it no longer defines special attributes to record
 *  the points. Instead it uses single node paths to define points
 *  who are then separate objects that can be fixed on the canvas,
 *  grouped into objects and take full advantage of all transform, snap
 *  and align functionality of all other objects.
 *
 *     I think that the style change between polyline and orthogonal
 *     would be much clearer with two buttons (radio behaviour -- just
 *     one is true).
 *
 *     The other tools show a label change from "New:" to "Change:"
 *     depending on whether an object is selected.  We could consider
 *     this but there may not be space.
 *
 *     Likewise for the avoid/ignore shapes buttons.  These should be
 *     inactive when a shape is not selected in the connector context.
 *
 */

#include "connector-tool.h"

#include <string>
#include <cstring>

#include <glibmm/i18n.h>
#include <glibmm/stringutils.h>
#include <gdk/gdkkeysyms.h>

#include "context-fns.h"
#include "desktop-style.h"
#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "message-context.h"
#include "message-stack.h"
#include "selection.h"
#include "snap.h"

#include "display/control/canvas-item-bpath.h"
#include "display/control/canvas-item-ctrl.h"
#include "display/curve.h"

#include "3rdparty/adaptagrams/libavoid/router.h"

#include "object/sp-conn-end.h"
#include "object/sp-flowtext.h"
#include "object/sp-namedview.h"
#include "object/sp-path.h"
#include "object/sp-text.h"
#include "object/sp-use.h"

#include "ui/icon-names.h"
#include "ui/knot/knot.h"
#include "ui/widget/canvas.h"  // Enter events hack
#include "ui/widget/events/canvas-event.h"

#include "xml/node.h"

#include "svg/svg.h"

namespace Inkscape::UI::Tools {

void CCToolShapeNodeObserver::notifyAttributeChanged(Inkscape::XML::Node &repr, GQuark name_, Util::ptr_shared, Util::ptr_shared)
{
    auto tool = static_cast<ConnectorTool*>(this);

    auto const name = g_quark_to_string(name_);
    // Look for changes that result in onscreen movement.
    if (!strcmp(name, "d") || !strcmp(name, "x") || !strcmp(name, "y") ||
        !strcmp(name, "width") || !strcmp(name, "height") ||
        !strcmp(name, "transform")) {
        if (&repr == tool->active_shape_repr) {
            // Active shape has moved. Clear active shape.
            tool->cc_clear_active_shape();
        } else if (&repr == tool->active_conn_repr) {
            // The active conn has been moved.
            // Set it again, which just sets new handle positions.
            tool->cc_set_active_conn(tool->active_conn);
        }
    }
}

void CCToolLayerNodeObserver::notifyChildRemoved(Inkscape::XML::Node&, Inkscape::XML::Node &child, Inkscape::XML::Node*)
{
    auto tool = static_cast<ConnectorTool*>(this);

    if (&child == tool->active_shape_repr) {
        // The active shape has been deleted. Clear active shape.
        tool->cc_clear_active_shape();
    }
}

using Inkscape::DocumentUndo;

static void cc_clear_active_knots(SPKnotList k);

static void cc_select_handle(SPKnot* knot);
static void cc_deselect_handle(SPKnot* knot);
static bool cc_item_is_shape(SPItem *item);

/*static Geom::Point connector_drag_origin_w(0, 0);
static bool connector_within_tolerance = false;*/

ConnectorTool::ConnectorTool(SPDesktop *desktop)
    : ToolBase(desktop, "/tools/connector", "connector.svg")
    , state {SP_CONNECTOR_CONTEXT_IDLE}
{
    this->selection = desktop->getSelection();

    this->sel_changed_connection.disconnect();
    this->sel_changed_connection = this->selection->connectChanged(
        sigc::mem_fun(*this, &ConnectorTool::_selectionChanged)
    );

    /* Create red bpath */
    red_bpath = new Inkscape::CanvasItemBpath(desktop->getCanvasSketch());
    red_bpath->set_stroke(red_color);
    red_bpath->set_fill(0x0, SP_WIND_RULE_NONZERO);

    /* Create red curve */
    red_curve.emplace();

    /* Create green curve */
    green_curve.emplace();

    // Notice the initial selection.
    //cc_selection_changed(this->selection, (gpointer) this);
    this->_selectionChanged(this->selection);

    this->within_tolerance = false;

    sp_event_context_read(this, "curvature");
    sp_event_context_read(this, "orthogonal");
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (prefs->getBool("/tools/connector/selcue", false)) {
        this->enableSelectionCue();
    }

    // Make sure we see all enter events for canvas items,
    // even if a mouse button is depressed.
    desktop->getCanvas()->set_all_enter_events(true);
}

ConnectorTool::~ConnectorTool()
{
    this->_finish();
    this->state = SP_CONNECTOR_CONTEXT_IDLE;

    if (this->selection) {
        this->selection = nullptr;
    }

    this->cc_clear_active_shape();
    this->cc_clear_active_conn();

    // Restore the default event generating behaviour.
    _desktop->getCanvas()->set_all_enter_events(false);

    this->sel_changed_connection.disconnect();

    for (auto &i : endpt_handle) {
        if (i) {
            SPKnot::unref(i);
            i = nullptr;
        }
    }

    if (this->shref) {
        g_free(this->shref);
        this->shref = nullptr;
    }

    if (this->ehref) {
        g_free(this->shref);
        this->shref = nullptr;
    }

    g_assert(this->newConnRef == nullptr);
}

void ConnectorTool::set(const Inkscape::Preferences::Entry &val)
{
    /* fixme: Proper error handling for non-numeric data.  Use a locale-independent function like
     * g_ascii_strtod (or a thin wrapper that does the right thing for invalid values inf/nan). */
    Glib::ustring name = val.getEntryName();

    if (name == "curvature") {
        this->curvature = val.getDoubleLimited(); // prevents NaN and +/-Inf from messing up
    } else if (name == "orthogonal") {
        this->isOrthogonal = val.getBool();
    }
}

//-----------------------------------------------------------------------------


void ConnectorTool::cc_clear_active_shape()
{
    if (this->active_shape == nullptr) {
        return;
    }
    g_assert( this->active_shape_repr );
    g_assert( this->active_shape_layer_repr );

    this->active_shape = nullptr;

    if (this->active_shape_repr) {
        this->active_shape_repr->removeObserver(shapeNodeObserver());
        Inkscape::GC::release(this->active_shape_repr);
        this->active_shape_repr = nullptr;

        this->active_shape_layer_repr->removeObserver(layerNodeObserver());
        Inkscape::GC::release(this->active_shape_layer_repr);
        this->active_shape_layer_repr = nullptr;
    }

    cc_clear_active_knots(this->knots);
}

static void cc_clear_active_knots(SPKnotList k)
{
    // Hide the connection points if they exist.
    if (k.size()) {
        for (auto & it : k) {
            it.first->hide();
        }
    }
}

void ConnectorTool::cc_clear_active_conn()
{
    if (this->active_conn == nullptr) {
        return;
    }
    g_assert( this->active_conn_repr );

    this->active_conn = nullptr;

    if (this->active_conn_repr) {
        this->active_conn_repr->removeObserver(shapeNodeObserver());
        Inkscape::GC::release(this->active_conn_repr);
        this->active_conn_repr = nullptr;
    }

    // Hide the endpoint handles.
    for (auto & i : this->endpt_handle) {
        if (i) {
            i->hide();
        }
    }
}


bool ConnectorTool::_ptHandleTest(Geom::Point& p, gchar **href, gchar **subhref)
{
    if (this->active_handle && (this->knots.find(this->active_handle) != this->knots.end())) {
        p = this->active_handle->pos;
        *href = g_strdup_printf("#%s", this->active_handle->owner->getId());
        if(this->active_handle->sub_owner) {
            auto id = this->active_handle->sub_owner->getAttribute("id");
            if(id) {
                *subhref = g_strdup_printf("#%s", id);
            }
        } else {
            *subhref = nullptr;
        }
        return true;
    }
    *href = nullptr;
    *subhref = nullptr;
    return false;
}

static void cc_select_handle(SPKnot* knot)
{
    knot->ctrl->set_selected(true);
    knot->setSize(HandleSize::LARGE);
    knot->setAnchor(SP_ANCHOR_CENTER);
    knot->updateCtrl();
}

static void cc_deselect_handle(SPKnot* knot)
{
    knot->ctrl->set_selected(false);
    knot->setSize(HandleSize::NORMAL);
    knot->setAnchor(SP_ANCHOR_CENTER);
    knot->updateCtrl();
}

bool ConnectorTool::item_handler(SPItem *item, CanvasEvent const &event)
{
    bool ret = false;

    inspect_event(event,
    [&] (ButtonReleaseEvent const &event) {
        if (event.button == 1) {
            if ((this->state == SP_CONNECTOR_CONTEXT_DRAGGING) && this->within_tolerance) {
                this->_resetColors();
                this->state = SP_CONNECTOR_CONTEXT_IDLE;
            }

            if (this->state != SP_CONNECTOR_CONTEXT_IDLE) {
                // Doing something else like rerouting.
                return;
            }

            // find out clicked item, honoring Alt
            auto const item = sp_event_context_find_item(_desktop, event.pos, event.modifiers & GDK_ALT_MASK, false);

            if (event.modifiers & GDK_SHIFT_MASK) {
                this->selection->toggle(item);
            } else {
                this->selection->set(item);
                /* When selecting a new item, do not allow showing
                   connection points on connectors. (yet?)
                */

                if (item != this->active_shape && !cc_item_is_connector(item)) {
                    this->_setActiveShape(item);
                }
            }

            ret = true;
        }
    },
    [&] (MotionEvent const &event) {
        auto const item = _desktop->getItemAtPoint(event.pos, false);
        if (cc_item_is_shape(item)) {
            _setActiveShape(item);
        }
    },
    [&] (CanvasEvent const &event) {}
    );

    return ret;
}

bool ConnectorTool::root_handler(CanvasEvent const &event)
{
    bool ret = false;

    inspect_event(event,
    [&] (ButtonPressEvent const &event) {
        if (event.num_press == 1) {
            ret = _handleButtonPress(event);
        }
    },
    [&] (MotionEvent const &event) {
        ret = _handleMotionNotify(event);
    },
    [&] (ButtonReleaseEvent const &event) {
        ret = _handleButtonRelease(event);
    },
    [&] (KeyPressEvent const &event) {
        ret = _handleKeyPress(get_latin_keyval(event));
    },
    [&] (CanvasEvent const &event) {}
    );

    return ret || ToolBase::root_handler(event);
}

bool ConnectorTool::_handleButtonPress(ButtonPressEvent const &bevent)
{
    Geom::Point const event_w = bevent.pos;
    /* Find desktop coordinates */
    Geom::Point p = _desktop->w2d(event_w);

    bool ret = false;

    if (bevent.button == 1) {
        if (Inkscape::have_viable_layer(_desktop, defaultMessageContext()) == false) {
            return true;
        }

        auto const event_w = bevent.pos;

        saveDragOrigin(event_w);

        Geom::Point const event_dt = _desktop->w2d(event_w);

        SnapManager &m = _desktop->getNamedView()->snap_manager;

        switch (this->state) {
        case SP_CONNECTOR_CONTEXT_STOP:

            /* This is allowed, if we just canceled curve */
        case SP_CONNECTOR_CONTEXT_IDLE:
        {
            if ( this->npoints == 0 ) {
                this->cc_clear_active_conn();

                _desktop->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("Creating new connector"));

                /* Set start anchor */
                /* Create green anchor */
                Geom::Point p = event_dt;

                // Test whether we clicked on a connection point
                bool found = this->_ptHandleTest(p, &this->shref, &this->sub_shref);

                if (!found) {
                    // This is the first point, so just snap it to the grid
                    // as there's no other points to go off.
                    m.setup(_desktop);
                    m.freeSnapReturnByRef(p, Inkscape::SNAPSOURCE_OTHER_HANDLE);
                    m.unSetup();
                }
                this->_setInitialPoint(p);

            }
            this->state = SP_CONNECTOR_CONTEXT_DRAGGING;
            ret = true;
            break;
        }
        case SP_CONNECTOR_CONTEXT_DRAGGING:
        {
            // This is the second click of a connector creation.
            m.setup(_desktop);
            m.freeSnapReturnByRef(p, Inkscape::SNAPSOURCE_OTHER_HANDLE);
            m.unSetup();

            this->_setSubsequentPoint(p);
            this->_finishSegment(p);

            this->_ptHandleTest(p, &this->ehref, &this->sub_ehref);
            if (this->npoints != 0) {
                this->_finish();
            }
            this->cc_set_active_conn(this->newconn);
            this->state = SP_CONNECTOR_CONTEXT_IDLE;
            ret = true;
            break;
        }
        case SP_CONNECTOR_CONTEXT_CLOSE:
        {
            g_warning("Button down in CLOSE state");
            break;
        }
        default:
            break;
        }
    } else if (bevent.button == 3) {
        if (this->state == SP_CONNECTOR_CONTEXT_REROUTING) {
            // A context menu is going to be triggered here,
            // so end the rerouting operation.
            this->_reroutingFinish(&p);

            this->state = SP_CONNECTOR_CONTEXT_IDLE;

            // Don't set ret to TRUE, so we drop through to the
            // parent handler which will open the context menu.
        } else if (this->npoints != 0) {
            this->_finish();
            this->state = SP_CONNECTOR_CONTEXT_IDLE;
            ret = true;
        }
    }

    return ret;
}

bool ConnectorTool::_handleMotionNotify(MotionEvent const &mevent)
{
    bool ret = false;

    if (mevent.modifiers & (GDK_BUTTON2_MASK | GDK_BUTTON3_MASK)) {
        // allow middle-button scrolling
        return false;
    }

    auto const event_w = mevent.pos;

    if (!checkDragMoved(event_w)) {
        return false;
    }

    // Find desktop coordinates.
    Geom::Point p = _desktop->w2d(event_w);

    SnapManager &m = _desktop->getNamedView()->snap_manager;

    switch (this->state) {
    case SP_CONNECTOR_CONTEXT_DRAGGING:
    {
        gobble_motion_events(mevent.modifiers);
        // This is movement during a connector creation.
        if ( this->npoints > 0 ) {
            m.setup(_desktop);
            m.freeSnapReturnByRef(p, Inkscape::SNAPSOURCE_OTHER_HANDLE);
            m.unSetup();
            this->selection->clear();
            this->_setSubsequentPoint(p);
            ret = true;
        }
        break;
    }
    case SP_CONNECTOR_CONTEXT_REROUTING:
    {
        gobble_motion_events(GDK_BUTTON1_MASK);
        g_assert(is<SPPath>(clickeditem));

        m.setup(_desktop);
        m.freeSnapReturnByRef(p, Inkscape::SNAPSOURCE_OTHER_HANDLE);
        m.unSetup();

        // Update the hidden path
        auto i2d = clickeditem->i2dt_affine();
        auto d2i = i2d.inverse();
        auto path = cast<SPPath>(clickeditem);
        auto curve = *path->curve();
        if (clickedhandle == endpt_handle[0]) {
            auto o = endpt_handle[1]->pos;
            curve.stretch_endpoints(p * d2i, o * d2i);
        } else {
            auto o = endpt_handle[0]->pos;
            curve.stretch_endpoints(o * d2i, p * d2i);
        }
        path->setCurve(std::move(curve));
        sp_conn_reroute_path_immediate(path);

        // Copy this to the temporary visible path
        red_curve = path->curveForEdit()->transformed(i2d);
        red_bpath->set_bpath(&*red_curve);

        ret = true;
        break;
    }
    case SP_CONNECTOR_CONTEXT_STOP:
        /* This is perfectly valid */
        break;
    default:
        if (!this->sp_event_context_knot_mouseover()) {
            m.setup(_desktop);
            m.preSnap(Inkscape::SnapCandidatePoint(p, Inkscape::SNAPSOURCE_OTHER_HANDLE));
            m.unSetup();
        }
        break;
    }
    return ret;
}

bool ConnectorTool::_handleButtonRelease(ButtonReleaseEvent const &revent)
{
    bool ret = false;

    if (revent.button == 1) {
        SPDocument *doc = _desktop->getDocument();
        SnapManager &m = _desktop->getNamedView()->snap_manager;

        auto const event_w = revent.pos;

        // Find desktop coordinates.
        Geom::Point p = _desktop->w2d(event_w);

        switch (this->state) {
        //case SP_CONNECTOR_CONTEXT_POINT:
        case SP_CONNECTOR_CONTEXT_DRAGGING:
        {
            m.setup(_desktop);
            m.freeSnapReturnByRef(p, Inkscape::SNAPSOURCE_OTHER_HANDLE);
            m.unSetup();

            if (this->within_tolerance) {
                this->_finishSegment(p);
                return true;
            }
            // Connector has been created via a drag, end it now.
            this->_setSubsequentPoint(p);
            this->_finishSegment(p);
            // Test whether we clicked on a connection point
            this->_ptHandleTest(p, &this->ehref, &this->sub_ehref);
            if (this->npoints != 0) {
                this->_finish();
            }
            this->cc_set_active_conn(this->newconn);
            this->state = SP_CONNECTOR_CONTEXT_IDLE;
            break;
        }
        case SP_CONNECTOR_CONTEXT_REROUTING:
        {
            m.setup(_desktop);
            m.freeSnapReturnByRef(p, Inkscape::SNAPSOURCE_OTHER_HANDLE);
            m.unSetup();
            this->_reroutingFinish(&p);

            doc->ensureUpToDate();
            this->state = SP_CONNECTOR_CONTEXT_IDLE;
            return true;
            break;
        }
        case SP_CONNECTOR_CONTEXT_STOP:
            /* This is allowed, if we just cancelled curve */
            break;
        default:
            break;
        }
        ret = true;
    }
    return ret;
}

bool ConnectorTool::_handleKeyPress(guint const keyval)
{
    bool ret = false;

    switch (keyval) {
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
        if (this->npoints != 0) {
            this->_finish();
            this->state = SP_CONNECTOR_CONTEXT_IDLE;
            ret = true;
        }
        break;
    case GDK_KEY_Escape:
        if (this->state == SP_CONNECTOR_CONTEXT_REROUTING) {
            SPDocument *doc = _desktop->getDocument();

            this->_reroutingFinish(nullptr);

            DocumentUndo::undo(doc);

            this->state = SP_CONNECTOR_CONTEXT_IDLE;
            _desktop->messageStack()->flash( Inkscape::NORMAL_MESSAGE,
                    _("Connector endpoint drag cancelled."));
            ret = true;
        } else if (this->npoints != 0) {
            // if drawing, cancel, otherwise pass it up for deselecting
            this->state = SP_CONNECTOR_CONTEXT_STOP;
            this->_resetColors();
            ret = true;
        }
        break;
    default:
        break;
    }
    return ret;
}

void ConnectorTool::_reroutingFinish(Geom::Point *const p)
{
    SPDocument *doc = _desktop->getDocument();

    // Clear the temporary path:
    this->red_curve->reset();
    red_bpath->set_bpath(nullptr);

    if (p != nullptr) {
        // Test whether we clicked on a connection point
        gchar *shape_label;
        gchar *sub_label;
        bool found = this->_ptHandleTest(*p, &shape_label, &sub_label);

        if (found) {
            if (this->clickedhandle == this->endpt_handle[0]) {
                this->clickeditem->setAttribute("inkscape:connection-start", shape_label);
                this->clickeditem->setAttribute("inkscape:connection-start-point", sub_label);
            } else {
                this->clickeditem->setAttribute("inkscape:connection-end", shape_label);
                this->clickeditem->setAttribute("inkscape:connection-end-point", sub_label);
            }
            g_free(shape_label);
            if(sub_label) {
                g_free(sub_label);
            }
        }
    }
    this->clickeditem->setHidden(false);
    sp_conn_reroute_path_immediate(cast<SPPath>(this->clickeditem));
    this->clickeditem->updateRepr();
    DocumentUndo::done(doc, _("Reroute connector"), INKSCAPE_ICON("draw-connector"));
    this->cc_set_active_conn(this->clickeditem);
}


void ConnectorTool::_resetColors()
{
    /* Red */
    this->red_curve->reset();
    red_bpath->set_bpath(nullptr);

    this->green_curve->reset();
    this->npoints = 0;
}

void ConnectorTool::_setInitialPoint(Geom::Point const p)
{
    g_assert( this->npoints == 0 );

    this->p[0] = p;
    this->p[1] = p;
    this->npoints = 2;
    red_bpath->set_bpath(nullptr);
}

void ConnectorTool::_setSubsequentPoint(Geom::Point const p)
{
    g_assert( this->npoints != 0 );

    Geom::Point o = _desktop->dt2doc(this->p[0]);
    Geom::Point d = _desktop->dt2doc(p);
    Avoid::Point src(o[Geom::X], o[Geom::Y]);
    Avoid::Point dst(d[Geom::X], d[Geom::Y]);

    if (!this->newConnRef) {
        Avoid::Router *router = _desktop->getDocument()->getRouter();
        this->newConnRef = new Avoid::ConnRef(router);
        this->newConnRef->setEndpoint(Avoid::VertID::src, src);
        if (this->isOrthogonal) {
            this->newConnRef->setRoutingType(Avoid::ConnType_Orthogonal);
        } else {
            this->newConnRef->setRoutingType(Avoid::ConnType_PolyLine);
        }
    }
    // Set new endpoint.
    this->newConnRef->setEndpoint(Avoid::VertID::tar, dst);
    // Immediately generate new routes for connector.
    this->newConnRef->makePathInvalid();
    this->newConnRef->router()->processTransaction();
    // Recreate curve from libavoid route.
    red_curve = SPConnEndPair::createCurve(newConnRef, curvature);
    red_curve->transform(_desktop->doc2dt());
    red_bpath->set_bpath(&*red_curve, true);
}


/**
 * Concats red, blue and green.
 * If any anchors are defined, process these, optionally removing curves from white list
 * Invoke _flush_white to write result back to object.
 */
void ConnectorTool::_concatColorsAndFlush()
{
    auto c = std::make_optional<SPCurve>();
    std::swap(c, green_curve);

    red_curve->reset();
    red_bpath->set_bpath(nullptr);

    if (c->is_empty()) {
        return;
    }

    _flushWhite(&*c);
}


/*
 * Flushes white curve(s) and additional curve into object
 *
 * No cleaning of colored curves - this has to be done by caller
 * No rereading of white data, so if you cannot rely on ::modified, do it in caller
 *
 */

void ConnectorTool::_flushWhite(SPCurve *c)
{
    /* Now we have to go back to item coordinates at last */
    c->transform(_desktop->dt2doc());

    SPDocument *doc = _desktop->getDocument();
    Inkscape::XML::Document *xml_doc = doc->getReprDoc();

    if ( !c->is_empty() ) {
        /* We actually have something to write */

        Inkscape::XML::Node *repr = xml_doc->createElement("svg:path");
        /* Set style */
        sp_desktop_apply_style_tool(_desktop, repr, "/tools/connector", false);

        repr->setAttribute("d", sp_svg_write_path(c->get_pathvector()));

        /* Attach repr */
        auto layer = currentLayer();
        this->newconn = cast<SPItem>(layer->appendChildRepr(repr));
        this->newconn->transform = layer->i2doc_affine().inverse();

        bool connection = false;
        this->newconn->setAttribute( "inkscape:connector-type",
                                   this->isOrthogonal ? "orthogonal" : "polyline");
        this->newconn->setAttribute( "inkscape:connector-curvature",
                                   Glib::Ascii::dtostr(this->curvature).c_str());
        if (this->shref) {
            connection = true;
            this->newconn->setAttribute( "inkscape:connection-start", this->shref);
            if(this->sub_shref) {
                this->newconn->setAttribute( "inkscape:connection-start-point", this->sub_shref);
            }
        }

        if (this->ehref) {
            connection = true;
            this->newconn->setAttribute( "inkscape:connection-end", this->ehref);
            if(this->sub_ehref) {
                this->newconn->setAttribute( "inkscape:connection-end-point", this->sub_ehref);
            }
        }
        // Process pending updates.
        this->newconn->updateRepr();
        doc->ensureUpToDate();

        if (connection) {
            // Adjust endpoints to shape edge.
            sp_conn_reroute_path_immediate(cast<SPPath>(this->newconn));
            this->newconn->updateRepr();
        }

        this->newconn->doWriteTransform(this->newconn->transform, nullptr, true);

        // Only set the selection after we are finished with creating the attributes of
        // the connector.  Otherwise, the selection change may alter the defaults for
        // values like curvature in the connector context, preventing subsequent lookup
        // of their original values.
        this->selection->set(repr);
        Inkscape::GC::release(repr);
    }

    DocumentUndo::done(doc, _("Create connector"), INKSCAPE_ICON("draw-connector"));
}


void ConnectorTool::_finishSegment(Geom::Point const /*p*/)
{
    if (!this->red_curve->is_empty()) {
        green_curve->append_continuous(*red_curve);

        this->p[0] = this->p[3];
        this->p[1] = this->p[4];
        this->npoints = 2;

        this->red_curve->reset();
    }
}

void ConnectorTool::_finish()
{
    _desktop->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("Finishing connector"));

    this->red_curve->reset();
    this->_concatColorsAndFlush();

    this->npoints = 0;

    if (this->newConnRef) {
        this->newConnRef->router()->deleteConnector(this->newConnRef);
        this->newConnRef = nullptr;
    }
}


static bool cc_generic_knot_handler(CanvasEvent const &event, SPKnot *knot)
{
    g_assert(knot);

    SPKnot::ref(knot);

    auto cc = SP_CONNECTOR_CONTEXT(knot->desktop->getTool());

    bool consumed = false;

    inspect_event(event,
    [&] (EnterEvent const &event) {
        knot->setFlag(SP_KNOT_MOUSEOVER, true);

        cc->active_handle = knot;
        knot->desktop->getTool()->defaultMessageContext()->set(Inkscape::NORMAL_MESSAGE, _("Click to join at this point"));

        consumed = true;
    },
    [&] (LeaveEvent const &event) {
        knot->setFlag(SP_KNOT_MOUSEOVER, false);

        /* FIXME: the following test is a workaround for LP Bug #1273510.
         * It seems that a signal is not correctly disconnected, maybe
         * something missing in cc_clear_active_conn()? */
        if (cc) {
            cc->active_handle = nullptr;
        }

        knot->desktop->getTool()->defaultMessageContext()->clear();

        consumed = true;
    },
    [&] (CanvasEvent const &event) {}
    );

    SPKnot::unref(knot);

    return consumed;
}

static bool endpt_handler(CanvasEvent const &event, ConnectorTool *cc)
{
    bool consumed = false;

    inspect_event(event,
    [&] (ButtonPressEvent const &event) {
        g_assert( (cc->active_handle == cc->endpt_handle[0]) ||
                  (cc->active_handle == cc->endpt_handle[1]) );
        if (cc->state == SP_CONNECTOR_CONTEXT_IDLE) {
            cc->clickeditem = cc->active_conn;
            cc->clickedhandle = cc->active_handle;
            cc->cc_clear_active_conn();
            cc->state = SP_CONNECTOR_CONTEXT_REROUTING;

            // Disconnect from attached shape
            unsigned ind = (cc->active_handle == cc->endpt_handle[0]) ? 0 : 1;
            sp_conn_end_detach(cc->clickeditem, ind);

            Geom::Point origin;
            if (cc->clickedhandle == cc->endpt_handle[0]) {
                origin = cc->endpt_handle[1]->pos;
            } else {
                origin = cc->endpt_handle[0]->pos;
            }

            // Show the red path for dragging.
            auto path = static_cast<SPPath const *>(cc->clickeditem);
            cc->red_curve = path->curveForEdit()->transformed(cc->clickeditem->i2dt_affine());
            cc->red_bpath->set_bpath(&*cc->red_curve, true);

            cc->clickeditem->setHidden(true);

            // The rest of the interaction rerouting the connector is
            // handled by the context root handler.
            consumed = true;
        }
    },
    [&] (CanvasEvent const &event) {}
    );

    return consumed;
}

void ConnectorTool::_activeShapeAddKnot(SPItem* item, SPItem* subitem)
{
    SPKnot *knot = new SPKnot(_desktop, "", Inkscape::CANVAS_ITEM_CTRL_TYPE_SHAPER, "CanvasItemCtrl:ConnectorTool:Shape");
    knot->owner = item;

    if (subitem) {
        auto use = cast<SPUse>(item);
        g_assert(use != nullptr);
        knot->sub_owner = subitem;
        knot->setSize(HandleSize::LARGE);
        knot->setAnchor(SP_ANCHOR_CENTER);

        // Set the point to the middle of the sub item
        knot->setPosition(subitem->getAvoidRef().getConnectionPointPos() * _desktop->doc2dt(), 0);
    } else {
        knot->setSize(HandleSize::NORMAL);
        knot->setAnchor(SP_ANCHOR_CENTER);
        // Set the point to the middle of the object
        knot->setPosition(item->getAvoidRef().getConnectionPointPos() * _desktop->doc2dt(), 0);
    }

    knot->updateCtrl();

    // We don't want to use the standard knot handler.
    knot->_event_connection.disconnect();
    knot->_event_connection =
        knot->ctrl->connect_event(sigc::bind(sigc::ptr_fun(cc_generic_knot_handler), knot));

    knot->show();
    this->knots[knot] = 1;
}

void ConnectorTool::_setActiveShape(SPItem *item)
{
    g_assert(item != nullptr );

    if (this->active_shape != item) {
        // The active shape has changed
        // Rebuild everything
        this->active_shape = item;
        // Remove existing active shape listeners
        if (this->active_shape_repr) {
            this->active_shape_repr->removeObserver(shapeNodeObserver());
            Inkscape::GC::release(this->active_shape_repr);

            this->active_shape_layer_repr->removeObserver(layerNodeObserver());
            Inkscape::GC::release(this->active_shape_layer_repr);
        }

        // Listen in case the active shape changes
        this->active_shape_repr = item->getRepr();
        if (this->active_shape_repr) {
            Inkscape::GC::anchor(this->active_shape_repr);
            this->active_shape_repr->addObserver(shapeNodeObserver());

            this->active_shape_layer_repr = this->active_shape_repr->parent();
            Inkscape::GC::anchor(this->active_shape_layer_repr);
            this->active_shape_layer_repr->addObserver(layerNodeObserver());
        }

        cc_clear_active_knots(this->knots);

        // The idea here is to try and add a group's children to solidify
        // connection handling. We react to path objects with only one node.
        for (auto& child: item->children) {
            if(child.getAttribute("inkscape:connector")) {
                this->_activeShapeAddKnot((SPItem *) &child, nullptr);
            }
        }
        // Special connector points in a symbol
        if (auto use = cast<SPUse>(item)) {
            SPItem *orig = use->root();
            //SPItem *orig = use->get_original();
            for (auto& child: orig->children) {
                if(child.getAttribute("inkscape:connector")) {
                    this->_activeShapeAddKnot(item, (SPItem *) &child);
                }
            }
        }
        // Center point to any object
        this->_activeShapeAddKnot(item, nullptr);

    } else {
        // Ensure the item's connection_points map
        // has been updated
        item->document->ensureUpToDate();
    }
}

void ConnectorTool::cc_set_active_conn(SPItem *item)
{
    g_assert( is<SPPath>(item) );

    const SPCurve *curve = cast<SPPath>(item)->curveForEdit();
    Geom::Affine i2dt = item->i2dt_affine();

    if (this->active_conn == item) {
        if (curve->is_empty()) {
            // Connector is invisible because it is clipped to the boundary of
            // two overlapping shapes.
            this->endpt_handle[0]->hide();
            this->endpt_handle[1]->hide();
        } else {
            // Just adjust handle positions.
            Geom::Point startpt = *(curve->first_point()) * i2dt;
            this->endpt_handle[0]->setPosition(startpt, 0);

            Geom::Point endpt = *(curve->last_point()) * i2dt;
            this->endpt_handle[1]->setPosition(endpt, 0);
        }

        return;
    }

    this->active_conn = item;

    // Remove existing active conn listeners
    if (this->active_conn_repr) {
        this->active_conn_repr->removeObserver(shapeNodeObserver());
        Inkscape::GC::release(this->active_conn_repr);
        this->active_conn_repr = nullptr;
    }

    // Listen in case the active conn changes
    this->active_conn_repr = item->getRepr();
    if (this->active_conn_repr) {
        Inkscape::GC::anchor(this->active_conn_repr);
        this->active_conn_repr->addObserver(shapeNodeObserver());
    }

    for (int i = 0; i < 2; ++i) {
        // Create the handle if it doesn't exist
        if ( this->endpt_handle[i] == nullptr ) {
            SPKnot *knot = new SPKnot(_desktop,
                                      _("<b>Connector endpoint</b>: drag to reroute or connect to new shapes"),
                                      Inkscape::CANVAS_ITEM_CTRL_TYPE_SHAPER, "CanvasItemCtrl:ConnectorTool:Endpoint");

            knot->setSize(HandleSize::SMALL);
            knot->setAnchor(SP_ANCHOR_CENTER);
            knot->updateCtrl();

            // We don't want to use the standard knot handler,
            // since we don't want this knot to be draggable.
            knot->_event_connection.disconnect();
            knot->_event_connection =
                knot->ctrl->connect_event(sigc::bind(sigc::ptr_fun(cc_generic_knot_handler), knot));

            this->endpt_handle[i] = knot;
        }

        // Remove any existing handlers
        this->endpt_handler_connection[i].disconnect();
        this->endpt_handler_connection[i] =
            this->endpt_handle[i]->ctrl->connect_event(sigc::bind(sigc::ptr_fun(endpt_handler), this));
    }

    if (curve->is_empty()) {
        // Connector is invisible because it is clipped to the boundary
        // of two overlpapping shapes.  So, it doesn't need endpoints.
        return;
    }

    Geom::Point startpt = *(curve->first_point()) * i2dt;
    this->endpt_handle[0]->setPosition(startpt, 0);

    Geom::Point endpt = *(curve->last_point()) * i2dt;
    this->endpt_handle[1]->setPosition(endpt, 0);

    this->endpt_handle[0]->show();
    this->endpt_handle[1]->show();
}

void cc_create_connection_point(ConnectorTool* cc)
{
    if (cc->active_shape && cc->state == SP_CONNECTOR_CONTEXT_IDLE) {
        if (cc->selected_handle) {
            cc_deselect_handle( cc->selected_handle );
        }

        SPKnot *knot = new SPKnot(cc->getDesktop(), "", Inkscape::CANVAS_ITEM_CTRL_TYPE_SHAPER,
            "CanvasItemCtrl::ConnectorTool:ConnectionPoint");

        // We do not process events on this knot.
        knot->_event_connection.disconnect();

        cc_select_handle( knot );
        cc->selected_handle = knot;
        cc->selected_handle->show();
        cc->state = SP_CONNECTOR_CONTEXT_NEWCONNPOINT;
    }
}

static bool cc_item_is_shape(SPItem *item)
{
    if (auto path = cast<SPPath>(item)) {
        SPCurve const *curve = path->curve();
        if ( curve && !(curve->is_closed()) ) {
            // Open paths are connectors.
            return false;
        }
    } else if (is<SPText>(item) || is<SPFlowtext>(item)) {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        if (prefs->getBool("/tools/connector/ignoretext", true)) {
            // Don't count text as a shape we can connect connector to.
            return false;
        }
    }
    return true;
}


bool cc_item_is_connector(SPItem *item)
{
    if (auto path = cast<SPPath>(item)) {
        bool closed = path->curveForEdit()->is_closed();
        if (path->connEndPair.isAutoRoutingConn() && !closed) {
            // To be considered a connector, an object must be a non-closed
            // path that is marked with a "inkscape:connector-type" attribute.
            return true;
        }
    }
    return false;
}


void cc_selection_set_avoid(SPDesktop *desktop, bool const set_avoid)
{
    if (desktop == nullptr) {
        return;
    }

    SPDocument *document = desktop->getDocument();

    Inkscape::Selection *selection = desktop->getSelection();


    int changes = 0;

    for (SPItem *item: selection->items()) {
        char const *value = (set_avoid) ? "true" : nullptr;

        if (cc_item_is_shape(item)) {
            item->setAttribute("inkscape:connector-avoid", value);
            item->getAvoidRef().handleSettingChange();
            changes++;
        }
    }

    if (changes == 0) {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE,
                _("Select <b>at least one non-connector object</b>."));
        return;
    }

    char *event_desc = (set_avoid) ?
            _("Make connectors avoid selected objects") :
            _("Make connectors ignore selected objects");
    DocumentUndo::done(document, event_desc, INKSCAPE_ICON("draw-connector"));
}

void ConnectorTool::_selectionChanged(Inkscape::Selection *selection)
{
    SPItem *item = selection->singleItem();
    if (this->active_conn == item) {
        // Nothing to change.
        return;
    }

    if (item == nullptr) {
        this->cc_clear_active_conn();
        return;
    }

    if (cc_item_is_connector(item)) {
        this->cc_set_active_conn(item);
    }
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
