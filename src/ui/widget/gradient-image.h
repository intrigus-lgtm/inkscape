// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * A simple gradient preview
 *
 * Author:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 2001-2002 Lauris Kaplinski
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_SP_GRADIENT_IMAGE_H
#define SEEN_SP_GRADIENT_IMAGE_H

#include <cairomm/refptr.h>
#include <glibmm/refptr.h>
#include <gtkmm/drawingarea.h>

#include "helper/auto-connection.h"

class SPGradient;
class SPObject;
class SPStop;

namespace Gdk {
class Pixbuf;
} // namespace Gdk

namespace Inkscape::UI::Widget {

class GradientImage : public Gtk::DrawingArea {
public:
    GradientImage(SPGradient *gradient);
    void set_gradient(SPGradient *gr);

private:
    SPGradient *_gradient = nullptr;
    auto_connection _release_connection;
    auto_connection _modified_connection;

    void draw_func(Cairo::RefPtr<Cairo::Context> const &cr, int width, int height);
    void gradient_release (SPObject const *obj);
    void gradient_modified(SPObject const *obj, guint flags);
};

} // namespace Inkscape::UI::Widget

void                       sp_gradient_draw         (SPGradient *gr, int width, int height,
                                                     cairo_t *ct);
GdkPixbuf                 *sp_gradient_to_pixbuf    (SPGradient *gr, int width, int height);
Glib::RefPtr<Gdk::Pixbuf>  sp_gradient_to_pixbuf_ref(SPGradient *gr, int width, int height);
Glib::RefPtr<Gdk::Pixbuf>  sp_gradstop_to_pixbuf_ref(SPStop     *gr, int width, int height);

#endif // SEEN_SP_GRADIENT_IMAGE_H

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
