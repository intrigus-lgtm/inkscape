// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author:
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Copyright (C) 2023 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_WIDGET_TOOLBAR_MENU_BUTTON_H
#define INKSCAPE_UI_WIDGET_TOOLBAR_MENU_BUTTON_H

#include <string>
#include <utility>
#include <vector>

#include <glibmm/refptr.h>
#include <gtkmm/menubutton.h>

namespace Gtk {
class Box;
class Builder;
} // namespace Gtk

namespace Inkscape::UI::Widget {

/**
 * TODO: Add description
 * ToolbarMenuButton widget
 */
class ToolbarMenuButton : public Gtk::MenuButton
{
public:
    ToolbarMenuButton() = default;
    ToolbarMenuButton(BaseObjectType *cobject, Glib::RefPtr<Gtk::Builder> const &);

    void init(int priority, std::string tag, Gtk::Box *popover_box, std::vector<Gtk::Widget *> &children);

    int get_priority() const { return _priority; }
    std::string const &get_tag() const { return _tag; }
    auto const &get_children() const { return _children; }
    Gtk::Box *get_popover_box() { return _popover_box; }

private:
    int _priority{};
    std::string _tag;
    std::vector<std::pair<int, Gtk::Widget *>> _children;
    Gtk::Box *_popover_box{};
};

} // namespace Inkscape::UI::Widget

#endif // INKSCAPE_UI_WIDGET_TOOLBAR_MENU_BUTTON_H

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
