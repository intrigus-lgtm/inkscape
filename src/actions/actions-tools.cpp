// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Gio::Actions for switching tools.
 *
 * Copyright (C) 2020 Tavmjong Bah
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 *
 */

#include <iostream>
#include <map>

#include <giomm.h>  // Not <gtkmm.h>! To eventually allow a headless version!
#include <glibmm/i18n.h>

#include "actions-tools.h"
#include "actions-helper.h"

#include "inkscape-application.h"
#include "inkscape-window.h"
#include "message-context.h"

#include "object/box3d.h"
#include "object/sp-ellipse.h"
#include "object/sp-flowtext.h"
#include "object/sp-offset.h"
#include "object/sp-path.h"
#include "object/sp-rect.h"
#include "object/sp-spiral.h"
#include "object/sp-star.h"
#include "object/sp-text.h"
#include "object/sp-marker.h"

#include "ui/dialog/dialog-container.h"
#include "ui/dialog/dialog-manager.h"
#include "ui/dialog/inkscape-preferences.h"
#include "ui/tools/connector-tool.h"
#include "ui/tools/text-tool.h"

class ToolData {
public:
    int tool = TOOLS_INVALID; // TODO: Switch to named enum
    int pref = TOOLS_INVALID;
    Glib::ustring pref_path;
};

static std::map<Glib::ustring, ToolData> const &get_tool_data()
{
    static std::map<Glib::ustring, ToolData> tool_data = {
        // clang-format off
    {"Select",       {TOOLS_SELECT,          PREFS_PAGE_TOOLS_SELECTOR,       "/tools/select",          }},
    {"Node",         {TOOLS_NODES,           PREFS_PAGE_TOOLS_NODE,           "/tools/nodes",           }},
    {"Booleans",     {TOOLS_BOOLEANS,        PREFS_PAGE_TOOLS,/*No Page*/     "/tools/booleans",        }},
    {"Marker",       {TOOLS_MARKER,          PREFS_PAGE_TOOLS,/*No Page*/     "/tools/marker",          }},
    {"Rect",         {TOOLS_SHAPES_RECT,     PREFS_PAGE_TOOLS_SHAPES_RECT,    "/tools/shapes/rect",     }},
    {"Arc",          {TOOLS_SHAPES_ARC,      PREFS_PAGE_TOOLS_SHAPES_ELLIPSE, "/tools/shapes/arc",      }},
    {"Star",         {TOOLS_SHAPES_STAR,     PREFS_PAGE_TOOLS_SHAPES_STAR,    "/tools/shapes/star",     }},
    {"3DBox",        {TOOLS_SHAPES_3DBOX,    PREFS_PAGE_TOOLS_SHAPES_3DBOX,   "/tools/shapes/3dbox",    }},
    {"Spiral",       {TOOLS_SHAPES_SPIRAL,   PREFS_PAGE_TOOLS_SHAPES_SPIRAL,  "/tools/shapes/spiral",   }},
    {"Pencil",       {TOOLS_FREEHAND_PENCIL, PREFS_PAGE_TOOLS_PENCIL,         "/tools/freehand/pencil", }},
    {"Pen",          {TOOLS_FREEHAND_PEN,    PREFS_PAGE_TOOLS_PEN,            "/tools/freehand/pen",    }},
    {"Calligraphic", {TOOLS_CALLIGRAPHIC,    PREFS_PAGE_TOOLS_CALLIGRAPHY,    "/tools/calligraphic",    }},
    {"Text",         {TOOLS_TEXT,            PREFS_PAGE_TOOLS_TEXT,           "/tools/text",            }},
    {"Gradient",     {TOOLS_GRADIENT,        PREFS_PAGE_TOOLS_GRADIENT,       "/tools/gradient",        }},
    {"Mesh",         {TOOLS_MESH,            PREFS_PAGE_TOOLS, /* No Page */  "/tools/mesh",            }},
    {"Zoom",         {TOOLS_ZOOM,            PREFS_PAGE_TOOLS_ZOOM,           "/tools/zoom",            }},
    {"Measure",      {TOOLS_MEASURE,         PREFS_PAGE_TOOLS_MEASURE,        "/tools/measure",         }},
    {"Dropper",      {TOOLS_DROPPER,         PREFS_PAGE_TOOLS_DROPPER,        "/tools/dropper",         }},
    {"Tweak",        {TOOLS_TWEAK,           PREFS_PAGE_TOOLS_TWEAK,          "/tools/tweak",           }},
    {"Spray",        {TOOLS_SPRAY,           PREFS_PAGE_TOOLS_SPRAY,          "/tools/spray",           }},
    {"Connector",    {TOOLS_CONNECTOR,       PREFS_PAGE_TOOLS_CONNECTOR,      "/tools/connector",       }},
    {"PaintBucket",  {TOOLS_PAINTBUCKET,     PREFS_PAGE_TOOLS_PAINTBUCKET,    "/tools/paintbucket",     }},
    {"Eraser",       {TOOLS_ERASER,          PREFS_PAGE_TOOLS_ERASER,         "/tools/eraser",          }},
    {"LPETool",      {TOOLS_LPETOOL,         PREFS_PAGE_TOOLS, /* No Page */  "/tools/lpetool",         }},
    {"Pages",        {TOOLS_PAGES,           PREFS_PAGE_TOOLS,                "/tools/pages",           }},
    {"Picker",       {TOOLS_PICKER,          PREFS_PAGE_TOOLS, /* No Page */  "/tools/picker",          }}
        // clang-format on
    };
    return tool_data;
}

