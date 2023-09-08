// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "colorpicker.h"

#include <glibmm/i18n.h>
#include <gtkmm/box.h>

#include "document-undo.h"                       // for DocumentUndo
#include "live_effects/effect.h"                 // for Effect
#include "live_effects/parameter/colorpicker.h"  // for ColorPickerParam
#include "svg/svg-color.h"                       // for guint32
#include "ui/icon-names.h"                       // for INKSCAPE_ICON
#include "ui/pack.h"                             // for pack_start
#include "ui/widget/registered-widget.h"         // for RegisteredColorPicker
#include "util/safe-printf.h"                    // for safeprintf

class SPDocument;

namespace Inkscape {
namespace LivePathEffect {

ColorPickerParam::ColorPickerParam( const Glib::ustring& label, const Glib::ustring& tip,
                      const Glib::ustring& key, Inkscape::UI::Widget::Registry* wr,
                      Effect* effect, const guint32 default_color )
    : Parameter(label, tip, key, wr, effect),
      value(default_color),
      defvalue(default_color)
{
}

void
ColorPickerParam::param_set_default()
{
    param_setValue(defvalue);
}

static guint32 sp_read_color_alpha(gchar const *str, guint32 def)
{
    guint32 val = 0;
    if (str == nullptr) return def;
    while ((*str <= ' ') && *str) str++;
    if (!*str) return def;

    if (str[0] == '#') {
        gint i;
        for (i = 1; str[i]; i++) {
            int hexval;
            if (str[i] >= '0' && str[i] <= '9')
                hexval = str[i] - '0';
            else if (str[i] >= 'A' && str[i] <= 'F')
                hexval = str[i] - 'A' + 10;
            else if (str[i] >= 'a' && str[i] <= 'f')
                hexval = str[i] - 'a' + 10;
            else
                break;
            val = (val << 4) + hexval;
        }
        if (i != 1 + 8) {
            return def;
        }
    }
    return val;
}

void 
ColorPickerParam::param_update_default(const gchar * default_value)
{
    defvalue = sp_read_color_alpha(default_value, 0x000000ff);
}

bool
ColorPickerParam::param_readSVGValue(const gchar * strvalue)
{
    param_setValue(sp_read_color_alpha(strvalue, 0x000000ff));
    return true;
}

Glib::ustring
ColorPickerParam::param_getSVGValue() const
{
    gchar c[32];
    safeprintf(c, "#%08x", value);
    return c;
}

Glib::ustring
ColorPickerParam::param_getDefaultSVGValue() const
{
    gchar c[32];
    safeprintf(c, "#%08x", defvalue);
    return c;
}

Gtk::Widget *
ColorPickerParam::param_newWidget()
{
    auto const hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
    hbox->set_margin(5);

    auto const colorpickerwdg = Gtk::make_managed<UI::Widget::RegisteredColorPicker>( param_label,
                                                                                      param_label,
                                                                                      param_tooltip,
                                                                                      param_key,
                                                                                      param_key + "_opacity_LPE",
                                                                                     *param_wr,
                                                                                      param_effect->getRepr(),
                                                                                      param_effect->getSPDoc() );

    {
        SPDocument *document = param_effect->getSPDoc();
        DocumentUndo::ScopedInsensitive _no_undo(document);
        colorpickerwdg->setRgba32(value);
    }

    colorpickerwdg->set_undo_parameters(_("Change color button parameter"), INKSCAPE_ICON("dialog-path-effects"));

    UI::pack_start(*hbox, *colorpickerwdg, true, true);
    return hbox;
}

void
ColorPickerParam::param_setValue(const guint32 newvalue)
{
    value = newvalue;
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
