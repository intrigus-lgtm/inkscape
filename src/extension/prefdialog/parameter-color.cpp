// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2005-2007 Authors:
 *   Ted Gould <ted@gould.cx>
 *   Johan Engelen <johan@shouraizou.nl>
 *   Christopher Brown <audiere@gmail.com>
 *   Jon A. Cruz <jon@joncruz.org>
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <gtkmm/box.h>
#include <gtkmm/colorbutton.h>
#include <gtkmm/label.h>

#include "parameter-color.h"

#include "color.h"
#include "extension/extension.h"
#include "preferences.h"
#include "ui/pack.h"
#include "ui/widget/color-notebook.h"
#include "xml/node.h"

namespace Inkscape::Extension {

ParamColor::ParamColor(Inkscape::XML::Node *xml, Inkscape::Extension::Extension *ext)
    : InxParameter(xml, ext)
{
    // get value
    unsigned int _value = 0x000000ff; // default to black
    if (xml->firstChild()) {
        const char *value = xml->firstChild()->content();
        if (value)
            string_to_value(value);
        _value = _color.value();
    }

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    _value = prefs->getUInt(pref_name(), _value);

    _color.setValue(_value);

    _color_changed = _color.signal_changed.connect(sigc::mem_fun(*this, &ParamColor::_onColorChanged));
    // TODO: SelectedColor does not properly emit signal_changed after dragging, so we also need the following
    _color_released = _color.signal_released.connect(sigc::mem_fun(*this, &ParamColor::_onColorChanged));

    // parse appearance
    if (_appearance) {
        if (!strcmp(_appearance, "colorbutton")) {
            _mode = COLOR_BUTTON;
        } else {
            g_warning("Invalid value ('%s') for appearance of parameter '%s' in extension '%s'",
                      _appearance, _name, _extension->get_id());
        }
    }
}

ParamColor::~ParamColor()
{
    _color_changed.disconnect();
    _color_released.disconnect();
}

unsigned int ParamColor::set(unsigned int in)
{
    _color.setValue(in);

    return in;
}

Gtk::Widget *ParamColor::get_widget(sigc::signal<void ()> *changeSignal)
{
    if (_hidden) {
        return nullptr;
    }

    if (changeSignal) {
        _changeSignal = std::make_unique<sigc::signal<void ()>>(*changeSignal);
    }

    auto const hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, GUI_PARAM_WIDGETS_SPACING);
    if (_mode == COLOR_BUTTON) {
        auto const label = Gtk::make_managed<Gtk::Label>(_text, Gtk::Align::START);
        label->set_visible(true);
        UI::pack_start(*hbox, *label, true, true);

        Gdk::RGBA rgba;
        rgba.set_red_u  (((_color.value() >> 24) & 255) << 8);
        rgba.set_green_u(((_color.value() >> 16) & 255) << 8);
        rgba.set_blue_u (((_color.value() >>  8) & 255) << 8);
        rgba.set_alpha_u(((_color.value() >>  0) & 255) << 8);

        // TODO: It would be nicer to have a custom Gtk::ColorButton() implementation here,
        //       that wraps an Inkscape::UI::Widget::ColorNotebook into a new dialog
        _color_button = Gtk::make_managed<Gtk::ColorButton>(rgba);
        _color_button->set_title(_text);
        _color_button->set_use_alpha();
        _color_button->set_visible(true);
        UI::pack_end(*hbox, *_color_button, false, false);

        _color_button->signal_color_set().connect(sigc::mem_fun(*this, &ParamColor::_onColorButtonChanged));
    } else {
        Gtk::Widget *selector = Gtk::make_managed<Inkscape::UI::Widget::ColorNotebook>(_color);
        UI::pack_start(*hbox, *selector, true, true);
        selector->set_visible(true);
    }

    hbox->set_visible(true);
    return hbox;

}

void ParamColor::_onColorChanged()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setUInt(pref_name(), _color.value());

    if (_changeSignal)
        _changeSignal->emit();
}

void ParamColor::_onColorButtonChanged()
{
    Gdk::RGBA rgba = _color_button->get_rgba();
    unsigned int value = ((rgba.get_red_u()   >> 8) << 24) +
                         ((rgba.get_green_u() >> 8) << 16) +
                         ((rgba.get_blue_u()  >> 8) <<  8) +
                         ((rgba.get_alpha_u() >> 8) <<  0);
    set(value);
}

std::string ParamColor::value_to_string() const
{
    return std::to_string(_color.value());
}

void ParamColor::string_to_value(const std::string &in)
{
    // If we canʼt convert this will return 0 or ULONG_MAX
    _color.setValue(std::strtoul(in.c_str(), nullptr, 0));
}

} // namespace Inkscape::Extension
