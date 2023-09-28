// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Gio::Actions for toggling snapping preferences. Not tied to a particular document.
 *
 * As preferences are stored per document, changes should be propagated to any window with same document.
 *
 * Copyright (C) 2019 Tavmjong Bah
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 */

#include "actions-canvas-snapping.h"

#include <unordered_map>
#include <giomm/actionmap.h>
#include <glibmm/i18n.h>
#include <glibmm/ustring.h>

#include "actions-helper.h"
#include "actions/actions-extra-data.h"
#include "inkscape-application.h"
#include "inkscape.h"
#include "inkscape-window.h"
#include "desktop.h"
#include "snap-preferences.h"

using namespace Inkscape;

void set_actions_canvas_snapping(Gio::ActionMap& map);

// There are two snapping lists that must be connected:
// 1. The Inkscape::SNAPTARGET value:  e.g. Inkscape::SNAPTARGET_BBOX_CATEGORY.
// 2. The Gio::Action name:            e.g. "snap-bbox"

struct SnapInfo {
    Glib::ustring action_name; // action name without "doc." prefix
    SnapTargetType type;       // corresponding snapping type
    bool set;                  // this is default for when "simple snapping" is ON and also initial value when preferences are deleted
};

typedef std::vector<SnapInfo> SnapVector;
typedef std::unordered_map<SnapTargetType, Glib::ustring> SnapMap;

SnapVector snap_bbox = {
    { "snap-bbox",               SNAPTARGET_BBOX_CATEGORY,      true },
    { "snap-bbox-edge",          SNAPTARGET_BBOX_EDGE,          true },
    { "snap-bbox-corner",        SNAPTARGET_BBOX_CORNER,        true },
    { "snap-bbox-edge-midpoint", SNAPTARGET_BBOX_EDGE_MIDPOINT, false },
    { "snap-bbox-center",        SNAPTARGET_BBOX_MIDPOINT,      false },
};

SnapVector snap_node = {
    { "snap-node-category",      SNAPTARGET_NODE_CATEGORY,      true },
    { "snap-path",               SNAPTARGET_PATH,               true },
    { "snap-path-intersection",  SNAPTARGET_PATH_INTERSECTION,  false }, // Note: OFF by default, as it is extremely slow in large documents!
    { "snap-node-cusp",          SNAPTARGET_NODE_CUSP,          true },
    { "snap-node-smooth",        SNAPTARGET_NODE_SMOOTH,        true },
    { "snap-line-midpoint",      SNAPTARGET_LINE_MIDPOINT,      true },
    { "snap-line-tangential",    SNAPTARGET_PATH_TANGENTIAL,    false },
    { "snap-line-perpendicular", SNAPTARGET_PATH_PERPENDICULAR, false },
};

SnapVector snap_alignment = {
    { "snap-alignment",          SNAPTARGET_ALIGNMENT_CATEGORY,    true },
    { "snap-alignment-self",     SNAPTARGET_ALIGNMENT_HANDLE,      false },
    // separate category:
    { "snap-distribution",       SNAPTARGET_DISTRIBUTION_CATEGORY, true },
};

SnapVector snap_all_the_rest = {
    { "snap-others",             SNAPTARGET_OTHERS_CATEGORY,    true },
    { "snap-object-midpoint",    SNAPTARGET_OBJECT_MIDPOINT,    false },
    { "snap-rotation-center",    SNAPTARGET_ROTATION_CENTER,    false },
    { "snap-text-baseline",      SNAPTARGET_TEXT_BASELINE,      true },
    { "snap-path-mask",          SNAPTARGET_PATH_MASK,          true },
    { "snap-path-clip",          SNAPTARGET_PATH_CLIP,          true },

    { "snap-page-border",        SNAPTARGET_PAGE_EDGE_BORDER,   true },
    { "snap-page-margin",        SNAPTARGET_PAGE_MARGIN_BORDER, true },
    { "snap-grid",               SNAPTARGET_GRID,               true },
    { "snap-grid-line",          SNAPTARGET_GRID_LINE,          true },
    { "snap-guide",              SNAPTARGET_GUIDE,              true },
};

