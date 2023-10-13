// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A wrapper for Gtk::Notebook.
 */
/*
 * Authors: see git history
 *   Tavmjong Bah
 *
 * Copyright (c) 2018 Tavmjong Bah, Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <algorithm>
#include <iostream>
#include <optional>
#include <tuple>
#include <utility>
#include <glibmm/i18n.h>
#include <gdkmm/dragcontext.h>
#include <gtkmm/button.h>
#include <gtkmm/eventbox.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/gesturemultipress.h>
#include <gtkmm/scrollbar.h>
#include <gtkmm/separator.h>

#include "dialog-notebook.h"
#include "enums.h"
#include "inkscape.h"
#include "inkscape-window.h"
#include "ui/column-menu-builder.h"
#include "ui/dialog/dialog-base.h"
#include "ui/dialog/dialog-data.h"
#include "ui/dialog/dialog-container.h"
#include "ui/dialog/dialog-multipaned.h"
#include "ui/dialog/dialog-window.h"
#include "ui/icon-loader.h"
#include "ui/pack.h"
#include "ui/util.h"
#include "ui/widget/popover-menu-item.h"

namespace Inkscape::UI::Dialog {

std::list<DialogNotebook *> DialogNotebook::_instances;

/**
 * DialogNotebook constructor.
 *
 * @param container the parent DialogContainer of the notebook.
 */
DialogNotebook::DialogNotebook(DialogContainer *container)
    : Gtk::ScrolledWindow()
    , _container(container)
    , _menu    {*this, Gtk::PositionType::BOTTOM}
    , _menutabs{*this, Gtk::PositionType::BOTTOM}
    , _labels_auto(true)
    , _detaching_duplicate(false)
    , _selected_page(nullptr)
    , _label_visible(true)
{
    set_name("DialogNotebook");
    set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::NEVER);
    set_shadow_type(Gtk::SHADOW_NONE);
    set_vexpand(true);
    set_hexpand(true);

    // =========== Getting preferences ==========
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (prefs == nullptr) {
        return;
    }

    auto const label_status = prefs->getInt("/options/notebooklabels/value", PREFS_NOTEBOOK_LABELS_AUTO);
    _labels_auto = label_status == PREFS_NOTEBOOK_LABELS_AUTO;
    _labels_off  = label_status == PREFS_NOTEBOOK_LABELS_OFF ;

    // ============= Notebook menu ==============
    _notebook.set_name("DockedDialogNotebook");
    _notebook.set_show_border(false);
    _notebook.set_group_name("InkscapeDialogGroup");
    _notebook.set_scrollable(true);

    UI::Widget::PopoverMenuItem *new_menu_item = nullptr;

    int row = 0;
    // Close tab
    new_menu_item = Gtk::make_managed<UI::Widget::PopoverMenuItem>(_("Close Current Tab"));
    _conn.emplace_back(
        new_menu_item->signal_activate().connect(sigc::mem_fun(*this, &DialogNotebook::close_tab_callback)));
    _menu.attach(*new_menu_item, 0, 2, row, row + 1);
    row++;

    // Close notebook
    new_menu_item = Gtk::make_managed<UI::Widget::PopoverMenuItem>(_("Close Panel"));
    _conn.emplace_back(
        new_menu_item->signal_activate().connect(sigc::mem_fun(*this, &DialogNotebook::close_notebook_callback)));
    _menu.attach(*new_menu_item, 0, 2, row, row + 1);
    row++;

    // Move to new window
    new_menu_item = Gtk::make_managed<UI::Widget::PopoverMenuItem>(_("Move Tab to New Window"));
    _conn.emplace_back(
        new_menu_item->signal_activate().connect([this]{ pop_tab_callback(); }));
    _menu.attach(*new_menu_item, 0, 2, row, row + 1);
    row++;

    struct Dialog {
        Glib::ustring key;
        Glib::ustring label;
        Glib::ustring order;
        Glib::ustring icon_name;
        DialogData::Category category;
        ScrollProvider provide_scroll;
    };
    std::vector<Dialog> all_dialogs;
    auto const &dialog_data = get_dialog_data();
    all_dialogs.reserve(dialog_data.size());
    for (auto&& kv : dialog_data) {
        const auto& key = kv.first;
        const auto& data = kv.second;
        if (data.category == DialogData::Other) {
            continue;
        }
        // for sorting dialogs alphabetically, remove '_' (used for accelerators)
        Glib::ustring order = data.label; // Already translated
        auto underscore = order.find('_');
        if (underscore != Glib::ustring::npos) {
            order = order.erase(underscore, 1);
        }
        all_dialogs.emplace_back(Dialog {
                                     .key = key,
                                     .label = data.label,
                                     .order = order,
                                     .icon_name = data.icon_name,
                                     .category = data.category,
                                     .provide_scroll = data.provide_scroll
                                 });
    }
    // sort by categories and then by names
    std::sort(all_dialogs.begin(), all_dialogs.end(), [](const Dialog& a, const Dialog& b){
        if (a.category != b.category) return a.category < b.category;
        return a.order < b.order;
    });

    auto builder = ColumnMenuBuilder<DialogData::Category>{_menu, 2, Gtk::ICON_SIZE_MENU,
                                                           row};
    for (auto const &data : all_dialogs) {
        auto callback = [key = data.key]{
            // get desktop's container, it may be different than current '_container'!
            if (auto desktop = SP_ACTIVE_DESKTOP) {
                if (auto container = desktop->getContainer()) {
                    container->new_dialog(key);
                }
            }
        };
        builder.add_item(data.label, data.category, {}, data.icon_name, true, false,
                         std::move(callback));
        if (builder.new_section()) {
            builder.set_section(gettext(dialog_categories[data.category]));
        }
    }

    if (prefs->getBool("/theme/symbolicIcons", true)) {
        _menu.get_style_context()->add_class("symbolic");
    }

    _menu.show_all_children();

    auto const menubtn = Gtk::make_managed<Gtk::MenuButton>();
    menubtn->set_image_from_icon_name("go-down-symbolic");
    menubtn->set_popover(_menu);
    _notebook.set_action_widget(menubtn, Gtk::PACK_END);
    menubtn->set_visible(true);
    menubtn->set_relief(Gtk::RELIEF_NORMAL);
    menubtn->set_valign(Gtk::Align::CENTER);
    menubtn->set_halign(Gtk::Align::CENTER);
    menubtn->set_can_focus(false);
    menubtn->set_name("DialogMenuButton");

    // =============== Signals ==================
    _conn.emplace_back(signal_size_allocate().connect(sigc::mem_fun(*this, &DialogNotebook::on_size_allocate_scroll)));
    _conn.emplace_back(_notebook.signal_drag_begin().connect(sigc::mem_fun(*this, &DialogNotebook::on_drag_begin)));
    _conn.emplace_back(_notebook.signal_drag_end().connect(sigc::mem_fun(*this, &DialogNotebook::on_drag_end)));
    _conn.emplace_back(_notebook.signal_page_added().connect(sigc::mem_fun(*this, &DialogNotebook::on_page_added)));
    _conn.emplace_back(_notebook.signal_page_removed().connect(sigc::mem_fun(*this, &DialogNotebook::on_page_removed)));
    _conn.emplace_back(_notebook.signal_switch_page().connect(sigc::mem_fun(*this, &DialogNotebook::on_page_switch)));

    // ============= Finish setup ===============
    _reload_context = true;
    add(_notebook);
    show_all();

    _instances.push_back(this);
}

DialogNotebook::~DialogNotebook()
{
    // disconnect signals first, so no handlers are invoked when removing pages
    _conn.clear();
    _connmenu.clear();
    _tab_connections.clear();

    // Unlink and remove pages
    for (int i = _notebook.get_n_pages(); i >= 0; --i) {
        DialogBase *dialog = dynamic_cast<DialogBase *>(_notebook.get_nth_page(i));
        _container->unlink_dialog(dialog);
        _notebook.remove_page(i);
    }

    _instances.remove(this);
}

void DialogNotebook::add_highlight_header()
{
    const auto &style = _notebook.get_style_context();
    style->add_class("nb-highlight");
}

