// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A notebook with RGB, CMYK, CMS, HSL, and Wheel pages
 *//*
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Tomasz Boczkowski <penginsbacon@gmail.com> (c++-sification)
 *
 * Copyright (C) 2001-2014 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#ifndef SEEN_SP_COLOR_NOTEBOOK_H
#define SEEN_SP_COLOR_NOTEBOOK_H

#include <memory>               // for unique_ptr
#include <vector>               // for vector

#include <glibmm/ustring.h>     // for ustring
#include <gtkmm/grid.h>         // for Grid
#include <gtkmm/widget.h>       // for GtkWidget, Widget (ptr only)
#include <sigc++/connection.h>  // for connection

#include "preferences.h"        // for PrefObserver
#include "ui/selected-color.h"  // for ColorSelectorFactory, SelectedColor (...

class ColorRGBA;
class SPDocument;

namespace Gtk {
class Box;
class Label;
class Stack;
class StackSwitcher;
} // namespace Gtk

namespace Inkscape::UI::Widget {

class IconComboBox;

class ColorNotebook
    : public Gtk::Grid
{
public:
    ColorNotebook(SelectedColor &color, bool no_alpha = false);
    ~ColorNotebook() override;

    void set_label(const Glib::ustring& label);

protected:
    struct Page {
        Page(std::unique_ptr<Inkscape::UI::ColorSelectorFactory> selector_factory, const char* icon);

        std::unique_ptr<Inkscape::UI::ColorSelectorFactory> selector_factory;
        Glib::ustring icon_name;
    };

    void _initUI(bool no_alpha);
    void _addPage(Page &page, bool no_alpha, const Glib::ustring vpath);
    void setDocument(SPDocument *document);

    void _pickColor(ColorRGBA *color);
    static void _onPickerClicked(GtkWidget *widget, ColorNotebook *colorbook);
    virtual void _onSelectedColorChanged();
    int getPageIndex(const Glib::ustring &name);
    int getPageIndex(Gtk::Widget *widget);

    void _updateICCButtons();
    void _setCurrentPage(int i, bool sync_combo);

    Inkscape::UI::SelectedColor &_selected_color;
    unsigned long _entryId = 0;
    Gtk::Stack* _book = nullptr;
    Gtk::StackSwitcher* _switcher = nullptr;
    Gtk::Box* _buttonbox = nullptr;
    Gtk::Label* _label = nullptr;
    GtkWidget *_rgbal = nullptr; /* RGBA entry */
    GtkWidget *_outofgamut = nullptr;
    GtkWidget *_colormanaged = nullptr;
    GtkWidget *_toomuchink = nullptr;
    GtkWidget *_btn_picker = nullptr;
    GtkWidget *_p = nullptr; /* Color preview */
    sigc::connection _onetimepick;
    IconComboBox* _combo = nullptr;

private:
    PrefObserver _observer;
    std::vector<PrefObserver> _visibility_observers;

    SPDocument *_document = nullptr;
    sigc::connection _doc_replaced_connection;
    sigc::connection _icc_changed_connection;
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_SP_COLOR_NOTEBOOK_H

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

