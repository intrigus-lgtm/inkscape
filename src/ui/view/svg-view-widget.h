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

#include <memory>

#include "ui/widget/bin.h"

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
class SVGViewWidget : public UI::Widget::Bin
{
public:
    SVGViewWidget(SPDocument *document);
    ~SVGViewWidget() override;

    void setDocument(SPDocument *document);
    void setResize(int width, int height);

protected:
    void on_size_allocate(int width, int height, int baseline) override;

private:
    std::unique_ptr<UI::Widget::Canvas> _canvas;
    bool _clicking = false;

    SPDocument *_document = nullptr;
    unsigned _dkey = 0;
    CanvasItemGroup *_parent  = nullptr;
    CanvasItemDrawing *_drawing = nullptr;
    double _hscale = 1.0; ///< Horizontal scale
    double _vscale = 1.0; ///< Vertical scale
    bool _rescale = true; ///< Whether to rescale automatically
    bool _keepaspect = true;
    double _width = 0.0;
    double _height = 0.0;

    bool event(CanvasEvent const &event, DrawingItem *drawing_item);

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