void DialogNotebook::remove_highlight_header()
{
    const auto &style = _notebook.get_style_context();
    style->remove_class("nb-highlight");
}

/**
 * get provide scroll
 */
bool 
DialogNotebook::provide_scroll(Gtk::Widget &page) {
    auto const &dialog_data = get_dialog_data();
    auto dialogbase = dynamic_cast<DialogBase*>(&page);
    if (dialogbase) {
        auto data = dialog_data.find(dialogbase->get_type());
        if ((*data).second.provide_scroll == ScrollProvider::PROVIDE) {
            return true;
        }
    }
    return false;
}

Gtk::ScrolledWindow *
DialogNotebook::get_scrolledwindow(Gtk::Widget &page)
{
    if (auto const children = UI::get_children(page); !children.empty()) {
        if (auto const scrolledwindow = dynamic_cast<Gtk::ScrolledWindow *>(children[0])) {
            return scrolledwindow;
        }
    }
    return nullptr;
}

/**
 * Set provide scroll
 */
Gtk::ScrolledWindow *
DialogNotebook::get_current_scrolledwindow(bool const skip_scroll_provider)
{
    auto const pagenum = _notebook.get_current_page();
    if (auto const page = _notebook.get_nth_page(pagenum)) {
        if (skip_scroll_provider && provide_scroll(*page)) {
            return nullptr;
        }
        return get_scrolledwindow(*page);
    }
    return nullptr;
}

/**
 * Adds a widget as a new page with a tab.
 */
void DialogNotebook::add_page(Gtk::Widget &page, Gtk::Widget &tab, Glib::ustring)
{
    _reload_context = true;
    page.set_vexpand();

    // TODO: It is not exactly great to replace the passed pageʼs children from under it like this.
    //       But stopping it requires to ensure all references to page elsewhere are right/updated.
    if (auto const box = dynamic_cast<Gtk::Box *>(&page)) {
        auto const wrapper = Gtk::make_managed<Gtk::ScrolledWindow>();
        wrapper->set_vexpand(true);
        wrapper->set_propagate_natural_height(true);
        wrapper->set_overlay_scrolling(false);
        wrapper->get_style_context()->add_class("noborder");

        auto const wrapperbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL,0);
        wrapperbox->set_vexpand(true);

        // This used to transfer pack-type and child properties, but now those are set on children.
        for_each_child(*box, [=](Gtk::Widget &child) {
            box       ->remove(child);
            wrapperbox->add   (child);
            return ForEachResult::_continue;
        });

        wrapper->add(*wrapperbox);
        box    ->add(*wrapper);

        if (provide_scroll(page)) {
            wrapper->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::EXTERNAL);
        } else {
            wrapper->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        }
    }

    int page_number = _notebook.append_page(page, tab);
    _notebook.set_tab_reorderable(page);
    _notebook.set_tab_detachable(page);
    _notebook.show_all();
    _notebook.set_current_page(page_number);
}

/**
 * Moves a page from a different notebook to this one.
 */
void DialogNotebook::move_page(Gtk::Widget &page)
{
    // Find old notebook
    Gtk::Notebook *old_notebook = dynamic_cast<Gtk::Notebook *>(page.get_parent());
    if (!old_notebook) {
        std::cerr << "DialogNotebook::move_page: page not in notebook!" << std::endl;
        return;
    }

    Gtk::Widget *tab = old_notebook->get_tab_label(page);
    Glib::ustring text = old_notebook->get_menu_label_text(page);

    // Keep references until re-attachment
    tab->reference();
    page.reference();

    old_notebook->detach_tab(page);
    _notebook.append_page(page, *tab);
    // Remove unnecessary references
    tab->unreference();
    page.unreference();

    // Set default settings for a new page
    _notebook.set_tab_reorderable(page);
    _notebook.set_tab_detachable(page);
    _notebook.show_all();
    _reload_context = true;
}

// ============ Notebook callbacks ==============

/**
 * Callback to close the current active tab.
 */
