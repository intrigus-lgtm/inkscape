// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Implementation of the file dialog interfaces defined in filedialogimpl.h.
 */
/* Authors:
 *   Bob Jamison
 *   Joel Holdsworth
 *   Bruno Dilly
 *   Others from The Inkscape Organization
 *   Abhishek Sharma
 *
 * Copyright (C) 2004-2007 Bob Jamison
 * Copyright (C) 2006 Johan Engelen <johan@shouraizou.nl>
 * Copyright (C) 2007-2008 Joel Holdsworth
 * Copyright (C) 2004-2007 The Inkscape Organization
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "filedialogimpl-gtkmm.h"

#include <glibmm/convert.h>
#include <glibmm/fileutils.h>
#include <glibmm/i18n.h>
#include <glibmm/miscutils.h>
#include <glibmm/stringutils.h>

#include "extension/db.h"
#include "extension/input.h"
#include "extension/output.h"
#include "io/resource.h"
#include "io/sys.h"
#include "preferences.h"
#include "ui/dialog-events.h"
#include "ui/dialog-run.h"

namespace Inkscape::UI::Dialog {

/*#########################################################################
### F I L E     D I A L O G    B A S E    C L A S S
#########################################################################*/

FileDialogBaseGtk::FileDialogBaseGtk(Gtk::Window &parentWindow, Glib::ustring const &title,
                                     Gtk::FileChooser::Action const dialogType,
                                     FileDialogType const type,
                                     char const * const preferenceBase)
    : Gtk::FileChooserDialog{parentWindow, title, dialogType}
    , _preferenceBase{preferenceBase ? preferenceBase : "unknown"}
    , _dialogType(type)
{
    reference();
}

FileDialogBaseGtk::~FileDialogBaseGtk()
{
    unreference();
}

Glib::RefPtr<Gtk::FileFilter> FileDialogBaseGtk::addFilter(const Glib::ustring &name, Glib::ustring ext,
                                                           Inkscape::Extension::Extension *extension)
{
    auto filter = Gtk::FileFilter::create();
    filter->set_name(name);
    add_filter(filter);

    if (!ext.empty()) {
        filter->add_pattern(extToPattern(ext));
    }

    filterExtensionMap[filter] = extension;
    extensionFilterMap[extension] = filter;

    return filter;
}

// Replace this with add_suffix in Gtk4
Glib::ustring FileDialogBaseGtk::extToPattern(const Glib::ustring &extension) const
{
    Glib::ustring pattern = "*";
    for (unsigned int ch : extension) {
        if (Glib::Unicode::isalpha(ch)) {
            pattern += '[';
            pattern += Glib::Unicode::toupper(ch);
            pattern += Glib::Unicode::tolower(ch);
            pattern += ']';
        } else {
            pattern += ch;
        }
    }
    return pattern;
}

/*#########################################################################
### F I L E    O P E N
#########################################################################*/

/**
 * Constructor.  Not called directly.  Use the factory.
 */
FileOpenDialogImplGtk::FileOpenDialogImplGtk(Gtk::Window &parentWindow, const std::string &dir,
                                             FileDialogType fileTypes, const Glib::ustring &title)
    : FileDialogBaseGtk(parentWindow, title, Gtk::FileChooser::Action::OPEN, fileTypes, "/dialogs/open")
{
    if (_dialogType == EXE_TYPES) {
        /* One file at a time */
        set_select_multiple(false);
    } else {
        /* And also Multiple Files */
        set_select_multiple(true);
    }

    // set_local_only(false); // Gone?

    /* Set our dialog type (open, import, etc...)*/
    _dialogType = fileTypes;

    /* Set the pwd and/or the filename */
    if (dir.size() > 0) {
        std::string udir(dir);
        std::string::size_type len = udir.length();
        // leaving a trailing backslash on the directory name leads to the infamous
        // double-directory bug on win32

        if (len != 0 && udir[len - 1] == '\\') {
            udir.erase(len - 1);
        }

        auto file = Gio::File::create_for_path(udir);
        if (_dialogType == EXE_TYPES) {
            set_file(file);
        } else {
            set_current_folder(file);
        }
    }

    // Add the file types menu.
    createFilterMenu();

    add_button(_("_Cancel"), Gtk::ResponseType::CANCEL);
    add_button(_("_Open"),   Gtk::ResponseType::OK);
    set_default_response(Gtk::ResponseType::OK);

    // Allow easy access to our examples folder.
    using namespace Inkscape::IO::Resource;
    auto examplesdir = get_path_string(SYSTEM, EXAMPLES);
    if (Glib::file_test(examplesdir, Glib::FileTest::IS_DIR) && Glib::path_is_absolute(examplesdir)) {
        add_shortcut_folder(Gio::File::create_for_path(examplesdir));
    }
}