const struct {const char* action_name; SimpleSnap option; bool set;} simple_snap_options[] = {
    { "simple-snap-bbox",      SimpleSnap::BBox,      true },
    { "simple-snap-nodes",     SimpleSnap::Nodes,     true },
    { "simple-snap-alignment", SimpleSnap::Alignment, false }
};

const SnapMap& get_snap_map() {
    static SnapMap map;
    if (map.empty()) {
        for (auto&& snap : snap_bbox)           { map[snap.type] = snap.action_name; }
        for (auto&& snap : snap_node)           { map[snap.type] = snap.action_name; }
        for (auto&& snap : snap_alignment)      { map[snap.type] = snap.action_name; }
        for (auto&& snap : snap_all_the_rest)   { map[snap.type] = snap.action_name; }
    }
    return map;
}

const SnapVector& get_snap_vect() {
    static SnapVector vect;
    if (vect.empty()) {
        for (auto v : {&snap_bbox, &snap_node, &snap_alignment, &snap_all_the_rest}) {
            vect.insert(vect.end(), v->begin(), v->end());
        }
    }
    return vect;
}

const Glib::ustring snap_pref_path = "/options/snapping/";
const Glib::ustring global_toggle = "snap-global-toggle";

// global and single location of snapping preferences
SnapPreferences& get_snapping_preferences() {
    static SnapPreferences preferences;
    static bool initialized = false;

    if (!initialized) {
        // after starting up restore all snapping preferences:
        auto prefs = Preferences::get();
        for (auto&& info : get_snap_vect()) {
            bool enabled = prefs->getBool(snap_pref_path + info.action_name, info.set);
            preferences.setTargetSnappable(info.type, enabled);
        }
        for (auto&& info : simple_snap_options) {
            bool enabled = prefs->getBool(snap_pref_path + info.action_name, info.set);
            preferences.set_simple_snap(info.option, enabled);
        }

        auto simple = prefs->getEntry("/toolbox/simplesnap");
        if (!simple.isValid()) {
            // first time up after creating preferences; apply "simple" snapping defaults
            prefs->setBool(simple.getPath(), true);
            transition_to_simple_snapping();
        }

        auto enabled = prefs->getEntry(snap_pref_path + global_toggle);
        preferences.setSnapEnabledGlobally(enabled.getBool());

        initialized = true;
    }

    return preferences;
}

void store_snapping_action(const Glib::ustring& action_name, bool enabled) {
    Preferences::get()->setBool(snap_pref_path + action_name, enabled);
}

// Turn requested snapping type on or off:
// * type - snap target
// * enabled - true to turn it on, false to turn it off
//
void set_canvas_snapping(SnapTargetType type, bool enabled) {
    get_snapping_preferences().setTargetSnappable(type, enabled);

    auto it = get_snap_map().find(type);
    if (it == get_snap_map().end()) {
        g_warning("No action for snap target type %d", int(type));
    }
    else {
        auto&& action_name = it->second;
        store_snapping_action(action_name, enabled);
    }
}

void update_actions(Gio::ActionMap& map) {
    // Some actions depend on others... we need to update everything!
    set_actions_canvas_snapping(map);
}

static void canvas_snapping_toggle(Gio::ActionMap& map, SnapTargetType type) {
    bool enabled = get_snapping_preferences().isSnapButtonEnabled(type);
    set_canvas_snapping(type, !enabled);
    update_actions(map);
}

void set_simple_snap(SimpleSnap option, bool value) {
    const SnapVector* vect = nullptr;
    switch (option) {
    case SimpleSnap::BBox:
        vect = &snap_bbox;
        break;
    case SimpleSnap::Nodes:
        vect = &snap_node;
        break;
    case SimpleSnap::Alignment:
        vect = &snap_alignment;
        break;
    case SimpleSnap::Rest:
        vect = &snap_all_the_rest;
        break;
    default:
        show_output(Glib::ustring("missing case statement in ") + __func__);
        break;
    }

    if (vect) {
        for (auto&& info : *vect) {
            bool enable = value && info.set;
            set_canvas_snapping(info.type, enable);
        }

        Glib::ustring action_name;
        for (auto&& info : simple_snap_options) {
            if (info.option == option) {
                action_name = info.action_name;
                break;
            }
        }
        // simple snap option 'Rest' does not have an action; only save other ones
        if (!action_name.empty()) {
            get_snapping_preferences().set_simple_snap(option, value);
            Preferences::get()->setBool(snap_pref_path + action_name, value);
        }
    }
}

