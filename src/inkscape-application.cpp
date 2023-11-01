// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The main Inkscape application.
 *
 * Copyright (C) 2018 Tavmjong Bah
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 */

/* Application flow:
 *   main() -> InkscapeApplication::singleton().gio_app()->run(argc, argv);
 *
 *     InkscapeApplication::InkscapeApplication
 *       Initialized: GC, Debug, Gettext, Autosave, Actions, Commandline
 *     InkscapeApplication::on_handle_local_options
 *       InkscapeApplication::parse_actions
 *
 *     -- Switch to main instance if new Inkscape instance is merged with existing instance. --
 *        New instances are merged with existing instance unless app_id is changed, see below.
 *
 *     InkscapeApplication::on_startup                   | Only called for main instance
 *
 *     InkscapeApplication::on_activate (no file specified) OR InkscapeApplication::on_open (file specified)
 *       InkscapeApplication::process_document           | Will use command-line actions from main instance!
 *
 *       InkscapeApplication::create_window (document)
 *         Inkscape::Shortcuts
 *
 *     InkscapeApplication::create_window (file)         |
 *       InkscapeApplication::create_window (document)   | Open/Close document
 *     InkscapeApplication::destroy_window               |
 *
 *     InkscapeApplication::on_quit
 *       InkscapeApplication::destroy_all
 *       InkscapeApplication::destroy_window
 */

#include "inkscape-application.h"

#ifdef HAVE_CONFIG_H
# include "config.h"      // Defines ENABLE_NLS
#endif

#include <iostream>
#include <fstream>
#include <iomanip>
#include <cerrno>  // History file
#include <regex>
#include <numeric>
#include <unistd.h>
#include <chrono>
#include <thread>

#include <giomm/file.h>
#include <glibmm/i18n.h>  // Internationalization
#include <gtkmm/application.h>
#include <gtkmm/recentmanager.h>

#include "inkscape-version-info.h"
#include "inkscape-window.h"
#include "auto-save.h"              // Auto-save
#include "desktop.h"                // Access to window
#include "document.h"
#include "file.h"                   // sp_file_convert_dpi
#include "inkscape.h"               // Inkscape::Application
#include "selection.h"
#include "path-prefix.h"            // Data directory

#include "actions/actions-base.h"
#include "actions/actions-dialogs.h"
#include "actions/actions-edit.h"
#include "actions/actions-effect.h"
#include "actions/actions-element-a.h"
#include "actions/actions-element-image.h"
#include "actions/actions-file.h"
#include "actions/actions-helper.h"
#include "actions/actions-helper-gui.h"
#include "actions/actions-hide-lock.h"
#include "actions/actions-object-align.h"
#include "actions/actions-object.h"
#include "actions/actions-output.h"
#include "actions/actions-paths.h"
#include "actions/actions-selection-object.h"
#include "actions/actions-selection.h"
#include "actions/actions-text.h"
#include "actions/actions-transform.h"
#include "actions/actions-tutorial.h"
#include "actions/actions-window.h"
#include "debug/logger.h"           // INKSCAPE_DEBUG_LOG support
#include "extension/db.h"
#include "extension/effect.h"
#include "extension/init.h"
#include "extension/input.h"
#include "include/glibmm_version.h"
#include "inkgc/gc-core.h"          // Garbage Collecting init
#include "io/file.h"                // File open (command line).
#include "io/fix-broken-links.h"    // Fix up references.
#include "io/resource.h"            // TEMPLATE
#include "object/sp-root.h"         // Inkscape version.
#include "ui/desktop/document-check.h"    // Check for data loss on closing document window.
#include "ui/desktop/menubar.h"
#include "ui/dialog-run.h"
#include "ui/dialog/dialog-manager.h"     // Save state
#include "ui/dialog/font-substitution.h"  // Warn user about font substitution.
#include "ui/dialog/startup.h"
#include "ui/interface.h"                 // sp_ui_error_dialog
#include "ui/shortcuts.h"           // Shortcuts... init
#include "ui/widget/desktop-widget.h"
#include "util/scope_exit.h"
#include "util/statics.h"
#include "util/units.h"           // Redimension window

#ifdef ENABLE_NLS
// Native Language Support - shouldn't this always be used?
#include "helper/gettext.h"   // gettext init
#endif // ENABLE_NLS

#ifdef WITH_GNU_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

using Inkscape::IO::Resource::UIS;

// This is a bit confusing as there are two ways to handle command line arguments and files
// depending on if the Gio::Application::Flags::HANDLES_OPEN and/or Gio::Application::Flags::HANDLES_COMMAND_LINE
// flags are set. If the open flag is set and the command line not, the all the remainng arguments
// after calling on_handle_local_options() are assumed to be filenames.

// Add document to app.
void
InkscapeApplication::document_add(SPDocument* document)
{
    if (document) {
        auto it = _documents.find(document);
        if (it == _documents.end()) {
            _documents[document] = std::vector<InkscapeWindow*>();
        } else {
            // Should never happen.
            std::cerr << "InkscapeApplication::add_document: Document already opened!" << std::endl;
        }
    } else {
        // Should never happen!
        std::cerr << "InkscapeApplication::add_document: No document!" << std::endl;
    }
}

// New document, add it to app. TODO: This should really be open_document with option to strip template data.
SPDocument*
InkscapeApplication::document_new(const std::string &Template)
{
    auto my_template = Template;
    if (my_template.empty()) {
        my_template = Inkscape::IO::Resource::get_filename(Inkscape::IO::Resource::TEMPLATES, "default.svg", true);
    }

    // Open file
    SPDocument *document = ink_file_new(my_template);
    if (document) {
        document_add(document);

        // Set viewBox if it doesn't exist.
        if (!document->getRoot()->viewBox_set) {
            document->setViewBox();
        }

    } else {
        std::cerr << "InkscapeApplication::new_document: failed to open new document!" << std::endl;
    }

    return document;
}


// Open a document, add it to app.
SPDocument*
InkscapeApplication::document_open(const Glib::RefPtr<Gio::File>& file, bool *cancelled)
{
    // Open file
    SPDocument *document = ink_file_open(file, cancelled);

    if (document) {
        document->setVirgin(false); // Prevents replacing document in same window during file open.

        // Add/promote recent file; when we call add_item and file is on a recent list already,
        // then apparently only "modified" time changes.
        if (auto recentmanager = Gtk::RecentManager::get_default()) {
            auto uri = file->get_uri();
            auto path = file->get_path();
            // Opening crash files, we can link them back using the recent files manager
            // to get the original context for the file.
            bool is_crash = false;
            try {
                auto orig = recentmanager->lookup_item(uri);
                if ((is_crash = orig->has_group("Crash"))) {
                    document->setModifiedSinceSave(true);
                    // Crash files store the original name in the display name field.
                    auto old_path = Inkscape::IO::find_original_file(path, Glib::filename_from_utf8(orig->get_display_name()));
                    document->setDocumentFilename(old_path.empty() ? nullptr : old_path.c_str());
                    // We don't want other programs to gain access to this crash file
                    recentmanager->remove_item(uri);
                }
            } catch(Glib::Error) {
                // Nothing to do since lookup failed.
            }
            if (!is_crash) {
                recentmanager->add_item(uri);
            }
        }

        document_add (document);
    } else if (cancelled == nullptr || !(*cancelled)) {
        std::cerr << "InkscapeApplication::document_open: Failed to open: " << file->get_parse_name().raw() << std::endl;
    }

    return document;
}


// Open a document, add it to app.
SPDocument*
InkscapeApplication::document_open(const std::string& data)
{
    // Open file
    SPDocument *document = ink_file_open(data);

    if (document) {
        document->setVirgin(false); // Prevents replacing document in same window during file open.

        document_add (document);
    } else {
        std::cerr << "InkscapeApplication::document_open: Failed to open memory document." << std::endl;
    }

    return document;
}


/** Swap out one document for another in a window... maybe this should disappear.
 *  Does not delete old document!
 */
bool
InkscapeApplication::document_swap(InkscapeWindow* window, SPDocument* document)
{
    if (!document || !window) {
        std::cerr << "InkscapeAppliation::swap_document: Missing window or document!" << std::endl;
        return false;
    }

    SPDesktop* desktop = window->get_desktop();
    SPDocument* old_document = window->get_document();
    desktop->change_document(document);

    // We need to move window from the old document to the new document.

    // Find old document
    auto it = _documents.find(old_document);
    if (it != _documents.end()) {

        // Remove window from document map.
        auto it2 = std::find(it->second.begin(), it->second.end(), window);
        if (it2 != it->second.end()) {
            it->second.erase(it2);
        } else {
            std::cerr << "InkscapeApplication::swap_document: Window not found!" << std::endl;
        }

    } else {
        std::cerr << "InkscapeApplication::swap_document: Document not in map!" << std::endl;
    }

    // Find new document
    it = _documents.find(document);
    if (it != _documents.end()) {
        it->second.push_back(window);
    } else {
        std::cerr << "InkscapeApplication::swap_document: Document not in map!" << std::endl;
    }

    // To be removed (add/delete once per window)!
    INKSCAPE.add_document(document);
    INKSCAPE.remove_document(old_document);

    _active_document  = document;
    _active_selection = desktop->getSelection();
    _active_desktop   = desktop;
    _active_window    = window;
    return true;
}

