// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A window for floating dialogs.
 *
 * Authors: see git history
 *   Tavmjong Bah
 *
 * Copyright (c) 2018 Tavmjong Bah, Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "dialog-window.h"

#include <algorithm>
#include <iostream>
#include <glibmm/i18n.h>
#include <giomm/actiongroup.h>
#include <gtkmm/application.h>
#include <gtkmm/box.h>

#include "document.h"
#include "inkscape.h"
#include "inkscape-application.h"
#include "inkscape-window.h"
#include "preferences.h"
#include "ui/controller.h"
#include "ui/dialog/dialog-base.h"
#include "ui/dialog/dialog-container.h"
#include "ui/dialog/dialog-manager.h"
#include "ui/dialog/dialog-multipaned.h"
#include "ui/dialog/dialog-notebook.h"
#include "ui/pack.h"
#include "ui/themes.h"

// Sizing constants
static constexpr int MINIMUM_WINDOW_WIDTH       = 210;
static constexpr int MINIMUM_WINDOW_HEIGHT      = 320;
static constexpr int INITIAL_WINDOW_WIDTH       = 360;
static constexpr int INITIAL_WINDOW_HEIGHT      = 520;
static constexpr int WINDOW_DROPZONE_SIZE       =  10;
static constexpr int WINDOW_DROPZONE_SIZE_LARGE =  16;
static constexpr int NOTEBOOK_TAB_HEIGHT        =  36;

