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

#ifndef INKSCAPE_UI_DIALOG_NOTEBOOK_H
#define INKSCAPE_UI_DIALOG_NOTEBOOK_H

#include <map>
#include <variant>
#include <vector>
#include <glibmm/refptr.h>
#include <gtkmm/gesture.h> // Gtk::EventSequenceState
#include <gtkmm/notebook.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/widget.h>

#include "helper/auto-connection.h"
#include "ui/widget/popover-menu.h"

namespace Glib {
class ValueBase;
} // namespace Glib

namespace Gdk {
class ContentProvider;
class Drag;
} // namespace Gdk

namespace Gtk {
class EventController;
class GestureClick;
} // namespace Gtk

namespace Inkscape::UI {

namespace Dialog {

enum class TabsStatus {
    NONE,
    SINGLE,
    ALL
};

class DialogContainer;
class DialogWindow;

/**
 * A widget that wraps a Gtk::Notebook with dialogs as pages.
 *
 * A notebook is fixed to a specific DialogContainer which manages the dialogs inside the notebook.
 */
class DialogNotebook : public Gtk::ScrolledWindow
{
public:
    DialogNotebook(DialogContainer *container);
    ~DialogNotebook() override;

    void add_page(Gtk::Widget &page, Gtk::Widget &tab, Glib::ustring label);
    void move_page(Gtk::Widget &page);

    // Getters
    Gtk::Notebook *get_notebook() { return &_notebook; }
    DialogContainer *get_container() { return _container; }

    // Notebook callbacks
    void close_tab_callback();
    void close_notebook_callback();
    DialogWindow* pop_tab_callback();
    Gtk::ScrolledWindow * get_scrolledwindow(Gtk::Widget &page);
    Gtk::ScrolledWindow * get_current_scrolledwindow(bool skip_scroll_provider);
    void set_requested_height(int height);

private:
    // Widgets
    DialogContainer *_container;
    UI::Widget::PopoverMenu _menu;
    UI::Widget::PopoverMenu _menutabs;
    Gtk::Notebook _notebook;

    // State variables
    bool _label_visible;
    bool _labels_auto;
    bool _labels_off;
    bool _labels_set_off = false;
    bool _detaching_duplicate;
    bool _reload_context = true;
    gint _prev_alloc_width = 0;
    gint _none_tab_width = 0;
    gint _single_tab_width = 0;
    TabsStatus tabstatus = TabsStatus::NONE;
    TabsStatus prev_tabstatus = TabsStatus::NONE;
    Gtk::Widget *_selected_page;
    std::vector<auto_connection> _conn;
    std::vector<auto_connection> _connmenu;
    using TabConnection = std::variant<auto_connection, Glib::RefPtr<Gtk::EventController>>;
    std::multimap<Gtk::Widget *, TabConnection> _tab_connections;

    static std::list<DialogNotebook *> _instances;
    void add_highlight_header();
    void remove_highlight_header();

    // Signal handlers - notebook
    void on_drag_begin(Glib::RefPtr<Gdk::Drag> const &drag);
    void on_drag_end  (Glib::RefPtr<Gdk::Drag> const &drag, bool delete_data);
    void on_page_added(Gtk::Widget *page, int page_num);
    void on_page_removed(Gtk::Widget *page, int page_num);
    void size_allocate_vfunc(int width, int height, int baseline) final;
    void on_size_allocate_scroll  (int width);
    void on_size_allocate_notebook(int width);
    Gtk::EventSequenceState on_tab_click_event(Gtk::GestureClick const &click,
                                               int n_press, double x, double y,
                                               Gtk::Widget *page);
    void on_close_button_click_event(Gtk::Widget *page);
    void on_page_switch(Gtk::Widget *page, guint page_number);
    // Helpers
    bool provide_scroll(Gtk::Widget &page);
    void preventOverflow();
    void change_page(size_t pagenum);
    void reload_tab_menu();
    void toggle_tab_labels_callback(bool show);
    void add_tab_connections(Gtk::Widget *page);
    void remove_tab_connections(Gtk::Widget *page);
    void measure_vfunc(Gtk::Orientation orientation, int for_size, int &min, int &nat, int &min_baseline, int &nat_baseline) const override;
    int _natural_height = 0;
};

} // namespace Dialog

} // namespace Inkscape::UI

#endif // INKSCAPE_UI_DIALOG_NOTEBOOK_H

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
