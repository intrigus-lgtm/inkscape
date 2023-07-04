// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Copyright (C) 2007 Author
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_WIDGET_FONT_BUTTON_H
#define INKSCAPE_UI_WIDGET_FONT_BUTTON_H

#include "labelled.h"

namespace Gtk {
class FontButton;
} // namespace Gtk

namespace Inkscape::UI::Widget {

/**
 * A labelled font button for entering font values
 */
class FontButton : public Labelled
{
public:
    /**
     * Construct a FontButton Widget.
     *
     * @param label     Label, as per the Labelled base class.
     * @param tooltip   Tooltip, as per the Labelled base class.
     * @param icon      Icon name, placed before the label (defaults to empty).
     * @param mnemonic  Mnemonic toggle; if true, an underscore (_) in the label
     *                  indicates the next character should be used for the
     *                  mnemonic accelerator key (defaults to false).
     */
    FontButton(Glib::ustring const &label,
               Glib::ustring const &tooltip,
               Glib::ustring const &icon = {},
               bool mnemonic = true);

    Glib::ustring getValue() const;
    void setValue(Glib::ustring const &fontspec);

    /**
    * Signal raised when the font button's value changes.
    */
    Glib::SignalProxy<void()> signal_font_value_changed();

private:
    [[nodiscard]] Gtk::FontButton const &getFontButton() const;
    [[nodiscard]] Gtk::FontButton       &getFontButton()      ;
};

} // namespace Inkscape::UI::Widget

#endif // INKSCAPE_UI_WIDGET_RANDOM_H

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
