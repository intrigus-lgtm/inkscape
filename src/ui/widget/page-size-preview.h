// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author:
 *   Michael Kowalski
 *
 * Copyright (C) 2021 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_WIDGET_PAGE_SIZE_PREVIEW_H
#define INKSCAPE_UI_WIDGET_PAGE_SIZE_PREVIEW_H

#include <gtkmm/drawingarea.h>

namespace Inkscape {    
namespace UI {
namespace Widget {

class PageSizePreview : public Gtk::DrawingArea {
public:
    PageSizePreview();
    // static PageSizePreview* create();

    void set_desk_color(unsigned int rgba);
    void set_page_color(unsigned int rgba);
    void set_border_color(unsigned int rgba);
    void draw_border(bool border);
    void enable_drop_shadow(bool shadow);
    void set_page_size(double width, double height);
    void enable_checkerboard(bool checkerboard);

    ~PageSizePreview() override = default;

private:
    void draw_func(Cairo::RefPtr<Cairo::Context> const &cr, int width, int height);
    unsigned int _border_color = 0x0000001f;
    unsigned int _page_color = 0xffffff00;
    unsigned int _desk_color = 0xc8c8c8ff;
    bool _draw_border = true;
    bool _draw_shadow = true;
    bool _draw_checkerboard = false;
    double _width = 10;
    double _height = 7;
};

} } } // namespace Inkscape/Widget/UI

#endif // INKSCAPE_UI_WIDGET_PAGE_SIZE_PREVIEW_H