void FileOpenDialogImplGtk::createFilterMenu()
{
    if (_dialogType == CUSTOM_TYPE) {
        return;
    }

    addFilter(_("All Files"), "*");

    if (_dialogType != EXE_TYPES) {
        auto allInkscapeFilter = addFilter(_("All Inkscape Files"));
        auto allImageFilter = addFilter(_("All Images"));
        auto allVectorFilter = addFilter(_("All Vectors"));
        auto allBitmapFilter = addFilter(_("All Bitmaps"));

        // patterns added dynamically below
        Inkscape::Extension::DB::InputList extension_list;
        Inkscape::Extension::db.get_input_list(extension_list);

        for (auto imod : extension_list)
        {
            addFilter(imod->get_filetypename(true), imod->get_extension(), imod);

            auto upattern = extToPattern(imod->get_extension());
            allInkscapeFilter->add_pattern(upattern);
            if (strncmp("image", imod->get_mimetype(), 5) == 0)
                allImageFilter->add_pattern(upattern);

            // I don't know of any other way to define "bitmap" formats other than by listing them
            if (strncmp("image/png", imod->get_mimetype(), 9) == 0 ||
                strncmp("image/jpeg", imod->get_mimetype(), 10) == 0 ||
                strncmp("image/gif", imod->get_mimetype(), 9) == 0 ||
                strncmp("image/x-icon", imod->get_mimetype(), 12) == 0 ||
                strncmp("image/x-navi-animation", imod->get_mimetype(), 22) == 0 ||
                strncmp("image/x-cmu-raster", imod->get_mimetype(), 18) == 0 ||
                strncmp("image/x-xpixmap", imod->get_mimetype(), 15) == 0 ||
                strncmp("image/bmp", imod->get_mimetype(), 9) == 0 ||
                strncmp("image/vnd.wap.wbmp", imod->get_mimetype(), 18) == 0 ||
                strncmp("image/tiff", imod->get_mimetype(), 10) == 0 ||
                strncmp("image/x-xbitmap", imod->get_mimetype(), 15) == 0 ||
                strncmp("image/x-tga", imod->get_mimetype(), 11) == 0 ||
                strncmp("image/x-pcx", imod->get_mimetype(), 11) == 0)
            {
                allBitmapFilter->add_pattern(upattern);
             } else {
                allVectorFilter->add_pattern(upattern);
            }
        }
    }
    return;
}

/**
 * Show this dialog modally.  Return true if user hits [OK]
 */
bool FileOpenDialogImplGtk::show()
{
    set_modal(true); // Window
    sp_transientize(*this); // Make transient
    int response = dialog_run(*this); // Dialog

    if (response == Gtk::ResponseType::OK) {
        setExtension(filterExtensionMap[get_filter()]);
        return true;
    }

    return false;
}


//########################################################################
//# F I L E    S A V E
//########################################################################

/**
 * Constructor
 */
