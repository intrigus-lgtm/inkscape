// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Interface to main application.
 */
/* Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Liam P. White <inkscapebrony@gmail.com>
 *
 * Copyright (C) 1999-2014 authors
 * c++ port Copyright (C) 2003 Nathan Hurst
 * c++ification Copyright (C) 2014 Liam P. White
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <unistd.h>

#include <fstream>
#include <map>
#define NOGDI
#define _NO_W32_PSEUDO_MODIFIERS
#include <boost/stacktrace.hpp>
#undef near
#undef IGNORE
#include <glibmm/regex.h>
#include <glibmm/i18n.h>
#include <glibmm/miscutils.h>
#include <glibmm/convert.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/label.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/recentmanager.h>
#include <gtkmm/textbuffer.h>

#include "desktop.h"
#include "document.h"
#include "inkscape.h"
#include "inkscape-application.h"
#include "inkscape-version-info.h"
#include "inkscape-window.h"
#include "message-stack.h"
#include "path-prefix.h"
#include "selection.h"

#include "color/cms-system.h"
#include "debug/simple-event.h"
#include "debug/event-tracker.h"
#include "io/resource.h"
#include "io/sys.h"
#include "libnrtype/font-factory.h"
#include "object/sp-item-group.h"
#include "object/sp-root.h"
#include "io/resource.h"
#include "ui/builder-utils.h"
#include "ui/themes.h"
#include "ui/dialog-events.h"
#include "ui/dialog-run.h"
#include "ui/dialog/dialog-manager.h"
#include "ui/dialog/dialog-window.h"
#include "ui/themes.h"
#include "ui/tools/tool-base.h"
#include "ui/util.h"

// Inkscape::Application static members
Inkscape::Application * Inkscape::Application::_S_inst = nullptr;
bool Inkscape::Application::_crashIsHappening = false;

#define DESKTOP_IS_ACTIVE(d) (INKSCAPE._desktops != nullptr && !INKSCAPE._desktops->empty() && ((d) == INKSCAPE._desktops->front()))

static void (* segv_handler) (int) = SIG_DFL;
static void (* abrt_handler) (int) = SIG_DFL;
static void (* fpe_handler)  (int) = SIG_DFL;
static void (* ill_handler)  (int) = SIG_DFL;
#ifndef _WIN32
static void (* bus_handler)  (int) = SIG_DFL;
#endif

static constexpr int SP_INDENT = 8;

/**  C++ification TODO list
 * - _S_inst should NOT need to be assigned inside the constructor, but if it isn't the Filters+Extensions menus break.
 * - Application::_deskops has to be a pointer because of a signal bug somewhere else. Basically, it will attempt to access a deleted object in sp_ui_close_all(),
 *   but if it's a pointer we can stop and return NULL in Application::active_desktop()
 * - These functions are calling Application::create for no good reason I can determine:
 *
 *   Inkscape::UI::Dialog::SVGPreview::SVGPreview()
 *       src/ui/dialog/filedialogimpl-gtkmm.cpp:542:9
 */


class InkErrorHandler : public Inkscape::ErrorReporter {
public:
    InkErrorHandler(bool useGui) : Inkscape::ErrorReporter(),
                                   _useGui(useGui)
    {}
    ~InkErrorHandler() override = default;

    void handleError( Glib::ustring const& primary, Glib::ustring const& secondary ) const override
    {
        if (_useGui) {
            Gtk::MessageDialog err(primary, false, Gtk::MessageType::WARNING, Gtk::ButtonsType::OK, true);
            err.set_secondary_text(secondary);
            Inkscape::UI::dialog_run(err);
        } else {
            g_message("%s", primary.data());
            g_message("%s", secondary.data());
        }
    }

private:
    bool _useGui;
};

void inkscape_ref(Inkscape::Application & in)
{
    in.refCount++;
}

void inkscape_unref(Inkscape::Application & in)
{
    in.refCount--;

    if (&in == Inkscape::Application::_S_inst) {
        if (in.refCount <= 0) {
            delete Inkscape::Application::_S_inst;
        }
    } else {
        g_error("Attempt to unref an Application (=%p) not the current instance (=%p) (maybe it's already been destroyed?)",
                &in, Inkscape::Application::_S_inst);
    }
}

