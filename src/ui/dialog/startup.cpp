// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A dialog for the about screen
 */
/*
 * Copyright (C) Martin Owens 2019 <doctormo@gmail.com>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "startup.h"

#include <limits>
#include <string>
#include <glibmm/i18n.h>
#include <glibmm/fileutils.h>
#include <gdkmm/display.h>
#include <gtkmm/builder.h>
#include <gtkmm/button.h>
#include <gtkmm/combobox.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/infobar.h>
#include <gtkmm/liststore.h>
#include <gtkmm/notebook.h>
#include <gtkmm/overlay.h>
#include <gtkmm/recentmanager.h>
#include <gtkmm/settings.h>
#include <gtkmm/stack.h>
#include <gtkmm/styleprovider.h>
#include <gtkmm/switch.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/treeview.h>
#include <gtkmm/window.h>

#include "color.h"
#include "color-rgba.h"
#include "inkscape-application.h"
#include "inkscape.h"
#include "inkscape-version.h"
#include "inkscape-version-info.h"
#include "io/resource.h"
#include "preferences.h"
#include "ui/builder-utils.h"
#include "ui/controller.h"
#include "ui/dialog/filedialog.h"
#include "ui/shortcuts.h"
#include "ui/themes.h"
#include "ui/util.h"
#include "ui/widget/template-list.h"
#include "util/units.h"

using namespace Inkscape::IO;
using namespace Inkscape::UI::View;

namespace Inkscape::UI::Dialog {

class NameIdCols: public Gtk::TreeModel::ColumnRecord {
    public:
        // These types must match those for the model in the .glade file
        NameIdCols() {
            this->add(this->col_name);
            this->add(this->col_id);
        }
        Gtk::TreeModelColumn<Glib::ustring> col_name;
        Gtk::TreeModelColumn<Glib::ustring> col_id;
};

class RecentCols: public Gtk::TreeModel::ColumnRecord {
    public:
        // These types must match those for the model in the .glade file
        RecentCols() {
            this->add(this->col_name);
            this->add(this->col_id);
            this->add(this->col_dt);
            this->add(this->col_crash);
        }
        Gtk::TreeModelColumn<Glib::ustring> col_name;
        Gtk::TreeModelColumn<Glib::ustring> col_id;
        Gtk::TreeModelColumn<gint64> col_dt;
        Gtk::TreeModelColumn<bool> col_crash;
};

class CanvasCols: public Gtk::TreeModel::ColumnRecord {
    public:
        // These types must match those for the model in the .glade file
        CanvasCols() {
            this->add(this->id);
            this->add(this->name);
            this->add(this->icon_filename);
            this->add(this->pagecolor);
            this->add(this->checkered);
            this->add(this->bordercolor);
            this->add(this->shadow);
            this->add(this->deskcolor);
        }
        Gtk::TreeModelColumn<Glib::ustring> id;
        Gtk::TreeModelColumn<Glib::ustring> name;
        Gtk::TreeModelColumn<Glib::ustring> icon_filename;
        Gtk::TreeModelColumn<Glib::ustring> pagecolor;
        Gtk::TreeModelColumn<bool> checkered;
        Gtk::TreeModelColumn<Glib::ustring> bordercolor;
        Gtk::TreeModelColumn<bool> shadow;
        Gtk::TreeModelColumn<Glib::ustring> deskcolor;
};

class ThemeCols: public Gtk::TreeModel::ColumnRecord {
    public:
        // These types must match those for the model in the .glade file
        ThemeCols() {
            this->add(this->id);
            this->add(this->name);
            this->add(this->theme);
            this->add(this->icons);
            this->add(this->base);
            this->add(this->base_dark);
            this->add(this->success);
            this->add(this->warn);
            this->add(this->error);
            this->add(this->symbolic);
            this->add(this->smallicons);
            this->add(this->enabled);
        }
        Gtk::TreeModelColumn<Glib::ustring> id;
        Gtk::TreeModelColumn<Glib::ustring> name;
        Gtk::TreeModelColumn<Glib::ustring> theme;
        Gtk::TreeModelColumn<Glib::ustring> icons;
        Gtk::TreeModelColumn<Glib::ustring> base;
        Gtk::TreeModelColumn<Glib::ustring> base_dark;
        Gtk::TreeModelColumn<Glib::ustring> success;
        Gtk::TreeModelColumn<Glib::ustring> warn;
        Gtk::TreeModelColumn<Glib::ustring> error;
        Gtk::TreeModelColumn<bool> symbolic;
        Gtk::TreeModelColumn<bool> smallicons;
        Gtk::TreeModelColumn<bool> enabled;
};

/**
 * Color is store as a string in the form #RRGGBBAA, '0' means "unset"
 *
 * @param color - The string color from glade.
 */
unsigned int get_color_value(const Glib::ustring color)
{
    Gdk::RGBA gdk_color = Gdk::RGBA(color);
    ColorRGBA  sp_color(gdk_color.get_red(), gdk_color.get_green(),
                        gdk_color.get_blue(), gdk_color.get_alpha());
    return sp_color.getIntValue();
}

using Inkscape::UI::Widget::TemplateList;

StartScreen::StartScreen()
    : Gtk::Dialog()
    , builder(create_builder("inkscape-start.glade"))
    , window         (get_widget<Gtk::Window>  (builder, "start-screen-window"))
      // Global widgets
    , tabs           (get_widget<Gtk::Notebook>        (builder, "tabs"))
    , templates      (get_derived_widget<TemplateList> (builder, "kinds"))
    , banners        (get_widget<Gtk::Overlay>         (builder, "banner"))
    , themes         (get_widget<Gtk::ComboBox>        (builder, "themes"))
    , recent_treeview(get_widget<Gtk::TreeView>        (builder, "recent_treeview"))
    , load_btn       (get_widget<Gtk::Button>          (builder, "load"))
{
    set_name("start-screen-window");
    set_title(Inkscape::inkscape_version());
    set_focusable(true);
    grab_focus();
    set_receives_default(true);
    set_default_widget(*this);
    // set_urgency_hint(true);  // Draw user's attention to this window! // Gone.
    set_modal(true);
    // set_position(Gtk::WIN_POS_CENTER_ALWAYS); // Gmone.
    set_default_size(700, 360);

    // Populate with template extensions
    templates.init(Inkscape::Extension::TEMPLATE_NEW_WELCOME);

    // Get references to various widget used locally. (In order of appearance.)
    auto canvas      = &get_widget<Gtk::ComboBox>(builder, "canvas");
    auto keys        = &get_widget<Gtk::ComboBox>(builder, "keys");
    auto save        = &get_widget<Gtk::Button>  (builder, "save");
    auto thanks      = &get_widget<Gtk::Button>  (builder, "thanks");
    auto close_btn   = &get_widget<Gtk::Button>  (builder, "close_window");
    auto new_btn     = &get_widget<Gtk::Button>  (builder, "new");
    auto show_toggle = &get_widget<Gtk::Button>  (builder, "show_toggle");
    auto dark_toggle = &get_widget<Gtk::Switch>  (builder, "dark_toggle");

    // Unparent to move to our dialog window.
    remove_banners_and_tabs(window);

    // Add signals and setup things.
    auto prefs = Inkscape::Preferences::get();

    Controller::add_key<&StartScreen::on_key_pressed>(*this, *this);
    tabs.signal_switch_page().connect(sigc::mem_fun(*this, &StartScreen::notebook_switch));

    // Setup the lists of items
    enlist_recent_files();
    enlist_keys();
    filter_themes();
    set_active_combo("themes", prefs->getString("/options/boot/theme"));
    set_active_combo("canvas", prefs->getString("/options/boot/canvas"));

    // initialise dark depending on prefs and background
    refresh_dark_switch();

    // Welcome! tab
    auto const welcome_text_file = Resource::get_filename(Resource::SCREENS,
                                                          "start-welcome-text.svg", true);
    get_widget<Gtk::Image>(builder, "welcome_text").set(welcome_text_file);
    
    canvas->signal_changed().connect(sigc::mem_fun(*this, &StartScreen::canvas_changed));
    keys->signal_changed().connect(sigc::mem_fun(*this, &StartScreen::keyboard_changed));
    themes.signal_changed().connect(sigc::mem_fun(*this, &StartScreen::theme_changed));
    dark_toggle->property_active().signal_changed().connect(sigc::mem_fun(*this, &StartScreen::theme_changed));
    save->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &StartScreen::notebook_next), save));

    // "Supported by You" tab
    thanks->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &StartScreen::notebook_next), thanks));

    // "Time to Draw" tab
    recent_treeview.signal_row_activated().connect(sigc::hide(sigc::hide((sigc::mem_fun(*this, &StartScreen::load_document)))));
    recent_treeview.get_selection()->signal_changed().connect(sigc::mem_fun(*this, &StartScreen::on_recent_changed));
    templates.signal_switch_page().connect(sigc::mem_fun(*this, &StartScreen::on_kind_changed));
    load_btn.set_sensitive(true);

    show_toggle->signal_clicked().connect(sigc::mem_fun(*this, &StartScreen::show_toggle));
    load_btn.signal_clicked().connect(sigc::mem_fun(*this, &StartScreen::load_document));
    templates.connectItemSelected(sigc::mem_fun(*this, &StartScreen::new_document));
    new_btn->signal_clicked().connect(sigc::mem_fun(*this, &StartScreen::new_document));
    close_btn->signal_clicked().connect([this] { response(GTK_RESPONSE_CANCEL); });

    // Reparent to our dialog window
    set_titlebar(banners);
    Gtk::Box* box = get_content_area();
    box->append(tabs);

    // Show the first tab ONLY on the first run for this version
    std::string opt_shown = "/options/boot/shown/ver";
    opt_shown += Inkscape::version_string_without_revision;
    if (!prefs->getBool(opt_shown, false)) {
        theme_changed();
        tabs.set_current_page(0);
        prefs->setBool(opt_shown, true);
    } else {
        tabs.set_current_page(2);
        notebook_switch(nullptr, 2);
    }

    set_modal(true);
    // set_position(Gtk::WIN_POS_CENTER_ALWAYS); // Gone.
    property_resizable() = false;
    set_default_size(700, 360);
    set_visible(true);
}

