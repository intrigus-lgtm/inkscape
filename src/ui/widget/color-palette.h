// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Color palette widget
 */
/* Authors:
 *   Michael Kowalski
 *
 * Copyright (C) 2021 Michael Kowalski
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_COLOR_PALETTE_H
#define SEEN_COLOR_PALETTE_H

#include <memory>
#include <vector>
#include <glibmm/ustring.h>
#include <glibmm/refptr.h>
#include <gtkmm/box.h>
#include <sigc++/signal.h>
#include <2geom/int-point.h>

#include "helper/auto-connection.h"
#include "ui/widget/palette_t.h"
#include "ui/widget/bin.h"

namespace Gtk {
class Builder;
class Button;
class FlowBox;
class Popover;
class ScrolledWindow;
} // namespace Gtk

namespace Inkscape::UI {

namespace Dialog {
class ColorItem;
} // namespace Dialog

namespace Widget {

class ColorPaletteMenuItem;
class PopoverMenu;

class ColorPalette : public UI::Widget::Bin
{
public:
    ColorPalette();
    ~ColorPalette() override;

    // set colors presented in a palette
    void set_colors(std::vector<Dialog::ColorItem*> const &swatches);
    // list of palettes to present in the menu
    void set_palettes(const std::vector<palette_t>& palettes);
    // enable compact mode (true) with mini-scroll buttons, or normal mode (false) with regular scrollbars
    void set_compact(bool compact);
    // enlarge color tiles in a pinned panel
    void set_large_pinned_panel(bool large);
    // preferred number of colors in a group (used for color alignment in columns)
    void set_page_size(int page_size);

    void set_tile_size(int size_px);
    void set_tile_border(int border_px);
    void set_rows(int rows);
    void set_aspect(double aspect);
    // show horizontal scrollbar when only 1 row is set
    void enable_scrollbar(bool show);
    // allow tile stretching (horizontally)
    void enable_stretch(bool enable);
    // Show labels in swatches dialog
    void enable_labels(bool labels);
    // Show/hide settings
    void set_settings_visibility(bool show);

    int get_tile_size() const;
    int get_tile_border() const;
    int get_rows() const;
    double get_aspect() const;
    bool is_scrollbar_enabled() const;
    bool is_stretch_enabled() const;
    bool is_pinned_panel_large() const;
    bool are_labels_enabled() const;

    void set_selected(const Glib::ustring& id);

    sigc::signal<void (Glib::ustring)>& get_palette_selected_signal();
    sigc::signal<void ()>& get_settings_changed_signal();

    Gtk::Popover& get_settings_popover();

    void set_filter(std::function<bool (const Dialog::ColorItem&)> filter);
    void apply_filter();

private:
    void resize();
    void set_up_scrolling();
    void scroll(int dx, int dy, double snap, bool smooth);
    void do_scroll(int dx, int dy);
    static gboolean scroll_cb(gpointer self);
    void _set_tile_size(int size_px);
    void _set_tile_border(int border_px);
    void _set_rows(int rows);
    void _set_aspect(double aspect);
    void _enable_scrollbar(bool show);
    void _enable_stretch(bool enable);
    void _set_large_pinned_panel(bool large);
    static gboolean check_scrollbar(gpointer self);
    void update_checkbox();
    void update_stretch();
    int get_tile_size(bool horz) const;
    int get_tile_width() const;
    int get_tile_height() const;
    int get_palette_height() const;

    Gtk::Widget *_get_widget(Dialog::ColorItem *item);
    void rebuild_widgets();
    void refresh();

    std::vector<Dialog::ColorItem *> _normal_items;
    std::vector<Dialog::ColorItem *> _pinned_items;

    Glib::RefPtr<Gtk::Builder> _builder;
    Gtk::FlowBox& _normal_box;
    Gtk::FlowBox& _pinned_box;
    Gtk::ScrolledWindow& _scroll;
    Gtk::FlowBox& _scroll_btn;
    Gtk::Button& _scroll_up;
    Gtk::Button& _scroll_down;
    Gtk::Button& _scroll_left;
    Gtk::Button& _scroll_right;
    std::unique_ptr<PopoverMenu> _menu;
    std::vector<std::unique_ptr<ColorPaletteMenuItem>> _palette_menu_items;
    int _size = 10;
    int _border = 0;
    int _rows = 1;
    double _aspect = 0.0;
    bool _compact = true;
    sigc::signal<void (Glib::ustring)> _signal_palette_selected;
    sigc::signal<void ()> _signal_settings_changed;
    bool _in_update = false;
    guint _active_timeout = 0;
    bool _force_scrollbar = false;
    bool _stretch_tiles = false;
    double _scroll_step = 0.0; // smooth scrolling step
    double _scroll_final = 0.0; // smooth scroll final value
    bool _large_pinned_panel = false;
    bool _show_labels = false;
    int _page_size = 0;
    Geom::IntPoint _allocation;
    auto_connection _idle_resize;
};

} // namespace Widget
} // namespace Inkscape::UI

#endif // SEEN_COLOR_PALETTE_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:
