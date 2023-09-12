// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A base class for all dialogs.
 *
 * Authors: see git history
 *   Tavmjong Bah
 *
 * Copyright (c) 2018 Tavmjong Bah, Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "dialog-base.h"

#include <utility>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/refptr.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/notebook.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/viewport.h>
#include <gtkmm/window.h>

#include "inkscape.h"
#include "desktop.h"
#include "selection.h"
#include "ui/controller.h"
#include "ui/dialog-events.h"
#include "ui/dialog/dialog-data.h"
#include "ui/dialog/dialog-notebook.h"
#include "ui/tools/tool-base.h" // get_latin_keyval
#include "ui/util.h"
#include "ui/widget/canvas.h"
#include "widgets/spw-utilities.h"

namespace Inkscape::UI::Dialog {

static void remove_first(Glib::ustring &name, Glib::ustring const &pattern)
{
    if (auto const pos = name.find(pattern); pos != name.npos) {
        name.erase(pos, pattern.size());
    }
}

/**
 * DialogBase constructor.
 *
 * @param prefs_path characteristic path to load/save dialog position.
 * @param dialog_type is the "type" string for the dialog.
 */
DialogBase::DialogBase(char const * const prefs_path, Glib::ustring dialog_type)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , _name("DialogBase")
    , _prefs_path(prefs_path)
    , _dialog_type{std::move(dialog_type)}
    , _app(InkscapeApplication::instance())
{
    auto const &dialog_data = get_dialog_data();
    // Derive a pretty display name for the dialog.
    if (auto const it = dialog_data.find(_dialog_type); it != dialog_data.end()) {
        _name = it->second.label; // Already translated
        // remove ellipsis and mnemonics
        remove_first(_name, "...");
        remove_first(_name, "…"  );
        remove_first(_name, "_"  );
    }

    set_name(_dialog_type); // Essential for dialog functionality
    set_margin(1); // Essential for dialog UI

    // TODO: GTK4: See if we can add the Controller on self — since all widgets receive all events.
    Controller::add_key_on_window<&DialogBase::on_window_key_pressed>(*this, *this,
                                                                      Gtk::PropagationPhase::CAPTURE);
}

DialogBase::~DialogBase() {
#ifdef _WIN32
    // this is bad, but it supposedly fixes some resizng problem on Windows
    ensure_size();
#endif

    unsetDesktop();
};

void DialogBase::ensure_size() {
    if (desktop) {
        resize_widget_children(desktop->getToplevel());
        resize_widget_children(this);
    }
}

void DialogBase::on_map() {
    // Update asks the dialogs if they need their Gtk widgets updated.
    update();
    // Set the desktop on_map, although we might want to be smarter about this.
    // Note: Inkscape::Application::instance().active_desktop() is used here, as it contains current desktop at
    // the time of dialog creation. Formerly used _app.get_active_view() did not at application start-up.
    setDesktop(Inkscape::Application::instance().active_desktop());
    parent_type::on_map();
    ensure_size();
}

bool DialogBase::on_window_key_pressed(GtkEventControllerKey const * const controller,
                                       unsigned const keyval, unsigned const keycode,
                                       GdkModifierType const state)
{
    // We listen for key on window, so must ensure WE have focus, to not break Esc from canvas etc.
    auto const &window = dynamic_cast<Gtk::Window const &>(*get_toplevel());
    auto const focus = window.get_focus();
    if (!focus || !is_descendant_of(*focus, *this)) {
        return false;
    }

    switch (Inkscape::UI::Tools::get_latin_keyval(controller, keyval, keycode, state)) {
        case GDK_KEY_Escape:
            defocus_dialog();
            return true;
    }

    return false;
}

/**
 * Highlight notebook where dialog already exists.
 */
void DialogBase::blink()
{
    Gtk::Notebook *notebook = dynamic_cast<Gtk::Notebook *>(get_parent());
    if (notebook && notebook->get_is_drawable()) {
        // Switch notebook to this dialog.
        notebook->set_current_page(notebook->page_num(*this));
        notebook->add_css_class("blink");

        // Add timer to turn off blink.
        sigc::slot<bool ()> slot = sigc::mem_fun(*this, &DialogBase::blink_off);
        sigc::connection connection = Glib::signal_timeout().connect(slot, 1000); // msec
    }
}

void DialogBase::focus_dialog() {
    if (auto window = dynamic_cast<Gtk::Window*>(get_root())) {
        window->present();
    }

    // widget that had focus, if any
    if (auto child = get_focus_child()) {
        child->grab_focus();
    } else {
        // find first focusable widget
        if (auto const child = find_focusable_widget(*this)) {
            child->grab_focus();
        }
    }
}

void DialogBase::defocus_dialog() {
    if (auto wnd = dynamic_cast<Gtk::Window*>(get_root())) {
        // defocus floating dialog:
        sp_dialog_defocus_cpp(wnd);

        // for docked dialogs, move focus to canvas
        if (auto desktop = getDesktop()) {
            desktop->getCanvas()->grab_focus();
        }
    }
}

/**
 * Callback to reset the dialog highlight.
 */
bool DialogBase::blink_off()
{
    Gtk::Notebook *notebook = dynamic_cast<Gtk::Notebook *>(get_parent());
    if (notebook && notebook->get_is_drawable()) {
        notebook->remove_css_class("blink");
    }
    return false;
}

/**
 * Called when the desktop might have changed for this dialog.
 */
void DialogBase::setDesktop(SPDesktop *new_desktop)
{
    if (desktop == new_desktop) {
        return;
    }

    unsetDesktop();

    if (new_desktop) {
        desktop = new_desktop;

        if (auto sel = desktop->getSelection()) {
            selection = sel;
            _select_changed = selection->connectChanged([this](Inkscape::Selection *selection) {
                _changed_while_hidden = !_showing;
                if (_showing)
                    selectionChanged(selection);
            });
            _select_modified = selection->connectModified([this](Inkscape::Selection *selection, guint flags) {
                _modified_while_hidden = !_showing;
                _modified_flags = flags;
                if (_showing)
                    selectionModified(selection, flags);
            });
        }

        _doc_replaced = desktop->connectDocumentReplaced(sigc::hide<0>(sigc::mem_fun(*this, &DialogBase::setDocument)));
        _desktop_destroyed = desktop->connectDestroy(sigc::mem_fun(*this, &DialogBase::desktopDestroyed));
        this->setDocument(desktop->getDocument());

        if (desktop->getSelection()) {
            selectionChanged(selection);
        }
        set_sensitive(true);
    }

    desktopReplaced();
}

void DialogBase::fix_inner_scroll(Gtk::Widget * const widget)
{
    auto const scrollwin = dynamic_cast<Gtk::ScrolledWindow *>(widget);
    if (!scrollwin) return;

    Gtk::Widget *child = nullptr;
    if (auto const viewport = dynamic_cast<Gtk::Viewport *>(scrollwin->get_child())) {
        child = viewport->get_child();
    } else {
        child = scrollwin->get_child();
    }
    if (!child) return;

    child->signal_scroll_event().connect([this, adj = scrollwin->get_vadjustment()]
                                         (GdkEventScroll * const event)
    {
        auto const children = UI::get_children(*this);
        if (children.empty()) return false;

        auto const parentscroll = dynamic_cast<Gtk::ScrolledWindow *>(children.at(0));
        if (!parentscroll) return false;

        if (event->delta_y > 0 && adj->get_value() + adj->get_page_size() == adj->get_upper() ||
            event->delta_y < 0 && adj->get_value() == adj->get_lower())
        {
            parentscroll->event(reinterpret_cast<GdkEvent *>(event));
            return true;
        }
        return false;
    });
}

/**
 * function called from notebook dialog that performs an update of the dialog and sets the dialog showing state true
 */
void 
DialogBase::setShowing(bool showing) {
    _showing = showing;
    if (showing && _changed_while_hidden) {
        selectionChanged(getSelection());
        _changed_while_hidden = false;
    }
    if (showing && _modified_while_hidden) {
        selectionModified(getSelection(), _modified_flags);
        _modified_while_hidden = false;
    }
}

/**
 * Called to destruct desktops, must not call virtuals
 */
void DialogBase::unsetDesktop()
{
    desktop = nullptr;
    document = nullptr;
    selection = nullptr;
    _desktop_destroyed.disconnect();
    _doc_replaced.disconnect();
    _select_changed.disconnect();
    _select_modified.disconnect();
}

void DialogBase::desktopDestroyed(SPDesktop* old_desktop)
{
    if (old_desktop == desktop && desktop) {
        unsetDesktop();
        desktopReplaced();
        set_sensitive(false);
    }
}

/**
 * Called when the document might have changed, called from setDesktop too.
 */
void DialogBase::setDocument(SPDocument *new_document)
{
    if (document != new_document) {
        document = new_document;
        documentReplaced();
    }
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