void toggle_simple_snap_option(Gio::ActionMap& map, SimpleSnap option) {
    // toggle desired option
    bool enabled = !get_snapping_preferences().get_simple_snap(option);
    set_simple_snap(option, enabled);

    // reset others not visible / not exposed to their "simple" defaults
    for (auto&& info : snap_all_the_rest) {
        set_canvas_snapping(info.type, info.set);
    }

    update_actions(map);
}

void apply_simple_snap_defaults(Gio::ActionMap& map) {
    set_simple_snap(SimpleSnap::BBox, true);
    set_simple_snap(SimpleSnap::Nodes, true);
    set_simple_snap(SimpleSnap::Alignment, false);
    set_simple_snap(SimpleSnap::Rest, true);
    update_actions(map);
}

std::vector<std::vector<Glib::ustring>> raw_data_canvas_snapping =
{
    {"win.snap-global-toggle",        N_("Snapping"),                          "Snap",  N_("Toggle snapping on/off")                             },

    {"win.snap-alignment",            N_("Snap Objects that Align"),           "Snap",  N_("Toggle alignment snapping")                          },
    {"win.snap-alignment-self",       N_("Snap Nodes that Align"),             "Snap",  N_("Toggle alignment snapping to nodes in the same path")},

    {"win.snap-distribution",         N_("Snap Objects at Equal Distances"),   "Snap",  N_("Toggle snapping objects at equal distances")},

    {"win.snap-bbox",                 N_("Snap Bounding Boxes"),               "Snap",  N_("Toggle snapping to bounding boxes (global)")         },
    {"win.snap-bbox-edge",            N_("Snap Bounding Box Edges"),           "Snap",  N_("Toggle snapping to bounding-box edges")              },
    {"win.snap-bbox-corner",          N_("Snap Bounding Box Corners"),         "Snap",  N_("Toggle snapping to bounding-box corners")            },
    {"win.snap-bbox-edge-midpoint",   N_("Snap Bounding Box Edge Midpoints"),  "Snap",  N_("Toggle snapping to bounding-box edge mid-points")    },
    {"win.snap-bbox-center",          N_("Snap Bounding Box Centers"),         "Snap",  N_("Toggle snapping to bounding-box centers")            },

    {"win.snap-node-category",        N_("Snap Nodes"),                        "Snap",  N_("Toggle snapping to nodes (global)")                  },
    {"win.snap-path",                 N_("Snap Paths"),                        "Snap",  N_("Toggle snapping to paths")                           },
    {"win.snap-path-intersection",    N_("Snap Path Intersections"),           "Snap",  N_("Toggle snapping to path intersections")              },
    {"win.snap-node-cusp",            N_("Snap Cusp Nodes"),                   "Snap",  N_("Toggle snapping to cusp nodes, including rectangle corners")},
    {"win.snap-node-smooth",          N_("Snap Smooth Node"),                  "Snap",  N_("Toggle snapping to smooth nodes, including quadrant points of ellipses")},
    {"win.snap-line-midpoint",        N_("Snap Line Midpoints"),               "Snap",  N_("Toggle snapping to midpoints of lines")              },
    {"win.snap-line-perpendicular",   N_("Snap Perpendicular Lines"),          "Snap",  N_("Toggle snapping to perpendicular lines")             },
    {"win.snap-line-tangential",      N_("Snap Tangential Lines"),             "Snap",  N_("Toggle snapping to tangential lines")                },

    {"win.snap-others",               N_("Snap Others"),                       "Snap",  N_("Toggle snapping to misc. points (global)")           },
    {"win.snap-object-midpoint",      N_("Snap Object Midpoint"),              "Snap",  N_("Toggle snapping to object midpoint")                 },
    {"win.snap-rotation-center",      N_("Snap Rotation Center"),              "Snap",  N_("Toggle snapping to object rotation center")          },
    {"win.snap-text-baseline",        N_("Snap Text Baselines"),               "Snap",  N_("Toggle snapping to text baseline and text anchors")  },

    {"win.snap-page-border",          N_("Snap Page Border"),                  "Snap",  N_("Toggle snapping to page border")                     },
    {"win.snap-page-margin",          N_("Snap Page Margin"),                  "Snap",  N_("Toggle snapping to page margin")                     },
    {"win.snap-grid",                 N_("Snap Grids"),                        "Snap",  N_("Toggle snapping to grids")                           },
    {"win.snap-grid-line",            N_("Snap Grid Lines"),                   "Snap",  N_("Toggle snapping to grid lines")                      },
    {"win.snap-guide",                N_("Snap Guide Lines"),                  "Snap",  N_("Toggle snapping to guide lines")                     },

    {"win.snap-path-mask",            N_("Snap Mask Paths"),                   "Snap",  N_("Toggle snapping to mask paths")                      },
    {"win.snap-path-clip",            N_("Snap Clip Paths"),                   "Snap",  N_("Toggle snapping to clip paths")                      },

    {"win.simple-snap-bbox",          N_("Simple Snap Bounding Box"),          "Snap",  N_("Toggle snapping to bounding boxes")                  },
    {"win.simple-snap-nodes",         N_("Simple Snap Nodes"),                 "Snap",  N_("Toggle snapping to nodes")                           },
    {"win.simple-snap-alignment",     N_("Simple Snap Alignment"),             "Snap",  N_("Toggle alignment snapping")                          },
};

