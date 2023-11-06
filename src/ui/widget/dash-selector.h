// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Combobox for selecting dash patterns.
 */
/* Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Maximilian Albert <maximilian.albert> (gtkmm-ification)
 *
 * Copyright (C) 2002 Lauris Kaplinski
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_SP_DASH_SELECTOR_NEW_H
#define SEEN_SP_DASH_SELECTOR_NEW_H

#include <cstddef>
#include <vector>
#include <glibmm/refptr.h>
#include <gtkmm/box.h>
#include <gtkmm/cellrendererpixbuf.h>
#include <gtkmm/combobox.h>
#include <sigc++/signal.h>

namespace Gtk {
class Adjustment;
class ListStore;
} // namespace Gtk

namespace Inkscape::UI::Widget {

class SpinButton;

/**
 * Class that wraps a combobox and spinbutton for selecting dash patterns.
 */
class DashSelector final : public Gtk::Box {
public:
    DashSelector();
    ~DashSelector() final;

    /**
     * Get and set methods for dashes
     */
    void set_dash(const std::vector<double>& dash, double offset);
    const std::vector<double>& get_dash(double* offset) const;

    sigc::signal<void ()> changed_signal;

    double get_offset();

private:
    /**
     * Initialize dashes list from preferences
     */
    static void init_dashes();

    /**
     * Fill a pixbuf with the dash pattern using standard cairo drawing
     */
    Cairo::RefPtr<Cairo::Surface> sp_dash_to_pixbuf(const std::vector<double>& pattern);

    /**
     * Fill a pixbuf with text standard cairo drawing
     */
    Cairo::RefPtr<Cairo::Surface> sp_text_to_pixbuf(const char* text);

    /**
     * Callback for combobox image renderer
     */
    void prepareImageRenderer( Gtk::TreeModel::const_iterator const &row );

    /**
     * Callback for offset adjustment changing
     */
    void offset_value_changed();

    /**
     * Callback for combobox selection changing
     */
    void on_selection();

    /**
     * Combobox columns
     */
    class DashColumns : public Gtk::TreeModel::ColumnRecord {
    public:
        Gtk::TreeModelColumn<std::size_t> dash;
        DashColumns() {
            add(dash);
        }
    };
    DashColumns dash_columns;
    Glib::RefPtr<Gtk::ListStore> _dash_store;
    Gtk::ComboBox _dash_combo;
    Gtk::CellRendererPixbuf _image_renderer;
    Glib::RefPtr<Gtk::Adjustment> _offset;
    Inkscape::UI::Widget::SpinButton *_sb;
    static gchar const *const _prefs_path;
    int _preview_width;
    int _preview_height;
    int _preview_lineheight;
    std::vector<double>* _pattern = nullptr;
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_SP_DASH_SELECTOR_NEW_H

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
