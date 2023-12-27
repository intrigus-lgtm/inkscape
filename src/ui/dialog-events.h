// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief  Event handler for dialog windows
 */
/* Authors:
 *   bulia byak <bulia@dr.com>
 *
 * Copyright (C) 2003-2014 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_DIALOG_EVENTS_H
#define SEEN_DIALOG_EVENTS_H

#include <gtk/gtk.h>
 
namespace Gtk {
class Entry;
class SpinButton;
class Window;
} // namespace Gtk

void sp_dialog_defocus(Gtk::Window *win);
void sp_dialog_defocus_on_enter(Gtk::Entry *e);
void sp_dialog_defocus_on_enter(Gtk::SpinButton &s);
void sp_transientize(Gtk::Window &win);

#endif // SEEN_DIALOG_EVENTS_H

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
