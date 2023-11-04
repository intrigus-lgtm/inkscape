// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * A light-weight widget containing an Inkscape canvas for rendering an SVG.
 */
/*
 * Authors:
 *   Tavmjong Bah <tavmjong@free.fr>
 *
 * Includes code moved from svg-view.cpp authored by:
 *   MenTaLGuy
 *   Ralf Stephan <ralf@ark.in-berlin.de>
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright (C) 2018 Authors
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 * Read the file 'COPYING' for more information.
 *
 */

#include "svg-view-widget.h"

#include <iostream>
#include <2geom/transforms.h>
#include <giomm/appinfo.h>
#include <gtkmm/window.h>

#include "document.h"
#include "display/drawing.h"
#include "display/control/canvas-item-drawing.h"
#include "display/control/canvas-item-group.h"
#include "object/sp-item.h"
#include "object/sp-root.h"
#include "object/sp-anchor.h"
#include "ui/widget/canvas.h"
#include "ui/widget/events/canvas-event.h"
#include "util/units.h"

namespace Inkscape::UI::View {

SVGViewWidget::SVGViewWidget(SPDocument *document)
{
    _canvas = std::make_unique<Inkscape::UI::Widget::Canvas>();

    _canvas->set_expand(true);
    set_expand(false);
    set_child(*_canvas);

    _drawing = new Inkscape::CanvasItemDrawing(_canvas->get_canvas_item_root());
    _canvas->set_drawing(_drawing->get_drawing());
    _drawing->connect_drawing_event(sigc::mem_fun(*this, &SVGViewWidget::event));
    _drawing->get_drawing()->setCursorTolerance(0);

    setDocument(document);
}

SVGViewWidget::~SVGViewWidget()
{
    setDocument(nullptr);
}

static void set_layer_modes(SPObject *obj, unsigned dkey)
{
    if (is<SPGroup>(obj) && !is<SPAnchor>(obj)) {
        cast_unsafe<SPGroup>(obj)->setLayerDisplayMode(dkey, SPGroup::LAYER);
    }

    for (auto &c : obj->children) {
        set_layer_modes(&c, dkey);
    }
}

void SVGViewWidget::setDocument(SPDocument *document)
{
    // Clear old document
    if (_document) {
        _document->getRoot()->invoke_hide(_dkey); // Removed from display tree
    }

    _document = document;

    // Add new document
    if (_document) {
        auto drawing_item = _document->getRoot()->invoke_show(
            *_drawing->get_drawing(),
            _dkey,
            SP_ITEM_SHOW_DISPLAY);

        if (drawing_item) {
            _drawing->get_drawing()->root()->prependChild(drawing_item);
        }

        set_layer_modes(_document->getRoot(), _dkey);

        doRescale();
    }
}

void SVGViewWidget::setResize(int width, int height)
{
    // Triggers size_allocation which calls SVGViewWidget::size_allocate.
    set_size_request(width, height);
    queue_resize();
}

void SVGViewWidget::on_size_allocate(int const width, int const height, int const baseline)
{
    if (!(_width == width && _height == height)) {
        if (width < 0 || height < 0) {
            std::cerr << "SVGViewWidget::size_allocate: negative dimensions!" << std::endl;
        } else {
            _width = width;
            _height = height;

            doRescale();
        }
    }

    Bin::on_size_allocate(width, height, baseline);
}

/**
 * Callback connected with drawing_event.
 * Results in a cursor change over <a></a> links, and allows clicking them.
 */
bool SVGViewWidget::event(CanvasEvent const &event, DrawingItem *drawing_item)
{
    auto const spanchor = drawing_item ? cast<SPAnchor>(drawing_item->getItem()) : nullptr;
    auto const href = spanchor ? spanchor->href : nullptr;

    inspect_event(event,
        [&] (ButtonPressEvent const &event) {
            if (event.num_press == 1 && event.button == 1) {
                _clicking = true;
            }
        },
        [&] (MotionEvent const &event) {
            _clicking = false;
        },
        [&] (ButtonReleaseEvent const &event) {
            if (event.button == 1 && _clicking && href) {
                Gio::AppInfo::launch_default_for_uri(href);
            }
            _clicking = false;
        },
        [&] (EnterEvent const &event) {
            if (href) {
                set_cursor("pointer");
                set_tooltip_text(href);
            }
        },
        [&] (LeaveEvent const &event) {
            if (href) {
                set_cursor(Glib::RefPtr<Gdk::Cursor>{});
                set_tooltip_text("");
            }
        },
        [&] (CanvasEvent const &event) {}
    );

    return true;
}

void SVGViewWidget::doRescale()
{
    if (!_document) {
        std::cerr << "SVGViewWidget::doRescale: No document!" << std::endl;
        return;
    }

    if (_document->getWidth().value("px") < 1e-9) {
        std::cerr << "SVGViewWidget::doRescale: Width too small!" << std::endl;
        return;
    }

    if (_document->getHeight().value("px") < 1e-9) {
        std::cerr << "SVGViewWidget::doRescale: Height too small!" << std::endl;
        return;
    }

    double x_offset = 0.0;
    double y_offset = 0.0;
    if (_rescale) {
        _hscale = _width / _document->getWidth().value("px");
        _vscale = _height / _document->getHeight().value("px");
        if (_keepaspect) {
            if (_hscale > _vscale) {
                _hscale = _vscale;
                x_offset = (_document->getWidth().value("px") * _hscale - _width) / 2.0;
            } else {
                _vscale = _hscale;
                y_offset = (_document->getHeight().value("px") * _vscale - _height) / 2.0;
            }
        }
    }

    if (_drawing) {
        _canvas->set_affine(Geom::Scale(_hscale, _vscale));
        _canvas->set_pos(Geom::Point(x_offset, y_offset));
    }
}

} // namespace Inkscape::UI::View

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
