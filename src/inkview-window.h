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

#ifndef INKVIEW_WINDOW_H
#define INKVIEW_WINDOW_H

#include <vector>
#include <glibmm/refptr.h>
#include <giomm/application.h>
#include <gtkmm/applicationwindow.h>

namespace Gio {
class File;
class SimpleActionGroup;
} // namespace Gio

class SPDocument;

namespace Inkscape::UI::View {
class SVGViewWidget;
} // namespace Inkscape::UI::View

class InkviewWindow : public Gtk::ApplicationWindow
{
public:
    InkviewWindow(Gio::Application::type_vec_files files,
                  bool fullscreen, bool recursive, int timer, double scale, bool preload);

    struct NoValidFilesException {};

private:
    std::vector<Glib::RefPtr<Gio::File>> create_file_list(std::vector<Glib::RefPtr<Gio::File>> const &files);
    void update_title();
    bool show_document(SPDocument *document);
    SPDocument *load_document();
    void preload_documents();

    Gio::Application::type_vec_files  _files;
    bool   _fullscreen;
    bool   _recursive;
    int    _timer;
    double _scale;
    bool   _preload;

    int _index = -1;
    std::vector<SPDocument*> _documents;

    Inkscape::UI::View::SVGViewWidget *_view = nullptr;
    Gtk::Window *_controlwindow = nullptr;
    Glib::RefPtr<Gio::SimpleActionGroup> _group;

    // Callbacks
    void show_control();
    void show_next();
    void show_prev();
    void show_first();
    void show_last();

    bool key_press(GtkEventControllerKey *controller, unsigned keyval, unsigned keycode, GdkModifierType state);
    bool on_timer();
};

#endif // INKVIEW_WINDOW_H

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
