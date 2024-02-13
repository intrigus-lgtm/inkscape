// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef INKSCAPE_UI_WIDGET_COMPLETION_POPUP_H
#define INKSCAPE_UI_WIDGET_COMPLETION_POPUP_H

#include <sigc++/signal.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/box.h>
#include <gtkmm/builder.h>

#include "ui/widget/popover-menu.h"

namespace Gtk {
class EntryCompletion;
class ListStore;
class MenuButton;
class SearchEntry2;
} // namespace Gtk

namespace Inkscape::UI::Widget {

class CompletionPopup final : public Gtk::Box {
public:
    CompletionPopup();
    ~CompletionPopup() final;

    PopoverMenu& get_menu();
    Gtk::SearchEntry2& get_entry();
    Glib::RefPtr<Gtk::ListStore> get_list();

    void clear_completion_list();
    void add_to_completion_list(int id, Glib::ustring name, Glib::ustring icon_name, Glib::ustring search_text = Glib::ustring());

    sigc::signal<void (int)>& on_match_selected();
    sigc::signal<void ()>& on_button_press();
    sigc::signal<bool ()>& on_focus();

private:
    Glib::RefPtr<Gtk::Builder> _builder;
    Glib::RefPtr<Gtk::ListStore> _list;
    Gtk::SearchEntry2& _search;
    Gtk::MenuButton& _button;
    PopoverMenu _popover_menu;
    Glib::RefPtr<Gtk::EntryCompletion> _completion;
    sigc::signal<void (int)> _match_selected;
    sigc::signal<void ()> _button_press;
    sigc::signal<bool ()> _on_focus;

    void clear();
};

} // namespace Inkscape::UI::Widget

#endif // INKSCAPE_UI_WIDGET_COMPLETION_POPUP_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:
