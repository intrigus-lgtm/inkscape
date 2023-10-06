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

#include <cassert>
#include <iostream>
#include <numeric>
#include <glibmm/i18n.h>
#include <gtkmm/box.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/overlay.h>
#include <sigc++/functors/mem_fun.h>

#include "dialog-multipaned.h"
#include "dialog-window.h"
#include "ui/controller.h"
#include "ui/dialog/dialog-notebook.h"
#include "ui/util.h"
#include "ui/widget/canvas-grid.h"

static constexpr int DROPZONE_SIZE      =  5;
static constexpr int DROPZONE_EXPANSION = 15;
static constexpr int HANDLE_SIZE        = 12;
static constexpr int HANDLE_CROSS_SIZE  = 25;

namespace Inkscape::UI::Dialog {

/*
 * References:
 *   https://blog.gtk.org/2017/06/
 *   https://developer.gnome.org/gtkmm-tutorial/stable/sec-custom-containers.html.en
 *   https://wiki.gnome.org/HowDoI/Gestures
 *
 * The children widget sizes are "sticky". They change a minimal
 * amount when the parent widget is resized or a child is added or
 * removed.
 *
 * A gesture is used to track handle movement. This must be attached
 * to the parent widget (the offset_x/offset_y values are relative to
 * the widget allocation which changes for the handles as they are
 * moved).
 */

int get_handle_size() {
    return HANDLE_SIZE;
}

/* ============ MyDropZone ============ */

/**
 * Dropzones are eventboxes at the ends of a DialogMultipaned where you can drop dialogs.
 */
class MyDropZone final
    : public Gtk::Orientable
    , public Gtk::Box
{
public:
    MyDropZone(Gtk::Orientation orientation);
    ~MyDropZone() final;

    static void add_highlight_instances();
    static void remove_highlight_instances();

private:
    void set_size(int size);
    bool _active = false;
    void add_highlight();
    void remove_highlight();

    static std::vector<MyDropZone *> _instances_list;
    friend class DialogMultipaned;
};

std::vector<MyDropZone *> MyDropZone::_instances_list;

MyDropZone::MyDropZone(Gtk::Orientation orientation)
    : Glib::ObjectBase("MultipanedDropZone")
    , Gtk::Orientable()
    , Gtk::Box()
{
    set_name("MultipanedDropZone");
    set_orientation(orientation);
    set_size(DROPZONE_SIZE);

    add_css_class("backgnd-passive");

    signal_drag_motion().connect([=](Glib::RefPtr<Gdk::DragContext> const &/*ctx*/, int x, int y, guint time) {
        if (!_active) {
            _active = true;
            add_highlight();
            set_size(DROPZONE_SIZE + DROPZONE_EXPANSION);
        }
        return true;
    });

    signal_drag_leave().connect([=](Glib::RefPtr<Gdk::DragContext> const &/*ctx*/, guint time) {
        if (_active) {
            _active = false;
            set_size(DROPZONE_SIZE);
        }
    });

    _instances_list.push_back(this);
}

MyDropZone::~MyDropZone()
{
    auto const it = std::find(_instances_list.cbegin(), _instances_list.cend(), this);
    assert(it != _instances_list.cend());
    _instances_list.erase(it);
}

void MyDropZone::add_highlight_instances()
{
    for (auto *instance : _instances_list) {
        instance->add_highlight();
    }
}

void MyDropZone::remove_highlight_instances()
{
    for (auto *instance : _instances_list) {
        instance->remove_highlight();
    }
}

void MyDropZone::add_highlight()
{
    remove_css_class("backgnd-passive");
    add_css_class   ("backgnd-active" );
}

void MyDropZone::remove_highlight()
{
    remove_css_class("backgnd-active" );
    add_css_class   ("backgnd-passive");
}

void MyDropZone::set_size(int size)
{
    if (get_orientation() == Gtk::Orientation::HORIZONTAL) {
        set_size_request(size, -1);
    } else {
        set_size_request(-1, size);
    }
}

/* ============  MyHandle  ============ */

/**
 * Handles are event boxes that help with resizing DialogMultipaned' children.
 */
class MyHandle final
    : public Gtk::Orientable
    , public Gtk::Overlay
{
public:
    MyHandle(Gtk::Orientation orientation, int size);
    ~MyHandle() final = default;

    void set_dragging    (bool dragging);
    void set_drag_updated(bool updated );

private:
    void on_motion_enter (GtkEventControllerMotion const *motion,
                          double x, double y);
    void on_motion_motion(GtkEventControllerMotion const *motion,
                          double x, double y);
    void on_motion_leave (GtkEventControllerMotion const *motion);

    Gtk::EventSequenceState on_click_pressed (Gtk::GestureClick const &gesture,
                                              int n_press, double x, double y);
    Gtk::EventSequenceState on_click_released(Gtk::GestureClick const &gesture,
                                              int n_press, double x, double y);

    void toggle_multipaned();
    void update_click_indicator(double x, double y);
    void show_click_indicator(bool show);
    bool on_drawing_area_draw(Cairo::RefPtr<Cairo::Context> const &cr);
    Cairo::Rectangle get_active_click_zone();

    Gtk::DrawingArea * const _drawing_area;
    int _cross_size;
    Gtk::Widget *_child;
    void resize_handler(Gtk::Allocation &allocation);
    bool is_click_resize_active() const;
    bool _click = false;
    bool _click_indicator = false;

    bool _dragging = false;
    bool _drag_updated = false;
};

MyHandle::MyHandle(Gtk::Orientation orientation, int size = get_handle_size())
    : Glib::ObjectBase("MultipanedHandle")
    , Gtk::Orientable()
    , Gtk::Overlay{}
    , _drawing_area{Gtk::make_managed<Gtk::DrawingArea>()}
    , _cross_size(0)
    , _child(nullptr)
{
    set_name("MultipanedHandle");
    set_orientation(orientation);

    auto const image = Gtk::make_managed<Gtk::Image>();
    if (get_orientation() == Gtk::Orientation::HORIZONTAL) {
        image->set_from_icon_name("view-more-symbolic");
        set_size_request(size, -1);
    } else {
        image->set_from_icon_name("view-more-horizontal-symbolic");
        set_size_request(-1, size);
    }
    image->set_pixel_size(size);
    add(*image);

    _drawing_area->signal_draw().connect(sigc::mem_fun(*this, &MyHandle::on_drawing_area_draw));
    add_overlay(*_drawing_area);

    signal_size_allocate().connect(sigc::mem_fun(*this, &MyHandle::resize_handler));

    Controller::add_motion<&MyHandle::on_motion_enter ,
                           &MyHandle::on_motion_motion,
                           &MyHandle::on_motion_leave >
                          (*_drawing_area, *this, Gtk::PropagationPhase::TARGET);

    Controller::add_click(*_drawing_area,
                          sigc::mem_fun(*this, &MyHandle::on_click_pressed ),
                          sigc::mem_fun(*this, &MyHandle::on_click_released),
                          Controller::Button::any, Gtk::PropagationPhase::TARGET);
}

// draw rectangle with rounded corners
void rounded_rectangle(const Cairo::RefPtr<Cairo::Context>& cr, double x, double y, double w, double h, double r) {
    cr->begin_new_sub_path();
    cr->arc(x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cr->arc(x + w - r, y + r, r, 3 * M_PI / 2, 2 * M_PI);
    cr->arc(x + w - r, y + h - r, r, 0, M_PI / 2);
    cr->arc(x + r, y + h - r, r, M_PI / 2, M_PI);
    cr->close_path();
}

// part of the handle where clicking makes it automatically collapse/expand docked dialogs
Cairo::Rectangle MyHandle::get_active_click_zone() {
    const Gtk::Allocation& allocation = get_allocation();
    double width = allocation.get_width();
    double height = allocation.get_height();
    double h = height / 5;
    Cairo::Rectangle rect = { .x = 0, .y = (height - h) / 2, .width = width, .height = h };
    return rect;
}

bool MyHandle::on_drawing_area_draw(Cairo::RefPtr<Cairo::Context> const &cr)
{
    // show click indicator/highlight?
    if (_click_indicator && is_click_resize_active() && !_dragging) {
        auto rect = get_active_click_zone();
        if (rect.width > 4 && rect.height > 0) {
            auto const fg = get_color();
            rounded_rectangle(cr, rect.x + 2, rect.y, rect.width - 4, rect.height, 3);
            cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.26);
            cr->fill();
        }
    }
    return true;
}

void MyHandle::set_dragging(bool dragging) {
    if (_dragging != dragging) {
        _dragging = dragging;
        if (_click_indicator) {
            _drawing_area->queue_draw();
        }
    }
}

void MyHandle::set_drag_updated(bool const updated) {
    _drag_updated = updated;
}

/**
 * Change the mouse pointer into a resize icon to show you can drag.
 */
void MyHandle::on_motion_enter(GtkEventControllerMotion const * /*motion*/,
                               double const x, double const y)
{
    auto window = get_window();
    auto display = get_display();

    if (get_orientation() == Gtk::Orientation::HORIZONTAL) {
        auto cursor = Gdk::Cursor::create(display, "col-resize");
        window->set_cursor(cursor);
    } else {
        auto cursor = Gdk::Cursor::create(display, "row-resize");
        window->set_cursor(cursor);
    }

    update_click_indicator(x, y);
}

void MyHandle::on_motion_leave(GtkEventControllerMotion const * /*motion*/)
{
    get_window()->set_cursor({});
    show_click_indicator(false);
}

void MyHandle::show_click_indicator(bool show) {
    if (!is_click_resize_active()) return;

    if (show != _click_indicator) {
        _click_indicator = show;
        _drawing_area->queue_draw();
    }
}

void MyHandle::update_click_indicator(double x, double y) {
    if (!is_click_resize_active()) return;

    auto rect = get_active_click_zone();
    bool inside =
        x >= rect.x && x < rect.x + rect.width &&
        y >= rect.y && y < rect.y + rect.height;

    show_click_indicator(inside);
}

bool MyHandle::is_click_resize_active() const {
    return get_orientation() == Gtk::Orientation::HORIZONTAL;
}

Gtk::EventSequenceState MyHandle::on_click_pressed(Gtk::GestureClick const &gesture,
                                                   int /*n_press*/, double /*x*/, double /*y*/)
{
    // Detect single-clicks, except after a (moving/updated) drag
    _click = !_drag_updated && gesture.get_current_button() == 1;
    set_drag_updated(false);
    return Gtk::EventSequenceState::NONE;
}

Gtk::EventSequenceState MyHandle::on_click_released(Gtk::GestureClick const &gesture,
                                                    int /*n_press*/, double /*x*/, double /*y*/)
{
    // single-click on active zone?
    if (_click && gesture.get_current_button() == 1 && _click_indicator) {
        _click = false;
        _dragging = false;
        // handle clicked
        if (is_click_resize_active()) {
            toggle_multipaned();
            return Gtk::EventSequenceState::CLAIMED;
        }
    }

    _click = false;
    return Gtk::EventSequenceState::NONE;
}

void MyHandle::toggle_multipaned() {
    // visibility toggle of multipaned in a floating dialog window doesn't make sense; skip
    if (dynamic_cast<DialogWindow*>(get_root())) return;

    auto panel = dynamic_cast<DialogMultipaned*>(get_parent());
    if (!panel) return;

    auto const &children = panel->get_multipaned_children();
    Gtk::Widget* multi = nullptr; // multipaned widget to toggle
    bool left_side = true; // panels to the left of canvas
    size_t i = 0;

    // find multipaned widget to resize; it is adjacent (sibling) to 'this' handle in widget hierarchy
    for (auto widget : children) {
        if (dynamic_cast<Inkscape::UI::Widget::CanvasGrid*>(widget)) {
            // widget past canvas are on the right side (of canvas)
            left_side = false;
        }

        if (widget == this) {
            if (left_side && i > 0) {
                // handle to the left of canvas toggles preceeding panel
                multi = dynamic_cast<DialogMultipaned*>(children[i - 1]);
            }
            else if (!left_side && i + 1 < children.size()) {
                // handle to the right of canvas toggles next panel
                multi = dynamic_cast<DialogMultipaned*>(children[i + 1]);
            }

            if (multi) {
                if (multi->is_visible()) {
                    multi->set_visible(false);
                }
                else {
                    multi->set_visible(true);
                }
                // resize parent
                panel->children_toggled();
            }
            break;
        }

        ++i;
    }
}

void MyHandle::on_motion_motion(GtkEventControllerMotion const * /*motion*/,
                                double const x, double const y)
{
    // motion invalidates click; it activates resizing
    _click = false;
    update_click_indicator(x, y);
}

/**
 * This allocation handler function is used to add/remove handle icons in order to be able
 * to hide completely a transversal handle into the sides of a DialogMultipaned.
 *
 * The image has a specific size set up in the constructor and will not naturally shrink/hide.
 * In conclusion, we remove it from the handle and save it into an internal reference.
 */
void MyHandle::resize_handler(Gtk::Allocation &allocation)
{
    int size = (get_orientation() == Gtk::Orientation::HORIZONTAL) ? allocation.get_height() : allocation.get_width();

    if (_cross_size > size && HANDLE_CROSS_SIZE > size && !_child) {
        _child = get_child();
        remove();
    } else if (_cross_size < size && HANDLE_CROSS_SIZE < size && _child) {
        add(*_child);
        _child = nullptr;
    }

    _cross_size = size;
}

/* ============ DialogMultipaned ============= */

DialogMultipaned::DialogMultipaned(Gtk::Orientation orientation)
    : Glib::ObjectBase("DialogMultipaned")
    , Gtk::Orientable()
    , Gtk::Container()
    , _empty_widget(nullptr)
{
    set_name("DialogMultipaned");
    set_orientation(orientation);
    set_has_window(false);
    set_redraw_on_allocate(false);

    // ============= Add dropzones ==============
    auto const dropzone_s = Gtk::make_managed<MyDropZone>(orientation);
    auto const dropzone_e = Gtk::make_managed<MyDropZone>(orientation);
    dropzone_s->set_parent(*this);
    dropzone_e->set_parent(*this);
    children.push_back(dropzone_s);
    children.push_back(dropzone_e);

    // ============ Connect signals =============
    Controller::add_drag(*this,
        sigc::mem_fun(*this, &DialogMultipaned::on_drag_begin ),
        sigc::mem_fun(*this, &DialogMultipaned::on_drag_update),
        sigc::mem_fun(*this, &DialogMultipaned::on_drag_end   ),
        Gtk::PropagationPhase::CAPTURE);
    _connections.emplace_back(
        signal_drag_data_received().connect(sigc::mem_fun(*this, &DialogMultipaned::on_drag_data)));
    _connections.emplace_back(
        dropzone_s->signal_drag_data_received().connect(sigc::mem_fun(*this, &DialogMultipaned::on_prepend_drag_data)));
    _connections.emplace_back(
        dropzone_e->signal_drag_data_received().connect(sigc::mem_fun(*this, &DialogMultipaned::on_append_drag_data)));

    // add empty widget to initiate the container
    add_empty_widget();

    set_visible(true);
}

DialogMultipaned::~DialogMultipaned()
{
    for (;;) {
        auto it = std::find_if(children.begin(), children.end(), [](auto w) {
            return dynamic_cast<DialogMultipaned *>(w) || dynamic_cast<DialogNotebook *>(w);
        });
        if (it != children.end()) {
            // delete dialog multipanel or notebook; this action results in its removal from 'children'!
            delete *it;
        } else {
            // no more dialog panels
            break;
        }
    }

    // need to remove CanvasGrid from this container to avoid on idle repainting and crash:
    //   Gtk:ERROR:../gtk/gtkwidget.c:5871:gtk_widget_get_frame_clock: assertion failed: (window != NULL)
    //   Bail out! Gtk:ERROR:../gtk/gtkwidget.c:5871:gtk_widget_get_frame_clock: assertion failed: (window != NULL)
    for (auto child : children) {
        if (dynamic_cast<Inkscape::UI::Widget::CanvasGrid*>(child)) {
            remove(*child);
            break;
        }
    }
}

void DialogMultipaned::insert(int const pos, Gtk::Widget &child)
{
    auto const parent = child.get_parent();
    g_assert(!parent || parent == this);

    // Zero/positive pos means insert @children[pos]. Negative means @children[children.size()-pos]
    // We update children, so we must get iterator anew each time it is to be used. Check bound too
    g_assert(pos >= 0 &&  pos <= children.size() || // (prepending) inserting at 1-past-end is A-OK
             pos <  0 && -pos <= children.size());  // (appending) inserting@ 1-before-begin is NOT
    auto const get_iter = [&]{ return (pos >= 0 ? children.begin() : children.end()) + pos; };

    remove_empty_widget(); // Will remove extra widget if existing

    // If there are MyMultipane children that are empty, they will be removed
    for (auto const &child1 : children) {
        DialogMultipaned *paned = dynamic_cast<DialogMultipaned *>(child1);
        if (paned && paned->has_empty_widget()) {
            remove(*child1);
            remove_empty_widget();
        }
    }

    // Add handle
    if (children.size() > 2) {
        auto const my_handle = Gtk::make_managed<MyHandle>(get_orientation());
        my_handle->set_parent(*this);
        children.insert(get_iter(), my_handle);
    }

    // Add child
    children.insert(get_iter(), &child);
    if (!parent) {
        child.set_parent(*this);
    }
}

void DialogMultipaned::prepend(Gtk::Widget &child)
{
    insert(+1, child); // After start dropzone
}

void DialogMultipaned::append(Gtk::Widget &child)
{
    insert(-1, child); // Before end dropzone
}

void DialogMultipaned::add_empty_widget()
{
    const int EMPTY_WIDGET_SIZE = 60; // magic number

    // The empty widget is a label
    auto const label = Gtk::make_managed<Gtk::Label>(_("You can drop dockable dialogs here."));
    label->set_line_wrap();
    label->set_justify(Gtk::Justification::CENTER);
    label->set_valign(Gtk::Align::CENTER);
    label->set_vexpand();

    append(*label);
    _empty_widget = label;

    if (get_orientation() == Gtk::Orientation::VERTICAL) {
        int dropzone_size = (get_height() - EMPTY_WIDGET_SIZE) / 2;
        if (dropzone_size > DROPZONE_SIZE) {
            set_dropzone_sizes(dropzone_size, dropzone_size);
        }
    }
}

void DialogMultipaned::remove_empty_widget()
{
    if (_empty_widget) {
        auto it = std::find(children.begin(), children.end(), _empty_widget);
        if (it != children.end()) {
            children.erase(it);
        }
        _empty_widget->unparent();
        _empty_widget = nullptr;
    }

    if (get_orientation() == Gtk::Orientation::VERTICAL) {
        set_dropzone_sizes(DROPZONE_SIZE, DROPZONE_SIZE);
    }
}

Gtk::Widget *DialogMultipaned::get_first_widget()
{
    if (children.size() > 2) {
        return children[1];
    } else {
        return nullptr;
    }
}

Gtk::Widget *DialogMultipaned::get_last_widget()
{
    if (children.size() > 2) {
        return children[children.size() - 2];
    } else {
        return nullptr;
    }
}

/**
 * Set the sizes of the DialogMultipaned dropzones.
 * @param start, the size you want or -1 for the default `DROPZONE_SIZE`
 * @param end, the size you want or -1 for the default `DROPZONE_SIZE`
 */
void DialogMultipaned::set_dropzone_sizes(int start, int end)
{
    bool orientation = get_orientation() == Gtk::Orientation::HORIZONTAL;

    if (start == -1) {
        start = DROPZONE_SIZE;
    }

    MyDropZone *dropzone_s = dynamic_cast<MyDropZone *>(children[0]);

    if (dropzone_s) {
        if (orientation) {
            dropzone_s->set_size_request(start, -1);
        } else {
            dropzone_s->set_size_request(-1, start);
        }
    }

    if (end == -1) {
        end = DROPZONE_SIZE;
    }

    MyDropZone *dropzone_e = dynamic_cast<MyDropZone *>(children[children.size() - 1]);

    if (dropzone_e) {
        if (orientation) {
            dropzone_e->set_size_request(end, -1);
        } else {
            dropzone_e->set_size_request(-1, end);
        }
    }
}

/**
 * Show/hide as requested all children of this container that are of type multipaned
 */
void DialogMultipaned::toggle_multipaned_children(bool show)
{
    _handle = -1;
    _drag_handle = -1;

    for (auto child : children) {
        if (auto panel = dynamic_cast<DialogMultipaned*>(child)) {
            if (show) {
                panel->set_visible(true);
            }
            else {
                panel->set_visible(false);
            }
        }
    }
}

/**
 * Ensure that this dialog container is visible.
 */
void DialogMultipaned::ensure_multipaned_children()
{
    toggle_multipaned_children(true);
    // hide_multipaned = false;
    // queue_allocate();
}

// ****************** OVERRIDES ******************

// The following functions are here to define the behavior of our custom container

Gtk::SizeRequestMode DialogMultipaned::get_request_mode_vfunc() const
{
    if (get_orientation() == Gtk::Orientation::HORIZONTAL) {
        return Gtk::SIZE_REQUEST_WIDTH_FOR_HEIGHT;
    } else {
        return Gtk::SIZE_REQUEST_HEIGHT_FOR_WIDTH;
    }
}

void DialogMultipaned::get_preferred_width_vfunc(int &minimum_width, int &natural_width) const
{
    minimum_width = 0;
    natural_width = 0;
    for (auto const &child : children) {
        if (child && child->is_visible()) {
            int child_minimum_width = 0;
            int child_natural_width = 0;
            child->get_preferred_width(child_minimum_width, child_natural_width);
            if (get_orientation() == Gtk::Orientation::VERTICAL) {
                minimum_width = std::max(minimum_width, child_minimum_width);
                natural_width = std::max(natural_width, child_natural_width);
            } else {
                minimum_width += child_minimum_width;
                natural_width += child_natural_width;
            }
        }
    }
    if (_natural_width > natural_width) {
        natural_width = _natural_width;
    }
}

void DialogMultipaned::get_preferred_height_vfunc(int &minimum_height, int &natural_height) const
{
    minimum_height = 0;
    natural_height = 0;
    for (auto const &child : children) {
        if (child && child->is_visible()) {
            int child_minimum_height = 0;
            int child_natural_height = 0;
            child->get_preferred_height(child_minimum_height, child_natural_height);
            if (get_orientation() == Gtk::Orientation::HORIZONTAL) {
                minimum_height = std::max(minimum_height, child_minimum_height);
                natural_height = std::max(natural_height, child_natural_height);
            } else {
                minimum_height += child_minimum_height;
                natural_height += child_natural_height;
            }
        }
    }
}

void DialogMultipaned::get_preferred_width_for_height_vfunc(int height, int &minimum_width, int &natural_width) const
{
    minimum_width = 0;
    natural_width = 0;
    for (auto const &child : children) {
        if (child && child->is_visible()) {
            int child_minimum_width = 0;
            int child_natural_width = 0;
            child->get_preferred_width_for_height(height, child_minimum_width, child_natural_width);
            if (get_orientation() == Gtk::Orientation::VERTICAL) {
                minimum_width = std::max(minimum_width, child_minimum_width);
                natural_width = std::max(natural_width, child_natural_width);
            } else {
                minimum_width += child_minimum_width;
                natural_width += child_natural_width;
            }
        }
    }

    if (_natural_width > natural_width) {
        natural_width = _natural_width;
    }
}

void DialogMultipaned::get_preferred_height_for_width_vfunc(int width, int &minimum_height, int &natural_height) const
{
    minimum_height = 0;
    natural_height = 0;
    for (auto const &child : children) {
        if (child && child->is_visible()) {
            int child_minimum_height = 0;
            int child_natural_height = 0;
            child->get_preferred_height_for_width(width, child_minimum_height, child_natural_height);
            if (get_orientation() == Gtk::Orientation::HORIZONTAL) {
                minimum_height = std::max(minimum_height, child_minimum_height);
                natural_height = std::max(natural_height, child_natural_height);
            } else {
                minimum_height += child_minimum_height;
                natural_height += child_natural_height;
            }
        }
    }
}

void DialogMultipaned::children_toggled() {
    _handle = -1;
    _drag_handle = -1;
    queue_allocate();
}

/**
 * This function allocates the sizes of the children widgets (be them internal or not) from
 * the container's allocated size.
 *
 * Natural width: The width the widget really wants.
 * Minimum width: The minimum width for a widget to be useful.
 * Minimum <= Natural.
 */
void DialogMultipaned::on_size_allocate(Gtk::Allocation &allocation)
{
    set_allocation(allocation);
    bool horizontal = get_orientation() == Gtk::Orientation::HORIZONTAL;

    if (_drag_handle != -1) { // Exchange allocation between the widgets on either side of moved handle
        // Allocation values calculated in on_drag_update();
        children[_drag_handle - 1]->size_allocate(allocation1);
        children[_drag_handle]->size_allocate(allocationh);
        children[_drag_handle + 1]->size_allocate(allocation2);
        _drag_handle = -1;
    }
    // initially widgets get created with a 1x1 size; ignore it and wait for the final resize
    else if (allocation.get_width() > 1 && allocation.get_height() > 1) {
        _natural_width = allocation.get_width();
    }

    std::vector<bool> expandables;              // Is child expandable?
    std::vector<int> sizes_minimums;            // Difference between allocated space and minimum space.
    std::vector<int> sizes_naturals;            // Difference between allocated space and natural space.
    std::vector<int> sizes_current;             // The current sizes along main axis
    int left = horizontal ? allocation.get_width() : allocation.get_height();

    int index = 0;
    bool force_resize = false;  // initially panels are not sized yet, so we will apply their natural sizes
    int canvas_index = -1;
    for (auto child : children) {
        bool visible = child->get_visible();

        if (dynamic_cast<Inkscape::UI::Widget::CanvasGrid*>(child)) {
            canvas_index = index;
        }

        expandables.push_back(child->compute_expand(get_orientation()));

        Gtk::Requisition req_minimum;
        Gtk::Requisition req_natural;
        child->get_preferred_size(req_minimum, req_natural);
        if (child == _resizing_widget1 || child == _resizing_widget2) {
            // ignore limits for widget being resized interactively and use their current size
            req_minimum.width = req_minimum.height = 0;
            auto alloc = child->get_allocation();
            req_natural.width = alloc.get_width();
            req_natural.height = alloc.get_height();
        }

        sizes_minimums.push_back(visible ? horizontal ? req_minimum.width : req_minimum.height : 0);
        sizes_naturals.push_back(visible ? horizontal ? req_natural.width : req_natural.height : 0);

        Gtk::Allocation child_allocation = child->get_allocation();
        int size = 0;
        if (visible) {
            if (dynamic_cast<MyHandle*>(child)) {
                // resizing handles should never be smaller than their min size:
                size = horizontal ? req_minimum.width : req_minimum.height;
            }
            else {
                // all other widgets can get smaller than their min size
                size = horizontal ? child_allocation.get_width() : child_allocation.get_height();
                auto min = horizontal ? req_minimum.width : req_minimum.height;
                // enforce some minimum size, so newly inserted panels don't collapse to nothing
                if (size < min) size = std::min(20, min); // arbitrarily chosen 20px
            }
        }
        sizes_current.push_back(size);
        index++;

        if (sizes_current.back() < sizes_minimums.back()) force_resize = true;
    }

    std::vector<int> sizes = sizes_current; // The new allocation sizes

    const int sum_current = std::accumulate(sizes_current.begin(), sizes_current.end(), 0);
    {
        // Precalculate the minimum, natural and current totals
        const int sum_minimums = std::accumulate(sizes_minimums.begin(), sizes_minimums.end(), 0);
        const int sum_naturals = std::accumulate(sizes_naturals.begin(), sizes_naturals.end(), 0);

        // initial resize requested?
        if (force_resize && sum_naturals <= left) {
            sizes = sizes_naturals;
            left -= sum_naturals;
        } else if (sum_minimums <= left && left < sum_current) {
            // requested size exeeds available space; try shrinking it by starting from the last element
            sizes = sizes_current;
            auto excess = sum_current - left;
            for (int i = static_cast<int>(sizes.size() - 1); excess > 0 && i >= 0; --i) {
                auto extra = sizes_current[i] - sizes_minimums[i];
                if (extra > 0) {
                    if (extra >= excess) {
                        // we are done, enough space found
                        sizes[i] -= excess;
                        excess = 0;
                    }
                    else {
                        // shrink as far as possible, then continue to the next panel
                        sizes[i] -= extra;
                        excess -= extra;
                    }
                }
            }

            if (excess > 0) {
                sizes = sizes_minimums;
                left -= sum_minimums;
            }
            else {
                left = 0;
            }
        }
        else {
            left = std::max(0, left - sum_current);
        }
    }

    if (canvas_index >= 0) { // give remaining space to canvas element
        sizes[canvas_index] += left;
    } else { // or, if in a sub-dialogmultipaned, give it to the last panel

        for (int i = static_cast<int>(children.size()) - 1; i >= 0; --i) {
            if (expandables[i]) {
                sizes[i] += left;
                break;
            }
        }
    }

    // Check if we actually need to change the sizes on the main axis
    left = horizontal ? allocation.get_width() : allocation.get_height();
    if (left == sum_current) {
        bool valid = true;
        for (size_t i = 0; i < children.size(); ++i) {
            valid = sizes_minimums[i] <= sizes_current[i] &&        // is it over the minimums?
                    (expandables[i] || sizes_current[i] <= sizes_naturals[i]); // but does it want to be expanded?
            if (!valid)
                break;
        }
        if (valid) {
            sizes = sizes_current; // The current sizes are good, don't change anything;
        }
    }

    // Set x and y values of allocations (widths should be correct).
    int current_x = allocation.get_x();
    int current_y = allocation.get_y();

    // Allocate
    for (size_t i = 0; i < children.size(); ++i) {
        Gtk::Allocation child_allocation = children[i]->get_allocation();
        child_allocation.set_x(current_x);
        child_allocation.set_y(current_y);

        int size = sizes[i];

        if (horizontal) {
            child_allocation.set_width(size);
            current_x += size;
            child_allocation.set_height(allocation.get_height());
        } else {
            child_allocation.set_height(size);
            current_y += size;
            child_allocation.set_width(allocation.get_width());
        }

        children[i]->size_allocate(child_allocation);
    }
}

void DialogMultipaned::forall_vfunc(gboolean, GtkCallback callback, gpointer callback_data)
{
    for (auto const &child : children) {
        if (child) {
            callback(child->gobj(), callback_data);
        }
    }
}

void DialogMultipaned::on_add(Gtk::Widget *child)
{
    g_assert(child);

    append(*child);
}

/**
 * Callback when a widget is removed from DialogMultipaned and executes the removal.
 * It does not remove handles or dropzones.
 */
void DialogMultipaned::on_remove(Gtk::Widget *child)
{
    g_assert(child);

    MyDropZone *dropzone = dynamic_cast<MyDropZone *>(child);
    if (dropzone) {
        return;
    }
    MyHandle *my_handle = dynamic_cast<MyHandle *>(child);
    if (my_handle) {
        return;
    }

    const bool visible = child->get_visible();
    if (children.size() > 2) {
        auto it = std::find(children.begin(), children.end(), child);
        if (it != children.end()) {         // child found
            if (it + 2 != children.end()) { // not last widget
                my_handle = dynamic_cast<MyHandle *>(*(it + 1));
                my_handle->unparent();
                child->unparent();
                children.erase(it, it + 2);
            } else {                        // last widget
                if (children.size() == 3) { // only widget
                    child->unparent();
                    children.erase(it);
                } else { // not only widget, delete preceding handle
                    my_handle = dynamic_cast<MyHandle *>(*(it - 1));
                    my_handle->unparent();
                    child->unparent();
                    children.erase(it - 1, it + 1);
                }
            }
        }
    }
    if (visible) {
        queue_resize();
    }

    if (children.size() == 2) {
        add_empty_widget();
        _empty_widget->set_size_request(300, -1);
        _signal_now_empty.emit();
    }
}

Gtk::EventSequenceState DialogMultipaned::on_drag_begin(Gtk::GestureDrag const & /*gesture*/,
                                                        double const start_x, double const start_y)
{
    _hide_widget1 = _hide_widget2 = nullptr;
    _resizing_widget1 = _resizing_widget2 = nullptr;
    // We clicked on handle.
    bool found = false;
    int child_number = 0;
    Gtk::Allocation allocation = get_allocation();
    for (auto const &child : children) {
        MyHandle *my_handle = dynamic_cast<MyHandle *>(child);
        if (my_handle) {
            Gtk::Allocation child_allocation = my_handle->get_allocation();

            // Did drag start in handle?
            int x = child_allocation.get_x() - allocation.get_x();
            int y = child_allocation.get_y() - allocation.get_y();
            if (x < start_x && start_x < x + child_allocation.get_width() && y < start_y &&
                start_y < y + child_allocation.get_height()) {
                found = true;
                my_handle->set_dragging(true);
                break;
            }
        }
        ++child_number;
    }

    if (!found) {
        return Gtk::EventSequenceState::DENIED;
    }

    if (child_number < 1 || child_number > (int)(children.size() - 2)) {
        std::cerr << "DialogMultipaned::on_drag_begin: Invalid child (" << child_number << "!!" << std::endl;
        return Gtk::EventSequenceState::DENIED;
    }

    // Save for use in on_drag_update().
    _handle = child_number;
    start_allocation1 = children[_handle - 1]->get_allocation();
    if (!children[_handle - 1]->is_visible()) {
        start_allocation1.set_width(0);
        start_allocation1.set_height(0);
    }
    start_allocationh = children[_handle]->get_allocation();
    start_allocation2 = children[_handle + 1]->get_allocation();
    if (!children[_handle + 1]->is_visible()) {
        start_allocation2.set_width(0);
        start_allocation2.set_height(0);
    }

    return Gtk::EventSequenceState::CLAIMED;
}

Gtk::EventSequenceState DialogMultipaned::on_drag_end(Gtk::GestureDrag const & /*gesture*/,
                                                      double const offset_x, double const offset_y)
{
    if (_handle >= 0 && _handle < children.size()) {
        if (auto my_handle = dynamic_cast<MyHandle*>(children[_handle])) {
            my_handle->set_dragging(false);
        }
    }

    _handle = -1;
    _drag_handle = -1;
    if (_hide_widget1) {
        _hide_widget1->set_visible(false);
    }
    if (_hide_widget2) {
        _hide_widget2->set_visible(false);
    }
    _hide_widget1 = nullptr;
    _hide_widget2 = nullptr;
    _resizing_widget1 = nullptr;
    _resizing_widget2 = nullptr;

    queue_allocate(); // reimpose limits if any were bent during interactive resizing

    return Gtk::EventSequenceState::DENIED;
}

// docking panels in application window can be collapsed (to left or right side) to make more
// room for canvas; this functionality is only meaningful in app window, not in floating dialogs
bool can_collapse(Gtk::Widget* widget, Gtk::Widget* handle) {
    // can only collapse DialogMultipaned widgets
    if (!widget || dynamic_cast<DialogMultipaned*>(widget) == nullptr) return false;

    // collapsing is not supported in floating dialogs
    if (dynamic_cast<DialogWindow*>(widget->get_root())) return false;

    auto parent = handle->get_parent();
    if (!parent) return false;

    // find where the resizing handle is in relation to canvas area: left or right side;
    // next, find where the panel is in relation to the handle: on its left or right
    bool left_side = true;
    bool left_handle = false;
    size_t panel_index = 0;
    size_t handle_index = 0;
    size_t i = 0;
    for (auto const child : UI::get_children(*parent)) {
        if (dynamic_cast<Inkscape::UI::Widget::CanvasGrid *>(child)) {
            left_side = false;
        } else if (child == handle) {
            left_handle = left_side;
            handle_index = i;
        } else if (child == widget) {
            panel_index = i;
        }
        ++i;
    }

    if (left_handle && panel_index < handle_index) {
        return true;
    }
    if (!left_handle && panel_index > handle_index) {
        return true;
    }

    return false;
}

// return minimum widget size; this fn works for hidden widgets too
int get_min_width(Gtk::Widget* widget) {
    bool hidden = !widget->is_visible();
    if (hidden) widget->set_visible(true);
    int minimum_size = 0;
    int natural_size = 0;
    widget->get_preferred_width(minimum_size, natural_size);
    if (hidden) widget->set_visible(false);
    return minimum_size;
}

// Different docking resizing activities use easing functions to speed up or slow down certain phases of resizing
// Below are two picewise linear functions used for that purpose

// easing function for revealing collapsed panels
double reveal_curve(double val, double size) {
    if (size > 0 && val <= size && val >= 0) {
        // slow start (resistance to opening) and then quick reveal
        auto x = val / size;
        auto pos = x;
        if (x <= 0.2) {
            pos = x * 0.25;
        }
        else {
            pos = x * 9.5 - 1.85;
            if (pos > 1) pos = 1;
        }
        return size * pos;
    }

    return val;
}

// easing function for collapsing panels
// note: factors for x dictate how fast resizing happens when moving mouse (with 1 being at the same speed);
// other constants are to make this fn produce values in 0..1 range and seamlessly connect three segments
double collapse_curve(double val, double size) {
    if (size > 0 && val <= size && val >= 0) {
        // slow start (resistance), short pause and then quick collapse
        auto x = val / size;
        auto pos = x;
        if (x < 0.5) {
            // fast collapsing
            pos = x * 10 - 5 + 0.92;
            if (pos < 0) {
                // panel collapsed
                pos = 0;
            }
        }
        else if (x < 0.6) {
            // pause
            pos = 0.2 * 0.6 + 0.8; // = 0.92;
        }
        else {
            // resistance to collapsing (move slow, x 0.2 decrease)
            pos = x * 0.2 + 0.8;
        }
        return size * pos;
    }

    return val;
}

Gtk::EventSequenceState DialogMultipaned::on_drag_update(Gtk::GestureDrag const & /*gesture*/,
                                                         double offset_x, double offset_y)
{
    if (_handle < 0) {
        return Gtk::EventSequenceState::NONE;
    }

    auto child1 = children[_handle - 1];
    auto child2 = children[_handle + 1];
    allocation1 = children[_handle - 1]->get_allocation();
    allocationh = children[_handle]->get_allocation();
    allocation2 = children[_handle + 1]->get_allocation();

    // HACK: The bias prevents erratic resizing when dragging the handle fast, outside the bounds of the app.
    const int BIAS = 1;

    auto const handle = dynamic_cast<MyHandle *>(children[_handle]);
    handle->set_drag_updated(true);

    if (get_orientation() == Gtk::Orientation::HORIZONTAL) {
        // function to resize panel
        auto resize_fn = [](Gtk::Widget* handle, Gtk::Widget* child, int start_width, double& offset_x) {
            int minimum_size = get_min_width(child);
            auto width = start_width + offset_x;
            bool resizing = false;
            Gtk::Widget* hide = nullptr;

            if (!child->is_visible() && can_collapse(child, handle)) {
                child->set_visible(true);
                resizing = true;
            }

            if (width < minimum_size) {
                if (can_collapse(child, handle)) {
                    resizing = true;
                    auto w = start_width == 0 ? reveal_curve(width, minimum_size) : collapse_curve(width, minimum_size);
                    offset_x = w - start_width;
                    // facilitate closing/opening panels: users don't have to drag handle all the
                    // way to collapse/expand a panel, they just need to move it fraction of the way;
                    // note: those thresholds correspond to the easing functions used
                    auto threshold = start_width == 0 ? minimum_size * 0.20 : minimum_size * 0.42;
                    hide = width <= threshold ? child : nullptr;
                }
                else {
                    offset_x = -(start_width - minimum_size) + BIAS;
                }
            }

            return std::make_pair(resizing, hide);
        };

        /*
        TODO NOTE:
        Resizing should ideally take into account all columns, not just adjacent ones (left and right here).
        Without it, expanding second collapsed column does not work, since first one may already have min width,
        and cannot be shrunk anymore. Instead it should be pushed out of the way (canvas should be shrunk).
        */

        // panel on the left
        auto action1 = resize_fn(handle, child1, start_allocation1.get_width(), offset_x);
        _resizing_widget1 = action1.first ? child1 : nullptr;
        _hide_widget1 = action1.second ? child1 : nullptr;

        // panel on the right (needs reversing offset_x, so it can use the same logic)
        offset_x = -offset_x;
        auto action2 = resize_fn(handle, child2, start_allocation2.get_width(), offset_x);
        _resizing_widget2 = action2.first ? child2 : nullptr;
        _hide_widget2 = action2.second ? child2 : nullptr;
        offset_x = -offset_x;

        // set new sizes; they may temporarily violate min size panel requirements
        // GTK is not happy about 0-size allocations
        allocation1.set_width(start_allocation1.get_width() + offset_x);
        allocationh.set_x(start_allocationh.get_x() + offset_x);
        allocation2.set_x(start_allocation2.get_x() + offset_x);
        allocation2.set_width(start_allocation2.get_width() - offset_x);
    } else {
        // nothing fancy about resizing in vertical direction; no panel collapsing happens here
        int minimum_size;
        int natural_size;
        children[_handle - 1]->get_preferred_height(minimum_size, natural_size);
        if (start_allocation1.get_height() + offset_y < minimum_size)
            offset_y = -(start_allocation1.get_height() - minimum_size) + BIAS;
        children[_handle + 1]->get_preferred_height(minimum_size, natural_size);
        if (start_allocation2.get_height() - offset_y < minimum_size)
            offset_y = start_allocation2.get_height() - minimum_size - BIAS;

        allocation1.set_height(start_allocation1.get_height() + offset_y);
        allocationh.set_y(start_allocationh.get_y() + offset_y);
        allocation2.set_y(start_allocation2.get_y() + offset_y);
        allocation2.set_height(start_allocation2.get_height() - offset_y);
    }

    _drag_handle = _handle;
    queue_allocate(); // Relayout DialogMultipaned content.

    return Gtk::EventSequenceState::NONE;
}

void DialogMultipaned::set_target_entries(const std::vector<Gtk::TargetEntry> &target_entries)
{
    auto &front = dynamic_cast<MyDropZone &>(*children.at(0) );
    auto &back  = dynamic_cast<MyDropZone &>(*children.back());

    drag_dest_set(target_entries);
    front.drag_dest_set(target_entries, Gtk::DEST_DEFAULT_ALL, Gdk::DragAction::MOVE);
    back .drag_dest_set(target_entries, Gtk::DEST_DEFAULT_ALL, Gdk::DragAction::MOVE);
}

void DialogMultipaned::on_drag_data(Glib::RefPtr<Gdk::DragContext> const &context, int x, int y,
                                    const Gtk::SelectionData &selection_data, guint info, guint time)
{
    _signal_prepend_drag_data.emit(selection_data);
}

void DialogMultipaned::on_prepend_drag_data(Glib::RefPtr<Gdk::DragContext> const &context, int x, int y,
                                            const Gtk::SelectionData &selection_data, guint info, guint time)
{
    _signal_prepend_drag_data.emit(selection_data);
}

void DialogMultipaned::on_append_drag_data(Glib::RefPtr<Gdk::DragContext> const &context, int x, int y,
                                           const Gtk::SelectionData &selection_data, guint info, guint time)
{
    _signal_append_drag_data.emit(selection_data);
}

// Signals
sigc::signal<void (Gtk::SelectionData const &)> DialogMultipaned::signal_prepend_drag_data()
{
    resize_widget_children(this);
    return _signal_prepend_drag_data;
}

sigc::signal<void (Gtk::SelectionData const &)> DialogMultipaned::signal_append_drag_data()
{
    resize_widget_children(this);
    return _signal_append_drag_data;
}

sigc::signal<void ()> DialogMultipaned::signal_now_empty()
{
    return _signal_now_empty;
}

void DialogMultipaned::set_restored_width(int width) {
    _natural_width = width;
}

void DialogMultipaned::add_drop_zone_highlight_instances()
{
    MyDropZone::add_highlight_instances();
}

void DialogMultipaned::remove_drop_zone_highlight_instances()
{
    MyDropZone::remove_highlight_instances();
}

} // namespace Inkscape::UI::Dialog

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
