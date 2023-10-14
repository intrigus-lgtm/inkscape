// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Messages dialog - implementation.
 */
/* Authors:
 *   Bob Jamison
 *   Other dudes from The Inkscape Organization
 *
 * Copyright (C) 2004, 2005 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "messages.h"

#include "ui/pack.h"

namespace Inkscape::UI::Dialog {

//#########################################################################
//## E V E N T S
//#########################################################################

/**
 * Also a public method.  Remove all text from the dialog
 */
void Messages::clear()
{
    Glib::RefPtr<Gtk::TextBuffer> buffer = messageText.get_buffer();
    buffer->erase(buffer->begin(), buffer->end());
}

//#########################################################################
//## C O N S T R U C T O R    /    D E S T R U C T O R
//#########################################################################
/**
 * Constructor
 */
Messages::Messages()
    : DialogBase("/dialogs/messages", "Messages")
    , buttonClear(_("_Clear"), _("Clear log messages"))
    , checkCapture(_("Capture log messages"), _("Capture log messages"))
    , buttonBox(Gtk::Orientation::HORIZONTAL)
{
    messageText.set_editable(false);
    textScroll.set_child(messageText);
    textScroll.set_policy(Gtk::PolicyType::ALWAYS, Gtk::PolicyType::ALWAYS);
    UI::pack_start(*this, textScroll);

    buttonBox.set_spacing(6);
    UI::pack_start(buttonBox, checkCapture, true, true, 6);
    UI::pack_end(buttonBox, buttonClear, false, false, 10);
    UI::pack_start(*this, buttonBox, UI::PackOptions::shrink);

    // sick of this thing shrinking too much
    set_size_request(400, -1);

    message(_("Ready."));

    buttonClear.signal_clicked().connect(sigc::mem_fun(*this, &Messages::clear));
    checkCapture.signal_toggled().connect(sigc::mem_fun(*this, &Messages::toggleCapture));
}

//#########################################################################
//## M E T H O D S
//#########################################################################

void Messages::message(char const * const msg)
{
    Glib::RefPtr<Gtk::TextBuffer> buffer = messageText.get_buffer();
    Glib::ustring uMsg = msg;
    if (uMsg[uMsg.length()-1] != '\n')
        uMsg += '\n';
    buffer->insert (buffer->end(), uMsg);
}

// dialogLoggingCallback is already used in debug.cpp
static void dialogLoggingCallback(char const */*log_domain*/,
                                  GLogLevelFlags /*log_level*/,
                                  char const * const messageText,
                                  gpointer user_data)
{
    Messages *dlg = static_cast<Messages *>(user_data);
    dlg->message(messageText);

}

void Messages::toggleCapture()
{
    if (checkCapture.get_active()) {
        captureLogMessages();
    } else {
        releaseLogMessages();
    }
}

void Messages::captureLogMessages()
{
    /*
    This might likely need more code, to capture Gtkmm
    and Glibmm warnings, or maybe just simply grab stdout/stderr
    */
   GLogLevelFlags flags = (GLogLevelFlags) (G_LOG_LEVEL_ERROR   | G_LOG_LEVEL_CRITICAL |
                             G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE  |
                             G_LOG_LEVEL_INFO    | G_LOG_LEVEL_DEBUG);
    if ( !handlerDefault ) {
        handlerDefault = g_log_set_handler(nullptr, flags,
              dialogLoggingCallback, (gpointer)this);
    }
    if ( !handlerGlibmm ) {
        handlerGlibmm = g_log_set_handler("glibmm", flags,
              dialogLoggingCallback, (gpointer)this);
    }
    if ( !handlerAtkmm ) {
        handlerAtkmm = g_log_set_handler("atkmm", flags,
              dialogLoggingCallback, (gpointer)this);
    }
    if ( !handlerPangomm ) {
        handlerPangomm = g_log_set_handler("pangomm", flags,
              dialogLoggingCallback, (gpointer)this);
    }
    if ( !handlerGdkmm ) {
        handlerGdkmm = g_log_set_handler("gdkmm", flags,
              dialogLoggingCallback, (gpointer)this);
    }
    if ( !handlerGtkmm ) {
        handlerGtkmm = g_log_set_handler("gtkmm", flags,
              dialogLoggingCallback, (gpointer)this);
    }
    message(_("Log capture started."));
}

void Messages::releaseLogMessages()
{
    if ( handlerDefault ) {
        g_log_remove_handler(nullptr, handlerDefault);
        handlerDefault = 0;
    }
    if ( handlerGlibmm ) {
        g_log_remove_handler("glibmm", handlerGlibmm);
        handlerGlibmm = 0;
    }
    if ( handlerAtkmm ) {
        g_log_remove_handler("atkmm", handlerAtkmm);
        handlerAtkmm = 0;
    }
    if ( handlerPangomm ) {
        g_log_remove_handler("pangomm", handlerPangomm);
        handlerPangomm = 0;
    }
    if ( handlerGdkmm ) {
        g_log_remove_handler("gdkmm", handlerGdkmm);
        handlerGdkmm = 0;
    }
    if ( handlerGtkmm ) {
        g_log_remove_handler("gtkmm", handlerGtkmm);
        handlerGtkmm = 0;
    }
    message(_("Log capture stopped."));
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