StartScreen::~StartScreen()
{
    // These are "owned" by builder... don't delete them!
    remove_banners_and_tabs(*this);
}

/**
 * Return the active row of the named combo box.
 *
 * @param widget_name - The name of the widget in the glade file
 * @return Gtk Row object ready for use.
 * @throws Two errors depending on where it failed.
 */
Gtk::TreeModel::Row
StartScreen::active_combo(std::string widget_name)
{
    auto &combo = get_widget<Gtk::ComboBox>(builder, widget_name.c_str());
    Gtk::TreeModel::iterator iter = combo.get_active();
    if (!iter) throw 2;
    Gtk::TreeModel::Row row = *iter;
    if (!row) throw 3;
    return row;
}

/**
 * Set the active item in the combo based on the unique_id (column set in glade)
 *
 * @param widget_name - The name of the widget in the glade file
 * @param unique_id - The column id to activate, sets to first item if blank.
 */
void
StartScreen::set_active_combo(std::string widget_name, std::string unique_id)
{
    auto &combo = get_widget<Gtk::ComboBox>(builder, widget_name.c_str());
    if (unique_id.empty()) {
        combo.set_active(0); // Select the first
    } else if (!combo.set_active_id(unique_id)) {
        combo.set_active(-1); // Select nothing
    }
}

/**
 * When a notbook is switched, reveal the right banner image (gtk signal).
 */
void
StartScreen::notebook_switch(Gtk::Widget *tab, unsigned page_num)
{
    auto &stack = get_widget<Gtk::Stack>(builder, "banner-stack");
    auto const pages = UI::get_children(stack);
    auto &page = *pages.at(page_num);
    stack.set_visible_child(page);
}

