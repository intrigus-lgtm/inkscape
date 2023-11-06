// SPDX-License-Identifier: GPL-2.0-or-later
/* Authors:
 *   Thomas Holder
 *
 * Copyright (C) 2020 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "scroll-utils.h"
#include <gtkmm/scrolledwindow.h>
#include "ui/util.h"

namespace Inkscape::UI::Widget {

/**
 * Get the first ancestor which is scrollable.
 */
Gtk::Widget *get_scrollable_ancestor(Gtk::Widget *widget)
{
    g_return_val_if_fail(widget, nullptr);

    return for_each_parent(*widget, [](Gtk::Widget &parent)
    {
        auto const scrollable = dynamic_cast<Gtk::ScrolledWindow *>(&parent);
        return scrollable ? ForEachResult::_break : ForEachResult::_continue;
    });
}

/**
 * Get the first ancestor which is scrollable.
 */
Gtk::Widget const *get_scrollable_ancestor(Gtk::Widget const * const widget)
{
    g_return_val_if_fail(widget, nullptr);

    auto const mut_widget = const_cast<Gtk::Widget *>(widget);
    auto const mut_ancestor = get_scrollable_ancestor(mut_widget);
    return const_cast<Gtk::Widget const *>(mut_ancestor);
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
