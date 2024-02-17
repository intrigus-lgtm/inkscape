// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Main UI stuff.
 */
/* Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Frank Felfe <innerspace@iname.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *   Kris De Gussem <Kris.DeGussem@gmail.com>
 *
 * Copyright (C) 2012 Kris De Gussem
 * Copyright (C) 2010 authors
 * Copyright (C) 1999-2005 authors
 * Copyright (C) 2004 David Turner
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "interface.h"

#include <cassert>                 // for assert
#include <cstring>                 // for strlen

#include <glibmm/convert.h>        // for filename_to_utf8
#include <glibmm/i18n.h>           // for _
#include <glibmm/miscutils.h>      // for path_get_basename, path_get_dirname
#include <gtk/gtk.h>               // for gtk_dialog_run, gtk_widget_destroy
#include <gtkmm/messagedialog.h>   // for MessageDialog, ButtonsType

#include "desktop.h"               // for SPDesktop
#include "file.h"                  // for file_import
#include "inkscape-application.h"  // for InkscapeApplication
#include "inkscape.h"              // for Application, SP_ACTIVE_DOCUMENT

#include "io/sys.h"                // for file_test, sanitizeString
#include "ui/dialog-events.h"      // for sp_transientize
#include "ui/dialog-run.h"         // for dialog_run

class SPDocument;

void sp_ui_new_view()
{
    auto app = InkscapeApplication::instance();

    auto doc = SP_ACTIVE_DOCUMENT;
    if (!doc) {
        return;
    }

    app->window_open(doc);
}

void sp_ui_close_view()
{
    auto app = InkscapeApplication::instance();

    auto window = app->get_active_window();
    assert(window);

    app->destroy_window(window, true); // Keep inkscape alive!
}

Glib::ustring getLayoutPrefPath(SPDesktop *desktop)
{
    if (desktop->is_focusMode()) {
        return "/focus/";
    } else if (desktop->is_fullscreen()) {
        return "/fullscreen/";
    } else {
        return "/window/";
    }
}

void sp_ui_error_dialog(char const *message)
{
    auto const safeMsg = Inkscape::IO::sanitizeString(message);

    auto dlg = Gtk::MessageDialog(safeMsg, true, Gtk::MessageType::ERROR, Gtk::ButtonsType::CLOSE);
    sp_transientize(dlg);

    Inkscape::UI::dialog_run(dlg);
}

bool sp_ui_overwrite_file(std::string const &filename)
{
    // Fixme: Passing a std::string filename to an argument expecting utf8.
    if (!Inkscape::IO::file_test(filename.c_str(), G_FILE_TEST_EXISTS)) {
        return true;
    }

    auto const basename = Glib::filename_to_utf8(Glib::path_get_basename(filename));
    auto const dirname = Glib::filename_to_utf8(Glib::path_get_dirname(filename));
    auto const msg = Glib::ustring::compose(_("<span weight=\"bold\" size=\"larger\">A file named \"%1\" already exists. Do you want to replace it?</span>\n\n"
                                              "The file already exists in \"%2\". Replacing it will overwrite its contents."),
                                            basename, dirname);

    auto window = SP_ACTIVE_DESKTOP->getToplevel();
    auto dlg = Gtk::MessageDialog(*window, msg, true, Gtk::MessageType::QUESTION, Gtk::ButtonsType::NONE);
    dlg.add_button(_("_Cancel"), Gtk::ResponseType::NO);
    dlg.add_button(_("Replace"), Gtk::ResponseType::YES);
    dlg.set_default_response(Gtk::ResponseType::YES);

    return Inkscape::UI::dialog_run(dlg) == Gtk::ResponseType::YES;
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