/** Revert document: open saved document and swap it for each window.
 */
bool
InkscapeApplication::document_revert(SPDocument* document)
{
    // Find saved document.
    gchar const *path = document->getDocumentFilename();
    if (!path) {
        std::cerr << "InkscapeApplication::revert_document: Document never saved, cannot revert." << std::endl;
        return false;
    }

    // Open saved document.
    Glib::RefPtr<Gio::File> file = Gio::File::create_for_path(document->getDocumentFilename());
    SPDocument* new_document = document_open (file);
    if (!new_document) {
        std::cerr << "InkscapeApplication::revert_document: Cannot open saved document!" << std::endl;
        return false;
    }

    // Allow overwriting current document.
    document->setVirgin(true);

    auto it = _documents.find(document);
    if (it != _documents.end()) {

        // Swap reverted document in all windows.
        for (auto it2 : it->second) {

            SPDesktop* desktop = it2->get_desktop();

            // Remember current zoom and view.
            double zoom = desktop->current_zoom();
            Geom::Point c = desktop->current_center();

            bool reverted = document_swap (it2, new_document);

            if (reverted) {
                desktop->zoom_absolute(c, zoom, false);
                /** Update LPE and Fix legacy LPE system **/
                sp_file_fix_lpe(desktop->getDocument());
            } else {
                std::cerr << "InkscapeApplication::revert_document: Revert failed!" << std::endl;
            }
        }

        document_close (document);
    } else {
        std::cerr << "InkscapeApplication::revert_document: Document not found!" << std::endl;
        return false;
    }
    return true;
}



/** Close a document, remove from app. No checking is done on modified status, etc.
 */
void
InkscapeApplication::document_close(SPDocument* document)
{
    if (document) {

        auto it = _documents.find(document);
        if (it != _documents.end()) {
            if (it->second.size() != 0) {
                std::cerr << "InkscapeApplication::close_document: Window vector not empty!" << std::endl;
            }
            _documents.erase(it);
        } else {
            std::cerr << "InkscapeApplication::close_document: Document not registered with application." << std::endl;
        }

        Inkscape::GC::release(document);
        assert(document->_anchored_refcount() == 0);
        delete document;

    } else {
        std::cerr << "InkscapeApplication::close_document: No document!" << std::endl;
    }
}


/** Return number of windows with document.
 */
unsigned
InkscapeApplication::document_window_count(SPDocument* document)
{
    unsigned count = 0;

    auto it = _documents.find(document);
    if (it != _documents.end()) {
        count = it->second.size();
    } else {
        std::cerr << "InkscapeApplication::document_window_count: Document not in map!" << std::endl;
    }

    return count;
}

/** Fix up a document if necessary (Only fixes that require GUI). MOVE TO ANOTHER FILE!
 */
void
InkscapeApplication::document_fix(InkscapeWindow* window)
{
    // Most fixes are handled when document is opened in SPDocument::createDoc().
    // But some require the GUI to be present. These are handled here.

    if (_with_gui) {

        SPDocument* document = window->get_document();

        // Perform a fixup pass for hrefs.
        if ( Inkscape::fixBrokenLinks(document) ) {
            Glib::ustring msg = _("Broken links have been changed to point to existing files.");
            SPDesktop* desktop = window->get_desktop();
            if (desktop != nullptr) {
                desktop->showInfoDialog(msg);
            }
        }

        // Fix dpi (pre-92 files).
        if ( sp_version_inside_range( document->getRoot()->version.inkscape, 0, 1, 0, 92 ) ) {
            sp_file_convert_dpi(document);
        }
        /** Update LPE and Fix legacy LPE system **/
        sp_file_fix_lpe(document);

        // Check for font substitutions, requires text to have been rendered.
        Inkscape::UI::Dialog::checkFontSubstitutions(document);
    }
}

/** Get a list of open documents (from document map).
 */
std::vector<SPDocument*>
InkscapeApplication::get_documents()
{
    std::vector<SPDocument*> documents;
    for (auto &i : _documents) {
        documents.push_back(i.first);
    }
    return documents;
}



// Take an already open document and create a new window, adding window to document map.
InkscapeWindow*
InkscapeApplication::window_open(SPDocument* document)
{
    // Once we've removed Inkscape::Application (separating GUI from non-GUI stuff)
    // it will be more easy to start up the GUI after-the-fact. Until then, prevent
    // opening a window if GUI not selected at start-up time.
    if (!_with_gui) {
        std::cerr << "InkscapeApplication::window_open: Not in gui mode!" << std::endl;
        return nullptr;
    }

    InkscapeWindow* window = new InkscapeWindow(document);
    // TODO Add window to application. (Instead of in InkscapeWindow constructor.)

    // To be removed (add once per window)!
    INKSCAPE.add_document(document);

    _active_window    = window;
    _active_desktop   = window->get_desktop();
    _active_selection = window->get_desktop()->getSelection();
    _active_document  = document;

    auto it = _documents.find(document);
    if (it != _documents.end()) {
        it->second.push_back(window);
    } else {
        std::cerr << "InkscapeApplication::window_open: Document not in map!" << std::endl;
    }

    document_fix(window); // May need flag to prevent this from being called more than once.

    return window;
}


// Close a window. Does not delete document.
void
InkscapeApplication::window_close(InkscapeWindow* window)
{
    // std::cout << "InkscapeApplication::close_window" << std::endl;
    // dump();

    if (window) {

        SPDocument* document = window->get_document();
        if (document) {

            // To be removed (remove once per window)!
            /* bool last = */ INKSCAPE.remove_document(document);

            // Leave active document alone (maybe should find new active window and reset variables).
            _active_selection = nullptr;
            _active_desktop   = nullptr;
            _active_window    = nullptr;

            // Remove window from document map.
            auto it = _documents.find(document);
            if (it != _documents.end()) {
                auto it2 = std::find(it->second.begin(), it->second.end(), window);
                if (it2 != it->second.end()) {
                    if (get_number_of_windows() == 1) {
                        // persist layout of docked and floating dialogs before deleting the last window
                        Inkscape::UI::Dialog::DialogManager::singleton().save_dialogs_state(
                           window->get_desktop_widget()->getDialogContainer());
                    }
                    it->second.erase(it2);
                    delete window; // Results in call to SPDesktop::destroy()
                } else {
                    std::cerr << "InkscapeApplication::close_window: window not found!" << std::endl;
                }
            } else {
                std::cerr << "InkscapeApplication::close_window: document not in map!" << std::endl;
            }
        } else {
            std::cerr << "InkscapeApplication::close_window: No document!" << std::endl;
        }

    } else {
        std::cerr << "InkscapeApplication::close_window: No window!" << std::endl;
    }

    // dump();
}


// Closes active window (useful for scripting).
void
InkscapeApplication::window_close_active()
{
    if (_active_window) {
        window_close (_active_window);
    } else {
        std::cerr << "InkscapeApplication::window_close_active: no active window!" << std::endl;
    }
}


/** Update windows in response to:
 *  - New active window
 *  - Document change
 *  - Selection change
 */
void
InkscapeApplication::windows_update(SPDocument* document)
{
    // Find windows:
    auto it = _documents.find( document );
    if (it != _documents.end()) {
        std::vector<InkscapeWindow*> windows = it->second;
        // std::cout << "InkscapeApplication::update_windows: windows size: " << windows.size() << std::endl;
        // Loop over InkscapeWindows.
        // Loop over DialogWindows. TBD
    } else {
        // std::cout << "InkscapeApplication::update_windows: no windows found" << std::endl;
    }
}

/** Debug function
 */
void
InkscapeApplication::dump()
{
    std::cout << "InkscapeApplication::dump()" << std::endl;
    std::cout << "  Documents: " << _documents.size() << std::endl;
    for (auto i : _documents) {
        std::cout << "    Document: " << (i.first->getDocumentName()?i.first->getDocumentName():"unnamed") << std::endl;
        for (auto j : i.second) {
            std::cout << "      Window: " << j->get_title() << std::endl;
        }
    }
}

static InkscapeApplication *_instance = nullptr;

InkscapeApplication *InkscapeApplication::instance()
{
    return _instance;
}

void
InkscapeApplication::_start_main_option_section(const Glib::ustring& section_name)
{
#ifndef _WIN32
    // Avoid outputting control characters to non-tty destinations.
    //
    // However, isatty() is not useful on Windows
    //   - it doesn't recognize mintty and similar terminals
    //   - it doesn't work in cmd.exe either, where we have to use the inkscape.com wrapper, connecting stdout to a pipe
    if (!isatty(fileno(stdout))) {
        return;
    }
#endif

    auto *gapp = gio_app();

    if (section_name.empty()) {
        gapp->add_main_option_entry(Gio::Application::OptionType::BOOL, Glib::ustring("\b\b  "));
    } else {
        gapp->add_main_option_entry(Gio::Application::OptionType::BOOL, Glib::ustring("\b\b  \n") + section_name + ":");
    }
}

