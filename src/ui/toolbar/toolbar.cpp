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

#include "ui/widget/toolbar-menu-button.h"

namespace Inkscape::UI::Toolbar {

Toolbar::Toolbar(SPDesktop *desktop)
    : _desktop(desktop)
{}

void Toolbar::addCollapsibleButton(UI::Widget::ToolbarMenuButton *button)
{
    _expanded_menu_btns.emplace(button);
}

void Toolbar::get_preferred_width_vfunc(int &min_w, int &nat_w) const
{
    _toolbar->get_preferred_width(min_w, nat_w);

    if (_toolbar->get_orientation() == Gtk::Orientation::HORIZONTAL && !_expanded_menu_btns.empty()) {
        // HACK: Return too-small value to allow shrinking.
        min_w = 0;
    }
}

void Toolbar::get_preferred_height_vfunc(int &min_h, int &nat_h) const
{
    _toolbar->get_preferred_height(min_h, nat_h);

    if (_toolbar->get_orientation() == Gtk::Orientation::VERTICAL && !_expanded_menu_btns.empty()) {
        // HACK: Return too-small value to allow shrinking.
        min_h = 0;
    }
}

void Toolbar::on_size_allocate(Gtk::Allocation &allocation)
{
    _resize_handler(allocation);
    Gtk::Box::on_size_allocate(allocation);
}

static int min_dimension(Gtk::Widget const *widget, Gtk::Orientation const orientation)
{
    int min = 0;
    int nat = 0;

    if (orientation == Gtk::Orientation::HORIZONTAL) {
        widget->get_preferred_width(min, nat);
    } else {
        widget->get_preferred_height(min, nat);
    }

    return min;
};

void Toolbar::_resize_handler(Gtk::Allocation &allocation)
{
    if (!_toolbar) {
        return;
    }

    auto const orientation = _toolbar->get_orientation();
    auto const allocated_size = orientation == Gtk::Orientation::VERTICAL ? allocation.get_height() : allocation.get_width();
    int min_size = min_dimension(_toolbar, orientation);

    if (allocated_size < min_size) {
        // Shrinkage required.

        // While there are still expanded buttons to collapse...
        while (allocated_size < min_size && !_expanded_menu_btns.empty()) {
            // Collapse the topmost expanded button.
            auto menu_btn = _expanded_menu_btns.top();
            _move_children(_toolbar, menu_btn->get_popover_box(), menu_btn->get_children());
            menu_btn->set_visible(true);

            _expanded_menu_btns.pop();
            _collapsed_menu_btns.push(menu_btn);

            min_size = min_dimension(_toolbar, orientation);
        }

    } else if (allocated_size > min_size) {
        // Once the allocated size of the toolbar is greater than its
        // minimum size, try to re-insert a group of elements back
        // into the toolbar.

        // While there are collapsed buttons to expand...
        while (!_collapsed_menu_btns.empty()) {
            // See if we have enough space to expand the topmost collapsed button.
            auto menu_btn = _collapsed_menu_btns.top();
            int req_size = min_size + menu_btn->get_required_width();

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

void Toolbar::_move_children(Gtk::Box *src, Gtk::Box *dest, std::vector<std::pair<int, Gtk::Widget *>> const &children,
                             bool is_expanding)
{
    for (auto [pos, child] : children) {
        src->remove(*child);
        dest->add(*child);

        // is_expanding will be true when the children are being put back into
        // the toolbar. In that case, insert the children at their previous
        // positions.
        if (is_expanding) {
            dest->reorder_child(*child, pos);
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