void DialogNotebook::close_tab_callback()
{
    int page_number = _notebook.get_current_page();

    if (_selected_page) {
        page_number = _notebook.page_num(*_selected_page);
        _selected_page = nullptr;
    }

    if (dynamic_cast<DialogBase*>(_notebook.get_nth_page(page_number))) {
        // is this a dialog in a floating window?
        if (auto window = dynamic_cast<DialogWindow*>(_container->get_toplevel())) {
            // store state of floating dialog before it gets deleted
            DialogManager::singleton().store_state(*window);
        }
    }

    // Remove page from notebook
    _notebook.remove_page(page_number);

    // Delete the signal connection
    remove_tab_connections(_selected_page);

    if (_notebook.get_n_pages() == 0) {
        close_notebook_callback();
        return;
    }

    // Update tab labels by comparing the sum of their widths to the allocation
    Gtk::Allocation allocation = get_allocation();
    on_size_allocate_scroll(allocation);
    _reload_context = true;
}

/**
 * Shutdown callback - delete the parent DialogMultipaned before destructing.
 */
void DialogNotebook::close_notebook_callback()
{
    // Search for DialogMultipaned
    DialogMultipaned *multipaned = dynamic_cast<DialogMultipaned *>(get_parent());
    if (multipaned) {
        multipaned->remove(*this);
    } else if (get_parent()) {
        std::cerr << "DialogNotebook::close_notebook_callback: Unexpected parent!" << std::endl;
        get_parent()->remove(*this);
    }
    delete this;
}

/**
 * Callback to move the current active tab.
 */
DialogWindow* DialogNotebook::pop_tab_callback()
{
    // Find page.
    Gtk::Widget *page = _notebook.get_nth_page(_notebook.get_current_page());

    if (_selected_page) {
        page = _selected_page;
        _selected_page = nullptr;
    }

    if (!page) {
        std::cerr << "DialogNotebook::pop_tab_callback: page not found!" << std::endl;
        return nullptr;
    }

    // Move page to notebook in new dialog window (attached to active InkscapeWindow).
    auto inkscape_window = _container->get_inkscape_window();
    auto window = new DialogWindow(inkscape_window, page);
    window->show_all();

    if (_notebook.get_n_pages() == 0) {
        close_notebook_callback();
        return window;
    }

    // Update tab labels by comparing the sum of their widths to the allocation
    Gtk::Allocation allocation = get_allocation();
    on_size_allocate_scroll(allocation);

    return window;
}

// ========= Signal handlers - notebook =========

[[nodiscard]] static bool should_set_floating(Glib::RefPtr<Gdk::DragContext> const &context)
{
    auto const window = context->get_dest_window();
    return !window || window->get_window_type() == Gdk::WINDOW_FOREIGN;
}

/**
 * Signal handler to pop a dragged tab into its own DialogWindow.
 *
 * A failed drag means that the page was not dropped on an existing notebook.
 * Thus create a new window with notebook to move page to.
 *
 * BUG: this has inconsistent behavior on Wayland.
 */
void DialogNotebook::on_drag_end(const Glib::RefPtr<Gdk::DragContext> &context)
{
    // Remove dropzone highlights
    DialogMultipaned::remove_drop_zone_highlight_instances();
    for (auto instance : _instances) {
        instance->remove_highlight_header();
    }

    bool const set_floating = should_set_floating(context);
    if (set_floating) {
        // Find page
        if (auto const page = _notebook.get_nth_page(_notebook.get_current_page())) {
            // Move page to notebook in new dialog window
            auto inkscape_window = _container->get_inkscape_window();
            auto window = new DialogWindow(inkscape_window, page);

            // Move window to mouse pointer
            if (auto device = context->get_device()) {
                int x = 0, y = 0;
                device->get_position(x, y);
                window->move(std::max(0, x - 50), std::max(0, y - 50));
            }

            window->show_all();
        }
    }

    // Reload the context menu next time it is shown, to reflect new tabs/order.
    _reload_context = true;

    // Closes the notebook if empty.
    if (_notebook.get_n_pages() == 0) {
        close_notebook_callback();
        return;
    }

    // Update tab labels by comparing the sum of their widths to the allocation
    Gtk::Allocation allocation = get_allocation();
    on_size_allocate_scroll(allocation);
}

