// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * CommandPalette: Class providing Command Palette feature
 */
/* Authors:
 *     Abhay Raj Singh <abhayonlyone@gmail.com>
 *
 * Copyright (C) 2020 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_DIALOG_COMMAND_PALETTE_H
#define INKSCAPE_DIALOG_COMMAND_PALETTE_H

#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <sigc++/connection.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtk/gtk.h> // GtkEventControllerKey
#include <gtkmm/enums.h> // Gtk::DirectionType

#include "xml/document.h"

namespace Gio {
class Action;
} // namespace Gio

namespace Gtk {
class Box;
class Builder;
class Label;
class ListBox;
class ListBoxRow;
class ScrolledWindow;
class SearchEntry2;
class Widget;
} // namespace Gtk

namespace Inkscape::UI::Dialog {

// Enables using switch case
enum class TypeOfVariant
{
    NONE,
    UNKNOWN,
    BOOL,
    INT,
    DOUBLE,
    STRING,
    TUPLE_DD
};

enum class CPMode
{
    SEARCH,
    INPUT, ///< Input arguments
    SHELL,
    HISTORY
};

enum class HistoryType
{
    LPE,
    ACTION,
    OPEN_FILE,
    IMPORT_FILE,
};

struct History
{
    HistoryType history_type;
    std::string data;

    History(HistoryType ht, std::string &&data)
        : history_type(ht)
        , data(data)
    {}
};

class CPHistoryXML
{
public:
    // constructors, asssignment, destructor
    CPHistoryXML();
    ~CPHistoryXML();

    // Handy wrappers for code clearity
    void add_action(const std::string &full_action_name);

    void add_import(const std::string &uri);
    void add_open(const std::string &uri);

    /// Remember parameter for action
    void add_action_parameter(const std::string &full_action_name, const std::string &param);

    std::optional<History> get_last_operation();

    /// To construct _CPHistory
    std::vector<History> get_operation_history() const;
    /// To get parameter history when an action is selected, LIFO stack like so more recent first
    std::vector<std::string> get_action_parameter_history(const std::string &full_action_name) const;

private:
    void save() const;

    void add_operation(const HistoryType history_type, const std::string &data);

    static std::optional<HistoryType> _get_operation_type(Inkscape::XML::Node *operation);

    const std::string _file_path;

    Inkscape::XML::Document *_xml_doc;
    // handy for xml doc child
    Inkscape::XML::Node *_operations;
    Inkscape::XML::Node *_params;
};

class CommandPalette
{
public:
    CommandPalette();
    ~CommandPalette() = default;

    CommandPalette(CommandPalette const &) = delete;            // no copy
    CommandPalette &operator=(CommandPalette const &) = delete; // no assignment

    void open();
    void close();
    void toggle();

    Gtk::Box &get_base_widget();

private:
    using ActionPtr = Glib::RefPtr<Gio::Action>;
    using ActionPtrName = std::pair<ActionPtr, Glib::ustring>;

    // Insert actions in _CPSuggestions
    void load_app_actions();
    void load_win_doc_actions();

    void append_recent_file_operation(const Glib::ustring &path, bool is_suggestion, bool is_import = true);
    bool generate_action_operation(const ActionPtrName &action_ptr_name, const bool is_suggestion);

    void on_search();

    int on_filter_general(Gtk::ListBoxRow *child);
    bool on_filter_full_action_name(Gtk::ListBoxRow *child);
    bool on_filter_recent_file(Gtk::ListBoxRow *child, bool const is_import);

    bool on_window_key_pressed(GtkEventControllerKey const *controller,
                               unsigned keyval, unsigned keycode, GdkModifierType state);
    void on_window_focus(Gtk::Widget const *focus);

    void on_activate_cpfilter();
    bool on_focus_cpfilter(Gtk::DirectionType direction);

    // when search bar is empty
    void hide_suggestions();
    // when search bar isn't empty
    void show_suggestions();

    void on_row_activated(Gtk::ListBoxRow *activated_row);
    void on_history_selection_changed(Gtk::ListBoxRow *lb);

    bool operate_recent_file(Glib::ustring const &uri, bool const import);

    void on_action_fullname_clicked(const Glib::ustring &action_fullname);

    /// Implements text matching logic
    static bool fuzzy_search(const Glib::ustring &subject, const Glib::ustring &search);
    static bool normal_search(const Glib::ustring &subject, const Glib::ustring &search);
    static bool fuzzy_tolerance_search(const Glib::ustring &subject, const Glib::ustring &search);
    static int fuzzy_points(const Glib::ustring &subject, const Glib::ustring &search);
    static int fuzzy_tolerance_points(const Glib::ustring &subject, const Glib::ustring &search);
    static int fuzzy_points_compare(int fuzzy_points_count_1, int fuzzy_points_count_2, int text_len_1, int text_len_2);

    static void test_sort();
    int on_sort(Gtk::ListBoxRow *row1, Gtk::ListBoxRow *row2);
    void set_mode(CPMode mode);

    // Color addition in searched character
    void add_color(Gtk::Label *label, const Glib::ustring &search, const Glib::ustring &subject, bool tooltip=false);
    void remove_color(Gtk::Label *label, const Glib::ustring &subject, bool tooltip=false);
    static void add_color_description(Gtk::Label *label, const Glib::ustring &search);

    // Executes Action
    bool ask_action_parameter(const ActionPtrName &action);
    static ActionPtrName get_action_ptr_name(Glib::ustring full_action_name);
    bool execute_action(const ActionPtrName &action, const Glib::ustring &value);

    static TypeOfVariant get_action_variant_type(const ActionPtr &action_ptr);

    static std::pair<Gtk::Label *, Gtk::Label *> get_name_desc(Gtk::ListBoxRow *child);
    Gtk::Label *get_full_action_name(Gtk::ListBoxRow *child);

    // Widgets
    Glib::RefPtr<Gtk::Builder> _builder;
    Gtk::Box &_CPBase;
    Gtk::Box &_CPListBase;
    Gtk::SearchEntry2 &_CPFilter;
    Gtk::ListBox &_CPSuggestions;
    Gtk::ListBox &_CPHistory;
    Gtk::ScrolledWindow &_CPSuggestionsScroll;
    Gtk::ScrolledWindow &_CPHistoryScroll;

    // Data
    const int _max_height_requestable = 360;
    Glib::ustring _search_text;

    // States
    bool _is_open = false;
    bool _win_doc_actions_loaded = false;

    /// History
    CPHistoryXML _history_xml;
    /**
     * Remember the mode we are in helps in unnecessary signal disconnection and reconnection
     * Used by set_mode()
     */
    CPMode _mode = CPMode::SHELL;
    // Default value other than SEARCH required
    // set_mode() switches between mode hence checks if it already in the target mode.
    // Constructed value is sometimes SEARCH being the first Item for now
    // set_mode() never attaches the on search listener then
    // This initialising value can be any thing other than the initial required mode
    // Example currently it's open in search mode

    /// Stores the search connection to deactivate when not needed
    sigc::connection _cpfilter_search_connection;

    /// Stores the most recent ask_action_name for when Entry::activate fires & we are in INPUT mode
    std::optional<ActionPtrName> _ask_action_ptr_name;
};

} // namespace Inkscape::UI::Dialog

#endif // INKSCAPE_DIALOG_COMMAND_PALETTE_H

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
