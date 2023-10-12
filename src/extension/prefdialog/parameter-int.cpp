// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2005-2007 Authors:
 *   Ted Gould <ted@gould.cx>
 *   Johan Engelen <johan@shouraizou.nl> *
 *   Jon A. Cruz <jon@joncruz.org>
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "parameter-int.h"

#include <cstring>
#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/label.h>

#include "extension/extension.h"
#include "preferences.h"
#include "ui/pack.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/spin-scale.h"
#include "xml/node.h"

namespace Inkscape::Extension {

ParamInt::ParamInt(Inkscape::XML::Node *xml, Inkscape::Extension::Extension *ext)
    : InxParameter(xml, ext)
{
    // get value
    if (xml->firstChild()) {
        const char *value = xml->firstChild()->content();
        if (value)
            string_to_value(value);
    }

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    _value = prefs->getInt(pref_name(), _value);

    // parse and apply limits
    const char *min = xml->attribute("min");
    if (min) {
        _min = std::strtol(min, nullptr, 0);
    }

    const char *max = xml->attribute("max");
    if (max) {
        _max = std::strtol(max, nullptr, 0);
    }

    if (_value < _min) {
        _value = _min;
    }

    if (_value > _max) {
        _value = _max;
    }

    // parse appearance
    if (_appearance) {
        if (!strcmp(_appearance, "full")) {
            _mode = FULL;
        } else {
            g_warning("Invalid value ('%s') for appearance of parameter '%s' in extension '%s'",
                      _appearance, _name, _extension->get_id());
        }
    }
}

/**
 * A function to set the \c _value.
 * This function sets the internal value, but it also sets the value
 * in the preferences structure.  To put it in the right place \c pref_name() is used.
 *
 * @param  in   The value to set to.
 */
int ParamInt::set(int in)
{
    _value = in;
    if (_value > _max) {
        _value = _max;
    }
    if (_value < _min) {
        _value = _min;
    }

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setInt(pref_name(), _value);

    return _value;
}

/** A class to make an adjustment that uses Extension params. */
class ParamIntAdjustment : public Gtk::Adjustment {
    /** The parameter to adjust. */
    ParamInt *_pref;
    sigc::signal<void ()> *_changeSignal;
public:
    /** Make the adjustment using an extension and the string describing the parameter. */
    ParamIntAdjustment(ParamInt *param, sigc::signal<void ()> *changeSignal)
        : Gtk::Adjustment(0.0, param->min(), param->max(), 1.0, 10.0, 0)
        , _pref(param)
        , _changeSignal(changeSignal)
    {
        this->set_value(_pref->get());
        this->signal_value_changed().connect(sigc::mem_fun(*this, &ParamIntAdjustment::val_changed));
    };

    void val_changed ();
}; /* class ParamIntAdjustment */

/**
 * A function to respond to the value_changed signal from the adjustment.
 *
 * This function just grabs the value from the adjustment and writes
 * it to the parameter.  Very simple, but yet beautiful.
 */
void ParamIntAdjustment::val_changed()
{
    _pref->set((int)this->get_value());
    if (_changeSignal != nullptr) {
        _changeSignal->emit();
    }
}

/**
 * Creates a Int Adjustment for a int parameter.
 *
 * Builds a hbox with a label and a int adjustment in it.
 */
Gtk::Widget *
ParamInt::get_widget(sigc::signal<void ()> *changeSignal)
{
    if (_hidden) {
        return nullptr;
    }

    auto const hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, GUI_PARAM_WIDGETS_SPACING);

    auto pia = new ParamIntAdjustment(this, changeSignal);
    Glib::RefPtr<Gtk::Adjustment> fadjust(pia);

    if (_mode == FULL) {
        Glib::ustring text;
        if (_text != nullptr)
            text = _text;

        auto const scale = Gtk::make_managed<UI::Widget::SpinScale>(text, fadjust, 0);
        scale->set_size_request(400, -1);
        UI::pack_start(*hbox, *scale, true, true);
    } else if (_mode == DEFAULT) {
        auto const label = Gtk::make_managed<Gtk::Label>(_text, Gtk::Align::START);
        UI::pack_start(*hbox, *label, true, true);

        auto const spin = Gtk::make_managed<Inkscape::UI::Widget::SpinButton>(fadjust, 1.0, 0);
        UI::pack_start(*hbox, *spin, false, false);
    }

    return hbox;
}

std::string ParamInt::value_to_string() const
{
    char value_string[32];
    std::snprintf(value_string, 32, "%d", _value);
    return value_string;
}

void ParamInt::string_to_value(const std::string &in)
{
    _value = std::strtol(in.c_str(), nullptr, 0);
}

} // namespace Inkscape::Extension

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
