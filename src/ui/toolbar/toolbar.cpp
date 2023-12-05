// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * TODO: insert short description here
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "toolbar.h"

#include "ui/util.h"
#include "ui/widget/toolbar-menu-button.h"

namespace Inkscape::UI::Toolbar {

Toolbar::Toolbar(SPDesktop *desktop)
    : _desktop(desktop)
{}

void Toolbar::addCollapsibleButton(UI::Widget::ToolbarMenuButton *button)
{
    _expanded_menu_btns.emplace(button);
}

void Toolbar::measure_vfunc(Gtk::Orientation orientation, int for_size, int &min, int &nat, int &min_baseline, int &nat_baseline) const
{
    _toolbar->measure(orientation, for_size, min, nat, min_baseline, nat_baseline);

    if (_toolbar->get_orientation() == orientation) {
        // HACK: Return too-small value to allow shrinking.
        min = 0;
    }
}

void Toolbar::on_size_allocate(int width, int height, int baseline)
{
    _resize_handler(width, height);
    UI::Widget::Bin::on_size_allocate(width, height, baseline);
}

static int min_dimension(Gtk::Widget const *widget, Gtk::Orientation const orientation)
{
    int min = 0;
    int ignore = 0;
    widget->measure(orientation, -1, min, ignore, ignore, ignore);
    return min;
};

void Toolbar::_resize_handler(int width, int height)
{
    if (!_toolbar) {
        return;
    }

    auto const orientation = _toolbar->get_orientation();
    auto const allocated_size = orientation == Gtk::Orientation::VERTICAL ? height : width;
    int min_size = min_dimension(_toolbar, orientation);

    if (allocated_size < min_size) {
        // Shrinkage required.

        // While there are still expanded buttons to collapse...
        while (allocated_size < min_size && !_expanded_menu_btns.empty()) {
            // Collapse the topmost expanded button.
            auto menu_btn = _expanded_menu_btns.top();
            _move_children(_toolbar, menu_btn->get_popover_box(), menu_btn->get_children());
            menu_btn->set_visible(true);

            int old = min_size;
            min_size = min_dimension(_toolbar, orientation);
            int change = old - min_size;

            _expanded_menu_btns.pop();
            _collapsed_menu_btns.push({menu_btn, change});
        }

    } else if (allocated_size > min_size) {
        // Once the allocated size of the toolbar is greater than its
        // minimum size, try to re-insert a group of elements back
        // into the toolbar.

        // While there are collapsed buttons to expand...
        while (!_collapsed_menu_btns.empty()) {
            // See if we have enough space to expand the topmost collapsed button.
            auto [menu_btn, change] = _collapsed_menu_btns.top();
            int req_size = min_size + change;

            if (req_size > allocated_size) {
                // Not enough space - stop.
                break;
            }

            // Move a group of widgets back into the toolbar.
            _move_children(menu_btn->get_popover_box(), _toolbar, menu_btn->get_children(), true);
            menu_btn->set_visible(false);

            _collapsed_menu_btns.pop();
            _expanded_menu_btns.push(menu_btn);

            min_size = min_dimension(_toolbar, orientation);
        }
    }
}

void Toolbar::_move_children(Gtk::Box *src, Gtk::Box *dest, std::vector<std::pair<int, Gtk::Widget *>> const &children, bool is_expanding)
{
    for (auto [pos, child] : children) {
        src->remove(*child);

        // is_expanding will be true when the children are being put back into
        // the toolbar. In that case, insert the children at their previous
        // positions.
        if (is_expanding) {
            if (pos == 0) {
                dest->insert_child_at_start(*child);
            } else {
                dest->insert_child_after(*child, UI::get_nth_child(*dest, pos - 1));
            }
        } else {
            dest->append(*child);
        }
    }
}

} // namespace Inkscape::UI::Toolbar

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
