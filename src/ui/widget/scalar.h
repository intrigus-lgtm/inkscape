// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Carl Hetherington <inkscape@carlh.net>
 *   Derek P. Moore <derekm@hackunix.org>
 *   Bryce Harrington <bryce@bryceharrington.org>
 *   Johan Engelen <j.b.c.engelen@alumnus.utwente.nl>
 *
 * Copyright (C) 2004-2011 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_WIDGET_SCALAR_H
#define INKSCAPE_UI_WIDGET_SCALAR_H

#include <glibmm/refptr.h>

#include "labelled.h"

namespace Gtk {
class Adjustment;
} // namespace Gtk

namespace Inkscape::UI::Widget {

class SpinButton;

/**
 * A labelled text box, with spin buttons and optional
 * icon, for entering arbitrary number values.
 */
class Scalar : public Labelled
{
public:
    /**
     * Construct a Scalar Widget.
     *
     * @param label     Label, as per the Labelled base class.
     * @param tooltip   Tooltip, as per the Labelled base class.
     * @param icon      Icon name, placed before the label (defaults to empty).
     * @param mnemonic  Mnemonic toggle; if true, an underscore (_) in the label
     *                  indicates the next character should be used for the
     *                  mnemonic accelerator key (defaults to true).
     */
    Scalar(Glib::ustring const &label,
           Glib::ustring const &tooltip,
           Glib::ustring const &icon = {},
           bool mnemonic = true);

    /**
     * Construct a Scalar Widget.
     *
     * @param label     Label, as per the Labelled base class.
     * @param tooltip   Tooltip, as per the Labelled base class.
     * @param digits    Number of decimal digits to display.
     * @param icon      Icon name, placed before the label (defaults to empty).
     * @param mnemonic  Mnemonic toggle; if true, an underscore (_) in the label
     *                  indicates the next character should be used for the
     *                  mnemonic accelerator key (defaults to true).
     */
    Scalar(Glib::ustring const &label,
           Glib::ustring const &tooltip,
           unsigned digits,
           Glib::ustring const &icon = {},
           bool mnemonic = true);

    /**
     * Construct a Scalar Widget.
     *
     * @param label     Label, as per the Labelled base class.
     * @param tooltip   Tooltip, as per the Labelled base class.
     * @param adjust    Adjustment to use for the SpinButton.
     * @param digits    Number of decimal digits to display (defaults to 0).
     * @param icon      Icon name, placed before the label (defaults to empty).
     * @param mnemonic  Mnemonic toggle; if true, an underscore (_) in the label
     *                  indicates the next character should be used for the
     *                  mnemonic accelerator key (defaults to true).
     */
    Scalar(Glib::ustring const &label,
           Glib::ustring const &tooltip,
           Glib::RefPtr<Gtk::Adjustment> const &adjust,
           unsigned digits = 0,
           Glib::ustring const &icon = {},
           bool mnemonic = true);

    /**
     * Fetches the precision of the spin button.
     */
    unsigned getDigits() const;

    /**
     * Gets the current step increment used by the spin button.
     */
    double  getStep() const;

    /**
     * Gets the current page increment used by the spin button.
     */
    double  getPage() const;

    /**
     * Gets the minimum range value allowed for the spin button.
     */
    double  getRangeMin() const;

    /**
     * Gets the maximum range value allowed for the spin button.
     */
    double  getRangeMax() const;

    bool    getSnapToTicks() const;

    /**
     * Get the value in the spin_button.
     */
    double  getValue() const;

    /**
     * Get the value spin_button represented as an integer.
     */
    int     getValueAsInt() const;

    /**
     * Sets the precision to be displayed by the spin button.
     */
    void    setDigits(unsigned digits);

    /**
     * Sets the step and page increments for the spin button.
     * @todo Remove the second parameter - deprecated
     */
    void    setIncrements(double step, double page);

    /**
     * Sets the minimum and maximum range allowed for the spin button.
     */
    void    setRange(double min, double max);

    /**
     * Sets the value of the spin button.
     */
    void    setValue(double value, bool setProg = true);

    /**
     * Sets the width of the spin button by number of characters.
     */
    void    setWidthChars(unsigned chars);

    /**
     * Manually forces an update of the spin button.
     */
    void    update();

    /**
     * Adds a slider (HScale) to the left of the spinbox.
     */
    void    addSlider();

    /**
     * remove leading zeros fron widget.
     */
    void setNoLeadingZeros();
    bool setNoLeadingZerosOutput();

    /**
     * Set the number of set width chars of entry.
     */
    void setWidthChars(gint width_chars);

    /**
     * Signal raised when the spin button's value changes.
     */
    Glib::SignalProxy<void()> signal_value_changed();

    /**
     * true if the value was set by setValue, not changed by the user;
     * if a callback checks it, it must reset it back to false.
     */
    bool setProgrammatically;

    // permanently hide label part
    void hide_label();

    SpinButton const &getSpinButton() const;
    SpinButton       &getSpinButton() { return const_cast<SpinButton &>(std::as_const(*this).getSpinButton()); }
};

} // namespace Inkscape::UI::Widget

#endif // INKSCAPE_UI_WIDGET_SCALAR_H

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
