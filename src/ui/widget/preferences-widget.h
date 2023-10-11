// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Widgets for Inkscape Preferences dialog.
 */
/*
 * Authors:
 *   Marco Scholten
 *   Bruno Dilly <bruno.dilly@gmail.com>
 *
 * Copyright (C) 2004, 2006, 2007  Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_WIDGET_INKSCAPE_PREFERENCES_H
#define INKSCAPE_UI_WIDGET_INKSCAPE_PREFERENCES_H

#include <cstdint>
#include <span>
#include <vector>
#include <sigc++/signal.h>
#include <glibmm/refptr.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/grid.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/textview.h>

#include "ui/widget/color-picker.h"
#include "ui/widget/unit-menu.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/scalar-unit.h"

namespace Gtk {
class Scale;
} // namespace Gtk

namespace Inkscape::UI::Widget {

class PrefCheckButton : public Gtk::CheckButton
{
public:
    void init(Glib::ustring const &label, Glib::ustring const &prefs_path,
              bool default_value);
    // Allow use with the GtkBuilder get_derived_widget
    PrefCheckButton(BaseObjectType *cobject, const Glib::RefPtr<Gtk::Builder> &refGlade, Glib::ustring pref, bool def)
        : Gtk::CheckButton(cobject)
    {
        init("", pref, def);
    }
    PrefCheckButton() : Gtk::CheckButton() {};
    sigc::signal<void (bool)> changed_signal;

private:
    Glib::ustring _prefs_path;
    void on_toggled() override;
};

class PrefRadioButton : public Gtk::CheckButton
{
public:
    void init(Glib::ustring const &label, Glib::ustring const &prefs_path,
              int int_value, bool default_value, PrefRadioButton* group_member);
    void init(Glib::ustring const &label, Glib::ustring const &prefs_path,
              Glib::ustring const &string_value, bool default_value, PrefRadioButton* group_member);
    sigc::signal<void (bool)> changed_signal;

private:
    Glib::ustring _prefs_path;
    Glib::ustring _string_value;
    int _value_type;
    enum
    {
        VAL_INT,
        VAL_STRING
    };
    int _int_value;
    void on_toggled() override;
};

struct PrefItem { Glib::ustring label; int int_value; Glib::ustring tooltip; bool is_default = false; };

class PrefRadioButtons : public Gtk::Box {
public:
    PrefRadioButtons(const std::vector<PrefItem>& buttons, const Glib::ustring& prefs_path);
};

class PrefSpinButton : public SpinButton
{
public:
    void init(Glib::ustring const &prefs_path,
              double lower, double upper, double step_increment, double page_increment,
              double default_value, bool is_int, bool is_percent);
    sigc::signal<void (double)> changed_signal;

private:
    Glib::ustring _prefs_path;
    bool _is_int;
    bool _is_percent;

    void on_value_changed();
};

class PrefSpinUnit : public ScalarUnit
{
public:
    PrefSpinUnit() : ScalarUnit("", "") {};

    void init(Glib::ustring const &prefs_path,
              double lower, double upper, double step_increment,
              double default_value,
              UnitType unit_type, Glib::ustring const &default_unit);

private:
    Glib::ustring _prefs_path;
    bool _is_percent;
    void on_my_value_changed();
};

class ZoomCorrRuler : public Gtk::DrawingArea {
public:
    ZoomCorrRuler(int width = 100, int height = 20);
    void set_size(int x, int y);
    void set_unit_conversion(double conv) { _unitconv = conv; }

    int width() { return _min_width + _border*2; }

    static const double textsize;
    static const double textpadding;

private:
    void on_draw(Cairo::RefPtr<Cairo::Context> const &cr, int width, int height);
    void draw_marks(Cairo::RefPtr<Cairo::Context> const &cr, double dist, int major_interval);

    double _unitconv;
    int _min_width;
    int _height;
    int _border;
    int _drawing_width;
};

class ZoomCorrRulerSlider : public Gtk::Box
{
public:
    ZoomCorrRulerSlider() : Gtk::Box(Gtk::Orientation::VERTICAL) {}

    void init(int ruler_width, int ruler_height, double lower, double upper,
              double step_increment, double page_increment, double default_value);

private:
    void on_slider_value_changed();
    void on_spinbutton_value_changed();
    void on_unit_changed();
    bool on_mnemonic_activate( bool group_cycling ) override;

    Inkscape::UI::Widget::SpinButton *_sb;
    UnitMenu        _unit;
    Gtk::Scale*      _slider;
    ZoomCorrRuler   _ruler;
    bool freeze; // used to block recursive updates of slider and spinbutton
};

class PrefSlider : public Gtk::Box
{
public:
    PrefSlider(bool spin = true) : Gtk::Box(Gtk::Orientation::HORIZONTAL) { _spin = spin; }

    void init(Glib::ustring const &prefs_path,
    		  double lower, double upper, double step_increment, double page_increment, double default_value, int digits);

    Gtk::Scale*  getSlider() {return _slider;};
    Inkscape::UI::Widget::SpinButton * getSpinButton() {return _sb;};

private:
    void on_slider_value_changed();
    void on_spinbutton_value_changed();
    bool on_mnemonic_activate( bool group_cycling ) override;

    Glib::ustring _prefs_path;
    Inkscape::UI::Widget::SpinButton *_sb = nullptr;
    bool _spin;
    Gtk::Scale*     _slider = nullptr;

    bool freeze; // used to block recursive updates of slider and spinbutton
};

class PrefCombo : public Gtk::ComboBoxText
{
public:
    void init(Glib::ustring const &prefs_path,
              std::span<Glib::ustring const> labels,
              std::span<int const> values,
              int default_value);

    void init(Glib::ustring const &prefs_path,
              std::span<Glib::ustring const> labels,
              std::span<Glib::ustring const> values,
              Glib::ustring const &default_value);

private:
    Glib::ustring _prefs_path;
    std::vector<int> _values;
    std::vector<Glib::ustring> _ustr_values;    ///< string key values used optionally instead of numeric _values
    void on_changed() override;
};

class PrefEntry : public Gtk::Entry
{
public:
    void init(Glib::ustring const &prefs_path, bool mask);

protected:
    Glib::ustring _prefs_path;

private:
    void on_changed() override;
};

class PrefEntryFile : public PrefEntry
{
    void on_changed() override;
};

class PrefMultiEntry : public Gtk::ScrolledWindow
{
public:
    void init(Glib::ustring const &prefs_path, int height);

private:
    Glib::ustring       _prefs_path;
    Gtk::TextView       _text;
    void on_changed();
};

class PrefEntryButtonHBox : public Gtk::Box
{
public:
    PrefEntryButtonHBox() : Gtk::Box(Gtk::Orientation::HORIZONTAL) {}

    void init(Glib::ustring const &prefs_path,
            bool mask, Glib::ustring const &default_string);

private:
    Glib::ustring _prefs_path;
    Glib::ustring _default_string;
    Gtk::Button *relatedButton;
    Gtk::Entry *relatedEntry;
    void onRelatedEntryChangedCallback();
    void onRelatedButtonClickedCallback();
    bool on_mnemonic_activate( bool group_cycling ) override;
};

class PrefEntryFileButtonHBox : public Gtk::Box
{
public:
    PrefEntryFileButtonHBox() : Gtk::Box(Gtk::Orientation::HORIZONTAL) {}

    void init(Glib::ustring const &prefs_path,
              bool mask);

private:
    Glib::ustring _prefs_path;
    Gtk::Button *relatedButton;
    Gtk::Entry *relatedEntry;
    void onRelatedEntryChangedCallback();
    void onRelatedButtonClickedCallback();
    bool on_mnemonic_activate( bool group_cycling ) override;
};

class PrefOpenFolder : public Gtk::Box {
  public:
    PrefOpenFolder() : Gtk::Box(Gtk::Orientation::HORIZONTAL) {}

    void init(Glib::ustring const &entry_string, Glib::ustring const &tooltip);

private:
    Gtk::Button *relatedButton;
    Gtk::Entry *relatedEntry;
    void onRelatedButtonClickedCallback();
};

class PrefColorPicker : public ColorPicker
{
public:
    PrefColorPicker() : ColorPicker("", "", 0, false) {};
    ~PrefColorPicker() override = default;;

    void init(Glib::ustring const &abel, Glib::ustring const &prefs_path,
              std::uint32_t default_rgba);

private:
    Glib::ustring _prefs_path;
    void on_changed(std::uint32_t rgba) override;
};

class PrefUnit : public UnitMenu
{
public:
    void init(Glib::ustring const &prefs_path);

private:
    Glib::ustring _prefs_path;
    void on_changed() override;
};

class DialogPage : public Gtk::Grid
{
public:
    DialogPage();
    void add_line(bool indent, Glib::ustring const &label, Gtk::Widget& widget, Glib::ustring const &suffix, Glib::ustring const &tip, bool expand = true, Gtk::Widget *other_widget = nullptr);
    void add_group_header(Glib::ustring name, int columns = 1);
    void add_group_note(Glib::ustring name);
    void set_tip(Gtk::Widget &widget, Glib::ustring const &tip);
};

} // namespace Inkscape::UI::Widget

#endif //INKSCAPE_UI_WIDGET_INKSCAPE_PREFERENCES_H

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