InkscapeApplication::InkscapeApplication()
{
    if (_instance) {
        std::cerr << "Multiple instances of InkscapeApplication" << std::endl;
        std::terminate();
    }
    _instance = this;

    using T = Gio::Application;

    auto app_id = Glib::ustring("org.inkscape.Inkscape");
    auto flags = Gio::Application::Flags::HANDLES_OPEN | // Use default file opening.
                 Gio::Application::Flags::CAN_OVERRIDE_APP_ID;
    auto non_unique = false;

    // Allow an independent instance of Inkscape to run. Will have matching DBus name and paths
    // (e.g org.inkscape.Inkscape.tag, /org/inkscape/Inkscape/tag/window/1).
    // If this flag isn't set, any new instance of Inkscape will be merged with the already running
    // instance of Inkscape before on_open() or on_activate() is called.
    if (Glib::getenv("INKSCAPE_APP_ID_TAG") != "") {
        flags |= Gio::Application::Flags::CAN_OVERRIDE_APP_ID;
        app_id += "." + Glib::getenv("INKSCAPE_APP_ID_TAG");
        if (!Gio::Application::id_is_valid(app_id)) {
            std::cerr << "InkscapeApplication: invalid application id: " << app_id.raw() << std::endl;
            std::cerr << "  tag must be ASCII and not start with a number." << std::endl;
        }
        non_unique = true;
    } else if (Glib::getenv("SELF_CALL") == "") {
        // Version protection attempts to refuse to merge with inkscape version
        // that have a different build/revision hash. This is important for testing.
        auto test_app = Gio::Application::create(app_id, flags);
        test_app->register_application();
        if (test_app->get_default()->is_remote()) {
            bool enabled;
            Glib::VariantBase hint;
            if (!test_app->query_action(Inkscape::inkscape_revision(), enabled, hint)) {
                app_id += "." + Inkscape::inkscape_revision();
                non_unique = true;
            }
        }
        Gio::Application::unset_default();
    }

    if (gtk_init_check()) {
        g_set_prgname(app_id.c_str());
        _gio_application = Gtk::Application::create(app_id, flags);
    } else {
        _gio_application = Gio::Application::create(app_id, flags);
        _with_gui = false;
    }

    auto *gapp = gio_app();

    gapp->signal_startup().connect([this]() { this->on_startup(); });
    gapp->signal_activate().connect([this]() { this->on_activate(); });
    gapp->signal_open().connect(sigc::mem_fun(*this, &InkscapeApplication::on_open));

    // ==================== Initializations =====================
    // Garbage Collector
    Inkscape::GC::init();

#ifndef NDEBUG
    // Use environment variable INKSCAPE_DEBUG_LOG=log.txt for event logging
    Inkscape::Debug::Logger::init();
#endif

#ifdef ENABLE_NLS
    // Native Language Support (shouldn't this always be used?).
    Inkscape::initialize_gettext();
#endif

    // Autosave
    Inkscape::AutoSave::getInstance().init(this);

    // Don't set application name for now. We don't use it anywhere but
    // it overrides the name used for adding recently opened files and breaks the Gtk::RecentFilter
    // Glib::set_application_name(N_("Inkscape - A Vector Drawing Program"));  // After gettext() init.

    // ======================== Actions =========================
    add_actions_base(this);                 // actions that are GUI independent
    add_actions_edit(this);                 // actions for editing
    add_actions_dialogs(this);              // actions to open dialogs
    add_actions_effect(this);               // actions for Filters and Extensions
    add_actions_element_a(this);            // actions for the SVG a (anchor) element
    add_actions_element_image(this);        // actions for the SVG image element
    add_actions_file(this);                 // actions for file handling
    add_actions_hide_lock(this);            // actions for hiding/locking items.
    add_actions_object(this);               // actions for object manipulation
    add_actions_object_align(this);         // actions for object alignment
    add_actions_output(this);               // actions for file export
    add_actions_selection(this);            // actions for object selection
    add_actions_path(this);                 // actions for Paths
    add_actions_selection_object(this);     // actions for selected objects
    add_actions_text(this);                 // actions for Text
    add_actions_tutorial(this);             // actions for opening tutorials (with GUI only)
    add_actions_transform(this);            // actions for transforming selected objects
    add_actions_window(this);               // actions for windows

    // ====================== Command Line ======================

    // Will automatically handle character conversions.
    // Note: OptionType::FILENAME => std::string, OptionType::STRING => Glib::ustring.

#if GLIBMM_CHECK_VERSION(2,56,0)
    // Additional informational strings for --help output
    // TODO: Claims to be translated automatically, but seems broken, so pass already translated strings
    gapp->set_option_context_parameter_string(_("file1 [file2 [fileN]]"));
    gapp->set_option_context_summary(_("Process (or open) one or more files."));
    gapp->set_option_context_description(Glib::ustring("\n") + _("Examples:") + '\n'
            + "  " + Glib::ustring::compose(_("Export input SVG (%1) to PDF (%2) format:"), "in.svg", "out.pdf") + '\n'
            + '\t' + "inkscape --export-filename=out.pdf in.svg\n"
            + "  " + Glib::ustring::compose(_("Export input files (%1) to PNG format keeping original name (%2):"), "in1.svg, in2.svg", "in1.png, in2.png") + '\n'
            + '\t' + "inkscape --export-type=png in1.svg in2.svg\n"
            + "  " + Glib::ustring::compose(_("See %1 and %2 for more details."), "'man inkscape'", "http://wiki.inkscape.org/wiki/index.php/Using_the_Command_Line"));
#endif

    // clang-format off
    // General
    gapp->add_main_option_entry(T::OptionType::BOOL,     "version",                 'V', N_("Print Inkscape version"),                                                  "");
    gapp->add_main_option_entry(T::OptionType::BOOL,     "debug-info",             '\0', N_("Print debugging information"),                                                        "");
    gapp->add_main_option_entry(T::OptionType::BOOL,     "system-data-directory",  '\0', N_("Print system data directory"),                                             "");
    gapp->add_main_option_entry(T::OptionType::BOOL,     "user-data-directory",    '\0', N_("Print user data directory"),                                               "");
    gapp->add_main_option_entry(T::OptionType::BOOL,     "list-input-types",       '\0', N_("List all available input file extensions"),                                               "");
    gapp->add_main_option_entry(T::OptionType::STRING,   "app-id-tag",             '\0', N_("Create a unique instance of Inkscape with the application ID 'org.inkscape.Inkscape.TAG'"), "");

    // Open/Import
    _start_main_option_section(_("File import"));
    gapp->add_main_option_entry(T::OptionType::BOOL,     "pipe",                    'p', N_("Read input file from standard input (stdin)"),                             "");
    gapp->add_main_option_entry(T::OptionType::STRING,   "pages",                   'n', N_("Page numbers to import from multi-page document, i.e. PDF"), N_("PAGE[,PAGE]"));
    gapp->add_main_option_entry(T::OptionType::BOOL,     "pdf-poppler",            '\0', N_("Use poppler when importing via commandline"),                              "");
    gapp->add_main_option_entry(T::OptionType::STRING,   "pdf-font-strategy",      '\0', N_("How fonts are parsed in the internal PDF importer [draw-missing|draw-all|delete-missing|delete-all|substitute|keep]"), N_("STRATEGY")); // xSP
    gapp->add_main_option_entry(T::OptionType::STRING,   "convert-dpi-method",     '\0', N_("Method used to convert pre-0.92 document dpi, if needed: [none|scale-viewbox|scale-document]"), N_("METHOD"));
    gapp->add_main_option_entry(T::OptionType::BOOL,     "no-convert-text-baseline-spacing", '\0', N_("Do not fix pre-0.92 document's text baseline spacing on opening"), "");

    // Export - File and File Type
    _start_main_option_section(_("File export"));
    gapp->add_main_option_entry(T::OptionType::FILENAME, "export-filename",        'o', N_("Output file name (defaults to input filename; file type is guessed from extension if present; use '-' to write to stdout)"), N_("FILENAME"));
    gapp->add_main_option_entry(T::OptionType::BOOL,     "export-overwrite",      '\0', N_("Overwrite input file (otherwise add '_out' suffix if type doesn't change)"), "");
    gapp->add_main_option_entry(T::OptionType::STRING,   "export-type",           '\0', N_("File type(s) to export: [svg,png,ps,eps,pdf,emf,wmf,xaml]"), N_("TYPE[,TYPE]*"));
    gapp->add_main_option_entry(T::OptionType::STRING,   "export-extension",      '\0', N_("Extension ID to use for exporting"),                         N_("EXTENSION-ID"));

    // Export - Geometry
    _start_main_option_section(_("Export geometry"));                                                                                                                        // B = PNG, S = SVG, P = PS/EPS/PDF
    gapp->add_main_option_entry(T::OptionType::BOOL,     "export-area-page",       'C', N_("Area to export is page"),                                                   ""); // BSP
    gapp->add_main_option_entry(T::OptionType::BOOL,     "export-area-drawing",    'D', N_("Area to export is whole drawing (ignoring page size)"),                     ""); // BSP
    gapp->add_main_option_entry(T::OptionType::STRING,   "export-area",            'a', N_("Area to export in SVG user units"),                          N_("x0:y0:x1:y1")); // BSP
    gapp->add_main_option_entry(T::OptionType::BOOL,     "export-area-snap",      '\0', N_("Snap the bitmap export area outwards to the nearest integer values"),       ""); // Bxx
    gapp->add_main_option_entry(T::OptionType::DOUBLE,   "export-dpi",             'd', N_("Resolution for bitmaps and rasterized filters; default is 96"),      N_("DPI")); // BxP
    gapp->add_main_option_entry(T::OptionType::INT,      "export-width",           'w', N_("Bitmap width in pixels (overrides --export-dpi)"),                 N_("WIDTH")); // Bxx
    gapp->add_main_option_entry(T::OptionType::INT,      "export-height",          'h', N_("Bitmap height in pixels (overrides --export-dpi)"),               N_("HEIGHT")); // Bxx
    gapp->add_main_option_entry(T::OptionType::INT,      "export-margin",         '\0', N_("Margin around export area: units of page size for SVG, mm for PS/PDF"), N_("MARGIN")); // xSP

    // Export - Options
    _start_main_option_section(_("Export options"));
    gapp->add_main_option_entry(T::OptionType::STRING,   "export-page",           '\0', N_("Page number to export"), N_("all|n[,a-b]"));
    gapp->add_main_option_entry(T::OptionType::STRING,   "export-id",              'i', N_("ID(s) of object(s) to export"),                   N_("OBJECT-ID[;OBJECT-ID]*")); // BSP
    gapp->add_main_option_entry(T::OptionType::BOOL,     "export-id-only",         'j', N_("Hide all objects except object with ID selected by export-id"),             ""); // BSx
    gapp->add_main_option_entry(T::OptionType::BOOL,     "export-plain-svg",       'l', N_("Remove Inkscape-specific SVG attributes/properties"),                       ""); // xSx
    gapp->add_main_option_entry(T::OptionType::INT,      "export-ps-level",       '\0', N_("Postscript level (2 or 3); default is 3"),                         N_("LEVEL")); // xxP
    gapp->add_main_option_entry(T::OptionType::STRING,   "export-pdf-version",    '\0', N_("PDF version (1.4 or 1.5); default is 1.5"),                      N_("VERSION")); // xxP
    gapp->add_main_option_entry(T::OptionType::BOOL,     "export-text-to-path",    'T', N_("Convert text to paths (PS/EPS/PDF/SVG)"),                                   ""); // xxP
    gapp->add_main_option_entry(T::OptionType::BOOL,     "export-latex",          '\0', N_("Export text separately to LaTeX file (PS/EPS/PDF)"),                        ""); // xxP
    gapp->add_main_option_entry(T::OptionType::BOOL,     "export-ignore-filters", '\0', N_("Render objects without filters instead of rasterizing (PS/EPS/PDF)"),       ""); // xxP
    gapp->add_main_option_entry(T::OptionType::BOOL,     "export-use-hints",       't', N_("Use stored filename and DPI hints when exporting object selected by --export-id"), ""); // Bxx
    gapp->add_main_option_entry(T::OptionType::STRING,   "export-background",      'b', N_("Background color for exported bitmaps (any SVG color string)"),         N_("COLOR")); // Bxx
    // FIXME: Opacity should really be a DOUBLE, but an upstream bug means 0.0 is detected as NULL
    gapp->add_main_option_entry(T::OptionType::STRING,   "export-background-opacity", 'y', N_("Background opacity for exported bitmaps (0.0 to 1.0, or 1 to 255)"), N_("VALUE")); // Bxx
    gapp->add_main_option_entry(T::OptionType::STRING,   "export-png-color-mode", '\0', N_("Color mode (bit depth and color type) for exported bitmaps (Gray_1/Gray_2/Gray_4/Gray_8/Gray_16/RGB_8/RGB_16/GrayAlpha_8/GrayAlpha_16/RGBA_8/RGBA_16)"), N_("COLOR-MODE")); // Bxx
    gapp->add_main_option_entry(T::OptionType::STRING,      "export-png-use-dithering", '\0', N_("Force dithering or disables it"), "false|true"); // Bxx
    // FIXME: Compression should really be an INT, but an upstream bug means 0 is detected as NULL
    gapp->add_main_option_entry(T::OptionType::STRING,   "export-png-compression", '\0', N_("Compression level for PNG export (0 to 9); default is 6"), N_("LEVEL"));
    // FIXME: Antialias should really be an INT, but an upstream bug means 0 is detected as NULL
    gapp->add_main_option_entry(T::OptionType::STRING,   "export-png-antialias",   '\0', N_("Antialias level for PNG export (0 to 3); default is 2"),   N_("LEVEL"));

    // Query - Geometry
    _start_main_option_section(_("Query object/document geometry"));
    gapp->add_main_option_entry(T::OptionType::STRING,   "query-id",               'I', N_("ID(s) of object(s) to be queried"),              N_("OBJECT-ID[,OBJECT-ID]*"));
    gapp->add_main_option_entry(T::OptionType::BOOL,     "query-all",              'S', N_("Print bounding boxes of all objects"),                                     "");
    gapp->add_main_option_entry(T::OptionType::BOOL,     "query-x",                'X', N_("X coordinate of drawing or object (if specified by --query-id)"),          "");
    gapp->add_main_option_entry(T::OptionType::BOOL,     "query-y",                'Y', N_("Y coordinate of drawing or object (if specified by --query-id)"),          "");
    gapp->add_main_option_entry(T::OptionType::BOOL,     "query-width",            'W', N_("Width of drawing or object (if specified by --query-id)"),                 "");
    gapp->add_main_option_entry(T::OptionType::BOOL,     "query-height",           'H', N_("Height of drawing or object (if specified by --query-id)"),                "");

    // Processing
    _start_main_option_section(_("Advanced file processing"));
    gapp->add_main_option_entry(T::OptionType::BOOL,     "vacuum-defs",           '\0', N_("Remove unused definitions from the <defs> section(s) of document"),        "");
    gapp->add_main_option_entry(T::OptionType::STRING,   "select",                '\0', N_("Select objects: comma-separated list of IDs"),   N_("OBJECT-ID[,OBJECT-ID]*"));

    // Actions
    _start_main_option_section();
    gapp->add_main_option_entry(T::OptionType::STRING,   "actions",                'a', N_("List of actions (with optional arguments) to execute"),     N_("ACTION(:ARG)[;ACTION(:ARG)]*"));
    gapp->add_main_option_entry(T::OptionType::BOOL,     "action-list",           '\0', N_("List all available actions"),                                               "");
    gapp->add_main_option_entry(T::OptionType::FILENAME, "actions-file",          '\0', N_("Use a file to input actions list"),                             N_("FILENAME"));

    // Interface
    _start_main_option_section(_("Interface"));
    gapp->add_main_option_entry(T::OptionType::BOOL,     "with-gui",               'g', N_("With graphical user interface (required by some actions)"),                 "");
    gapp->add_main_option_entry(T::OptionType::BOOL,     "batch-process",         '\0', N_("Close GUI after executing all actions"),                                    "");
    _start_main_option_section();
    gapp->add_main_option_entry(T::OptionType::BOOL,     "shell",                 '\0', N_("Start Inkscape in interactive shell mode"),                                 "");
    gapp->add_main_option_entry(T::OptionType::BOOL,     "active-window",          'q', N_("Use active window from commandline"),                                       "");
    // clang-format on

    gapp->signal_handle_local_options().connect(sigc::mem_fun(*this, &InkscapeApplication::on_handle_local_options), true);

    if (_with_gui && !non_unique) { // Will fail to register if not unique.
        // On macOS, this enables:
        //   - DnD via dock icon
        //   - system menu "Quit"
        gtk_app()->property_register_session() = true;
    }
}