void add_actions_canvas_snapping(Gio::ActionMap* map) {
    assert(map != nullptr);

    map->add_action_bool(global_toggle, [=]() {
        auto& pref = get_snapping_preferences();
        bool enabled = !pref.getSnapEnabledGlobally();
        pref.setSnapEnabledGlobally(enabled);
        store_snapping_action(global_toggle, enabled);
        update_actions(*map);
    });

    for (auto&& info : get_snap_vect()) {
        map->add_action_bool(info.action_name, [=](){ canvas_snapping_toggle(*map, info.type); });
    }

    // Simple snapping popover
    for (auto&& info : simple_snap_options) {
        map->add_action_bool(info.action_name, [=](){ toggle_simple_snap_option(*map, info.option); });
    }

    // Check if there is already an application instance (GUI or non-GUI).
    auto app = InkscapeApplication::instance();
    if (!app) {
        show_output("add_actions_canvas_snapping: no app!");
        return;
    }
    app->get_action_extra_data().add_data(raw_data_canvas_snapping);

    update_actions(*map);
}


void
set_actions_canvas_snapping_helper(Gio::ActionMap& map, Glib::ustring action_name, bool state, bool enabled)
{
    // Glib::RefPtr<Gio::SimpleAction> saction = map->lookup_action(action_name); NOT POSSIBLE!

    // We can't enable/disable action directly! (Gio::Action can "get" enabled value but can not
    // "set" it! We need to cast to Gio::SimpleAction)
    Glib::RefPtr<Gio::Action> action = map.lookup_action(action_name);
    if (!action) {
        show_output(Glib::ustring("set_actions_canvas_snapping_helper: action ") + action_name.raw() + " missing!");
        return;
    }

    auto simple = std::dynamic_pointer_cast<Gio::SimpleAction>(action);
    if (!simple) {
        show_output(Glib::ustring("set_actions_canvas_snapping_helper: action ") + action_name.raw() + " not SimpleAction!");
        return;
    }

    simple->change_state(state);
    simple->set_enabled(enabled);
}