static std::map<Glib::ustring, Glib::ustring> const &get_tool_msg()
{
    static std::map<Glib::ustring, Glib::ustring> tool_msg = {
        // clang-format off
    {"Select",       _("<b>Click</b> to Select and Transform objects, <b>Drag</b> to select many objects.")                                                                                                                   },
    {"Node",         _("Modify selected path points (nodes) directly.")                                                                                                                                                       },
    {"Booleans",     _("Construct shapes with the interactive Boolean tool.")                                                                                                                                                 },
    {"Rect",         _("<b>Drag</b> to create a rectangle. <b>Drag controls</b> to round corners and resize. <b>Click</b> to select.")                                                                                        },
    {"Arc",          _("<b>Drag</b> to create an ellipse. <b>Drag controls</b> to make an arc or segment. <b>Click</b> to select.")                                                                                           },
    {"Star",         _("<b>Drag</b> to create a star. <b>Drag controls</b> to edit the star shape. <b>Click</b> to select.")                                                                                                  },
    {"3DBox",        _("<b>Drag</b> to create a 3D box. <b>Drag controls</b> to resize in perspective. <b>Click</b> to select (with <b>Ctrl+Alt</b> for single faces).")                                                      },
    {"Spiral",       _("<b>Drag</b> to create a spiral. <b>Drag controls</b> to edit the spiral shape. <b>Click</b> to select.")                                                                                              },
    {"Marker",       _("<b>Click</b> a shape to start editing its markers. <b>Drag controls</b> to change orientation, scale, and position.")                                                                                 },
    {"Pencil",       _("<b>Drag</b> to create a freehand line. <b>Shift</b> appends to selected path, <b>Alt</b> activates sketch mode.")                                                                                     },
    {"Pen",          _("<b>Click</b> or <b>click and drag</b> to start a path; with <b>Shift</b> to append to selected path. <b>Ctrl+click</b> to create single dots (straight line modes only).")                            },
    {"Calligraphic", _("<b>Drag</b> to draw a calligraphic stroke; with <b>Ctrl</b> to track a guide path. <b>Arrow keys</b> adjust width (left/right) and angle (up/down).")                                                 },
    {"Text",         _("<b>Click</b> to select or create text, <b>drag</b> to create flowed text; then type.")                                                                                                                },
    {"Gradient",     _("<b>Drag</b> or <b>double click</b> to create a gradient on selected objects, <b>drag handles</b> to adjust gradients.")                                                                               },
    {"Mesh",         _("<b>Drag</b> or <b>double click</b> to create a mesh on selected objects, <b>drag handles</b> to adjust meshes.")                                                                                      },
    {"Zoom",         _("<b>Click</b> or <b>drag around an area</b> to zoom in, <b>Shift+click</b> to zoom out.")                                                                                                              },
    {"Measure",      _("<b>Drag</b> to measure the dimensions of objects.")                                                                                                                                                   },
    {"Dropper",      _("<b>Click</b> to set fill, <b>Shift+click</b> to set stroke; <b>drag</b> to average color in area; with <b>Alt</b> to pick inverse color; <b>Ctrl+C</b> to copy the color under mouse to clipboard")   },
    {"Tweak",        _("To tweak a path by pushing, select it and drag over it.")                                                                                                                                             },
    {"Spray",        _("<b>Drag</b>, <b>click</b> or <b>click and scroll</b> to spray the selected objects.")                                                                                                                 },
    {"Connector",    _("<b>Click and drag</b> between shapes to create a connector.")                                                                                                                                         },
    {"PaintBucket",  _("<b>Click</b> to paint a bounded area, <b>Shift+click</b> to union the new fill with the current selection, <b>Ctrl+click</b> to change the clicked object's fill and stroke to the current setting.") },
    {"Eraser",       _("<b>Drag</b> to erase.")                                                                                                                                                                               },
    {"LPETool",      _("Choose a subtool from the toolbar")                                                                                                                                                                   },
    {"Pages",        _("Create and manage pages.")},
    {"Picker",       _("Pick objects.")}
        // clang-format on
    };
    return tool_msg;
}

