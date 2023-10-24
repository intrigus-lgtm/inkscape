// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * Filter effect selection selection widget
 */
/*
 * Author:
 *   Nicholas Bishop <nicholasbishop@gmail.com>
 *   Tavmjong Bah
 *
 * Copyright (C) 2007, 2017 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "filter-effect-chooser.h"

#include "ui/pack.h"

namespace Inkscape {

// Blend modes are in six groups according to the types of changes they make to luminosity
// See: https://typefully.com/DanHollick/blending-modes-KrBa0JP
// Add 5 to ENDMODE for the five additional separators in the list
const int SP_CSS_BLEND_COUNT = SP_CSS_BLEND_ENDMODE + 5;
const EnumData<SPBlendMode> SPBlendModeData[SP_CSS_BLEND_COUNT] = {
    { SP_CSS_BLEND_NORMAL,     NC_("BlendMode", "Normal"), "normal" },
    { SP_CSS_BLEND_ENDMODE, "-", "-" },
    { SP_CSS_BLEND_DARKEN,     NC_("BlendMode", "Darken"), "darken" },
    { SP_CSS_BLEND_MULTIPLY,   NC_("BlendMode", "Multiply"), "multiply" },
    { SP_CSS_BLEND_COLORBURN,  NC_("BlendMode", "Color Burn"), "color-burn" },
    { SP_CSS_BLEND_ENDMODE, "-", "-" },
    { SP_CSS_BLEND_LIGHTEN,    NC_("BlendMode", "Lighten"), "lighten" },
    { SP_CSS_BLEND_SCREEN,     NC_("BlendMode", "Screen"), "screen" },
    { SP_CSS_BLEND_COLORDODGE, NC_("BlendMode", "Color Dodge"), "color-dodge" },
    { SP_CSS_BLEND_ENDMODE, "-", "-" },
    { SP_CSS_BLEND_OVERLAY,    NC_("BlendMode", "Overlay"), "overlay" },
    { SP_CSS_BLEND_SOFTLIGHT,  NC_("BlendMode", "Soft Light"), "soft-light" },
    { SP_CSS_BLEND_HARDLIGHT,  NC_("BlendMode", "Hard Light"), "hard-light" },
    { SP_CSS_BLEND_ENDMODE, "-", "-" },
    { SP_CSS_BLEND_DIFFERENCE, NC_("BlendMode", "Difference"), "difference" },
    { SP_CSS_BLEND_EXCLUSION,  NC_("BlendMode", "Exclusion"), "exclusion" },
    { SP_CSS_BLEND_ENDMODE, "-", "-" },
    { SP_CSS_BLEND_HUE,        NC_("BlendMode", "Hue"), "hue" },
    { SP_CSS_BLEND_SATURATION, NC_("BlendMode", "Saturation"), "saturation" },
    { SP_CSS_BLEND_COLOR,      NC_("BlendMode", "Color"), "color" },
    { SP_CSS_BLEND_LUMINOSITY, NC_("BlendMode", "Luminosity"), "luminosity" }
};
const EnumDataConverter<SPBlendMode> SPBlendModeConverter(SPBlendModeData, SP_CSS_BLEND_COUNT);


namespace UI {
namespace Widget {

SimpleFilterModifier::SimpleFilterModifier(int flags)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , _flags(flags)
    , _lb_blend(_("Blend mode:"))
    , _lb_isolation("Isolate") // Translate for 1.1
    , _blend(SPBlendModeConverter, SPAttr::INVALID, false, "BlendMode")
    , _blur(_("Blur (%)"), 0, 0, 100, 1, 0.1, 1)
    , _opacity(_("Opacity (%)"), 0, 0, 100, 1, 0.1, 1)
    , _notify(true)
    , _hb_blend(Gtk::Orientation::HORIZONTAL, 4)
{
    set_name("SimpleFilterModifier");

    /* "More options" expander --------
    _extras.set_visible();
    _extras.set_label(_("More options"));
    auto const box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    _extras.add(*box);
    if (flags & (BLEND | BLUR)) {
        add(_extras);
    }
    */

    _flags = flags;

    if (flags & BLEND) {
        append(_hb_blend);
        _lb_blend.set_use_underline();
        _hb_blend.set_halign(Gtk::Align::END);
        _hb_blend.set_valign(Gtk::Align::CENTER);
        _hb_blend.set_margin_top(0);
        _hb_blend.set_margin_bottom(1);
        _hb_blend.set_margin_end(2);
        _lb_blend.set_mnemonic_widget(_blend);
        UI::pack_start(_hb_blend, _lb_blend, false, false);
        UI::pack_start(_hb_blend, _blend, false, false);
        /*
        * For best fit inkscape-browsers with no GUI to isolation we need all groups,
        * clones, and symbols with isolation == isolate to not show to the Inkscape
        * user "strange" behaviour from the designer point of view.
        * It's strange because it only happens when object doesn't have: clip, mask,
        * filter, blending, or opacity.
        * Anyway the feature is a no-gui feature and renders as expected.
        */
        /* if (flags & ISOLATION) {
            _isolation.property_active() = false;
            UI::pack_start(_hb_blend, _isolation, false, false, 5);
            UI::pack_start(_hb_blend, _lb_isolation, false, false, 5);
            _isolation.set_tooltip_text("Don't blend childrens with objects behind");
            _lb_isolation.set_tooltip_text("Don't blend childrens with objects behind");
        } */
    }

    if (flags & BLUR) {
       append(_blur);
    }

    if (flags & OPACITY) {
        append(_opacity);
    }

    _blend.signal_changed().connect(signal_blend_changed());
    _blur.signal_value_changed().connect(signal_blur_changed());
    _opacity.signal_value_changed().connect(signal_opacity_changed());
    _isolation.signal_toggled().connect(signal_isolation_changed());
}

sigc::signal<void ()> &SimpleFilterModifier::signal_isolation_changed()
{
    if (_notify) {
        return _signal_isolation_changed;
    }
    _notify = true;
    return _signal_null;
}

sigc::signal<void ()>& SimpleFilterModifier::signal_blend_changed()
{
    if (_notify) {
        return _signal_blend_changed;
    }
    _notify = true;
    return _signal_null;
}

sigc::signal<void ()>& SimpleFilterModifier::signal_blur_changed()
{
    // we dont use notifi to block use aberaje for multiple
    return _signal_blur_changed;
}

sigc::signal<void ()>& SimpleFilterModifier::signal_opacity_changed()
{
    // we dont use notifi to block use averaje for multiple
    return _signal_opacity_changed;
}

SPIsolation SimpleFilterModifier::get_isolation_mode()
{
    return _isolation.get_active() ? SP_CSS_ISOLATION_ISOLATE : SP_CSS_ISOLATION_AUTO;
}

void SimpleFilterModifier::set_isolation_mode(const SPIsolation val, bool notify)
{
    _notify = notify;
    _isolation.set_active(val == SP_CSS_ISOLATION_ISOLATE);
}

SPBlendMode SimpleFilterModifier::get_blend_mode()
{
    const Util::EnumData<SPBlendMode> *d = _blend.get_active_data();
    if (d) {
        return _blend.get_active_data()->id;
    } else {
        return SP_CSS_BLEND_NORMAL;
    }
}

void SimpleFilterModifier::set_blend_mode(const SPBlendMode val, bool notify)
{
    _notify = notify;
    _blend.set_active_by_id(val);
}

double SimpleFilterModifier::get_blur_value() const
{
    return _blur.get_value();
}

void SimpleFilterModifier::set_blur_value(const double val)
{
    _blur.set_value(val);
}

double SimpleFilterModifier::get_opacity_value() const
{
    return _opacity.get_value();
}

void SimpleFilterModifier::set_opacity_value(const double val)
{
    _opacity.set_value(val);
}

}
}
}

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
