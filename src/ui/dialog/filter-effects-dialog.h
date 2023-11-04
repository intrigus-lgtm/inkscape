// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Filter Effects dialog.
 */
/* Authors:
 *   Nicholas Bishop <nicholasbishop@gmail.org>
 *   Rodrigo Kumpera <kumpera@gmail.com>
 *   Felipe C. da S. Sanches <juca@members.fsf.org>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *   insaner
 *
 * Copyright (C) 2007 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_DIALOG_FILTER_EFFECTS_H
#define INKSCAPE_UI_DIALOG_FILTER_EFFECTS_H

#include <memory>
#include <2geom/point.h>
#include <sigc++/connection.h>
#include <sigc++/signal.h>
#include <glibmm/property.h>
#include <glibmm/propertyproxy.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtk/gtk.h> // GtkEventControllerMotion
#include <gtkmm/box.h>
#include <gtkmm/builder.h>
#include <gtkmm/cellrenderertoggle.h>
#include <gtkmm/gesture.h> // Gtk::EventSequenceState
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/treemodelcolumn.h>
#include <gtkmm/treeview.h>

#include "attributes.h"
#include "display/nr-filter-types.h"
#include "helper/auto-connection.h"
#include "ui/dialog/dialog-base.h"
#include "ui/widget/bin.h"
#include "ui/widget/combo-enums.h"
#include "ui/widget/completion-popup.h"
#include "xml/helper-observer.h"

namespace Gdk {
class Drag;
} // namespace Gdk

namespace Gtk {
class Button;
class CheckButton;
class DragSource;
class GestureClick;
class Grid;
class Label;
class ListStore;
class Paned;
class ScrolledWindow;
class ToggleButton;
} // namespace Gtk

class SPFilter;
class SPFilterPrimitive;

namespace Inkscape::UI {

namespace Widget {
class PopoverMenu;
} // namespace Widget

namespace Dialog {

class EntryAttr;
class FileOrElementChooser;
class DualSpinButton;
class MultiSpinButton;

class FilterEffectsDialog : public DialogBase
{
    using parent_type = DialogBase;

public:
    FilterEffectsDialog();
    ~FilterEffectsDialog() override;

    void set_attrs_locked(const bool);

private:
    void documentReplaced() override;
    void selectionChanged(Inkscape::Selection *selection) override;
    void selectionModified(Inkscape::Selection *selection, guint flags) override;

    Inkscape::auto_connection _resource_changed;

    friend class FileOrElementChooser;

    class FilterModifier : public Gtk::Box
    {
    public:
        FilterModifier(FilterEffectsDialog& d, Glib::RefPtr<Gtk::Builder> builder);

        void update_filters();
        void update_selection(Selection *);

        SPFilter* get_selected_filter();
        void select_filter(const SPFilter*);
        void add_filter();
        bool is_selected_filter_active();
        void toggle_current_filter();
        bool filters_present() const;

        sigc::signal<void ()>& signal_filter_changed()
        {
            return _signal_filter_changed;
        }
        sigc::signal<void ()>& signal_filters_updated() {
            return _signal_filters_updated;
        }

    private:
        class Columns : public Gtk::TreeModel::ColumnRecord
        {
        public:
            Columns()
            {
                add(filter);
                add(label);
                add(sel);
                add(count);
            }

            Gtk::TreeModelColumn<SPFilter*> filter;
            Gtk::TreeModelColumn<Glib::ustring> label;
            Gtk::TreeModelColumn<int> sel;
            Gtk::TreeModelColumn<int> count;
        };

        std::unique_ptr<UI::Widget::PopoverMenu> create_menu();
        void on_filter_selection_changed();
        void on_name_edited(const Glib::ustring&, const Glib::ustring&);
        void on_selection_toggled(const Glib::ustring&);
        void selection_toggled(Gtk::TreeModel::iterator iter, bool toggle);

        void update_counts();
        Gtk::EventSequenceState filter_list_click_released(Gtk::GestureClick const &click,
                                                           int n_press, double x, double y);
        void remove_filter();
        void duplicate_filter();
        void rename_filter();
        void select_filter_elements();

        Glib::RefPtr<Gtk::Builder> _builder;
        FilterEffectsDialog& _dialog;
        Gtk::TreeView& _list;
        Glib::RefPtr<Gtk::ListStore> _filters_model;
        Columns _columns;
        Gtk::CellRendererToggle _cell_toggle;
        Gtk::Button& _add;
        Gtk::Button& _dup;
        Gtk::Button& _del;
        Gtk::Button& _select;
        std::unique_ptr<UI::Widget::PopoverMenu> _menu;
        sigc::signal<void ()> _signal_filter_changed;
        std::unique_ptr<Inkscape::XML::SignalObserver> _observer;
        sigc::signal<void ()> _signal_filters_updated;
    };

    class PrimitiveColumns : public Gtk::TreeModel::ColumnRecord
    {
    public:
        PrimitiveColumns()
        {
            add(primitive);
            add(type_id);
            add(type);
            add(id);
        }

        Gtk::TreeModelColumn<SPFilterPrimitive*> primitive;
        Gtk::TreeModelColumn<Inkscape::Filters::FilterPrimitiveType> type_id;
        Gtk::TreeModelColumn<Glib::ustring> type;
        Gtk::TreeModelColumn<Glib::ustring> id;
    };

    class CellRendererConnection : public Gtk::CellRenderer
    {
    public:
        CellRendererConnection();
        Glib::PropertyProxy<void*> property_primitive();

        static constexpr int size_w = 16;
        static constexpr int size_h = 21;

    private:
        void get_preferred_width_vfunc(Gtk::Widget& widget,
                                       int& minimum_width,
                                       int& natural_width) const override;

        void get_preferred_width_for_height_vfunc(Gtk::Widget& widget,
                                                  int height,
                                                  int& minimum_width,
                                                  int& natural_width) const override;

        void get_preferred_height_vfunc(Gtk::Widget& widget,
                                        int& minimum_height,
                                        int& natural_height) const override;

        void get_preferred_height_for_width_vfunc(Gtk::Widget& widget,
                                                  int width,
                                                  int& minimum_height,
                                                  int& natural_height) const override;

        // void* should be SPFilterPrimitive*, some weirdness with properties prevents this
        Glib::Property<void*> _primitive;
    };

    class PrimitiveList : public Gtk::TreeView
    {
        using parent_type = Gtk::TreeView;

    public:
        PrimitiveList(FilterEffectsDialog&);

        sigc::signal<void ()>& signal_primitive_changed();

        void update();
        void set_menu(Gtk::Widget &parent,
                      sigc::slot<void ()>  dup,
                      sigc::slot<void ()>  rem);

        SPFilterPrimitive* get_selected();
        void select(SPFilterPrimitive *prim);
        void remove_selected();
        int primitive_count() const;
        int get_input_type_width() const;
        void set_inputs_count(int count);
        int get_inputs_count() const;

    private:
        void snapshot_vfunc(Glib::RefPtr<Gtk::Snapshot> const &snapshot) final;

        void on_drag_end(Gtk::DragSource const &source,
                         Glib::RefPtr<Gdk::Drag> const &drag, bool delete_data);

        Gtk::EventSequenceState on_click_pressed (Gtk::GestureClick const &click,
                                                  int n_press, double x, double y);
        Gtk::EventSequenceState on_click_released(Gtk::GestureClick const &click,
                                                  int n_press, double x, double y);
        void on_motion_motion(GtkEventControllerMotion const *motion,
                              double x, double y);

        void init_text();

        bool do_connection_node(const Gtk::TreeModel::iterator& row, const int input,
                                std::vector<Geom::Point> &points,
                                const int ix, const int iy);

        const Gtk::TreeModel::iterator find_result(const Gtk::TreeModel::iterator& start, const SPAttr attr, int& src_id, const int pos);
        int find_index(const Gtk::TreeModel::iterator& target);
        void draw_connection(const Cairo::RefPtr<Cairo::Context>& cr,
                             const Gtk::TreeModel::iterator&, const SPAttr attr, const int text_start_x,
                             const int x1, const int y1, const int row_count, const int pos,
                             const Gdk::RGBA fg_color, const Gdk::RGBA mid_color);
        void sanitize_connections(const Gtk::TreeModel::iterator& prim_iter);
        void on_primitive_selection_changed();
        bool on_scroll_timeout();

        FilterEffectsDialog& _dialog;
        Glib::RefPtr<Gtk::ListStore> _model;
        PrimitiveColumns _columns;
        CellRendererConnection _connection_cell;
        std::unique_ptr<UI::Widget::PopoverMenu> _primitive_menu;
        Glib::RefPtr<Pango::Layout> _vertical_layout;
        int _in_drag = 0;
        SPFilterPrimitive *_drag_prim = nullptr;
        sigc::signal<void ()> _signal_primitive_changed;
        sigc::connection _scroll_connection;
        int _autoscroll_y{};
        int _autoscroll_x{};
        std::unique_ptr<Inkscape::XML::SignalObserver> _observer;
        int _input_type_width {};
        int _input_type_height{};
        int _inputs_count     {};
    };

    void init_settings_widgets();

    // Handlers
    void add_primitive();
    void remove_primitive();
    void duplicate_primitive();
    void convolve_order_changed();
    void image_x_changed();
    void image_y_changed();
    void add_filter_primitive(Filters::FilterPrimitiveType type);

    void set_attr_direct(const UI::Widget::AttrWidget*);
    void set_child_attr_direct(const UI::Widget::AttrWidget*);
    void set_filternode_attr(const UI::Widget::AttrWidget*);
    void set_attr(SPObject*, const SPAttr, const gchar* val);
    void update_settings_view();
    void update_filter_general_settings_view();
    void update_settings_sensitivity();
    void update_color_matrix();
    void update_automatic_region(Gtk::CheckButton *btn);
    void add_effects(Inkscape::UI::Widget::CompletionPopup& popup, bool symbolic);

    Glib::RefPtr<Gtk::Builder> _builder;
    UI::Widget::Bin _bin;
    Glib::ustring _prefs = "/dialogs/filters";
    Gtk::Paned& _paned;
    Gtk::Grid& _main_grid;
    Gtk::Box& _params_box;
    Gtk::Box& _search_box;
    Gtk::Box& _search_wide_box;
    Gtk::ScrolledWindow& _filter_wnd;
    bool _narrow_dialog = true;
    Gtk::ToggleButton *_show_sources = nullptr;
    Gtk::CheckButton& _cur_filter_btn;
    sigc::connection _cur_filter_toggle;
    // View/add primitives
    Gtk::ScrolledWindow* _primitive_box;

    UI::Widget::ComboBoxEnum<Inkscape::Filters::FilterPrimitiveType> _add_primitive_type;
    Gtk::Button _add_primitive;

    // Bottom pane (filter effect primitive settings)
    Gtk::Box _settings_filter;
    Gtk::Box _settings_effect;
    Gtk::Label _empty_settings;
    Gtk::Label _no_filter_selected;
    Gtk::Label* _cur_effect_name;
    bool _settings_initialized;

    class Settings;
    class MatrixAttr;
    class ColorMatrixValues;
    class ComponentTransferValues;
    class LightSourceControl;

    std::unique_ptr<Settings> _settings;
    std::unique_ptr<Settings> _filter_general_settings;

    // General settings
    MultiSpinButton *_region_pos, *_region_size;

    // Color Matrix
    ColorMatrixValues* _color_matrix_values;

    // Component Transfer
    ComponentTransferValues* _component_transfer_values;

    // Convolve Matrix
    MatrixAttr* _convolve_matrix;
    DualSpinButton* _convolve_order;
    MultiSpinButton* _convolve_target;

    // Image
    EntryAttr* _image_x;
    EntryAttr* _image_y;

    // For controlling setting sensitivity
    Gtk::Widget* _k1, *_k2, *_k3, *_k4;

    // To prevent unwanted signals
    bool _locked;
    bool _attr_lock;

    // These go last since they depend on the prior initialization of
    // other FilterEffectsDialog members
    FilterModifier _filter_modifier;
    PrimitiveList _primitive_list;
    Inkscape::UI::Widget::CompletionPopup _effects_popup;
};

} // namespace Dialog

} // namespace Inkscape::UI

#endif // INKSCAPE_UI_DIALOG_FILTER_EFFECTS_H

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
