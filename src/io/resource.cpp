// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Inkscape::IO::Resource - simple resource API
 *//*
 * Authors:
 *   MenTaLguY <mental@rydia.net>
 *   Martin Owens <doctormo@gmail.com>
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "resource.h"

#ifdef HAVE_CONFIG_H
# include "config.h"  // only include where actually required!
#endif

#ifdef _WIN32
#include <shlobj.h> // for SHGetSpecialFolderLocation
#endif

#include <sys/stat.h>

#include <glibmm/convert.h>
#include <glibmm/i18n.h>
#include <glibmm/miscutils.h>
#include <glibmm/stringutils.h>
#include <glibmm/fileutils.h>

#include "inkscape-application.h"
#include "path-prefix.h"
#include "preferences.h"
#include "sys.h"

using Inkscape::IO::file_test;

namespace Inkscape::IO::Resource {

#define INKSCAPE_PROFILE_DIR "inkscape"

gchar *_get_path(Domain domain, Type type, char const *filename, char const *extra=nullptr)
{
    if (domain == USER || domain == SHARED) {
        switch (type) {
        case ATTRIBUTES:
        case EXAMPLES:
        case DOCS:
        case SCREENS:
        case TUTORIALS:
            // Happens for example with `get_filename_string(SCREENS, ...)`
            // but we don't want a user configurable about screen.
            return nullptr;
        }
    }

    char const *name = nullptr;
    char const *sysdir = nullptr;
    char const *envor = nullptr;

    switch (domain) {
        case CREATE: {
            sysdir = "create";
            switch (type) {
                case PAINT: name = "paint"; break;
                case PALETTES: name = "swatches"; break;
                default: return nullptr;
            }
        } break;
        case CACHE: {
            g_assert(type == NONE);
            return g_build_filename(g_get_user_cache_dir(), "inkscape", filename, extra, nullptr);
        } break;

        case SYSTEM:
            sysdir = "inkscape";
        case SHARED:
        case USER: {
            switch (type) {
                case ATTRIBUTES: name = "attributes"; break;
                case DOCS: name = "doc"; break;
                case EXAMPLES: name = "examples"; break;
                case EXTENSIONS: name = "extensions"; envor = "INKSCAPE_EXTENSIONS_DIR"; break;
                case FILTERS: name = "filters"; break;
                case FONTS: name = "fonts"; break;
                case FONTCOLLECTIONS: name = "fontcollections"; break;
                case ICONS: name = "icons"; break;
                case KEYS: name = "keys"; break;
                case MARKERS: name = "markers"; break;
                case PAINT: name = "paint"; break;
                case PALETTES: name = "palettes"; break;
                case SCREENS: name = "screens"; break;
                case SYMBOLS: name = "symbols"; break;
                case TEMPLATES: name = "templates"; break;
                case THEMES: name = "themes"; break;
                case TUTORIALS: name = "tutorials"; break;
                case UIS: name = "ui"; break;
                default: g_assert_not_reached();
                         return nullptr;
            }
        } break;
    }
    // Look for an over-ride in the local environment
    if (envor && domain == USER) {
        std::string env_dir = Glib::getenv(envor);
        if (!env_dir.empty()) {
            return g_build_filename(env_dir.c_str(), filename, extra, nullptr);
        }
    }

    if (!name) {
        return nullptr;
    }

    if (sysdir) {
        return g_build_filename(get_inkscape_datadir(), sysdir, name, filename, extra, nullptr);
    } else if (domain == SHARED) {
        if (shared_path().empty()) {
            return nullptr;
        }
        return g_build_filename(shared_path().c_str(), name, filename, extra, nullptr);
    } else {
        if (profile_path().empty()) {
            return nullptr;
        }
        return g_build_filename(profile_path().c_str(), name, filename, extra, nullptr);
    }
}



Util::ptr_shared get_path(Domain domain, Type type, char const *filename, char const *extra)
{
    char *path = _get_path(domain, type, filename, extra);
    if (!path) {
        return Util::ptr_shared();
    }
    Util::ptr_shared result=Util::share_string(path);
    g_free(path);
    return result;
}

std::string get_path_string(Domain domain, Type type, char const *filename, char const *extra)
{
    std::string result;
    char *path = _get_path(domain, type, filename, extra);
    if (path) {
        result = path;
        g_free(path);
    }
    return result;
}

/*
 * Same as get_path, but checks for file's existence and falls back
 * from USER to SYSTEM modes.
 *
 *  type      - The type of file to get, such as extension, template, ui etc
 *  filename  - The filename to get, i.e. preferences.xml
 *  localized - Prefer a localized version of the file, i.e. default.de.svg instead of default.svg.
 *              (will use gettext to determine the preferred language of the user)
 *  silent    - do not warn if file doesn't exist
 * 
 */
std::string get_filename(Type type, char const *filename, bool localized, bool silent)
{
    std::string result;

    char *user_filename = nullptr;
    char *shared_filename = nullptr;
    char *sys_filename = nullptr;
    char *user_filename_localized = nullptr;
    char *sys_filename_localized = nullptr;

    // TRANSLATORS: 'en' is an ISO 639-1 language code.
    // Replace with language code for your language, i.e. the name of your .po file
    localized = localized && strcmp(_("en"), "en");

    if (localized) {
        std::string localized_filename = filename;
        localized_filename.insert(localized_filename.rfind('.'), ".");
        localized_filename.insert(localized_filename.rfind('.'), _("en"));

        user_filename_localized = _get_path(USER, type, localized_filename.c_str());
        sys_filename_localized = _get_path(SYSTEM, type, localized_filename.c_str());
    }
    user_filename = _get_path(USER, type, filename);
    shared_filename = _get_path(SHARED, type, filename);
    sys_filename = _get_path(SYSTEM, type, filename);

    // impose the following load order:
    //   USER (localized) > USER > SYSTEM (localized) > SYSTEM
    if (localized && file_test(user_filename_localized, G_FILE_TEST_EXISTS)) {
        result = user_filename_localized;
        g_info("Found localized version of resource file '%s' in profile directory:\n\t%s", filename, result.c_str());
    } else if (file_test(user_filename, G_FILE_TEST_EXISTS)) {
        result = user_filename;
        g_info("Found resource file '%s' in profile directory:\n\t%s", filename, result.c_str());
    } else if (file_test(shared_filename, G_FILE_TEST_EXISTS)) {
        result = shared_filename;
        g_info("Found resource file '%s' in profile directory:\n\t%s", filename, result.c_str());
    } else if (localized && file_test(sys_filename_localized, G_FILE_TEST_EXISTS)) {
        result = sys_filename_localized;
        g_info("Found localized version of resource file '%s' in system directory:\n\t%s", filename, result.c_str());
    } else if (file_test(sys_filename, G_FILE_TEST_EXISTS)) {
        result = sys_filename;
        g_info("Found resource file '%s' in system directory:\n\t%s", filename, result.c_str());
    } else if (!silent) {
        if (localized) {
            g_warning("Failed to find resource file '%s'. Looked in:\n\t%s\n\t%s\n\t%s\n\t%s\n\t%s",
                      filename, user_filename_localized, user_filename, shared_filename, sys_filename_localized, sys_filename);
        } else {
            g_warning("Failed to find resource file '%s'. Looked in:\n\t%s\n\t%s\n\t%s",
                      filename, user_filename, shared_filename, sys_filename);
        }
    }

    g_free(user_filename);
    g_free(shared_filename);
    g_free(sys_filename);
    g_free(user_filename_localized);
    g_free(sys_filename_localized);

    return result;
}

/*
 * Similar to get_filename, but takes a path (or filename) for relative resolution
 *
 *  path - A directory or filename that is considered local to the path resolution.
 *  filename - The filename that we are looking for.
 */
std::string get_filename(std::string const& path, std::string const& filename)
{
    // Test if it's a filename and get the parent directory instead
    if (Glib::file_test(path, Glib::FileTest::IS_REGULAR)) {
        auto dirname = Glib::path_get_dirname(path);
        g_assert(!Glib::file_test(dirname, Glib::FileTest::IS_REGULAR)); // recursion sanity check
        return get_filename(dirname, filename);
    }
    if (g_path_is_absolute(filename.c_str())) {
        if (Glib::file_test(filename, Glib::FileTest::EXISTS)) {
            return filename;
        }
    } else {
        auto ret = Glib::build_filename(path, filename);
        if (Glib::file_test(ret, Glib::FileTest::EXISTS)) {
            return ret;
        }
    }
    return {};
}

/*
 * Gets all the files in a given type, for all domain types.
 *
 *  domain - Optional domain (overload), will check return domains if not.
 *  type - The type of files, e.g. TEMPLATES
 *  extensions - A list of extensions to return, e.g. xml, svg
 *  exclusions - A list of names to exclude e.g. default.xml
 */
std::vector<std::string> get_filenames(Type type, std::vector<const char *> const &extensions, std::vector<const char *> const &exclusions)
{
    std::vector<std::string> ret;
    get_filenames_from_path(ret, get_path_string(USER, type), extensions, exclusions);
    get_filenames_from_path(ret, get_path_string(SHARED, type), extensions, exclusions);
    get_filenames_from_path(ret, get_path_string(SYSTEM, type), extensions, exclusions);
    get_filenames_from_path(ret, get_path_string(CREATE, type), extensions, exclusions);
    return ret;
}

std::vector<std::string> get_filenames(Domain domain, Type type, std::vector<const char *> const &extensions, std::vector<const char *> const &exclusions)
{
    std::vector<std::string> ret;
    get_filenames_from_path(ret, get_path_string(domain, type), extensions, exclusions);
    return ret;
}
std::vector<std::string> get_filenames(std::string path, std::vector<const char *> const &extensions, std::vector<const char *> const &exclusions)
{
    std::vector<std::string> ret;
    get_filenames_from_path(ret, Glib::filename_from_utf8(path), extensions, exclusions);
    return ret;
}

/*
 * Gets all folders inside each type, for all domain types.
 *
 *  domain - Optional domain (overload), will check return domains if not.
 *  type - The type of files, e.g. TEMPLATES
 *  extensions - A list of extensions to return, e.g. xml, svg
 *  exclusions - A list of names to exclude e.g. default.xml
 */
std::vector<std::string> get_foldernames(Type type, std::vector<const char *> const &exclusions)
{
    std::vector<std::string> ret;
    get_foldernames_from_path(ret, get_path_string(USER, type), exclusions);
    get_foldernames_from_path(ret, get_path_string(SHARED, type), exclusions);
    get_foldernames_from_path(ret, get_path_string(SYSTEM, type), exclusions);
    get_foldernames_from_path(ret, get_path_string(CREATE, type), exclusions);
    return ret;
}

std::vector<std::string> get_foldernames(Domain domain, Type type, std::vector<const char *> const &exclusions)
{
    std::vector<std::string> ret;
    get_foldernames_from_path(ret, get_path_string(domain, type), exclusions);
    return ret;
}

std::vector<std::string> get_foldernames(std::string const &path, std::vector<const char *> const &exclusions)
{
    std::vector<std::string> ret;
    get_foldernames_from_path(ret, path, exclusions);
    return ret;
}


/*
 * Get all the files from a specific path and any sub-dirs, populating &files vector
 *
 * &files - Output list to populate, will be populated with full paths
 * path - The directory to parse, will add nothing if directory doesn't exist
 * extensions - Only add files with these extensions, they must be duplicated
 * exclusions - Exclude files that exactly match these names.
 */
void get_filenames_from_path(std::vector<std::string> &files, std::string const &path,
                             std::vector<const char *> const &extensions, std::vector<const char *> const &exclusions)
{
    if(!Glib::file_test(path, Glib::FileTest::IS_DIR)) {
        return;
    }

    Glib::Dir dir(path);
    std::string file = dir.read_name();
    while (!file.empty()){
        // If not extensions are specified, don't reject ANY files.
        bool reject = !extensions.empty();

        // Unreject any file which has one of the extensions.
        for (auto &ext: extensions) {
	    reject ^= Glib::str_has_suffix(file, ext);
        }

        // Reject any file which matches the exclusions.
        for (auto &exc: exclusions) {
	    reject |= Glib::str_has_prefix(file, exc);
        }

        // Reject any filename which isn't a regular file
        auto filename = Glib::build_filename(path, file);

        if(Glib::file_test(filename, Glib::FileTest::IS_DIR)) {
            get_filenames_from_path(files, filename, extensions, exclusions);
        } else if(Glib::file_test(filename, Glib::FileTest::IS_REGULAR) && !reject) {
            files.push_back(Glib::filename_to_utf8(filename));
        }
        file = dir.read_name();
    }
}

/*
 * Get all the files from a specific path and any sub-dirs, populating &files vector
 *
 * &folders - Output list to populate, will be poulated with full paths
 * path - The directory to parse, will add nothing if directory doesn't exist
 * exclusions - Exclude files that exactly match these names.
 */
void get_foldernames_from_path(std::vector<std::string> &folders, std::string const &path,
                               std::vector<const char *> const &exclusions)
{
    if (!Glib::file_test(path, Glib::FileTest::IS_DIR)) {
        return;
    }

    Glib::Dir dir(path);
    std::string file = dir.read_name();
    while (!file.empty()) {
        // If not extensions are specified, don't reject ANY files.
        bool reject = false;

        // Reject any file which matches the exclusions.
        for (auto &exc : exclusions) {
            reject |= Glib::str_has_prefix(file, exc);
        }

        // Reject any filename which isn't a regular file
        auto filename = Glib::build_filename(path, file);

        if (Glib::file_test(filename, Glib::FileTest::IS_DIR) && !reject) {
            folders.push_back(Glib::filename_to_utf8(filename));
        }
        file = dir.read_name();
    }
}


/**
 * Get, or guess, or decide the location where the preferences.xml
 * file should be located. This also indicates where all other inkscape
 * shared files may optionally exist.
 */
std::string profile_path(const char *filename)
{
    if (profile_path().empty()) {
        return std::string("");
    }
    return Glib::build_filename(profile_path(), filename);
}

std::string profile_path()
{
    
    static std::string prefdir = "";

    if (prefdir.empty()) {
        // Check if profile directory is overridden using environment variable
        prefdir = Glib::getenv("INKSCAPE_PROFILE_DIR");
#ifdef _WIN32
        // prefer c:\Documents and Settings\UserName\Application Data\ to c:\Documents and Settings\userName\;
        // TODO: CSIDL_APPDATA is C:\Users\UserName\AppData\Roaming these days
        //       should we migrate to AppData\Local? Then we can simply use the portable g_get_user_config_dir()
        if (prefdir.empty()) {
            ITEMIDLIST *pidl = 0;
            if ( SHGetFolderLocation( NULL, CSIDL_APPDATA, NULL, 0, &pidl ) == S_OK ) {
                gchar * utf8Path = NULL;

                {
                    wchar_t pathBuf[MAX_PATH+1];
                    g_assert(sizeof(wchar_t) == sizeof(gunichar2));

                    if ( SHGetPathFromIDListW( pidl, pathBuf ) ) {
                        utf8Path = g_utf16_to_utf8( (gunichar2*)(&pathBuf[0]), -1, NULL, NULL, NULL );
                    }
                }

                if ( utf8Path ) {
                    if (!g_utf8_validate(utf8Path, -1, NULL)) {
                        g_warning( "SHGetPathFromIDListW() resulted in invalid UTF-8");
                        g_free( utf8Path );
                        utf8Path = 0;
                    } else {
                        prefdir = utf8Path;
                    }
                }
            }

            if (!prefdir.empty()) {
                prefdir = Glib::build_filename(prefdir, INKSCAPE_PROFILE_DIR);
            }
        }
#endif
        if (prefdir.empty()) {
            prefdir =  Glib::build_filename(get_user_config_dir(), INKSCAPE_PROFILE_DIR);
            // In case the XDG user config dir of the moment does not yet exist...
            int mode = S_IRWXU;
#ifdef S_IRGRP
            mode |= S_IRGRP;
#endif
#ifdef S_IXGRP
            mode |= S_IXGRP;
#endif
#ifdef S_IXOTH
            mode |= S_IXOTH;
#endif
            if (g_mkdir_with_parents(prefdir.c_str(), mode) == -1 ) {
                int problem = errno;
                g_warning("Unable to create profile directory (%s) (%d)", g_strerror(problem), problem);
            } else {
                gchar const *userDirs[] = { "keys", "templates", "icons", "extensions", "ui",
                                            "symbols", "paint", "themes", "palettes", "fontcollections", nullptr };
                for (gchar const** name = userDirs; *name; ++name) {
                    gchar *dir = g_build_filename(prefdir.c_str(), *name, nullptr);
                    g_mkdir_with_parents(dir, mode);
                    g_free(dir);
                }
            }
        }
    }
    return prefdir;
}

std::string shared_path(const char *filename)
{
    return shared_path().empty() ? shared_path() : Glib::build_filename(shared_path(), filename);
}

std::string shared_path()
{
    if (InkscapeApplication::instance()) {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        std::string shared_dir = prefs->getString("/options/resources/sharedpath");
        if (!shared_dir.empty() && Glib::file_test(shared_dir, Glib::FileTest::IS_DIR)) {
            return shared_dir;
        }
    }
    return std::string("");
}

/*
 * We return the profile_path because that is where most documentation
 * days log files will be generated in inkscape 0.92
 */
std::string log_path(const char *filename)
{
    return profile_path(filename);
}

std::string homedir_path()
{
    return g_get_home_dir();
}

} // namespace Inkscape::IO::Resource

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