void
StartScreen::enlist_recent_files()
{
    RecentCols cols;

    // We're not sure why we have to ask C for the TreeStore object
    auto store = Glib::wrap(GTK_LIST_STORE(gtk_tree_view_get_model(recent_treeview.gobj())));
    store->clear();
    // Now sort the result by visited time
    store->set_sort_column(cols.col_dt, Gtk::SortType::DESCENDING);

    // Open [other]
    Gtk::TreeModel::Row first_row = *(store->append());
    first_row[cols.col_name] = _("Browse for other files...");
    first_row[cols.col_id] = "";
    first_row[cols.col_dt] = std::numeric_limits<gint64>::max();
    recent_treeview.get_selection()->select(store->get_path(first_row.get_iter()));

    Glib::RefPtr<Gtk::RecentManager> manager = Gtk::RecentManager::get_default();
    for (auto item : manager->get_items()) {
        if (item->has_application(g_get_prgname())
            || item->has_application("org.inkscape.Inkscape")
            || item->has_application("inkscape")
            || item->has_application("inkscape.exe")
           ) {
            // This uri is a GVFS uri, so parse it with that or it will fail.
            auto file = Gio::File::create_for_uri(item->get_uri());
            std::string path = file->get_path();
            if (!path.empty() && Glib::file_test(path, Glib::FileTest::IS_REGULAR)
                && item->get_mime_type() == "image/svg+xml") {
                Gtk::TreeModel::Row row = *(store->append());
                row[cols.col_name] = item->get_display_name();
                row[cols.col_id] = item->get_uri();
                row[cols.col_dt] = item->get_modified().to_unix();
                row[cols.col_crash] = item->has_group("Crash");
            }
        }
    }

}

/**
 * Called when a new recent document is selected.
 */
void
StartScreen::on_recent_changed()
{
    // TODO: In the future this is where previews and other information can be loaded.
}

/**
 * Called when the left side tabs are changed.
 */
void
StartScreen::on_kind_changed(Gtk::Widget *tab, unsigned page_num)
{
    if (page_num == 0) {
        load_btn.set_visible(true);
    } else {
        load_btn.set_visible(false);
    }
}

/**
 * Called when new button clicked or template is double clicked, or escape pressed.
 */
void
StartScreen::new_document()
{
    // Generate a new document from the selected template.
    _document = templates.new_document();
    if (_document) {
    // Quit welcome screen if options not 'canceled'
        response(GTK_RESPONSE_APPLY);
    }
}

/**
 * Called when load button clicked.
 */
void
StartScreen::load_document()
{
    RecentCols cols;
    auto app = InkscapeApplication::instance();

    auto iter = recent_treeview.get_selection()->get_selected();
    if (iter) {
        Gtk::TreeModel::Row row = *iter;
        if (row) {
            Glib::ustring uri = row[cols.col_id];
            Glib::RefPtr<Gio::File> file;

            if (!uri.empty()) {
                file = Gio::File::create_for_uri(uri);
            } else {
                // Browse for file instead
                std::string open_path;
                get_start_directory(open_path, "/dialogs/open/path");

                auto browser = Inkscape::UI::Dialog::FileOpenDialog::create(
                    *this, open_path, Inkscape::UI::Dialog::SVG_TYPES, _("Open a different file"));
                browser->setSelectMultiple(false); // We can only handle one document via start up screen!

                if (browser->show()) {
                    auto prefs = Inkscape::Preferences::get();
                    prefs->setString("/dialogs/open/path", browser->getCurrentDirectory());
                    file = browser->getFile();
                    delete browser;
                } else {
                    delete browser;
                    return; // Cancel
                }
            }

            // Now we have file, open document.
            bool canceled = false;
            _document = app->document_open(file, &canceled);

            if (!canceled && _document) {
                // We're done, hand back to app.
                response(GTK_RESPONSE_OK);
            }
        }
    }
}

/**
 * When a button needs to go to the next notebook page.
 */
void
StartScreen::notebook_next(Gtk::Widget *button)
{
    int page = tabs.get_current_page();
    if (page == 2) {
        response(GTK_RESPONSE_CANCEL); // Only occurs from keypress.
    } else {
        tabs.set_current_page(page + 1);
    }
}

/**
 * When a key is pressed in the main window.
 */
bool StartScreen::on_key_pressed(GtkEventControllerKey const * /*controller*/,
                                 unsigned const keyval, unsigned /*keycode*/,
                                 GdkModifierType const state)
{
#ifdef GDK_WINDOWING_QUARTZ
    // On macOS only, if user press Cmd+Q => exit
    if (keyval == 'q' && state == (GDK_MOD2_MASK | GDK_META_MASK)) {
        close();
        return false;
    }
#endif

    switch (keyval) {
        case GDK_KEY_Escape:
            // Prevent loading any selected items
            response(GTK_RESPONSE_CANCEL);
            return true;
        case GDK_KEY_Return:
            notebook_next(nullptr);
            return true;
    }

    return false;
}

void
StartScreen::on_response(int response_id)
{
    if (response_id == GTK_RESPONSE_DELETE_EVENT) {
        // Don't open a window for force closing.
        return;
    }
    if (response_id == GTK_RESPONSE_CANCEL) {
        templates.reset_selection();
    }
    if (response_id != GTK_RESPONSE_OK && !_document) {
        // Last ditch attempt to generate a new document while exiting.
        _document = templates.new_document();
    }
}

void
StartScreen::show_toggle()
{
    auto &button = get_widget<Gtk::ToggleButton>(builder, "show_toggle");
    auto prefs = Inkscape::Preferences::get();
    prefs->setBool("/options/boot/enabled", button.get_active());

}

/**
 * Refresh theme in-place so user can see a semi-preview. This theme selection
 * is not meant to be perfect, but hint to the user that they can set the
 * theme if they want.
 *
 * @param theme_name - The name of the theme to load.
 */
