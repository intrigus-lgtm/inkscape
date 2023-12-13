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
#include <gtkmm/shortcut.h>
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
    : app{dynamic_cast<Gtk::Application *>(Gio::Application::get_default().get())}
{
    if (!app) {
        std::cerr << "Shortcuts::Shortcuts: No app! Shortcuts cannot be used without a Gtk::Application!" << std::endl;
        return;
    }

    // Shared among all Shortcut controllers.
    _liststore = Gio::ListStore<Gtk::Shortcut>::create();
}

Shortcuts::~Shortcuts() {}

void
Shortcuts::init() {
    initialized = true;

    // Clear arrays (we may be re-reading).
    _clear();

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
        success = _read(file);
        if (!success) {
            std::cerr << "Shortcut::Shortcut: Unable to read shortcut file listed in preferences: " + path << std::endl;
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
        success = _read(file);
    }
  
    if (!success) {
        std::cerr << "Shortcut::Shortcut: Failed to read file default.xml, trying inkscape.xml" << std::endl;

        Glib::RefPtr<Gio::File> file = Gio::File::create_for_path(get_path_string(SYSTEM, KEYS, "inkscape.xml"));
        success = _read(file);
    }

    if (!success) {
        std::cerr << "Shortcut::Shortcut: Failed to read file inkscape.xml; giving up!" << std::endl;
    }

    // ------------ Open Shared shortcut file -------------
    Glib::RefPtr<Gio::File> file = Gio::File::create_for_path(get_path_string(SHARED, KEYS, "default.xml"));
    // Test if file exists before attempting to read to avoid generating warning message.
    if (file->query_exists()) {
        _read(file, true);
    }

    // ------------ Open User shortcut file -------------
    file = Gio::File::create_for_path(get_path_string(USER, KEYS, "default.xml"));
    // Test if file exists before attempting to read to avoid generating warning message.
    if (file->query_exists()) {
        _read(file, true);
    }

    // Emit changed signal in case of read-reading (user selects different file).
    _changed.emit();

    // _dump();
}


// ****** User Shortcuts ******

// Add a user shortcut, updating user's shortcut file if successful.
bool
Shortcuts::add_user_shortcut(Glib::ustring const &detailed_action_name,const Gtk::AccelKey& trigger)
{
    // Add shortcut, if successful, save to file.
    if (_add_shortcut(detailed_action_name, trigger.get_abbrev(), true)) {  // Always user.
        _changed.emit();

        // Save
        return write_user();
    }

    std::cerr << "Shortcut::add_user_shortcut: Failed to add: " << detailed_action_name.raw()
              << " with shortcut " << trigger.get_abbrev().raw() << std::endl;
    return false;
};

// Remove a user shortcut, updating user's shortcut file.
bool
Shortcuts::remove_user_shortcut(Glib::ustring const &detailed_action_name)
{
    // Check if really user shortcut.
    bool user_shortcut = is_user_set(detailed_action_name);

    if (!user_shortcut) {
        // We don't allow removing non-user shortcuts.
        return false;
    }

    if (_remove_shortcuts(detailed_action_name)) {
        // Save
        write_user();

        // Reread to get original shortcut (if any). And emit changes signal.
        init();

        return true;
    }

    std::cerr << "Shortcuts::remove_user_shortcut: Failed to remove shortcut for: "
              << detailed_action_name.raw() << std::endl;
    return false;
}


/**
 * Remove all user's shortcuts (simply overwrites existing file).
 */
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

    // Re-read everything! And emit changed signal.
    init();
    return true;
}

/**
 * Return if user set shortcut for Gio::Action.
 */
bool
Shortcuts::is_user_set(Glib::ustring const &detailed_action_name)
{
    auto it = _shortcuts.find(detailed_action_name);

    if (it != _shortcuts.end()) {
        // We need to test only one entry, as there will be only one if user set.
        return (it->second.user_set);
    }

    return false;
}

/**
 * Write user shortcuts to file.
 */
