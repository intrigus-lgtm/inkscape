// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Combobox for selecting dash patterns - implementation.
 */
/* Author:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *
 * Copyright (C) 2002 Lauris Kaplinski
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_SP_MARKER_COMBO_BOX_H
#define SEEN_SP_MARKER_COMBO_BOX_H

#include <map>
#include <memory>
#include <string>
#include <vector>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <giomm/liststore.h>
#include <gtkmm/box.h>
#include <gtkmm/cellrendererpixbuf.h>
#include <gtkmm/treemodel.h>
#include <sigc++/signal.h>

#include "display/drawing.h"
#include "document.h"
#include "helper/auto-connection.h"
#include "ui/operation-blocker.h"
#include "ui/widget/widget-vfuncs-class-init.h"

namespace Gtk {
class Builder;
class Button;
class CheckButton;
class FlowBox;
class Image;
class Label;
class MenuButton;
class Picture;
class SpinButton;
class ToggleButton;
} // namespace Gtk

class SPDocument;
class SPMarker;
class SPObject;

namespace Inkscape::UI::Widget {

class Bin;

/**
 * ComboBox-like class for selecting stroke markers.
 */
class MarkerComboBox final
    : public WidgetVfuncsClassInit
    , public Gtk::Box
{
    using parent_type = Gtk::Box;

public:
    MarkerComboBox(Glib::ustring id, int loc);

    void setDocument(SPDocument *);

    void set_current(SPObject *marker);
    std::string get_active_marker_uri();
    bool in_update() { return _update.pending(); };
    const char* get_id() { return _combo_id.c_str(); };
    int get_loc() { return _loc; };

    sigc::connection connect_changed(sigc::slot<void ()> slot);
    sigc::connection connect_edit   (sigc::slot<void ()> slot);

private:
    class MarkerItem : public Glib::Object
    {
    public:
        Cairo::RefPtr<Cairo::Surface> pix;
        SPDocument* source = nullptr;
        std::string id;
        std::string label;
        bool stock = false;
        bool history = false;
        bool separator = false;
        int width = 0;
        int height = 0;

        bool operator == (const MarkerItem& item) const;

        static Glib::RefPtr<MarkerItem> create() {
            return Glib::make_refptr_for_instance(new MarkerItem());
        }

    protected:
        MarkerItem() = default;
    };

    SPMarker* get_current() const;
    Glib::ustring _current_marker_id;

    sigc::signal<void ()> _signal_changed;
    sigc::signal<void ()> _signal_edit;

    Glib::RefPtr<Gtk::Builder> _builder;
    Gtk::FlowBox& _marker_list;
    Gtk::Label& _marker_name;
    Glib::RefPtr<Gio::ListStore<MarkerItem>> _marker_store;
    std::vector<Glib::RefPtr<MarkerItem>> _stock_items;
    std::vector<Glib::RefPtr<MarkerItem>> _history_items;
    std::map<Gtk::Widget*, Glib::RefPtr<MarkerItem>> _widgets_to_markers;
    UI::Widget::Bin &_preview_bin;
    Gtk::Picture& _preview;
    bool _preview_no_alloc = true;
    Gtk::Button& _link_scale;
    Gtk::SpinButton& _angle_btn;
    Gtk::MenuButton& _menu_btn;
    Gtk::SpinButton& _scale_x;
    Gtk::SpinButton& _scale_y;
    Gtk::CheckButton& _scale_with_stroke;
    Gtk::SpinButton& _offset_x;
    Gtk::SpinButton& _offset_y;
    Gtk::Widget& _input_grid;
    Gtk::ToggleButton& _orient_auto_rev;
    Gtk::ToggleButton& _orient_auto;
    Gtk::ToggleButton& _orient_angle;
    Gtk::Button& _orient_flip_horz;
    Gtk::Image& _current_img;
    Gtk::Button& _edit_marker;
    bool _scale_linked = true;
    guint32 _background_color;
    guint32 _foreground_color;
    Glib::ustring _combo_id;
    int _loc;
    OperationBlocker _update;
    SPDocument *_document = nullptr;
    std::unique_ptr<SPDocument> _sandbox;
    Gtk::CellRendererPixbuf _image_renderer;

    class MarkerColumns : public Gtk::TreeModel::ColumnRecord {
    public:
        Gtk::TreeModelColumn<Glib::ustring> label;
        Gtk::TreeModelColumn<const gchar *> marker;   // ustring doesn't work here on windows due to unicode
        Gtk::TreeModelColumn<gboolean> stock;
        Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf>> pixbuf;
        Gtk::TreeModelColumn<gboolean> history;
        Gtk::TreeModelColumn<gboolean> separator;

        MarkerColumns() {
            add(label); add(stock);  add(marker);  add(history); add(separator); add(pixbuf);
        }
    };
    MarkerColumns marker_columns;

    void update_ui(SPMarker* marker, bool select);
    void update_widgets_from_marker(SPMarker* marker);
    void update_store();
    Glib::RefPtr<MarkerItem> add_separator(bool filler);
    void update_scale_link();
    Glib::RefPtr<MarkerItem> get_active();
    Glib::RefPtr<MarkerItem> find_marker_item(SPMarker* marker);
    void css_changed(GtkCssStyleChange *change) override;
    void update_preview(Glib::RefPtr<MarkerItem> marker_item);
    void update_menu_btn(Glib::RefPtr<MarkerItem> marker_item);
    void set_active(Glib::RefPtr<MarkerItem> item);
    void init_combo();
    void set_history(Gtk::TreeModel::Row match_row);
    void marker_list_from_doc(SPDocument* source, bool history);
    std::vector<SPMarker*> get_marker_list(SPDocument* source);
    void add_markers (std::vector<SPMarker *> const& marker_list, SPDocument *source,  gboolean history);
    void remove_markers (gboolean history);
    Cairo::RefPtr<Cairo::Surface> create_marker_image(Geom::IntPoint pixel_size, gchar const *mname,
        SPDocument *source, Inkscape::Drawing &drawing, unsigned /*visionkey*/, bool checkerboard, bool no_clip, double scale);
    void refresh_after_markers_modified();
    auto_connection modified_connection;
    auto_connection _idle;
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_SP_MARKER_COMBO_BOX_H

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
