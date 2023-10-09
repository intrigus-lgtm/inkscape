// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Shortcuts
 *
 * Copyright (C) 2020 Tavmjong Bah
 * Rewrite of code (C) MenTalguY and others.
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 *
 */

/* Much of the complexity of this code is in dealing with both Inkscape verbs and Gio::Actions at
 * the same time. When we remove verbs we can avoid using 'unsigned long long int shortcut' to
 * track keys and rely directly on Glib::ustring as used by
 * Gtk::Application::get_accels_for_action(). This will then automatically handle the '<Primary>'
 * modifier value (which takes care of the differences between Linux and OSX) as well as allowing
 * us to set multiple accelerators for actions in InkscapePreferences. */

// TODO: GTK4: Replace Application.[un]set_accels*() with GtkShortcutController in CAPTURE phase.
// See: https://gitlab.com/dboles/inkscape/-/issues/1

#include "shortcuts.h"

#include <memory>
#include <numeric>
#include <iostream>
#include <iomanip>
#include <glibmm/convert.h>
#include <glibmm/i18n.h>
#include <glibmm/miscutils.h>
#include <glibmm/regex.h>
#include <glibmm/variant.h>
#include <giomm/file.h>
#include <giomm/simpleaction.h>
#include <gtkmm/accelerator.h>
#include <gtkmm/actionable.h>
#include <gtkmm/application.h>
#include <gtkmm/window.h>

#include "document.h"
#include "inkscape-application.h"
#include "inkscape-window.h"
#include "preferences.h"
#include "io/dir-util.h"
#include "io/resource.h"
#include "io/sys.h"
#include "ui/modifiers.h"
#include "ui/tools/tool-base.h"    // For latin keyval
#include "ui/dialog/filedialog.h"  // Importing/exporting files.
#include "ui/util.h"
#include "ui/widget/events/canvas-event.h"
#include "xml/simple-document.h"
#include "xml/node.h"
#include "xml/node-iterators.h"

using namespace Inkscape::IO::Resource;
using namespace Inkscape::Modifiers;

