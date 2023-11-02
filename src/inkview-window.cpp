// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Inkview - An SVG file viewer.
 */
/*
 * Authors:
 *   Tavmjong Bah
 *
 * Copyright (C) 2018 Authors
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 * Read the file 'COPYING' for more information.
 */

#include "inkview-window.h"

#include <iostream>
#include <glibmm/main.h>
#include <gtkmm/builder.h>
#include <gtkmm/window.h>
#include <sigc++/functors/mem_fun.h>

#include "document.h"
#include "ui/builder-utils.h"
#include "ui/controller.h"
#include "ui/monitor.h"
#include "ui/view/svg-view-widget.h"
#include "util/units.h"

InkviewWindow::InkviewWindow(Gio::Application::type_vec_files files,
                             bool fullscreen,
                             bool recursive,
                             int timer,
                             double scale,
                             bool preload)
    : _files(std::move(files))
    , _fullscreen(fullscreen)
    , _recursive(recursive)
    , _timer(timer)
    , _scale(scale)
    , _preload(preload)
{
    _files = create_file_list(_files);

    if (_preload) {
        preload_documents();
    }

    if (_files.empty()) {
        throw NoValidFilesException();
    }

    _documents.resize(_files.size(), nullptr); // We keep _documents and _files in sync.

    // Callbacks
    Inkscape::UI::Controller::add_key<&InkviewWindow::key_press>(*this, *this);

    if (_timer) {
        Glib::signal_timeout().connect_seconds(sigc::mem_fun(*this, &InkviewWindow::on_timer), _timer);
    }

    // Actions
    _group = Gio::SimpleActionGroup::create();
    _group->add_action("show_first", sigc::mem_fun(*this, &InkviewWindow::show_first));
    _group->add_action("show_prev",  sigc::mem_fun(*this, &InkviewWindow::show_prev) );
    _group->add_action("show_next",  sigc::mem_fun(*this, &InkviewWindow::show_next) );
    _group->add_action("show_last",  sigc::mem_fun(*this, &InkviewWindow::show_last) );
    insert_action_group("win", _group);

    // ToDo: Add Pause, Resume.

    if (_fullscreen) {
        Gtk::Window::fullscreen();
    }

    // Show first file
    activate_action("win.show_first");
}

std::vector<Glib::RefPtr<Gio::File>> InkviewWindow::create_file_list(std::vector<Glib::RefPtr<Gio::File>> const &files)
{
    std::vector<Glib::RefPtr<Gio::File>> valid_files;

    static bool first = true;

    for (auto const &file : files) {
        Gio::FileType type = file->query_file_type();
        switch (type) {
            case Gio::FileType::UNKNOWN:
                std::cerr << "InkviewWindow: File or directory does not exist: "
                          << file->get_basename() << std::endl;
                break;

            case Gio::FileType::REGULAR: {
                // Only look at SVG and SVGZ files.
                std::string basename = file->get_basename();
                std::string extension = basename.substr(basename.find_last_of(".") + 1);
                if (extension == "svg" || extension == "svgz") {
                    valid_files.push_back(file);
                }
                break;
            }

            case Gio::FileType::DIRECTORY:
                if (_recursive || first) {
                    // No easy way to get children of directory!
                    Glib::RefPtr<Gio::FileEnumerator> children = file->enumerate_children();
                    std::vector<Glib::RefPtr<Gio::File>> input;
                    while (auto info = children->next_file()) {
                        input.push_back(children->get_child(info));
                    }
                    auto new_files = create_file_list(input);
                    valid_files.insert(valid_files.end(), new_files.begin(), new_files.end());
                }
                break;

            default:
                std::cerr << "InkviewWindow: Unknown file type: " << (int)type << std::endl;
                break;
        }
        first = false;
    }

    return valid_files;
}

void InkviewWindow::update_title()
{
    auto title = Glib::ustring{_documents[_index]->getDocumentName()};

    if (_documents.size() > 1) {
        title += Glib::ustring::compose("  (%1/%2)", _index + 1, _documents.size());
    }

    set_title(title);
}

