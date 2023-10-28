// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * TextTool
 */
/*
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 1999-2005 authors
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_TOOLS_TEXT_TOOL_H
#define INKSCAPE_UI_TOOLS_TEXT_TOOL_H

#include <string>
#include <vector>
#include <2geom/point.h>

#include "display/control/canvas-item-ptr.h"
#include "helper/auto-connection.h"
#include "libnrtype/Layout-TNG.h"
#include "ui/tools/tool-base.h"

using GtkIMContext = struct _GtkIMContext;

namespace Inkscape {
class CanvasItemCurve; // Cursor
class CanvasItemQuad;  // Highlighted text
class CanvasItemRect;  // Indicator, Frame
class CanvasItemBpath;
class Selection;
} // namespace Inkscape

namespace Inkscape::UI::Tools {

class TextTool : public ToolBase
{
public:
    TextTool(SPDesktop *desktop);
    ~TextTool() override;

    bool pasteInline(Glib::ustring const clip_text);
    void placeCursorAt(SPObject *text, Geom::Point const &p);
    void placeCursor(SPObject *text, Text::Layout::iterator where);
    bool deleteSelection();
    void deleteSelected();

    SPItem *textItem() const { return text; }

    // Insertion point position
    // Fixme: Public due to hack used by text toolbar.
    Text::Layout::iterator text_sel_start;
    Text::Layout::iterator text_sel_end;

protected:
    bool root_handler(CanvasEvent const &event) override;
    bool item_handler(SPItem *item, CanvasEvent const &event) override;

private:
    // The text we're editing, or null if none selected
    SPItem *text = nullptr;

    // Text item position in root coordinates
    Geom::Point pdoc;

    // Input method
    GtkIMContext *imc = nullptr;

    // Unicode input
    std::string uni;
    bool unimode = false;

    // On-canvas editing
    CanvasItemPtr<CanvasItemCurve> cursor;
    CanvasItemPtr<CanvasItemRect> indicator;
    CanvasItemPtr<CanvasItemBpath> frame; // Highlighting flowtext shapes or textpath path
    CanvasItemPtr<CanvasItemBpath> padding_frame; // Highlighting flowtext padding
    std::vector<CanvasItemPtr<CanvasItemQuad>> text_selection_quads;

    // Cursor blinking
    bool show = false;
    bool phase = false;
    int blink_time = 0;

    bool nascent_object = false; // clicked on canvas to place cursor, but no text typed yet so ->text still null
    bool over_text = false; // true if cursor is over a text object
    unsigned dragging_state = 0;  // dragging selection over text
    bool creating = false;  // dragging rubberband to create flowtext
    Geom::Point p0;         // initial point if the flowtext rect

    auto_connection sel_changed_connection;
    auto_connection sel_modified_connection;
    auto_connection style_set_connection;
    auto_connection style_query_connection;
    auto_connection focus_in_conn;
    auto_connection focus_out_conn;
    auto_connection blink_conn;

    void _updateCursor(bool scroll_to_see = true);
    void _updateTextSelection();
    void _setupText();

    void _commit(GtkIMContext *imc, char *string);

    void _validateCursorIterators();
    void _blinkCursor();
    void _resetBlinkTimer();
    void _showCursor();
    void _forgetText();
    void _insertUnichar();
    void _showCurrUnichar();

    void _selectionChanged(Selection *selection);
    void _selectionModified(Selection *selection, unsigned flags);
    bool _styleSet(SPCSSAttr const *css);
    int _styleQueried(SPStyle *style, int property);
};

Glib::ustring get_selected_text(TextTool const &tool);
SPCSSAttr *get_style_at_cursor(TextTool const &tool);
Text::Layout::iterator const *get_cursor_position(TextTool const &tool, SPObject const *text);

} // namespace Inkscape::UI::Tools

inline auto SP_TEXT_CONTEXT(Inkscape::UI::Tools::ToolBase *tool) { return dynamic_cast<Inkscape::UI::Tools::TextTool*>(tool); }
inline auto SP_TEXT_CONTEXT(Inkscape::UI::Tools::ToolBase const *tool) { return dynamic_cast<Inkscape::UI::Tools::TextTool const *>(tool); }

#endif // INKSCAPE_UI_TOOLS_TEXT_TOOL_H

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
