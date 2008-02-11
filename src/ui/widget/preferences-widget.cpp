/**
 * \brief Inkscape Preferences dialog
 *
 * Authors:
 *   Marco Scholten
 *   Bruno Dilly <bruno.dilly@gmail.com>
 *
 * Copyright (C) 2004, 2006, 2007 Authors
 *
 * Released under GNU GPL.  Read the file 'COPYING' for more information.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <gtkmm/frame.h>
#include <gtkmm/alignment.h>
#include <gtkmm/box.h>

#include "prefs-utils.h"
#include "ui/widget/preferences-widget.h"
#include "verbs.h"
#include "selcue.h"
#include <iostream>
#include "enums.h"
#include "inkscape.h"
#include "desktop-handles.h"
#include "message-stack.h"
#include "style.h"
#include "selection.h"
#include "selection-chemistry.h"
#include "xml/repr.h"

using namespace Inkscape::UI::Widget;

namespace Inkscape {
namespace UI {
namespace Widget {

DialogPage::DialogPage()
{
    this->set_border_width(12);
    this->set_col_spacings(12);
    this->set_row_spacings(6);
}

void DialogPage::add_line(bool indent, const Glib::ustring label, Gtk::Widget& widget, const Glib::ustring suffix, const Glib::ustring& tip, bool expand_widget)
{
    int start_col;
    int row = this->property_n_rows();
    Gtk::Widget* w;
    if (expand_widget)
    {
        w = &widget;
    }
    else
    {
        Gtk::HBox* hb = Gtk::manage(new Gtk::HBox());
        hb->set_spacing(12);
        hb->pack_start(widget,false,false);
        w = (Gtk::Widget*) hb;
    }
    if (label != "")
    {
        Gtk::Label* label_widget;
        label_widget = Gtk::manage(new Gtk::Label(label , Gtk::ALIGN_LEFT , Gtk::ALIGN_CENTER, true));
        label_widget->set_mnemonic_widget(widget);
        if (indent)
        {
            Gtk::Alignment* alignment = Gtk::manage(new Gtk::Alignment());
            alignment->set_padding(0, 0, 12, 0);
            alignment->add(*label_widget);
            this->attach(*alignment , 0, 1, row, row + 1, Gtk::FILL, Gtk::AttachOptions(), 0, 0);
        }
        else
            this->attach(*label_widget , 0, 1, row, row + 1, Gtk::FILL, Gtk::AttachOptions(), 0, 0);
        start_col = 1;
    }
    else
        start_col = 0;

    if (start_col == 0 && indent) //indent this widget
    {
        Gtk::Alignment* alignment = Gtk::manage(new Gtk::Alignment());
        alignment->set_padding(0, 0, 12, 0);
        alignment->add(*w);
        this->attach(*alignment, start_col, 2, row, row + 1, Gtk::FILL | Gtk::EXPAND, Gtk::AttachOptions(),  0, 0);
    }
    else
    {
        this->attach(*w, start_col, 2, row, row + 1, Gtk::FILL | Gtk::EXPAND, Gtk::AttachOptions(),  0, 0);
    }

    if (suffix != "")
    {
        Gtk::Label* suffix_widget = Gtk::manage(new Gtk::Label(suffix , Gtk::ALIGN_LEFT , Gtk::ALIGN_CENTER, true));
        if (expand_widget)
            this->attach(*suffix_widget, 2, 3, row, row + 1, Gtk::FILL,  Gtk::AttachOptions(), 0, 0);
        else
            ((Gtk::HBox*)w)->pack_start(*suffix_widget,false,false);
    }

    if (tip != "")
    {
        _tooltips.set_tip (widget, tip);
    }

}

void DialogPage::add_group_header(Glib::ustring name)
{
    int row = this->property_n_rows();
    if (name != "")
    {
        Gtk::Label* label_widget = Gtk::manage(new Gtk::Label(Glib::ustring(/*"<span size='large'>*/"<b>") + name +
                                               Glib::ustring("</b>"/*</span>"*/) , Gtk::ALIGN_LEFT , Gtk::ALIGN_CENTER, true));
        label_widget->set_use_markup(true);
        this->attach(*label_widget , 0, 4, row, row + 1, Gtk::FILL, Gtk::AttachOptions(), 0, 0);
        if (row != 1)
            this->set_row_spacing(row - 1, 18);
    }
}

void DialogPage::set_tip(Gtk::Widget& widget, const Glib::ustring& tip)
{
    _tooltips.set_tip (widget, tip);
}

