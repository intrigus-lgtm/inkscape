// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * path-prefix.cpp - Inkscape specific prefix handling *//*
 * Authors:
 *   Patrick Storz <eduard.braun2@gmx.de>
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"  // only include where actually required!
#endif

#ifdef _WIN32
#include <windows.h> // for GetModuleFileNameW
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h> // for _NSGetExecutablePath
#endif

#ifdef __NetBSD__
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#ifdef __FreeBSD__
#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#include <cassert>
#include <glib.h>
#include <glibmm.h>

#include "path-prefix.h"

/**
 * Guess the absolute path of the application bundle prefix directory.
 * The result path is not guaranteed to exist.
 *
 * On Windows, the result should be identical to
 * g_win32_get_package_installation_directory_of_module(nullptr)
 */
static std::string _get_bundle_prefix_dir()
{
    char const *program_dir = get_program_dir();
    auto prefix = Glib::path_get_dirname(program_dir);

    if (g_str_has_suffix(program_dir, "Contents/MacOS")) {
        // macOS
        prefix += "/Resources";
    } else if (Glib::path_get_basename(program_dir) == "bin") {
        // Windows, Linux
    } else if (Glib::path_get_basename(prefix) == "lib") {
        // AppImage
        // program_dir=appdir/lib/x86_64-linux-gnu
        // prefix_dir=appdir/usr
        prefix = Glib::build_filename(Glib::path_get_dirname(prefix), "usr");
    }

    return prefix;
}

/**
 * Determine the location of the Inkscape data directory (typically the share/ folder
 * from where Inkscape should be loading resources).
 *
 * The data directory is the first of:
 *
 * - Environment variable $INKSCAPE_DATADIR if not empty
 * - If a bundle is detected: "<bundle-prefix>/share"
 * - Compile time value of INKSCAPE_DATADIR
 */
char const *get_inkscape_datadir()
{
    static char const *inkscape_datadir = nullptr;
    if (!inkscape_datadir) {
        static std::string datadir = Glib::getenv("INKSCAPE_DATADIR");

        if (datadir.empty()) {
            datadir = Glib::build_filename(_get_bundle_prefix_dir(), "share");

            if (!Glib::file_test(Glib::build_filename(datadir, "inkscape"), Glib::FileTest::IS_DIR)) {
                datadir = INKSCAPE_DATADIR;
            }
        }

        inkscape_datadir = datadir.c_str();

#if GLIB_CHECK_VERSION(2,58,0)
        inkscape_datadir = g_canonicalize_filename(inkscape_datadir, nullptr);
#endif
    }

    return inkscape_datadir;
}

/**
 * Sets environment variables for a relocatable application bundle.
 *
 * Only does work on the first call, follow-up calls immediately return.
 *
 * Only sets environment variables if this actually looks like a relocatable bundle.
 *
 * Currently only handles macOS. Windows and Linux (AppImage) use alternative solutions.
 */
void set_xdg_env()
{
    static bool ready = false;
    if (ready)
        return;
    ready = true;
#ifdef __APPLE__
    // use bundle identifier
    // https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/FileSystemProgrammingGuide/MacOSXDirectories/MacOSXDirectories.html
    auto app_support_dir = Glib::getenv("HOME") + "/Library/Application Support/org.inkscape.Inkscape";

    auto bundle_resources_dir       = Glib::path_get_dirname(get_inkscape_datadir());
    auto bundle_resources_etc_dir   = bundle_resources_dir + "/etc";
    auto bundle_resources_bin_dir   = bundle_resources_dir + "/bin";
    auto bundle_resources_lib_dir   = bundle_resources_dir + "/lib";
    auto bundle_resources_share_dir = bundle_resources_dir + "/share";

    // failsafe: Check if the expected content is really there, using GIO modules
    // as an indicator.
    // This is also helpful to developers as it enables the possibility to
    //      1. cmake -DCMAKE_INSTALL_PREFIX=Inkscape.app/Contents/Resources
    //      2. move binary to Inkscape.app/Contents/MacOS and set rpath
    //      3. copy Info.plist
    // to ease up on testing and get correct application behavior (like dock icon).
    if (!Glib::file_test(bundle_resources_lib_dir + "/gio/modules", Glib::FileTest::EXISTS)) {
        // doesn't look like a standalone bundle
        return;
    }

    // XDG
    // https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
    Glib::setenv("XDG_DATA_HOME",   app_support_dir + "/share");
    Glib::setenv("XDG_DATA_DIRS",   bundle_resources_share_dir);
    Glib::setenv("XDG_CONFIG_HOME", app_support_dir + "/config");
    Glib::setenv("XDG_CONFIG_DIRS", bundle_resources_etc_dir + "/xdg");
    Glib::setenv("XDG_CACHE_HOME",  app_support_dir + "/cache");

    // GdkPixbuf
    // https://gitlab.gnome.org/GNOME/gdk-pixbuf
    Glib::setenv("GDK_PIXBUF_MODULE_FILE", bundle_resources_lib_dir + "/gdk-pixbuf-2.0/2.10.0/loaders.cache");

    // fontconfig
    Glib::setenv("FONTCONFIG_PATH", bundle_resources_etc_dir + "/fonts");

    // GIO
    Glib::setenv("GIO_MODULE_DIR", bundle_resources_lib_dir + "/gio/modules");

    // GObject Introspection
    Glib::setenv("GI_TYPELIB_PATH", bundle_resources_lib_dir + "/girepository-1.0");

    // libenchant (patched)
    // https://gitlab.com/inkscape/devel/mibap/-/blob/f71d24a/modulesets/gtk-osx-random.modules#L138
    Glib::setenv("ENCHANT_PREFIX", bundle_resources_dir);

    // PATH
    Glib::setenv("PATH", bundle_resources_bin_dir + ":" + Glib::getenv("PATH"));

    // DYLD_LIBRARY_PATH
    // This is required to make Python GTK bindings work as they use dlopen()
    // to load libraries.
    Glib::setenv("DYLD_LIBRARY_PATH", bundle_resources_lib_dir + ":"
            + bundle_resources_lib_dir + "/gdk-pixbuf-2.0/2.10.0/loaders");
#endif
}


