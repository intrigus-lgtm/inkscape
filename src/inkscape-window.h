// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Inkscape - An SVG editor.
 */
/*
 * Authors:
 *   Tavmjong Bah
 *
 * Copyright (C) 2018 Authors
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 * Read the file 'COPYING' for more information.
 *
 */

#ifndef INKSCAPE_WINDOW_H
#define INKSCAPE_WINDOW_H

#include <glibmm/refptr.h>
#include <gdkmm/toplevel.h>
#include <gtkmm/applicationwindow.h>

#include "helper/auto-connection.h"

namespace Gtk { class Box; }

class InkscapeApplication;
class SPDocument;
class SPDesktop;
class SPDesktopWidget;

class InkscapeWindow final : public Gtk::ApplicationWindow
{
public:
    InkscapeWindow(SPDocument* document);
    ~InkscapeWindow() final;

    SPDocument*      get_document()       { return _document; }
    SPDesktop*       get_desktop()        { return _desktop; }
    SPDesktopWidget* get_desktop_widget() { return _desktop_widget; }
    void change_document(SPDocument* document);

private:
    InkscapeApplication *_app = nullptr;
    SPDocument*          _document = nullptr;
    SPDesktop*           _desktop = nullptr;
    SPDesktopWidget*     _desktop_widget = nullptr;
    Gtk::Box*      _mainbox = nullptr;
    Glib::RefPtr<Gtk::ShortcutController> _shortcut_controller;

    void setup_view();
    void add_document_actions();

    Inkscape::auto_connection _toplevel_state_connection;
    Gdk::Toplevel::State _old_toplevel_state{};

    void on_realize() override;
    Glib::RefPtr<Gdk::Toplevel const> get_toplevel() const;
    void on_toplevel_state_changed();
    void on_is_active_changed();
    bool on_close_request() override;
    void on_size_changed();

    void update_dialogs();
};

#endif // INKSCAPE_WINDOW_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