FileSaveDialogImplGtk::FileSaveDialogImplGtk(Gtk::Window &parentWindow, const std::string &dir,
                                             FileDialogType fileTypes, const Glib::ustring &title,
                                             const Glib::ustring & /*default_key*/, const gchar *docTitle,
                                             const Inkscape::Extension::FileSaveMethod save_method)
    : FileDialogBaseGtk(parentWindow, title, Gtk::FileChooser::Action::SAVE, fileTypes,
                        (save_method == Inkscape::Extension::FILE_SAVE_METHOD_SAVE_COPY) ? "/dialogs/save_copy"
                                                                                         : "/dialogs/save_as")
    , save_method(save_method)
{
    if (docTitle) {
        FileSaveDialog::myDocTitle = docTitle;
    }

    // One file at a time.
    set_select_multiple(false);

    // set_local_only(false); // Gone?

    // ===== Choices =====

    add_choice("Extension",  _("Append filename extension automatically"));
    add_choice("SVG1.1",     _("Export as SVG 1.1 per settings in Preferences dialog"));

    // Initial choice values.
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    // Append extention automatically?
    bool append_extension = false;
    if (save_method == Inkscape::Extension::FILE_SAVE_METHOD_SAVE_COPY) {
        append_extension = prefs->getBool("/dialogs/save_copy/append_extension", true);
    } else {
        append_extension = prefs->getBool("/dialogs/save_as/append_extension", true);
    }
    set_choice("Extension", append_extension ? "true" : "false");  // set_boolean_extension missing

    // Export as SVG1.1?
    bool export_as_svg1_1 = prefs->getBool(_preferenceBase + "/dialogs/s/enable_svgexport", false); 
    set_choice("SVG1.1", export_as_svg1_1 ? "true" : "false");

    // ===== Filters =====

    if (_dialogType != CUSTOM_TYPE) {
        createFilterMenu();
    }

    // ===== Templates =====

    // Allow easy access to the user's own templates folder.
    using namespace Inkscape::IO::Resource;
    char const *templates = Inkscape::IO::Resource::get_path(USER, TEMPLATES);
    if (Inkscape::IO::file_test(templates, G_FILE_TEST_EXISTS) &&
        Inkscape::IO::file_test(templates, G_FILE_TEST_IS_DIR) && g_path_is_absolute(templates)) {
        add_shortcut_folder(Gio::File::create_for_path(templates));
    }

    // ===== Buttons =====

    add_button(_("_Cancel"), Gtk::ResponseType::CANCEL);
    add_button(_("_Save"),   Gtk::ResponseType::OK);
    set_default_response(Gtk::ResponseType::OK);

    // ===== Initial Value =====

    // Set the directory or filename. Do this last, after dialog is completely set up.
    if (dir.size() > 0) {
        std::string udir(dir);
        std::string::size_type len = udir.length();
        // Leaving a trailing backslash on the directory name leads to the infamous
        // double-directory bug on win32.
        if ((len != 0) && (udir[len - 1] == '\\')) {
            udir.erase(len - 1);
        }

        auto file = Gio::File::create_for_path(udir);
        auto display_name = Glib::filename_to_utf8(file->get_basename());
        Gio::FileType type = file->query_file_type();
        switch (type) {
            case Gio::FileType::UNKNOWN:
                set_file(file); // Set directory.
                set_current_name(display_name); // Set entry (Glib::ustring).
                break;
            case Gio::FileType::REGULAR:
                // The extension set here is over-written when called by sp_file_save_dialog().
                set_file(file); // Set directory (but not entry).
                set_current_name(display_name); // Set entry.
                break;
            case Gio::FileType::DIRECTORY:
                set_current_folder(file); // Set directory.
                break;
            default:
                std::cerr << "FileDialogImplGtk: Unknown file type: " << (int)type << std::endl;
        }
    }

    property_filter().signal_changed().connect([this]() { filefilterChanged(); });
    // signal_selection_changed().connect([this]() { filenameChanged(); }); // Gone?
}

/**
 * Show this dialog modally.  Return true if user hits [OK]
 */
bool FileSaveDialogImplGtk::show()
{
    set_modal(true); // Window
    sp_transientize(*this);

    int response = dialog_run(*this); // Dialog

    if (response == Gtk::ResponseType::OK) {

        Inkscape::Preferences *prefs = Inkscape::Preferences::get();

        // Store changes of "Choices".
        bool append_extension = get_choice("Extension") == "true";
        bool save_as_svg1_1   = get_choice("SVG1.1")    == "true";
        if (save_method == Inkscape::Extension::FILE_SAVE_METHOD_SAVE_COPY) {
            prefs->setBool("/dialogs/save_copy/append_extension", append_extension);
            prefs->setBool("/dialogs/save_copy/enable_svgexport", save_as_svg1_1);
        } else {
            prefs->setBool("/dialogs/save_as/append_extension", append_extension);
            prefs->setBool("/dialogs/save_as/enable_svgexport", save_as_svg1_1);
        }

        auto extension = getExtension();
        Inkscape::Extension::store_file_extension_in_prefs((extension != nullptr ? extension->get_id() : ""), save_method);
        return true;
    }

    return false;
} // show()

