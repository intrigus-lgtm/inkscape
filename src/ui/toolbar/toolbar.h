// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * TODO: insert short description here
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_TOOLBAR_H
#define SEEN_TOOLBAR_H

#include <stack>
#include <utility>
#include <vector>

#include <gtkmm/box.h>

class SPDesktop;

namespace Inkscape::UI::Widget { class ToolbarMenuButton; }

namespace Inkscape::UI::Toolbar {

/**
 * \brief Base class for all toolbars.
 */
class Toolbar : public Gtk::Box
{
protected:
    Toolbar(SPDesktop *desktop);

    SPDesktop *const _desktop;
    Gtk::Box *_toolbar = nullptr;

    void addCollapsibleButton(UI::Widget::ToolbarMenuButton *button);

    void measure(Gtk::Orientation orientation, int for_size, int &min, int &nat, int &min_baseline, int &nat_baseline) const;

private:
    std::stack<UI::Widget::ToolbarMenuButton *> _expanded_menu_btns;
    std::stack<UI::Widget::ToolbarMenuButton *> _collapsed_menu_btns;

    void _resize_handler(Gtk::Allocation &allocation);
    void _move_children(Gtk::Box *src, Gtk::Box *dest, std::vector<std::pair<int, Gtk::Widget *>> const &children, bool is_expanding = false);
};

} // namespace Inkscape::UI::Toolbar

#endif // SEEN_TOOLBAR_H

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