void PrefCheckButton::init(const Glib::ustring& label, const std::string& prefs_path, const std::string& attr,
                           bool default_value)
{
    _prefs_path = prefs_path;
    _attr = attr;
    this->set_label(label);
    this->set_active( prefs_get_int_attribute (_prefs_path.c_str(), _attr.c_str(), (int)default_value) );
}

void PrefCheckButton::on_toggled()
{
    if (this->is_visible()) //only take action if the user toggled it
    {
        prefs_set_int_attribute (_prefs_path.c_str(), _attr.c_str(), (int) this->get_active());
    }
}

void PrefRadioButton::init(const Glib::ustring& label, const std::string& prefs_path, const std::string& attr,
                           const std::string& string_value, bool default_value, PrefRadioButton* group_member)
{
    (void)default_value;
    _value_type = VAL_STRING;
    _prefs_path = prefs_path;
    _attr = attr;
    _string_value = string_value;
    this->set_label(label);
    if (group_member)
    {
        Gtk::RadioButtonGroup rbg = group_member->get_group();
        this->set_group(rbg);
    }
    const gchar* val = prefs_get_string_attribute( _prefs_path.c_str(), _attr.c_str() );
    if ( val )
        this->set_active( std::string( val ) == _string_value);
    else
        this->set_active( false );
}

void PrefRadioButton::init(const Glib::ustring& label, const std::string& prefs_path, const std::string& attr,
                           int int_value, bool default_value, PrefRadioButton* group_member)
{
    _value_type = VAL_INT;
    _prefs_path = prefs_path;
    _attr = attr;
    _int_value = int_value;
    this->set_label(label);
    if (group_member)
    {
        Gtk::RadioButtonGroup rbg = group_member->get_group();
        this->set_group(rbg);
    }
    if (default_value)
        this->set_active( prefs_get_int_attribute( _prefs_path.c_str(), _attr.c_str(), int_value ) == _int_value);
    else
        this->set_active( prefs_get_int_attribute( _prefs_path.c_str(), _attr.c_str(), int_value + 1 )== _int_value);
}

void PrefRadioButton::on_toggled()
{
    this->changed_signal.emit(this->get_active());
    if (this->is_visible() && this->get_active() ) //only take action if toggled by user (to active)
    {
        if ( _value_type == VAL_STRING )
            prefs_set_string_attribute ( _prefs_path.c_str(), _attr.c_str(), _string_value.c_str());
        else if ( _value_type == VAL_INT )
            prefs_set_int_attribute ( _prefs_path.c_str(), _attr.c_str(), _int_value);
    }
}

void PrefSpinButton::init(const std::string& prefs_path, const std::string& attr,
              double lower, double upper, double step_increment, double page_increment,
              double default_value, bool is_int, bool is_percent)
{
    _prefs_path = prefs_path;
    _attr = attr;
    _is_int = is_int;
    _is_percent = is_percent;

    double value;
    if (is_int)
        if (is_percent)
            value = 100 * prefs_get_double_attribute_limited (prefs_path.c_str(), attr.c_str(), default_value, lower/100.0, upper/100.0);
        else
            value = (double) prefs_get_int_attribute_limited (prefs_path.c_str(), attr.c_str(), (int) default_value, (int) lower, (int) upper);
    else
        value = prefs_get_double_attribute_limited (prefs_path.c_str(), attr.c_str(), default_value, lower, upper);

    this->set_range (lower, upper);
    this->set_increments (step_increment, page_increment);
    this->set_numeric();
    this->set_value (value);
    this->set_width_chars(6);
    if (is_int)
        this->set_digits(0);
    else if (step_increment < 0.1)
        this->set_digits(4);
    else
        this->set_digits(2);

}

void PrefSpinButton::on_value_changed()
{
    if (this->is_visible()) //only take action if user changed value
    {
        if (_is_int)
            if (_is_percent)
                prefs_set_double_attribute(_prefs_path.c_str(), _attr.c_str(), this->get_value()/100.0);
            else
                prefs_set_int_attribute(_prefs_path.c_str(), _attr.c_str(), (int) this->get_value());
        else
            prefs_set_double_attribute (_prefs_path.c_str(), _attr.c_str(), this->get_value());
    }
}

