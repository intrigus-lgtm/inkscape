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

#include "text-tool.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/regex.h>
#include <gdk/gdkkeysyms.h>
#include <gdkmm/clipboard.h>
#include <gtkmm/settings.h>
#include <gtkmm/window.h>

#include "context-fns.h"
#include "desktop-style.h"
#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "message-context.h"
#include "message-stack.h"
#include "rubberband.h"
#include "selection-chemistry.h"
#include "selection.h"
#include "style.h"
#include "text-editing.h"
#include "display/control/canvas-item-curve.h"
#include "display/control/canvas-item-quad.h"
#include "display/control/canvas-item-rect.h"
#include "display/control/canvas-item-bpath.h"
#include "display/curve.h"
#include "livarot/Path.h"
#include "livarot/Shape.h"
#include "object/sp-flowtext.h"
#include "object/sp-namedview.h"
#include "object/sp-text.h"
#include "object/sp-textpath.h"
#include "object/sp-shape.h"
#include "ui/knot/knot-holder.h"
#include "ui/icon-names.h"
#include "ui/shape-editor.h"
#include "ui/widget/canvas.h"
#include "ui/widget/events/canvas-event.h"
#include "ui/widget/events/debug.h"
#include "util/callback-converter.h"
#include "util/units.h"
#include "xml/sp-css-attr.h"

using Inkscape::DocumentUndo;