/**
 * Get the user configuration directory.
 */
const char *get_user_config_dir()
{
    set_xdg_env();
    return g_get_user_config_dir();
}

/**
 * Gets the the currently running program's executable name (including full path)
 *
 * @return executable name (including full path) encoded as UTF-8
 *         or NULL if it can't be determined
 */
char const *get_program_name()
{
    static gchar *program_name = NULL;

    if (program_name == NULL) {
        // There is no portable way to get an executable's name including path, so we need to do it manually.
        // TODO: Re-evaluate boost::dll::program_location() once we require Boost >= 1.61
        //
        // The following platform-specific code is partially based on GdxPixbuf's get_toplevel()
        // See also https://stackoverflow.com/a/1024937
#if defined(_WIN32)
        wchar_t module_file_name[MAX_PATH];
        if (GetModuleFileNameW(NULL, module_file_name, MAX_PATH)) {
            program_name = g_utf16_to_utf8((gunichar2 *)module_file_name, -1, NULL, NULL, NULL);
        } else {
            g_warning("get_program_name() - GetModuleFileNameW failed");
        }
#elif defined(__APPLE__)
        char pathbuf[PATH_MAX + 1];
        uint32_t bufsize = sizeof(pathbuf);
        if (_NSGetExecutablePath(pathbuf, &bufsize) == 0) {
            program_name = realpath(pathbuf, nullptr);
        } else {
            g_warning("get_program_name() - _NSGetExecutablePath failed");
        }
#elif defined(__linux__) || defined(__CYGWIN__)
        program_name = g_file_read_link("/proc/self/exe", NULL);
        if (!program_name) {
            g_warning("get_program_name() - g_file_read_link failed");
        }
#elif defined(__NetBSD__)
        static const int name[] = {CTL_KERN, KERN_PROC_ARGS, -1, KERN_PROC_PATHNAME};
        char path[MAXPATHLEN];
        size_t len = sizeof(path);
        if (sysctl(name, __arraycount(name), path, &len, NULL, 0) == 0) {
            program_name = realpath(path, nullptr);
        } else {
            g_warning("get_program_name() - sysctl failed");
        }
#elif defined(__FreeBSD__)
       int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
       char buf[MAXPATHLEN];
       size_t cb = sizeof(buf);
       if (sysctl(mib, 4, buf, &cb, NULL, 0) == 0) {
           program_name = realpath(buf, nullptr);
       } else {
           g_warning("get_program_name() - sysctl failed");
       }      
#else
#warning get_program_name() - no known way to obtain executable name on this platform
        g_info("get_program_name() - no known way to obtain executable name on this platform");
#endif
    }

    return program_name;
}

/**
 * Gets the the full path to the directory containing the currently running program's executable
 *
 * @return full path to directory encoded in native encoding,
 *         or NULL if it can't be determined
 */
char const *get_program_dir()
{
    static char *program_dir = g_path_get_dirname(get_program_name());
    return program_dir;
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