void PrefCombo::init(const std::string& prefs_path, const std::string& attr,
                     Glib::ustring labels[], int values[], int num_items, int default_value)
{
    _prefs_path = prefs_path;
    _attr = attr;
    int row = 0;
    int value = prefs_get_int_attribute (_prefs_path.c_str(), _attr.c_str(), default_value);

    for (int i = 0 ; i < num_items; ++i)
    {
        this->append_text(labels[i]);
        _values.push_back(values[i]);
        if (value == values[i])
            row = i;
    }
    this->set_active(row);
}

void PrefCombo::on_changed()
{
    if (this->is_visible()) //only take action if user changed value
    {
        prefs_set_int_attribute (_prefs_path.c_str(), _attr.c_str(), _values[this->get_active_row_number()]);
    }
}

void PrefEntryButtonHBox::init(const std::string& prefs_path, const std::string& attr,
            bool visibility, gchar* default_string)
{
    _prefs_path = prefs_path;
    _attr = attr;
    _default_string = default_string;
    relatedEntry = new Gtk::Entry();
    relatedButton = new Gtk::Button(_("Reset"));
    relatedEntry->set_invisible_char('*');
    relatedEntry->set_visibility(visibility);
    relatedEntry->set_text(prefs_get_string_attribute(_prefs_path.c_str(), _attr.c_str()));
    this->pack_start(*relatedEntry);
    this->pack_start(*relatedButton);
    relatedButton->signal_clicked().connect(
            sigc::mem_fun(*this, &PrefEntryButtonHBox::onRelatedButtonClickedCallback));
    relatedEntry->signal_changed().connect(
            sigc::mem_fun(*this, &PrefEntryButtonHBox::onRelatedEntryChangedCallback));
}

void PrefEntryButtonHBox::onRelatedEntryChangedCallback()
{
    if (this->is_visible()) //only take action if user changed value
    {
        prefs_set_string_attribute(_prefs_path.c_str(), _attr.c_str(),
        relatedEntry->get_text().c_str());
    }
}

void PrefEntryButtonHBox::onRelatedButtonClickedCallback()
{
    if (this->is_visible()) //only take action if user changed value
    {
        prefs_set_string_attribute(_prefs_path.c_str(), _attr.c_str(),
        _default_string);
        relatedEntry->set_text(_default_string);
    }
}


void PrefFileButton::init(const std::string& prefs_path, const std::string& attr)
{
    _prefs_path = prefs_path;
    _attr = attr;
    select_filename(Glib::filename_from_utf8(prefs_get_string_attribute(_prefs_path.c_str(), _attr.c_str())));

    signal_selection_changed().connect(sigc::mem_fun(*this, &PrefFileButton::onFileChanged));
}

void PrefFileButton::onFileChanged()
{
    prefs_set_string_attribute(_prefs_path.c_str(), _attr.c_str(), Glib::filename_to_utf8(get_filename()).c_str());
}

void PrefEntry::init(const std::string& prefs_path, const std::string& attr,
            bool visibility)
{
    _prefs_path = prefs_path;
    _attr = attr;
    this->set_invisible_char('*');
    this->set_visibility(visibility);
    this->set_text(prefs_get_string_attribute(_prefs_path.c_str(), _attr.c_str()));
}

void PrefEntry::on_changed()
{
    if (this->is_visible()) //only take action if user changed value
    {
        prefs_set_string_attribute(_prefs_path.c_str(), _attr.c_str(), this->get_text().c_str());
    }
}

void PrefColorPicker::init(const Glib::ustring& label, const std::string& prefs_path, const std::string& attr,
                           guint32 default_rgba)
{
    _prefs_path = prefs_path;
    _attr = attr;
    _title = label;
    this->setRgba32( prefs_get_int_attribute (_prefs_path.c_str(), _attr.c_str(), (int)default_rgba) );
}

void PrefColorPicker::on_changed (guint32 rgba)
{
    if (this->is_visible()) //only take action if the user toggled it
    {
        prefs_set_int_attribute (_prefs_path.c_str(), _attr.c_str(), (int) rgba);
    }
}

void PrefUnit::init(const std::string& prefs_path, const std::string& attr)
{
    _prefs_path = prefs_path;
    _attr = attr;
    setUnitType(UNIT_TYPE_LINEAR);
    gchar const * prefval = prefs_get_string_attribute(_prefs_path.c_str(), _attr.c_str());
    setUnit(prefval);
}

void PrefUnit::on_changed()
{
    if (this->is_visible()) //only take action if user changed value
    {
        prefs_set_string_attribute(_prefs_path.c_str(), _attr.c_str(), getUnitAbbr().c_str());
    }
}

} // namespace Widget
} // namespace UI
} // namespace Inkscape

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=99 :
