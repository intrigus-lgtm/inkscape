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

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <giomm/liststore.h>
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
class Shortcut;
class Widget;
class Window;
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
    Glib::RefPtr<Gio::ListStore<Gtk::Shortcut>> get_liststore() { return _liststore; }

    // User shortcuts
    bool add_user_shortcut(Glib::ustring const &detailed_action_name, Gtk::AccelKey const &trigger);
    bool remove_user_shortcut(Glib::ustring const &detailed_action_name);
    bool clear_user_shortcuts();
    bool is_user_set(Glib::ustring const &detailed_action_name);
    bool write_user();

    void update_gui_text_recursive(Gtk::Widget* widget);

    // Invoke action corresponding to key
    bool invoke_action(Gtk::AccelKey const &shortcut);
    bool invoke_action(KeyEvent const &event);
    bool invoke_action(GtkEventControllerKey const *controller,
                       unsigned keyval, unsigned keycode, GdkModifierType state);

    // Utility
    [[nodiscard]] std::vector<Glib::ustring> get_triggers(Glib::ustring const &action_name) const;
    [[nodiscard]] std::vector<Glib::ustring> get_actions(Glib::ustring const &trigger) const;

    static Glib::ustring get_label(const Gtk::AccelKey& shortcut);
    /// Controller provides the group. It can be nullptr; if so, we use group 0.
    static Gtk::AccelKey get_from(GtkEventControllerKey const *controller,
                                  unsigned keyval, unsigned keycode, GdkModifierType state,
                                  bool fix = false);
    static Gtk::AccelKey get_from_event(KeyEvent const &event, bool fix = false);

    std::vector<Glib::ustring> list_all_detailed_action_names();
    std::vector<Glib::ustring> list_all_actions();

    static std::vector<std::pair<Glib::ustring, std::string>> get_file_names();

    // Dialogs
    bool import_shortcuts();
    bool export_shortcuts();

    // Signals
    sigc::connection connect_changed(sigc::slot<void ()> const &slot);

private:

    // File
    bool _read (Glib::RefPtr<Gio::File> const &file, bool user_set = false);
    void _read (XML::Node const &keysnode, bool user_set);
    bool _write(Glib::RefPtr<Gio::File> const &file, What what     = User );

    // Add/remove shortcuts
    bool _add_shortcut(Glib::ustring const &detailed_action_name,
                       Glib::ustring const &trigger_string, bool user);
    bool _remove_shortcuts(Glib::ustring const &detailed_action_name);
    bool _remove_shortcut_trigger(Glib::ustring const& trigger);
    void _clear();

    // Debug
    void _dump();
    void _dump_all_recursive(Gtk::Widget* widget);

    // --- Variables ----

    // There can be more than one shortcut for each action. Using Gtk::ShortcutControllers,
    // we need to add each shortcut by itself (or we are limited to two shortcuts).
    struct ShortcutValue final {
        Glib::ustring trigger_string;
        Glib::RefPtr<Gtk::Shortcut> shortcut;
        bool user_set = false;
    };
    // Key is detailed action name.
    std::unordered_multimap<std::string, ShortcutValue> _shortcuts;

    // Gio::Actions
    Gtk::Application * const app;

    // Common liststore for all shortcut controllers.
    Glib::RefPtr<Gio::ListStore<Gtk::Shortcut>> _liststore;

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
