// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Shortcuts
 *
 * Copyright (C) 2020 Tavmjong Bah
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 */

#ifndef INK_SHORTCUTS_H
#define INK_SHORTCUTS_H

#include <map>
#include <utility>
#include <vector>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtk/gtk.h> // GtkEventControllerKey
#include <gtkmm/accelkey.h>
#include <sigc++/connection.h>
#include <sigc++/signal.h>

namespace Gio {
class File;
} // namespace Gio;

namespace Gtk {
class Application;
class Widget;
} // namespace Gtk

namespace Inkscape {

struct KeyEvent;
namespace UI::View { class View; }

namespace XML {
class Document;
class Node;
} // namespace XML

struct accel_key_less final
{
    bool operator()(const Gtk::AccelKey& key1, const Gtk::AccelKey& key2) const
    {
        if(key1.get_key() < key2.get_key()) return true;
        if(key1.get_key() > key2.get_key()) return false;
        return (key1.get_mod() < key2.get_mod());
    }
};

class Shortcuts final {
public:
    enum What {
        All,
        System,
        User
    };
        
    static Shortcuts& getInstance()
    {
        static Shortcuts instance;

        if (!instance.initialized) {
            instance.init();
        }

        return instance;
    }
  
private:
    Shortcuts();
    ~Shortcuts();

public:
    Shortcuts(Shortcuts const&)      = delete;
    void operator=(Shortcuts const&) = delete;

    void init();
    void clear();

    bool read (Glib::RefPtr<Gio::File> const &file, bool user_set = false);
    bool write(Glib::RefPtr<Gio::File> const &file, What what     = User );
    bool write_user();

    bool is_user_set(Glib::ustring const &action);

    // Add/remove shortcuts
    bool add_shortcut(Glib::ustring const &name, Gtk::AccelKey const &shortcut, bool user);
    bool remove_shortcut(Glib::ustring const &name);
    Glib::ustring remove_shortcut(const Gtk::AccelKey& shortcut);

    // User shortcuts
    bool add_user_shortcut(Glib::ustring const &name, Gtk::AccelKey const &shortcut);
    bool remove_user_shortcut(Glib::ustring const &name);
    bool clear_user_shortcuts();

    // Invoke action corresponding to key
    bool invoke_action(Gtk::AccelKey const &shortcut);
    bool invoke_action(GtkEventControllerKey const *controller,
                       unsigned keyval, unsigned keycode, GdkModifierType state);
    bool invoke_action(KeyEvent const &event);

    // Utility
    sigc::connection connect_changed(sigc::slot<void ()> const &slot);
    static Glib::ustring get_label(const Gtk::AccelKey& shortcut);
    /// Controller provides the group. It can be nullptr; if so, we use group 0.
    static Gtk::AccelKey get_from(GtkEventControllerKey const *controller,
                                  unsigned keyval, unsigned keycode, GdkModifierType state,
                                  bool fix = false);
    static Gtk::AccelKey get_from_event(KeyEvent const &event, bool fix = false);
    std::vector<Glib::ustring> list_all_detailed_action_names();
    std::vector<Glib::ustring> list_all_actions();

    static std::vector<std::pair<Glib::ustring, std::string>> get_file_names();

    void update_gui_text_recursive(Gtk::Widget* widget);

    // Dialogs
    bool import_shortcuts();
    bool export_shortcuts();

    // Debug
    void dump();

    void dump_all_recursive(Gtk::Widget* widget);

private:
    // Gio::Actions
    Gtk::Application * const app;
    std::map<Glib::ustring, bool> action_user_set;

    void _read(XML::Node const &keysnode, bool user_set);

    bool initialized = false;
    sigc::signal<void ()> _changed;
};

} // Namespace Inkscape

#endif // INK_SHORTCUTS_H

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
