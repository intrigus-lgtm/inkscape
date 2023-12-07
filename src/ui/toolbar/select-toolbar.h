// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * Selector aux toolbar
 */
/*
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <bulia@dr.com>
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Copyright (C) 2003 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_SELECT_TOOLBAR_H
#define SEEN_SELECT_TOOLBAR_H

#include <memory>
#include <string>
#include <vector>
#include <glibmm/refptr.h>

#include "toolbar.h"
#include "helper/auto-connection.h"

namespace Gtk {
class Adjustment;
class ToggleButton;
class Builder;
} // namespace Gtk

class SPDesktop;

namespace Inkscape {

class Selection;

namespace UI {

namespace Widget {
class UnitTracker;
class SpinButton;
} // namespace Widget

namespace Toolbar {

class SelectToolbar final : public Toolbar
{
public:
    using parent_type = Toolbar;

    SelectToolbar(SPDesktop *desktop);
    ~SelectToolbar() override;

private:
    Glib::RefPtr<Gtk::Builder> _builder;
    std::unique_ptr<UI::Widget::UnitTracker> _tracker;

    Gtk::ToggleButton &_select_touch_btn;

    Gtk::ToggleButton &_transform_stroke_btn;
    Gtk::ToggleButton &_transform_corners_btn;
    Gtk::ToggleButton &_transform_gradient_btn;
    Gtk::ToggleButton &_transform_pattern_btn;

    Inkscape::UI::Widget::SpinButton &_x_item;
    Inkscape::UI::Widget::SpinButton &_y_item;
    Inkscape::UI::Widget::SpinButton &_w_item;
    Inkscape::UI::Widget::SpinButton &_h_item;
    Gtk::ToggleButton &_lock_btn;

    std::vector<Gtk::Widget *> _context_items;
    std::vector<auto_connection> _connections;

    bool _update;
    std::string _action_key;
    std::string const _action_prefix;

    char const *get_action_key(double mh, double sh, double mv, double sv);
    void any_value_changed(Glib::RefPtr<Gtk::Adjustment>& adj);
    void layout_widget_update(Inkscape::Selection *sel);
    void on_inkscape_selection_modified(Inkscape::Selection *selection, unsigned flags);
    void on_inkscape_selection_changed (Inkscape::Selection *selection);
    void toggle_lock();
    void toggle_touch();
    void toggle_stroke();
    void toggle_corners();
    void toggle_gradient();
    void toggle_pattern();
    void setup_derived_spin_button(Inkscape::UI::Widget::SpinButton &btn, Glib::ustring const &name);

    void on_realize() final;
    void on_unrealize() final;
};

} // namespace Widget
} // namespace UI
} // namespace Inkscape

#endif /* !SEEN_SELECT_TOOLBAR_H */

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
