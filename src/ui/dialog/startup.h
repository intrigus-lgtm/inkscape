// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A dialog for the start screen
 */
/*
 * Copyright (C) Martin Owens 2020 <doctormo@gmail.com>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef STARTSCREEN_H
#define STARTSCREEN_H

#include <string>             // for string
#include <gdk/gdk.h>          // for GdkModifierType
#include <glibmm/refptr.h>    // for RefPtr
#include <glibmm/ustring.h>   // for ustring
#include <gtk/gtk.h>          // for GtkEventControllerKey
#include <gtkmm/dialog.h>     // for Dialog
#include <gtkmm/treemodel.h>  // for TreeModel

namespace Gtk {
class Builder;
class Button;
class ComboBox;
class Notebook;
class Overlay;
class TreeView;
class Widget;
} // namespace Gtk

class SPDocument;

namespace Inkscape::UI {

namespace Widget {
class TemplateList;
} // namespace Widget

namespace Dialog {

class StartScreen : public Gtk::Dialog {
public:
    StartScreen();
    ~StartScreen() override;

    SPDocument* get_document() { return _document; }

protected:
    void on_response(int response_id) override;

private:
    void notebook_next(Gtk::Widget *button);
    bool on_key_pressed(GtkEventControllerKey const *controller,
                        unsigned keyval, unsigned keycode, GdkModifierType state);
    Gtk::TreeModel::Row active_combo(std::string widget_name);
    void set_active_combo(std::string widget_name, std::string unique_id);
    void show_toggle();
    void enlist_recent_files();
    void enlist_keys();
    void filter_themes();
    void keyboard_changed();
    void notebook_switch(Gtk::Widget *tab, unsigned page_num);

    void theme_changed();
    void canvas_changed();
    void refresh_theme(Glib::ustring theme_name);
    void refresh_dark_switch();

    void new_document();
    void load_document();
    void on_recent_changed();
    void on_kind_changed(Gtk::Widget *tab, unsigned page_num);

    Glib::RefPtr<Gtk::Builder> builder;
    Gtk::Notebook &tabs;
    Gtk::Overlay  &banners;
    Gtk::ComboBox &themes;
    Gtk::TreeView &recent_treeview;
    Gtk::Button   &load_btn;
    Inkscape::UI::Widget::TemplateList &templates;

    SPDocument* _document = nullptr;
};

} // namespace Dialog

} // namespace Inkscape::UI

#endif // STARTSCREEN_H

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