void
StartScreen::refresh_theme(Glib::ustring theme_name)
{
    auto const display = Gdk::Display::get_default();

    if (INKSCAPE.themecontext->getContrastThemeProvider()) {
        Gtk::StyleProvider::remove_provider_for_display(display, INKSCAPE.themecontext->getContrastThemeProvider());
    }

    auto settings = Gtk::Settings::get_default();
    auto prefs = Inkscape::Preferences::get();

    settings->property_gtk_theme_name() = theme_name;
    settings->property_gtk_application_prefer_dark_theme() = prefs->getBool("/theme/preferDarkTheme", true);
    settings->property_gtk_icon_theme_name() = prefs->getString("/theme/iconTheme", prefs->getString("/theme/defaultIconTheme", ""));

    if (prefs->getBool("/theme/symbolicIcons", false)) {
        add_css_class("symbolic");
        remove_css_class("regular");
    } else {
        add_css_class("regular");
        remove_css_class("symbolic");
    }

    if (INKSCAPE.themecontext->getColorizeProvider()) {
        Gtk::StyleProvider::remove_provider_for_display(display, INKSCAPE.themecontext->getColorizeProvider());
    }

    if (!prefs->getBool("/theme/symbolicDefaultHighColors", false)) {
        Gtk::CssProvider::create();
        Glib::ustring css_str = INKSCAPE.themecontext->get_symbolic_colors();
        try {
            INKSCAPE.themecontext->getColorizeProvider()->load_from_string(css_str);
        } catch (Gtk::CssParserError const &ex) {
            g_critical("CSSProviderError::load_from_data(): failed to load '%s'\n(%s)", css_str.c_str(), ex.what());
        }
        Gtk::StyleProvider::add_provider_for_display(display, INKSCAPE.themecontext->getColorizeProvider(),
                                                     GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    // set dark switch and disable if there is no prefer option for dark
    refresh_dark_switch();

    INKSCAPE.themecontext->getChangeThemeSignal().emit();
}

/**
 * Set the theme, icon pack and other theme options from a set defined
 * in the glade file. The combo box has a number of columns with the needed
 * data describing how to set up the theme.
 */
void
StartScreen::theme_changed()
{
    auto prefs = Inkscape::Preferences::get();

    ThemeCols cols;
    try {
        auto row = active_combo("themes");
        Glib::ustring theme_id = row[cols.id];
        if (theme_id == "custom") return;
        prefs->setString("/options/boot/theme", row[cols.id]);

        // Update theme from combo.
        Glib::ustring icons = row[cols.icons];
        prefs->setBool("/toolbox/tools/small", row[cols.smallicons]);
        prefs->setString("/theme/gtkTheme", row[cols.theme]);
        prefs->setString("/theme/iconTheme", icons);
        prefs->setBool("/theme/symbolicIcons", row[cols.symbolic]);

        auto &dark_toggle = get_widget<Gtk::Switch>(builder, "dark_toggle");
        bool is_dark = dark_toggle.get_active();
        prefs->setBool("/theme/preferDarkTheme", is_dark);
        prefs->setBool("/theme/darkTheme", is_dark);
        // Symbolic icon colours
        if (get_color_value(row[cols.base]) == 0) {
            prefs->setBool("/theme/symbolicDefaultBaseColors", true);
            prefs->setBool("/theme/symbolicDefaultHighColors", true);
        } else {
            Glib::ustring prefix = "/theme/" + icons;
            prefs->setBool("/theme/symbolicDefaultBaseColors", false);
            prefs->setBool("/theme/symbolicDefaultHighColors", false);
            if (is_dark) {
                prefs->setUInt(prefix + "/symbolicBaseColor", get_color_value(row[cols.base_dark]));
            } else {
                prefs->setUInt(prefix + "/symbolicBaseColor", get_color_value(row[cols.base]));
            }
            prefs->setUInt(prefix + "/symbolicSuccessColor", get_color_value(row[cols.success]));
            prefs->setUInt(prefix + "/symbolicWarningColor", get_color_value(row[cols.warn]));
            prefs->setUInt(prefix + "/symbolicErrorColor", get_color_value(row[cols.error]));
        }

        refresh_theme(prefs->getString("/theme/gtkTheme", prefs->getString("/theme/defaultGtkTheme", "")));
    } catch(int e) {
        g_warning("Couldn't find theme value.");
    }
}

/**
 * Called when the canvas dropdown changes.
 */
void
StartScreen::canvas_changed()
{
    CanvasCols cols;
    try {
        auto row = active_combo("canvas");

        auto prefs = Inkscape::Preferences::get();
        prefs->setString("/options/boot/canvas", row[cols.id]);

        Gdk::RGBA gdk_color = Gdk::RGBA(row[cols.pagecolor]);
        SPColor sp_color(gdk_color.get_red(), gdk_color.get_green(), gdk_color.get_blue());
        prefs->setString("/template/base/pagecolor", sp_color.toString());
        prefs->setDouble("/template/base/pageopacity", gdk_color.get_alpha());

        Gdk::RGBA gdk_border = Gdk::RGBA(row[cols.bordercolor]);
        SPColor sp_border(gdk_border.get_red(), gdk_border.get_green(), gdk_border.get_blue());
        prefs->setString("/template/base/bordercolor", sp_border.toString());
        prefs->setDouble("/template/base/borderopacity", gdk_border.get_alpha());

        prefs->setBool("/template/base/pagecheckerboard", row[cols.checkered]);
        prefs->setInt("/template/base/pageshadow", row[cols.shadow] ? 2 : 0);

        Gdk::RGBA gdk_desk = Gdk::RGBA(row[cols.deskcolor]);
        SPColor sp_desk(gdk_desk.get_red(), gdk_desk.get_green(), gdk_desk.get_blue());
        prefs->setString("/template/base/deskcolor", sp_desk.toString());
    } catch(int e) {
        g_warning("Couldn't find canvas value.");
    }
}

void
StartScreen::filter_themes()
{
    ThemeCols cols;
    // We need to disable themes which aren't available.
    auto store = Glib::wrap(GTK_LIST_STORE(gtk_combo_box_get_model(themes.gobj())));
    auto available = INKSCAPE.themecontext->get_available_themes();

    // Detect use of custom theme here, detect defaults used in many systems.
    auto settings = Gtk::Settings::get_default();
    Glib::ustring theme_name = settings->property_gtk_theme_name();
    Glib::ustring icons_name = settings->property_gtk_icon_theme_name();

    bool has_system_theme = false;
    if (theme_name != "Adwaita" || icons_name != "hicolor") {
        has_system_theme = true;
        /* Enable if/when we want custom to be the default.
        if (prefs->getString("/options/boot/theme").empty()) {
            prefs->setString("/options/boot/theme", "system")
            theme_changed();
        }*/
    }

    for(auto row : store->children()) {
        Glib::ustring theme = row[cols.theme];
        if (!row[cols.enabled]) {
            // Available themes; We only "enable" them, we don't disable them.
            row[cols.enabled] = available.find(theme) != available.end();
        } else if(row[cols.id] == "system" && !has_system_theme) {
            // Disable system theme option if not available.
            row[cols.enabled] = false;
        }
    }
}

void
StartScreen::enlist_keys()
{
    NameIdCols cols;
    auto &keys = get_widget<Gtk::ComboBox>(builder, "keys");

    auto store = Glib::wrap(GTK_LIST_STORE(gtk_combo_box_get_model(keys.gobj())));
    store->clear();

    for (auto const &item : Inkscape::Shortcuts::get_file_names()) {
        Gtk::TreeModel::Row row = *(store->append());
        row[cols.col_name] = item.first;
        row[cols.col_id] = item.second;
    }

    auto prefs = Inkscape::Preferences::get();
    auto current = prefs->getString("/options/kbshortcuts/shortcutfile");
    if (current.empty()) {
        current = "inkscape.xml";
    }
    keys.set_active_id(current);
}

/**
 * Set the keys file based on the keys set in the enlist above
 */
void
StartScreen::keyboard_changed()
{
    NameIdCols cols;
    auto row = active_combo("keys");
    auto prefs = Inkscape::Preferences::get();
    Glib::ustring set_to = row[cols.col_id];
    prefs->setString("/options/kbshortcuts/shortcutfile", set_to);
    Inkscape::Shortcuts::getInstance().init();

    auto &keys_warning = get_widget<Gtk::InfoBar>(builder, "keys_warning");
    if (set_to != "inkscape.xml" && set_to != "default.xml") {
        keys_warning.set_message_type(Gtk::MessageType::WARNING);
        keys_warning.set_visible(true);
    } else {
        keys_warning.set_visible(false);
    }
}

/**
 * Set Dark Switch based on current selected theme.
 * We will disable switch if current theme doesn't have prefer dark theme option.
 */

void StartScreen::refresh_dark_switch()
{
    auto prefs = Inkscape::Preferences::get();

    auto const window = dynamic_cast<Gtk::Window *>(get_root());
    bool dark = INKSCAPE.themecontext->isCurrentThemeDark(window);
    prefs->setBool("/theme/preferDarkTheme", dark);
    prefs->setBool("/theme/darkTheme", dark);

    auto themes = INKSCAPE.themecontext->get_available_themes();
    Glib::ustring current_theme = prefs->getString("/theme/gtkTheme", prefs->getString("/theme/defaultGtkTheme", ""));

    auto &dark_toggle = get_widget<Gtk::Switch>(builder, "dark_toggle");
    dark_toggle.set_sensitive(themes[current_theme]);
    dark_toggle.set_active(dark);
}

void StartScreen::remove_banners_and_tabs(Gtk::Window &window)
{
    g_assert(banners.get_parent() == &window);
    g_assert(tabs   .get_parent() == &window);
    window.unset_titlebar();
    window.unset_child   ();
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