void set_actions_canvas_snapping(Gio::ActionMap& map) {
    auto& snapprefs = get_snapping_preferences();
    bool global = snapprefs.getSnapEnabledGlobally();
    bool alignment = snapprefs.isTargetSnappable(SNAPTARGET_ALIGNMENT_CATEGORY);
    bool distribution = snapprefs.isTargetSnappable(SNAPTARGET_DISTRIBUTION_CATEGORY);
    bool bbox = snapprefs.isTargetSnappable(SNAPTARGET_BBOX_CATEGORY);
    bool node = snapprefs.isTargetSnappable(SNAPTARGET_NODE_CATEGORY);
    bool other = snapprefs.isTargetSnappable(SNAPTARGET_OTHERS_CATEGORY);
    bool grid = snapprefs.isTargetSnappable(SNAPTARGET_GRID);

    struct { const char* action; bool state; bool enabled; } snap_options[] = {
        { "snap-global-toggle", global, true }, // Always enabled

        { "snap-alignment", alignment, global },
        { "snap-alignment-self",     snapprefs.isSnapButtonEnabled(SNAPTARGET_ALIGNMENT_HANDLE),   global && alignment },
        { "snap-distribution", distribution, global && alignment},

        { "snap-bbox", bbox, global },
        { "snap-bbox-edge",          snapprefs.isSnapButtonEnabled(SNAPTARGET_BBOX_EDGE),          global && bbox },
        { "snap-bbox-corner",        snapprefs.isSnapButtonEnabled(SNAPTARGET_BBOX_CORNER),        global && bbox },
        { "snap-bbox-edge-midpoint", snapprefs.isSnapButtonEnabled(SNAPTARGET_BBOX_EDGE_MIDPOINT), global && bbox },
        { "snap-bbox-center",        snapprefs.isSnapButtonEnabled(SNAPTARGET_BBOX_MIDPOINT),      global && bbox },

        { "snap-node-category", node, global },
        { "snap-path",               snapprefs.isSnapButtonEnabled(SNAPTARGET_PATH),               global && node },
        { "snap-path-intersection",  snapprefs.isSnapButtonEnabled(SNAPTARGET_PATH_INTERSECTION),  global && node },
        { "snap-node-cusp",          snapprefs.isSnapButtonEnabled(SNAPTARGET_NODE_CUSP),          global && node },
        { "snap-node-smooth",        snapprefs.isSnapButtonEnabled(SNAPTARGET_NODE_SMOOTH),        global && node },
        { "snap-line-midpoint",      snapprefs.isSnapButtonEnabled(SNAPTARGET_LINE_MIDPOINT),      global && node },
        { "snap-line-tangential",    snapprefs.isSnapButtonEnabled(SNAPTARGET_PATH_TANGENTIAL),    global && node },
        { "snap-line-perpendicular", snapprefs.isSnapButtonEnabled(SNAPTARGET_PATH_PERPENDICULAR), global && node },

        { "snap-others", other, global },
        { "snap-object-midpoint",    snapprefs.isSnapButtonEnabled(SNAPTARGET_OBJECT_MIDPOINT),    global && other },
        { "snap-rotation-center",    snapprefs.isSnapButtonEnabled(SNAPTARGET_ROTATION_CENTER),    global && other },
        { "snap-text-baseline",      snapprefs.isSnapButtonEnabled(SNAPTARGET_TEXT_BASELINE),      global && other },

        { "snap-path-clip",          snapprefs.isSnapButtonEnabled(SNAPTARGET_PATH_CLIP),          global && other},
        { "snap-path-mask",          snapprefs.isSnapButtonEnabled(SNAPTARGET_PATH_MASK),          global && other},

        { "snap-page-border",        snapprefs.isSnapButtonEnabled(SNAPTARGET_PAGE_EDGE_BORDER),   global },
        { "snap-page-margin",        snapprefs.isSnapButtonEnabled(SNAPTARGET_PAGE_MARGIN_BORDER), global },
        { "snap-grid",               snapprefs.isSnapButtonEnabled(SNAPTARGET_GRID),               global },
        { "snap-grid-line",          snapprefs.isSnapButtonEnabled(SNAPTARGET_GRID_LINE),          global && grid},
        { "snap-guide",              snapprefs.isSnapButtonEnabled(SNAPTARGET_GUIDE),              global },

        { "simple-snap-bbox", bbox, global },
        { "simple-snap-nodes", node, global },
        { "simple-snap-alignment", alignment, global },
    };

    for (auto&& snap : snap_options) {
        set_actions_canvas_snapping_helper(map, snap.action, snap.state, snap.enabled);
    }
}

/**
 * Simple snapping groups existing "advanced" options into three easy to understand choices (bounding box, nodes, alignment snap).
 * Behind the scene the same snapping properties to used. When entering "simple" mode those snapping properties need to be set
 * to the correct default values; advanced mode affords complete freedom in selecting them, simple mode restricts them.
 */
void transition_to_simple_snapping() {
    if (auto* dt = SP_ACTIVE_DESKTOP) {
        if (Gio::ActionMap* map = dt->getInkscapeWindow()) {
            apply_simple_snap_defaults(*map);
        }
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