namespace Inkscape::UI::Dialog {

[[nodiscard]] static auto get_max_margin(Gtk::Widget const &widget)
{
    return std::max({widget.get_margin_top(), widget.get_margin_bottom(),
                     widget.get_margin_start(), widget.get_margin_end()});
}

// Create a dialog window and move page from old notebook.
DialogWindow::DialogWindow(InkscapeWindow *inkscape_window, Gtk::Widget *page)
    : Gtk::Window()
    , _app(InkscapeApplication::instance())
    , _inkscape_window(inkscape_window)
    , _title(_("Dialog Window"))
{
    g_assert(_app != nullptr);
    g_assert(_inkscape_window != nullptr);

    // ============ Initialization ===============
    set_name("DialogWindow");
    set_transient_for(*inkscape_window);
    _app->gtk_app()->add_window(*this);

    signal_close_request().connect([this]{
        DialogManager::singleton().store_state(*this);
        delete this;
        return true;
    }, false); // before GTKʼs default handler, so it wonʼt try to double-delete

    auto win_action_group = dynamic_cast<Gio::ActionGroup *>(inkscape_window);
    if (win_action_group) {
        // C++ API requires Glib::RefPtr<Gio::ActionGroup>, use C API here.
        gtk_widget_insert_action_group(Gtk::Widget::gobj(), "win", win_action_group->gobj());
    } else {
        std::cerr << "DialogWindow::DialogWindow: Can't find InkscapeWindow Gio:ActionGroup!" << std::endl;
    }

    insert_action_group("doc", inkscape_window->get_document()->getActionGroup());

    // ================ Window ==================
    set_title(_title);
    set_name(_title);
    int window_width = INITIAL_WINDOW_WIDTH;
    int window_height = INITIAL_WINDOW_HEIGHT;

    // =============== Outer Box ================
    auto const box_outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    set_child(*box_outer);

    // =============== Container ================
    _container = Gtk::make_managed<DialogContainer>(inkscape_window);
    DialogMultipaned *columns = _container->get_columns();
    auto drop_size = Inkscape::Preferences::get()->getBool("/options/dockingzone/value", true) ? WINDOW_DROPZONE_SIZE / 2 : WINDOW_DROPZONE_SIZE;
    columns->set_dropzone_sizes(drop_size, drop_size);
    UI::pack_end(*box_outer, *_container);

    // If there is no page, create an empty Dialogwindow to be populated later
    if (page) {
        // ============= Initial Column =============
        DialogMultipaned *column = _container->create_column();
        columns->append(*column);

        // ============== New Notebook ==============
        auto const dialog_notebook = Gtk::make_managed<DialogNotebook>(_container);
        column->append(*dialog_notebook);
        column->set_dropzone_sizes(drop_size, drop_size);
        dialog_notebook->move_page(*page);

        // Set window title
        DialogBase *dialog = dynamic_cast<DialogBase *>(page);
        if (dialog) {
            _title = dialog->get_name();
            set_title(_title);
        }

        // Set window size considering what the dialog needs
        Gtk::Requisition minimum_size, natural_size;
        dialog->get_preferred_size(minimum_size, natural_size);
        int overhead = 2 * (drop_size + get_max_margin(*dialog));
        int width = natural_size.get_width() + overhead;
        int height = natural_size.get_height() + overhead + NOTEBOOK_TAB_HEIGHT;
        window_width = std::max(width, window_width);
        window_height = std::max(height, window_height);
    }

    // Set window sizing
    set_size_request(MINIMUM_WINDOW_WIDTH, MINIMUM_WINDOW_HEIGHT);
    set_default_size(window_width, window_height);

    if (page) {
        update_dialogs();
    }

    // To get right symbolic/regular class & other theming, apply themechange after adding children
    auto const themecontext = INKSCAPE.themecontext;
    g_assert(themecontext);
    themecontext->themechangecallback();

    // TODO: Double-check the phase. This needs to be called after default Window handlerʼs CAPTURE
    Controller::add_key<&DialogWindow::on_key_pressed>
                       (*this, *this, Gtk::PropagationPhase::TARGET);

    // window is created hidden; don't show it now, its size needs to be restored
}

/**
 * Change InkscapeWindow that DialogWindow is linked to.
 */
void DialogWindow::set_inkscape_window(InkscapeWindow* inkscape_window)
{
    if (!inkscape_window) {
        std::cerr << "DialogWindow::set_inkscape_window: no inkscape_window!" << std::endl;
        return;
    }

    _inkscape_window = inkscape_window;
    update_dialogs();
}

/**
 * Update all dialogs that are owned by the DialogWindow's _container.
 */
void DialogWindow::update_dialogs()
{
    g_assert(_app != nullptr);
    g_assert(_container != nullptr);
    g_assert(_inkscape_window != nullptr);

    _container->set_inkscape_window(_inkscape_window);
    _container->update_dialogs(); // Updating dialogs is not using the _app reference here.

    // Set window title.
    auto const &dialogs = _container->get_dialogs();
    if (dialogs.size() > 1) {
        _title = "Multiple dialogs";
    } else if (dialogs.size() == 1) {
        _title = dialogs.begin()->second->get_name();
    } else {
        // Should not happen... but does on closing a window!
        // std::cerr << "DialogWindow::update_dialogs(): No dialogs!" << std::endl;
        _title = "";
    }

    auto document_name = _inkscape_window->get_document()->getDocumentName();
    if (document_name) {
        set_title(_title + " - " + Glib::ustring(document_name));
    }
}

/**
 * Update window width and height in order to fit all dialogs inisde its container.
 *
 * The intended use of this function is at initialization.
 */
void DialogWindow::update_window_size_to_fit_children()
{
    // Declare variables
    int width = 0, height = 0;
    int overhead = 0;

    // Read needed data
    auto allocation = get_allocation();
    auto const &dialogs = _container->get_dialogs();

    // Get largest sizes for dialogs
    for (auto const &[name, dialog] : dialogs) {
        Gtk::Requisition minimum_size, natural_size;
        dialog->get_preferred_size(minimum_size, natural_size);
        width = std::max(natural_size.get_width(), width);
        height = std::max(natural_size.get_height(), height);
        overhead = std::max(overhead, get_max_margin(*dialog));
    }

    // Compute sizes including overhead
    overhead = 2 * (WINDOW_DROPZONE_SIZE_LARGE + overhead);
    width += overhead;
    height += overhead + NOTEBOOK_TAB_HEIGHT;

    // If sizes are lower then current, don't change them
    if (allocation.get_width() >= width && allocation.get_height() >= height) {
        return;
    }

    // Compute largest sizes on both axis
    width = std::max(width, allocation.get_width());
    height = std::max(height, allocation.get_height());

    // Resize window
    set_default_size(width, height);

    // Note: This function also used to maintain the center of the window
    // before GTK4 removed the ability to do that.
}

bool DialogWindow::on_key_pressed(GtkEventControllerKey * const controller,
                                  unsigned const keyval, unsigned const keycode,
                                  GdkModifierType const state)
{
    assert(_inkscape_window);
    return gtk_event_controller_key_forward(controller, _inkscape_window->Gtk::Widget::gobj());
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