// Returns true if successfully shows document.
bool InkviewWindow::show_document(SPDocument *document)
{
    document->ensureUpToDate();  // Crashes on some documents if this isn't called!

    // Resize window: (Might be better to use get_monitor_geometry_at_window(this))
    Gdk::Rectangle monitor_geometry = Inkscape::UI::get_monitor_geometry_primary();
    int width  = std::min<int>(document->getWidth().value("px")  * _scale, monitor_geometry.get_width());
    int height = std::min<int>(document->getHeight().value("px") * _scale, monitor_geometry.get_height());
    set_default_size(width, height);

    if (_view) {
        _view->setDocument(document);
    } else {
        _view = Gtk::make_managed<Inkscape::UI::View::SVGViewWidget>(document);
        set_child(*_view);
    }

    update_title();

    return true;
}

// Load document, if fail, remove entry from lists.
SPDocument *InkviewWindow::load_document()
{
    auto document = _documents[_index];

    if (!document) {
        // We need to load document. ToDo: Pass Gio::File. Is get_base_name() better?
        document = SPDocument::createNewDoc(_files[_index]->get_parse_name().c_str(), true, false);
        if (document) {
            // We've successfully loaded it!
            _documents[_index] = document;
        }
    }

    if (!document) {
        // Failed to load document, remove from vectors.
        _documents.erase(std::next(_documents.begin(), _index));
        _files    .erase(std::next(_files    .begin(), _index));
    }

    return document;
}

void InkviewWindow::preload_documents()
{
    for (auto it =_files.begin(); it != _files.end(); ) {
        auto document = SPDocument::createNewDoc((*it)->get_parse_name().c_str(), true, false);
        if (document) {
            _documents.push_back(document);
            ++it;
        } else {
            it = _files.erase(it);
        }
    }
}

void InkviewWindow::show_control()
{
    if (_controlwindow) {
        _controlwindow->present();
        return;
    }

    auto builder = Inkscape::UI::create_builder("inkview-controls.ui");

    _controlwindow = &Inkscape::UI::get_widget<Gtk::Window>(builder, "ControlWindow");

    // Need to give control window access to viewer window's actions.
    _controlwindow->insert_action_group("viewer", _group);

    _controlwindow->set_transient_for(*this);
    _controlwindow->set_visible(true);
}

// Next document
void InkviewWindow::show_next()
{
    ++_index;

    SPDocument *document = nullptr;

    while (_index < _documents.size() && !document) {
        document = load_document();
    }

    if (document) {
        // Show new document
        show_document(document);
    } else {
        // Failed to load new document, keep current.
        --_index;
    }
}

// Previous document
void InkviewWindow::show_prev()
{
    SPDocument *document = nullptr;
    int old_index = _index;

    while (_index > 0 && !document) {
        --_index;
        document = load_document();
    }

    if (document) {
        // Show new document
        show_document(document);
    } else {
        // Failed to load new document, keep current.
        _index = old_index;
    }
}

// Show first document
void InkviewWindow::show_first()
{
    _index = -1;
    show_next();
}

// Show last document
void InkviewWindow::show_last()
{
    _index = _documents.size();
    show_prev();
}

bool InkviewWindow::key_press(GtkEventControllerKey *, unsigned keyval, unsigned, GdkModifierType)
{
    switch (keyval) {
        case GDK_KEY_Up:
        case GDK_KEY_Home:
            show_first();
            break;

        case GDK_KEY_Down:
        case GDK_KEY_End:
            show_last();
            break;

        case GDK_KEY_F11:
            if (_fullscreen) {
                unfullscreen();
                _fullscreen = false;
            } else {
                fullscreen();
                _fullscreen = true;
            }
            break;

        case GDK_KEY_Return:
            show_control();
            break;

        case GDK_KEY_KP_Page_Down:
        case GDK_KEY_Page_Down:
        case GDK_KEY_Right:
        case GDK_KEY_space:
            show_next();
            break;

        case GDK_KEY_KP_Page_Up:
        case GDK_KEY_Page_Up:
        case GDK_KEY_Left:
        case GDK_KEY_BackSpace:
            show_prev();
            break;

        case GDK_KEY_Escape:
        case GDK_KEY_q:
        case GDK_KEY_Q:
            close();
            break;

        default:
            break;
    }
    return false;
}

bool InkviewWindow::on_timer()
{
    show_next();

    // Stop if at end.
    if (_index >= _documents.size() - 1) {
        return false;
    }

    return true;
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
