// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A widget that manages DialogNotebook's and other widgets inside a horizontal DialogMultipaned.
 *
 * Authors: see git history
 *   Tavmjong Bah
 *
 * Copyright (c) 2018 Tavmjong Bah, Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "dialog-container.h"

#include <iostream>
#include <glibmm/i18n.h>
#include <glibmm/keyfile.h>
#include <giomm/file.h>
#include <gtkmm/accelerator.h>
#include <gtkmm/box.h>
#include <gtkmm/eventbox.h>
#include <gtkmm/image.h>
#include <gtkmm/selectiondata.h>
#include <gtkmm/targetentry.h>
#include <gtkmm/viewport.h>
#include <sigc++/adaptors/bind.h>
#include <sigc++/functors/mem_fun.h>

#include "enums.h"
#include "inkscape.h"
#include "inkscape-application.h"
#include "inkscape-window.h"
#include "ui/dialog/clonetiler.h"
#include "ui/dialog/dialog-data.h"
#include "ui/dialog/dialog-multipaned.h"
#include "ui/dialog/dialog-notebook.h"
#include "ui/dialog/dialog-window.h"
#include "ui/dialog/document-properties.h"
#include "ui/dialog/document-resources.h"
#include "ui/dialog/export.h"
#include "ui/dialog/extensions-gallery.h"
#include "ui/dialog/fill-and-stroke.h"
#include "ui/dialog/filter-effects-dialog.h"
#include "ui/dialog/find.h"
#include "ui/dialog/font-collections-manager.h"
#include "ui/dialog/glyphs.h"
#include "ui/dialog/icon-preview.h"
#include "ui/dialog/inkscape-preferences.h"
#include "ui/dialog/livepatheffect-editor.h"
#include "ui/dialog/memory.h"
#include "ui/dialog/messages.h"
#include "ui/dialog/object-attributes.h"
#include "ui/dialog/object-properties.h"
#include "ui/dialog/objects.h"
#include "ui/dialog/paint-servers.h"
#include "ui/dialog/selectorsdialog.h"
#if WITH_GSPELL
#include "ui/dialog/spellcheck.h"
#endif
#include "ui/dialog/svg-fonts-dialog.h"
#include "ui/dialog/swatches.h"
#include "ui/dialog/symbols.h"
#include "ui/dialog/text-edit.h"
#include "ui/dialog/tile.h"
#include "ui/dialog/tracedialog.h"
#include "ui/dialog/transformation.h"
#include "ui/dialog/undo-history.h"
#include "ui/dialog/xml-tree.h"
#include "ui/icon-names.h"
#include "ui/themes.h"
#include "ui/util.h"
#include "ui/widget/canvas-grid.h"

