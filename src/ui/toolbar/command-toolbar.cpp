// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Toolbar for Commands.
 */
/* Authors:
 *   Tavmjong Bah
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Copyright (C) 2023 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "command-toolbar.h"

#include "ui/builder-utils.h"
#include "ui/util.h"
#include "ui/widget/toolbar-menu-button.h"

namespace Inkscape::UI::Toolbar {

CommandToolbar::CommandToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    , _builder(create_builder("toolbar-commands.ui"))
{
    _toolbar = &get_widget<Gtk::Box>(_builder, "commands-toolbar");

    // Fetch all the ToolbarMenuButtons at once from the UI file
    // Menu Button #1
    auto popover_box1 = &get_widget<Gtk::Box>(_builder, "popover_box1");
    auto menu_btn1 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn1");

    // Menu Button #2
    auto popover_box2 = &get_widget<Gtk::Box>(_builder, "popover_box2");
    auto menu_btn2 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn2");

    // Menu Button #3
    auto popover_box3 = &get_widget<Gtk::Box>(_builder, "popover_box3");
    auto menu_btn3 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn3");

    // Menu Button #4
    auto popover_box4 = &get_widget<Gtk::Box>(_builder, "popover_box4");
    auto menu_btn4 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn4");

    // Menu Button #5
    auto popover_box5 = &get_widget<Gtk::Box>(_builder, "popover_box5");
    auto menu_btn5 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn5");

    // Menu Button #6
    auto popover_box6 = &get_widget<Gtk::Box>(_builder, "popover_box6");
    auto menu_btn6 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn6");

    // Menu Button #7
    auto popover_box7 = &get_widget<Gtk::Box>(_builder, "popover_box7");
    auto menu_btn7 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn7");

    // Initialize all the ToolbarMenuButtons only after all the children of the
    // toolbar have been fetched. Otherwise, the children to be moved in the
    // popover will get mapped to a different position and it will probably
    // cause segfault.
    auto children = UI::get_children(*_toolbar);

    menu_btn1->init(1, "tag1", popover_box1, children);
    addCollapsibleButton(menu_btn1);

    menu_btn2->init(2, "tag2", popover_box2, children);
    addCollapsibleButton(menu_btn2);

    menu_btn3->init(3, "tag3", popover_box3, children);
    addCollapsibleButton(menu_btn3);

    menu_btn4->init(4, "tag4", popover_box4, children);
    addCollapsibleButton(menu_btn4);

    menu_btn5->init(5, "tag5", popover_box5, children);
    addCollapsibleButton(menu_btn5);

    menu_btn6->init(6, "tag6", popover_box6, children);
    addCollapsibleButton(menu_btn6);

    menu_btn7->init(6, "tag7", popover_box7, children);
    addCollapsibleButton(menu_btn7);

    set_child(*_toolbar);
}

CommandToolbar::~CommandToolbar() = default;

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
