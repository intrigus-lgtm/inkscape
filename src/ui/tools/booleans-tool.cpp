// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Martin Owens
 *
 * Copyright (C) 2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <glibmm/i18n.h>

#include "actions/actions-tools.h" // set_active_tool()
#include "ui/tools/booleans-tool.h"
#include "ui/tools/booleans-builder.h"
#include "display/control/canvas-item-group.h"
#include "display/control/canvas-item-drawing.h"
#include "display/drawing.h"

#include "desktop.h"
#include "document.h"
#include "document-undo.h"
#include "event-log.h"
#include "selection.h"
#include "ui/icon-names.h"
#include "ui/modifiers.h"
#include "ui/widget/events/canvas-event.h"
#include "style.h"

using Inkscape::DocumentUndo;
using Inkscape::Modifiers::Modifier;

namespace Inkscape {
namespace UI {
namespace Tools {

InteractiveBooleansTool::InteractiveBooleansTool(SPDesktop *desktop)
    : ToolBase(desktop, "/tools/booleans", "select.svg")
{
    to_commit = false;
    update_status();
    if (auto selection = desktop->getSelection()) {
        desktop->setWaitingCursor();
        boolean_builder = std::make_unique<BooleanBuilder>(selection);
        desktop->clearWaitingCursor();

        // Any changes to the selection cancel the shape building process
        _sel_modified = selection->connectModified([=](Selection *sel, int) { shape_cancel(); });
        _sel_changed = selection->connectChanged([=](Selection *sel) { shape_cancel(); });
    }
    _desktop->doc()->get_event_log()->updateUndoVerbs();

    auto prefs = Inkscape::Preferences::get();
    set_opacity(prefs->getDouble("/tools/booleans/opacity", 0.5));
    hide_selected_objects();
}

InteractiveBooleansTool::~InteractiveBooleansTool()
{
    set_opacity(1.0);
    hide_selected_objects(false);
    _desktop->doc()->get_event_log()->updateUndoVerbs();
}

/**
 * Hide all selected items, because they are going to be re-drawn as
 * a fractured pattern and we don't want them to appear twice.
 */
void InteractiveBooleansTool::hide_selected_objects(bool hide)
{
    if (auto selection = _desktop->getSelection()) {
        for (auto item : selection->items()) {
            // We don't hide any image or group that contains an image
            // FUTURE: There is a corner case where regular shapes are inside a group
            // alongside an image, they should be hidden, but that's much more convoluted.
            if (hide && boolean_builder && boolean_builder->contains_image(item))
                continue;
            if (auto ditem = item->get_arenaitem(_desktop->dkey)) {
                ditem->setOpacity(hide ? 0.0 : item->style->opacity.value);
            }
        }
    }
}

/**
 * Set the variable transparency of the rest of the canvas
 */
void InteractiveBooleansTool::set_opacity(double opacity)
{
    if (auto drawing = _desktop->getCanvasDrawing()->get_drawing()) {
        drawing->setOpacity(opacity);
    }
}

void InteractiveBooleansTool::switching_away(std::string const &new_tool)
{
    // We unhide the selected items before comitting to prevent undo from entering
    // into a state where the drawing item for a group is invisible.
    hide_selected_objects(false);

    if (boolean_builder && (new_tool == "/tools/select" || new_tool == "/tool/nodes")) {
        // Only forcefully commit if we have the user's explicit instruction to do so.
        if (boolean_builder->has_changes() || to_commit) {
            bool replace = Inkscape::Preferences::get()->getBool("/tools/booleans/replace", true);
            _desktop->getSelection()->setList(boolean_builder->shape_commit(true, replace));
            DocumentUndo::done(_desktop->doc(), "Built Shapes", INKSCAPE_ICON("draw-booleans"));
        }
    }
}

bool InteractiveBooleansTool::is_ready() const
{
    if (!boolean_builder || !boolean_builder->has_items()) {
        if (_desktop->getSelection()->isEmpty()) {
            _desktop->showNotice(_("You must select some objects to use the Shape Builder tool."), 5000);
        } else {
            _desktop->showNotice(_("The Shape Builder requires regular shapes to be selected."), 5000);
        }
        return false;
    }
    return true;
}

void InteractiveBooleansTool::set(Preferences::Entry const &val)
{
    Glib::ustring path = val.getEntryName();
    if (path == "/tools/booleans/mode") {
        update_status();
        boolean_builder->task_cancel();
    }
}

void InteractiveBooleansTool::shape_commit()
{
    to_commit = true;
    // disconnect so we don't get canceled by accident.
    _sel_modified.disconnect();
    _sel_changed.disconnect();
    set_active_tool(_desktop, "Select");
}

void InteractiveBooleansTool::shape_cancel()
{
    boolean_builder.reset();
    set_active_tool(_desktop, "Select");
}

bool InteractiveBooleansTool::root_handler(CanvasEvent const &event)
{
    if (!boolean_builder) {
        return false;
    }

    bool ret = false;

    inspect_event(event,
        [&] (ButtonPressEvent const &event) {
            ret = event_button_press_handler(event);
        },
        [&] (ButtonReleaseEvent const &event) {
            ret = event_button_release_handler(event);
        },
        [&] (KeyPressEvent const &event) {
            ret = event_key_press_handler(event);
        },
        [&] (MotionEvent const &event) {
            ret = event_motion_handler(event);
        },
        [&] (CanvasEvent const &event) {}
    );

    if (ret) {
        return true;
    }

    bool add = should_add(event.modifiersAfter());
    set_cursor(add ? "cursor-union.svg" : "cursor-delete.svg");
    update_status();

    return ToolBase::root_handler(event);
}

/**
 * Returns true if the shape builder should add items,
 * false if shape builder should delete items
 */
bool InteractiveBooleansTool::should_add(unsigned state) const
{
    auto prefs = Preferences::get();
    bool pref = prefs->getInt("/tools/booleans/mode", 0) != 0;
    auto modifier = Modifier::get(Modifiers::Type::BOOL_SHIFT);
    return pref == modifier->active(state);
}

void InteractiveBooleansTool::update_status()
{
    auto prefs = Preferences::get();
    bool pref = prefs->getInt("/tools/booleans/mode", 0) == 0;
    auto modifier = Modifier::get(Modifiers::Type::BOOL_SHIFT);
    message_context->setF(Inkscape::IMMEDIATE_MESSAGE,
        (pref ? "<b>Drag</b> over fragments to unite them. <b>Click</b> to create a segment. Hold <b>%s</b> to Subtract."
              : "<b>Drag</b> over fragments to delete them. <b>Click</b> to delete a segment. Hold <b>%s</b> to Unite."),
        modifier->get_label().c_str());
}

bool InteractiveBooleansTool::event_button_press_handler(ButtonPressEvent const &event)
{
    if (event.num_press != 1) {
        return false;
    }

    if (event.button == 1) {
        boolean_builder->task_select(event.pos, should_add(event.modifiers));
        return true;
    } else if (event.button == 3) {
        // right click; do not eat it so that right-click menu can appear, but cancel dragging
        boolean_builder->task_cancel();
    }

    return false;
}

bool InteractiveBooleansTool::event_motion_handler(MotionEvent const &event)
{
    bool add = should_add(event.modifiers);

    if (event.modifiers & GDK_BUTTON1_MASK) {
        if (boolean_builder->has_task()) {
            return boolean_builder->task_add(event.pos);
        } else {
            return boolean_builder->task_select(event.pos, add);
        }
    } else {
        return boolean_builder->highlight(event.pos, add);
    }
}

bool InteractiveBooleansTool::event_button_release_handler(ButtonReleaseEvent const &event)
{
    if (event.button == 1) {
        boolean_builder->task_commit();
    }
    return true;
}

bool InteractiveBooleansTool::catch_undo(bool redo)
{
    if (redo) {
        boolean_builder->redo();
    } else {
        boolean_builder->undo();
    }
    return true;
}

bool InteractiveBooleansTool::event_key_press_handler(KeyPressEvent const &event)
{
    switch (get_latin_keyval(event)) {
        case GDK_KEY_Escape:
            if (boolean_builder->has_task()) {
                boolean_builder->task_cancel();
            } else {
                shape_cancel();
            }
            return true;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if (boolean_builder->has_task()) {
                boolean_builder->task_commit();
            } else {
                shape_commit();
            }
            return true;
        case GDK_KEY_z:
        case GDK_KEY_Z:
            if (event.modifiers & GDK_CONTROL_MASK) {
                return catch_undo(event.modifiers & GDK_SHIFT_MASK);
            }
            break;
        default:
            break;
    }

    return false;
}

} // namespace Tools
} // namespace UI
} // namespace Inkscape
