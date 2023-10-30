// SPDX-License-Identifier: GPL-2.0-or-later

/** @file
 * @brief A widget with multiple panes. Agnostic to type what kind of widgets panes contain.
 *
 * Authors: see git history
 *   Tavmjong Bah
 *
 * Copyright (c) 2020 Tavmjong Bah, Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_DIALOG_MULTIPANED_H
#define INKSCAPE_UI_DIALOG_MULTIPANED_H

#include <vector>
#include <glibmm/refptr.h>
#include <gtkmm/gesture.h> // Gtk::EventSequenceState
#include <gtkmm/box.h>

#include "helper/auto-connection.h"

namespace Gdk {
class DragContext;
} // namespace Gdk

namespace Gtk {
class GestureDrag;
class SelectionData;
class TargetEntry;
} // namespace Gtk

namespace Inkscape::UI::Dialog {

/*
 * A widget with multiple panes. Agnostic to type what kind of widgets panes contain.
 * Handles allow a user to resize children widgets. Drop zones allow adding widgets
 * at either end.
 */
class DialogMultipaned final
    : public Gtk::Box
{
public:
    DialogMultipaned(Gtk::Orientation orientation = Gtk::Orientation::HORIZONTAL);
    ~DialogMultipaned() final;

    void prepend(Gtk::Widget &child);
    void append (Gtk::Widget &child);

    // Getters and setters
    Gtk::Widget *get_first_widget();
    Gtk::Widget *get_last_widget ();
    void get_children() = delete; ///< We manage our own child list. Call get_multipaned_children()
    std::vector<Gtk::Widget *> const &get_multipaned_children() const { return children; }
    void set_target_entries(const std::vector<Gtk::TargetEntry> &target_entries);
    bool has_empty_widget() const { return static_cast<bool>(_empty_widget); }

    // Signals
    sigc::signal<void (Gtk::SelectionData const &)> signal_prepend_drag_data();
    sigc::signal<void (Gtk::SelectionData const &)> signal_append_drag_data();
    sigc::signal<void ()> signal_now_empty();

    // UI functions
    void set_dropzone_sizes(int start, int end);
    void toggle_multipaned_children(bool show);
    void children_toggled();
    void ensure_multipaned_children();
    void set_restored_width(int width);

    static void    add_drop_zone_highlight_instances();
    static void remove_drop_zone_highlight_instances();

private:
    // Overrides
    Gtk::SizeRequestMode get_request_mode_vfunc() const override;
    /*void get_preferred_width_vfunc(int &minimum_width, int &natural_width) const override;
    void get_preferred_height_vfunc(int &minimum_height, int &natural_height) const override;
    void get_preferred_width_for_height_vfunc(int height, int &minimum_width, int &natural_width) const override;
    void get_preferred_height_for_width_vfunc(int width, int &minimum_height, int &natural_height) const override;
    void on_size_allocate(Gtk::Allocation &allocation) override;*/

    // Allow us to keep track of our widgets ourselves.
    //void forall_vfunc(gboolean include_internals, GtkCallback callback, gpointer callback_data) override;

    //void on_add(Gtk::Widget *child) override;
    //void on_remove(Gtk::Widget *child) override;

    // Signals
    sigc::signal<void (Gtk::SelectionData const &)> _signal_prepend_drag_data;
    sigc::signal<void (Gtk::SelectionData const &)> _signal_append_drag_data;
    sigc::signal<void ()> _signal_now_empty;

    // We must manage children ourselves.
    std::vector<Gtk::Widget *> children;

    // Values used when dragging handle.
    int _handle = -1; // Child number of active handle
    int _drag_handle = -1;
    Gtk::Widget* _resizing_widget1 = nullptr;
    Gtk::Widget* _resizing_widget2 = nullptr;
    Gtk::Widget* _hide_widget1 = nullptr;
    Gtk::Widget* _hide_widget2 = nullptr;
    Gtk::Allocation start_allocation1;
    Gtk::Allocation start_allocationh;
    Gtk::Allocation start_allocation2;
    Gtk::Allocation allocation1;
    Gtk::Allocation allocationh;
    Gtk::Allocation allocation2;

    // drag on handle/separator
    Gtk::EventSequenceState on_drag_begin (Gtk::GestureDrag const &gesture, double  start_x, double  start_y);
    Gtk::EventSequenceState on_drag_end   (Gtk::GestureDrag const &gesture, double offset_x, double offset_y);
    Gtk::EventSequenceState on_drag_update(Gtk::GestureDrag const &gesture, double offset_x, double offset_y);
    // drag+drop data
    void on_drag_data(Glib::RefPtr<Gdk::DragContext> const &context, int x, int y,
                      const Gtk::SelectionData &selection_data, guint info, guint time);
    void on_prepend_drag_data(Glib::RefPtr<Gdk::DragContext> const &context, int x, int y,
                              const Gtk::SelectionData &selection_data, guint info, guint time);
    void on_append_drag_data(Glib::RefPtr<Gdk::DragContext> const &context, int x, int y,
                             const Gtk::SelectionData &selection_data, guint info, guint time);

    // Others
    Gtk::Widget *_empty_widget; // placeholder in an empty container
    void insert(int pos, Gtk::Widget &child);
    void add_empty_widget();
    void remove_empty_widget();
    std::vector<auto_connection> _connections;
    int _natural_width = 0;
};

} // namespace Inkscape::UI::Dialog

#endif // INKSCAPE_UI_DIALOG_MULTIPANED_H

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