namespace Inkscape::UI::Tools {

TextTool::TextTool(SPDesktop *desktop)
    : ToolBase(desktop, "/tools/text", "text.svg")
{
    Gtk::Settings::get_default()->get_property("gtk-cursor-blink-time", blink_time);
    if (blink_time < 0) {
        blink_time = 200;
    } else {
        blink_time /= 2;
    }

    cursor = make_canvasitem<CanvasItemCurve>(_desktop->getCanvasControls());
    cursor->set_stroke(0x000000ff);
    cursor->set_visible(false);

    // The rectangle box tightly wrapping text object when selected or under cursor.
    indicator = make_canvasitem<CanvasItemRect>(_desktop->getCanvasControls());
    indicator->set_stroke(0x0000ff7f);
    indicator->set_shadow(0xffffff7f, 1);
    indicator->set_visible(false);

    // The shape that the text is flowing into
    frame = make_canvasitem<CanvasItemBpath>(_desktop->getCanvasControls());
    frame->set_fill(0x00 /* zero alpha */, SP_WIND_RULE_NONZERO);
    frame->set_stroke(0x0000ff7f);
    frame->set_visible(false);

    // A second frame for showing the padding of the above frame
    padding_frame = make_canvasitem<CanvasItemBpath>(_desktop->getCanvasControls());
    padding_frame->set_fill(0x00 /* zero alpha */, SP_WIND_RULE_NONZERO);
    padding_frame->set_stroke(0xccccccdf);
    padding_frame->set_visible(false);

    _resetBlinkTimer();

    imc = gtk_im_multicontext_new();
    if (imc) {
        auto canvas = _desktop->getCanvas();

        /* im preedit handling is very broken in inkscape for
         * multi-byte characters.  See bug 1086769.
         * We need to let the IM handle the preediting, and
         * just take in the characters when they're finished being
         * entered.
         */
        gtk_im_context_set_use_preedit(imc, FALSE);
        gtk_im_context_set_client_widget(imc, canvas->Gtk::Widget::gobj());

        // Note: Connecting to property_is_focus().signal_changed() would result in slight regression due to signal emisssion ordering.
        focus_in_conn = canvas->connectFocusIn([this] { gtk_im_context_focus_in(imc); });
        focus_out_conn = canvas->connectFocusOut([this] { gtk_im_context_focus_out(imc); });
        g_signal_connect(G_OBJECT(imc), "commit", Util::make_g_callback<&TextTool::_commit>, this);

        if (canvas->has_focus()) {
            gtk_im_context_focus_in(imc);
        }
    }

    shape_editor = new ShapeEditor(_desktop);

    auto item = _desktop->getSelection()->singleItem();
    if (is<SPFlowtext>(item) || is<SPText>(item)) {
        shape_editor->set_item(item);
    }

    sel_changed_connection = _desktop->getSelection()->connectChangedFirst(
        sigc::mem_fun(*this, &TextTool::_selectionChanged)
    );
    sel_modified_connection = _desktop->getSelection()->connectModifiedFirst(
        sigc::mem_fun(*this, &TextTool::_selectionModified)
    );
    style_set_connection = _desktop->connectSetStyle(
        sigc::mem_fun(*this, &TextTool::_styleSet)
    );
    style_query_connection = _desktop->connectQueryStyle(
        sigc::mem_fun(*this, &TextTool::_styleQueried)
    );

    _selectionChanged(_desktop->getSelection());

    auto prefs = Preferences::get();
    if (prefs->getBool("/tools/text/selcue")) {
        enableSelectionCue();
    }
    if (prefs->getBool("/tools/text/gradientdrag")) {
        enableGrDrag();
    }
}

TextTool::~TextTool()
{
    enableGrDrag(false);

    _forgetText();

    if (imc) {
        // Note: We rely on this being the last reference, so we don't need to disconnect from signals.
        g_object_unref(G_OBJECT(imc));
    }

    delete shape_editor;

    ungrabCanvasEvents();

    Rubberband::get(_desktop)->stop();
}

void TextTool::deleteSelected()
{
    deleteSelection();
    DocumentUndo::done(_desktop->getDocument(), _("Delete text"), INKSCAPE_ICON("draw-text"));
}

bool TextTool::item_handler(SPItem *item, CanvasEvent const &event)
{
    _validateCursorIterators();
    auto const old_start = text_sel_start;

    bool ret = false;

    inspect_event(event,
        [&] (ButtonPressEvent const &event) {
            if (event.button != 1) {
                return;
            }
            auto const n_press = event.num_press % 3; // cycle through selection modes on repeated clicking
            if (n_press == 1) {
                // this var allow too much lees subbselection queries
                // reducing it to cursor iteracion, mouseup and down
                // find out clicked item, disregarding groups
                auto const item_ungrouped = _desktop->getItemAtPoint(event.pos, true);
                if (is<SPText>(item_ungrouped) || is<SPFlowtext>(item_ungrouped)) {
                    _desktop->getSelection()->set(item_ungrouped);
                    if (text) {
                        // find out click point in document coordinates
                        auto const p = _desktop->w2d(event.pos);
                        // set the cursor closest to that point
                        if (event.modifiers & GDK_SHIFT_MASK) {
                            text_sel_start = old_start;
                            text_sel_end = sp_te_get_position_by_coords(text, p);
                        } else {
                            text_sel_start = text_sel_end = sp_te_get_position_by_coords(text, p);
                        }
                        // update display
                        _updateCursor();
                        _updateTextSelection();
                        dragging_state = 1;
                    }
                    ret = true;
                }
            } else if (n_press == 2 && text && dragging_state) {
                if (auto const layout = te_get_layout(text)) {
                    if (!layout->isStartOfWord(text_sel_start)) {
                        text_sel_start.prevStartOfWord();
                    }
                    if (!layout->isEndOfWord(text_sel_end)) {
                        text_sel_end.nextEndOfWord();
                    }
                    _updateCursor();
                    _updateTextSelection();
                    dragging_state = 2;
                    ret = true;
                }
            } else if (n_press == 0 && text && dragging_state) {
                text_sel_start.thisStartOfLine();
                text_sel_end.thisEndOfLine();
                _updateCursor();
                _updateTextSelection();
                dragging_state = 3;
                ret = true;
            }
        },
        [&] (ButtonReleaseEvent const &event) {
            if (event.button == 1 && dragging_state) {
                dragging_state = 0;
                discard_delayed_snap_event();
                _desktop->emit_text_cursor_moved(this, this);
                ret = true;
            }
        },
        [&] (CanvasEvent const &event) {}
    );

    return ret || ToolBase::item_handler(item, event);
}

void TextTool::_setupText()
{
    /* Create <text> */
    Inkscape::XML::Document *xml_doc = _desktop->doc()->getReprDoc();
    Inkscape::XML::Node *rtext = xml_doc->createElement("svg:text");
    rtext->setAttribute("xml:space", "preserve"); // we preserve spaces in the text objects we create

    /* Set style */
    sp_desktop_apply_style_tool(_desktop, rtext, "/tools/text", true);

    rtext->setAttributeSvgDouble("x", pdoc.x());
    rtext->setAttributeSvgDouble("y", pdoc.y());

    /* Create <tspan> */
    Inkscape::XML::Node *rtspan = xml_doc->createElement("svg:tspan");
    rtspan->setAttribute("sodipodi:role", "line"); // otherwise, why bother creating the tspan?
    rtext->addChild(rtspan, nullptr);
    Inkscape::GC::release(rtspan);

    /* Create TEXT */
    Inkscape::XML::Node *rstring = xml_doc->createTextNode("");
    rtspan->addChild(rstring, nullptr);
    Inkscape::GC::release(rstring);
    auto text_item = cast<SPItem>(currentLayer()->appendChildRepr(rtext));
    /* fixme: Is selection::changed really immediate? */
    /* yes, it's immediate .. why does it matter? */
    _desktop->getSelection()->set(text_item);
    Inkscape::GC::release(rtext);
    text_item->transform = currentLayer()->i2doc_affine().inverse();

    text_item->updateRepr();
    text_item->doWriteTransform(text_item->transform, nullptr, true);
    DocumentUndo::done(_desktop->getDocument(), _("Create text"), INKSCAPE_ICON("draw-text"));
}

/**
 * Insert the character indicated by uni to replace the current selection,
 * and reset uni to empty string.
 *
 * \pre uni non-empty.
 */
void TextTool::_insertUnichar()
{
    assert(!uni.empty());

    unsigned uv;
    std::stringstream ss;
    ss << std::hex << uni;
    ss >> uv;
    uni.clear();

    if (!g_unichar_isprint(static_cast<gunichar>(uv))
         && !(g_unichar_validate(static_cast<gunichar>(uv)) && g_unichar_type(static_cast<gunichar>(uv)) == G_UNICODE_PRIVATE_USE))
    {
        // This may be due to bad input, so it goes to statusbar.
        _desktop->messageStack()->flash(ERROR_MESSAGE, _("Non-printable character"));
    } else {
        if (!text) { // printable key; create text if none (i.e. if nascent_object)
            _setupText();
            nascent_object = false; // we don't need it anymore, having created a real <text>
        }

        char u[10];
        auto const len = g_unichar_to_utf8(uv, u);
        u[len] = '\0';

        text_sel_start = text_sel_end = sp_te_replace(text, text_sel_start, text_sel_end, u);
        _updateCursor();
        _updateTextSelection();
        DocumentUndo::done(_desktop->getDocument(), _("Insert Unicode character"), INKSCAPE_ICON("draw-text"));
    }
}

static void hex_to_printable_utf8_buf(char const *const ehex, char *utf8)
{
    unsigned uv;
    std::stringstream ss;
    ss << std::hex << ehex;
    ss >> uv;
    if (!g_unichar_isprint(static_cast<gunichar>(uv))) {
        uv = 0xfffd;
    }
    auto const len = g_unichar_to_utf8(uv, utf8);
    utf8[len] = '\0';
}

void TextTool::_showCurrUnichar()
{
    if (!uni.empty()) {
        char utf8[10];
        hex_to_printable_utf8_buf(uni.c_str(), utf8);

        // Status bar messages are in pango markup, so we need xml escaping.
        if (utf8[1] == '\0') {
            switch (utf8[0]) {
                case '<': strcpy(utf8, "&lt;"); break;
                case '>': strcpy(utf8, "&gt;"); break;
                case '&': strcpy(utf8, "&amp;"); break;
                default: break;
            }
        }
        defaultMessageContext()->setF(NORMAL_MESSAGE,
                                      _("Unicode (<b>Enter</b> to finish): %s: %s"), uni.c_str(), utf8);
    } else {
        defaultMessageContext()->set(NORMAL_MESSAGE, _("Unicode (<b>Enter</b> to finish): "));
    }
}

bool TextTool::root_handler(CanvasEvent const &event)
{
    if constexpr (DEBUG_EVENTS) {
        dump_event(event, "TextTool::root_handler");
    }

    indicator->set_visible(false);

    _validateCursorIterators();

    auto prefs = Preferences::get();
    tolerance = prefs->getIntLimited("/options/dragtolerance/value", 0, 0, 100);

    bool ret = false;

    inspect_event(event,
        [&] (ButtonPressEvent const &event) {
            if (event.button != 1 || event.num_press != 1) {
                return;
            }

            if (!have_viable_layer(_desktop, _desktop->getMessageStack())) {
                ret = true;
                return;
            }

            saveDragOrigin(event.pos);

            auto button_dt = _desktop->w2d(event.pos);

            auto &m = _desktop->getNamedView()->snap_manager;
            m.setup(_desktop);
            m.freeSnapReturnByRef(button_dt, SNAPSOURCE_NODE_HANDLE);
            m.unSetup();

            p0 = button_dt;
            Rubberband::get(_desktop)->start(_desktop, p0);

            grabCanvasEvents();

            creating = true;

            ret = true;
        },
        [&] (MotionEvent const &event) {
            if (creating && event.modifiers & GDK_BUTTON1_MASK) {
                if (!checkDragMoved(event.pos)) {
                    return;
                }

                auto p = _desktop->w2d(event.pos);

                auto &m = _desktop->getNamedView()->snap_manager;
                m.setup(_desktop);
                m.freeSnapReturnByRef(p, SNAPSOURCE_NODE_HANDLE);
                m.unSetup();

                Rubberband::get(_desktop)->move(p);
                gobble_motion_events(GDK_BUTTON1_MASK);

                // status text
                auto const diff = p - p0;
                auto const x_q = Util::Quantity(std::abs(diff.x()), "px");
                auto const y_q = Util::Quantity(std::abs(diff.y()), "px");
                auto const xs = x_q.string(_desktop->getNamedView()->display_units);
                auto const ys = y_q.string(_desktop->getNamedView()->display_units);
                message_context->setF(IMMEDIATE_MESSAGE, _("<b>Flowed text frame</b>: %s &#215; %s"), xs.c_str(), ys.c_str());
            } else if (!sp_event_context_knot_mouseover()) {
                auto &m = _desktop->getNamedView()->snap_manager;
                m.setup(_desktop);

                auto const motion_dt = _desktop->w2d(event.pos);
                m.preSnap(SnapCandidatePoint(motion_dt, SNAPSOURCE_OTHER_HANDLE));
                m.unSetup();
            }

            if (event.modifiers & GDK_BUTTON1_MASK && dragging_state) {
                auto const layout = te_get_layout(text);
                if (!layout) {
                    return;
                }
                // find out click point in document coordinates
                auto const p = _desktop->w2d(event.pos);
                // set the cursor closest to that point
                auto new_end = sp_te_get_position_by_coords(text, p);
                if (dragging_state == 2) {
                    // double-click dragging_state: go by word
                    if (new_end < text_sel_start) {
                        if (!layout->isStartOfWord(new_end)) {
                            new_end.prevStartOfWord();
                        }
                    } else if (!layout->isEndOfWord(new_end)) {
                        new_end.nextEndOfWord();
                    }
                } else if (dragging_state == 3) {
                    // triple-click dragging_state: go by line
                    if (new_end < text_sel_start) {
                        new_end.thisStartOfLine();
                    } else {
                        new_end.thisEndOfLine();
                    }
                }
                // update display
                if (text_sel_end != new_end) {
                    text_sel_end = new_end;
                    _updateCursor();
                    _updateTextSelection();
                }
                gobble_motion_events(GDK_BUTTON1_MASK);
                return;
            }

            // find out item under mouse, disregarding groups
            auto const item_ungrouped = _desktop->getItemAtPoint(event.pos, true, nullptr);
            if (is<SPText>(item_ungrouped) || is<SPFlowtext>(item_ungrouped)) {
                auto const layout = te_get_layout(item_ungrouped);
                if (layout->inputTruncated()) {
                    indicator->set_stroke(0xff0000ff);
                } else {
                    indicator->set_stroke(0x0000ff7f);
                }
                auto const ibbox = item_ungrouped->desktopVisualBounds();
                if (ibbox) {
                    indicator->set_rect(*ibbox);
                }
                indicator->set_visible(true);

                set_cursor("text-insert.svg");
                _updateTextSelection();
                if (is<SPText>(item_ungrouped)) {
                    _desktop->getTool()->defaultMessageContext()->set(
                        NORMAL_MESSAGE,
                        _("<b>Click</b> to edit the text, <b>drag</b> to select part of the text."));
                } else {
                    _desktop->getTool()->defaultMessageContext()->set(
                        NORMAL_MESSAGE,
                        _("<b>Click</b> to edit the flowed text, <b>drag</b> to select part of the text."));
                }
                over_text = true;
            } else {
                // update cursor and statusbar: we are not over a text object now
                set_cursor("text.svg");
                _desktop->getTool()->defaultMessageContext()->clear();
                over_text = false;
            }
        },

        [&] (ButtonReleaseEvent const &event) {
            if (event.button != 1) {
                return;
            }

            discard_delayed_snap_event();

            auto p1 = _desktop->w2d(event.pos);

            auto &m = _desktop->getNamedView()->snap_manager;
            m.setup(_desktop);
            m.freeSnapReturnByRef(p1, SNAPSOURCE_NODE_HANDLE);
            m.unSetup();

            ungrabCanvasEvents();

            Rubberband::get(_desktop)->stop();

            if (creating && within_tolerance) {
                // Button 1, set X & Y & new item.
                _desktop->getSelection()->clear();
                pdoc = _desktop->dt2doc(p1);
                nascent_object = true; // new object was just created

                // Cursor height is defined by the new text object's font size; it needs to be set
                // artificially here, for the text object does not exist yet:
                double cursor_height = sp_desktop_get_font_size_tool(_desktop);
                auto const y_dir = _desktop->yaxisdir();
                auto const cursor_size = Geom::Point(0, y_dir * cursor_height);
                cursor->set_coords(p1, p1 - cursor_size);
                _showCursor();

                if (imc) {
                    GdkRectangle im_cursor;
                    Geom::Point const top_left = _desktop->get_display_area().corner(0);
                    Geom::Point const im_d0 = _desktop->d2w(p1 - top_left);
                    Geom::Point const im_d1 = _desktop->d2w(p1 - cursor_size - top_left);
                    Geom::Rect const im_rect(im_d0, im_d1);
                    im_cursor.x = std::floor(im_rect.left());
                    im_cursor.y = std::floor(im_rect.top());
                    im_cursor.width = std::floor(im_rect.width());
                    im_cursor.height = std::floor(im_rect.height());
                    gtk_im_context_set_cursor_location(imc, &im_cursor);
                }
                message_context->set(NORMAL_MESSAGE, _("Type text; <b>Enter</b> to start new line.")); // FIXME:: this is a copy of a string from _update_cursor below, do not desync

                within_tolerance = false;
            } else if (creating) {
                double cursor_height = sp_desktop_get_font_size_tool(_desktop);
                if (std::abs(p1.y() - p0.y()) > cursor_height) {
                    // otherwise even one line won't fit; most probably a slip of hand (even if bigger than tolerance)

                    if (prefs->getBool("/tools/text/use_svg2", true)) {
                        // SVG 2 text
                        auto text = create_text_with_rectangle(_desktop, p0, p1);
                        _desktop->getSelection()->set(text);
                    } else {
                        // SVG 1.2 text
                        auto ft = create_flowtext_with_internal_frame(_desktop, p0, p1);
                        _desktop->getSelection()->set(ft);
                    }

                    _desktop->messageStack()->flash(NORMAL_MESSAGE, _("Flowed text is created."));
                    DocumentUndo::done(_desktop->getDocument(),  _("Create flowed text"), INKSCAPE_ICON("draw-text"));

                } else {
                    _desktop->messageStack()->flash(ERROR_MESSAGE, _("The frame is <b>too small</b> for the current font size. Flowed text not created."));
                }
            }
            creating = false;
            _desktop->emit_text_cursor_moved(this, this);

            ret = true;
        },
        [&] (KeyPressEvent const &event) {
            auto const group0_keyval = get_latin_keyval(event);

            if (group0_keyval == GDK_KEY_KP_Add || group0_keyval == GDK_KEY_KP_Subtract) {
                if (!(event.modifiers & GDK_CONTROL_MASK)) { // mod2 is NumLock; if on, type +/- keys
                    return; // otherwise pass on keypad +/- so they can zoom
                }
            }

            if (text || nascent_object) {
                // there is an active text object in this context, or a new object was just created

                // Input methods often use Ctrl+Shift+U for preediting (unimode).
                // Override it so we can use our unimode.
                bool preedit_activation = mod_ctrl(event) && mod_shift(event) && !mod_alt(event)
                                          && (group0_keyval == GDK_KEY_U || group0_keyval == GDK_KEY_u);

                auto surface = _desktop->getToplevel()->get_surface()->gobj();
                if (unimode || !imc || preedit_activation || !gtk_im_context_filter_key(imc, true, surface, const_cast<GdkDevice*>(event.device->gobj()), event.time, event.keycode, (GdkModifierType)event.modifiers, event.group)) {
                    // IM did not consume the key, or we're in unimode

                    if (!mod_ctrl_only(event) && unimode) {
                        /* TODO: ISO 14755 (section 3 Definitions) says that we should also
                           accept the first 6 characters of alphabets other than the latin
                           alphabet "if the Latin alphabet is not used".  The below is also
                           reasonable (viz. hope that the user's keyboard includes latin
                           characters and force latin interpretation -- just as we do for our
                           keyboard shortcuts), but differs from the ISO 14755
                           recommendation. */
                        switch (group0_keyval) {
                            case GDK_KEY_space:
                            case GDK_KEY_KP_Space: {
                                if (!uni.empty()) {
                                    _insertUnichar();
                                }
                                // Stay in unimode.
                                _showCurrUnichar();
                                ret = true;
                                return;
                            }

                            case GDK_KEY_BackSpace: {
                                if (!uni.empty()) {
                                    uni.pop_back();
                                }
                                _showCurrUnichar();
                                ret = true;
                                return;
                            }

                            case GDK_KEY_Return:
                            case GDK_KEY_KP_Enter: {
                                if (!uni.empty()) {
                                    _insertUnichar();
                                }
                                // Exit unimode.
                                unimode = false;
                                defaultMessageContext()->clear();
                                ret = true;
                                return;
                            }

                            case GDK_KEY_Escape: {
                                // Cancel unimode.
                                unimode = false;
                                gtk_im_context_reset(imc);
                                defaultMessageContext()->clear();
                                ret = true;
                                return;
                            }

                            case GDK_KEY_Shift_L:
                            case GDK_KEY_Shift_R:
                                break;

                            default: {
                                auto const xdigit = gdk_keyval_to_unicode(group0_keyval);
                                if (xdigit <= 255 && g_ascii_isxdigit(xdigit)) {
                                    uni.push_back(xdigit);
                                    if (uni.length() == 8) {
                                        /* This behaviour is partly to due to the previous use
                                           of a fixed-length buffer for uni. The reason for
                                           choosing the number 8 is that it's the length of
                                           ``canonical form'' mentioned in the ISO 14755 spec.
                                           An advantage over choosing 6 is that it allows using
                                           backspace for typos & misremembering when entering a
                                           6-digit number. */
                                        _insertUnichar();
                                    }
                                    _showCurrUnichar();
                                } else {
                                    /* The intent is to ignore but consume characters that could be
                                       typos for hex digits.  Gtk seems to ignore & consume all
                                       non-hex-digits, and we do similar here.  Though note that some
                                       shortcuts (like keypad +/- for zoom) get processed before
                                       reaching this code. */
                                }
                                ret = true;
                                return;
                            }
                        }
                    }

                    auto const old_start = text_sel_start;
                    auto const old_end = text_sel_end;
                    bool cursor_moved = false;
                    int screenlines = 1;
                    if (text) {
                        double spacing = sp_te_get_average_linespacing(this->text);
                        Geom::Rect const d = _desktop->get_display_area().bounds();
                        screenlines = static_cast<int>(std::floor(d.height() / spacing)) - 1;
                        screenlines = std::max(screenlines, 1);
                    }

                    // Neither unimode nor IM consumed key; process text tool shortcuts.
                    switch (group0_keyval) {
                        case GDK_KEY_x:
                        case GDK_KEY_X:
                            if (mod_alt_only(event)) {
                                _desktop->setToolboxFocusTo("TextFontFamilyAction_entry");
                                ret = true;
                                return;
                            }
                            break;
                        case GDK_KEY_space:
                            if (mod_ctrl_only(event)) {
                                // No-break space
                                if (!text) { // printable key; create text if none (i.e. if nascent_object)
                                    _setupText();
                                    nascent_object = false; // we don't need it anymore, having created a real <text>
                                }
                                text_sel_start = text_sel_end = sp_te_replace(text, text_sel_start, text_sel_end, "\302\240");
                                _updateCursor();
                                _updateTextSelection();
                                _desktop->messageStack()->flash(NORMAL_MESSAGE, _("No-break space"));
                                DocumentUndo::done(_desktop->getDocument(),  _("Insert no-break space"), INKSCAPE_ICON("draw-text"));
                                ret = true;
                                return;
                            }
                            break;
                        case GDK_KEY_U:
                        case GDK_KEY_u:
                            if (mod_ctrl_only(event) || (mod_ctrl(event) && mod_shift(event))) {
                                if (unimode) {
                                    unimode = false;
                                    defaultMessageContext()->clear();
                                } else {
                                    unimode = true;
                                    uni.clear();
                                    defaultMessageContext()->set(NORMAL_MESSAGE, _("Unicode (<b>Enter</b> to finish): "));
                                }
                                if (imc) {
                                    gtk_im_context_reset(imc);
                                }
                                ret = true;
                                return;
                            }
                            break;
                        case GDK_KEY_B:
                        case GDK_KEY_b:
                            if (mod_ctrl_only(event) && text) {
                                auto const style = sp_te_style_at_position(text, std::min(text_sel_start, text_sel_end));
                                auto const css = sp_repr_css_attr_new();
                                if (style->font_weight.computed == SP_CSS_FONT_WEIGHT_NORMAL
                                    || style->font_weight.computed == SP_CSS_FONT_WEIGHT_100
                                    || style->font_weight.computed == SP_CSS_FONT_WEIGHT_200
                                    || style->font_weight.computed == SP_CSS_FONT_WEIGHT_300
                                    || style->font_weight.computed == SP_CSS_FONT_WEIGHT_400)
                                {
                                    sp_repr_css_set_property(css, "font-weight", "bold");
                                } else {
                                    sp_repr_css_set_property(css, "font-weight", "normal");
                                }
                                sp_te_apply_style(text, text_sel_start, text_sel_end, css);
                                sp_repr_css_attr_unref(css);
                                DocumentUndo::done(_desktop->getDocument(),  _("Make bold"), INKSCAPE_ICON("draw-text"));
                                _updateCursor();
                                _updateTextSelection();
                                ret = true;
                                return;
                            }
                            break;
                        case GDK_KEY_I:
                        case GDK_KEY_i:
                            if (mod_ctrl_only(event) && text) {
                                auto const style = sp_te_style_at_position(text, std::min(text_sel_start, text_sel_end));
                                auto const css = sp_repr_css_attr_new();
                                if (style->font_style.computed != SP_CSS_FONT_STYLE_NORMAL) {
                                    sp_repr_css_set_property(css, "font-style", "normal");
                                } else {
                                    sp_repr_css_set_property(css, "font-style", "italic");
                                }
                                sp_te_apply_style(text, text_sel_start, text_sel_end, css);
                                sp_repr_css_attr_unref(css);
                                DocumentUndo::done(_desktop->getDocument(),  _("Make italic"), INKSCAPE_ICON("draw-text"));
                                _updateCursor();
                                _updateTextSelection();
                                ret = true;
                                return;
                            }
                            break;

                        case GDK_KEY_A:
                        case GDK_KEY_a:
                            if (mod_ctrl_only(event) && text) {
                                if (auto const layout = te_get_layout(text)) {
                                    text_sel_start = layout->begin();
                                    text_sel_end = layout->end();
                                    _updateCursor();
                                    _updateTextSelection();
                                    ret = true;
                                    return;
                                }
                            }
                            break;

                        case GDK_KEY_Return:
                        case GDK_KEY_KP_Enter: {
                            if (!text) { // printable key; create text if none (i.e. if nascent_object)
                                _setupText();
                                nascent_object = false; // we don't need it anymore, having created a real <text>
                            }

                            auto text_element = cast<SPText>(text);
                            if (text_element && (text_element->has_shape_inside() || text_element->has_inline_size())) {
                                // Handle new line like any other character.
                                text_sel_start = text_sel_end = sp_te_insert(text, text_sel_start, "\n");
                            } else {
                                // Replace new line by either <tspan sodipodi:role="line" or <flowPara>.
                                iterator_pair enter_pair;
                                sp_te_delete(text, text_sel_start, text_sel_end, enter_pair);
                                text_sel_start = text_sel_end = enter_pair.first;
                                text_sel_start = text_sel_end = sp_te_insert_line(text, text_sel_start);
                            }

                            _updateCursor();
                            _updateTextSelection();
                            DocumentUndo::done(_desktop->getDocument(),  _("New line"), INKSCAPE_ICON("draw-text"));
                            ret = true;
                            return;
                        }
                        case GDK_KEY_BackSpace:
                            if (text) { // if nascent_object, do nothing, but return TRUE; same for all other delete and move keys

                                bool noSelection = false;

                                if (mod_ctrl(event)) {
                                    text_sel_start = text_sel_end;
                                }

                                if (text_sel_start == text_sel_end) {
                                    if (mod_ctrl(event)) {
                                        text_sel_start.prevStartOfWord();
                                    } else {
                                        text_sel_start.prevCursorPosition();
                                    }
                                    noSelection = true;
                                }

                                iterator_pair bspace_pair;
                                bool success = sp_te_delete(text, text_sel_start, text_sel_end, bspace_pair);

                                if (noSelection) {
                                    if (success) {
                                        text_sel_start = text_sel_end = bspace_pair.first;
                                    } else { // nothing deleted
                                        text_sel_start = text_sel_end = bspace_pair.second;
                                    }
                                } else {
                                    if (success) {
                                        text_sel_start = text_sel_end = bspace_pair.first;
                                    } else { // nothing deleted
                                        text_sel_start = bspace_pair.first;
                                        text_sel_end = bspace_pair.second;
                                    }
                                }

                                _updateCursor();
                                _updateTextSelection();
                                DocumentUndo::done(_desktop->getDocument(),  _("Backspace"), INKSCAPE_ICON("draw-text"));
                            }
                            ret = true;
                            return;
                        case GDK_KEY_Delete:
                        case GDK_KEY_KP_Delete:
                            if (text) {
                                bool noSelection = false;

                                if (mod_ctrl(event)) {
                                    text_sel_start = text_sel_end;
                                }

                                if (text_sel_start == text_sel_end) {
                                    if (mod_ctrl(event)) {
                                        text_sel_end.nextEndOfWord();
                                    } else {
                                        text_sel_end.nextCursorPosition();
                                    }
                                    noSelection = true;
                                }

                                iterator_pair del_pair;
                                bool success = sp_te_delete(text, text_sel_start, text_sel_end, del_pair);

                                if (noSelection) {
                                    text_sel_start = text_sel_end = del_pair.first;
                                } else {
                                    if (success) {
                                        text_sel_start = text_sel_end = del_pair.first;
                                    } else { // nothing deleted
                                        text_sel_start = del_pair.first;
                                        text_sel_end = del_pair.second;
                                    }
                                }

                                _updateCursor();
                                _updateTextSelection();
                                DocumentUndo::done(_desktop->getDocument(),  _("Delete"), INKSCAPE_ICON("draw-text"));
                            }
                            ret = true;
                            return;
                        case GDK_KEY_Left:
                        case GDK_KEY_KP_Left:
                        case GDK_KEY_KP_4:
                            if (this->text) {
                                if (mod_alt(event)) {
                                    int mul = 1 + gobble_key_events(get_latin_keyval(event), 0); // with any mask
                                    if (mod_shift(event)) {
                                        sp_te_adjust_kerning_screen(text, text_sel_start, text_sel_end, _desktop, Geom::Point(mul * -10, 0));
                                    } else {
                                        sp_te_adjust_kerning_screen(text, text_sel_start, text_sel_end, _desktop, Geom::Point(mul * -1, 0));
                                    }
                                    _updateCursor();
                                    _updateTextSelection();
                                    DocumentUndo::maybeDone(_desktop->getDocument(), "kern:left",  _("Kern to the left"), INKSCAPE_ICON("draw-text"));
                                } else {
                                    if (mod_ctrl(event)) {
                                        text_sel_end.cursorLeftWithControl();
                                    } else {
                                        text_sel_end.cursorLeft();
                                    }
                                    cursor_moved = true;
                                    break;
                                }
                            }
                            ret = true;
                            return;
                        case GDK_KEY_Right:
                        case GDK_KEY_KP_Right:
                        case GDK_KEY_KP_6:
                            if (text) {
                                if (mod_alt(event)) {
                                    int mul = 1 + gobble_key_events(get_latin_keyval(event), 0); // with any mask
                                    if (mod_shift(event)) {
                                        sp_te_adjust_kerning_screen(text, text_sel_start, text_sel_end, _desktop, Geom::Point(mul * 10, 0));
                                    } else {
                                        sp_te_adjust_kerning_screen(text, text_sel_start, text_sel_end, _desktop, Geom::Point(mul * 1, 0));
                                    }
                                    _updateCursor();
                                    _updateTextSelection();
                                    DocumentUndo::maybeDone(_desktop->getDocument(), "kern:right",  _("Kern to the right"), INKSCAPE_ICON("draw-text"));
                                } else {
                                    if (mod_ctrl(event)) {
                                        text_sel_end.cursorRightWithControl();
                                    } else {
                                        text_sel_end.cursorRight();
                                    }
                                    cursor_moved = true;
                                    break;
                                }
                            }
                            ret = true;
                            return;
                        case GDK_KEY_Up:
                        case GDK_KEY_KP_Up:
                        case GDK_KEY_KP_8:
                            if (text) {
                                if (mod_alt(event)) {
                                    int mul = 1 + gobble_key_events(get_latin_keyval(event), 0); // with any mask
                                    if (mod_shift(event)) {
                                        sp_te_adjust_kerning_screen(text, text_sel_start, text_sel_end, _desktop, Geom::Point(0, mul * -10));
                                    } else {
                                        sp_te_adjust_kerning_screen(text, text_sel_start, text_sel_end, _desktop, Geom::Point(0, mul * -1));
                                    }
                                    _updateCursor();
                                    _updateTextSelection();
                                    DocumentUndo::maybeDone(_desktop->getDocument(), "kern:up",  _("Kern up"), INKSCAPE_ICON("draw-text"));
                                } else {
                                    if (mod_ctrl(event)) {
                                        text_sel_end.cursorUpWithControl();
                                    } else {
                                        text_sel_end.cursorUp();
                                    }
                                    cursor_moved = true;
                                    break;
                                }
                            }
                            ret = true;
                            return;
                        case GDK_KEY_Down:
                        case GDK_KEY_KP_Down:
                        case GDK_KEY_KP_2:
                            if (text) {
                                if (mod_alt(event)) {
                                    int mul = 1 + gobble_key_events(get_latin_keyval(event), 0); // with any mask
                                    if (mod_shift(event)) {
                                        sp_te_adjust_kerning_screen(text, text_sel_start, text_sel_end, _desktop, Geom::Point(0, mul * 10));
                                    } else {
                                        sp_te_adjust_kerning_screen(text, text_sel_start, text_sel_end, _desktop, Geom::Point(0, mul * 1));
                                    }
                                    _updateCursor();
                                    _updateTextSelection();
                                    DocumentUndo::maybeDone(_desktop->getDocument(), "kern:down",  _("Kern down"), INKSCAPE_ICON("draw-text"));
                                } else {
                                    if (mod_ctrl(event)) {
                                        text_sel_end.cursorDownWithControl();
                                    } else {
                                        text_sel_end.cursorDown();
                                    }
                                    cursor_moved = true;
                                    break;
                                }
                            }
                            ret = true;
                            return;
                        case GDK_KEY_Home:
                        case GDK_KEY_KP_Home:
                            if (text) {
                                if (mod_ctrl(event)) {
                                    text_sel_end.thisStartOfShape();
                                } else {
                                    text_sel_end.thisStartOfLine();
                                }
                                cursor_moved = true;
                                break;
                            }
                            ret = true;
                            return;
                        case GDK_KEY_End:
                        case GDK_KEY_KP_End:
                            if (text) {
                                if (mod_ctrl(event)) {
                                    text_sel_end.nextStartOfShape();
                                } else {
                                    text_sel_end.thisEndOfLine();
                                }
                                cursor_moved = true;
                                break;
                            }
                            ret = true;
                            return;
                        case GDK_KEY_Page_Down:
                        case GDK_KEY_KP_Page_Down:
                            if (text) {
                                text_sel_end.cursorDown(screenlines);
                                cursor_moved = true;
                                break;
                            }
                            ret = true;
                            return;
                        case GDK_KEY_Page_Up:
                        case GDK_KEY_KP_Page_Up:
                            if (text) {
                                text_sel_end.cursorUp(screenlines);
                                cursor_moved = true;
                                break;
                            }
                            ret = true;
                            return;
                        case GDK_KEY_Escape:
                            if (creating) {
                                creating = false;
                                ungrabCanvasEvents();
                                Rubberband::get(_desktop)->stop();
                            } else {
                                _desktop->getSelection()->clear();
                            }
                            nascent_object = false;
                            ret = true;
                            return;
                        case GDK_KEY_bracketleft:
                            if (text) {
                                if (mod_alt(event) || mod_ctrl(event)) {
                                    if (mod_alt(event)) {
                                        if (mod_shift(event)) {
                                            // FIXME: alt+shift+[] does not work, don't know why
                                            sp_te_adjust_rotation_screen(text, text_sel_start, text_sel_end, _desktop, -10);
                                        } else {
                                            sp_te_adjust_rotation_screen(text, text_sel_start, text_sel_end, _desktop, -1);
                                        }
                                    } else {
                                        sp_te_adjust_rotation(text, text_sel_start, text_sel_end, _desktop, -90);
                                    }
                                    DocumentUndo::maybeDone(_desktop->getDocument(), "textrot:ccw",  _("Rotate counterclockwise"), INKSCAPE_ICON("draw-text"));
                                    _updateCursor();
                                    _updateTextSelection();
                                    ret = true;
                                    return;
                                }
                            }
                            break;
                        case GDK_KEY_bracketright:
                            if (text) {
                                if (mod_alt(event) || mod_ctrl(event)) {
                                    if (mod_alt(event)) {
                                        if (mod_shift(event)) {
                                            // FIXME: alt+shift+[] does not work, don't know why
                                            sp_te_adjust_rotation_screen(text, text_sel_start, text_sel_end, _desktop, 10);
                                        } else {
                                            sp_te_adjust_rotation_screen(text, text_sel_start, text_sel_end, _desktop, 1);
                                        }
                                    } else {
                                        sp_te_adjust_rotation(text, text_sel_start, text_sel_end, _desktop, 90);
                                    }
                                    DocumentUndo::maybeDone(_desktop->getDocument(), "textrot:cw",  _("Rotate clockwise"), INKSCAPE_ICON("draw-text"));
                                    _updateCursor();
                                    _updateTextSelection();
                                    ret = true;
                                    return;
                                }
                            }
                            break;
                        case GDK_KEY_less:
                        case GDK_KEY_comma:
                            if (text) {
                                if (mod_alt(event)) {
                                    if (mod_ctrl(event)) {
                                        if (mod_shift(event)) {
                                            sp_te_adjust_linespacing_screen(text, text_sel_start, text_sel_end, _desktop, -10);
                                        } else {
                                            sp_te_adjust_linespacing_screen(text, text_sel_start, text_sel_end, _desktop, -1);
                                        }
                                        DocumentUndo::maybeDone(_desktop->getDocument(), "linespacing:dec",  _("Contract line spacing"), INKSCAPE_ICON("draw-text"));
                                    } else {
                                        if (mod_shift(event)) {
                                            sp_te_adjust_tspan_letterspacing_screen(text, text_sel_start, text_sel_end, _desktop, -10);
                                        } else {
                                            sp_te_adjust_tspan_letterspacing_screen(text, text_sel_start, text_sel_end, _desktop, -1);
                                        }
                                        DocumentUndo::maybeDone(_desktop->getDocument(), "letterspacing:dec",  _("Contract letter spacing"), INKSCAPE_ICON("draw-text"));
                                    }
                                    _updateCursor();
                                    _updateTextSelection();
                                    ret = true;
                                    return;
                                }
                            }
                            break;
                        case GDK_KEY_greater:
                        case GDK_KEY_period:
                            if (text) {
                                if (mod_alt(event)) {
                                    if (mod_ctrl(event)) {
                                        if (mod_shift(event)) {
                                            sp_te_adjust_linespacing_screen(text, text_sel_start, text_sel_end, _desktop, 10);
                                        } else {
                                            sp_te_adjust_linespacing_screen(text, text_sel_start, text_sel_end, _desktop, 1);
                                        }
                                        DocumentUndo::maybeDone(_desktop->getDocument(), "linespacing:inc",  _("Expand line spacing"), INKSCAPE_ICON("draw-text"));
                                    } else {
                                        if (mod_shift(event)) {
                                            sp_te_adjust_tspan_letterspacing_screen(text, text_sel_start, text_sel_end, _desktop, 10);
                                        } else {
                                            sp_te_adjust_tspan_letterspacing_screen(text, text_sel_start, text_sel_end, _desktop, 1);
                                        }
                                        DocumentUndo::maybeDone(_desktop->getDocument(), "letterspacing:inc",  _("Expand letter spacing"), INKSCAPE_ICON("draw-text"));
                                    }
                                    _updateCursor();
                                    _updateTextSelection();
                                    ret = true;
                                    return;
                                }
                            }
                            break;
                        default:
                            break;
                    }

                    if (cursor_moved) {
                        if (!mod_shift(event)) {
                            text_sel_start = text_sel_end;
                        }
                        if (old_start != text_sel_start || old_end != text_sel_end) {
                            _updateCursor();
                            _updateTextSelection();
                        }
                        ret = true;
                    }
                } else {
                    ret = true; // consumed by the IM
                }
            } else { // do nothing if there's no object to type in - the key will be sent to parent context,
                // except up/down that are swallowed to prevent the zoom field from activation
                if ((group0_keyval == GDK_KEY_Up    ||
                     group0_keyval == GDK_KEY_Down  ||
                     group0_keyval == GDK_KEY_KP_Up ||
                     group0_keyval == GDK_KEY_KP_Down )
                    && !mod_ctrl_only(event))
                {
                    ret = true;
                } else if (group0_keyval == GDK_KEY_Escape) { // cancel rubberband
                    if (creating) {
                        creating = false;
                        ungrabCanvasEvents();
                        Rubberband::get(_desktop)->stop();
                    }
                } else if ((group0_keyval == GDK_KEY_x || group0_keyval == GDK_KEY_X) && mod_alt_only(event)) {
                    _desktop->setToolboxFocusTo("TextFontFamilyAction_entry");
                    ret = true;
                }
            }
        },
        [&] (KeyReleaseEvent const &event) {
            auto surface = _desktop->getToplevel()->get_surface()->gobj();
            if (!unimode && imc && gtk_im_context_filter_key(imc, false, surface, const_cast<GdkDevice*>(event.device->gobj()), event.time, event.keycode, (GdkModifierType)event.modifiers, event.group)) {
                ret = true;
            }
        },
        [&] (CanvasEvent const &event) {}
    );

    return ret || ToolBase::root_handler(event);
}

/**
 * Attempts to paste system clipboard into the currently edited text, returns true on success
 */
bool TextTool::pasteInline(Glib::ustring const clip_text)
{
    if (text || nascent_object) {
        // There is an active text object, or a new object was just created.

        if (!clip_text.empty()) {

            bool is_svg2 = false;
            auto const textitem = cast<SPText>(text);
            if (textitem) {
                is_svg2 = textitem->has_shape_inside() /*|| textitem->has_inline_size()*/; // Do now since hiding messes this up.
                textitem->hide_shape_inside();
            }

            auto const flowtext = cast<SPFlowtext>(text);
            if (flowtext) {
                flowtext->fix_overflow_flowregion(false);
            }

            // Fix for 244940
            // The XML standard defines the following as valid characters
            // (Extensible Markup Language (XML) 1.0 (Fourth Edition) paragraph 2.2)
            // char ::=     #x9 | #xA | #xD | [#x20-#xD7FF] | [#xE000-#xFFFD] | [#x10000-#x10FFFF]
            // Since what comes in off the paste buffer will go right into XML, clean
            // the text here.
            auto txt = clip_text;

            for (auto itr = txt.begin(); itr != txt.end(); ) {
                auto const paste_string_uchar = *itr;

                // Make sure we don't have a control character. We should really check
                // for the whole range above... Add the rest of the invalid cases from
                // above if we find additional issues
                if (paste_string_uchar >= 0x00000020 ||
                    paste_string_uchar == 0x00000009 ||
                    paste_string_uchar == 0x0000000A ||
                    paste_string_uchar == 0x0000000D)
                {
                    ++itr;
                } else {
                    itr = txt.erase(itr);
                }
            }

            if (!text) { // create text if none (i.e. if nascent_object)
                _setupText();
                nascent_object = false; // we don't need it anymore, having created a real <text>
            }

            // using indices is slow in ustrings. Whatever.
            Glib::ustring::size_type begin = 0;
            while (true) {
                auto const end = txt.find('\n', begin);

                if (end == Glib::ustring::npos || is_svg2) {
                    // Paste everything
                    if (begin != txt.length()) {
                        text_sel_start = text_sel_end = sp_te_replace(text, text_sel_start, text_sel_end, txt.substr(begin).c_str());
                    }
                    break;
                }

                // Paste up to new line, add line, repeat.
                text_sel_start = text_sel_end = sp_te_replace(text, text_sel_start, text_sel_end, txt.substr(begin, end - begin).c_str());
                text_sel_start = text_sel_end = sp_te_insert_line(text, text_sel_start);
                begin = end + 1;
            }
            if (textitem) {
                textitem->show_shape_inside();
            }
            if (flowtext) {
                flowtext->fix_overflow_flowregion(true);
            }
            DocumentUndo::done(_desktop->getDocument(), _("Paste text"), INKSCAPE_ICON("draw-text"));

            return true;
        }
        
    } // FIXME: else create and select a new object under cursor!

    return false;
}

/**
 * Gets the raw characters that comprise the currently selected text, converting line
 * breaks into lf characters.
*/
Glib::ustring get_selected_text(TextTool const &tool)
{
    if (!tool.textItem()) {
        return {};
    }

    return sp_te_get_string_multiline(tool.textItem(), tool.text_sel_start, tool.text_sel_end);
}

SPCSSAttr *get_style_at_cursor(TextTool const &tool)
{
    if (!tool.textItem()) {
        return nullptr;
    }

    if (auto obj = sp_te_object_at_position(tool.textItem(), tool.text_sel_end)) {
        return take_style_from_item(const_cast<SPObject*>(obj));
    }

    return nullptr;
}

/**
 Deletes the currently selected characters. Returns false if there is no
 text selection currently.
*/
bool TextTool::deleteSelection()
{
    if (!text) {
        return false;
    }

    if (text_sel_start == text_sel_end) {
        return false;
    }

    iterator_pair pair;
    bool success = sp_te_delete(text, text_sel_start, text_sel_end, pair);

    if (success) {
        text_sel_start = text_sel_end = pair.first;
    } else { // nothing deleted
        text_sel_start = pair.first;
        text_sel_end = pair.second;
    }

    _updateCursor();
    _updateTextSelection();

    return true;
}

/**
 * \param selection Should not be NULL.
 */
void TextTool::_selectionChanged(Selection *selection)
{
    g_assert(selection);
    auto item = selection->singleItem();

    if (text && item != text) {
        _forgetText();
    }
    text = nullptr;

    shape_editor->unset_item();
    if (is<SPText>(item) || is<SPFlowtext>(item)) {
        shape_editor->set_item(item);

        text = item;
        if (auto layout = te_get_layout(text)) {
            text_sel_start = text_sel_end = layout->end();
        }
    } else {
        text = nullptr;
    }

    // we update cursor without scrolling, because this position may not be final;
    // item_handler moves cursor to the point of click immediately
    _updateCursor(false);
    _updateTextSelection();
}

void TextTool::_selectionModified(Selection */*selection*/, unsigned /*flags*/)
{
    bool scroll = !shape_editor->has_knotholder() ||
                  !shape_editor->knotholder->is_dragging();
    _updateCursor(scroll);
    _updateTextSelection();
}

bool TextTool::_styleSet(SPCSSAttr const *css)
{
    if (!text) {
        return false;
    }
    if (text_sel_start == text_sel_end) {
        return false;    // will get picked up by the parent and applied to the whole text object
    }

    sp_te_apply_style(text, text_sel_start, text_sel_end, css);

    // This is a bandaid fix... whenever a style is changed it might cause the text layout to
    // change which requires rewriting the 'x' and 'y' attributes of the tpsans for Inkscape
    // multi-line text (with sodipodi:role="line"). We need to rewrite the repr after this is
    // done. rebuldLayout() will be called a second time unnecessarily.
    if (auto sptext = cast<SPText>(text)) {
        sptext->rebuildLayout();
        sptext->updateRepr();
    }

    DocumentUndo::done(_desktop->getDocument(), _("Set text style"), INKSCAPE_ICON("draw-text"));
    _updateCursor();
    _updateTextSelection();
    return true;
}

int TextTool::_styleQueried(SPStyle *style, int property)
{
    if (!text) {
        return QUERY_STYLE_NOTHING;
    }

    auto layout = te_get_layout(this->text);
    if (!layout) {
        return QUERY_STYLE_NOTHING;
    }

    _validateCursorIterators();

    Inkscape::Text::Layout::iterator begin_it, end_it;
    if (text_sel_start < text_sel_end) {
        begin_it = text_sel_start;
        end_it = text_sel_end;
    } else {
        begin_it = text_sel_end;
        end_it = text_sel_start;
    }
    if (begin_it == end_it) {
        if (!begin_it.prevCharacter()) {
            end_it.nextCharacter();
        }
    }

    std::vector<SPItem*> styles_list;
    for (auto it = begin_it; it < end_it; it.nextStartOfSpan()) {
        SPObject *pos_obj = nullptr;
        layout->getSourceOfCharacter(it, &pos_obj);
        if (!pos_obj) {
            continue;
        }
        if (!pos_obj->parent) { // the string is not in the document anymore (deleted)
            return 0;
        }

        if (is<SPString>(pos_obj)) {
           pos_obj = pos_obj->parent;   // SPStrings don't have style
        }
        styles_list.emplace_back(cast_unsafe<SPItem>(pos_obj));
    }
    std::reverse(styles_list.begin(), styles_list.end());

    return sp_desktop_query_style_from_list(styles_list, style, property);
}

void TextTool::_validateCursorIterators()
{
    if (!text) {
        return;
    }
    if (auto layout = te_get_layout(text)) { // undo can change the text length without us knowing it
        layout->validateIterator(&text_sel_start);
        layout->validateIterator(&text_sel_end);
    }
}

void TextTool::_resetBlinkTimer()
{
    blink_conn = Glib::signal_timeout().connect([this] { _blinkCursor(); return true; }, blink_time);
}

void TextTool::_showCursor()
{
    show = true;
    phase = false;
    cursor->set_stroke(0x000000ff);
    cursor->set_visible(true);
    _resetBlinkTimer();
}

void TextTool::_updateCursor(bool scroll_to_see)
{
    if (text) {
        Geom::Point p0, p1;
        sp_te_get_cursor_coords(text, text_sel_end, p0, p1);
        Geom::Point const d0 = p0 * text->i2dt_affine();
        Geom::Point const d1 = p1 * text->i2dt_affine();

        // scroll to show cursor
        if (scroll_to_see) {

            // We don't want to scroll outside the text box area (i.e. when there is hidden text)
            // or we could end up in Timbuktu.
            bool scroll = true;
            if (auto sptext = cast<SPText>(text)) {
                Geom::OptRect opt_frame = sptext->get_frame();
                if (opt_frame && !opt_frame->contains(p0)) {
                    scroll = false;
                }
            } else if (auto spflowtext = cast<SPFlowtext>(text)) {
                SPItem *frame = spflowtext->get_frame(nullptr); // first frame only
                Geom::OptRect opt_frame = frame->geometricBounds();
                if (opt_frame && !opt_frame->contains(p0)) {
                    scroll = false;
                }
            }

            if (scroll) {
                Geom::Point const center = _desktop->current_center();
                if (Geom::L2(d0 - center) > Geom::L2(d1 - center)) {
                    // unlike mouse moves, here we must scroll all the way at first shot, so we override the autoscrollspeed
                    _desktop->scroll_to_point(d0);
                } else {
                    _desktop->scroll_to_point(d1);
                }
            }
        }

        cursor->set_coords(d0, d1);
        _showCursor();

        /* fixme: ... need another transformation to get canvas widget coordinate space? */
        if (imc) {
            GdkRectangle im_cursor = { 0, 0, 1, 1 };
            Geom::Point const top_left = _desktop->get_display_area().corner(0);
            Geom::Point const im_d0 =    _desktop->d2w(d0 - top_left);
            Geom::Point const im_d1 =    _desktop->d2w(d1 - top_left);
            Geom::Rect const im_rect(im_d0, im_d1);
            im_cursor.x = std::floor(im_rect.left());
            im_cursor.y = std::floor(im_rect.top());
            im_cursor.width = std::floor(im_rect.width());
            im_cursor.height = std::floor(im_rect.height());
            gtk_im_context_set_cursor_location(imc, &im_cursor);
        }

        auto layout = te_get_layout(text);
        int const nChars = layout->iteratorToCharIndex(layout->end());
        char const *edit_message = ngettext("Type or edit text (%d character%s); <b>Enter</b> to start new line.", "Type or edit text (%d characters%s); <b>Enter</b> to start new line.", nChars);
        char const *edit_message_flowed = ngettext("Type or edit flowed text (%d character%s); <b>Enter</b> to start new paragraph.", "Type or edit flowed text (%d characters%s); <b>Enter</b> to start new paragraph.", nChars);
        bool truncated = layout->inputTruncated();
        char const *trunc = truncated ? _(" [truncated]") : "";

        if (truncated) {
            frame->set_stroke(0xff0000ff);
        } else {
            frame->set_stroke(0x0000ff7f);
        }

        std::vector<SPItem const *> shapes;
        std::unique_ptr<Shape> exclusion_shape;
        double padding = 0.0;

        // Frame around text
        if (auto spflowtext = cast<SPFlowtext>(text)) {
            auto frame = spflowtext->get_frame(nullptr); // first frame only
            shapes.emplace_back(frame);

            message_context->setF(NORMAL_MESSAGE, edit_message_flowed, nChars, trunc);

        } else if (auto sptext = cast<SPText>(text)) {
            if (text->style->shape_inside.set) {
                for (auto const *href : text->style->shape_inside.hrefs) {
                    shapes.push_back(href->getObject());
                }
                if (text->style->shape_padding.set) {
                    // Calculate it here so we never show padding on FlowText or non-flowed Text (even if set)
                    padding = text->style->shape_padding.computed;
                }
                if (text->style->shape_subtract.set) {
                    // Find union of all exclusion shapes for later use
                    exclusion_shape = sptext->getExclusionShape();
                }
                message_context->setF(NORMAL_MESSAGE, edit_message_flowed, nChars, trunc);
            } else {
                for (auto &child : text->children) {
                    if (auto textpath = cast<SPTextPath>(&child)) {
                        shapes.emplace_back(sp_textpath_get_path_item(textpath));
                    }
                }
                message_context->setF(NORMAL_MESSAGE, edit_message, nChars, trunc);
            }
        }

        SPCurve curve;
        for (auto shape_item : shapes) {
            if (auto shape = cast<SPShape>(shape_item)) {
                if (shape->curve()) {
                    curve.append(shape->curve()->transformed(shape->transform));
                }
            }
        }

        if (!curve.is_empty()) {
            bool has_padding = std::abs(padding) > 1e-12;

            if (has_padding || exclusion_shape) {
                // Should only occur for SVG2 autoflowed text
                // See sp-text.cpp function _buildLayoutInit()
                Path temp;
                temp.LoadPathVector(curve.get_pathvector());

                // Get initial shape-inside curve
                auto uncross = std::make_unique<Shape>();
                {
                    Shape sh;
                    temp.ConvertWithBackData(0.25); // Convert to polyline
                    temp.Fill(&sh, 0);
                    uncross->ConvertToShape(&sh);
                }

                // Get padded shape exclusion
                if (has_padding) {
                    Shape pad_shape;
                    {
                        Path padded;
                        Path padt;
                        Shape sh;
                        padt.LoadPathVector(curve.get_pathvector());
                        padt.Outline(&padded, padding, join_round, butt_straight, 20.0);
                        padded.ConvertWithBackData(1.0); // Convert to polyline
                        padded.Fill(&sh, 0);
                        pad_shape.ConvertToShape(&sh);
                    }

                    auto copy = std::make_unique<Shape>();
                    copy->Booleen(uncross.get(), &pad_shape, padding > 0.0 ? bool_op_diff : bool_op_union);
                    uncross = std::move(copy);
                }

                // Remove exclusions plus margins from padding frame
                if (exclusion_shape && exclusion_shape->hasEdges()) {
                    auto copy = std::make_unique<Shape>();
                    copy->Booleen(uncross.get(), exclusion_shape.get(), bool_op_diff);
                    uncross = std::move(copy);
                }

                uncross->ConvertToForme(&temp);
                padding_frame->set_bpath(temp.MakePathVector() * text->i2dt_affine());
                padding_frame->set_visible(true);
            } else {
                padding_frame->set_visible(false);
            }

            // Transform curve after doing padding.
            curve.transform(text->i2dt_affine());
            frame->set_bpath(&curve);
            frame->set_visible(true);
        } else {
            frame->set_visible(false);
            padding_frame->set_visible(false);
        }

    } else {
        cursor->set_visible(false);
        frame->set_visible(false);
        show = false;
        if (!nascent_object) {
            message_context->set(NORMAL_MESSAGE, _("<b>Click</b> to select or create text, <b>drag</b> to create flowed text; then type.")); // FIXME: this is a copy of string from tools-switch, do not desync
        }
    }

    _desktop->emit_text_cursor_moved(this, this);
}

void TextTool::_updateTextSelection()
{
    text_selection_quads.clear();

    if (text) {
        auto const quads = sp_te_create_selection_quads(text, text_sel_start, text_sel_end, text->i2dt_affine());
        for (int i = 0; i + 3 < quads.size(); i += 4) {
            auto quad = make_canvasitem<CanvasItemQuad>(_desktop->getCanvasControls(), quads[i], quads[i+1], quads[i+2], quads[i+3]);
            quad->set_fill(0x00777777); // Semi-transparent blue as Cairo cannot do inversion.
            quad->set_visible(true);
            text_selection_quads.emplace_back(std::move(quad));
        }
    }

    if (shape_editor && shape_editor->knotholder) {
        shape_editor->knotholder->update_knots();
    }
}

void TextTool::_blinkCursor()
{
    if (!show) {
        return;
    }

    if (phase) {
        phase = false;
        cursor->set_stroke(0x000000ff);
    } else {
        phase = true;
        cursor->set_stroke(0xffffffff);
    }

    cursor->set_visible(true);
}

void TextTool::_forgetText()
{
    if (!text) {
        return;
    }
    auto ti = text;
    (void)ti;
    /* We have to set it to zero,
     * or selection changed signal messes everything up */
    text = nullptr;

/* FIXME: this automatic deletion when nothing is inputted crashes the XML editor and also crashes when duplicating an empty flowtext.
    So don't create an empty flowtext in the first place? Create it when first character is typed.
    */
/*
    if ((is<SPText>(ti) || is<SPFlowtext>(ti)) && sp_te_input_is_empty(ti)) {
        auto text_repr = ti->getRepr();
        // the repr may already have been unparented
        // if we were called e.g. as the result of
        // an undo or the element being removed from
        // the XML editor
        if (text_repr && text_repr->parent()) {
            sp_repr_unparent(text_repr);
            DocumentUndo::done(_desktop->getDocument(), _("Remove empty text"), INKSCAPE_ICON("draw-text"));
        }
    }
*/
}

void TextTool::_commit(GtkIMContext *, char *string)
{
    if (!text) {
        _setupText();
        nascent_object = false; // we don't need it anymore, having created a real <text>
    }

    text_sel_start = text_sel_end = sp_te_replace(text, text_sel_start, text_sel_end, string);
    _updateCursor();
    _updateTextSelection();

    DocumentUndo::done(text->document, _("Type text"), INKSCAPE_ICON("draw-text"));
}

void TextTool::placeCursor(SPObject *other_text, Text::Layout::iterator where)
{
    _desktop->getSelection()->set(other_text);
    text_sel_start = text_sel_end = where;
    _updateCursor();
    _updateTextSelection();
}

void TextTool::placeCursorAt(SPObject *other_text, Geom::Point const &p)
{
    _desktop->getSelection()->set(other_text);
    placeCursor(other_text, sp_te_get_position_by_coords(text, p));
}

Text::Layout::iterator const *get_cursor_position(TextTool const &tool, SPObject const *other_text)
{
    if (other_text != tool.textItem()) {
        return nullptr;
    }
    return &tool.text_sel_end;
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