InkscapeApplication::~InkscapeApplication()
{
    _instance = nullptr;
    Inkscape::Util::StaticsBin::get().destroy();
}

/** Create a window given a document. This is used internally in InkscapeApplication.
 */
InkscapeWindow*
InkscapeApplication::create_window(SPDocument *document, bool replace)
{
    if (!gtk_app()) {
        g_assert_not_reached();
        return nullptr;
    }

    SPDocument *old_document = _active_document;
    InkscapeWindow* window = InkscapeApplication::get_active_window();

    if (replace && old_document && window) {
        document_swap (window, document);

        // Delete old document if no longer attached to any window.
        auto it = _documents.find (old_document);
        if (it != _documents.end()) {
            if (it->second.size() == 0) {
                document_close (old_document);
            }
        }
    } else {
        window = window_open (document);
    }

    window->present();

    return window;
}


/** Create a window given a Gio::File. This is what most external functions should call.
  *
  * @param file - The filename to open as a Gio::File object
*/
void
InkscapeApplication::create_window(const Glib::RefPtr<Gio::File>& file)
{
    if (!gtk_app()) {
        g_assert_not_reached();
        return;
    }

    SPDocument* document = nullptr;
    InkscapeWindow* window = nullptr;
    bool cancelled = false;

    if (file) {
        startup_close();
        document = document_open(file, &cancelled);
        if (document) {
            // Remember document so much that we'll add it to recent documents
            auto recentmanager = Gtk::RecentManager::get_default();
            recentmanager->add_item (file->get_uri());

            SPDocument* old_document = _active_document;
            bool replace = old_document && old_document->getVirgin();

            window = create_window (document, replace);
            document_fix(window);
        } else if (!cancelled) {
            std::cerr << "ConcreteInkscapeApplication<T>::create_window: Failed to load: "
                      << file->get_parse_name().raw() << std::endl;

            gchar *text = g_strdup_printf(_("Failed to load the requested file %s"), file->get_parse_name().c_str());
            sp_ui_error_dialog(text);
            g_free(text);
        }

    } else {
        document = document_new ();
        if (document) {
            window = window_open (document);
        } else {
            std::cerr << "ConcreteInkscapeApplication<T>::create_window: Failed to open default document!" << std::endl;
        }
    }

    _active_document = document;
    _active_window   = window;
}

