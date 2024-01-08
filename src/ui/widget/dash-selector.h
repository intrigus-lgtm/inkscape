// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * A widget for selecting dash patterns and setting the dash offset.
 */
/* Authors:
 *   Tavmjong Bah (Rewrite to use Gio::ListStore and Gtk::GridView).
 *
 * Original authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Maximilian Albert <maximilian.albert@gmail.com> (gtkmm-ification)
 *
 * Copyright (C) 2002 Lauris Kaplinski
 * Copyright (C) 2023 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_DASH_SELECTOR_H
#define SEEN_DASH_SELECTOR_H

#include <glibmm/refptr.h>
#include <gtkmm/box.h>

namespace Gtk {
class DrawingArea;
class GridView;
class ListItem;
class Popover;
class SingleSelection;
} // namespace Gtk

namespace Inkscape::UI::Widget {

class DashSelector final : public Gtk::Box {

public:
    DashSelector();
    ~DashSelector() final;

    void set_dash_pattern(const std::vector<double>& dash, double offset);
    const std::vector<double>& get_dash_pattern() { return dash_pattern; }
    double get_offset() { return offset; }

    sigc::signal<void ()> changed_signal;

private:
    // Functions
    void update(int position);

    void activate(Gtk::GridView* grid, unsigned int position);

    void setup_listitem_cb(const Glib::RefPtr<Gtk::ListItem>& list_item);
    void bind_listitem_cb( const Glib::RefPtr<Gtk::ListItem>& list_item);

    void draw_pattern(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height,
                      const std::vector<double>& pattern);
    void draw_text(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

    // Variables
    std::vector<double> dash_pattern; // The current pattern.
    double offset = 0;                // The current offset.

    // Gtk
    Glib::RefPtr<Gtk::SingleSelection> selection;
    Gtk::DrawingArea* drawing_area = nullptr; // MenuButton
    Gtk::Popover* popover = nullptr;
    Glib::RefPtr<Gtk::Adjustment> adjustment; // Dash offset
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_DASH_SELECTOR_H

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
