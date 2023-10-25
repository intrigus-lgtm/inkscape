// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "font-button.h"

#include <utility>
#include <gtkmm/fontbutton.h>

namespace Inkscape::UI::Widget {

FontButton::FontButton(Glib::ustring const &label, Glib::ustring const &tooltip,
                       Glib::ustring const &icon, bool mnemonic)
    : Labelled{label, tooltip, new Gtk::FontButton{"Sans 10"}, icon, mnemonic}
{
}

Glib::ustring FontButton::getValue() const
{
    return getFontButton().get_font();
}

void FontButton::setValue(Glib::ustring const &fontspec)
{
    getFontButton().set_font(fontspec);
}

Glib::SignalProxy<void()> FontButton::signal_font_value_changed()
{
    return getFontButton().signal_font_set();
}

Gtk::FontButton const &FontButton::getFontButton() const
{
    auto const fontButton = dynamic_cast<Gtk::FontButton const *>(getWidget());
    g_assert(fontButton);
    return *fontButton;
}

Gtk::FontButton &FontButton::getFontButton()
{
    return const_cast<Gtk::FontButton &>(std::as_const(*this).getFontButton());
}

} // namespace Inkscape::UI::Widget

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
