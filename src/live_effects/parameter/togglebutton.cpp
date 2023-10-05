// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) Johan Engelen 2007 <j.b.c.engelen@utwente.nl>
 * Copyright (C) Jabiertxo Arraiza Cenoz 2014 <j.b.c.engelen@utwente.nl>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "togglebutton.h"

#include <utility>
#include <glibmm/i18n.h>
#include <gtkmm/label.h>

#include "inkscape.h"
#include "selection.h"

#include "live_effects/effect.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/pack.h"
#include "ui/util.h"
#include "ui/widget/registered-widget.h"
#include "util/numeric/converters.h"

namespace Inkscape::LivePathEffect {

ToggleButtonParam::ToggleButtonParam(const Glib::ustring &label, const Glib::ustring &tip, const Glib::ustring &key,
                                     Inkscape::UI::Widget::Registry *wr, Effect *effect, bool default_value,
                                     Glib::ustring inactive_label, char const *_icon_active, char const *_icon_inactive,
                                     Gtk::IconSize _icon_size)
    : Parameter(label, tip, key, wr, effect)
    , value(default_value)
    , defvalue(default_value)
    , inactive_label(std::move(inactive_label))
    , _icon_active(_icon_active)
    , _icon_inactive(_icon_inactive)
    , _icon_size(_icon_size)
{
    checkwdg = nullptr;
}

ToggleButtonParam::~ToggleButtonParam() {
    if (_toggled_connection.connected()) {
        _toggled_connection.disconnect();
    }
}

void
ToggleButtonParam::param_set_default()
{
    param_setValue(defvalue);
}

bool
ToggleButtonParam::param_readSVGValue(const gchar * strvalue)
{
    param_setValue(Inkscape::Util::read_bool(strvalue, defvalue));
    return true; // not correct: if value is unacceptable, should return false!
}

Glib::ustring
ToggleButtonParam::param_getSVGValue() const
{
    return value ? "true" : "false";
}

Glib::ustring
ToggleButtonParam::param_getDefaultSVGValue() const
{
    return defvalue ? "true" : "false";
}

void 
ToggleButtonParam::param_update_default(bool default_value)
{
    defvalue = default_value;
}

void 
ToggleButtonParam::param_update_default(const gchar * default_value)
{
    param_update_default(Inkscape::Util::read_bool(default_value, defvalue));
}

Gtk::Widget *
ToggleButtonParam::param_newWidget()
{
    if (_toggled_connection.connected()) {
        _toggled_connection.disconnect();
    }

   auto const checkwdg = Gtk::make_managed<UI::Widget::RegisteredToggleButton>( param_label,
                                                                                param_tooltip,
                                                                                param_key,
                                                                               *param_wr,
                                                                                false,
                                                                                param_effect->getRepr(),
                                                                                param_effect->getSPDoc() );

   auto const box_button = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);

   auto const label = Gtk::make_managed<Gtk::Label>();
   if (!param_label.empty()) {
       if (value || inactive_label.empty()) {
           label->set_text(param_label.c_str());
       } else {
           label->set_text(inactive_label.c_str());
       }
   }
   label->set_visible(true);
   if (_icon_active) {
       if (!_icon_inactive) {
           _icon_inactive = _icon_active;
       }
       box_button->set_visible(true);
       Gtk::Widget *icon_button = nullptr;
       if (!value) {
           icon_button = sp_get_icon_image(_icon_inactive, _icon_size);
       } else {
           icon_button = sp_get_icon_image(_icon_active, _icon_size);
       }
       icon_button->set_visible(true);
       UI::pack_start(*box_button, *icon_button, false, false, 1);
       if (!param_label.empty()) {
           UI::pack_start(*box_button, *label, false, false, 1);
       }
   } else {
       UI::pack_start(*box_button, *label, false, false, 1);
   }

   checkwdg->set_child(*box_button);
   checkwdg->setActive(value);
   checkwdg->setProgrammatically = false;
   checkwdg->set_undo_parameters(_("Change togglebutton parameter"), INKSCAPE_ICON("dialog-path-effects"));

   _toggled_connection = checkwdg->signal_toggled().connect(sigc::mem_fun(*this, &ToggleButtonParam::toggled));
   return checkwdg;
}

void
ToggleButtonParam::refresh_button()
{
    if (!_toggled_connection.connected()) {
        return;
    }

    if (!checkwdg){
        return;
    }

    auto const box_button = dynamic_cast<Gtk::Box *>(checkwdg->get_child());
    if (!box_button){
        return;
    }

    auto const children = UI::get_children(*box_button);
    g_assert(!children.empty());

    if (!param_label.empty()) {
        auto const lab = dynamic_cast<Gtk::Label *>(children.back());
        if (!lab) return;

        if (value || inactive_label.empty()) {
            lab->set_text(param_label.c_str());
        } else {
            lab->set_text(inactive_label.c_str());
        }
    }

    if ( _icon_active ) {
        auto const im = dynamic_cast<Gtk::Image *>(children.front());
        if (!im) return;

        gtk_image_set_from_icon_name(im->gobj(), value ? _icon_active : _icon_inactive);
        im->set_icon_size(_icon_size);
    }
}

void
ToggleButtonParam::param_setValue(bool newvalue)
{
    if (value != newvalue) {
        param_effect->refresh_widgets = true;
    }
    value = newvalue;
    refresh_button();
}

void
ToggleButtonParam::toggled() {
    if (SP_ACTIVE_DESKTOP) {
        Inkscape::Selection *selection = SP_ACTIVE_DESKTOP->getSelection();
        selection->emitModified();
    }
    _signal_toggled.emit();
}

} // namespace Inkscape::LivePathEffect

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
