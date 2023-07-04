// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Murray C
 *
 * Copyright (C) 2012 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "frame.h"

namespace Inkscape::UI::Widget {

Frame::Frame(Glib::ustring const &label_text /*= {}*/, bool label_bold /*= true*/ )
    : _label(label_text, Gtk::Align::END, Gtk::Align::CENTER, true)
{
    add_css_class("flat");

    set_label_widget(_label);
    set_label(label_text, label_bold);
}

void
Frame::add(Widget& widget)
{
    Gtk::Frame::set_child(widget);
    set_padding(4, 0, 8, 0);
}

void
Frame::set_label(Glib::ustring const &label_text, bool label_bold /*= true*/)
{
    if (label_bold) {
        _label.set_markup(Glib::ustring("<b>") + label_text + "</b>");
    } else {
        _label.set_text(label_text);
    }
}

void
Frame::set_padding(unsigned const padding_top, unsigned const padding_bottom,
                   unsigned const padding_left, unsigned const padding_right)
{
    auto child = get_child();

    if(child)
    {
        child->set_margin_top(padding_top);
        child->set_margin_bottom(padding_bottom);
        child->set_margin_start(padding_left);
        child->set_margin_end(padding_right);
    }
}

Gtk::Label const *
Frame::get_label_widget() const
{
    return &_label;
}

} // namespace Inkscape::UI::Widget

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