void DialogNotebook::on_drag_begin(const Glib::RefPtr<Gdk::DragContext> &context)
{
    DialogMultipaned::add_drop_zone_highlight_instances();
    for (auto instance : _instances) {
        instance->add_highlight_header();
    }
}

/**
 * Signal handler to update dialog list when adding a page.
 */
void DialogNotebook::on_page_added(Gtk::Widget *page, int page_num)
{
    DialogBase *dialog = dynamic_cast<DialogBase *>(page);

    // Does current container/window already have such a dialog?
    if (dialog && _container->has_dialog_of_type(dialog)) {
        // We already have a dialog of the same type

        // Highlight first dialog
        DialogBase *other_dialog = _container->get_dialog(dialog->get_type());
        other_dialog->blink();

        // Remove page from notebook
        _detaching_duplicate = true; // HACK: prevent removing the initial dialog of the same type
        _notebook.detach_tab(*page);
        return;
    } else if (dialog) {
        // We don't have a dialog of this type

        // Add to dialog list
        _container->link_dialog(dialog);
    } else {
        // This is not a dialog
        return;
    }

    // add click & close tab signals
    add_tab_connections(page);

    // Switch tab labels if needed
    if (!_labels_auto) {
        toggle_tab_labels_callback(false);
    }

    // Update tab labels by comparing the sum of their widths to the allocation
    Gtk::Allocation allocation = get_allocation();
    on_size_allocate_scroll(allocation);

    // Reload the context menu next time it is shown, to reflect new tabs/order.
    _reload_context = true;
}

/**
 * Signal handler to update dialog list when removing a page.
 */
void DialogNotebook::on_page_removed(Gtk::Widget *page, int page_num)
{
    /**
     * When adding a dialog in a notebooks header zone of the same type as an existing one,
     * we remove it immediately, which triggers a call to this method. We use `_detaching_duplicate`
     * to prevent reemoving the initial dialog.
     */
    if (_detaching_duplicate) {
        _detaching_duplicate = false;
        return;
    }

    // Remove from dialog list
    DialogBase *dialog = dynamic_cast<DialogBase *>(page);
    if (dialog) {
        _container->unlink_dialog(dialog);
    }

    // remove old click & close tab signals
    remove_tab_connections(page);

    // Reload the context menu next time it is shown, to reflect new tabs/order.
    _reload_context = true;
}

/**
 * We need to remove the scrollbar to snap a whole DialogNotebook to width 0.
 *
 */
void DialogNotebook::on_size_allocate_scroll(Gtk::Allocation &a)
{
    // magic number
    static constexpr int MIN_HEIGHT = 60;
    //  set or unset scrollbars to completely hide a notebook
    // because we have a "blocking" scroll per tab we need to loop to aboid
    // other page stop out scroll
    for_each_child(_notebook, [this](Gtk::Widget &page){
        if (!provide_scroll(page)) {
            auto const scrolledwindow = get_scrolledwindow(page);
            if (scrolledwindow) {
                double height = scrolledwindow->get_allocation().get_height();
                if (height > 1) {
                    auto property = scrolledwindow->property_vscrollbar_policy();
                    auto const policy = property.get_value();
                    if (height >= MIN_HEIGHT && policy != Gtk::PolicyType::AUTOMATIC) {
                        property.set_value(Gtk::PolicyType::AUTOMATIC);
                    } else if (height < MIN_HEIGHT && policy != Gtk::PolicyType::EXTERNAL) {
                        property.set_value(Gtk::PolicyType::EXTERNAL);
                    } else {
                        // we don't need to update; break
                        return ForEachResult::_break;
                    }
                }
            }
        }
        return ForEachResult::_continue;
    });

    set_allocation(a);
    // only update notebook tabs on horizontal changes
    if (a.get_width() != _prev_alloc_width) {
        on_size_allocate_notebook(a);
    }
}

/**
 * This function hides the tab labels if necessary (and _labels_auto == true)
 */