Glib::ustring
get_active_tool(InkscapeWindow *win)
{
    Glib::ustring state;

    auto action = win->lookup_action("tool-switch");
    if (!action) {
        show_output("get_active_tool: action 'tool-switch' missing!");
        return state;
    }

    auto saction = std::dynamic_pointer_cast<Gio::SimpleAction>(action);
    if (!saction) {
        show_output("get_active_tool: action 'tool-switch' not SimpleAction!");
        return state;
    }

    saction->get_state(state);

    return state;
}

int
get_active_tool_enum(InkscapeWindow *win)
{
    return get_tool_data().at(get_active_tool(win)).tool;
}

void tool_switch(Glib::ustring const &tool, InkscapeWindow *win);

void
set_active_tool(InkscapeWindow *win, Glib::ustring const &tool)
{
    // Seems silly to have a function to just flip argument order... but it's consistent with other
    // external functions.
    tool_switch(tool, win);
}

void
open_tool_preferences(InkscapeWindow *win, Glib::ustring const &tool)
{
    tool_preferences(tool, win);
}

/**
 * Set tool to appropriate one to edit 'item'.
 */
void
set_active_tool(InkscapeWindow *win, SPItem *item, Geom::Point const p)
{
    if (is<SPRect>(item)) {
        tool_switch("Rect", win);
    } else if (is<SPGenericEllipse>(item)) {
        tool_switch("Arc", win);
    } else if (is<SPStar>(item)) {
        tool_switch("Star", win);
    } else if (is<SPBox3D>(item)) {
        tool_switch("3DBox", win);
    } else if (is<SPSpiral>(item)) {
        tool_switch("Spiral", win);
    } else if (is<SPMarker>(item)) {
        tool_switch("Marker", win);
    } else if (is<SPPath>(item)) {
        if (Inkscape::UI::Tools::cc_item_is_connector(item)) {
            tool_switch("Connector", win);
        }
        else {
            tool_switch("Node", win);
        }
    } else if (is<SPText>(item) || is<SPFlowtext>(item))  {
        tool_switch("Text", win);
        SPDesktop* dt = win->get_desktop();
        if (!dt) {
            show_output("set_active_tool: no desktop!");
            return;
        }
        SP_TEXT_CONTEXT(dt->getTool())->placeCursorAt(item, p);
    } else if (is<SPOffset>(item))  {
        tool_switch("Node", win);
    }
}

/**
 * Set display mode. Callback for 'tool-switch' action.
 */
