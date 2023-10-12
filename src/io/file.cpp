// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * File operations (independent of GUI)
 *
 * Copyright (C) 2018, 2019 Tavmjong Bah
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 *
 */

#include "io/file.h"

#include <initializer_list>
#include <iostream>
#include <memory>
#include <unistd.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <giomm/file.h>

#include "document.h"
#include "document-undo.h"
#include "extension/system.h"     // Extension::open()
#include "extension/extension.h"
#include "extension/db.h"
#include "extension/output.h"
#include "extension/input.h"
#include "object/sp-root.h"
#include "xml/repr.h"

/**
 * Create a blank document, remove any template data.
 * Input: Empty string or template file name.
 */
SPDocument *
ink_file_new(const std::string &Template)
{
    SPDocument *doc = SPDocument::createNewDoc ((Template.empty() ? nullptr : Template.c_str()), true, true );

    if (!doc) {
        std::cerr << "ink_file_new: Did not create new document!" << std::endl;
        return nullptr;
    }

    // Remove all the template info from xml tree
    Inkscape::XML::Node *myRoot = doc->getReprRoot();
    for (auto const name: {"inkscape:templateinfo",
                           "inkscape:_templateinfo"}) // backwards-compatibility
    {
        if (auto node = std::unique_ptr<Inkscape::XML::Node>{sp_repr_lookup_name(myRoot, name)}) {
            Inkscape::DocumentUndo::ScopedInsensitive no_undo(doc);
            sp_repr_unparent(node.get());
        }
    }

    return doc;
}

/**
 * Open a document from memory.
 */
SPDocument *
ink_file_open(std::string const &data)
{
    SPDocument *doc = SPDocument::createNewDocFromMem (data.c_str(), data.length(), true);

    if (doc == nullptr) {
        std::cerr << "ink_file_open: cannot open file in memory (pipe?)" << std::endl;
        return nullptr;
    }

    // This is the only place original values should be set.
    SPRoot *root = doc->getRoot();
    root->original.inkscape = root->version.inkscape;
    root->original.svg      = root->version.svg;
    return doc;
}

/**
 * Open a document.
 */
SPDocument *
ink_file_open(const Glib::RefPtr<Gio::File>& file, bool *cancelled_param)
{
    bool cancelled = false;
    SPDocument *doc = nullptr;
    std::string path = file->get_path();

    // TODO: It's useless to catch these exceptions here (and below) unless we do something with them.
    //       If we can't properly handle them (e.g. by showing a user-visible message) don't catch them!
    // TODO: Why do we reset `doc` to nullptr? Surely if open() throws, we will never assign to it?
    try {
        doc = Inkscape::Extension::open(nullptr, path.c_str());
    } catch (Inkscape::Extension::Input::no_extension_found &e) {
        doc = nullptr;
    } catch (Inkscape::Extension::Input::open_failed &e) {
        doc = nullptr;
    } catch (Inkscape::Extension::Input::open_cancelled &e) {
        cancelled = true;
        doc = nullptr;
    }

    // Try to open explicitly as SVG.
    // TODO: Why is this necessary? Shouldn't this be handled by the first call already?
    if (doc == nullptr && !cancelled) {
        try {
            doc = Inkscape::Extension::open(Inkscape::Extension::db.get(SP_MODULE_KEY_INPUT_SVG), path.c_str());
        } catch (Inkscape::Extension::Input::no_extension_found &e) {
            doc = nullptr;
        } catch (Inkscape::Extension::Input::open_failed &e) {
            doc = nullptr;
        } catch (Inkscape::Extension::Input::open_cancelled &e) {
            cancelled = true;
            doc = nullptr;
        }
    }

    if (doc != nullptr) {
        // This is the only place original values should be set.
        SPRoot *root = doc->getRoot();
        root->original.inkscape = root->version.inkscape;
        root->original.svg      = root->version.svg;
    } else if (!cancelled) {
        std::cerr << "ink_file_open: '" << path << "' cannot be opened!" << std::endl;
    }

    if (cancelled_param) {
        *cancelled_param = cancelled;
    }

    return doc;
}

namespace Inkscape::IO {

/**
 * Create a temporary filename, which is closed and deleted when deconstructed.
 */
TempFilename::TempFilename(const std::string &pattern)
{
    try {
        _tempfd = Glib::file_open_tmp(_filename, pattern.c_str());
    } catch (...) {
        /// \todo Popup dialog here
    }
}
  
TempFilename::~TempFilename()
{
    close(_tempfd);
    unlink(_filename.c_str());
}

/**
 * Takes an absolute file path and returns a second file at the same
 * directory location, if and only if the filename exists and is a file.
 *
 * Returns the empty string if the new file is not found.
 */
std::string find_original_file(Glib::StdStringView const filepath, Glib::StdStringView const name)
{
    auto path = Glib::path_get_dirname(filepath);
    auto filename = Glib::build_filename(path, name);
    if (Glib::file_test(filename, Glib::FileTest::IS_REGULAR)) {
        return filename;
    }
    return ""; 
}

} // namespace Inkscape::IO

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
