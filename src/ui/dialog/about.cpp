// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A dialog for the about screen
 *
 * Copyright (C) Martin Owens 2019 <doctormo@gmail.com>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "about.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <random>
#include <regex>
#include <string>
#include <utility>
#include <vector>
#include <glibmm/main.h>
#include <gtkmm/aspectframe.h>
#include <gtkmm/builder.h>
#include <gtkmm/button.h>
#include <gdkmm/clipboard.h>
#include <gtkmm/label.h>
#include <gtkmm/notebook.h>
#include <gtkmm/textview.h>
#include <gtkmm/window.h>
#include <sigc++/adaptors/bind.h>

#include "document.h"
#include "inkscape-version-info.h"
#include "io/resource.h"
#include "ui/builder-utils.h"
#include "ui/util.h"
#include "ui/view/svg-view-widget.h"
#include "util/units.h"

using namespace Inkscape::IO;

namespace Inkscape::UI::Dialog {

static Gtk::Window *window = nullptr;
static Gtk::Notebook *tabs = nullptr;

static bool show_copy_button(Gtk::Button * const button, Gtk::Label * const label)
{
    reveal_widget(button, true);
    reveal_widget(label, false);
    return false;
}

static void copy(Gtk::Button * const button, Gtk::Label * const label, Glib::ustring const &text)
{
    auto clipboard = Gdk::Display::get_default()->get_clipboard();
    clipboard->set_text(text);
    if (label) {
        reveal_widget(button, false);
        reveal_widget(label, true);
        Glib::signal_timeout().connect_seconds(
            sigc::bind(&show_copy_button, button, label), 2);
    }
}

// Free function to handle key events
static void on_key_pressed(GtkEventControllerKey const * const controller,
                           unsigned const keyval, 
                           unsigned const keycode,
                           GdkModifierType const state,
                           void *user_data) {
    if (keyval == GDK_KEY_Escape) {
        auto const window = static_cast<Gtk::Window *>(user_data);
        window->hide();
    }
}

template <class Random>
[[nodiscard]] static auto get_shuffled_lines(std::string const &filename, Random &&random)
{
    std::ifstream fn{Resource::get_filename(Resource::DOCS, filename.c_str())};
    std::vector<std::string> lines;
    std::size_t capacity = 0;
    for (std::string line; getline(fn, line);) {
        capacity += line.size() + 1;
        lines.push_back(std::move(line));
    }
    std::shuffle(lines.begin(), lines.end(), random);
    return std::pair{std::move(lines), capacity};
}

void show_about()
{
    if (!window) {

        // Load builder file here
        auto builder = create_builder("inkscape-about.glade");
        window            = &get_widget<Gtk::Window>  (builder, "about-screen-window");
        tabs              = &get_widget<Gtk::Notebook>(builder, "tabs");
        auto version      = &get_widget<Gtk::Button>  (builder, "version");
        auto label        = &get_widget<Gtk::Label>   (builder, "version-copied");
        auto debug_info   = &get_widget<Gtk::Button>  (builder, "debug_info");
        auto label2       = &get_widget<Gtk::Label>   (builder, "debug-info-copied");
        auto copyright    = &get_widget<Gtk::Label>   (builder, "copyright");
        auto authors      = &get_widget<Gtk::TextView>(builder, "credits-authors");
        auto translators  = &get_widget<Gtk::TextView>(builder, "credits-translators");
        auto license      = &get_widget<Gtk::Label>   (builder, "license-text");

        // Automatic signal handling (requires -rdynamic compile flag)
        //gtk_builder_connect_signals(builder->gobj(), NULL);

        auto text = Inkscape::inkscape_version();
        version->set_label(text);
        version->signal_clicked().connect(
            sigc::bind(&copy, version, label, std::move(text)));

        debug_info->signal_clicked().connect(
            sigc::bind(&copy, version, label2, Inkscape::debug_info()));

        copyright->set_label(
            Glib::ustring::compose(copyright->get_label(), Inkscape::inkscape_build_year()));

        // Render the about screen image via inkscape SPDocument
        auto filename = Resource::get_filename(Resource::SCREENS, "about.svg", true, false);
        SPDocument *document = SPDocument::createNewDoc(filename.c_str(), true);

        // Bind builder's container to our SVGViewWidget class
        if (document) {
            auto const viewer = Gtk::make_managed<Inkscape::UI::View::SVGViewWidget>(document);
            double width  = document->getWidth().value("px");
            double height = document->getHeight().value("px");
            viewer->setResize(width, height);
            viewer->set_visible(true);

            auto splash_widget = &get_widget<Gtk::AspectFrame>(builder, "aspect-frame");
            splash_widget->property_ratio() = width / height;
            splash_widget->set_child(*viewer);
        } else {
            g_error("Error loading about screen SVG: no document!");
        }

        std::random_device rd;
        std::mt19937 g(rd());
        auto const [authors_data, capacity] = get_shuffled_lines("AUTHORS", g);
        std::string str_authors;
        str_authors.reserve(capacity);
        for (auto const &author : authors_data) {
            str_authors.append(author).append(1, '\n');
        }
        authors->get_buffer()->set_text(str_authors.c_str());

        auto const [translators_data, capacity2] = get_shuffled_lines("TRANSLATORS", g);
        std::string str_translators;
        str_translators.reserve(capacity2);
        std::regex e("(.*?)(<.*|)");
        for (auto const &translator : translators_data) {
            str_translators.append(std::regex_replace(translator, e, "$1")).append(1, '\n');
        }
        translators->get_buffer()->set_text(str_translators.c_str());

        std::ifstream fn(Resource::get_filename(Resource::DOCS, "LICENSE"));
        std::string str((std::istreambuf_iterator<char>(fn)),
                         std::istreambuf_iterator<char>());
        license->set_markup(str.c_str());

        // Connect the key event to the on_key_pressed function
        auto const controller = gtk_event_controller_key_new(GTK_WIDGET(window->gobj()));
        g_signal_connect(controller, "key-pressed", G_CALLBACK(on_key_pressed), window);
     }

    if (window) {
        window->set_visible(true);
        tabs->set_current_page(0);
    } else {
        g_error("About screen window couldn't be loaded. Missing window id in glade file.");
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