void
tool_switch(Glib::ustring const &tool, InkscapeWindow *win)
{
    auto const &tool_data = get_tool_data();
    // Valid tool?
    auto tool_it = tool_data.find(tool);
    if (tool_it == tool_data.end()) {
        show_output(Glib::ustring("tool-switch: invalid tool name: ") + tool.raw());
        return;
    }

    // Have desktop?
    SPDesktop* dt = win->get_desktop();
    if (!dt) {
        show_output("tool_switch: no desktop!");
        return;
    }

    auto action = win->lookup_action("tool-switch");
    if (!action) {
        show_output("tool-switch: action 'tool-switch' missing!");
        return;
    }

    auto saction = std::dynamic_pointer_cast<Gio::SimpleAction>(action);
    if (!saction) {
        show_output("tool-switch: action 'tool-switch' not SimpleAction!");
        return;
    }

    // Gtk sometimes fires multiple actions at us, including when switch 'away' from
    // an option. So we catch duplications here and don't switch to ourselves.
    Glib::ustring current_tool;
    saction->get_state(current_tool);
    if (current_tool == tool)
        return;

    // Update button states.
    saction->set_enabled(false); // Avoid infinite loop when called by tool_toogle().
    saction->change_state(tool);
    saction->set_enabled(true);


    // Switch to new tool. TODO: Clean this up. This should be one window function.
    // Setting tool via preference path is a bit strange.
    dt->tipsMessageContext()->set(Inkscape::NORMAL_MESSAGE, get_tool_msg().at(tool).c_str());
    dt->setTool(tool_data.at(tool).pref_path);

    if (auto new_tool = dt->getTool()) {
        new_tool->set_last_active_tool(current_tool);
    }
}

/**
 * Open preferences page for tool. Could be turned into actions if need be.
 */
void
tool_preferences(Glib::ustring const &tool, InkscapeWindow *win)
{
    auto const &tool_data = get_tool_data();
    // Valid tool?
    auto tool_it = tool_data.find(tool);
    if (tool_it == tool_data.end()) {
        show_output(Glib::ustring("tool-preferences: invalid tool name: ") + tool.raw());
        return;
    }

    // Have desktop?
    SPDesktop* dt = win->get_desktop();
    if (!dt) {
        show_output("tool-preferences: no desktop!");
        return;
    }

    auto prefs = Inkscape::Preferences::get();
    prefs->setInt("/dialogs/preferences/page", tool_it->second.pref);
    Inkscape::UI::Dialog::DialogContainer* container = dt->getContainer();

    // Create dialog if it doesn't exist (also sets page if dialog not already in opened tab).
    container->new_floating_dialog("Preferences");

    // Find dialog and explicitly set page (in case not set in previous line).
    auto dialog = Inkscape::UI::Dialog::DialogManager::singleton().find_floating_dialog("Preferences");
    if (dialog) {
        auto pref_dialog = dynamic_cast<Inkscape::UI::Dialog::InkscapePreferences *>(dialog);
        if (pref_dialog) {
            pref_dialog->showPage(); // Switch to page indicated in preferences file (set above).
        }
    }
}

/**
 * Toggle between "Selector" and last used tool.
 */
void
tool_toggle(Glib::ustring const &tool, InkscapeWindow *win)
{
    SPDesktop* dt = win->get_desktop();
    if (!dt) {
        show_output("tool_toggle: no desktop!");
        return;
    }

    auto action = win->lookup_action("tool-switch");
    if (!action) {
        show_output("tool_toggle: action 'tool_switch' missing!");
        return;
    }

    auto saction = std::dynamic_pointer_cast<Gio::SimpleAction>(action);
    if (!saction) {
        show_output("tool_toogle: action 'tool_switch' not SimpleAction!");
        return;
    }

    static Glib::ustring old_tool = "Select";

    Glib::ustring current_tool;
    saction->get_state(current_tool);
    if (current_tool == tool) {
        current_tool = old_tool;
    } else {
        old_tool = current_tool;
        current_tool = tool;
    }

    tool_switch(current_tool, win);
}

Glib::ustring get_active_tool(SPDesktop *desktop)
{
    InkscapeWindow* win = desktop->getInkscapeWindow();
    return get_active_tool(win);
}

int get_active_tool_enum(SPDesktop *desktop)
{
    InkscapeWindow* win = desktop->getInkscapeWindow();
    return get_active_tool_enum(win);
}

void set_active_tool(SPDesktop *desktop, Glib::ustring const &tool)
{
    InkscapeWindow* win = desktop->getInkscapeWindow();
    set_active_tool(win, tool);
}

void set_active_tool(SPDesktop *desktop, SPItem *item, Geom::Point const p)
{
    InkscapeWindow* win = desktop->getInkscapeWindow();
    set_active_tool(win, item, p);
}

