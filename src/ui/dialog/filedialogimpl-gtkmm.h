// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Implementation of the file dialog interfaces defined in filedialogimpl.h
 */
/* Authors:
 *   Bob Jamison
 *   Johan Engelen <johan@shouraizou.nl>
 *   Joel Holdsworth
 *   Bruno Dilly
 *   Others from The Inkscape Organization
 *
 * Copyright (C) 2004-2008 Authors
 * Copyright (C) 2004-2007 The Inkscape Organization
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_FILE_DIALOG_IMPL_GTKMM_H
#define SEEN_FILE_DIALOG_IMPL_GTKMM_H

#include <vector>
#include <giomm/file.h>
#include <glibmm/ustring.h>
#include <giomm/liststore.h>
#include <gtkmm/filechooserdialog.h>

#include "filedialog.h"

namespace Gtk {
class Entry;
class FileFilter;
class Window;
} // namespace Gtk

namespace Inkscape {

class URI;

namespace UI {

namespace View {
class SVGViewWidget;
} // namespace View

namespace Dialog {

/*#########################################################################
### F I L E     D I A L O G    B A S E    C L A S S
#########################################################################*/

/**
 * This class is the base implementation for the others.  This
 * reduces redundancies and bugs.
 */
class FileDialogBaseGtk : public Gtk::FileChooserDialog
{
public:
    FileDialogBaseGtk(Gtk::Window &parentWindow, Glib::ustring const &title,
                      Gtk::FileChooser::Action dialogType, FileDialogType type,
                      char const *preferenceBase);

    ~FileDialogBaseGtk() override;

    /**
     * Add a Gtk filter to our specially controlled filter dropdown.
     */
    Glib::RefPtr<Gtk::FileFilter> addFilter(const Glib::ustring &name, Glib::ustring pattern = "",
                                            Inkscape::Extension::Extension *mod = nullptr);

    Glib::ustring extToPattern(const Glib::ustring &extension) const;

protected:
    Glib::ustring const _preferenceBase;

    /**
     * What type of 'open' are we? (open, import, place, etc)
     */
    FileDialogType _dialogType;

    /**
     * Maps extension <-> filter.
     */
    std::map<Glib::RefPtr<Gtk::FileFilter>, Inkscape::Extension::Extension *> filterExtensionMap;
    std::map<Inkscape::Extension::Extension *, Glib::RefPtr<Gtk::FileFilter>> extensionFilterMap;
};

/*#########################################################################
### F I L E    O P E N
#########################################################################*/

/**
 * Our implementation class for the FileOpenDialog interface..
 */
class FileOpenDialogImplGtk final
    : public FileOpenDialog
    , public FileDialogBaseGtk
{
public:
    FileOpenDialogImplGtk(Gtk::Window& parentWindow,
                          std::string const &dir,
                          FileDialogType fileTypes,
                          Glib::ustring const &title);

    bool show() override;

    void setSelectMultiple(bool value) override { set_select_multiple(value); }
    Glib::RefPtr<Gio::ListModel> getFiles() override { return get_files(); }
    Glib::RefPtr<Gio::File> getFile() override { return get_file(); }

    Glib::RefPtr<Gio::File> getCurrentDirectory() override { return get_current_folder(); }

    void addFilterMenu(Glib::ustring const &name, Glib::ustring pattern = "",
                       Inkscape::Extension::Extension *mod = nullptr) override
    {
        addFilter(name, pattern, mod);
    }

private:
    /**
     *  Create a filter menu for this type of dialog
     */
    void createFilterMenu();
};

//########################################################################
//# F I L E    S A V E
//########################################################################

/**
 * Our implementation of the FileSaveDialog interface.
 */
class FileSaveDialogImplGtk final
    : public FileSaveDialog
    , public FileDialogBaseGtk
{
public:
    FileSaveDialogImplGtk(Gtk::Window &parentWindow,
                          const std::string &dir,
                          FileDialogType fileTypes,
                          const Glib::ustring &title,
                          const Glib::ustring &default_key,
                          const gchar* docTitle,
                          const Inkscape::Extension::FileSaveMethod save_method);

    bool show() final;

    // One at a time.
    const Glib::RefPtr<Gio::File> getFile() override { return get_file(); }

    void setCurrentName(Glib::ustring name) override { set_current_name(name); }
    Glib::RefPtr<Gio::File> getCurrentDirectory() override { return get_current_folder(); }

    // Sets module for saving, updating GUI if necessary.
    bool setExtension(Glib::ustring const &filename_utf8);
    void setExtension(Inkscape::Extension::Extension *key) override;

    void addFilterMenu(const Glib::ustring &name, Glib::ustring pattern = {},
                       Inkscape::Extension::Extension *mod = nullptr) override
    {
        addFilter(name, pattern, mod);
    }

private:
    /**
     * The file save method (essentially whether the dialog was invoked by "Save as ..." or "Save a
     * copy ..."), which is used to determine file extensions and save paths.
     */
    Inkscape::Extension::FileSaveMethod save_method;

    /**
     *  Create a filter menu for this type of dialog
     */
    void createFilterMenu();

    /**
     * Callback for filefilter.
     */
    void filefilterChanged();
    bool from_filefilter_changed = false;

    /**
     * Callback for filename.
     */
    void filenameChanged();
    bool from_filename_changed = false;
};

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif // SEEN_FILE_DIALOG_IMPL_GTKMM_H

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