namespace Inkscape {

Shortcuts::Shortcuts()
{
    Glib::RefPtr<Gio::Application> gapp = Gio::Application::get_default();
    app = std::dynamic_pointer_cast<Gtk::Application>(gapp); // Save as we constantly use it.
    if (!app) {
        std::cerr << "Shortcuts::Shortcuts: No app! Shortcuts cannot be used without a Gtk::Application!" << std::endl;
    }
}

Shortcuts::~Shortcuts() = default;

void
Shortcuts::init() {
    initialized = true;

    // Clear arrays (we may be re-reading).
    clear();

    bool success = false; // We've read a shortcut file!
    std::string path;
  
    // ------------ Open Inkscape shortcut file ------------

    // Try filename from preferences first.
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    path = prefs->getString("/options/kbshortcuts/shortcutfile");
    if (!path.empty()) {
        bool absolute = true;
        if (!Glib::path_is_absolute(path)) {
            path = get_path_string(SYSTEM, KEYS, path.c_str());
            absolute = false;
        }

        Glib::RefPtr<Gio::File> file = Gio::File::create_for_path(path);
        success = read(file);
        if (!success) {
            std::cerr << "Shortcut::Shortcut: Unable to read shortcut file listed in preferences: " + path << std::endl;;
        }

        // Save relative path to "share/keys" if possible to handle parallel installations of
        // Inskcape gracefully.
        if (success && absolute) {
            auto const relative_path = sp_relative_path_from_path(path, get_path_string(SYSTEM, KEYS));
            prefs->setString("/options/kbshortcuts/shortcutfile", relative_path.c_str());
        }
    }

    if (!success) {
        // std::cerr << "Shortcut::Shortcut: " << reason << ", trying default.xml" << std::endl;

        Glib::RefPtr<Gio::File> file = Gio::File::create_for_path(get_path_string(SYSTEM, KEYS, "default.xml"));
        success = read(file);
    }
  
    if (!success) {
        std::cerr << "Shortcut::Shortcut: Failed to read file default.xml, trying inkscape.xml" << std::endl;

        Glib::RefPtr<Gio::File> file = Gio::File::create_for_path(get_path_string(SYSTEM, KEYS, "inkscape.xml"));
        success = read(file);
    }

    if (!success) {
        std::cerr << "Shortcut::Shortcut: Failed to read file inkscape.xml; giving up!" << std::endl;
    }

    // ------------ Open Shared shortcut file -------------
    Glib::RefPtr<Gio::File> file = Gio::File::create_for_path(get_path_string(SHARED, KEYS, "default.xml"));
    // Test if file exists before attempting to read to avoid generating warning message.
    if (file->query_exists()) {
        read(file, true);
    }
    // ------------ Open User shortcut file -------------
    file = Gio::File::create_for_path(get_path_string(USER, KEYS, "default.xml"));
    // Test if file exists before attempting to read to avoid generating warning message.
    if (file->query_exists()) {
        read(file, true);
    }

    // dump();
}

// Clear all shortcuts
void
Shortcuts::clear()
{
    // Actions: We rely on Gtk for everything except user/system setting.
    for (auto const &action_description : app->list_action_descriptions()) {
        app->unset_accels_for_action(action_description);
    }
    action_user_set.clear();
}

/**  Trigger action from a shortcut. Useful if we want to intercept the event from GTK */
bool
Shortcuts::invoke_action(Gtk::AccelKey const &shortcut)
{
    // This can be simplified in GTK4.
    Glib::ustring accel = Gtk::Accelerator::name(shortcut.get_key(), shortcut.get_mod());
    std::vector<Glib::ustring> actions = app->get_actions_for_accel(accel);
    if (!actions.empty()) {
        Glib::ustring const &action = actions[0];
        Glib::ustring action_name;
        Glib::VariantBase value;
        Gio::SimpleAction::parse_detailed_name_variant(action.substr(4), action_name, value);
        if (action.compare(0, 4, "app.") == 0) {
            app->activate_action(action_name, value);
            return true;
        } else if (action.compare(0, 4, "win.") == 0) {
            auto window = dynamic_cast<InkscapeWindow *>(app->get_active_window());
            if (window) {
                window->activate_action(action_name, value);
                return true;
            }
        }
    }
    return false;
}

/**  Trigger action from a shortcut. Useful if we want to intercept the event from GTK */
bool Shortcuts::invoke_action(KeyEvent const &event)
{
    auto const shortcut = get_from_event(event);
    return invoke_action(shortcut);
}

/**  Trigger action from a shortcut. Useful if we want to intercept the event from GTK */
bool
Shortcuts::invoke_action(GtkEventControllerKey const * const controller,
                         unsigned const keyval, unsigned const keycode,
                         GdkModifierType const state)
{
    auto const shortcut = get_from(controller, keyval, keycode, state);
    return invoke_action(shortcut);
}

Gdk::ModifierType
parse_modifier_string(char const * const modifiers_string)
{
    Gdk::ModifierType modifiers(Gdk::ModifierType(0));
    if (modifiers_string) {
        std::vector<Glib::ustring> mod_vector = Glib::Regex::split_simple("\\s*,\\s*", modifiers_string);
  
        for (auto const &mod : mod_vector) {
            if (mod == "Control" || mod == "Ctrl") {
                modifiers |= Gdk::CONTROL_MASK;
            } else if (mod == "Shift") {
                modifiers |= Gdk::SHIFT_MASK;
            } else if (mod == "Alt") {
                modifiers |= Gdk::ALT_MASK;
            } else if (mod == "Super") {
                modifiers |= Gdk::SUPER_MASK; // Not used
            } else if (mod == "Hyper") {
                modifiers |= Gdk::HYPER_MASK; // Not used
            } else if (mod == "Meta") {
                modifiers |= Gdk::META_MASK;
            } else if (mod == "Primary") {
                // System dependent key to invoke menus. (Needed for OSX in particular.)
                // We only read "Primary" and never write it.
                auto display = Gdk::Display::get_default();
                if (display) {
                    GdkKeymap* keymap = display->get_keymap();
                    GdkModifierType type = 
                        gdk_keymap_get_modifier_mask (keymap, GDK_MODIFIER_INTENT_PRIMARY_ACCELERATOR);
                    gdk_keymap_add_virtual_modifiers(keymap, &type);
                    if (type & GDK_CONTROL_MASK)
                        modifiers |= Gdk::CONTROL_MASK;
                    else if (type & GDK_META_MASK)
                        modifiers |= Gdk::META_MASK;
                    else {
                        std::cerr << "Shortcut::read: Unknown primary accelerator!" << std::endl;
                        modifiers |= Gdk::CONTROL_MASK;
                    }
                } else {
                    modifiers |= Gdk::CONTROL_MASK;
                }
            } else {
                std::cerr << "Shortcut::read: Unknown GDK modifier: " << mod.c_str() << std::endl;
            }
        }
    }
    return modifiers;
}

// Read a shortcut file.
bool
Shortcuts::read(Glib::RefPtr<Gio::File> file, bool user_set)
{
    if (!file->query_exists()) {
        std::cerr << "Shortcut::read: file does not exist: " << file->get_path() << std::endl;
        return false;
    }

    XML::Document *document = sp_repr_read_file(file->get_path().c_str(), nullptr, true);
    if (!document) {
        std::cerr << "Shortcut::read: could not parse file: " << file->get_path() << std::endl;
        return false;
    }

    XML::NodeConstSiblingIterator iter = document->firstChild();
    for ( ; iter ; ++iter ) { // We iterate in case of comments.
        if (strcmp(iter->name(), "keys") == 0) {
            break;
        }
    }

    if (!iter) {
        std::cerr << "Shortcuts::read: File in wrong format: " << file->get_path() << std::endl;
        return false;
    }

    // Loop through the children in <keys> (may have nested keys)
    _read(*iter, user_set);

    return true;
}

/**
 * Recursively reads shortcuts from shortcut file.
 *
 * @param keysnode The <keys> element. Its child nodes will be processed.
 * @param user_set true if reading from user shortcut file
 */
void
Shortcuts::_read(XML::Node const &keysnode, bool user_set)
{
    XML::NodeConstSiblingIterator iter {keysnode.firstChild()};
    for ( ; iter ; ++iter ) {
        if (strcmp(iter->name(), "modifier") == 0) {
            char const * const mod_name = iter->attribute("action");
            if (!mod_name) {
                std::cerr << "Shortcuts::read: Missing modifier for action!" << std::endl;;
                continue;
            }

            Modifier *mod = Modifier::get(mod_name);
            if (mod == nullptr) {
                std::cerr << "Shortcuts::read: Can't find modifier: " << mod_name << std::endl; 
                continue;
            }
 
            // If mods isn't specified then it should use default, if it's an empty string
            // then the modifier is None (i.e. happens all the time without a modifier)
            KeyMask and_modifier = NOT_SET;
            char const * const mod_attr = iter->attribute("modifiers");
            if (mod_attr) {
                and_modifier = (KeyMask) parse_modifier_string(mod_attr);
            }

            // Parse not (cold key) modifier
            KeyMask not_modifier = NOT_SET;
            char const * const not_attr = iter->attribute("not_modifiers");
            if (not_attr) {
                not_modifier = (KeyMask) parse_modifier_string(not_attr);
            }

            char const * const disabled_attr = iter->attribute("disabled");
            if (disabled_attr && strcmp(disabled_attr, "true") == 0) {
                and_modifier = NEVER;
            }

            if (and_modifier != NOT_SET) {
                if(user_set) {
                    mod->set_user(and_modifier, not_modifier);
                } else {
                    mod->set_keys(and_modifier, not_modifier);
                }
            }
            continue;
        } else if (strcmp(iter->name(), "keys") == 0) {
            _read(*iter, user_set);
            continue;
        } else if (strcmp(iter->name(), "bind") != 0) {
            // Unknown element, do not complain.
            continue;
        }

        // Gio::Action's
        char const * const gaction = iter->attribute("gaction");
        char const * const keys    = iter->attribute("keys");
        if (gaction && keys) {

            // Trim leading spaces
            Glib::ustring Keys = keys;
            auto p = Keys.find_first_not_of(" ");
            Keys = Keys.erase(0, p);

            std::vector<Glib::ustring> key_vector = Glib::Regex::split_simple("\\s*,\\s*", Keys);
            // Set one shortcut at a time so we can check if it has been previously used.
            for (auto const &key : key_vector) {
                add_shortcut(gaction, key, user_set);
            }

            // Uncomment to see what the cat dragged in.
            // if (!key_vector.empty()) {
            //     std::cout << "Shortcut::read: gaction: "<< gaction
            //               << ", user set: " << std::boolalpha << user_set << ", ";
            //     for (auto const &key : key_vector) {
            //         std::cout << key << ", ";
            //     }
            //     std::cout << std::endl;
            // }

            continue;
        }
    }
}

bool
Shortcuts::write_user() {
    Glib::RefPtr<Gio::File> file = Gio::File::create_for_path(get_path_string(USER, KEYS, "default.xml"));
    return write(file, User);
}

[[nodiscard]] static Glib::ustring
join(std::vector<Glib::ustring> const &accels, char const separator)
{
    auto const capacity = std::accumulate(accels.begin(), accels.end(), std::size_t{0},
        [](std::size_t capacity, auto const &accel){ return capacity += accel.size() + 1; });
    Glib::ustring result;
    result.reserve(capacity);
    for (auto const &accel: accels) {
        if (!result.empty()) result += separator;
        result += accel;
    }
    return result;
}

// In principle, we only write User shortcuts. But for debugging, we might want to write something else.
bool
Shortcuts::write(Glib::RefPtr<Gio::File> file, What what) {

    auto *document = new XML::SimpleDocument();
    XML::Node * node = document->createElement("keys");
    switch (what) {
        case User:
            node->setAttribute("name", "User Shortcuts");
            break;
        case System:
            node->setAttribute("name", "System Shortcuts");
            break;
        default:
            node->setAttribute("name", "Inkscape Shortcuts");
    }

    document->appendChild(node);

    // Actions: write out all actions with accelerators.
    for (auto const &action_name : list_all_detailed_action_names()) {
        if ( what == All                                 ||
            (what == System && !action_user_set[action_name]) ||
            (what == User   &&  action_user_set[action_name]) )
        {
            std::vector<Glib::ustring> accels = app->get_accels_for_action(action_name);
            if (!accels.empty()) {
                XML::Node * node = document->createElement("bind");

                node->setAttribute("gaction", action_name);

                auto const keys = join(accels, ',');
                node->setAttribute("keys", keys);

                document->root()->appendChild(node);
            }
        }
    }

    for(auto modifier: Inkscape::Modifiers::Modifier::getList()) {
        if (what == User && modifier->is_set_user()) {
            XML::Node * node = document->createElement("modifier");
            node->setAttribute("action", modifier->get_id());

            if (modifier->get_config_user_disabled()) {
                node->setAttribute("disabled", "true");
            } else {
                node->setAttribute("modifiers", modifier->get_config_user_and());
                auto not_mask = modifier->get_config_user_not();
                if (!not_mask.empty() and not_mask != "-") {
                    node->setAttribute("not_modifiers", not_mask);
                }
            }

            document->root()->appendChild(node);
        }
    }

    sp_repr_save_file(document, file->get_path().c_str(), nullptr);
    GC::release(document);

    return true;
};

// Return if user set shortcut for Gio::Action.
bool
Shortcuts::is_user_set(Glib::ustring const &action)
{
    auto it = action_user_set.find(action);
    if (it != action_user_set.end()) {
        return it->second;
    } else {
        return false;
    }
}

// Get a list of detailed action names (as defined in action extra data).
// This is more useful for shortcuts than a list of all actions.
std::vector<Glib::ustring>
Shortcuts::list_all_detailed_action_names()
{
    auto *iapp = InkscapeApplication::instance();
    InkActionExtraData& action_data = iapp->get_action_extra_data();
    return action_data.get_actions();
}

// Get a list of all actions (application, window, and document), properly prefixed.
// We need to do this ourselves as Gtk::Application does not have a function for this.
std::vector<Glib::ustring>
Shortcuts::list_all_actions()
{
    std::vector<Glib::ustring> all_actions;

    auto actions = app->list_actions();
    std::sort(actions.begin(), actions.end());
    for (auto &&action: std::move(actions)) {
        all_actions.push_back("app." + std::move(action));
    }

    auto gwindow = app->get_active_window();
    auto window = dynamic_cast<InkscapeWindow *>(gwindow);
    if (window) {
        actions = window->list_actions();
        std::sort(actions.begin(), actions.end());
        for (auto &&action: std::move(actions)) {
            all_actions.push_back("win." + std::move(action));
        }

        auto document = window->get_document();
        if (document) {
            auto map = document->getActionGroup();
            if (map) {
                actions = map->list_actions();
                std::sort(actions.begin(), actions.end());
                for (auto &&action: std::move(actions)) {
                    all_actions.push_back("doc." + std::move(action));
                }
            } else {
                std::cerr << "Shortcuts::list_all_actions: No document map!" << std::endl;
            }
        }
    }

    return all_actions;
}

// Add a shortcut, removing any previous use of shortcut.
bool
Shortcuts::add_shortcut(Glib::ustring const &name, const Gtk::AccelKey& shortcut, bool const user)
{
    // Remove previous use of shortcut (already removed if new user shortcut).
    if (auto const &old_name = remove_shortcut(shortcut); !old_name.empty()) {
        // std::cerr << "Shortcut::add_shortcut: duplicate shortcut found for: " << shortcut.get_abbrev().raw()
                //   << "  Old: " << old_name.raw() << "  New: " << name.raw() << " !" << std::endl;
    }

    // Add shortcut

    // To see if action exists, We need to compare action names without values...
    Glib::ustring action_name_new;
    Glib::VariantBase value_new;
    Gio::SimpleAction::parse_detailed_name_variant(name, action_name_new, value_new);

    for (auto const &action : list_all_detailed_action_names()) {
        Glib::ustring action_name_old;
        Glib::VariantBase value_old;
        Gio::SimpleAction::parse_detailed_name_variant(action, action_name_old, value_old);

        if (action_name_new == action_name_old) {
            std::vector<Glib::ustring> accels;
            // Action exists, add shortcut to list of shortcuts, if it's not a user shortcut.
            // If it is a user-defined shortcut, then it replaces any defaults that might have been present.
            // That's what we show in the UI when we define shortcuts (only new one) and that's also
            // the only way to let user "overwrite" default shortcut, as there's no removal possible.
            if (!user) {
                accels = app->get_accels_for_action(name);
            }
            accels.push_back(shortcut.get_abbrev());
            app->set_accels_for_action(name, accels);
            action_user_set[name] = user;
            _changed.emit();
            return true;
        }
    }

    // Oops, not an action!
    std::cerr << "Shortcuts::add_shortcut: No Action for " << name.raw() << std::endl;
    return false;
}

// Add a user shortcut, updating user's shortcut file if successful.
bool
Shortcuts::add_user_shortcut(Glib::ustring const &name, const Gtk::AccelKey& shortcut)
{
    // Remove previous shortcut(s) for action.
    remove_shortcut(name);

    // Remove previous use of shortcut from other actions.
    remove_shortcut(shortcut);

    // Add shortcut, if successful, save to file.
    if (add_shortcut(name, shortcut, true)) {  // Always user.
        // Save
        return write_user();
    }

    std::cerr << "Shortcut::add_user_shortcut: Failed to add: " << name.raw() << " with shortcut " << shortcut.get_abbrev().raw() << std::endl;
    return false;
};

// Remove a shortcut via key. Return name of removed action.
Glib::ustring
Shortcuts::remove_shortcut(const Gtk::AccelKey& shortcut)
{
    std::vector<Glib::ustring> actions = app->get_actions_for_accel(shortcut.get_abbrev());
    if (actions.empty()) {
        return Glib::ustring(); // No action, no pie.
    }

    Glib::ustring action_name;
    for (auto const &action : actions) {
        // Remove just the one shortcut, leaving the others intact.
        std::vector<Glib::ustring> accels = app->get_accels_for_action(action);
        auto it = std::find(accels.begin(), accels.end(), shortcut.get_abbrev());
        if (it != accels.end()) {
            action_name = action;
            accels.erase(it);
            app->set_accels_for_action(action, accels);
            _changed.emit();
        }
    }

    return action_name;
}

// Remove a shortcut via action name.
bool
Shortcuts::remove_shortcut(Glib::ustring const &name)
{
    for (auto const &action : list_all_detailed_action_names()) {
        if (action == name) {
            // Action exists
            app->unset_accels_for_action(action);
            action_user_set.erase(action);
            _changed.emit();
            return true;
        }
    }

    return false;
}

// Remove a user shortcut, updating user's shortcut file.
bool
Shortcuts::remove_user_shortcut(Glib::ustring const &name)
{
    // Check if really user shortcut.
    bool user_shortcut = is_user_set(name);

    if (!user_shortcut) {
        // We don't allow removing non-user shortcuts.
        return false;
    }

    if (remove_shortcut(name)) {
        // Save
        write_user();

        // Reread to get original shortcut (if any).
        init();
        return true;
    }

    std::cerr << "Shortcuts::remove_user_shortcut: Failed to remove shortcut for: " << name.raw() << std::endl;
    return false;
}

// Remove all user's shortcuts (simply overwrites existing file).
bool
Shortcuts::clear_user_shortcuts()
{
    // Create new empty document and save
    auto *document = new XML::SimpleDocument();
    XML::Node * node = document->createElement("keys");
    node->setAttribute("name", "User Shortcuts");
    document->appendChild(node);
    Glib::RefPtr<Gio::File> file = Gio::File::create_for_path(get_path_string(USER, KEYS, "default.xml"));
    sp_repr_save_file(document, file->get_path().c_str(), nullptr);
    GC::release(document);
    
    // Re-read everything!
    init();
    _changed.emit();
    return true;
}

Glib::ustring
Shortcuts::get_label(const Gtk::AccelKey& shortcut)
{
    Glib::ustring label;

    if (!shortcut.is_null()) {
        // ::get_label shows key pad and numeric keys identically.
        // TODO: Results in labels like "Numpad Alt+5"
        if (shortcut.get_abbrev().find("KP") != Glib::ustring::npos) {
            label += _("Numpad");
            label += " ";
        }

        label += Gtk::Accelerator::get_label(shortcut.get_key(), shortcut.get_mod());
    }

    return label;
}

static Gtk::AccelKey
get_from_event_impl(unsigned const event_keyval, unsigned const event_keycode,
                    GdkModifierType const event_state, unsigned const event_group,
                    bool const fix)
{
    // MOD2 corresponds to the NumLock key. Masking it out allows
    // shortcuts to work regardless of its state.
    auto const initial_modifiers = static_cast<Gdk::ModifierType>(event_state & ~GDK_MOD2_MASK);

    unsigned int consumed_modifiers = 0;
    auto keyval = Inkscape::UI::Tools::get_latin_keyval_impl(
        event_keyval, event_keycode, event_state, event_group, &consumed_modifiers);

    // If a key value is "convertible", i.e. it has different lower case and upper case versions,
    // convert to lower case and don't consume the "shift" modifier.
    bool is_case_convertible = !(gdk_keyval_is_upper(keyval) && gdk_keyval_is_lower(keyval));
    if (is_case_convertible) {
        keyval = gdk_keyval_to_lower(keyval);
        consumed_modifiers &= ~ Gdk::SHIFT_MASK;
    }

    // The InkscapePreferences dialog returns an event structure where the Shift modifier is not
    // set for keys like '('. This causes '(' to be converted to '9' by get_latin_keyval. It also
    // returns 'Shift-k' for 'K' (instead of 'Shift-K') but this is not a problem.
    // We fix this by restoring keyval to its original value.
    if (fix) {
        keyval = event_keyval;
    }

    auto unused_modifiers = Gdk::ModifierType((initial_modifiers &~ consumed_modifiers)
                                                                 & GDK_MODIFIER_MASK
                                                                 &~ GDK_LOCK_MASK);

    // std::cout << "Shortcuts::get_from_event: End:   "
    //           << " Key: " << std::hex << keyval << " (" << (char)keyval << ")"
    //           << " Mod: " << std::hex << unused_modifiers << std::endl;
    return (Gtk::AccelKey(keyval, unused_modifiers));
}

/**
 * Return: keyval translated to group 0 in lower 32 bits, modifier encoded in upper 32 bits.
 *
 * Usage of group 0 (i.e. the main, typically English layout) instead of simply event->keyval
 * ensures that shortcuts work regardless of the active keyboard layout (e.g. Cyrillic).
 *
 * The returned modifiers are the modifiers that were not "consumed" by the translation and
 * can be used by the application to define a shortcut, e.g.
 *  - when pressing "Shift+9" the resulting character is "(";
 *    the shift key was "consumed" to make this character and should not be part of the shortcut
 *  - when pressing "Ctrl+9" the resulting character is "9";
 *    the ctrl key was *not* consumed to make this character and must be included in the shortcut
 *  - Exception: letter keys like [A-Z] always need the shift modifier,
 *               otherwise lower case and uper case keys are treated as equivalent.
 */
Gtk::AccelKey
Shortcuts::get_from(GtkEventControllerKey const * const controller,
                    unsigned const keyval, unsigned const keycode, GdkModifierType const state,
                    bool const fix)
{
    auto const group = controller ? gtk_event_controller_key_get_group(const_cast<GtkEventControllerKey*>(controller)) : 0u;
    return get_from_event_impl(keyval, keycode, state, group, fix);
}

Gtk::AccelKey Shortcuts::get_from_event(KeyEvent const &event, bool fix)
{
    return get_from_event_impl(event.keyval, event.keycode,
                               static_cast<GdkModifierType>(event.modifiers), event.group, fix);
}

template <typename T>
static void append(std::vector<T> &target, std::vector<T> &&source)
{
    target.insert(target.end(), std::move_iterator{source.begin()}, std::move_iterator{source.end()});
}

// Get a list of filenames to populate menu
std::vector<std::pair<Glib::ustring, std::string>>
Shortcuts::get_file_names()
{
    using namespace Inkscape::IO::Resource;

    // Make a list of all key files from System and User.  Glib::ustring should be std::string!
    auto filenames = get_filenames(SYSTEM, KEYS, {".xml"});
    // Exclude default.xml as it only contains user modifications.
    append(filenames, get_filenames(SHARED, KEYS, {".xml"}, {"default.xml"}));
    append(filenames, get_filenames(USER  , KEYS, {".xml"}, {"default.xml"}));

    // Check file exists and extract out label if it does.
    std::vector<std::pair<Glib::ustring, std::string>> names_and_paths;
    for (auto const &filename : filenames) {
        Glib::ustring label = Glib::path_get_basename(filename.raw());
        auto filename_relative = sp_relative_path_from_path(filename, get_path_string(SYSTEM, KEYS));

        XML::Document *document = sp_repr_read_file(filename.c_str(), nullptr, true);
        if (!document) {
            std::cerr << "Shortcut::get_file_names: could not parse file: " << filename << std::endl;
            continue;
        }

        XML::NodeConstSiblingIterator iter = document->firstChild();
        for ( ; iter ; ++iter ) { // We iterate in case of comments.
            if (strcmp(iter->name(), "keys") == 0) {
                char const * const name = iter->attribute("name");
                if (name) {
                    label = Glib::ustring::compose("%1 (%2)", name, label);
                }
                names_and_paths.emplace_back(std::move(label), std::move(filename_relative));
                break;
            }
        }
        if (!iter) {
            std::cerr << "Shortcuts::get_File_names: not a shortcut keys file: " << filename << std::endl;
        }

        Inkscape::GC::release(document);
    }

    // Sort by name
    std::sort(names_and_paths.begin(), names_and_paths.end(),
            [](auto const &pair1, auto const &pair2) {
                return pair1.first < pair2.first;
            });
    // But default.xml at top
    auto it_default = std::find_if(names_and_paths.begin(), names_and_paths.end(),
            [](auto const &pair) {
                return pair.second == "default.xml";
            });
    if (it_default != names_and_paths.end()) {
        std::rotate(names_and_paths.begin(), it_default, it_default+1);
    }

    return names_and_paths;
}

// void on_foreach(Gtk::Widget& widget) {
//     std::cout <<  "on_foreach: " << widget.get_name() << std::endl;;
// }

/*
 * Update text with shortcuts.
 * Inkscape includes shortcuts in tooltips and in dialog titles. They need to be updated
 * anytime a tooltip is changed.
 */
void
Shortcuts::update_gui_text_recursive(Gtk::Widget* widget)
{
    if (auto const actionable = dynamic_cast<Gtk::Actionable *>(widget)) {
        if (auto action = actionable->get_action_name(); !action.empty()) {
            Glib::ustring variant;
            if (auto const value = actionable->get_action_target_value()) {
                auto const type = value.get_type_string();
                if (type == "s") {
                    variant = static_cast<Glib::Variant<Glib::ustring> const &>(value).get();
                    action += "('" + variant + "')";
                } else if (type == "i") {
                    variant = std::to_string(static_cast<Glib::Variant<std::int32_t> const &>(value).get());
                    action += "(" + variant + ")";
                } else {
                    std::cerr << "Shortcuts::update_gui_text_recursive: unhandled variant type: " << type << std::endl;
                }
            }

            std::vector<Glib::ustring> accels = app->get_accels_for_action(action);

            Glib::ustring tooltip;
            auto *iapp = InkscapeApplication::instance();
            if (iapp) {
                tooltip = iapp->get_action_extra_data().get_tooltip_for_action(action, true, true);
            }

            // Add new primary accelerator.
            if (accels.size() > 0) {
                // Add space between tooltip and accel if there is a tooltip
                if (!tooltip.empty()) {
                    tooltip += " ";
                }

                // Convert to more user friendly notation.
                unsigned int key = 0;
                Gdk::ModifierType mod = Gdk::ModifierType(0);
                Gtk::Accelerator::parse(accels[0], key, mod);
                tooltip += "(" + Gtk::Accelerator::get_label(key, mod) + ")";
            }

            // Update tooltip.
            widget->set_tooltip_markup(tooltip);
        }
    }

    for (auto const child : UI::get_children(*widget)) {
        update_gui_text_recursive(child);
    }
}

// Dialogs

// Import user shortcuts from a file.
bool
Shortcuts::import_shortcuts() {
    // Users key directory.
    auto const &directory = get_path_string(USER, KEYS, {});

    // Create and show the dialog
    Gtk::Window* window = app->get_active_window();
    if (!window) {
        return false;
    }

    auto const importFileDialog = std::unique_ptr<UI::Dialog::FileOpenDialog>{
        UI::Dialog::FileOpenDialog::create(*window, directory, Inkscape::UI::Dialog::CUSTOM_TYPE,
                                           _("Select a file to import"))};
    importFileDialog->addFilterMenu(_("Inkscape shortcuts (*.xml)"), "*.xml");
    bool const success = importFileDialog->show();

    if (!success) {
        return false;
    }

    // Get file and read.
    auto file_read = importFileDialog->getFile();
    if (!read(file_read, true)) {
        std::cerr << "Shortcuts::import_shortcuts: Failed to read file!" << std::endl;
        return false;
    }

    // Save
    return write_user();
};

bool
Shortcuts::export_shortcuts() {
    // Users key directory.
    auto const &directory = get_path_string(USER, KEYS, {});

    // Create and show the dialog
    Gtk::Window* window = app->get_active_window();
    if (!window) {
        return false;
    }

    auto const saveFileDialog = std::unique_ptr<UI::Dialog::FileSaveDialog>{
        UI::Dialog::FileSaveDialog::create(*window, directory, Inkscape::UI::Dialog::CUSTOM_TYPE,
                                           _("Select a filename for export"),
                                           {}, {}, Inkscape::Extension::FILE_SAVE_METHOD_SAVE_AS)};
    saveFileDialog->addFilterMenu(_("Inkscape shortcuts (*.xml)"), "*.xml");
    saveFileDialog->setCurrentName("shortcuts.xml");
    bool success = saveFileDialog->show();

    // Get file name and write.
    if (success) {
        auto file = saveFileDialog->getFile();
        success = write(file, User);
        if (!success) {
            std::cerr << "Shortcuts::export_shortcuts: Failed to save file!" << std::endl;
        }
    }
    return success;
};

/** Connects to a signal emitted whenever the shortcuts change */
sigc::connection Shortcuts::connect_changed(sigc::slot<void ()> const &slot)
{
    return _changed.connect(slot);
}

// For debugging.
void
Shortcuts::dump() {
    // What shortcuts are being used?
    static std::vector<Gdk::ModifierType> const modifiers{
        Gdk::ModifierType(0),
        Gdk::SHIFT_MASK,
        Gdk::CONTROL_MASK,
        Gdk::ALT_MASK,
        Gdk::SHIFT_MASK   |  Gdk::CONTROL_MASK,
        Gdk::SHIFT_MASK   |  Gdk::ALT_MASK,
        Gdk::CONTROL_MASK |  Gdk::ALT_MASK,
        Gdk::SHIFT_MASK   |  Gdk::CONTROL_MASK   | Gdk::ALT_MASK
    };
    for (auto mod : modifiers) {
        for (char key = '!'; key <= '~'; ++key) {
            Glib::ustring action;
            Glib::ustring accel = Gtk::Accelerator::name(key, mod);
            std::vector<Glib::ustring> actions = app->get_actions_for_accel(accel);
            if (!actions.empty()) {
                action = actions[0];
            }

            std::cout << "  shortcut:"
                      << "  " << std::setw(8) << std::hex << (unsigned)mod
                      << "  " << std::setw(8) << std::hex << key
                      << "  " << std::setw(30) << std::left << accel
                      << "  " << action
                      << std::endl;
        }
    }
}

void
Shortcuts::dump_all_recursive(Gtk::Widget* widget)
{
    static unsigned int indent = 0;
    ++indent;
    for (int i = 0; i < indent; ++i) std::cout << "  ";

    if (auto const actionable = dynamic_cast<Gtk::Actionable *>(widget)) {
        action = actionable->get_action_name();
    }

    std::cout << widget->get_name()
              << ":   actionable: " << std::boolalpha << is_actionable
              << ":   " << widget->get_tooltip_text()
              << ":   " << action
              << std::endl;

    for (auto const child : UI::get_children(*widget)) {
        dump_all_recursive(child);
    }

    --indent;
}

} // namespace Inkscape

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