std::vector<std::vector<Glib::ustring>> raw_data_tools =
{
    // clang-format off
    {"win.tool-switch('Select')",       N_("Selector Tool"),        "Tool Switch",   N_("Select and transform objects")                  },
    {"win.tool-switch('Node')",         N_("Node Tool"),          "Tool Switch",   N_("Edit paths by nodes")                           },
    {"win.tool-switch('Booleans')",     N_("Shape Builder Tool"), "Tool Switch",   N_("Build shapes with the Boolean tool")           },

    {"win.tool-switch('Rect')",         N_("Rectangle Tool"),     "Tool Switch",   N_("Create rectangles and squares")                 },
    {"win.tool-switch('Arc')",          N_("Ellipse/Arc Tool"),   "Tool Switch",   N_("Create circles, ellipses and arcs")             },
    {"win.tool-switch('Star')",         N_("Star/Polygon Tool"),  "Tool Switch",   N_("Create stars and polygons")                     },
    {"win.tool-switch('3DBox')",        N_("3D Box Tool"),        "Tool Switch",   N_("Create 3D Boxes")                               },
    {"win.tool-switch('Spiral')",       N_("Spiral Tool"),        "Tool Switch",   N_("Create spirals")                                },
    {"win.tool-switch('Marker')",       N_("Marker Tool"),        "Tool Switch",   N_("Edit markers")                                  },

    {"win.tool-switch('Pen')",          N_("Pen Tool"),           "Tool Switch",   N_("Draw Bezier curves and straight lines")         },
    {"win.tool-switch('Pencil')",       N_("Pencil Tool"),        "Tool Switch",   N_("Draw freehand lines")                           },
    {"win.tool-switch('Calligraphic')", N_("Calligraphy Tool"),   "Tool Switch",   N_("Draw calligraphic or brush strokes")            },
    {"win.tool-switch('Text')",         N_("Text Tool"),          "Tool Switch",   N_("Create and edit text objects")                  },

    {"win.tool-switch('Gradient')",     N_("Gradient Tool"),      "Tool Switch",   N_("Create and edit gradients")                     },
    {"win.tool-switch('Mesh')",         N_("Mesh Tool"),          "Tool Switch",   N_("Create and edit meshes")                        },
    {"win.tool-switch('Dropper')",      N_("Dropper Tool"),       "Tool Switch",   N_("Pick colors from image")                        },
    {"win.tool-switch('PaintBucket')",  N_("Paint Bucket Tool"),  "Tool Switch",   N_("Fill bounded areas")                            },

    {"win.tool-switch('Tweak')",        N_("Tweak Tool"),         "Tool Switch",   N_("Tweak objects by sculpting or painting")        },
    {"win.tool-switch('Spray')",        N_("Spray Tool"),         "Tool Switch",   N_("Spray copies or clones of objects")             },
    {"win.tool-switch('Eraser')",       N_("Eraser Tool"),        "Tool Switch",   N_("Erase objects or paths")                        },
    {"win.tool-switch('Connector')",    N_("Connector Tool"),     "Tool Switch",   N_("Create diagram connectors")                     },
    {"win.tool-switch('LPETool')",      N_("LPE Tool"),           "Tool Switch",   N_("Do geometric constructions")                    },

    {"win.tool-switch('Zoom')",         N_("Zoom Tool"),          "Tool Switch",   N_("Zoom in or out")                                },
    {"win.tool-switch('Measure')",      N_("Measure Tool"),       "Tool Switch",   N_("Measure objects")                               },
    {"win.tool-switch('Pages')",        N_("Pages Tool"),         "Tool Switch",   N_("Create and edit document pages")                },

    {"win.tool-toggle('Select')",       N_("Toggle Selector Tool"),      "Tool Switch",   N_("Toggle between Selector tool and last used tool") },
    {"win.tool-toggle('Dropper')",      N_("Toggle Dropper"),     "Tool Switch",   N_("Toggle between Dropper tool and last used tool")},
    // clang-format on
};


void
add_actions_tools(InkscapeWindow* win)
{
    // clang-format off
    win->add_action_radio_string ( "tool-switch",        sigc::bind(sigc::ptr_fun(&tool_switch),  win), "Select");
    win->add_action_radio_string ( "tool-toggle",        sigc::bind(sigc::ptr_fun(&tool_toggle),  win), "Select");
    // clang-format on

    auto app = InkscapeApplication::instance();
    if (!app) {
        show_output("add_actions_tools: no app!");
        return;
    }

    app->get_action_extra_data().add_data(raw_data_tools);
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