void DialogNotebook::on_size_allocate_notebook(Gtk::Allocation &a)
{
    // we unset scrollable when FULL mode on to prevent overflow with 
    // container at full size that makes an unmaximized desktop freeze 
    _notebook.set_scrollable(false);
    if (!_labels_set_off && !_labels_auto) {
        toggle_tab_labels_callback(false);
    }
    if (!_labels_auto) {
        return;
    }

    int alloc_width = get_allocation().get_width();
    // Don't update on closed dialog container, prevent console errors
    if (alloc_width < 2) {
        _notebook.set_scrollable(true);
        return;
    }

    int nat_width = 0;
    int initial_width = 0;
    int total_width = 0;
    _notebook.get_preferred_width(initial_width, nat_width); // get current notebook allocation

    for_each_child(_notebook, [this](Gtk::Widget &page){
        auto const cover = dynamic_cast<Gtk::EventBox *>(_notebook.get_tab_label(page));
        if (cover) cover->show_all();
        return ForEachResult::_continue;
    });
    _notebook.get_preferred_width(total_width, nat_width); // get full notebook allocation (all open)

    prev_tabstatus = tabstatus;
    if (_single_tab_width != _none_tab_width && 
        ((_none_tab_width && _none_tab_width > alloc_width) || 
        (_single_tab_width > alloc_width && _single_tab_width < total_width)))
    {
        tabstatus = TabsStatus::NONE;
        if (_single_tab_width != initial_width || prev_tabstatus == TabsStatus::NONE) {
            _none_tab_width = initial_width;
        }
    } else {
        tabstatus = (alloc_width <= total_width) ? TabsStatus::SINGLE : TabsStatus::ALL;
        if (total_width != initial_width &&
            prev_tabstatus == TabsStatus::SINGLE && 
            tabstatus == TabsStatus::SINGLE) 
        {
            _single_tab_width = initial_width;
        }
    }
    if ((_single_tab_width && !_none_tab_width) || 
        (_single_tab_width && _single_tab_width == _none_tab_width)) 
    {
        _none_tab_width = _single_tab_width - 1;
    }    
     
    /* 
    std::cout << "::::::::::tabstatus::" << (int)tabstatus  << std::endl;
    std::cout << ":::::prev_tabstatus::" << (int)prev_tabstatus << std::endl;
    std::cout << "::::::::alloc_width::" << alloc_width << std::endl;
    std::cout << "::_prev_alloc_width::" << _prev_alloc_width << std::endl;
    std::cout << "::::::initial_width::" << initial_width << std::endl;
    std::cout << "::::::::::nat_width::" << nat_width << std::endl;
    std::cout << "::::::::total_width::" << total_width << std::endl;
    std::cout << "::::_none_tab_width::" << _none_tab_width  << std::endl;
    std::cout << "::_single_tab_width::" << _single_tab_width  << std::endl;
    std::cout << ":::::::::::::::::::::" << std::endl;    
    */
    
    _prev_alloc_width = alloc_width;
    bool show = tabstatus == TabsStatus::ALL;
    toggle_tab_labels_callback(show);
}

/**
 * Signal handler to close a tab on middle-click or to open menu on right-click.
 */
Gtk::EventSequenceState DialogNotebook::on_tab_click_event(Gtk::GestureClick const &click,
                                                           int /*n_press*/, double /*x*/, double /*y*/,
                                                           Gtk::Widget *page)
{
    if (_menutabs.get_visible()) {
        _menutabs.popdown();
        return Gtk::EventSequenceState::NONE;
    }

    auto const button = click.get_current_button();
    if (button == 2) { // Close tab
        _selected_page = page;
        close_tab_callback();
        return Gtk::EventSequenceState::CLAIMED;
    } else if (button == 3) { // Show menu
        _selected_page = page;
        reload_tab_menu();
        _menutabs.popup_at(*_notebook.get_tab_label(*page));
        return Gtk::EventSequenceState::CLAIMED;
    }
    return Gtk::EventSequenceState::NONE;
}

void DialogNotebook::on_close_button_click_event(Gtk::Widget *page)
{
    _selected_page = page;
    close_tab_callback();
}

// ================== Helpers ===================

