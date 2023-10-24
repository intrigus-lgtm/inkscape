// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A replacement for GTK3ʼs Gtk::Menu, as removed in GTK4.
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "ui/widget/popover-menu.h"

#include <glibmm/main.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>
#include <gtkmm/window.h>

#include "ui/menuize.h"
#include "ui/popup-menu.h"
#include "ui/util.h"
#include "ui/widget/css-name-class-init.h"
#include "ui/widget/popover-menu-item.h"

namespace Inkscape::UI::Widget {

// Make our Grid have CSS name `menu` to try to piggyback “real” Menusʼ theming.
// Ditto, we leave Popover as `popover` so we don't lose normal Popover theming.
class PopoverMenuGrid final
    : public CssNameClassInit
    , public Gtk::Grid
{
public:
    [[nodiscard]] PopoverMenuGrid()
        : Glib::ObjectBase{"PopoverMenuGrid"}
        , CssNameClassInit{"menu"}
        , Gtk::Grid{}
    {
        add_css_class("menu");
        set_orientation(Gtk::Orientation::VERTICAL);
    }
};

PopoverMenu::PopoverMenu(Gtk::Widget &parent, Gtk::PositionType const position)
    : Glib::ObjectBase{"PopoverMenu"}
    , Gtk::Popover{}
    , _scrolled_window{*Gtk::make_managed<Gtk::ScrolledWindow>()}
    , _grid           {*Gtk::make_managed<PopoverMenuGrid    >()}
{
    add_css_class("popover-menu");
    add_css_class("menu");

    set_parent(parent);
    set_position(position);

    _scrolled_window.set_propagate_natural_width (true);
    _scrolled_window.set_propagate_natural_height(true);
    _scrolled_window.set_child(_grid);
    set_child(_scrolled_window);

    signal_show().connect([this]
    {
        check_child_invariants();

        set_scrolled_window_size();

        // FIXME: Initially focused item is sometimes wrong on first popup. GTK bug?
        // Grabbing focus in ::show does not always work & sometimes even crashes :(
        // For now, just remove possibly wrong, visible selection until hover/keynav
        // This is also nicer for menus with only 1 item, like the ToolToolbar popup
        Glib::signal_idle().connect_once( [this]{ unset_items_focus_hover(nullptr); });
    });

    // Temporarily hide tooltip of relative-to widget to avoid it covering us up
    UI::autohide_tooltip(*this);
}

void PopoverMenu::attach(Gtk::Widget &item,
                         int const left_attach, int const right_attach,
                         int const top_attach, int const bottom_attach)
{
    check_child_invariants();

    auto const width = right_attach - left_attach;
    auto const height = bottom_attach - top_attach;
    _grid.attach(item, left_attach, top_attach, width, height);
    _items.push_back(&item);
}

void PopoverMenu::append(Gtk::Widget &item)
{
    check_child_invariants();

    _grid.attach_next_to(item, Gtk::PositionType::BOTTOM);
    _items.push_back(&item);
}

void PopoverMenu::prepend(Gtk::Widget &item)
{
    check_child_invariants();

    _grid.attach_next_to(item, Gtk::PositionType::TOP);
    _items.push_back(&item);
}

void PopoverMenu::remove(Gtk::Widget &item)
{
    // Check was added with one of our methods, is not Grid, etc.
    auto const it = std::find(_items.begin(), _items.end(), &item);
    g_return_if_fail(it != _items.end());

    _grid.remove(item);
    _items.erase(it);
}

void PopoverMenu::remove_all()
{
    remove_all(false);
}

void PopoverMenu::delete_all()
{
    remove_all(true);
}

void PopoverMenu::append_section_label(Glib::ustring const &markup)
{
    auto const label = Gtk::make_managed<Gtk::Label>();
    label->set_markup(markup);
    auto const item = Gtk::make_managed<PopoverMenuItem>();
    item->set_child(*label);
    item->set_sensitive(false);
    append(*item);
}

void PopoverMenu::append_separator()
{
    append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));
}

void PopoverMenu::popup_at(Gtk::Widget &widget,
                           int const x_offset, int const y_offset)
{
    ::Inkscape::UI::popup_at(*this, widget, x_offset, y_offset);
}

void PopoverMenu::popup_at_center(Gtk::Widget &widget)
{
    ::Inkscape::UI::popup_at_center(*this, widget);
}

std::vector<Gtk::Widget *> const &PopoverMenu::get_items()
{
    return _items;
}

void PopoverMenu::check_child_invariants()
{
    // Check no one (accidentally?) removes our Grid or ScrolledWindow.
    g_assert(_scrolled_window.get_parent() ==  this);
    // ScrolledWindow will have interposed a Gtk::Viewport between, so:
    g_assert(_grid.get_parent());
    g_assert(is_descendant_of(_grid, _scrolled_window));
}

void PopoverMenu::set_scrolled_window_size()
{
    static constexpr int padding = 16; // Spare some window size for border etc.
    auto &window = dynamic_cast<Gtk::Window const &>(*get_root());
    _scrolled_window.set_max_content_width (window.get_width () - 2 * padding);
    _scrolled_window.set_max_content_height(window.get_height() - 2 * padding);
}

void PopoverMenu::unset_items_focus_hover(Gtk::Widget * const except_active)
{
    for (auto const item : _items) {
        if (item != except_active) {
            item->unset_state_flags(Gtk::StateFlags::FOCUSED | Gtk::StateFlags::PRELIGHT);
        }
    }
}

void PopoverMenu::remove_all(bool const and_delete)
{
    for (auto const item: _items) {
        _grid.remove(*item);
        if (and_delete) {
            g_assert(item->is_managed_()); // "Private API", but sanity check for us
            delete item; // This is not ideal, but gtkmm/object.cc says should be OK
        }
    }
    _items.clear();
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
