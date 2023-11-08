// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Implementation of the file dialog interfaces defined in filedialog.h.
 */
/* Authors:
 *   Bob Jamison
 *   Joel Holdsworth
 *   Others from The Inkscape Organization
 *
 * Copyright (C) 2004-2007 Bob Jamison
 * Copyright (C) 2006 Johan Engelen <johan@shouraizou.nl>
 * Copyright (C) 2007-2008 Joel Holdsworth
 * Copyright (C) 2004-2008 The Inkscape Organization
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "filedialog.h"

#include <gtkmm.h>  // Glib::get_home_dir()
#include <glibmm/convert.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "filedialogimpl-gtkmm.h"

#include "preferences.h"
#include "ui/dialog-events.h"
#include "extension/output.h"


namespace Inkscape::UI::Dialog {

/*#########################################################################
  ### U T I L I T Y
  #########################################################################*/

bool hasSuffix(const Glib::ustring &str, const Glib::ustring &ext)
{
    int strLen = str.length();
    int extLen = ext.length();
    if (extLen > strLen)
        return false;
    int strpos = strLen-1;
    for (int extpos = extLen-1 ; extpos>=0 ; extpos--, strpos--) {
        Glib::ustring::value_type ch = str[strpos];
        if (ch != ext[extpos]) {
            if ( ((ch & 0xff80) != 0) ||
                 static_cast<Glib::ustring::value_type>( g_ascii_tolower( static_cast<gchar>(0x07f & ch) ) ) != ext[extpos] )
            {
                return false;
            }
        }
    }
    return true;
}

// start_path is usually a static string.
// try_document_dir only used by WIN32.
void get_start_directory(std::string &start_path, Glib::ustring const &prefs_path, bool try_document_dir)
{
    // Get the current directory for finding files.
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    std::string attr = prefs->getString(prefs_path); // Glib::ustring -> std::string
    if (!attr.empty()) {
        start_path = attr;
    }

    // Test if the path directory exists.
    if (!Glib::file_test(start_path, Glib::FileTest::EXISTS)) {
        start_path = "";
    }

#ifdef _WIN32
    // If no start path, default to our win32 documents folder.
    if (start_path.empty() && try_document_dir) {

        // The path to the My Documents folder is read from the
        // value "HKEY_CURRENT_USER\Software\Windows\CurrentVersion\Explorer\Shell Folders\Personal"
        HKEY key = NULL;
        if (RegOpenKeyExA(HKEY_CURRENT_USER,
                          "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders",
                          0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
        {
            WCHAR utf16path[_MAX_PATH];
            DWORD value_type;
            DWORD data_size = sizeof(utf16path);
            if(RegQueryValueExW(key, L"Personal", NULL, &value_type,
                                (BYTE*)utf16path, &data_size) == ERROR_SUCCESS)
            {
                g_assert(value_type == REG_SZ);
                gchar *utf8path = g_utf16_to_utf8(
                    (const gunichar2*)utf16path, -1, NULL, NULL, NULL);
                if (utf8path) {
                    start_path = Glib::ustring(utf8path); // Glib::ustring -> std::string
                    g_free(utf8path);
                }
            }
        }
    }
#endif

    // If no start path, default to our home directory.
    if (start_path.empty()) {
        start_path = Glib::get_home_dir();
        start_path.append(G_DIR_SEPARATOR_S);
    }
}

bool isValidImageFile(const Glib::ustring &fileName)
{
    std::vector<Gdk::PixbufFormat>formats = Gdk::Pixbuf::get_formats(); // Returns Glib::ustrings!
    for (auto format : formats)
    {
        std::vector<Glib::ustring>extensions = format.get_extensions();
        for (auto ext : extensions)
        {
            if (hasSuffix(fileName, ext))
                return true;
        }
    }
    return false;
}

/*#########################################################################
  ### F I L E    O P E N
  #########################################################################*/

/**
 * Public factory.  Called by file.cpp, among others.
 */
std::unique_ptr<FileOpenDialog> FileOpenDialog::create(Gtk::Window &parentWindow,
                                                       std::string const &path,
                                                       FileDialogType fileTypes,
                                                       char const *title)
{
    return std::make_unique<FileOpenDialogImplGtk>(parentWindow, path, fileTypes, title);
}

//########################################################################
//# F I L E    S A V E
//########################################################################

/**
 * Public factory method.  Used in file.cpp
 */
std::unique_ptr<FileSaveDialog> FileSaveDialog::create(Gtk::Window& parentWindow,
                                                       std::string const &path,
                                                       FileDialogType fileTypes,
                                                       char const *title,
                                                       Glib::ustring const &default_key,
                                                       char const *docTitle,
                                                       Inkscape::Extension::FileSaveMethod save_method)
{
    return std::make_unique<FileSaveDialogImplGtk>(parentWindow, path, fileTypes, title, default_key, docTitle, save_method);
}

// Used in FileSaveDialogImplGtk to update displayed filename (thus utf8).
void FileSaveDialog::appendExtension(Glib::ustring& filename_utf8, Inkscape::Extension::Output* outputExtension)
{
    if (!outputExtension) {
        return;
    }

    bool appendExtension = true;
    Glib::ustring::size_type pos = filename_utf8.rfind('.');
    if ( pos != Glib::ustring::npos ) {
        Glib::ustring trail = filename_utf8.substr( pos );
        Glib::ustring foldedTrail = trail.casefold();
        if ( (trail == ".")
             | (foldedTrail != Glib::ustring( outputExtension->get_extension() ).casefold()
                && ( knownExtensions.find(foldedTrail) != knownExtensions.end() ) ) ) {
            filename_utf8 = filename_utf8.erase( pos );
        } else {
            appendExtension = false;
        }
    }

    if (appendExtension) {
        filename_utf8 = filename_utf8 + outputExtension->get_extension();
    }
}

} //namespace Inkscape::UI::Dialog

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
