// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Color item used in palettes and swatches UI.
 */
/* Authors: PBS <pbs3141@gmail.com>
 * Copyright (C) 2022 PBS
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_DIALOG_COLOR_ITEM_H
#define INKSCAPE_UI_DIALOG_COLOR_ITEM_H

#include <array>
#include <string>
#include <variant>
#include <cairomm/refptr.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtk/gtk.h> // GtkEventControllerMotion
#include <gtkmm/drawingarea.h>
#include <gtkmm/gesture.h> // Gtk::EventSequenceState

#include "widgets/paintdef.h"

namespace Cairo {
class Context;
class ImageSurface;
} // namespace Cairo

namespace Gdk {
class Drag;
} // namespace Gdk

namespace Gtk {
class DragSource;
class GestureClick;
} // namespace Gtk

class SPGradient;

namespace Inkscape::UI::Dialog {

class DialogBase;

/**
 * The color item you see on-screen as a clickable box.
 *
 * Note: This widget must be outlived by its parent dialog, passed in the constructor.
 */
class ColorItem final : public Gtk::DrawingArea
{
public:
    /// Create a static color from a paintdef.
    ColorItem(PaintDef const&, DialogBase*);
    /// Add new group or filler element.
    ColorItem(Glib::ustring name);
    ~ColorItem() final;

    // Returns true if this is group heading rather than a color
    bool is_group() const;
    // Returns true if this is alignmet filler item, not a color
    bool is_filler() const;
    // Is paint "None"?
    bool is_paint_none() const;

    /**
     * Create a dynamically-updating color from a gradient, to which it remains linked.
     * If the gradient is destroyed, the widget will go into an inactive state.
     */
    ColorItem(SPGradient*, DialogBase*);

    /// Update the fill indicator, showing this widget is the fill of the current selection.
    void set_fill(bool);

    /// Update the stroke indicator, showing this widget is the stroke of the current selection.
    void set_stroke(bool);

    /// Update whether this item is pinned.
    bool is_pinned() const;
    void set_pinned_pref(const std::string &path);

    const Glib::ustring &get_description() const { return description; }

    sigc::signal<void ()>& signal_modified() { return _signal_modified; };
    sigc::signal<void ()>& signal_pinned() { return _signal_pinned; };

private:
    void draw_func(Cairo::RefPtr<Cairo::Context> const&, int width, int height);
    void size_allocate_vfunc(int width, int height, int baseline) final;

    Glib::RefPtr<Gdk::ContentProvider> on_drag_prepare(Gtk::DragSource const &source,
                                                       double x, double y);
    void on_drag_begin(Gtk::DragSource &source, Glib::RefPtr<Gdk::Drag> const &drag);

    // Common post-construction setup.
    void common_setup();

    void on_motion_enter(GtkEventControllerMotion const *motion,
                         double x, double y);
    void on_motion_leave(GtkEventControllerMotion const *motion);

    Gtk::EventSequenceState on_click_pressed (Gtk::GestureClick const &click,
                                              int n_press, double x, double y);
    Gtk::EventSequenceState on_click_released(Gtk::GestureClick const &click,
                                              int n_press, double x, double y);

    // Perform the on-click action of setting the fill or stroke.
    void on_click(bool stroke);

    // Perform the right-click action of showing the context menu.
    void on_rightclick();

    // Actions
    void action_set_fill();
    void action_set_stroke();
    void action_delete();
    void action_edit();
    void action_toggle_pin();
    void action_convert(Glib::ustring const &name);

    // Draw the color only (i.e. no indicators) to a Cairo context. Used for drawing both the widget and the drag/drop icon.
    void draw_color(Cairo::RefPtr<Cairo::Context> const &cr, int w, int h) const;

    // Construct an equivalent paintdef for use during drag/drop.
    PaintDef to_paintdef() const;

    // Return the color (or average if a gradient), for choosing the color of the fill/stroke indicators.
    std::array<double, 3> average_color() const;

    // Description of the color, shown in help text.
    Glib::ustring description;
    Glib::ustring color_id;
    Glib::ustring tooltip;

    /// The pinned preference path
    Glib::ustring pinned_pref;
    bool pinned_default = false;

    // The color.
    struct RGBData { std::array<unsigned, 3> rgb; };
    struct GradientData { SPGradient *gradient; };
    enum Undefined {};
    enum PaintNone {};
    std::variant<Undefined, PaintNone, RGBData, GradientData> data;

    // The dialog this widget belongs to. Used for determining what desktop to take action on.
    DialogBase *dialog = nullptr;

    // Whether this color is in use as the fill or stroke of the current selection.
    bool is_fill = false;
    bool is_stroke = false;

    // A cache of the widget contents, if necessary.
    Cairo::RefPtr<Cairo::ImageSurface> cache;
    bool cache_dirty = true;
    bool was_grad_pinned = false;

    // For ensuring that clicks that release outside the widget don't count.
    bool mouse_inside = false;

    sigc::signal<void ()> _signal_modified;
    sigc::signal<void ()> _signal_pinned;
};

} // namespace Inkscape::UI::Dialog

#endif // INKSCAPE_UI_DIALOG_COLOR_ITEM_H

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