/// Get the icon, label, and close button from a tab "cover" i.e. EventBox.
static std::optional<std::tuple<Gtk::Image *, Gtk::Label *, Gtk::Button *>>
get_cover_box_children(Gtk::Widget * const tab_label)
{
    if (!tab_label) {
        return std::nullopt;
    }

    auto const cover = dynamic_cast<Gtk::EventBox *>(tab_label);
    if (!cover) {
        return std::nullopt;
    }

    auto const box = dynamic_cast<Gtk::Box *>(cover->get_child());
    if (!box) {
        return std::nullopt;
    }

    auto const children = UI::get_children(*box);
    if (children.size() < 2) {
        return std::nullopt;
    }

    auto const icon  = dynamic_cast<Gtk::Image *>(children[0]);
    auto const label = dynamic_cast<Gtk::Label *>(children[1]);

    Gtk::Button *close = nullptr;
    if (children.size() >= 3) {
        close = dynamic_cast<Gtk::Button *>(children[children.size() - 1]);
    }

    return std::tuple{icon, label, close};
}

/**
 * Reload tab menu
 */
void DialogNotebook::reload_tab_menu()
{
    if (_reload_context) {
        _reload_context = false;

        _connmenu.clear();

        _menutabs.delete_all();

        auto prefs = Inkscape::Preferences::get();
        bool symbolic = false;
        if (prefs->getBool("/theme/symbolicIcons", false)) {
            symbolic = true;
        }

        for_each_child(_notebook, [=, this](Gtk::Widget &page){
            auto const children = get_cover_box_children(_notebook.get_tab_label(page));
            if (!children) {
                return ForEachResult::_continue;
            }

            auto const boxmenu = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
            boxmenu->set_halign(Gtk::Align::START);

            auto const menuitem = Gtk::make_managed<UI::Widget::PopoverMenuItem>();
            menuitem->add(*boxmenu);

            auto const &[icon, label, close] = *children;

            if (icon) {
                auto name = icon->get_icon_name();
                if (!name.empty()) {
                    if (symbolic && name.find("-symbolic") == Glib::ustring::npos) {
                        name += Glib::ustring("-symbolic");
                    }
                    Gtk::Image *iconend  = sp_get_icon_image(name, Gtk::ICON_SIZE_MENU);
                    boxmenu->add(*iconend);
                }
            }

            auto const labelto = Gtk::make_managed<Gtk::Label>(label->get_text());
            labelto->set_hexpand(true);
            boxmenu->add(*labelto);

            size_t const pagenum = _notebook.page_num(page);
            _connmenu.emplace_back(
                menuitem->signal_activate().connect(sigc::bind(sigc::mem_fun(*this, &DialogNotebook::change_page),pagenum)));
            
            _menutabs.append(*menuitem);

            return ForEachResult::_continue;
        });
    }

    _menutabs.show_all();
}

/**
 * Callback to toggle all tab labels to the selected state.
 * @param show: whether you want the labels to show or not
 */
void DialogNotebook::toggle_tab_labels_callback(bool show)
{
    _label_visible = show;

    for_each_child(_notebook, [=, this](Gtk::Widget &page){
        auto const children = get_cover_box_children(_notebook.get_tab_label(page));
        if (!children) {
            return ForEachResult::_continue;
        }

        auto const &[icon, label, close] = *children;
        int n = _notebook.get_current_page();
        if (close && label) {
            if (&page != _notebook.get_nth_page(n)) {
                show ? close->set_visible(true) : close->set_visible(false);
                show ? label->set_visible(true) : label->set_visible(false);
            } else if (tabstatus == TabsStatus::NONE || _labels_off) {
                if (&page != _notebook.get_nth_page(n)) {
                    close->set_visible(false);
                } else {
                    close->set_visible(true);
                }
                label->set_visible(false);
            } else {
                close->set_visible(true);
                label->set_visible(true);
            }
        }

        return ForEachResult::_continue;
    });

    _labels_set_off = _labels_off;
    if (_prev_alloc_width && prev_tabstatus != tabstatus && (show || tabstatus != TabsStatus::NONE || !_labels_off)) {
        resize_widget_children(&_notebook);
    }
    if (show && _single_tab_width) {
        _notebook.set_scrollable(true);
    }
}