bool
Shortcuts::write_user()
{
    Glib::RefPtr<Gio::File> file = Gio::File::create_for_path(get_path_string(USER, KEYS, "default.xml"));
    return _write(file, User);
}

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

            auto const &triggers = get_triggers(action);

            Glib::ustring tooltip;
            auto *iapp = InkscapeApplication::instance();
            if (iapp) {
                tooltip = iapp->get_action_extra_data().get_tooltip_for_action(action, true, true);
            }

            // Add new primary accelerator.
            if (triggers.size() > 0) {
                // Add space between tooltip and accel if there is a tooltip
                if (!tooltip.empty()) {
                    tooltip += " ";
                }

                // Convert to more user friendly notation.
                unsigned int key = 0;
                Gdk::ModifierType mod{};
                Gtk::Accelerator::parse(triggers[0], key, mod);
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

//  ******** Invoke Actions *******

/**  Trigger action from a shortcut. Useful if we want to intercept the event from GTK */
bool
Shortcuts::invoke_action(Gtk::AccelKey const &shortcut)
{
    // This can be simplified in GTK4.
    Glib::ustring accel = Gtk::Accelerator::name(shortcut.get_key(), shortcut.get_mod());

    auto const actions = get_actions(accel);
    if (!actions.empty()) {
        Glib::ustring const &action = actions[0];
        Glib::ustring action_name;
        Glib::VariantBase value;
        Gio::SimpleAction::parse_detailed_name_variant(action.substr(4), action_name, value);
        if (action.compare(0, 4, "app.") == 0) {
            app->activate_action(action_name, value);
            return true;
        } else {
            auto window = dynamic_cast<InkscapeWindow *>(app->get_active_window());
            if (window) {
                window->activate_action(action, value); // Not action_name in Gtk4!
                return true;
            }
        }
    }
    return false;
}

/**  Trigger action from a shortcut. Useful if we want to intercept the event from GTK */
// Used by Tools
bool
Shortcuts::invoke_action(KeyEvent const &event)
{
    auto const shortcut = get_from_event(event);
    return invoke_action(shortcut);
}

/**  Trigger action from a shortcut. Useful if we want to intercept the event from GTK */
// NOT USED CURRENTLY
bool
Shortcuts::invoke_action(GtkEventControllerKey const * const controller,
                         unsigned const keyval, unsigned const keycode,
                         GdkModifierType const state)
{
    auto const shortcut = get_from(controller, keyval, keycode, state);
    return invoke_action(shortcut);
}

// ******* Utility *******

/**
 * Returns a vector of triggers for a given detailed_action_name.
 */
std::vector<Glib::ustring>
Shortcuts::get_triggers(Glib::ustring const &detailed_action_name) const
{
    std::vector<Glib::ustring> triggers;
    auto matches = _shortcuts.equal_range(detailed_action_name);
    for (auto it = matches.first; it != matches.second; ++it) {
        triggers.push_back(it->second.trigger_string);
    }
    return triggers;
}

/**
 * Returns a vector of detailed_action_names for a given trigger.
 */
std::vector<Glib::ustring>
Shortcuts::get_actions(Glib::ustring const &trigger) const
{
    std::vector<Glib::ustring> actions;
    for (auto const &[detailed_action_name, value] : _shortcuts) {
        if (trigger == value.trigger_string) {
            actions.emplace_back(detailed_action_name);
        }
    }
    return actions;
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
    auto const default_mod_mask = Gtk::Accelerator::get_default_mod_mask();
    auto const initial_modifiers = static_cast<Gdk::ModifierType>(event_state) & default_mod_mask;

    auto consumed_modifiers = 0u;
    auto keyval = Inkscape::UI::Tools::get_latin_keyval_impl(
        event_keyval, event_keycode, event_state, event_group, &consumed_modifiers);

    // If a key value is "convertible", i.e. it has different lower case and upper case versions,
    // convert to lower case and don't consume the "shift" modifier.
    bool is_case_convertible = !(gdk_keyval_is_upper(keyval) && gdk_keyval_is_lower(keyval));
    if (is_case_convertible) {
        keyval = gdk_keyval_to_lower(keyval);
        consumed_modifiers &= ~static_cast<unsigned>(Gdk::ModifierType::SHIFT_MASK);
    }

    // The InkscapePreferences dialog returns an event structure where the Shift modifier is not
    // set for keys like '('. This causes '(' to be converted to '9' by get_latin_keyval. It also
    // returns 'Shift-k' for 'K' (instead of 'Shift-K') but this is not a problem.
    // We fix this by restoring keyval to its original value.
    if (fix) {
        keyval = event_keyval;
    }

    auto const unused_modifiers = Gdk::ModifierType(static_cast<unsigned>(initial_modifiers)
                                                                          & ~consumed_modifiers
                                                                          &  GDK_MODIFIER_MASK
                                                                          & ~GDK_LOCK_MASK);

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
    // TODO: Once controller.h is updated to use gtkmm 4 wrappers, we can get rid of const_cast etc
    auto const mcontroller = const_cast<GtkEventControllerKey *>(controller);
    auto const group = controller ? gtk_event_controller_key_get_group(mcontroller) : 0u;
    return get_from_event_impl(keyval, keycode, state, group, fix);
}

Gtk::AccelKey
Shortcuts::get_from_event(KeyEvent const &event, bool fix)
{
    return get_from_event_impl(event.keyval, event.keycode,
                               static_cast<GdkModifierType>(event.modifiers), event.group, fix);
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

template <typename T>
static void append(std::vector<T> &target, std::vector<T> &&source)
{
    target.insert(target.end(), std::move_iterator{source.begin()}, std::move_iterator{source.end()});
}

/**
 * Get a list of filenames to populate menu in preferences dialog.
 */
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
        Glib::ustring label = Glib::path_get_basename(filename);
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
    if (!_read(file_read, true)) {
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
        success = _write(file, User);
        if (!success) {
            std::cerr << "Shortcuts::export_shortcuts: Failed to save file!" << std::endl;
        }
    }
    return success;
};

/** Connects to a signal emitted whenever the shortcuts change. */
sigc::connection Shortcuts::connect_changed(sigc::slot<void ()> const &slot)
{
    return _changed.connect(slot);
}


// -------- Private --------

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

Gdk::ModifierType
parse_modifier_string(char const * const modifiers_string)
{
    Gdk::ModifierType modifiers{};
    if (modifiers_string) {
        std::vector<Glib::ustring> mod_vector = Glib::Regex::split_simple("\\s*,\\s*", modifiers_string);

        for (auto const &mod : mod_vector) {
            if (mod == "Control" || mod == "Ctrl") {
                modifiers |= Gdk::ModifierType::CONTROL_MASK;
            } else if (mod == "Shift") {
                modifiers |= Gdk::ModifierType::SHIFT_MASK;
            } else if (mod == "Alt") {
                modifiers |= Gdk::ModifierType::ALT_MASK;
            } else if (mod == "Super") {
                modifiers |= Gdk::ModifierType::SUPER_MASK; // Not used
            } else if (mod == "Hyper") {
                modifiers |= Gdk::ModifierType::HYPER_MASK; // Not used
            } else if (mod == "Meta") {
                modifiers |= Gdk::ModifierType::META_MASK;
            } else {
                std::cerr << "Shortcut::read: Unknown GDK modifier: " << mod.c_str() << std::endl;
            }
        }
    }
    return modifiers;
}

// ******* Files *******

/**
 * Read a shortcut file.
 */
bool
Shortcuts::_read(Glib::RefPtr<Gio::File> const &file, bool const user_set)
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
                std::cerr << "Shortcuts::read: Missing modifier for action!" << std::endl;
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
            std::reverse(key_vector.begin(), key_vector.end()); // Last key added will appear in menus.

            // Set one shortcut at a time so we can check if it has been previously used.
            for (auto const &key : key_vector) {
                _add_shortcut(gaction, key, user_set);
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

// In principle, we only write User shortcuts. But for debugging, we might want to write something else.
bool
Shortcuts::_write(Glib::RefPtr<Gio::File> const &file, What const what)
{
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
        bool user_set = is_user_set(action_name.raw());
        if ( (what == All)                 ||
             (what == System && !user_set) ||
             (what == User   &&  user_set) )
        {
            auto const &triggers = get_triggers(action_name);
            if (!triggers.empty()) {
                XML::Node * node = document->createElement("bind");

                node->setAttribute("gaction", action_name);

                auto const keys = join(triggers, ',');
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

// ******* Add/remove shortcuts *******

/**
 * Add a shortcut. Other shortcuts may already exist for the same action.
 * For user shortcut, all other shortcuts for actions should have been removed.
 * If shortcut added, return true.
 */
bool
Shortcuts::_add_shortcut(Glib::ustring const &detailed_action_name,
                         Glib::ustring const &trigger_string, bool user)
{
    // Format has changed between Gtk3 and Gtk4. Pass through xxx to standardize form.
    Gtk::AccelKey key(trigger_string);
    auto trigger_normalized = key.get_abbrev();

    // Check if action actually exists. Need to compare action names without values...
    Glib::ustring action_name;
    Glib::VariantBase target;
    Gio::SimpleAction::parse_detailed_name_variant(detailed_action_name, action_name, target);

    bool found = false;
    for (auto const &action : list_all_detailed_action_names()) {
        Glib::ustring action_name_old;
        Glib::VariantBase target_old;
        Gio::SimpleAction::parse_detailed_name_variant(action, action_name_old, target_old);

        if (action_name == action_name_old) {
            found = true;
            break;
        }
    }

    if (!found) {
        // Oops, not an action!
        std::cerr << "Shortcuts::_add_shortcut: No Action for " << detailed_action_name.raw() << std::endl;
        return false;
    }

    // Remove previous use of trigger.
    [[maybe_unused]] auto const removed = _remove_shortcut_trigger(trigger_normalized);
    // if (removed) {
    //     std::cerr << "Shortcut::add_shortcut: duplicate shortcut found for: " << trigger_normalized
    //               << "  New: " << detailed_action_name.raw() << " !" << std::endl;
    // }

    // A user shortcut replaces all others.
    if (user) {
        _remove_shortcuts(detailed_action_name);
    }

    auto const trigger = Gtk::ShortcutTrigger::parse_string(trigger_normalized);
    g_assert(trigger);

    auto const action = Gtk::NamedAction::create(action_name);
    g_assert(action);

    auto shortcut = Gtk::Shortcut::create(trigger, action);
    g_assert(shortcut);
    if (target) {
        shortcut->set_arguments(target);
    }

    _liststore->append(shortcut);

    auto value = ShortcutValue{std::move(trigger_normalized), std::move(shortcut), user};
    _shortcuts.emplace(detailed_action_name.raw(), std::move(value));

    return true;
}

/**
 * Remove shortcuts via AccelKey.
 * Returns true of shortcut(s) removed, false if nothing removed.
 */
bool
Shortcuts::_remove_shortcut_trigger(Glib::ustring const& trigger)
{
    bool changed = false;
    for (auto it = _shortcuts.begin(); it != _shortcuts.end(); ) {
        Gtk::AccelKey key1(trigger);
        Gtk::AccelKey key2(it->second.trigger_string);
        if (it->second.trigger_string == trigger) {

            // Liststores are ugly!
            auto shortcut = it->second.shortcut;
            for (int i = 0; i < _liststore->get_n_items(); ++i) {
                if (shortcut == _liststore->get_item(i)) {
                    _liststore->remove(i);
                    break;
                }
            }

            it = _shortcuts.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    if (changed) {
        return true;
    }

    return false;
}

/**
 * Remove all shortcuts for a detailed action. There can be multiple.
 */
bool
Shortcuts::_remove_shortcuts(Glib::ustring const &detailed_action_name)
{
    bool removed = false;
    for (auto it = _shortcuts.begin(); it != _shortcuts.end(); ) {
        if (it->first == detailed_action_name.raw()) {
            auto const &shortcut = it->second.shortcut;
            g_assert(shortcut);

            // Liststores are ugly!
            for (int i = 0; i < _liststore->get_n_items(); ++i) {
                if (shortcut == _liststore->get_item(i)) {
                    _liststore->remove(i);
                    break;
                }
            }

            removed = true;
            it = _shortcuts.erase(it);
        } else {
            ++it;
        }
    }

    return removed;
}

/**
 * Clear all shortcuts.
 */
void
Shortcuts::_clear()
{
    _liststore->remove_all();
    _shortcuts.clear();
}


// For debugging.
void
Shortcuts::_dump() {
    // What shortcuts are being used?
    static std::vector<Gdk::ModifierType> const modifiers{
        Gdk::ModifierType{},
        Gdk::ModifierType::SHIFT_MASK,
        Gdk::ModifierType::CONTROL_MASK,
        Gdk::ModifierType::ALT_MASK,
        Gdk::ModifierType::SHIFT_MASK   |  Gdk::ModifierType::CONTROL_MASK,
        Gdk::ModifierType::SHIFT_MASK   |  Gdk::ModifierType::ALT_MASK,
        Gdk::ModifierType::CONTROL_MASK |  Gdk::ModifierType::ALT_MASK,
        Gdk::ModifierType::SHIFT_MASK   |  Gdk::ModifierType::CONTROL_MASK   | Gdk::ModifierType::ALT_MASK
    };

    for (auto mod : modifiers) {
        for (char key = '!'; key <= '~'; ++key) {
            Glib::ustring action;
            Glib::ustring accel = Gtk::Accelerator::name(key, mod);
            auto const actions = get_actions(accel);
            if (!actions.empty()) {
                action = actions[0];
            }

            std::cout << "  shortcut:"
                      << "  " << std::setw( 8) << std::hex  << static_cast<int>(mod)
                      << "  " << std::setw( 8) << std::hex  << key
                      << "  " << std::setw(30) << std::left << accel
                      << "  " << action
                      << std::endl;
        }
    }

    int count = _liststore->get_n_items();
    for (int i = 0; i < count; ++i) {
        auto shortcut = _liststore->get_item(i);
        auto trigger = shortcut->get_trigger();
        auto action = shortcut->get_action();
        auto variant = shortcut->get_arguments();

        std::cout << action->to_string();
        if (variant) {
            std::cout << "(" << variant.print() << ")";
        }
        std::cout << ": " << trigger->to_string() << std::endl;
    }
}

void
Shortcuts::_dump_all_recursive(Gtk::Widget* widget)
{
    static unsigned int indent = 0;
    ++indent;
    for (int i = 0; i < indent; ++i) std::cout << "  ";

    auto const actionable = dynamic_cast<Gtk::Actionable *>(widget);
    auto const action = actionable ? actionable->get_action_name() : "";

    std::cout << widget->get_name()
              << ":   actionable: " << std::boolalpha << static_cast<bool>(actionable)
              << ":   " << widget->get_tooltip_text()
              << ":   " << action
              << std::endl;

    for (auto const child : UI::get_children(*widget)) {
        _dump_all_recursive(child);
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
