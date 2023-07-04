// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Event handler for dialog windows.
 */
/* Authors:
 *   bulia byak <bulia@dr.com>
 *   Johan Engelen <j.b.c.engelen@ewi.utwente.nl>
 *
 * Copyright (C) 2003-2014 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <gtkmm/entry.h>
#include <gtkmm/window.h>

#include "desktop.h"
#include "inkscape.h"
#include "enums.h"
#include "ui/dialog-events.h"

/**
 * Remove focus from window to whoever it is transient for.
 */
void sp_dialog_defocus_cpp(Gtk::Window *win)
{
    // find out the document window we're transient for
    if (auto w = win->get_transient_for()) {
        // switch to it
        w->present();
    }
}

void sp_dialog_defocus(GtkWindow *win)
{
    // find out the document window we're transient for
    if (auto w = gtk_window_get_transient_for(GTK_WINDOW(win))) {
        // switch to it
        gtk_window_present(w);
    }
}

void sp_dialog_defocus_on_enter_cpp(Gtk::Entry *e)
{
    e->signal_activate().connect([e] {
        sp_dialog_defocus_cpp(dynamic_cast<Gtk::Window*>(e->get_root()));
    });
}

static void sp_dialog_defocus_callback(GtkWindow*, gpointer data)
{
    sp_dialog_defocus(GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(data))));
}

void sp_dialog_defocus_on_enter(GtkWidget *w)
{
    g_signal_connect(G_OBJECT(w), "activate", G_CALLBACK(sp_dialog_defocus_callback), w);
}

/**
 * Make the argument dialog transient to the currently active document window.
 */
void sp_transientize(GtkWidget *dialog)
{
    auto prefs = Inkscape::Preferences::get();

#ifndef _WIN32
    // FIXME: Temporary Win32 special code to enable transient dialogs
    // _set_skip_taskbar_hint makes transient dialogs NON-transient! When dialogs
    // are made transient (_set_transient_for), they are already removed from
    // the taskbar in Win32.
    if (prefs->getBool( "/options/dialogsskiptaskbar/value")) {
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW (dialog), TRUE);
    }
#endif

    gint transient_policy = prefs->getIntLimited("/options/transientpolicy/value", PREFS_DIALOGS_WINDOWS_NORMAL,
                                                 PREFS_DIALOGS_WINDOWS_NONE, PREFS_DIALOGS_WINDOWS_AGGRESSIVE);

#ifdef _WIN32 // Win32 special code to enable transient dialogs
    transient_policy = PREFS_DIALOGS_WINDOWS_AGGRESSIVE;
#endif

    if (transient_policy) {
        // if there's an active document window, attach dialog to it as a transient:
        if (SP_ACTIVE_DESKTOP) {
            SP_ACTIVE_DESKTOP->setWindowTransient(dialog, transient_policy);
        }
    }
}

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
