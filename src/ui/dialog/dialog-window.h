// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A window for floating docks.
 *
 * Authors: see git history
 *   Tavmjong Bah
 *
 * Copyright (c) 2018 Tavmjong Bah, Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_DIALOG_WINDOW_H
#define INKSCAPE_UI_DIALOG_WINDOW_H

#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/window.h>
#include "inkscape-application.h"

class InkscapeWindow;

namespace Inkscape::UI::Dialog {

class DialogContainer;

/**
 * DialogWindow holds DialogContainer instances for undocked dialogs.
 *
 * It watches the last active InkscapeWindow and updates its inner dialogs, if any.
 */
class DialogWindow final : public Gtk::Window
{
public:
    DialogWindow(InkscapeWindow* window, Gtk::Widget *page = nullptr);

    void set_inkscape_window(InkscapeWindow *window);
    InkscapeWindow* get_inkscape_window() { return _inkscape_window; }

    void update_dialogs();
    void update_window_size_to_fit_children();

    DialogContainer *get_container() { return _container; }

private:
    bool on_key_pressed(GtkEventControllerKey *controller,
                        unsigned keyval, unsigned keycode, GdkModifierType state);

    InkscapeApplication *_app = nullptr;

    /// The Inkscape window that dialog window is attached to.
    /// Changes when mouse moves into new Inkscape window.
    InkscapeWindow *_inkscape_window = nullptr;

    DialogContainer *_container = nullptr;

    Glib::ustring _title;
};

} // namespace Inkscape::UI::Dialog

#endif // INKSCAPE_UI_DIALOG_WINDOW_H

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
