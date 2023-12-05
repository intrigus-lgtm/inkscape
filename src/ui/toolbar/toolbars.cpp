// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 *
 * A container for tool toolbars, displaying one toolbar at a time.
 *
 *//*
 * Authors:
 *  Tavmjong Bah
 *  Alex Valavanis
 *  Mike Kowalski
 *  Vaibhav Malik
 *
 * Copyright (C) 2023 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "toolbars.h"

#include <iostream>
#include <glibmm/i18n.h>
#include <gtkmm/button.h>
#include <gtkmm/grid.h>
#include <gtkmm/spinbutton.h>

// For creating toolbars
#include "ui/toolbar/arc-toolbar.h"
#include "ui/toolbar/box3d-toolbar.h"
#include "ui/toolbar/calligraphy-toolbar.h"
#include "ui/toolbar/connector-toolbar.h"
#include "ui/toolbar/dropper-toolbar.h"
#include "ui/toolbar/eraser-toolbar.h"
#include "ui/toolbar/gradient-toolbar.h"
#include "ui/toolbar/lpe-toolbar.h"
#include "ui/toolbar/mesh-toolbar.h"
#include "ui/toolbar/measure-toolbar.h"
#include "ui/toolbar/node-toolbar.h"
#include "ui/toolbar/booleans-toolbar.h"
#include "ui/toolbar/rect-toolbar.h"
#include "ui/toolbar/marker-toolbar.h"
#include "ui/toolbar/page-toolbar.h"
#include "ui/toolbar/paintbucket-toolbar.h"
#include "ui/toolbar/pencil-toolbar.h"
#include "ui/toolbar/select-toolbar.h"
#include "ui/toolbar/spray-toolbar.h"
#include "ui/toolbar/spiral-toolbar.h"
#include "ui/toolbar/star-toolbar.h"
#include "ui/toolbar/tweak-toolbar.h"
#include "ui/toolbar/text-toolbar.h"
#include "ui/toolbar/zoom-toolbar.h"
#include "ui/tools/tool-base.h"
#include "ui/widget/style-swatch.h"
#include "ui/util.h"

