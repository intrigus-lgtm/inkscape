// SPDX-License-Identifier: GPL-2.0-or-later


#ifndef INKSCAPE_UI_WIDGET_CANVAS_NOTICE_H
#define INKSCAPE_UI_WIDGET_CANVAS_NOTICE_H

#include <glibmm/refptr.h>
#include <gtkmm/revealer.h>

#include "helper/auto-connection.h"

namespace Gtk {
class Builder;
class Image;
class Label;
} // namespace Gtk

namespace Inkscape::UI::Widget {

class CanvasNotice : public Gtk::Revealer {
public:
    static CanvasNotice *create();

    CanvasNotice(BaseObjectType *cobject, Glib::RefPtr<Gtk::Builder> const &builder);
    void show(Glib::ustring const &msg, unsigned timeout = 0);
    void hide();

private:
    Glib::RefPtr<Gtk::Builder> _builder;

    Gtk::Image& _icon;
    Gtk::Label& _label;

    Inkscape::auto_connection _timeout;
};

} // namespace Inkscape::UI::Widget

#endif // INKSCAPE_UI_WIDGET_CANVAS_NOTICE_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:
