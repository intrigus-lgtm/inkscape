// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <glibmm/i18n.h>
#include <gtkmm.h>

#include <utility>

#include "include/gtkmm_version.h"
#include "live_effects/parameter/message.h"
#include "live_effects/effect.h"

namespace Inkscape {

namespace LivePathEffect {

MessageParam::MessageParam( const Glib::ustring& label, const Glib::ustring& tip,
                      const Glib::ustring& key, Inkscape::UI::Widget::Registry* wr,
                      Effect* effect, const gchar * default_message, Glib::ustring  legend, 
                      Gtk::Align halign, Gtk::Align valign, double marginstart, double marginend)
    : Parameter(label, tip, key, wr, effect),
      defmessage(default_message),
      _legend(std::move(legend)),
      _halign(halign),
      _valign(valign),
      _marginstart(marginstart),
      _marginend(marginend)
{
    if (_legend == Glib::ustring("Use Label")) {
        _legend = label;
    }
    _label  = nullptr;
    _min_height = -1;
}

void
MessageParam::param_set_default()
{
    // do nothing
}

void 
MessageParam::param_update_default(const gchar * default_message)
{
    defmessage = default_message;
}

bool
MessageParam::param_readSVGValue(const gchar * strvalue)
{
    param_setValue(strvalue);
    return true;
}

Glib::ustring
MessageParam::param_getSVGValue() const
{
    return "";  // we dorn want to store messages in the SVG we store in LPE volatile
    // variables and get content with
    // param_getDefaultSVGValue() instead
}

Glib::ustring
MessageParam::param_getDefaultSVGValue() const
{
    return defmessage;
}

void
MessageParam::param_set_min_height(int height)
{
    _min_height = height;
    if (_label) {
        _label->set_size_request(-1, _min_height);
    }
}


Gtk::Widget *
MessageParam::param_newWidget()
{
    auto const frame = Gtk::make_managed<Gtk::Frame>(_legend);
    auto const widg_frame = frame->get_label_widget();
    widg_frame->set_margin_end(_marginend);
    widg_frame->set_margin_start(_marginstart);

    _label = Gtk::make_managed<Gtk::Label>(defmessage, Gtk::Align::END);
    _label->set_use_underline (true);
    _label->set_use_markup();
    _label->set_wrap(true);
    _label->set_size_request(-1, _min_height);
    _label->set_halign(_halign);
    _label->set_valign(_valign);
    _label->set_margin_end(_marginend);
    _label->set_margin_start(_marginstart);

    frame->add(*_label);
    return frame;
}

void
MessageParam::param_setValue(const gchar * strvalue)
{
    if (g_strcmp0(strvalue, defmessage.c_str())) {
        param_effect->refresh_widgets = true;
    }
    defmessage = strvalue;
}


} /* namespace LivePathEffect */

} /* namespace Inkscape */

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