namespace Inkscape {

/**
 * Defined only for debugging purposes. If we are certain the bugs are gone we can remove this
 * and the references in inkscape_ref and inkscape_unref.
 */
Application*
Application::operator &() const
{
    return const_cast<Application*>(this);
}
/**
 *  Creates a new Inkscape::Application global object.
 */
void
Application::create(bool use_gui)
{
   if (!Application::exists()) {
        new Application(use_gui);
    } else {
       // g_assert_not_reached();  Can happen with InkscapeApplication
    }
}


/**
 *  Checks whether the current Inkscape::Application global object exists.
 */
bool
Application::exists()
{
    return Application::_S_inst != nullptr;
}

/**
 *  Returns the current Inkscape::Application global object.
 *  \pre Application::_S_inst != NULL
 */
Application&
Application::instance()
{
    if (!exists()) {
         g_error("Inkscape::Application does not yet exist.");
    }
    return *Application::_S_inst;
}

/* \brief Constructor for the application.
 *  Creates a new Inkscape::Application.
 *
 *  \pre Application::_S_inst == NULL
 */

Application::Application(bool use_gui) :
    _use_gui(use_gui)
{
    using namespace Inkscape::IO::Resource;
    /* fixme: load application defaults */

    // we need a app runing to know shared path
    auto extensiondir_shared = get_path_string(SHARED, EXTENSIONS);
    if (!extensiondir_shared.empty()) {
        std::string pythonpath = extensiondir_shared;
        auto pythonpath_old = Glib::getenv("PYTHONPATH");
        if (!pythonpath_old.empty()) {
            pythonpath += G_SEARCHPATH_SEPARATOR + pythonpath_old;
        }
        Glib::setenv("PYTHONPATH", pythonpath);
    }
    segv_handler = signal (SIGSEGV, Application::crash_handler);
    abrt_handler = signal (SIGABRT, Application::crash_handler);
    fpe_handler  = signal (SIGFPE,  Application::crash_handler);
    ill_handler  = signal (SIGILL,  Application::crash_handler);
#ifndef _WIN32
    bus_handler  = signal (SIGBUS,  Application::crash_handler);
#endif

    // \TODO: this belongs to Application::init but if it isn't here
    // then the Filters and Extensions menus don't work.
    _S_inst = this;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    InkErrorHandler* handler = new InkErrorHandler(use_gui);
    prefs->setErrorHandler(handler);
    {
        Glib::ustring msg;
        Glib::ustring secondary;
        if (prefs->getLastError( msg, secondary )) {
            handler->handleError(msg, secondary);
        }
    }

    if (use_gui) {
        using namespace Inkscape::IO::Resource;

        auto display = Gdk::Display::get_default();
        auto icon_theme = Gtk::IconTheme::get_for_display(display);
        // Fixme: Previously prepend_search_path() in the reverse order.
        icon_theme->add_search_path(get_path_string(USER, ICONS));
        icon_theme->add_search_path(get_path_string(SHARED, ICONS));
        icon_theme->add_search_path(get_path_string(SYSTEM, ICONS));

        themecontext = new Inkscape::UI::ThemeContext();
        themecontext->add_gtk_css(false);
        auto scale = prefs->getDoubleLimited(UI::ThemeContext::get_font_scale_pref_path(), 100, 50, 150);
        themecontext->adjustGlobalFontScale(scale / 100.0);
        Inkscape::UI::ThemeContext::initialize_source_syntax_styles();
    }

    /* set language for user interface according setting in preferences */
    Glib::ustring ui_language = prefs->getString("/ui/language");
    if(!ui_language.empty())
    {
        Glib::setenv("LANGUAGE", ui_language.raw(), true);
#ifdef _WIN32
        // locale may be set to C with some Windows Region Formats (like English(Europe)).
        // forcing the LANGUAGE variable to be ignored
        // see :guess_category_value:gettext-runtime/intl/dcigettext.c,
        // and :gl_locale_name_from_win32_LANGID:gettext-runtime/gnulib-lib/localename.c
        Glib::setenv("LANG", ui_language.raw(), true);
#endif
    }

    if (use_gui)
    {
        Inkscape::UI::Tools::init_latin_keys_group();
        /* Check for global remapping of Alt key */
        mapalt(guint(prefs->getInt("/options/mapalt/value", 0)));
        trackalt(guint(prefs->getInt("/options/trackalt/value", 0)));

        /* update highlight colors when theme changes */
        themecontext->getChangeThemeSignal().connect([this](){
            themecontext->themechangecallback();
        });
    }

    /* Initialize font factory */
    auto &factory = FontFactory::get();
    if (prefs->getBool("/options/font/use_fontsdir_system", true)) {
        char const *fontsdir = get_path(SYSTEM, FONTS);
        factory.AddFontsDir(fontsdir);
    }
    // we keep user font dir for simplicity
    if (prefs->getBool("/options/font/use_fontsdir_user", true)) {
        char const *fontsdirshared = get_path(SHARED, FONTS);
        if (fontsdirshared) {
            factory.AddFontsDir(fontsdirshared);
        }
        char const *fontsdir = get_path(USER, FONTS);
        factory.AddFontsDir(fontsdir);
    }
    Glib::ustring fontdirs_pref = prefs->getString("/options/font/custom_fontdirs");
    std::vector<Glib::ustring> fontdirs = Glib::Regex::split_simple("\\|", fontdirs_pref);
    for (auto &fontdir : fontdirs) {
        factory.AddFontsDir(fontdir.c_str());
    }
}

Application::~Application()
{
    if (_desktops) {
        g_error("FATAL: desktops still in list on application destruction!");
    }

    Inkscape::Preferences::unload();
    Inkscape::CMSSystem::unload();

    _S_inst = nullptr; // this will probably break things

    refCount = 0;
}

/** Sets the keyboard modifier to map to Alt.
 *
 * Zero switches off mapping, as does '1', which is the default.
 */
void Application::mapalt(guint maskvalue)
{
    if ( maskvalue < 2 || maskvalue > 5 ) {  // MOD5 is the highest defined in gdktypes.h
        _mapalt = 0;
    } else {
        _mapalt = (GDK_ALT_MASK << (maskvalue-1));
    }
}

void
Application::crash_handler (int /*signum*/)
{
    using Inkscape::Debug::SimpleEvent;
    using Inkscape::Debug::EventTracker;
    using Inkscape::Debug::Logger;

    static bool recursion = false;

    /*
     * reset all signal handlers: any further crashes should just be allowed
     * to crash normally.
     * */
    signal (SIGSEGV, segv_handler );
    signal (SIGABRT, abrt_handler );
    signal (SIGFPE,  fpe_handler  );
    signal (SIGILL,  ill_handler  );
#ifndef _WIN32
    signal (SIGBUS,  bus_handler  );
#endif

    /* Stop bizarre loops */
    if (recursion) {
        abort ();
    }
    recursion = true;

    _crashIsHappening = true;

    EventTracker<SimpleEvent<Inkscape::Debug::Event::CORE> > tracker("crash");
    tracker.set<SimpleEvent<> >("emergency-save");

    fprintf(stderr, "\nEmergency save activated!\n");

    time_t sptime = time (nullptr);
    struct tm *sptm = localtime (&sptime);
    gchar sptstr[256];
    strftime(sptstr, 256, "%Y_%m_%d_%H_%M_%S", sptm);

    gint count = 0;
    gchar *curdir = g_get_current_dir(); // This one needs to be freed explicitly
    std::vector<gchar *> savednames;
    std::vector<gchar *> failednames;
    for (std::map<SPDocument*,int>::iterator iter = INKSCAPE._document_set.begin(), e = INKSCAPE._document_set.end();
          iter != e;
          ++iter) {
        SPDocument *doc = iter->first;
        Inkscape::XML::Node *repr;
        repr = doc->getReprRoot();
        if (doc->isModifiedSinceSave()) {
            const gchar *docname;
            char n[64];

            /* originally, the document name was retrieved from
             * the sodipod:docname attribute */
            docname = doc->getDocumentName();
            if (docname) {
                /* Removes an emergency save suffix if present: /(.*)\.[0-9_]*\.[0-9_]*\.[~\.]*$/\1/ */
                const char* d0 = strrchr ((char*)docname, '.');
                if (d0 && (d0 > docname)) {
                    const char* d = d0;
                    unsigned int dots = 0;
                    while ((isdigit (*d) || *d=='_' || *d=='.') && d>docname && dots<2) {
                        d -= 1;
                        if (*d=='.') dots++;
                    }
                    if (*d=='.' && d>docname && dots==2) {
                        size_t len = MIN (d - docname, 63);
                        memcpy (n, docname, len);
                        n[len] = '\0';
                        docname = n;
                    }
                }
            }
            if (!docname || !*docname) docname = "emergency";

            // Emergency filename
            char c[1024];
            g_snprintf (c, 1024, "%.256s.%s.%d.svg", docname, sptstr, count);

            const char* document_filename = doc->getDocumentFilename();
            char* document_base = nullptr;
            if (document_filename) {
                document_base = g_path_get_dirname(document_filename);
            }

            // Find a location
            const char* locations[] = {
                // Don't use getDocumentBase as that also can be unsaved template locations.
                document_base,
                g_get_home_dir(),
                g_get_tmp_dir(),
                curdir,
            };
            FILE *file = nullptr;
            for(auto & location : locations) {
                if (!location) continue; // It seems to be okay, but just in case
                gchar * filename = g_build_filename(location, c, nullptr);
                Inkscape::IO::dump_fopen_call(filename, "E");
                file = Inkscape::IO::fopen_utf8name(filename, "w");
                if (file) {
                    g_snprintf (c, 1024, "%s", filename); // we want the complete path to be stored in c (for reporting purposes)
                    break;
                }
            }
            if (document_base) {
                g_free(document_base);
            }

            // Save
            if (file) {
                sp_repr_save_stream (repr->document(), file, SP_SVG_NS_URI);
                savednames.push_back(g_strdup (c));
                fclose (file);

                // Attempt to add the emergency save to the recent files, so users can find it on restart
                auto recentmanager = Gtk::RecentManager::get_default();
                if (recentmanager && Glib::path_is_absolute(c)) {
                    Glib::ustring uri = Glib::filename_to_uri(c);
                    recentmanager->add_item(uri, {
                        docname,                 // Name
                        "Emergency Saved Image", // Description
                        "image/svg+xml",         // Mime type
                        "org.inkscape.Inkscape", // App name
                        "",                      // Execute
                        {"Crash"},               // Groups
                        true,                    // Private
                    });
                }
            } else {
                failednames.push_back((doc->getDocumentName()) ? g_strdup(doc->getDocumentName()) : g_strdup (_("Untitled document")));
            }
            count++;
        }
    }
    g_free(curdir);

    if (!savednames.empty()) {
        fprintf (stderr, "\nEmergency save document locations:\n");
        for (auto i:savednames) {
            fprintf (stderr, "  %s\n", i);
        }
    }
    if (!failednames.empty()) {
        fprintf (stderr, "\nFailed to do emergency save for documents:\n");
        for (auto i:failednames) {
            fprintf (stderr, "  %s\n", i);
        }
    }

    // do not save the preferences since they can be in a corrupted state
    Inkscape::Preferences::unload(false);

    fprintf (stderr, "Emergency save completed. Inkscape will close now.\n");
    fprintf (stderr, "If you can reproduce this crash, please file a bug at https://inkscape.org/report\n");
    fprintf (stderr, "with a detailed description of the steps leading to the crash, so we can fix it.\n");

    /* Show nice dialog box */

    char const *istr = "";
    char const *sstr = _("Automatic backups of unsaved documents were done to the following locations:\n");
    char const *fstr = _("Automatic backup of the following documents failed:\n");
    gint nllen = strlen ("\n");
    gint len = strlen (istr) + strlen (sstr) + strlen (fstr);
    for (auto i:savednames) {
        len = len + SP_INDENT + strlen (i) + nllen;
    }
    for (auto i:failednames) {
        len = len + SP_INDENT + strlen (i) + nllen;
    }
    len += 1;
    gchar *b = g_new (gchar, len);
    gint pos = 0;
    len = strlen (istr);
    memcpy (b + pos, istr, len);
    pos += len;
    if (!savednames.empty()) {
        len = strlen (sstr);
        memcpy (b + pos, sstr, len);
        pos += len;
        for (auto i:savednames) {
            memset (b + pos, ' ', SP_INDENT);
            pos += SP_INDENT;
            len = strlen(i);
            memcpy (b + pos, i, len);
            pos += len;
            memcpy (b + pos, "\n", nllen);
            pos += nllen;
        }
    }
    if (!failednames.empty()) {
        len = strlen (fstr);
        memcpy (b + pos, fstr, len);
        pos += len;
        for (auto i:failednames) {
            memset (b + pos, ' ', SP_INDENT);
            pos += SP_INDENT;
            len = strlen(i);
            memcpy (b + pos, i, len);
            pos += len;
            memcpy (b + pos, "\n", nllen);
            pos += nllen;
        }
    }
    *(b + pos) = '\0';

    if ( exists() && instance().use_gui() ) {
        try {
            auto builder = UI::create_builder("dialog-crash.glade");
            UI::get_widget<Gtk::Label>(builder, "message").set_label(b);
            UI::get_object<Gtk::TextBuffer>(builder, "stacktrace")->set_text("<pre>\n" + boost::stacktrace::to_string(boost::stacktrace::stacktrace()) + "</pre>\n<details><summary>System info</summary>\n" + debug_info() + "\n</details>");
            Gtk::MessageDialog &m = UI::get_widget<Gtk::MessageDialog>(builder, "crash_dialog");
            sp_transientize(m);
            UI::dialog_run(m);
        } catch (const Glib::Error &ex) {
            g_message("Glade file loading failed for crash handler... Anyway, error was: %s", b);
            std::cerr << boost::stacktrace::stacktrace();
        }
    } else {
        g_message( "Error: %s", b );
        std::cerr << boost::stacktrace::stacktrace();
    }
    g_free (b);

    tracker.clear();
    Logger::shutdown();

    fflush(stderr); // make sure buffers are empty before crashing (otherwise output might be suppressed)

    /* on exit, allow restored signal handler to take over and crash us */
}


void
Application::add_desktop (SPDesktop * desktop)
{
    g_return_if_fail (desktop != nullptr);
    if (_desktops == nullptr) {
        _desktops = new std::vector<SPDesktop*>;
    }

    if (std::find(_desktops->begin(), _desktops->end(), desktop) != _desktops->end()) {
        g_error("Attempted to add desktop already in list.");
    }

    _desktops->insert(_desktops->begin(), desktop);

    signal_activate_desktop.emit(desktop);
    signal_selection_set.emit(desktop->getSelection());
    signal_selection_changed.emit(desktop->getSelection());
}



void
Application::remove_desktop (SPDesktop * desktop)
{
    g_return_if_fail (desktop != nullptr);

    if (std::find (_desktops->begin(), _desktops->end(), desktop) == _desktops->end() ) {
        g_error("Attempted to remove desktop not in list.");
    }


    if (DESKTOP_IS_ACTIVE (desktop)) {
        signal_deactivate_desktop.emit(desktop);
        if (_desktops->size() > 1) {
            SPDesktop * new_desktop = *(++_desktops->begin());
            _desktops->erase(std::find(_desktops->begin(), _desktops->end(), new_desktop));
            _desktops->insert(_desktops->begin(), new_desktop);

            signal_activate_desktop.emit(new_desktop);
            signal_selection_set.emit(new_desktop->getSelection());
            signal_selection_changed.emit(new_desktop->getSelection());
        } else {
            if (desktop->getSelection())
                desktop->getSelection()->clear();
        }
    }

    _desktops->erase(std::find(_desktops->begin(), _desktops->end(), desktop));

    // if this was the last desktop, shut down the program
    if (_desktops->empty()) {
        this->exit();
        delete _desktops;
        _desktops = nullptr;
    }
}



void
Application::activate_desktop (SPDesktop * desktop)
{
    g_return_if_fail (desktop != nullptr);

    if (DESKTOP_IS_ACTIVE (desktop)) {
        return;
    }

    std::vector<SPDesktop*>::iterator i;

    if ((i = std::find (_desktops->begin(), _desktops->end(), desktop)) == _desktops->end()) {
        g_error("Tried to activate desktop not added to list.");
    }

    SPDesktop *current = _desktops->front();

    signal_deactivate_desktop.emit(current);

    _desktops->erase (i);
    _desktops->insert (_desktops->begin(), desktop);

    signal_activate_desktop.emit(desktop);
    signal_selection_set(desktop->getSelection());
    signal_selection_changed(desktop->getSelection());
}


/**
 *  Resends ACTIVATE_DESKTOP for current desktop; needed when a new desktop has got its window that dialogs will transientize to
 */
void
Application::reactivate_desktop (SPDesktop * desktop)
{
    g_return_if_fail (desktop != nullptr);

    if (DESKTOP_IS_ACTIVE (desktop)) {
        signal_activate_desktop.emit(desktop);
    }
}



SPDesktop *
Application::find_desktop_by_dkey (unsigned int dkey)
{
    for (auto & _desktop : *_desktops) {
        if (_desktop->dkey == dkey){
            return _desktop;
        }
    }
    return nullptr;
}


unsigned int
Application::maximum_dkey()
{
    unsigned int dkey = 0;

    for (auto & _desktop : *_desktops) {
        if (_desktop->dkey > dkey){
            dkey = _desktop->dkey;
        }
    }
    return dkey;
}



SPDesktop *
Application::next_desktop ()
{
    SPDesktop *d = nullptr;
    unsigned int dkey_current = (_desktops->front())->dkey;

    if (dkey_current < maximum_dkey()) {
        // find next existing
        for (unsigned int i = dkey_current + 1; i <= maximum_dkey(); ++i) {
            d = find_desktop_by_dkey (i);
            if (d) {
                break;
            }
        }
    } else {
        // find first existing
        for (unsigned int i = 0; i <= maximum_dkey(); ++i) {
            d = find_desktop_by_dkey (i);
            if (d) {
                break;
            }
        }
    }

    g_assert (d);
    return d;
}



SPDesktop *
Application::prev_desktop ()
{
    SPDesktop *d = nullptr;
    unsigned int dkey_current = (_desktops->front())->dkey;

    if (dkey_current > 0) {
        // find prev existing
        for (signed int i = dkey_current - 1; i >= 0; --i) {
            d = find_desktop_by_dkey (i);
            if (d) {
                break;
            }
        }
    }
    if (!d) {
        // find last existing
        d = find_desktop_by_dkey (maximum_dkey());
    }

    g_assert (d);
    return d;
}



void
Application::switch_desktops_next ()
{
    next_desktop()->presentWindow();
}

void
Application::switch_desktops_prev()
{
    prev_desktop()->presentWindow();
}

void
Application::external_change()
{
    signal_external_change.emit();
}

/**
 * fixme: These need probably signals too
 */
void
Application::add_document (SPDocument *document)
{
    g_return_if_fail (document != nullptr);

    // try to insert the pair into the list
    if (!(_document_set.insert(std::make_pair(document, 1)).second)) {
        //insert failed, this key (document) is already in the list
        for (auto & iter : _document_set) {
            if (iter.first == document) {
                // found this document in list, increase its count
                iter.second ++;
            }
       }
    }
}


// returns true if this was last reference to this document, so you can delete it
bool
Application::remove_document (SPDocument *document)
{
    g_return_val_if_fail (document != nullptr, false);

    for (std::map<SPDocument *,int>::iterator iter = _document_set.begin();
              iter != _document_set.end();
              ++iter) {
        if (iter->first == document) {
            // found this document in list, decrease its count
            iter->second --;
            if (iter->second < 1) {
                // this was the last one, remove the pair from list
                _document_set.erase (iter);

                return true;
            } else {
                return false;
            }
        }
    }

    return false;
}

SPDesktop *
Application::active_desktop()
{
    if (!_desktops || _desktops->empty()) {
        return nullptr;
    }

    return _desktops->front();
}

SPDocument *
Application::active_document()
{
    if (SP_ACTIVE_DESKTOP) {
        return SP_ACTIVE_DESKTOP->getDocument();
    } else if (!_document_set.empty()) {
        // If called from the command line there will be no desktop
        // So 'fall back' to take the first listed document in the Inkscape instance
        return _document_set.begin()->first;
    }

    return nullptr;
}

bool
Application::sole_desktop_for_document(SPDesktop const &desktop) {
    SPDocument const* document = desktop.doc();
    if (!document) {
        return false;
    }
    for (auto other_desktop : *_desktops) {
        SPDocument *other_document = other_desktop->doc();
        if ( other_document == document && other_desktop != &desktop ) {
            return false;
        }
    }
    return true;
}

/*#####################
# HELPERS
#####################*/

/**
 *  Handler for Inkscape's Exit verb.  This emits the shutdown signal,
 *  saves the preferences if appropriate, and quits.
 */
void
Application::exit ()
{
    //emit shutdown signal so that dialogs could remember layout
    signal_shut_down.emit();

    Inkscape::Preferences::unload();
}

void
Application::get_all_desktops(std::list< SPDesktop* >& listbuf)
{
    listbuf.insert(listbuf.end(), _desktops->begin(), _desktops->end());
}

} // namespace Inkscape

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
