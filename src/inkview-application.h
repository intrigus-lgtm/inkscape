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
 *
 */

#ifndef INKVIEW_APPLICATION_H
#define INKVIEW_APPLICATION_H

#include <glibmm/refptr.h>
#include <gtkmm/application.h>

namespace Glib {
class ustring;
class VariantDict;
} // namespace Glib

class InkviewWindow;

class InkviewApplication : public Gtk::Application
{
public:
    /// Exclusively for the creation of the singleton instance inside main().
    InkviewApplication();
    ~InkviewApplication() override;

protected:
    void on_startup() override;
    void on_activate() override;
    void on_open(Gio::Application::type_vec_files const &files, Glib::ustring const &hint) override;

private:
    // Callbacks
    int on_handle_local_options(Glib::RefPtr<Glib::VariantDict> const &options) override;

    // Command line options
    bool   fullscreen;
    bool   recursive;
    int    timer;
    double scale;
    bool   preload;

    InkviewWindow *window;
};

#endif // INKVIEW_APPLICATION_H

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