namespace Inkscape::UI::Dialog {

[[nodiscard]] static auto const &get_target_entries()
{
    static auto const target_entries = std::vector{Gtk::TargetEntry{"GTK_NOTEBOOK_TAB"}};
    return target_entries;
}

// Clear dialogs a bit early, to not do ~MultiPaned → ~Notebook → our unlink_dialog()→erase()→crash
DialogContainer::~DialogContainer()
{
    dialogs.clear();
}

DialogContainer::DialogContainer(InkscapeWindow* inkscape_window)
    : _inkscape_window(inkscape_window)
{
    g_assert(_inkscape_window != nullptr);

    add_css_class("DialogContainer");

    columns = std::make_unique<DialogMultipaned>(Gtk::Orientation::HORIZONTAL);
    setup_drag_and_drop(columns.get());
    add(*columns.get());
}

DialogMultipaned *DialogContainer::create_column()
{
    auto const column = Gtk::make_managed<DialogMultipaned>(Gtk::Orientation::VERTICAL);
    setup_drag_and_drop(column);
    connections.emplace_back(column->signal_now_empty().connect(
        sigc::bind(sigc::mem_fun(*this, &DialogContainer::column_empty), column)));
    return column;
}

void DialogContainer::setup_drag_and_drop(DialogMultipaned * const column)
{
    connections.emplace_back(column->signal_prepend_drag_data().connect(
        sigc::bind(sigc::mem_fun(*this, &DialogContainer::prepend_drop), column)));
    connections.emplace_back(column->signal_append_drag_data().connect(
        sigc::bind(sigc::mem_fun(*this, &DialogContainer::append_drop), column)));
    column->set_target_entries(get_target_entries());
}

/**
 * Get an instance of a DialogBase dialog using the associated dialog name.
 */
std::unique_ptr<DialogBase> DialogContainer::dialog_factory(Glib::ustring const &dialog_type)
{
    // clang-format off
         if (dialog_type == "AlignDistribute")    return std::make_unique<ArrangeDialog>();
    else if (dialog_type == "CloneTiler")         return std::make_unique<CloneTiler>();
    else if (dialog_type == "DocumentProperties") return std::make_unique<DocumentProperties>();
    else if (dialog_type == "DocumentResources")  return std::make_unique<DocumentResources>();
    else if (dialog_type == "Export")             return std::make_unique<Export>();
    else if (dialog_type == "ExtensionsGallery")  return std::make_unique<ExtensionsGallery>(ExtensionsGallery::Effects);
    else if (dialog_type == "FillStroke")         return std::make_unique<FillAndStroke>();
    else if (dialog_type == "FilterEffects")      return std::make_unique<FilterEffectsDialog>();
    else if (dialog_type == "FilterGallery")      return std::make_unique<ExtensionsGallery>(ExtensionsGallery::Filters);
    else if (dialog_type == "Find")               return std::make_unique<Find>();
    else if (dialog_type == "FontCollections")    return std::make_unique<FontCollectionsManager>();
    else if (dialog_type == "Glyphs")             return std::make_unique<GlyphsPanel>();
    else if (dialog_type == "IconPreview")        return std::make_unique<IconPreviewPanel>();
    else if (dialog_type == "LivePathEffect")     return std::make_unique<LivePathEffectEditor>();
    else if (dialog_type == "Memory")             return std::make_unique<Memory>();
    else if (dialog_type == "Messages")           return std::make_unique<Messages>();
    else if (dialog_type == "ObjectAttributes")   return std::make_unique<ObjectAttributes>();
    else if (dialog_type == "ObjectProperties")   return std::make_unique<ObjectProperties>();
    else if (dialog_type == "Objects")            return std::make_unique<ObjectsPanel>();
    else if (dialog_type == "PaintServers")       return std::make_unique<PaintServersDialog>();
    else if (dialog_type == "Preferences")        return std::make_unique<InkscapePreferences>();
    else if (dialog_type == "Selectors")          return std::make_unique<SelectorsDialog>();
    else if (dialog_type == "SVGFonts")           return std::make_unique<SvgFontsDialog>();
    else if (dialog_type == "Swatches")           return std::make_unique<SwatchesPanel>(false);
    else if (dialog_type == "Symbols")            return std::make_unique<SymbolsDialog>();
    else if (dialog_type == "Text")               return std::make_unique<TextEdit>();
    else if (dialog_type == "Trace")              return TraceDialog::create();
    else if (dialog_type == "Transform")          return std::make_unique<Transformation>();
    else if (dialog_type == "UndoHistory")        return std::make_unique<UndoHistory>();
    else if (dialog_type == "XMLEditor")          return std::make_unique<XmlTree>();
#if WITH_GSPELL
    else if (dialog_type == "Spellcheck")         return std::make_unique<SpellCheck>();
#endif
#ifdef DEBUG
    else if (dialog_type == "Prototype")          return std::make_unique<Prototype>();
#endif
    else {
        std::cerr << "DialogContainer::dialog_factory: Unhandled dialog: " << dialog_type.raw() << std::endl;
        return nullptr;
    }
    // clang-format on
}

// Create the notebook tab
Gtk::Widget *DialogContainer::create_notebook_tab(Glib::ustring const &label_str,
                                                  Glib::ustring const &image_str,
                                                  Glib::ustring const &shortcut)
{
    auto const label = Gtk::make_managed<Gtk::Label>(label_str);
    auto const image = Gtk::make_managed<Gtk::Image>();
    auto const close = Gtk::make_managed<Gtk::Button>();
    image->set_from_icon_name(image_str);
    auto const tab = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    close->set_image_from_icon_name("window-close");
    close->set_tooltip_text(_("Close Tab"));
    close->add_css_class("close-button");
    Glib::ustring label_str_fix = label_str;
    label_str_fix = Glib::Regex::create("\\W")->replace_literal(label_str_fix, 0, "-", (Glib::Regex::MatchFlags)0);
    tab->add_css_class(label_str_fix);
    tab->append(*image);
    tab->append(*label);
    tab->append(*close);

    // Workaround to the fact that Gtk::Box doesn't receive on_button_press event
    auto const cover = Gtk::make_managed<Gtk::EventBox>();
    cover->add(*tab);

    // Add shortcut tooltip
    if (shortcut.size() > 0) {
        auto tlabel = shortcut;
        int pos = tlabel.find("&", 0);
        if (pos >= 0 && pos < tlabel.length()) {
            tlabel.replace(pos, 1, "&amp;");
        }
        cover->set_tooltip_markup(label_str + " (<b>" + tlabel + "</b>)");
    } else {
        cover->set_tooltip_text(label_str);
    }

    return cover;
}

// find dialog's multipaned parent; is there a better way?
DialogMultipaned* get_dialog_parent(DialogBase* dialog) {
    if (!dialog) return nullptr;

    // dialogs are nested inside Gtk::Notebook
    if (auto notebook = dynamic_cast<Gtk::Notebook*>(dialog->get_parent())) {
        // notebooks are inside viewport, inside scrolled window
        if (auto viewport = dynamic_cast<Gtk::Viewport*>(notebook->get_parent())) {
            if (auto scroll = dynamic_cast<Gtk::ScrolledWindow*>(viewport->get_parent())) {
                // finally get the panel
                if (auto panel = dynamic_cast<DialogMultipaned*>(scroll->get_parent())) {
                    return panel;
                }
            }
        }
    }

    return nullptr;
}

/**
 * Add new dialog to the current container or in a floating window, based on preferences.
 */
void DialogContainer::new_dialog(const Glib::ustring& dialog_type )
{
    // Open all dialogs as floating, if set in preferences
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (prefs == nullptr) {
        return;
    }

    int dockable = prefs->getInt("/options/dialogtype/value", PREFS_DIALOGS_BEHAVIOR_DOCKABLE);
    bool floating = DialogManager::singleton().should_open_floating(dialog_type);
    if (dockable == PREFS_DIALOGS_BEHAVIOR_FLOATING || floating) {
        new_floating_dialog(dialog_type);
    } else {
        new_dialog(dialog_type, nullptr);
    }

    if (DialogBase* dialog = find_existing_dialog(dialog_type)) {
        dialog->focus_dialog();
    }
}


DialogBase* DialogContainer::find_existing_dialog(const Glib::ustring& dialog_type) {
    DialogBase *existing_dialog = get_dialog(dialog_type);
    if (!existing_dialog) {
        existing_dialog = DialogManager::singleton().find_floating_dialog(dialog_type);
    }
    return existing_dialog;
}

[[nodiscard]] static Glib::ustring get_open_action_name(Glib::ustring const &dialog_type)
{
    auto const action_name = "win.dialog-open('" + dialog_type += "')";
    auto const app = InkscapeApplication::instance();
    auto const &accels = app->gtk_app()->get_accels_for_action(action_name);
    if (accels.empty()) return {};

    unsigned key{};
    Gdk::ModifierType mods{};
    Gtk::Accelerator::parse(accels[0], key, mods);
    return Gtk::Accelerator::get_label(key, mods);
}

/**
 * Overloaded new_dialog
 */
void DialogContainer::new_dialog(const Glib::ustring& dialog_type, DialogNotebook *notebook)
{
    columns->ensure_multipaned_children();

    // Limit each container to containing one of any type of dialog.
    if (DialogBase* existing_dialog = find_existing_dialog(dialog_type)) {
        // make sure parent window is not hidden/collapsed
        if (auto panel = get_dialog_parent(existing_dialog)) {
            panel->set_visible(true);
        }
        // found existing dialog; blink & exit
        existing_dialog->blink();
        return;
    }

    // Create the dialog widget
    DialogBase *dialog = dialog_factory(dialog_type).release(); // Evil, but necessitated by GTK.

    if (!dialog) {
        std::cerr << "DialogContainer::new_dialog(): couldn't find dialog for: " << dialog_type.raw() << std::endl;
        return;
    }

    // manage the dialog instance
    dialog = Gtk::manage(dialog);

    // Create the notebook tab
    auto const &dialog_data = get_dialog_data();
    Glib::ustring image("inkscape-logo");
    auto it = dialog_data.find(dialog_type);
    if (it != dialog_data.end()) {
        image = it->second.icon_name;
    }

    auto const &label = get_open_action_name(dialog_type);
    Gtk::Widget *tab = create_notebook_tab(dialog->get_name(), image, label);

    // If not from notebook menu add at top of last column.
    if (!notebook) {
        // Look to see if last column contains a multipane. If not, add one.
        DialogMultipaned *last_column = dynamic_cast<DialogMultipaned *>(columns->get_last_widget());
        if (!last_column) {
            last_column = create_column();
            columns->append(*last_column);
        }

        // Look to see if first widget in column is notebook, if not add one.
        notebook = dynamic_cast<DialogNotebook *>(last_column->get_first_widget());
        if (!notebook) {
            notebook = Gtk::make_managed<DialogNotebook>(this);
            last_column->prepend(*notebook);
        }
    }

    // Add dialog
    notebook->add_page(*dialog, *tab, dialog->get_name());

    if (auto panel = dynamic_cast<DialogMultipaned*>(notebook->get_parent())) {
        // if panel is collapsed, show it now, or else new dialog will be mysteriously missing
        panel->set_visible(true);
    }
}

[[nodiscard]] static auto get_key(std::size_t const notebook_idx)
{
    return Glib::ustring::compose("Notebook%1Dialogs", notebook_idx);
}

// recreate dialogs hosted (docked) in a floating DialogWindow; window will be created
bool DialogContainer::recreate_dialogs_from_state(InkscapeWindow* inkscape_window, const Glib::KeyFile* keyfile)
{
    g_assert(inkscape_window != nullptr);

    bool restored = false;
    // Step 1: check if we want to load the state
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    int save_state = prefs->getInt("/options/savedialogposition/value", PREFS_DIALOGS_STATE_SAVE);
    if (save_state == PREFS_DIALOGS_STATE_NONE) {
        return restored; // User has turned off this feature in Preferences
    }

    // if it isn't dockable, all saved docked dialogs are made floating
    bool is_dockable =
        prefs->getInt("/options/dialogtype/value", PREFS_DIALOGS_BEHAVIOR_DOCKABLE) != PREFS_DIALOGS_BEHAVIOR_FLOATING;

    if (!is_dockable)
        return false; // not applicable if docking is off

    // Step 2: get the number of windows; should be 1
    int windows_count = 0;
    try {
        // we may have no 'Windows' initially when recreating floating dialog state (state is empty)
        if (keyfile->has_group("Windows") && keyfile->has_key("Windows", "Count")) {
            windows_count = keyfile->get_integer("Windows", "Count");
        }
    } catch (Glib::Error const &error) {
        std::cerr << G_STRFUNC << ": " << error.what().raw() << std::endl;
    }

    // Step 3: for each window, load its state.
    for (int window_idx = 0; window_idx < windows_count; ++window_idx) {
        Glib::ustring group_name = "Window" + std::to_string(window_idx);

        bool has_position = keyfile->has_key(group_name, "Position") && keyfile->get_boolean(group_name, "Position");
        window_position_t pos;
        if (has_position) { // floating window position recorded?
            pos.x = keyfile->get_integer(group_name, "x");
            pos.y = keyfile->get_integer(group_name, "y");
            pos.width = keyfile->get_integer(group_name, "width");
            pos.height = keyfile->get_integer(group_name, "height");
        }
        // Step 3.0: read the window parameters
        int column_count = 0;
        try {
            column_count = keyfile->get_integer(group_name, "ColumnCount");
        } catch (Glib::Error const &error) {
            std::cerr << G_STRFUNC << ": " << error.what().raw() << std::endl;
        }

        // Step 3.1: get the window's container columns where we want to create the dialogs
        DialogWindow *dialog_window = new DialogWindow(inkscape_window, nullptr);
        DialogContainer *active_container = dialog_window->get_container();
        DialogMultipaned *active_columns = active_container ? active_container->get_columns() : nullptr;

        if (!active_container || !active_columns) {
            continue;
        }

        // Step 3.2: for each column, load its state
        for (int column_idx = 0; column_idx < column_count; ++column_idx) {
            Glib::ustring column_group_name = group_name + "Column" + std::to_string(column_idx);

            // Step 3.2.0: read the column parameters
            int notebook_count = 0;
            bool before_canvas = false;
            try {
                notebook_count = keyfile->get_integer(column_group_name, "NotebookCount");
                if (keyfile->has_key(column_group_name, "BeforeCanvas")) {
                    before_canvas = keyfile->get_boolean(column_group_name, "BeforeCanvas");
                }
            } catch (Glib::Error const &error) {
                std::cerr << G_STRFUNC << ": " << error.what().raw() << std::endl;
            }

            // Step 3.2.1: create the column
            DialogMultipaned *column = active_container->create_column();

            before_canvas ? active_columns->prepend(*column) : active_columns->append(*column);

            // Step 3.2.2: for each noteboook, load its dialogs
            for (int notebook_idx = 0; notebook_idx < notebook_count; ++notebook_idx) {
                auto const key = get_key(notebook_idx);

                // Step 3.2.2.0 read the list of dialogs in the current notebook
                std::vector<Glib::ustring> dialogs;
                try {
                    dialogs = keyfile->get_string_list(column_group_name, key);
                } catch (Glib::Error const &error) {
                    std::cerr << G_STRFUNC << ": " << error.what().raw() << std::endl;
                }

                if (!dialogs.size()) {
                    continue;
                }

                DialogNotebook *notebook = nullptr;
                auto const &dialog_data = get_dialog_data();

                // Step 3.2.2.1 create each dialog in the current notebook
                for (auto type : dialogs) {
                    if (DialogManager::singleton().find_floating_dialog(type)) {
                        // avoid duplicates
                        continue;
                    }

                    if (dialog_data.find(type) != dialog_data.end()) {
                        if (!notebook) {
                            notebook = Gtk::make_managed<DialogNotebook>(active_container);
                            column->append(*notebook);
                        }
                        active_container->new_dialog(type, notebook);
                    } else {
                        std::cerr << "recreate_dialogs_from_state: invalid dialog type: " << type.raw() << std::endl;
                    }
                }
            }
        }

        if (has_position) {
            dm_restore_window_position(*dialog_window, pos);
        } else {
            dialog_window->update_window_size_to_fit_children();
        }

        dialog_window->set_visible(true);

        // Set the style and icon theme of the new menu based on the desktop
        INKSCAPE.themecontext->getChangeThemeSignal().emit();
        INKSCAPE.themecontext->add_gtk_css(true);
        restored = true;
    }

    return restored;
}

/**
 * Add a new floating dialog (or reuse existing one if it's already up)
 */
DialogWindow *DialogContainer::new_floating_dialog(const Glib::ustring& dialog_type)
{
    return create_new_floating_dialog(dialog_type, true);
}

DialogWindow *DialogContainer::create_new_floating_dialog(const Glib::ustring& dialog_type, bool blink)
{
    // check if this dialog is already open
    if (DialogBase* existing_dialog = find_existing_dialog(dialog_type)) {
        // found existing dialog; blink & exit
        if (blink) {
            existing_dialog->blink();
            // show its window if it is hidden
            if (auto window = DialogManager::singleton().find_floating_dialog_window(dialog_type)) {
                DialogManager::singleton().set_floating_dialog_visibility(window, true);
            }
        }
        return nullptr;
    }
    
    // check if this dialog *was* open and floating; if so recreate its window
    if (auto state = DialogManager::singleton().find_dialog_state(dialog_type)) {
        if (recreate_dialogs_from_state(_inkscape_window, state.get())) {
            return nullptr;
        }
    }

    // Create the dialog widget
    DialogBase *dialog = dialog_factory(dialog_type).release(); // Evil, but necessitated by GTK.

    if (!dialog) {
        std::cerr << "DialogContainer::new_dialog(): couldn't find dialog for: " << dialog_type.raw() << std::endl;
        return nullptr;
    }

    // manage the dialog instance
    dialog = Gtk::manage(dialog);

    // Create the notebook tab
    auto const &label = get_open_action_name(dialog_type);
    Gtk::Widget *tab =
        create_notebook_tab(dialog->get_name(), INKSCAPE_ICON("inkscape-logo"), label);

    // New temporary noteboook
    auto const notebook = Gtk::make_managed<DialogNotebook>(this);
    notebook->add_page(*dialog, *tab, dialog->get_name());

    return notebook->pop_tab_callback();
}

// toggle dialogs (visibility) is invoked on a top container embedded in Inkscape window
void DialogContainer::toggle_dialogs()
{
    // check how many dialog panels are visible and how many are hidden
    // we use this info to decide what it means to toggle visibility
    int visible = 0;
    int hidden = 0;
    for (auto const child : columns->get_multipaned_children()) {
        // only examine panels, skip drop zones and handles
        if (auto panel = dynamic_cast<DialogMultipaned*>(child)) {
            if (panel->is_visible()) {
                ++visible;
            } else {
                ++hidden;
            }
        }
    }

    // next examine floating dialogs
    auto windows = DialogManager::singleton().get_all_floating_dialog_windows();
    for (auto wnd : windows) {
        if (wnd->is_visible()) {
            ++visible;
        }
        else {
            ++hidden;
        }
    }

    bool show_dialogs = true;
    // if some dialogs are hidden, toggle will first show them;
    // another option could be to hide all if some dialogs are visible
    if (hidden > 0) {
        show_dialogs = true;
    }
    else {
        // if everything's visible, hide them
        show_dialogs = false;
    }

    // set visibility of floating dialogs
    for (auto wnd : windows) {
        DialogManager::singleton().set_floating_dialog_visibility(wnd, show_dialogs);
    }

    // set visibility of docked dialogs
    columns->toggle_multipaned_children(show_dialogs);
}

// Update dialogs
void DialogContainer::update_dialogs()
{
    for_each(dialogs.begin(), dialogs.end(), [&](auto dialog) { dialog.second->update(); });
}

void DialogContainer::set_inkscape_window(InkscapeWindow* inkscape_window)
{
    g_assert(inkscape_window != nullptr);
    _inkscape_window = inkscape_window;
    auto desktop = _inkscape_window->get_desktop();
    for_each(dialogs.begin(), dialogs.end(), [&](auto dialog) { dialog.second->setDesktop(desktop); });
}

bool DialogContainer::has_dialog_of_type(DialogBase *dialog)
{
    return (dialogs.find(dialog->get_type()) != dialogs.end());
}

DialogBase *DialogContainer::get_dialog(const Glib::ustring& dialog_type)
{
    auto found = dialogs.find(dialog_type);
    if (found != dialogs.end()) {
        return found->second;
    }
    return nullptr;
}

// Add dialog to list.
void DialogContainer::link_dialog(DialogBase *dialog)
{
    dialogs.insert(std::pair<Glib::ustring, DialogBase *>(dialog->get_type(), dialog));

    DialogWindow *window = dynamic_cast<DialogWindow *>(get_root());
    if (window) {
        window->update_dialogs();
    }
    else {
        // dialog without DialogWindow has been docked; remove it's floating state
        // so if user closes and reopens it, it shows up docked again, not floating
        DialogManager::singleton().remove_dialog_floating_state(dialog->get_type());
    }
}

// Remove dialog from list.
void DialogContainer::unlink_dialog(DialogBase *dialog)
{
    if (!dialog) {
        return;
    }

    auto found = dialogs.find(dialog->get_type());
    if (found != dialogs.end()) {
        dialogs.erase(found);
    }

    DialogWindow *window = dynamic_cast<DialogWindow *>(get_root());
    if (window) {
        window->update_dialogs();
    }
}

/**
 * Load last open window's dialog configuration state.
 *
 * For the keyfile format, check `save_container_state()`.
 */
void DialogContainer::load_container_state(Glib::KeyFile *keyfile, bool include_floating)
{
    // Step 1: check if we want to load the state
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    // if it isn't dockable, all saved docked dialogs are made floating
    bool is_dockable =
        prefs->getInt("/options/dialogtype/value", PREFS_DIALOGS_BEHAVIOR_DOCKABLE) != PREFS_DIALOGS_BEHAVIOR_FLOATING;

    // Step 2: get the number of windows
    int windows_count = keyfile->get_integer("Windows", "Count");

    // Step 3: for each window, load its state. Only the first window is not floating (the others are DialogWindow)
    for (int window_idx = 0; window_idx < windows_count; ++window_idx) {
        if (window_idx > 0 && !include_floating)
            break;

        Glib::ustring group_name = "Window" + std::to_string(window_idx);

        // Step 3.0: read the window parameters
        int column_count = 0;
        bool floating = window_idx != 0;
        window_position_t pos;
        bool has_position = false;
        try {
            column_count = keyfile->get_integer(group_name, "ColumnCount");
            floating = keyfile->get_boolean(group_name, "Floating");
            if (keyfile->has_key(group_name, "Position") && keyfile->get_boolean(group_name, "Position")) {
                pos.x = keyfile->get_integer(group_name, "x");
                pos.y = keyfile->get_integer(group_name, "y");
                pos.width = keyfile->get_integer(group_name, "width");
                pos.height = keyfile->get_integer(group_name, "height");
                has_position = true;
            }
        } catch (Glib::Error const &error) {
            std::cerr << "DialogContainer::load_container_state: " << error.what().raw() << std::endl;
        }

        // Step 3.1: get the window's container columns where we want to create the dialogs
        DialogContainer *active_container = nullptr;
        DialogMultipaned *active_columns = nullptr;
        DialogWindow *dialog_window = nullptr;

        if (is_dockable) {
            if (floating) {
                dialog_window = new DialogWindow(_inkscape_window, nullptr);
                if (dialog_window) {
                    active_container = dialog_window->get_container();
                    active_columns = dialog_window->get_container()->get_columns();
                }
            } else {
                active_container = this;
                active_columns = columns.get();
            }

            if (!active_container || !active_columns) {
                continue;
            }
        }

        // Step 3.2: for each column, load its state
        for (int column_idx = 0; column_idx < column_count; ++column_idx) {
            Glib::ustring column_group_name = group_name + "Column" + std::to_string(column_idx);

            // Step 3.2.0: read the column parameters
            int notebook_count = 0;
            bool before_canvas = false;
            try {
                notebook_count = keyfile->get_integer(column_group_name, "NotebookCount");
                before_canvas = keyfile->get_boolean(column_group_name, "BeforeCanvas");
            } catch (Glib::Error const &error) {
                std::cerr << "DialogContainer::load_container_state: " << error.what().raw() << std::endl;
            }

            // Step 3.2.1: create the column
            DialogMultipaned *column = nullptr;
            if (is_dockable) {
                column = active_container->create_column();
                if (!column) {
                    continue;
                }

                if (keyfile->has_key(column_group_name, "ColumnWidth")) {
                    auto width = keyfile->get_integer(column_group_name, "ColumnWidth");
                    column->set_restored_width(width);
                }

                before_canvas ? active_columns->prepend(*column) : active_columns->append(*column);
            }

            // Step 3.2.2: for each noteboook, load its dialogs
            for (int notebook_idx = 0; notebook_idx < notebook_count; ++notebook_idx) {
                auto const key = get_key(notebook_idx);

                // Step 3.2.2.0 read the list of dialogs in the current notebook
                std::vector<Glib::ustring> dialogs;
                try {
                    dialogs = keyfile->get_string_list(column_group_name, key);
                } catch (Glib::Error const &error) {
                    std::cerr << "DialogContainer::load_container_state: " << error.what().raw() << std::endl;
                }

                if (!dialogs.size()) {
                    continue;
                }

                DialogNotebook *notebook = nullptr;
                if (is_dockable) {
                    notebook = Gtk::make_managed<DialogNotebook>(active_container);
                    column->append(*notebook);
                }

                auto const &dialog_data = get_dialog_data();
                // Step 3.2.2.1 create each dialog in the current notebook
                for (auto type : dialogs) {

                    if (dialog_data.find(type) != dialog_data.end()) {
                        if (is_dockable) {
                            active_container->new_dialog(type, notebook);
                        } else {
                            dialog_window = create_new_floating_dialog(type, false);
                        }
                    } else {
                        std::cerr << "load_container_state: invalid dialog type: " << type.raw() << std::endl;
                    }
                }

                if (notebook) {
                    Glib::ustring row = "Notebook" + std::to_string(notebook_idx) + "Height";
                    if (keyfile->has_key(column_group_name, row)) {
                        auto height = keyfile->get_integer(column_group_name, row);
                        notebook->set_requested_height(height);
                    }
                    Glib::ustring tab = "Notebook" + std::to_string(notebook_idx) + "ActiveTab";
                    if (keyfile->has_key(column_group_name, tab)) {
                        if (auto nb = notebook->get_notebook()) {
                            auto page = keyfile->get_integer(column_group_name, tab);
                            nb->set_current_page(page);
                        }
                    }
                }
            }
        }

        if (dialog_window) {
            if (has_position) {
                dm_restore_window_position(*dialog_window, pos);
            } else {
                dialog_window->update_window_size_to_fit_children();
            }

            dialog_window->set_visible(true);

            // Set the style and icon theme of the new menu based on the desktop
        }
    }
    INKSCAPE.themecontext->getChangeThemeSignal().emit();
    INKSCAPE.themecontext->add_gtk_css(true);
}

void save_wnd_position(Glib::KeyFile *keyfile, const Glib::ustring &group_name, const window_position_t *position)
{
    keyfile->set_boolean(group_name, "Position", position != nullptr);
    if (position) { // floating window position?
        keyfile->set_integer(group_name, "x", position->x);
        keyfile->set_integer(group_name, "y", position->y);
        keyfile->set_integer(group_name, "width", position->width);
        keyfile->set_integer(group_name, "height", position->height);
    }
}

[[nodiscard]] static auto get_notebook_dialogs(DialogNotebook &dialog_notebook)
{
    auto const notebook = dialog_notebook.get_notebook();
    g_assert(notebook);
    std::vector<Glib::ustring> dialogs;
    for (auto const child : UI::get_children(*notebook)) {
        if (auto const dialog = dynamic_cast<DialogBase *>(child)) {
            dialogs.push_back(dialog->get_type());
        }
    }
    return dialogs;
}

// get *this* container's state only; store window 'position' in the state if given
std::shared_ptr<Glib::KeyFile> DialogContainer::get_container_state(const window_position_t *position) const
{
    static constexpr int window_idx = 0;

    auto const keyfile = std::make_shared<Glib::KeyFile>();

    // Step 2: save the number of windows
    keyfile->set_integer("Windows", "Count", 1);

    // Step 3.0: get all the multipanes of the window
    std::vector<DialogMultipaned const *> multipanes;

    for (auto const column : columns->get_multipaned_children()) {
        if (auto paned = dynamic_cast<DialogMultipaned *>(column)) {
            multipanes.push_back(paned);
        }
    }

    // Step 3.1: for each non-empty column, save its data.
    int column_count = 0; // non-empty columns count
    for (size_t column_idx = 0; column_idx < multipanes.size(); ++column_idx) {
        Glib::ustring group_name = "Window" + std::to_string(window_idx) + "Column" + std::to_string(column_idx);
        int notebook_count = 0; // non-empty notebooks count

        // Step 3.1.0: for each notebook, get its dialogs
        for (auto const columns_widget : multipanes[column_idx]->get_multipaned_children()) {
            if (auto const dialog_notebook = dynamic_cast<DialogNotebook *>(columns_widget)) {
                // save the dialogs type
                auto const key = get_key(notebook_count);
                keyfile->set_string_list(group_name, key, get_notebook_dialogs(*dialog_notebook));

                // increase the notebook count
                notebook_count++;
            }
        }

        // Step 3.1.1: increase the column count
        if (notebook_count != 0) {
            column_count++;
        }

        // Step 3.1.2: Save the column's data
        keyfile->set_integer(group_name, "NotebookCount", notebook_count);
    }

    // Step 3.2: save the window group
    Glib::ustring group_name = "Window" + std::to_string(window_idx);
    keyfile->set_integer(group_name, "ColumnCount", column_count);
    save_wnd_position(keyfile.get(), group_name, position);

    return keyfile;
}

/**
 * Save container state. The configuration of open dialogs and the relative positions of the notebooks are saved.
 *
 * The structure of such a KeyFile is:
 *
 * There is a "Windows" group that records the number of the windows:
 * [Windows]
 * Count=1
 *
 * A "WindowX" group saves the number of columns the window's container has and whether the window is floating:
 *
 * [Window0]
 * ColumnCount=1
 * Floating=false
 *
 * For each column, we have a "WindowWColumnX" group, where X is the index of the column. "BeforeCanvas" checks
 * if the column is before the canvas or not. "NotebookCount" records how many notebooks are in each column and
 * "NotebookXDialogs" records a list of the types for the dialogs in notebook X.
 *
 * [Window0Column0]
 * Notebook0Dialogs=Text;
 * NotebookCount=2
 * BeforeCanvas=false
 *
 */
std::unique_ptr<Glib::KeyFile> DialogContainer::save_container_state()
{
    std::unique_ptr<Glib::KeyFile> keyfile = std::make_unique<Glib::KeyFile>();
    auto app = InkscapeApplication::instance();

    // Step 1: get all the container columns (in order, from the current container and all DialogWindow containers)
    std::vector<DialogMultipaned const *> windows       (1, columns.get());
    std::vector<DialogWindow           *> dialog_windows(1, nullptr      );

    for (auto const &window : app->gtk_app()->get_windows()) {
        DialogWindow *dialog_window = dynamic_cast<DialogWindow *>(window);
        if (dialog_window) {
            windows.push_back(dialog_window->get_container()->get_columns());
            dialog_windows.push_back(dialog_window);
        }
    }

    // Step 2: save the number of windows
    keyfile->set_integer("Windows", "Count", windows.size());

    // Step 3: for each window, save its data. Only the first window is not floating (the others are DialogWindow)
    for (int window_idx = 0; window_idx < (int)windows.size(); ++window_idx) {
        // Step 3.0: get all the multipanes of the window
        std::vector<DialogMultipaned const *> multipanes;

        // used to check if the column is before or after canvas
        auto multipanes_it = multipanes.begin();
        bool canvas_seen = window_idx != 0; // no floating windows (window_idx > 0) have a canvas
        int before_canvas_columns_count = 0;

        for (auto const column : windows[window_idx]->get_multipaned_children()) {
            if (!canvas_seen) {
                UI::Widget::CanvasGrid *canvas = dynamic_cast<UI::Widget::CanvasGrid *>(column);
                if (canvas) {
                    canvas_seen = true;
                } else {
                    DialogMultipaned *paned = dynamic_cast<DialogMultipaned *>(column);
                    if (paned) {
                        multipanes_it = multipanes.insert(multipanes_it, paned);
                        before_canvas_columns_count++;
                    }
                }
            } else {
                DialogMultipaned *paned = dynamic_cast<DialogMultipaned *>(column);
                if (paned) {
                    multipanes.push_back(paned);
                }
            }
        }

        // Step 3.1: for each non-empty column, save its data.
        int column_count = 0; // non-empty columns count
        for (int column_idx = 0; column_idx < (int)multipanes.size(); ++column_idx) {
            Glib::ustring group_name = "Window" + std::to_string(window_idx) + "Column" + std::to_string(column_idx);
            int notebook_count = 0; // non-empty notebooks count
            int width = multipanes[column_idx]->get_allocated_width();

            // Step 3.1.0: for each notebook, get its dialogs' types
            for (auto const columns_widget : multipanes[column_idx]->get_multipaned_children()) {
                if (auto const dialog_notebook = dynamic_cast<DialogNotebook *>(columns_widget)) {
                    // save the dialogs type
                    auto const key = get_key(notebook_count);
                    keyfile->set_string_list(group_name, key, get_notebook_dialogs(*dialog_notebook));

                    // save height; useful when there are multiple "rows" of docked dialogs
                    Glib::ustring row = "Notebook" + std::to_string(notebook_count) + "Height";
                    keyfile->set_integer(group_name, row, dialog_notebook->get_allocated_height());
                    if (auto notebook = dialog_notebook->get_notebook()) {
                        Glib::ustring row = "Notebook" + std::to_string(notebook_count) + "ActiveTab";
                        keyfile->set_integer(group_name, row, notebook->get_current_page());
                    }

                    // increase the notebook count
                    notebook_count++;
                }
            }

            // Step 3.1.1: increase the column count
            if (notebook_count != 0) {
                column_count++;
            }

            keyfile->set_integer(group_name, "ColumnWidth", width);

            // Step 3.1.2: Save the column's data
            keyfile->set_integer(group_name, "NotebookCount", notebook_count);
            keyfile->set_boolean(group_name, "BeforeCanvas", (column_idx < before_canvas_columns_count));
        }

        // Step 3.2: save the window group
        Glib::ustring group_name = "Window" + std::to_string(window_idx);
        keyfile->set_integer(group_name, "ColumnCount", column_count);
        keyfile->set_boolean(group_name, "Floating", window_idx != 0);
        if (window_idx != 0) { // floating?
            if (auto const wnd = dynamic_cast<DialogWindow *>(dialog_windows.at(window_idx))) {
                // store window position
                auto pos = dm_get_window_position(*wnd);
                save_wnd_position(keyfile.get(), group_name, pos ? &*pos : nullptr);
            }
        }
    }

    return keyfile;
}

// Signals -----------------------------------------------------

/**
 * No zombie windows. TODO: Need to work on this as it still leaves Gtk::Window! (?)
 */
void DialogContainer::on_unrealize() {
    // Disconnect all signals
    connections.clear();

    columns.reset();

    parent_type::on_unrealize();
}

// Create a new notebook and move page.
DialogNotebook *DialogContainer::prepare_drop(Gtk::SelectionData const &selection_data)
{
    if (selection_data.get_target() != "GTK_NOTEBOOK_TAB") {
        std::cerr << "DialogContainer::prepare_drop: tab not found!" << std::endl;
        return nullptr;
    }

    // Find page
    auto const cpage = reinterpret_cast<GtkWidget **>(const_cast<unsigned char *>(selection_data.get_data()));
    g_assert(cpage);
    g_assert(GTK_IS_WIDGET(*cpage));
    auto const page = Glib::wrap(*cpage);
    if (!page) {
        std::cerr << "DialogContainer::prepare_drop: page not found!" << std::endl;
        return nullptr;
    }

    // Create new notebook and move page.
    auto const new_notebook = Gtk::make_managed<DialogNotebook>(this);
    new_notebook->move_page(*page);

    // move_page() takes care of updating dialog lists.
    INKSCAPE.themecontext->getChangeThemeSignal().emit();
    INKSCAPE.themecontext->add_gtk_css(true);
    return new_notebook;
}

void DialogContainer::take_drop(PrependOrAppend const prepend_or_append,
                                Gtk::SelectionData const &selection_data,
                                DialogMultipaned * const multipane)
{
    auto const new_notebook = prepare_drop(selection_data); // Creates notebook, moves page.
    if (!new_notebook) {
        std::cerr << "DialogContainer::take_drop: no new notebook!" << std::endl;
        return;
    }

    if (multipane->get_orientation() == Gtk::Orientation::HORIZONTAL) {
        DialogMultipaned *column = create_column();
        (column->*prepend_or_append)(*new_notebook);
        (columns.get()->*prepend_or_append)(*column);
    } else {
        (multipane->*prepend_or_append)(*new_notebook);
    }

    update_dialogs(); // Always update dialogs on Notebook change
}

// Notebook page dropped on prepend target. Call function to create new notebook and then insert.
void DialogContainer::prepend_drop(Gtk::SelectionData const &selection_data,
                                   DialogMultipaned * const multipane)
{
    take_drop(&DialogMultipaned::prepend, selection_data, multipane);
}

// Notebook page dropped on append target. Call function to create new notebook and then insert.
void DialogContainer::append_drop(Gtk::SelectionData const &selection_data,
                                  DialogMultipaned * const multipane)
{
    take_drop(&DialogMultipaned::append, selection_data, multipane);
}

/**
 * If a DialogMultipaned column is empty and it can be removed, remove it
 */
void DialogContainer::column_empty(DialogMultipaned *column)
{
    DialogMultipaned *parent = dynamic_cast<DialogMultipaned *>(column->get_parent());
    if (parent) {
        parent->remove(*column);
    }

    DialogWindow *window = dynamic_cast<DialogWindow *>(get_root());
    if (window && parent) {
        auto const &children = parent->get_multipaned_children();
        // Close the DialogWindow if you're in an empty one
        if (children.size() == 3 && parent->has_empty_widget()) {
            window->close();
        }
    }
}

} // namespace Inkscape::UI::Dialog

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