/** Destroy a window and close the document it contains. Aborts if document needs saving.
 *  Replaces document and keeps window open if last window and keep_alive is true.
 *  Returns true if window destroyed.
 */
bool
InkscapeApplication::destroy_window(InkscapeWindow* window, bool keep_alive)
{
    if (!gtk_app()) {
        g_assert_not_reached();
        return false;
    }

    SPDocument* document = window->get_document();

    if (!document) {
        std::cerr << "InkscapeApplication::destroy_window: window has no document!" << std::endl;
        return false;
    }

    // Remove document if no window with document is left.
    auto it = _documents.find(document);
    if (it != _documents.end()) {
        // If only one window for document:
        if (it->second.size() == 1) {
            // Check if document needs saving.
            bool abort = document_check_for_data_loss(window);
            if (abort) {
                return false;
            }
        }

        if (get_number_of_windows() == 1 && keep_alive) {
            // Last window, replace with new document.
            auto new_document = document_new();
            document_swap(window, new_document);
        } else {
            window_close(window);
            if (get_number_of_windows() == 0) {
                // No Inkscape windows left, remove dialog windows.
                for (auto const &window : gtk_app()->get_windows()) {
                    window->close();
                }
            }
        }

        if (it->second.size() == 0) {
            // No window contains document so let's close it.
            document_close (document);
        }

    } else {
        std::cerr << "ConcreteInkscapeApplication<Gtk::Application>::destroy_window: Could not find document!" << std::endl;
    }

    // Debug
    // auto windows = get_windows();
    // std::cout << "destroy_windows: app windows size: " << windows.size() << std::endl;

    return true;
}

bool
InkscapeApplication::destroy_all()
{
    if (!gtk_app()) {
        g_assert_not_reached();
        return false;
    }

    while (_documents.size() != 0) {
        auto it = _documents.begin();
        if (!it->second.empty()) {
            auto it2 = it->second.begin();
            if (!destroy_window (*it2)) {
                return false; // If destroy aborted, we need to stop exit.
            }
        }
    }
    return true;
}

/** Common processing for documents
 */
void
InkscapeApplication::process_document(SPDocument* document, std::string output_path)
{
    // Add to Inkscape::Application...
    INKSCAPE.add_document(document);

    // Are we doing one file at a time? In that case, we don't recreate new windows for each file.
    bool replace = _use_pipe || _batch_process;

    // Open window if needed (reuse window if we are doing one file at a time inorder to save overhead).
    _active_document  = document;
    if (_with_gui) {
        _active_window = create_window(document, replace);
        _active_desktop = _active_window->get_desktop();
    } else {
        _active_window = nullptr;
        _active_desktop = nullptr;
        _active_selection = document->getSelection();
    }

    document->ensureUpToDate(); // Or queries don't work!

    // process_file
    activate_any_actions(_command_line_actions, _gio_application, _active_window, _active_document);

    if (_use_shell) {
        shell();
    }
    if (_with_gui && _active_window) {
        document_fix(_active_window);
    }
    // Only if --export-filename, --export-type --export-overwrite, or --export-use-hints are used.
    if (_auto_export) {
        // Save... can't use action yet.
        _file_export.do_export(document, output_path);
    }
}

/*
 * Called on first Inkscape instance creation. Not called if a new Inkscape instance is merged
 * with an existing instance.
 */
void
InkscapeApplication::on_startup()
{
    // Deprecated...
    Inkscape::Application::create(_with_gui);

    // Extensions
    Inkscape::Extension::init();

    // After extensions are loaded query effects to construct action data
    init_extension_action_data();

    // Command line execution. Must be after Extensions are initialized.
    parse_actions(_command_line_actions_input, _command_line_actions);

    if (!_with_gui) {
        return;
    }

    auto *gapp = gio_app();

    // ======================= Actions (GUI) ======================
    gapp->add_action("new",    sigc::mem_fun(*this, &InkscapeApplication::on_new   ));
    gapp->add_action("quit",   sigc::mem_fun(*this, &InkscapeApplication::on_quit  ));

    // ========================= GUI Init =========================
    Gtk::Window::set_default_icon_name("org.inkscape.Inkscape");

    // build_menu(); // Builds and adds menu to app. Used by all Inkscape windows. This can be done
                     // before all actions defined. * For the moment done by each window so we can add
                     // window action info to menu_label_to_tooltip map.
}

// Open document window with default document or pipe. Either this or on_open() is called.
void
InkscapeApplication::on_activate()
{
    std::string output;

    // Create new document, either from pipe or from template.
    SPDocument *document = nullptr;
    auto prefs = Inkscape::Preferences::get();

    if (_use_pipe) {

        // Create document from pipe in.
        std::istreambuf_iterator<char> begin(std::cin), end;
        std::string s(begin, end);
        document = document_open (s);
        output = "-";

    } else if(prefs->getBool("/options/boot/enabled", true)
               && !_use_command_line_argument
               && (gtk_app() && gtk_app()->get_windows().empty())) {

        Inkscape::UI::Dialog::StartScreen start_screen;

        // add start window to gtk_app to ensure proper closing on quit
        gtk_app()->add_window(start_screen);

        Inkscape::UI::dialog_run(start_screen);
        document = start_screen.get_document();
    } else {

        // Create a blank document from template
        document = document_new();
    }
    startup_close();

    if (!document) {
        std::cerr << "ConcreteInkscapeApplication::on_activate: failed to create document!" << std::endl;
        return;
    }

    // Process document (command line actions, shell, create window)
    process_document (document, output);

    if (_batch_process) {
        // If with_gui, we've reused a window for each file. We must quit to destroy it.
        gio_app()->quit();
    }
}

void
InkscapeApplication::startup_close()
{
    if (auto app = gtk_app()) {
        // Close any open start screens preventing double opens
        for (auto win : app->get_windows()) {
            if (auto start = dynamic_cast<Inkscape::UI::Dialog::StartScreen *>(win)) {
                start->close();
            }
        }
    }
}

// Open document window for each file. Either this or on_activate() is called.
// type_vec_files == std::vector<Glib::RefPtr<Gio::File> >
void
InkscapeApplication::on_open(const Gio::Application::type_vec_files& files, const Glib::ustring& hint)
{
    if(_pdf_poppler)
        INKSCAPE.set_pdf_poppler(_pdf_poppler);
    if(!_pages.empty())
        INKSCAPE.set_pages(_pages);

    INKSCAPE.set_pdf_font_strategy((int)_pdf_font_strategy);

    if (files.size() > 1 && !_file_export.export_filename.empty()) {
        std::cerr << "ConcreteInkscapeApplication<Gtk::Application>::on_open: "
                     "Can't use '--export-filename' with multiple input files "
                     "(output file would be overwritten for each input file). "
                     "Please use '--export-type' instead and rename manually."
                  << std::endl;
        return;
    }

    startup_close();
    for (auto file : files) {

        // Open file
        SPDocument *document = document_open (file);
        if (!document) {
            std::cerr << "ConcreteInkscapeApplication::on_open: failed to create document!" << std::endl;
            continue;
        }

        // Process document (command line actions, shell, create window)
        process_document (document, file->get_path());
    }

    if (_batch_process) {
        // If with_gui, we've reused a window for each file. We must quit to destroy it.
        gio_app()->quit();
    }
}

