// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Bin: widget that can hold one child, useful as a base class of custom widgets
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_WIDGET_BIN_H
#define INKSCAPE_UI_WIDGET_BIN_H

#include <gtkmm/widget.h>

namespace Inkscape::UI::Widget {

/**
 * The Bin is a widget that can hold a single child. Itʼs useful for subclassing
 * as it encapsulates propagating the size request/allocation from/to the child,
 * sparing implementors of custom widgets from having to repeat that every time,
 * without e.g. inheriting more complex bases like Box & exposing all their API,
 * and without losing access to size_allocate_vfunc() via using a layout manager
 */
class Bin : public Gtk::Widget
{
public:
    Bin(Gtk::Widget *child = nullptr);
    ~Bin() override;

    /// Gets the child widget, or nullptr if none.
    Gtk::Widget *get_child() { return _child; }

    /// Gets the child widget, or nullptr if none.
    Gtk::Widget const *get_child() const { return _child; }

    /// Sets (parents or unparents) the child widget.
    void set_child(Gtk::Widget *child);

    /// Sets (parents) the child widget.
    void set_child(Gtk::Widget &child) { set_child(&child); }

    /// Unsets (unparents) the child widget.
    void unset_child() { set_child(nullptr); }

protected:
    void size_allocate_vfunc(int width, int height, int baseline) override;

private:
    Gtk::Widget *_child = nullptr;

    Gtk::SizeRequestMode get_request_mode_vfunc() const final;

    void measure_vfunc(Gtk::Orientation orientation, int for_size,
                       int &minimum, int &natural,
                       int &minimum_baseline, int &natural_baseline) const final;
};

} // namespace Inkscape::UI::Widget

#endif // INKSCAPE_UI_WIDGET_BIN_H

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
