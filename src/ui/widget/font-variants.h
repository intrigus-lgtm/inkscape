// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author:
 *   Tavmjong Bah <tavmjong@free.fr>
 *
 * Copyright (C) 2015, 2018 Tavmong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_WIDGET_FONT_VARIANT_H
#define INKSCAPE_UI_WIDGET_FONT_VARIANT_H

#include <map>
#include <string>
#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/entry.h>
#include <gtkmm/expander.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/checkbutton.h>

class SPDesktop;
class SPObject;
class SPStyle;
class SPCSSAttr;

namespace Inkscape::UI::Widget {

class Feature;

/**
 * A container for selecting font variants (OpenType Features).
 */
class FontVariants final : public Gtk::Box
{
public:
    FontVariants();

private:
    // Ligatures: To start, use four check buttons.
    Gtk::Expander       _ligatures_frame;
    Gtk::Grid           _ligatures_grid;
    Gtk::CheckButton    _ligatures_common;
    Gtk::CheckButton    _ligatures_discretionary;
    Gtk::CheckButton    _ligatures_historical;
    Gtk::CheckButton    _ligatures_contextual;
    Gtk::Label          _ligatures_label_common;
    Gtk::Label          _ligatures_label_discretionary;
    Gtk::Label          _ligatures_label_historical;
    Gtk::Label          _ligatures_label_contextual;

    // Position: Exclusive options
    Gtk::Expander       _position_frame;
    Gtk::Grid           _position_grid;
    Gtk::CheckButton    _position_normal;
    Gtk::CheckButton    _position_sub;
    Gtk::CheckButton    _position_super;
    
    // Caps: Exclusive options (maybe a dropdown menu to save space?)
    Gtk::Expander       _caps_frame;
    Gtk::Grid           _caps_grid;
    Gtk::CheckButton    _caps_normal;
    Gtk::CheckButton    _caps_small;
    Gtk::CheckButton    _caps_all_small;
    Gtk::CheckButton    _caps_petite;
    Gtk::CheckButton    _caps_all_petite;
    Gtk::CheckButton    _caps_unicase;
    Gtk::CheckButton    _caps_titling;

    // Numeric: Complicated!
    Gtk::Expander       _numeric_frame;
    Gtk::Grid           _numeric_grid;

    Gtk::CheckButton    _numeric_default_style;
    Gtk::CheckButton    _numeric_lining;
    Gtk::Label          _numeric_lining_label;
    Gtk::CheckButton    _numeric_old_style;
    Gtk::Label          _numeric_old_style_label;

    Gtk::CheckButton    _numeric_default_width;
    Gtk::CheckButton    _numeric_proportional;
    Gtk::Label          _numeric_proportional_label;
    Gtk::CheckButton    _numeric_tabular;
    Gtk::Label          _numeric_tabular_label;

    Gtk::CheckButton    _numeric_default_fractions;
    Gtk::CheckButton    _numeric_diagonal;
    Gtk::Label          _numeric_diagonal_label;
    Gtk::CheckButton    _numeric_stacked;
    Gtk::Label          _numeric_stacked_label;

    Gtk::CheckButton    _numeric_ordinal;
    Gtk::Label          _numeric_ordinal_label;

    Gtk::CheckButton    _numeric_slashed_zero;
    Gtk::Label          _numeric_slashed_zero_label;

    // East Asian: Complicated!
    Gtk::Expander       _asian_frame;
    Gtk::Grid           _asian_grid;

    Gtk::CheckButton    _asian_default_variant;
    Gtk::CheckButton    _asian_jis78;
    Gtk::CheckButton    _asian_jis83;
    Gtk::CheckButton    _asian_jis90;
    Gtk::CheckButton    _asian_jis04;
    Gtk::CheckButton    _asian_simplified;
    Gtk::CheckButton    _asian_traditional;

    Gtk::CheckButton    _asian_default_width;
    Gtk::CheckButton    _asian_full_width;
    Gtk::CheckButton    _asian_proportional_width;

    Gtk::CheckButton    _asian_ruby;

    // -----
    Gtk::Expander       _feature_frame;
    Gtk::Grid           _feature_grid;
    Gtk::Box            _feature_vbox;
    Gtk::Entry          _feature_entry;
    Gtk::Label          _feature_label;
    Gtk::Label          _feature_list;
    Gtk::Label          _feature_substitutions;

    void ligatures_init();
    void ligatures_callback();

    void position_init();
    void position_callback();

    void caps_init();
    void caps_callback();

    void numeric_init();
    void numeric_callback();

    void asian_init();
    void asian_callback();

    void feature_init();
public:
    void feature_callback();

private:
    // To determine if we need to write out property (may not be necessary)
    unsigned _ligatures_all;
    unsigned _position_all;
    unsigned _caps_all;
    unsigned _numeric_all;
    unsigned _asian_all;

    unsigned _ligatures_mix;
    unsigned _position_mix;
    unsigned _caps_mix;
    unsigned _numeric_mix;
    unsigned _asian_mix;

    bool _ligatures_changed;
    bool _position_changed;
    bool _caps_changed;
    bool _numeric_changed;
    bool _feature_changed;
    bool _asian_changed;

    std::map<std::string, Feature*> _features;

    sigc::signal<void ()> _changed_signal;

public:

    /**
     * Update GUI based on query results.
     */
    void update( SPStyle const *query, bool different_features, Glib::ustring& font_spec );

    /**
     * Update GUI based on OpenType features of selected font.
     */
    void update_opentype( Glib::ustring& font_spec );

    /**
     * Fill SPCSSAttr based on settings of buttons.
     */
    void fill_css( SPCSSAttr* css );

    /**
     * Get CSS string for markup.
     */
    Glib::ustring get_markup();

    /**
     * Let others know that user has changed GUI settings.
     * (Used to enable 'Apply' and 'Default' buttons.)
     */
    sigc::connection connectChanged(sigc::slot<void ()> slot) {
        return _changed_signal.connect(slot);
    }
};

} // namespace Inkscape::UI::Widget

#endif // INKSCAPE_UI_WIDGET_FONT_VARIANT_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8 :
