// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * TODO: insert short description here
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
/*
 * BinReloc - a library for creating relocatable executables
 * Written by: Mike Hearn <mike@theoretic.com>
 *             Hongli Lai <h.lai@chello.nl>
 * http://autopackage.org/
 *
 * This source code is public domain. You can relicense this code
 * under whatever license you want.
 *
 * NOTE: if you're using C++ and are getting "undefined reference
 * to br_*", try renaming prefix.c to prefix.cpp
 */

/* WARNING, BEFORE YOU MODIFY PREFIX.C:
 *
 * If you make changes to any of the functions in prefix.c, you MUST
 * change the BR_NAMESPACE macro (in prefix.h).
 * This way you can avoid symbol table conflicts with other libraries
 * that also happen to use BinReloc.
 *
 * Example:
 * #define BR_NAMESPACE(funcName) foobar_ ## funcName
 * --> expands br_locate to foobar_br_locate
 */

#ifdef HAVE_CONFIG_H
# include "config.h"  // only include where actually required!
#endif

#ifndef _PREFIX_C_
#define _PREFIX_C_

#include <glib.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include "prefix.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#ifdef __GNUC__
    #define br_return_val_if_fail(expr,val) if (!(expr)) {fprintf (stderr, "** BinReloc (%s): assertion %s failed\n", __PRETTY_FUNCTION__, #expr); return val;}
#else
    #define br_return_val_if_fail(expr,val) if (!(expr)) return val
#endif /* __GNUC__ */


#ifdef ENABLE_BINRELOC

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <unistd.h>

/**
 * br_locate:
 * symbol: A symbol that belongs to the app/library you want to locate.
 * Returns: A newly allocated string containing the full path of the
 *        app/library that func belongs to, or NULL on error. This
 *        string should be freed when not when no longer needed.
 *
 * Finds out to which application or library symbol belongs, then locate
 * the full path of that application or library.
 * Note that symbol cannot be a pointer to a function. That will not work.
 *
 * Example:
 * --> main.c
 * #include "prefix.h"
 * #include "libfoo.h"
 *
 * int main (int argc, char *argv[]) {
 *    printf ("Full path of this app: %s\n", br_locate (&argc));
 *    libfoo_start ();
 *    return 0;
 * }
 *
 * --> libfoo.c starts here
 * #include "prefix.h"
 *
 * void libfoo_start () {
 *    --> "" is a symbol that belongs to libfoo (because it's called
 *    --> from libfoo_start()); that's why this works.
 *    printf ("libfoo is located in: %s\n", br_locate (""));
 * }
 */
char *
br_locate (void *symbol)
{
    char line[5000];
    FILE *f;
    char *path;

    br_return_val_if_fail (symbol != NULL, NULL);

    f = fopen ("/proc/self/maps", "r");
    if (!f)
        return NULL;

    while (!feof (f))
    {
        unsigned long start, end;

        if (!fgets (line, sizeof (line), f))
            continue;

        int inode = 0;
        if (sscanf(line, "%lx-%lx r%*s %*x %*s %d", &start, &end, &inode) != 3 || inode == 0)
            continue;

        if (symbol >= (void *) start && symbol < (void *) end)
        {
            char *tmp;
            size_t len;

            /* Extract the filename; it is always an absolute path */
            path = strchr (line, '/');
            if (!path)
                continue;

            /* Get rid of the newline */
            tmp = strrchr (path, '\n');
            if (tmp) *tmp = 0;

            /* Get rid of "(deleted)" */
            len = strlen (path);
            if (len > 10 && strcmp (path + len - 10, " (deleted)") == 0)
            {
                tmp = path + len - 10;
                *tmp = 0;
            }

            fclose(f);
            return strdup (path);
        }
    }

    fclose (f);
    return NULL;
}


/**
 * br_locate_prefix:
 * symbol: A symbol that belongs to the app/library you want to locate.
 * Returns: A prefix. This string should be freed when no longer needed.
 *
 * Locates the full path of the app/library that symbol belongs to, and return
 * the prefix of that path, or NULL on error.
 * Note that symbol cannot be a pointer to a function. That will not work.
 *
 * Example:
 * --> This application is located in /usr/bin/foo
 * br_locate_prefix (&argc);   --> returns: "/usr"
 */
char *
br_locate_prefix (void *symbol)
{
    char *path, *prefix;

    br_return_val_if_fail (symbol != NULL, NULL);

    path = br_locate (symbol);
    if (!path) return NULL;

    prefix = br_extract_prefix (path);
    free (path);
    return prefix;
}


/**
 * br_prepend_prefix:
 * symbol: A symbol that belongs to the app/library you want to locate.
 * path: The path that you want to prepend the prefix to.
 * Returns: The new path, or NULL on error. This string should be freed when no
 *        longer needed.
 *
 * Gets the prefix of the app/library that symbol belongs to. Prepend that prefix to path.
 * Note that symbol cannot be a pointer to a function. That will not work.
 *
 * Example:
 * --> The application is /usr/bin/foo
 * br_prepend_prefix (&argc, "/share/foo/data.png");   --> Returns "/usr/share/foo/data.png"
 */
