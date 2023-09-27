// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Static style swatch (fill, stroke, opacity)
 */
/* Authors:
 *   buliabyak@gmail.com
 *   Krzysztof Kosiński <tweenk.pl@gmail.com>
 *
 * Copyright (C) 2005-2008 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_INKSCAPE_UI_STYLE_SWATCH_H
#define SEEN_INKSCAPE_UI_STYLE_SWATCH_H

#include <memory>
#include <gtkmm/box.h>
#include <gtkmm/gesture.h> // Gtk::EventSequenceState
#include <gtkmm/label.h>

#include "desktop.h"
#include "preferences.h"

class SPStyle;
class SPCSSAttr;

namespace Gtk {
class GestureClick;
class Grid;
} // namespace Gtk

namespace Inkscape {

namespace Util {
class Unit;
} // namespace Util

namespace UI::Widget {

class ColorPreview;

class StyleSwatch : public Gtk::Box
{
public:
    StyleSwatch (SPCSSAttr *attr, gchar const *main_tip, Gtk::Orientation orient = Gtk::Orientation::VERTICAL);
    ~StyleSwatch() override;

    void setStyle(SPStyle *style);
    void setStyle(SPCSSAttr *attr);
    SPCSSAttr *getStyle();

    void setWatchedTool (const char *path, bool synthesize);
    void setToolName(const Glib::ustring& tool_name);
    void setDesktop(SPDesktop *desktop);

private:
    Gtk::EventSequenceState on_click(Gtk::GestureClick const &click,
                                     int n_press, double x, double y);

    using PrefObs = Preferences::PreferencesObserver;

    SPDesktop *_desktop;
    Glib::ustring _tool_name;
    SPCSSAttr *_css;
    std::unique_ptr<PrefObs> _tool_obs;
    std::unique_ptr<PrefObs> _style_obs;
    Glib::ustring _tool_path;
    Gtk::Grid *_table;
    Gtk::Label _label[2];
    Gtk::Box _empty_space;
    Gtk::Label _value[2];
    Gtk::Box _place[2];
    Gtk::Label _opacity_value;
    std::unique_ptr<ColorPreview> _color_preview[2];
    Glib::ustring _color[2];
    Gtk::Box _stroke;
    Gtk::Label _stroke_width;
    Util::Unit *_sw_unit;

    friend void tool_obs_callback(StyleSwatch &, Preferences::Entry const &);
};

} // namespace UI::Widget

} // namespace Inkscape

#endif // SEEN_INKSCAPE_UI_STYLE_SWATCH_H

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