void DialogNotebook::on_page_switch(Gtk::Widget *curr_page, guint)
{
    if (auto container = dynamic_cast<Gtk::Container *>(curr_page)) {
        container->show_all_children();
    }

    for_each_child(_notebook, [=, this](Gtk::Widget &page){
        if (auto const dialogbase = dynamic_cast<DialogBase *>(&page)) {
            if (auto const widgs = UI::get_children(*dialogbase); !widgs.empty()) {
                if (curr_page == &page) {
                    widgs[0]->show_now();
                } else {
                    widgs[0]->set_visible(false);
                }
            }
            if (_prev_alloc_width) {
                dialogbase->setShowing(curr_page == &page);
            }
        }

        if (_label_visible) {
            return ForEachResult::_continue;
        }

        auto const children = get_cover_box_children(_notebook.get_tab_label(page));
        if (!children) {
            return ForEachResult::_continue;
        }

        auto const &[icon, label, close] = *children;

        if (&page == curr_page) {
            if (label) {
                if (tabstatus == TabsStatus::NONE) {
                    label->set_visible(false);
                } else {
                    label->set_visible(true);
                }
            }

            if (close) {
                if (tabstatus == TabsStatus::NONE && curr_page != &page) {
                    close->set_visible(false);
                } else {
                    close->set_visible(true);
                }
            }

            return ForEachResult::_continue;
        }

        close->set_visible(false);
        label->set_visible(false);

        return ForEachResult::_continue;
    });

    if (_prev_alloc_width) {
        if (!_label_visible) {
            queue_allocate(); 
        }
        auto window = dynamic_cast<DialogWindow*>(_container->get_toplevel());
        if (window) {
            resize_widget_children(window->get_container());
        } else {
            if (auto desktop = SP_ACTIVE_DESKTOP) {
                if (auto container = desktop->getContainer()) {
                    resize_widget_children(container);
                }
            }
        }
    }
}

/**
 * Helper method that change the page
 */
void DialogNotebook::change_page(size_t pagenum)
{
    _notebook.set_current_page(pagenum);
}

/**
 * Helper method that adds the click and close tab signal connections for the page given.
 */
void DialogNotebook::add_tab_connections(Gtk::Widget * const page)
{
    Gtk::Widget *tab = _notebook.get_tab_label(*page);
    auto const children = get_cover_box_children(tab);
    auto const &[icon, label, close] = children.value();

    sigc::connection close_connection = close->signal_clicked().connect(
            sigc::bind(sigc::mem_fun(*this, &DialogNotebook::on_close_button_click_event), page), true);
    _tab_connections.emplace(page, std::move(close_connection));

    auto click = Gtk::GestureClick::create(*tab);
    click->set_button(0); // all
    click->signal_pressed().connect([this, page, &click = *click.get()](int const n_press, double const x, double const y)
    {
        auto const state = on_tab_click_event(click, n_press, x, y, page);
        if (state != Gtk::EventSequenceState::NONE) click.set_state(state);
    });
    _tab_connections.emplace(page, std::move(click));
}

/**
 * Helper method that removes the click & close tab signal connections for the page given.
 */
void DialogNotebook::remove_tab_connections(Gtk::Widget *page)
{
    auto const [first, last] = _tab_connections.equal_range(page);
    _tab_connections.erase(first, last);
}

void DialogNotebook::get_preferred_height_for_width_vfunc(int width, int& minimum_height, int& natural_height) const {
    Gtk::ScrolledWindow::get_preferred_height_for_width_vfunc(width, minimum_height, natural_height);
    if (_natural_height > 0) {
        natural_height = _natural_height;
        if (minimum_height > _natural_height) {
            minimum_height = _natural_height;
        }
    }
}

void DialogNotebook::get_preferred_height_vfunc(int& minimum_height, int& natural_height) const {
    Gtk::ScrolledWindow::get_preferred_height_vfunc(minimum_height, natural_height);
    if (_natural_height > 0) {
        natural_height = _natural_height;
        if (minimum_height > _natural_height) {
            minimum_height = _natural_height;
        }
    }
}

void DialogNotebook::set_requested_height(int height) {
    _natural_height = height;
}

} // namespace Inkscape::UI::Dialog

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
