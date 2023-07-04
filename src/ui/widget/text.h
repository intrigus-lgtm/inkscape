// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Carl Hetherington <inkscape@carlh.net>
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *
 * Copyright (C) 2004 Carl Hetherington
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_WIDGET_TEXT_H
#define INKSCAPE_UI_WIDGET_TEXT_H

#include "labelled.h"

namespace Gtk {
class Entry;
} // namespace Gtk

namespace Inkscape::UI::Widget {

/**
 * A labelled text box, with optional icon, for entering arbitrary number values.
 */
class Text : public Labelled
{
public:

    /**
     * Construct a Text Widget.
     *
     * @param label     Label, as per the Labelled base class.
     * @param tooltip   Tooltip, as per the Labelled base class.
     * @param icon      Icon name, placed before the label (defaults to empty).
     * @param mnemonic  Mnemonic toggle; if true, an underscore (_) in the label
     *                  indicates the next character should be used for the
     *                  mnemonic accelerator key (defaults to false).
     */
    Text(Glib::ustring const &label,
	 Glib::ustring const &tooltip,
	 Glib::ustring const &icon = {},
	 bool mnemonic = true);

    /**
     * Get the text in the entry.
     */
    Glib::ustring const getText() const;

    /**
     * Sets the text of the text entry.
     */
    void setText(Glib::ustring const text);

    void update();

    /**
     * Signal raised when the spin button's value changes.
     */
    Glib::SignalProxy<void()> signal_activate();

    bool setProgrammatically; // true if the value was set by setValue, not changed by the user;
                              // if a callback checks it, it must reset it back to false

private:
    Gtk::Entry const &getEntry() const;
    Gtk::Entry       &getEntry()      ;
};

} // namespace Inkscape::UI::Widget

#endif // INKSCAPE_UI_WIDGET_TEXT_H

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
