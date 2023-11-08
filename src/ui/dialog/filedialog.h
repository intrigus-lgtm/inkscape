// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Virtual base definitions for native file dialogs
 */
/* Authors:
 *   Bob Jamison <rwjj@earthlink.net>
 *   Joel Holdsworth
 *   Inkscape developers.
 *
 * Copyright (C) 2006 Johan Engelen <johan@shouraizou.nl>
 * Copyright (C) 2007-2008 Joel Holdsworth
 * Copyright (C) 2004-2008, Inkscape Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_FILE_DIALOG_H
#define SEEN_FILE_DIALOG_H

#include <map>
#include <vector>
#include <set>

#include "extension/system.h"

#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <giomm/listmodel.h>

namespace Gtk { class Window; }

class SPDocument;

namespace Gio {
class File;
} // namespace Gio

namespace Gtk {
class Window;
} // namespace Gtk

namespace Inkscape::Extension {
class Extension;
class Output;
} // namespace Inkscape::Extension

namespace Inkscape::UI::Dialog {

/**
 * Used for setting filters and options, and
 * reading them back from user selections.
 */
enum FileDialogType
{
    SVG_TYPES,
    IMPORT_TYPES,
    EXPORT_TYPES,
    EXE_TYPES,
    SWATCH_TYPES,
    CUSTOM_TYPE
};

/**
 * Used for returning the type selected in a SaveAs
 */
enum FileDialogSelectionType
{
    SVG_NAMESPACE,
    SVG_NAMESPACE_WITH_EXTENSIONS
};

/**
 * Return true if the string ends with the given suffix
 */
bool hasSuffix(const Glib::ustring &str, const Glib::ustring &ext);

/**
 * Set initial directory for dialog given a preference path.
 */
void get_start_directory(std::string &start_path, Glib::ustring const &prefs_path, bool try_document_dir = false);

/**
 * Return true if the image is loadable by Gdk, else false.
 * Only user is svg-preview.cpp which is disappearing, don't worry about string type.
 */
bool isValidImageFile(const Glib::ustring &fileName);

class FileDialog
{
public:
    /**
     * Return the 'key' (filetype) of the selection, if any
     * @return a pointer to a string if successful (which must
     * be later freed with g_free(), else NULL.
     */
    virtual Inkscape::Extension::Extension *getExtension() { return _extension; }
    virtual void setExtension(Inkscape::Extension::Extension *key) { _extension = key; }

    /**
     * Show file selector.
     * @return the selected path if user selected one, else NULL
     */
    virtual bool show() = 0;

    /**
     * Add a filter menu to the file dialog.
     */
    virtual void addFilterMenu(const Glib::ustring &name, Glib::ustring pattern = "",
                               Inkscape::Extension::Extension *mod = nullptr) = 0;

    /**
     * Get the current directory of the file dialog.
     */
    virtual Glib::RefPtr<Gio::File> getCurrentDirectory() = 0;

protected:
    // The selected extension
    Inkscape::Extension::Extension *_extension;
};

/**
 * This class provides an implementation-independent API for
 * file "Open" dialogs.  Using a standard interface obviates the need
 * for ugly #ifdefs in file open code
 */
class FileOpenDialog : public FileDialog
{
public:
    virtual ~FileOpenDialog() = default;

    /**
     * Factory.
     * @param path the directory where to start searching
     * @param fileTypes one of FileDialogTypes
     * @param title the title of the dialog
     */
    static std::unique_ptr<FileOpenDialog> create(Gtk::Window &parentWindow,
                                                  std::string const &path,
                                                  FileDialogType fileTypes,
                                                  char const *title);

    virtual void setSelectMultiple(bool value) = 0;
    virtual Glib::RefPtr<Gio::ListModel> getFiles() = 0;
    virtual Glib::RefPtr<Gio::File> getFile() = 0;

protected:
    FileOpenDialog() = default;
};

/**
 * This class provides an implementation-independent API for file "Save" dialogs.
 */
class FileSaveDialog : public FileDialog
{
public:
    // Constructor. Do not call directly. Use the factory.
    FileSaveDialog() = default;
    virtual ~FileSaveDialog() = default;

    /**
     * Factory.
     * @param path the directory where to start searching
     * @param fileTypes one of FileDialogTypes
     * @param title the title of the dialog
     * @param key a list of file types from which the user can select
     */
    static std::unique_ptr<FileSaveDialog> create(Gtk::Window &parentWindow,
                                                  std::string const &path,
                                                  FileDialogType fileTypes,
                                                  char const *title,
                                                  Glib::ustring const &default_key,
                                                  char const *docTitle,
                                                  Inkscape::Extension::FileSaveMethod save_method);

    virtual const Glib::RefPtr<Gio::File> getFile() = 0;
    virtual void setCurrentName(Glib::ustring) = 0;

    /**
     * Get the document title chosen by the user.
     * Valid after an [OK]
     */
    Glib::ustring getDocTitle () { return myDocTitle; } // Only used by rdf_set_work_entity()

protected:
    // Doc Title that was given
    Glib::ustring myDocTitle;

    // List of known file extensions.
    std::map<Glib::ustring, Inkscape::Extension::Output *> knownExtensions;

    void appendExtension(Glib::ustring& filename_utf8, Inkscape::Extension::Output* outputExtension);

}; //FileSaveDialog

} // namespace Inkscape::UI::Dialog

#endif // SEEN_FILE_DIALOG_H

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
