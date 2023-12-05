// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author:
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Copyright (C) 2023 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "toolbar-menu-button.h"

#include <gtkmm/box.h>

namespace Inkscape::UI::Widget {

ToolbarMenuButton::ToolbarMenuButton(BaseObjectType *cobject, Glib::RefPtr<Gtk::Builder> const &)
    : Gtk::MenuButton(cobject)
{
    // Workaround to hide the widget by default and when there are no children in the popover.
    set_visible(false);
    signal_show().connect([this]{
        g_assert(_popover_box);
        if (!_popover_box->get_first_child()) {
            set_visible(false);
        }
    }, false);
}

void ToolbarMenuButton::init(int priority, std::string tag, Gtk::Box *popover_box, std::vector<Gtk::Widget *> &children)
{
    _priority = priority;
    _tag = std::move(tag);
    _popover_box = popover_box;

    // Automatically fetch all the children having "tag" as their style class.
    // This approach will allow even non-programmers to group the children into
    // popovers. Store the position of the child in the toolbar as well. It'll
    // be used to reinsert the child in the right place when the toolbar is
    // large enough.
    int pos = 0;
    for (auto child : children) {
        bool const is_child = child->has_css_class(_tag);
        if (is_child) {
            _children.emplace_back(pos, child);
        }
        pos++;
    }
}

static int minw(Gtk::Widget const *widget)
{
    g_assert(widget);
    if (!widget) return 0;

    int min = 0;
    int ignore = 0;
    widget->measure(Gtk::Orientation::HORIZONTAL, -1, min, ignore, ignore, ignore);
    return min;
};

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