char *
br_prepend_prefix (void *symbol, char const *path)
{
    char *tmp, *newpath;

    br_return_val_if_fail (symbol != NULL, NULL);
    br_return_val_if_fail (path != NULL, NULL);

    tmp = br_locate_prefix (symbol);
    if (!tmp) return NULL;

    if (strcmp (tmp, "/") == 0)
        newpath = strdup (path);
    else
        newpath = br_strcat (tmp, path);

    /* Get rid of compiler warning ("br_prepend_prefix never used") */
    if (0) br_prepend_prefix (NULL, NULL);

    free (tmp);
    return newpath;
}

#endif /* ENABLE_BINRELOC */


/* Thread stuff for thread safetiness */
#if BR_THREADS

GPrivate* br_thread_key = (GPrivate *)NULL;

/*
   We do not need local store init() or fini(), because
   g_private_new (g_free) will take care of all of that
   for us.  Isn't GLib wonderful?
*/

#else /* !BR_THREADS */

static char *br_last_value = (char*)nullptr;

static void
br_free_last_value ()
{
    if (br_last_value)
        free (br_last_value);
}

#endif /* BR_THREADS */


/**
 * br_thread_local_store:
 * str: A dynamically allocated string.
 * Returns: str. This return value must not be freed.
 *
 * Store str in a thread-local variable and return str. The next
 * you run this function, that variable is freed too.
 * This function is created so you don't have to worry about freeing
 * strings.
 *
 * Example:
 * char *foo;
 * foo = thread_local_store (strdup ("hello")); --> foo == "hello"
 * foo = thread_local_store (strdup ("world")); --> foo == "world"; "hello" is now freed.
 */
const char *
br_thread_local_store (char *str)
{
    #if BR_THREADS
                if (!g_thread_supported ())
                    {
                    g_thread_init ((GThreadFunctions *)NULL);
                    br_thread_key = g_private_new (g_free);
                    }

        char *specific = (char *) g_private_get (br_thread_key);
        if (specific)
                    free (specific);
                g_private_set (br_thread_key, str);

    #else /* !BR_THREADS */
        static int initialized = 0;

        if (!initialized)
        {
            atexit (br_free_last_value);
            initialized = 1;
        }

        if (br_last_value)
            free (br_last_value);
        br_last_value = str;
    #endif /* BR_THREADS */

    return (const char *) str;
}


/**
 * br_strcat:
 * str1: A string.
 * str2: Another string.
 * Returns: A newly-allocated string. This string should be freed when no longer needed.
 *
 * Concatenate str1 and str2 to a newly allocated string.
 */
char *
br_strcat (const char *str1, const char *str2)
{
    char *result;
    size_t len1, len2;

    if (!str1) str1 = "";
    if (!str2) str2 = "";

    len1 = strlen (str1);
    len2 = strlen (str2);

    result = (char *) malloc (len1 + len2 + 1);
    memcpy (result, str1, len1);
    memcpy (result + len1, str2, len2);
    result[len1 + len2] = '\0';

    return result;
}


/* Emulates glibc's strndup() */
static char *
br_strndup (char *str, size_t size)
{
    char *result = (char*)nullptr;
    size_t len;

    br_return_val_if_fail (str != (char*)nullptr, (char*)nullptr);

    len = strlen (str);
    if (!len) return strdup ("");
    if (size > len) size = len;

    result = (char *) calloc (sizeof (char), len + 1);
    memcpy (result, str, size);
    return result;
}


/**
 * br_extract_dir:
 * path: A path.
 * Returns: A directory name. This string should be freed when no longer needed.
 *
 * Extracts the directory component of path. Similar to g_path_get_dirname()
 * or the dirname commandline application.
 *
 * Example:
 * br_extract_dir ("/usr/local/foobar");  --> Returns: "/usr/local"
 */
char *
br_extract_dir (const char *path)
{
    const char *end;
    char *result;

    br_return_val_if_fail (path != (char*)nullptr, (char*)nullptr);

    end = strrchr (path, '/');
    if (!end) return strdup (".");

    while (end > path && *end == '/')
        end--;
    result = br_strndup ((char *) path, end - path + 1);
    if (!*result)
    {
        free (result);
        return strdup ("/");
    } else
        return result;
}


/**
 * br_extract_prefix:
 * path: The full path of an executable or library.
 * Returns: The prefix, or NULL on error. This string should be freed when no longer needed.
 *
 * Extracts the prefix from path. This function assumes that your executable
 * or library is installed in an LSB-compatible directory structure.
 *
 * Example:
 * br_extract_prefix ("/usr/bin/gnome-panel");       --> Returns "/usr"
 * br_extract_prefix ("/usr/local/lib/libfoo.so");   --> Returns "/usr/local"
 * br_extract_prefix ("/usr/local/libfoo.so");       --> Returns "/usr"
 */
char *
br_extract_prefix (const char *path)
{
    const char *end;
    char *tmp, *result;

    br_return_val_if_fail (path != (char*)nullptr, (char*)nullptr);

    if (!*path) return strdup ("/");
    end = strrchr (path, '/');
    if (!end) return strdup (path);

    tmp = br_strndup ((char *) path, end - path);
    if (!*tmp)
    {
        free (tmp);
        return strdup ("/");
    }
    end = strrchr (tmp, '/');
    if (!end) return tmp;

    result = br_strndup (tmp, end - tmp);
    free (tmp);

    if (!*result)
    {
        free (result);
        result = strdup ("/");
    }

    return result;
}


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _PREFIX_C */