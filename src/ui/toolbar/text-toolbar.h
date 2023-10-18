// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_TEXT_TOOLBAR_H
#define SEEN_TEXT_TOOLBAR_H

/**
 * @file
 * Text aux toolbar
 */
/* Authors:
 *   MenTaLguY <mental@rydia.net>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Frank Felfe <innerspace@iname.com>
 *   John Cliff <simarilius@yahoo.com>
 *   David Turner <novalis@gnu.org>
 *   Josh Andler <scislac@scislac.com>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *   Tavmjong Bah <tavmjong@free.fr>
 *   Abhishek Sharma
 *   Kris De Gussem <Kris.DeGussem@gmail.com>
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Copyright (C) 2004 David Turner
 * Copyright (C) 2003 MenTaLguY
 * Copyright (C) 1999-2011 authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <sigc++/connection.h>

#include "object/sp-item.h"
#include "object/sp-object.h"
#include "style.h"
#include "text-editing.h"
#include "toolbar.h"

namespace Gtk {
class Builder;
class ListBox;
class ToggleButton;
} // namespace Gtk

class SPDesktop;

namespace Inkscape {
class Selection;

namespace UI {
namespace Tools {
class ToolBase;
class TextTool;
}

namespace Widget {
class ComboBoxEntryToolItem;
class ComboToolItem;
class SpinButton;
class UnitTracker;
}

namespace Toolbar {

class TextToolbar final : public Toolbar
{
public:
    TextToolbar(SPDesktop *desktop);
    ~TextToolbar() override;

private:
    using ValueChangedMemFun = void (TextToolbar::*)();
    using ModeChangedMemFun = void (TextToolbar::*)(int);

    Glib::RefPtr<Gtk::Builder> _builder;

    UI::Widget::UnitTracker *_tracker;
    UI::Widget::UnitTracker *_tracker_fs;

    std::vector<Gtk::ToggleButton *> _alignment_buttons;
    std::vector<Gtk::ToggleButton *> _writing_buttons;
    std::vector<Gtk::ToggleButton *> _orientation_buttons;
    std::vector<Gtk::ToggleButton *> _direction_buttons;

    Gtk::ListBox &_font_collections_list;

    UI::Widget::ComboBoxEntryToolItem *_font_family_item;
    UI::Widget::ComboBoxEntryToolItem *_font_size_item;
    UI::Widget::ComboToolItem *_font_size_units_item;
    UI::Widget::ComboBoxEntryToolItem *_font_style_item;
    UI::Widget::ComboToolItem *_line_height_units_item;
    UI::Widget::SpinButton &_line_height_item;
    Gtk::ToggleButton &_superscript_btn;
    Gtk::ToggleButton &_subscript_btn;

    UI::Widget::SpinButton &_word_spacing_item;
    UI::Widget::SpinButton &_letter_spacing_item;
    UI::Widget::SpinButton &_dx_item;
    UI::Widget::SpinButton &_dy_item;
    UI::Widget::SpinButton &_rotation_item;

    bool _freeze;
    bool _text_style_from_prefs;
    bool _outer;
    SPItem *_sub_active_item;
    int _lineheight_unit;
    Inkscape::Text::Layout::iterator wrap_start;
    Inkscape::Text::Layout::iterator wrap_end;
    bool _updating;
    int _cusor_numbers;
    SPStyle _query_cursor;
    double selection_fontsize;

    auto_connection fc_changed_selection;
    auto_connection fc_update;
    sigc::connection c_selection_changed;
    sigc::connection c_selection_modified;
    sigc::connection c_selection_modified_select_tool;
    sigc::connection c_subselection_changed;

    void setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name, double default_value,
                                   ValueChangedMemFun const value_changed_mem_fun);
    void configure_mode_buttons(std::vector<Gtk::ToggleButton *> &buttons, Gtk::Box &box, Glib::ustring const &name,
                                ModeChangedMemFun const mode_changed_mem_fun);
    void text_outer_set_style(SPCSSAttr *css);
    void fontfamily_value_changed();
    void fontsize_value_changed();
    void subselection_wrap_toggle(bool start);
    void fontstyle_value_changed();
    void script_changed(int mode);
    void align_mode_changed(int mode);
    void writing_mode_changed(int mode);
    void orientation_changed(int mode);
    void direction_changed(int mode);
    void lineheight_value_changed();
    void lineheight_unit_changed(int not_used);
    void wordspacing_value_changed();
    void letterspacing_value_changed();
    void dx_value_changed();
    void dy_value_changed();
    void prepare_inner();
    void focus_text();
    void rotation_value_changed();
    void fontsize_unit_changed(int not_used);
    void selection_changed(Inkscape::Selection *selection);
    void selection_modified(Inkscape::Selection *selection, guint flags);
    void selection_modified_select_tool(Inkscape::Selection *selection, guint flags);
    void subselection_changed(Inkscape::UI::Tools::TextTool* texttool);
    void watch_ec(SPDesktop* desktop, Inkscape::UI::Tools::ToolBase* tool);
    void set_sizes(int unit);
    void display_font_collections();
    void on_fcm_button_pressed();
    void on_reset_button_pressed();
    Inkscape::XML::Node *unindent_node(Inkscape::XML::Node *repr, Inkscape::XML::Node *before);
    bool mergeDefaultStyle(SPCSSAttr *css);

    Inkscape::auto_connection _fonts_updated_signal;
};
}
}
}

#endif /* !SEEN_TEXT_TOOLBAR_H */