namespace Inkscape::UI::Toolbar {
namespace {

// Data for building and tracking toolbars.
struct ToolBoxData
{
    char const *type_name; // Used by preferences
    Glib::ustring const tool_name;
    std::unique_ptr<Toolbar> (*create)(SPDesktop *desktop);
    char const *swatch_tip;
};

template <typename T, auto... args>
auto create = [](SPDesktop *desktop) -> std::unique_ptr<Toolbar> { return std::make_unique<T>(desktop, args...); };

ToolBoxData const aux_toolboxes[] = {
    // If you change the tool_name for Measure or Text here, change it also in desktop-widget.cpp.
    // clang-format off
    { "/tools/select",          "Select",       create<SelectToolbar>,        nullptr},
    { "/tools/nodes",           "Node",         create<NodeToolbar>,          nullptr},
    { "/tools/booleans",        "Booleans",     create<BooleansToolbar>,      nullptr},
    { "/tools/marker",          "Marker",       create<MarkerToolbar>,        nullptr},
    { "/tools/shapes/rect",     "Rect",         create<RectToolbar>,          N_("Style of new rectangles")},
    { "/tools/shapes/arc",      "Arc",          create<ArcToolbar>,           N_("Style of new ellipses")},
    { "/tools/shapes/star",     "Star",         create<StarToolbar>,          N_("Style of new stars")},
    { "/tools/shapes/3dbox",    "3DBox",        create<Box3DToolbar>,         N_("Style of new 3D boxes")},
    { "/tools/shapes/spiral",   "Spiral",       create<SpiralToolbar>,        N_("Style of new spirals")},
    { "/tools/freehand/pencil", "Pencil",       create<PencilToolbar, true>,  N_("Style of new paths created by Pencil")},
    { "/tools/freehand/pen",    "Pen",          create<PencilToolbar, false>, N_("Style of new paths created by Pen")},
    { "/tools/calligraphic",    "Calligraphic", create<CalligraphyToolbar>,   N_("Style of new calligraphic strokes")},
    { "/tools/text",            "Text",         create<TextToolbar>,          nullptr},
    { "/tools/gradient",        "Gradient",     create<GradientToolbar>,      nullptr},
    { "/tools/mesh",            "Mesh",         create<MeshToolbar>,          nullptr},
    { "/tools/zoom",            "Zoom",         create<ZoomToolbar>,          nullptr},
    { "/tools/measure",         "Measure",      create<MeasureToolbar>,       nullptr},
    { "/tools/dropper",         "Dropper",      create<DropperToolbar>,       nullptr},
    { "/tools/tweak",           "Tweak",        create<TweakToolbar>,         N_("Color/opacity used for color tweaking")},
    { "/tools/spray",           "Spray",        create<SprayToolbar>,         nullptr},
    { "/tools/connector",       "Connector",    create<ConnectorToolbar>,     nullptr},
    { "/tools/pages",           "Pages",        create<PageToolbar>,          nullptr},
    { "/tools/paintbucket",     "Paintbucket",  create<PaintbucketToolbar>,   N_("Style of Paint Bucket fill objects")},
    { "/tools/eraser",          "Eraser",       create<EraserToolbar>,        _("TBD")},
    { "/tools/lpetool",         "LPETool",      create<LPEToolbar>,           _("TBD")},
    { nullptr,                  "",             nullptr,                      nullptr}
    // clang-format on
};

} // namespace

// We only create an empty box, it is filled later after the desktop is created.
Toolbars::Toolbars()
    : Gtk::Box(Gtk::Orientation::VERTICAL)
{
    set_name("Tool-Toolbars");
}

// Fill the toolbars widget with toolbars.
// Toolbars are contained inside a grid with an optional swatch.
void Toolbars::create_toolbars(SPDesktop *desktop)
{
    // Create the toolbars using their "create" methods.
    for (int i = 0; aux_toolboxes[i].type_name; i++) {
        if (aux_toolboxes[i].create) {
            // Change create_func to return Gtk::Box!
            auto const sub_toolbox = Gtk::manage(aux_toolboxes[i].create(desktop).release());
            sub_toolbox->set_name("SubToolBox");
            sub_toolbox->set_hexpand();
            sub_toolbox->set_overflow(Gtk::Overflow::HIDDEN);

            // Use a grid to wrap the toolbar and a possible swatch.
            auto const grid = Gtk::make_managed<Gtk::Grid>();

            // Store a pointer to the grid so we can show/hide it as the tool changes.
            toolbar_map[aux_toolboxes[i].tool_name] = grid;

            Glib::ustring ui_name = aux_toolboxes[i].tool_name +
                                    "Toolbar"; // If you change "Toolbar" here, change it also in desktop-widget.cpp.
            grid->set_name(ui_name);

            grid->attach(*sub_toolbox, 0, 0, 1, 1);

            // Add a swatch widget if swatch tooltip is defined.
            if (aux_toolboxes[i].swatch_tip) {
                auto const swatch =
                    Gtk::make_managed<Inkscape::UI::Widget::StyleSwatch>(nullptr, _(aux_toolboxes[i].swatch_tip));
                swatch->setDesktop(desktop);
                swatch->setToolName(aux_toolboxes[i].tool_name);
                swatch->setWatchedTool(aux_toolboxes[i].type_name, true);

                //               ===== Styling =====
                // TODO: Remove and use CSS
                swatch->set_margin_start(7);
                swatch->set_margin_end(7);
                swatch->set_valign(Gtk::Align::CENTER);
                //             ===== End Styling =====

                grid->attach(*swatch, 1, 0, 1, 1);
            }

            append(*grid);

        } else if (aux_toolboxes[i].swatch_tip) {
            std::cerr << "Toolbars::create_toolbars: Could not create: " << aux_toolboxes[i].tool_name << std::endl;
        }
    }

    desktop->connectEventContextChanged(sigc::mem_fun(*this, &Toolbars::change_toolbar));

    // Show initial toolbar, hide others.
    change_toolbar(desktop, desktop->getTool());
}

void Toolbars::change_toolbar(SPDesktop * /*desktop*/, Tools::ToolBase *tool)
{
    if (!tool) {
        std::cerr << "Toolbars::change_toolbar: tool is null!" << std::endl;
        return;
    }

    for (int i = 0; aux_toolboxes[i].type_name; i++) {
        toolbar_map[aux_toolboxes[i].tool_name]->set_visible(tool->getPrefsPath() == aux_toolboxes[i].type_name);
    }
}

} // namespace Inkscape::UI::Toolbar

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
