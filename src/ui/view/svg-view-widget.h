// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * A light-weight widget containing an Inkscape canvas for rendering an SVG.
 */
/*
 * Authors:
 *   Tavmjong Bah <tavmjong@free.fr>
 *
 * Copyright (C) 2018 Authors
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 * Read the file 'COPYING' for more information.
 *
 */

#ifndef INKSCAPE_UI_VIEW_SVG_VIEW_WIDGET_H
#define INKSCAPE_UI_VIEW_SVG_VIEW_WIDGET_H

#include <gtkmm/box.h>

class SPDocument;

namespace Inkscape {

class CanvasItemDrawing;
class CanvasItemGroup;
struct CanvasEvent;
class DrawingItem;

namespace UI {

namespace Widget { class Canvas; }

namespace View {

/**
 * A light-weight widget containing an Inkscape canvas for rendering an SVG.
 */
class SVGViewWidget final : public Gtk::Box
{
public:
    SVGViewWidget(SPDocument *document);
    ~SVGViewWidget() final;

    void setDocument(SPDocument *document);
    void setResize(int width, int height);

private:
    UI::Widget::Canvas *_canvas = nullptr;
    bool _clicking = false;

    void size_allocate_vfunc(int width, int height, int baseline) final;

    bool event(CanvasEvent const &event, DrawingItem *drawing_item);

public:
    // From SVGView ---------------------------------
    SPDocument*     _document = nullptr;
    unsigned        _dkey     = 0;
    CanvasItemGroup   *_parent  = nullptr;
    CanvasItemDrawing *_drawing = nullptr;
    double          _hscale   = 1.0;     ///< horizontal scale
    double          _vscale   = 1.0;     ///< vertical scale
    bool            _rescale  = false;   ///< whether to rescale automatically
    bool            _keepaspect = false;
    double          _width    = 0.0;
    double          _height   = 0.0;

    /**
     * Helper function that sets rescale ratio.
     */
    void doRescale();
};

} // namespace View
} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_VIEW_SVG_VIEW_WIDGET_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8 :