void
InkscapeApplication::parse_actions(const Glib::ustring& input, action_vector_t& action_vector)
{
    const auto re_colon = Glib::Regex::create("\\s*:\\s*");

    // Split action list
    std::vector<Glib::ustring> tokens = Glib::Regex::split_simple("\\s*;\\s*", input);
    for (auto token : tokens) {
        // Note: split into 2 tokens max ("param:value"); allows value to contain colon (e.g. abs. paths on Windows)
        std::vector<Glib::ustring> tokens2 = re_colon->split(token, 0,  static_cast<Glib::Regex::MatchFlags>(0), 2);
        Glib::ustring action;
        Glib::ustring value;
        if (tokens2.size() > 0) {
            action = tokens2[0];
        }
        if (action.find_first_not_of(" \f\n\r\t\v") == std::string::npos) {
            continue;
        }
        if (tokens2.size() > 1) {
            value = tokens2[1];
        }

        Glib::RefPtr<Gio::Action> action_ptr = _gio_application->lookup_action(action);
        if (action_ptr) {
            // Doesn't seem to be a way to test this using the C++ binding without Glib-CRITICAL errors.
            const  GVariantType* gtype = g_action_get_parameter_type(action_ptr->gobj());
            if (gtype) {
                // With value.
                Glib::VariantType type = action_ptr->get_parameter_type();
                if (type.get_string() == "b") {
                    bool b = false;
                    if (value == "1" || value == "true" || value.empty()) {
                        b = true;
                    } else if (value == "0" || value == "false") {
                        b = false;
                    } else {
                        std::cerr << "InkscapeApplication::parse_actions: Invalid boolean value: " << action << ":" << value << std::endl;
                    }
                    action_vector.emplace_back(action, Glib::Variant<bool>::create(b));
                } else if (type.get_string() == "i") {
                    action_vector.emplace_back(action, Glib::Variant<int>::create(std::stoi(value)));
                } else if (type.get_string() == "d") {
                    action_vector.emplace_back(action, Glib::Variant<double>::create(std::stod(value)));
                } else if (type.get_string() == "s") {
                    action_vector.emplace_back(action, Glib::Variant<Glib::ustring>::create(value));
                 } else if (type.get_string() == "(dd)") {
                    std::vector<Glib::ustring> tokens3 = Glib::Regex::split_simple(",", value.c_str());
                    if (tokens3.size() != 2) {
                        std::cerr << "InkscapeApplication::parse_actions: " << action << " requires two comma separated numbers" << std::endl;
                        continue;
                    }

                    double d0 = 0;
                    double d1 = 0;
                    try {
                        d0 = std::stod(tokens3[0]);
                        d1 = std::stod(tokens3[1]);
                    } catch (...) {
                        std::cerr << "InkscapeApplication::parse_actions: " << action << " requires two comma separated numbers" << std::endl;
                        continue;
                    }

                    action_vector.emplace_back(action, Glib::Variant<std::tuple<double, double>>::create({d0, d1}));
               } else {
                    std::cerr << "InkscapeApplication::parse_actions: unhandled action value: "
                              << action << ": " << type.get_string() << std::endl;
                }
            } else {
                // Stateless (i.e. no value).
                action_vector.emplace_back(action, Glib::VariantBase());
            }
        } else {
            std::cerr << "InkscapeApplication::parse_actions: could not find action for: " << action << std::endl;
        }
    }
}

#ifdef WITH_GNU_READLINE

// For use in shell mode. Command completion of action names.
char* readline_generator (const char* text, int state)
{
    static std::vector<Glib::ustring> actions;

    // Fill the vector of action names.
    if (actions.size() == 0) {
        auto *app = InkscapeApplication::instance();
        actions = app->gio_app()->list_actions();
        std::sort(actions.begin(), actions.end());
    }

    static int list_index = 0;
    static int len = 0;

    if (!state) {
        list_index = 0;
        len = strlen(text);
    }

    const char* name = nullptr;
    while (list_index < actions.size()) {
        name = actions[list_index].c_str();
        list_index++;
        if (strncmp (name, text, len) == 0) {
            return (strdup(name));
        }
    }

    return ((char*)nullptr);
}

char** readline_completion(const char* text, int start, int end)
{
    char **matches = (char**)nullptr;

    // Match actions names, but only at start of line.
    // It would be nice to also match action names after a ';' but it's not possible as text won't include ';'.
    if (start == 0) {
        matches = rl_completion_matches (text, readline_generator);
    }

    return (matches);
}

void readline_init()
{
    rl_readline_name = "inkscape";
    rl_attempted_completion_function = readline_completion;
}
#endif // WITH_GNU_READLINE


// Once we don't need to create a window just to process verbs!
void
InkscapeApplication::shell(bool active_window)
{
    std::cout << "Inkscape interactive shell mode. Type 'action-list' to list all actions. "
              << "Type 'quit' to quit." << std::endl;
    std::cout << " Input of the form:" << std::endl;
    std::cout << " action1:arg1; action2:arg2; ..." << std::endl;
    if (!_with_gui && !active_window) {
        std::cout << "Only actions that don't require a desktop may be used." << std::endl;
    }

#ifdef WITH_GNU_READLINE
    auto history_file = Glib::build_filename(Inkscape::IO::Resource::profile_path(), "shell.history");

#ifdef _WIN32
    gchar *locale_filename = g_win32_locale_filename_from_utf8(history_file.c_str());
    if (locale_filename) {
        history_file = locale_filename;
        g_free(locale_filename);
    }
#endif

    static bool init = false;
    if (!init) {
        readline_init();
        using_history();
        init = true;

        int error = read_history(history_file.c_str());
        if (error && error != ENOENT) {
            std::cerr << "read_history error: " << std::strerror(error) << " " << history_file << std::endl;
        }
    }
#endif

    while (std::cin.good()) {
        bool eof = false;
        std::string input;

#ifdef WITH_GNU_READLINE
        char *readline_input = readline("> ");
        if (readline_input) {
            input = readline_input;
            if (input != "quit" && input != "q") {
                add_history(readline_input);
            }
        } else {
            eof = true;
        }
        free(readline_input);
#else
        std::cout << "> ";
        std::getline(std::cin, input);
#endif

        // Remove trailing space
        input = std::regex_replace(input, std::regex(" +$"), "");

        if (eof || input == "quit" || input == "q") {
            break;
        }

        action_vector_t action_vector;
        if (active_window) {
            input = "active-window-start;" + input + ";active-window-end";
            unlink(get_active_desktop_commands_location().c_str());
        }
        parse_actions(input, action_vector);
        activate_any_actions(action_vector, _gio_application, _active_window, _active_document);
        if (active_window) {
            redirect_output();
        } else {
            // This would allow displaying the results of actions on the fly... but it needs to be well
            // vetted first.
            auto context = Glib::MainContext::get_default();
            while (context->iteration(false)) {};
        }
    }

#ifdef WITH_GNU_READLINE
    stifle_history(200); // ToDo: Make number a preference.
    int error = write_history(history_file.c_str());
    if (error) {
        std::cerr << "write_history error: " << std::strerror(error) << " " << history_file << std::endl;
    }
#endif

    if (_with_gui) {
        _gio_application->quit(); // Force closing windows.
    }
}

