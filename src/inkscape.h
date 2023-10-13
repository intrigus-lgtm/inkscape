// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_INKSCAPE_H
#define SEEN_INKSCAPE_H

/*
 * Interface to main application
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Liam P. White <inkscapebrony@gmail.com>
 *
 * Copyright (C) 1999-2014 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <map>
#include <string>
#include <vector>

#include <gdk/gdk.h>  // GDK_ALT_MASK
#include <glib-object.h>
#include <glib.h>
#include <sigc++/signal.h>

class SPDesktop;
class SPDocument;
struct SPColor;

namespace Inkscape {

class Application;
class Selection;

namespace UI {
class ThemeContext;
namespace Tools {

class ToolBase;

} // namespace Tools
} // namespace UI

namespace XML {
class Node;
struct Document;
} // namespace XML

} // namespace Inkscape

void inkscape_ref  (Inkscape::Application & in);
void inkscape_unref(Inkscape::Application & in);

#define INKSCAPE (Inkscape::Application::instance())
#define SP_ACTIVE_DOCUMENT (INKSCAPE.active_document())
#define SP_ACTIVE_DESKTOP (INKSCAPE.active_desktop())

namespace Inkscape {

class Application {
public:
    static Application& instance();
    static bool exists();
    static void create(bool use_gui);
    
    // returns the mask of the keyboard modifier to map to Alt, zero if no mapping
    // Needs to be a guint because gdktypes.h does not define a 'no-modifier' value
    guint mapalt() const { return _mapalt; }
    
    // Sets the keyboard modifier to map to Alt. Zero switches off mapping, as does '1', which is the default 
    void mapalt(guint maskvalue);
    
    guint trackalt() const { return _trackalt; }
    void trackalt(guint trackvalue) { _trackalt = trackvalue; }

    bool use_gui() const { return _use_gui; }
    void use_gui(gboolean guival) { _use_gui = guival; }

    // no setter for this -- only we can control this variable
    static bool isCrashing() { return _crashIsHappening; }

    // useful functions
    void application_init (gboolean use_gui);
    void load_config (const gchar *filename, Inkscape::XML::Document *config, const gchar *skeleton, 
                      unsigned int skel_size, const gchar *e_notreg, const gchar *e_notxml, 
                      const gchar *e_notsp, const gchar *warn);

    Inkscape::UI::Tools::ToolBase * active_event_context();
    SPDocument * active_document();
    SPDesktop * active_desktop();
    bool sole_desktop_for_document(SPDesktop const &desktop);

    Inkscape::UI::ThemeContext *themecontext = nullptr;
    
    // Inkscape desktop stuff
    void add_desktop(SPDesktop * desktop);
    void remove_desktop(SPDesktop* desktop);
    void activate_desktop (SPDesktop * desktop);
    void switch_desktops_next ();
    void switch_desktops_prev ();
    void get_all_desktops (std::list< SPDesktop* >& listbuf);
    void reactivate_desktop (SPDesktop * desktop);
    SPDesktop * find_desktop_by_dkey (unsigned int dkey);
    unsigned int maximum_dkey();
    SPDesktop * next_desktop ();
    SPDesktop * prev_desktop ();
    std::vector<SPDesktop *> * get_desktops() { return _desktops; };
    
    void external_change ();
    
    // Moved document add/remove functions into public inkscape.h as they are used
    // (rightly or wrongly) by console-mode functions
    void add_document (SPDocument *document);
    bool remove_document (SPDocument *document);
    
    // fixme: This also
    void exit ();
    
    static void crash_handler(int signum);

    // nobody should be accessing our reference count, so it's made private.
    friend void ::inkscape_ref  (Application & in);
    friend void ::inkscape_unref(Application & in);

    // signals
    
    // one of selections changed
    sigc::signal<void (Inkscape::Selection *)> signal_selection_changed;
    // one of subselections (text selection, gradient handle, etc) changed
    sigc::signal<void (SPDesktop *)> signal_subselection_changed;
    // one of selections modified
    sigc::signal<void (Inkscape::Selection *, guint /*flags*/)> signal_selection_modified;
    // one of selections set
    sigc::signal<void (Inkscape::Selection *)> signal_selection_set;
    // some desktop got focus
    sigc::signal<void (SPDesktop *)> signal_activate_desktop;
    // some desktop lost focus
    sigc::signal<void (SPDesktop *)> signal_deactivate_desktop;
    
    // these are orphaned signals (nothing emits them and nothing connects to them)
    sigc::signal<void (SPDocument *)> signal_destroy_document;
    sigc::signal<void (SPColor *, double /*opacity*/)> signal_color_set;
    
    // inkscape is quitting
    sigc::signal<void ()> signal_shut_down;
    // a document was changed by some external means (undo or XML editor); this
    // may not be reflected by a selection change and thus needs a separate signal
    sigc::signal<void ()> signal_external_change;

    void set_pdf_poppler(bool p) {
        _pdf_poppler = p;
    }
    bool get_pdf_poppler() {
        return _pdf_poppler;
    }
    void set_pdf_font_strategy(int mode) {
        _pdf_font_strategy = mode;
    }
    int get_pdf_font_strategy() {
        return _pdf_font_strategy;
    }
    void set_pages(const std::string &pages) {
        _pages = pages;
    }
    const std::string &get_pages() const {
        return _pages;
    }

  private:
    static Inkscape::Application * _S_inst;

    Application(bool use_gui);
    ~Application();

    Application(Application const&); // no copy
    Application& operator=(Application const&); // no assign
    Application* operator&() const; // no pointer access
    std::map<SPDocument *, int> _document_set;
    std::vector<SPDesktop *> *_desktops = nullptr;
    std::string _pages;

    unsigned refCount = 1;
    guint _mapalt = GDK_ALT_MASK;
    guint _trackalt = false;
    static bool _crashIsHappening;
    bool _use_gui = false;
    bool _pdf_poppler = false;
    int _pdf_font_strategy = 0;
};

} // namespace Inkscape

#endif

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