// Given filename, find module for saving. If found, update all.
bool FileSaveDialogImplGtk::setExtension(Glib::ustring const &filename_utf8)
{
    // Find module
    Glib::ustring filename_folded = filename_utf8.casefold();
    Inkscape::Extension::Extension* key = nullptr;
    for (auto const &iter : knownExtensions) {
        auto ext = Glib::ustring(iter.second->get_extension()).casefold();
         if (Glib::str_has_suffix(filename_folded, ext)) {
            key = iter.second;
        }
    }

    if (key) {
        // Update all.
        setExtension(key);
        return true;
    }

    // This happens when saving shortcuts.
    // std::cerr << "FileSaveDialogImpGtk: no extension to handle this file type: " << filename_utf8 << std::endl;
    return false;
}

// Given module, set filter and filename (if required).
// If module is null, try to find module from current name.
void FileSaveDialogImplGtk::setExtension(Inkscape::Extension::Extension *key)
{
    if (!key) {
        // Try to use filename.
        auto filename_utf8 = get_current_name();
        setExtension(filename_utf8);
    }

    // Save module.
    FileDialog::setExtension(key);

    // Update filter.
    if (!from_filefilter_changed) {
        set_filter(extensionFilterMap[key]);
    }
    from_filefilter_changed = false;

    // Update filename.
    if (!from_filename_changed) {
        auto filename_utf8 = get_current_name(); // UTF8 encoded!
        auto output = dynamic_cast<Inkscape::Extension::Output *>(getExtension());
        if (output && get_choice("Extension") == "true") {
            // Append the file extension if it's not already present and display it in the file name entry field.
            appendExtension(filename_utf8, output);
            set_current_name(filename_utf8);
        }
    }
    from_filename_changed = false;
}

void FileSaveDialogImplGtk::createFilterMenu()
{
    Inkscape::Extension::DB::OutputList extension_list;
    Inkscape::Extension::db.get_output_list(extension_list);
    knownExtensions.clear();

    addFilter(_("Guess from extension"), "*"); // No output module!

    for (auto omod : extension_list) {
        // Export types are either exported vector types, or any raster type.
        if (!omod->is_exported() && omod->is_raster() != (_dialogType == EXPORT_TYPES))
            continue;

        // This extension is limited to save copy only.
        if (omod->savecopy_only() && save_method != Inkscape::Extension::FILE_SAVE_METHOD_SAVE_COPY)
            continue;

        Glib::ustring extension = omod->get_extension();
        addFilter(omod->get_filetypename(true), extension, omod);
        knownExtensions.insert(std::pair<Glib::ustring, Inkscape::Extension::Output*>(extension.casefold(), omod));
    }
}

/**
 * Callback for filefilter.
 */
void FileSaveDialogImplGtk::filefilterChanged()
{
    from_filefilter_changed = true;
    setExtension(filterExtensionMap[get_filter()]);
}

/**
 * Called when user types in filename entry.
 * Updates filter dropdown and extension module to match filename.
 */
void FileSaveDialogImplGtk::filenameChanged() {

    Glib::ustring filename_utf8 = get_current_name();

    // Find filename extension.
    Glib::ustring::size_type pos = filename_utf8.rfind('.');
    if ( pos == Glib::ustring::npos ) {
        // No extension.
        return;
    }
    Glib::ustring ext = filename_utf8.substr( pos ).casefold();

    if (auto output = dynamic_cast<Inkscape::Extension::Output *>(getExtension())) {
        if (Glib::ustring(output->get_extension()).casefold() == ext) {
            // Extension already set correctly.
            return;
        }
    }

    // This does not include bitmap types for which one must use the Export dialog.
    if (knownExtensions.find(ext) == knownExtensions.end()) {
        // Unknown extension. This happens when typing in a new extension.
        // std::cerr << "FileSaveDialogImplGtk::fileNameChanged: unknown extension: " << ext << std::endl;
        return;
    }

    from_filename_changed = true;
    setExtension(knownExtensions[ext]);
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