// Todo: Code can be improved by using proper IPC rather than temporary file polling.
void InkscapeApplication::redirect_output()
{
    auto const tmpfile = get_active_desktop_commands_location();

    for (int counter = 0; ; counter++) {
        if (Glib::file_test(tmpfile, Glib::FileTest::EXISTS)) {
            break;
        } else if (counter >= 300) { // 30 seconds exit
            std::cerr << "couldn't process response. File not found" << std::endl;
            return;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    auto tmpfile_delete_guard = scope_exit([&] {
        unlink(tmpfile.c_str());
    });

    auto awo = std::ifstream(tmpfile);
    if (!awo) {
        std::cout << "couldn't process response. Couldn't read" << std::endl;
        return;
    }

    auto const content = std::string(std::istreambuf_iterator<char>(awo), std::istreambuf_iterator<char>());
    awo.close();

    auto doc = sp_repr_read_mem(content.c_str(), strlen(content.c_str()), nullptr);
    if (!doc) {
        std::cout << "couldn't process response. Wrong data" << std::endl;
        return;
    }

    auto doc_delete_guard = scope_exit([&] {
        Inkscape::GC::release(doc);
    });

    bool noout = true;
    for (auto child = doc->root()->firstChild(); child; child = child->next()) {
        auto res = child->firstChild()->content();
        if (res) {
            if (!g_strcmp0(child->name(), "cerr")) {
                std::cerr << res << std::endl;
            } else {
                std::cout << res << std::endl;
            }
            noout = false;
        }
    }

    if (noout) {
        std::cout << "no output" << std::endl;
    }
}

// ========================= Callbacks ==========================

/*
 * Handle command line options.
 *
 * Options are processed in the order they appear in this function.
 * We process in order: Print -> GUI -> Open -> Query -> Process -> Export.
 * For each file without GUI: Open -> Query -> Process -> Export
 * More flexible processing can be done via actions.
 */
int
InkscapeApplication::on_handle_local_options(const Glib::RefPtr<Glib::VariantDict>& options)
{
    auto prefs = Inkscape::Preferences::get();
    if (!options) {
        std::cerr << "InkscapeApplication::on_handle_local_options: options is null!" << std::endl;
        return -1; // Keep going
    }

    // ===================== APP ID ====================
    if (options->contains("app-id-tag")) {
        Glib::ustring id_tag;
        options->lookup_value("app-id-tag", id_tag);
        Glib::ustring app_id = "org.inkscape.Inkscape." + id_tag;
        if (Gio::Application::id_is_valid(app_id)) {
            _gio_application->set_id(app_id);
        } else {
            std::cerr << "InkscapeApplication: invalid application id: " << app_id.raw() << std::endl;
            std::cerr << "  tag must be ASCII and not start with a number." << std::endl;
        }
    }

    // ===================== QUERY =====================
    // These are processed first as they result in immediate program termination.
    // Note: we cannot use actions here as the app has not been registered yet (registering earlier
    // causes problems with changing the app id).
    if (options->contains("version")) {
        std::cout << Inkscape::inkscape_version() << std::endl;
        return EXIT_SUCCESS;
    }

    if (options->contains("debug-info")) {
        std::cout << Inkscape::debug_info() << std::endl;
        return EXIT_SUCCESS;
    }

    if (options->contains("system-data-directory")) {
        std::cout << Glib::build_filename(get_inkscape_datadir(), "inkscape") << std::endl;
        return EXIT_SUCCESS;
    }

    if (options->contains("user-data-directory")) {
        std::cout << Inkscape::IO::Resource::profile_path() << std::endl;
        return EXIT_SUCCESS;
    }

    // Can't do this until after app is registered!
    // if (options->contains("action-list")) {
    //     print_action_list();
    //     return EXIT_SUCCESS;
    // }

    // For options without arguments.
    auto base = Glib::VariantBase();

    // ================== GUI and Shell ================

    // Use of most command line options turns off use of gui unless explicitly requested!
    // Listed in order that they appear in constructor.
    if (options->contains("pipe")                  ||

        options->contains("export-filename")       ||
        options->contains("export-overwrite")      ||
        options->contains("export-type")           ||
        options->contains("export-page")           ||

        options->contains("export-area-page")      ||
        options->contains("export-area-drawing")   ||
        options->contains("export-area")           ||
        options->contains("export-area-snap")      ||
        options->contains("export-dpi")            ||
        options->contains("export-width")          ||
        options->contains("export-height")         ||
        options->contains("export-margin")         ||
        options->contains("export-height")         ||

        options->contains("export-id")             ||
        options->contains("export-id-only")        ||
        options->contains("export-plain-svg")      ||
        options->contains("export-ps-level")       ||
        options->contains("export-pdf-version")    ||
        options->contains("export-text-to_path")   ||
        options->contains("export-latex")          ||
        options->contains("export-ignore-filters") ||
        options->contains("export-use-hints")      ||
        options->contains("export-background")     ||
        options->contains("export-background-opacity") ||
        options->contains("export-text-to_path")   ||
        options->contains("export-png-color-mode") ||
        options->contains("export-png-use-dithering") ||
        options->contains("export-png-compression") ||
        options->contains("export-png-antialias") ||

        options->contains("query-id")              ||
        options->contains("query-x")               ||
        options->contains("query-all")             ||
        options->contains("query-y")               ||
        options->contains("query-width")           ||
        options->contains("query-height")          ||

        options->contains("vacuum-defs")           ||
        options->contains("select")                ||
        options->contains("list-input-types")      ||
        options->contains("action-list")           ||
        options->contains("actions")               ||
        options->contains("actions-file")          ||
        options->contains("shell")
        ) {
        _with_gui = false;
    }

    if (options->contains("with-gui")        ||
        options->contains("batch-process")
        ) {
        _with_gui = bool(gtk_app()); // Override turning GUI off
        if (!_with_gui)
            std::cerr << "No GUI available, some actions may fail" << std::endl;
    }

    if (options->contains("batch-process"))  _batch_process = true;
    if (options->contains("shell"))          _use_shell = true;
    if (options->contains("pipe"))           _use_pipe  = true;

    // Enable auto-export
    if (options->contains("export-filename")  ||
        options->contains("export-type")      ||
        options->contains("export-overwrite") ||
        options->contains("export-use-hints")
        ) {
        _auto_export = true;
    }

    // 1. If we are running in command-line mode (without gui) and we haven't explicitly changed the app_id,
    // change it here so that this instance of Inkscape is not merged with an existing instance (otherwise
    // unwanted windows will pop up and the command-line arguments will be ignored).
    // 2. Set a new app id if we are calling a new inkscape instance (with a gui) through an extension
    // when the extension author hasn't already done so
    bool use_active_window = options->contains("active-window");
    if (!options->contains("app-id-tag") && ((_with_gui == false && use_active_window == false) ||
                                             (Glib::getenv("SELF_CALL") != "" && _with_gui == true))) {
        Glib::ustring app_id = "org.inkscape.Inkscape.p" + std::to_string(getpid());
        _gio_application->set_id(app_id);
    }

    // ==================== ACTIONS ====================
    // Actions as an argument string: e.g.: --actions="query-id:rect1;query-x".
    // Actions will be processed in order that they are given in argument.
    Glib::ustring actions;
    if (options->contains("actions-file")) {
        std::string fileactions;
        options->lookup_value("actions-file", fileactions);
        if (!fileactions.empty()) {
            std::ifstream awo(fileactions);
            if (awo) {
                std::string content((std::istreambuf_iterator<char>(awo)), (std::istreambuf_iterator<char>()));
                _command_line_actions_input = content + ";";
            }
        }
    } else if (options->contains("actions")) {
        options->lookup_value("actions", _command_line_actions_input);
    }

    // This must be done after the app has been registered!
    if (options->contains("action-list")) {
        _command_line_actions.emplace_back("action-list", base);
    }

    if (options->contains("list-input-types")) {
        _command_line_actions.emplace_back("list-input-types", base);
    }

    // ================= OPEN/IMPORT ===================

    if (options->contains("pages")) {
        options->lookup_value("pages", _pages);
    }

    if (options->contains("pdf-poppler")) {
        _pdf_poppler = true;
    }

    if (options->contains("pdf-font-strategy")) {
        Glib::ustring strategy;
        options->lookup_value("pdf-font-strategy", strategy);
        _pdf_font_strategy = FontStrategy::RENDER_MISSING;
        if (strategy == "delete-all") {
            _pdf_font_strategy = FontStrategy::DELETE_ALL;
        }
        if (strategy == "delete-missing") {
            _pdf_font_strategy = FontStrategy::DELETE_MISSING;
        }
        if (strategy == "draw-all") {
            _pdf_font_strategy = FontStrategy::RENDER_ALL;
        }
        if (strategy == "keep") {
            _pdf_font_strategy = FontStrategy::KEEP_MISSING;
        }
        if (strategy == "substitute") {
            _pdf_font_strategy = FontStrategy::SUBSTITUTE_MISSING;
        }
    }

    if (options->contains("convert-dpi-method")) {
        Glib::ustring method;
        options->lookup_value("convert-dpi-method", method);
        if (!method.empty()) {
            _command_line_actions.emplace_back("convert-dpi-method", Glib::Variant<Glib::ustring>::create(method));
        }
    }

    if (options->contains("no-convert-text-baseline-spacing")) {
        _command_line_actions.emplace_back("no-convert-baseline", base);
    }

    // ===================== QUERY =====================

    // 'query-id' should be processed first! Can be a comma-separated list.
    if (options->contains("query-id")) {
        Glib::ustring query_id;
        options->lookup_value("query-id", query_id);
        if (!query_id.empty()) {
            _command_line_actions.emplace_back("select-by-id", Glib::Variant<Glib::ustring>::create(query_id));
        }
    }

    if (options->contains("query-all"))    _command_line_actions.emplace_back("query-all",   base);
    if (options->contains("query-x"))      _command_line_actions.emplace_back("query-x",     base);
    if (options->contains("query-y"))      _command_line_actions.emplace_back("query-y",     base);
    if (options->contains("query-width"))  _command_line_actions.emplace_back("query-width", base);
    if (options->contains("query-height")) _command_line_actions.emplace_back("query-height",base);


    // =================== PROCESS =====================

    if (options->contains("vacuum-defs"))  _command_line_actions.emplace_back("vacuum-defs", base);

    if (options->contains("select")) {
        Glib::ustring select;
        options->lookup_value("select", select);
        if (!select.empty()) {
            _command_line_actions.emplace_back("select", Glib::Variant<Glib::ustring>::create(select));
        }
    }

    // ==================== EXPORT =====================
    if (options->contains("export-filename")) {
        options->lookup_value("export-filename",  _file_export.export_filename);
    }

    if (options->contains("export-type")) {
        options->lookup_value("export-type",      _file_export.export_type);
    }
    if (options->contains("export-extension")) {
        options->lookup_value("export-extension", _file_export.export_extension);
        _file_export.export_extension = _file_export.export_extension.lowercase();
    }

    if (options->contains("export-overwrite"))    _file_export.export_overwrite    = true;

    if (options->contains("export-page")) {
        options->lookup_value("export-page", _file_export.export_page);
    }

    // Export - Geometry
    if (options->contains("export-area")) {
        Glib::ustring area{};
        options->lookup_value("export-area", area);
        _file_export.set_export_area(area);
    }

    if (options->contains("export-area-drawing")) {
        _file_export.set_export_area_type(ExportAreaType::Drawing);
    }
    if (options->contains("export-area-page")) {
        _file_export.set_export_area_type(ExportAreaType::Page);
    }

    if (options->contains("export-margin")) {
        options->lookup_value("export-margin",    _file_export.export_margin);
    }

    if (options->contains("export-area-snap"))    _file_export.export_area_snap    = true;

    if (options->contains("export-width")) {
        options->lookup_value("export-width",     _file_export.export_width);
    }

    if (options->contains("export-height")) {
        options->lookup_value("export-height",    _file_export.export_height);
    }

    // Export - Options
    if (options->contains("export-id")) {
        options->lookup_value("export-id",        _file_export.export_id);
    }

    if (options->contains("export-id-only"))      _file_export.export_id_only     = true;
    if (options->contains("export-plain-svg"))    _file_export.export_plain_svg      = true;

    if (options->contains("export-dpi")) {
        options->lookup_value("export-dpi",       _file_export.export_dpi);
    }

    if (options->contains("export-ignore-filters")) _file_export.export_ignore_filters = true;
    if (options->contains("export-text-to-path"))   _file_export.export_text_to_path   = true;

    if (options->contains("export-ps-level")) {
        options->lookup_value("export-ps-level",  _file_export.export_ps_level);
    }

    if (options->contains("export-pdf-version")) {
        options->lookup_value("export-pdf-version", _file_export.export_pdf_level);
    }

    if (options->contains("export-latex"))        _file_export.export_latex       = true;
    if (options->contains("export-use-hints"))    _file_export.export_use_hints   = true;

    if (options->contains("export-background")) {
        options->lookup_value("export-background",_file_export.export_background);
    }

    // FIXME: Upstream bug means DOUBLE is ignored if set to 0.0 so doesn't exist in options
    if (options->contains("export-background-opacity")) {
        Glib::ustring opacity;
        options->lookup_value("export-background-opacity", opacity);
        _file_export.export_background_opacity = Glib::Ascii::strtod(opacity);
    }

    if (options->contains("export-png-color-mode")) {
        options->lookup_value("export-png-color-mode", _file_export.export_png_color_mode);
    }

    if (options->contains("export-png-use-dithering")) {
        Glib::ustring val;
        options->lookup_value("export-png-use-dithering", val);
        if (val == "true") {
            _file_export.export_png_use_dithering = true;
#if CAIRO_VERSION < CAIRO_VERSION_ENCODE(1, 18, 0)
            std::cerr << "Your cairo version does not support dithering! Option will be ignored." << std::endl;
#endif
        }
        else if (val == "false") _file_export.export_png_use_dithering = false;
        else std::cerr << "invalid value for export-png-use-dithering. Ignoring." << std::endl;
    } else _file_export.export_png_use_dithering = prefs->getBool("/options/dithering/value", true);

    // FIXME: Upstream bug means INT is ignored if set to 0 so doesn't exist in options
    if (options->contains("export-png-compression")) {
        Glib::ustring compression;
        options->lookup_value("export-png-compression", compression);
        const char *begin = compression.raw().c_str();
        char *end;
        long ival = strtol(begin, &end, 10);
        if (end == begin || *end != '\0' || errno == ERANGE) {
            std::cerr << "Cannot parse integer value "
                      << compression
                      << " for --export-png-compression; the default value "
                      <<  _file_export.export_png_compression
                      << " will be used"
                      << std::endl;
        }
        else {
            _file_export.export_png_compression = (int) ival;
        }
    }

    // FIXME: Upstream bug means INT is ignored if set to 0 so doesn't exist in options
    if (options->contains("export-png-antialias")) {
        Glib::ustring antialias;
        options->lookup_value("export-png-antialias", antialias);
        const char *begin = antialias.raw().c_str();
        char *end;
        long ival = strtol(begin, &end, 10);
        if (end == begin || *end != '\0' || errno == ERANGE) {
            std::cerr << "Cannot parse integer value "
                      << antialias
                      << " for --export-png-antialias; the default value "
                      <<  _file_export.export_png_antialias
                      << " will be used"
                      << std::endl;
        }
        else {
            _file_export.export_png_antialias = (int) ival;
        }
    }
    
    if (use_active_window) {
        _gio_application->register_application();
        if (!_gio_application->get_default()->is_remote()) {
#ifdef __APPLE__
            std::cerr << "Active window is not available in MAC" << std::endl;
#else
            std::cerr << "No active desktop to run" << std::endl;
#endif
            return EXIT_SUCCESS;
        }
        if (_use_shell) {
            shell(true);
        } else {
            _command_line_actions.emplace(_command_line_actions.begin(), "active-window-start", base);
            _command_line_actions_input = _command_line_actions_input + ";active-window-end";
            unlink(get_active_desktop_commands_location().c_str());
            parse_actions(_command_line_actions_input, _command_line_actions);
            activate_any_actions(_command_line_actions, _gio_application, _active_window, _active_document);
            redirect_output();
        }
        return EXIT_SUCCESS;
    }
    GVariantDict *options_copy = options->gobj_copy();
    GVariant *options_var = g_variant_dict_end(options_copy);
    if (g_variant_get_size(options_var) != 0) {
        _use_command_line_argument = true;
    }
    g_variant_dict_unref(options_copy);
    g_variant_unref(options_var);

    return -1; // Keep going
}

//   ========================  Actions  =========================

void
InkscapeApplication::on_new()
{
    create_window();
}

void
InkscapeApplication::on_quit()
{
    if (gtk_app()) {
        if (!destroy_all()) return; // Quit aborted.
        // For mac, ensure closing the gtk_app windows
        for (auto window : gtk_app()->get_windows()) {
            window->close();
        }
    }

    gio_app()->quit();
}

/*
 * Quit without checking for data loss.
 */
void
InkscapeApplication::on_quit_immediate()
{
    gio_app()->quit();
}

void
InkscapeApplication::print_action_list()
{
    auto const *gapp = gio_app();

    auto actions = gapp->list_actions();
    std::sort(actions.begin(), actions.end());
    for (auto const &action : actions) {
        Glib::ustring fullname("app." + action);
        std::cout << std::left << std::setw(20) << action
                  << ":  " << _action_extra_data.get_tooltip_for_action(fullname) << std::endl;
    }
}

/**
 * Prints file type extensions (without leading dot) of input formats.
 */
void InkscapeApplication::print_input_type_list() const
{
    Inkscape::Extension::DB::InputList extension_list;
    Inkscape::Extension::db.get_input_list(extension_list);

    for (auto *imod : extension_list) {
        auto suffix = imod->get_extension();
        if (suffix[0] == '.') {
            ++suffix;
        }
        std::cout << suffix << std::endl;
    }
}

/**
 * Return number of open Inkscape Windows (irrespective of number of documents)
.*/
int InkscapeApplication::get_number_of_windows() const {
    if (_with_gui) {
        return std::accumulate(_documents.begin(), _documents.end(), 0,
          [&](int sum, auto& v){ return sum + static_cast<int>(v.second.size()); });
    }
    return 0;
}

/**
 * Adds effect to Gio::Actions
 *
 *  \c effect is Filter or Extension
 *  \c show_prefs is used to show preferences dialog
*/
void action_effect(Inkscape::Extension::Effect* effect, bool show_prefs) {
    auto desktop = InkscapeApplication::instance()->get_active_desktop();
    if (effect->_workingDialog && show_prefs) {
        effect->prefs(desktop);
    } else {
        effect->effect(desktop);
    }
}

// Modifying string to get submenu id
std::string action_menu_name(std::string menu) {
    transform(menu.begin(), menu.end(), menu.begin(), ::tolower);
    for (auto &x:menu) {
        if (x==' ') {
            x = '-';
        }
    }
    return menu;
}

void InkscapeApplication::init_extension_action_data() {
    for (auto effect : Inkscape::Extension::db.get_effect_list()) {

        std::string aid = effect->get_sanitized_id();
        std::string action_id = "app." + aid;

        auto app = this;
        if (auto gapp = gtk_app()) {
            auto action = gapp->add_action(aid, [effect](){ action_effect(effect, true); });
            auto action_noprefs = gapp->add_action(aid + ".noprefs", [effect](){ action_effect(effect, false); });
            _effect_actions.emplace_back(action);
            _effect_actions.emplace_back(action_noprefs);
        }

        if (effect->hidden_from_menu()) continue;

        // Submenu retrieval as a list of strings (to handle nested menus).
        auto sub_menu_list = effect->get_menu_list();

        // Setting initial value of description to name of action in case there is no description
        auto description = effect->get_menu_tip();
        if (description.empty()) description = effect->get_name();

        if (effect->is_filter_effect()) {
            std::vector<std::vector<Glib::ustring>>raw_data_filter =
                {{ action_id, effect->get_name(), "Filters", description },
                { action_id + ".noprefs", Glib::ustring(effect->get_name()) + " " + _("(No preferences)"), "Filters (no prefs)", description }};
            app->get_action_extra_data().add_data(raw_data_filter);
        } else {
            std::vector<std::vector<Glib::ustring>>raw_data_effect =
                {{ action_id, effect->get_name(), "Extensions", description },
                { action_id + ".noprefs", Glib::ustring(effect->get_name()) + " " + _("(No preferences)"), "Extensions (no prefs)", description }};
            app->get_action_extra_data().add_data(raw_data_effect);
        }

#if false // enable to see all the loaded effects
        std::cout << " Effect: name:  " << effect->get_name();
        std::cout << "  id: " << aid.c_str();
        std::cout << "  menu: ";
        for (auto sub_menu : sub_menu_list) {
            std::cout << "|" << sub_menu.raw(); // Must use raw() as somebody has messed up encoding.
        }
        std::cout << "|  icon: " << effect->find_icon_file();
        std::cout << std::endl;
#endif

        // Add submenu to effect data
        gchar *ellipsized_name = effect->takes_input() ? g_strdup_printf(_("%s..."), effect->get_name()) : nullptr;
        Glib::ustring menu_name = ellipsized_name ? ellipsized_name : effect->get_name();
        bool is_filter = effect->is_filter_effect();
        app->get_action_effect_data().add_data(aid, is_filter, sub_menu_list, menu_name);
        g_free(ellipsized_name);
    }
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
