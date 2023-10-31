// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Toolbar for Tools.
 */
/* Authors:
 *   Mike Kowalski (Popovers)
 *   Matrin Owens (Tool button catagories)
 *   Jonathon Neuhauser (Open tool preferences)
 *   Tavmjong Bah
 *
 * Copyright (C) 2023 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <iostream>
#include <utility>
#include <glibmm/i18n.h>
#include <giomm/menu.h>
#include <giomm/simpleactiongroup.h>
#include <gtkmm/builder.h>
#include <gtkmm/button.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/popover.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>

#include "tool-toolbar.h"
#include "actions/actions-tools.h" // Function to open tool preferences.
#include "inkscape-window.h"
#include "ui/builder-utils.h"
#include "ui/controller.h"
#include "ui/pack.h"
#include "ui/util.h"
#include "ui/widget/popover-menu.h"
#include "ui/widget/popover-menu-item.h"
#include "widgets/spw-utilities.h" // Find action target

namespace Inkscape::UI::Toolbar {

ToolToolbar::ToolToolbar(InkscapeWindow *window)
    : _context_menu{makeContextMenu(window)}
{
    set_name("ToolToolbar");

    auto builder = Inkscape::UI::create_builder("toolbar-tool.ui");
    auto &tool_toolbar = UI::get_widget<Gtk::ScrolledWindow>(builder, "tool-toolbar");

    attachHandlers(builder, window);

    UI::pack_start(*this, tool_toolbar, true, true);

    // Hide/show buttons based on preferences.
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    buttons_pref_observer = prefs->createObserver(tools_button_path, [&]{ set_visible_buttons(tool_toolbar); });
    set_visible_buttons(tool_toolbar); // Must come after pack_start()
}

ToolToolbar::~ToolToolbar() = default;

void ToolToolbar::set_visible_buttons(Gtk::ScrolledWindow &tool_toolbar)
{
    int buttons_before_separator = 0;
    Gtk::Widget* last_sep = nullptr;
    Gtk::FlowBox* last_box = nullptr;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    // We recurse from the ScrolledWindow, not this, to skip context PopoverMenu
    for_each_descendant(tool_toolbar, [&](Gtk::Widget &widget) {
        if (auto const flowbox = dynamic_cast<Gtk::FlowBox *>(&widget)) {
            flowbox->set_visible(true);
            flowbox->set_max_children_per_line(1);
            last_box = flowbox;
        } else if (auto const btn = dynamic_cast<Gtk::Button *>(&widget)) {
            auto const name = sp_get_action_target(btn);
            auto show = prefs->getBool(get_tool_visible_button_path(name), true);
            auto const parent = btn->get_parent();
            if (show) {
                parent->set_visible(true);
                ++buttons_before_separator;
                // keep the max_children up to date improves display.
                last_box->set_max_children_per_line(buttons_before_separator);
                last_sep = nullptr;
            } else {
                parent->set_visible(false);
            }
        } else if (auto const sep = dynamic_cast<Gtk::Separator *>(&widget)) {
            if (buttons_before_separator <= 0) {
                sep->set_visible(false);
            } else {
                sep->set_visible(true);
                buttons_before_separator = 0;
                last_sep = sep;
            }
        }
        return ForEachResult::_continue;
    });

    if (last_sep) {
        // hide trailing separator
        last_sep->set_visible(false);
    }
}

// We should avoid passing in the window in Gtk4 by turning "tool_preferences()" into an action.
std::unique_ptr<UI::Widget::PopoverMenu> ToolToolbar::makeContextMenu(InkscapeWindow * const window)
{
    Glib::ustring icon_name;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (prefs->getInt("/theme/menuIcons", true)) {
        icon_name = "preferences-system";
    }

    auto &item = *Gtk::make_managed<UI::Widget::PopoverMenuItem>(_("Open tool preferences"), false,
                                                                 icon_name);
    item.signal_activate().connect([=, this]
    {
        tool_preferences(_context_menu_tool_name, window);
        _context_menu_tool_name.clear();
    });

    auto menu = std::make_unique<UI::Widget::PopoverMenu>(*this, Gtk::PositionType::BOTTOM);
    menu->append(item);
    return menu;
}

void ToolToolbar::showContextMenu(InkscapeWindow * const window,
                                  Gtk::Button &button, Glib::ustring const &tool_name)
{
    _context_menu_tool_name = tool_name;
    _context_menu->popup_at_center(button);
}

/**
 * @brief Attach handlers to all tool buttons, so that double-clicking on a tool
 *        in the toolbar opens up that tool's preferences, and a right click opens a
 *        context menu with the same functionality.
 * @param builder The builder that contains a loaded UI structure containing ToggleButtons.
 * @param window The Inkscape window which will display the preferences dialog.
 */
void ToolToolbar::attachHandlers(Glib::RefPtr<Gtk::Builder> builder, InkscapeWindow *window)
{
    for (auto &object : builder->get_objects()) {
        auto const radio = dynamic_cast<Gtk::ToggleButton *>(object.get());
        if (!radio) {
            continue;
        }

        Glib::VariantBase action_target;
        radio->get_property("action-target", action_target);
        if (!action_target.is_of_type(Glib::VARIANT_TYPE_STRING)) {
            continue;
        }

        auto tool_name = Glib::ustring((gchar const *)action_target.get_data());
        auto on_click_pressed = [=, this, tool_name = std::move(tool_name)]
                                (Gtk::GestureClick const &click,
                                 int const n_press, double /*x*/, double /*y*/)
        {
            // Open tool preferences upon double click
            auto const button = click.get_current_button();
            if (button == 1 && n_press == 2) {
                tool_preferences(tool_name, window);
                return Gtk::EventSequenceState::CLAIMED;
            }
            if (button == 3) {
                showContextMenu(window, *radio, tool_name);
                return Gtk::EventSequenceState::CLAIMED;
            }
            return Gtk::EventSequenceState::NONE;
        };
        Controller::add_click(*radio, std::move(on_click_pressed), {},
                              Controller::Button::any);
    }
}

Glib::ustring ToolToolbar::get_tool_visible_button_path(const Glib::ustring& button_action_name) {
    return Glib::ustring(tools_button_path) + "/show" + button_action_name;
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
