// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Selector aux toolbar
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Copyright (C) 2003-2005 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "select-toolbar.h"

#include <2geom/rect.h>
#include <glibmm/i18n.h>
#include <glibmm/main.h>

#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/enums.h>
#include <gtkmm/image.h>
#include <gtkmm/togglebutton.h>

#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "message-stack.h"
#include "object/sp-item-transform.h"
#include "object/sp-namedview.h"
#include "page-manager.h"
#include "selection.h"
#include "ui/builder-utils.h"
#include "ui/icon-names.h"
#include "ui/widget/canvas.h" // Focus widget
#include "ui/widget/combo-tool-item.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/toolbar-menu-button.h"
#include "ui/widget/unit-tracker.h"
#include "widgets/widget-sizes.h"

using Inkscape::UI::Widget::UnitTracker;
using Inkscape::Util::Unit;
using Inkscape::Util::Quantity;
using Inkscape::DocumentUndo;

namespace Inkscape::UI::Toolbar {

SelectToolbar::SelectToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    , _tracker(new UnitTracker(Inkscape::Util::UNIT_TYPE_LINEAR))
    , _update(false)
    , _action_prefix("selector:toolbar:")
    , _builder(create_builder("toolbar-select.ui"))
    , _select_touch_btn(get_widget<Gtk::ToggleButton>(_builder, "_select_touch_btn"))
    , _transform_stroke_btn(get_widget<Gtk::ToggleButton>(_builder, "_transform_stroke_btn"))
    , _transform_corners_btn(get_widget<Gtk::ToggleButton>(_builder, "_transform_corners_btn"))
    , _transform_gradient_btn(get_widget<Gtk::ToggleButton>(_builder, "_transform_gradient_btn"))
    , _transform_pattern_btn(get_widget<Gtk::ToggleButton>(_builder, "_transform_pattern_btn"))
    , _x_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_x_item"))
    , _y_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_y_item"))
    , _w_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_w_item"))
    , _h_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_h_item"))
    , _lock_btn(get_widget<Gtk::ToggleButton>(_builder, "_lock_btn"))
{
    auto prefs = Inkscape::Preferences::get();

    _toolbar = &get_widget<Gtk::Box>(_builder, "select-toolbar");

    setup_derived_spin_button(_x_item, "X");
    setup_derived_spin_button(_y_item, "Y");
    setup_derived_spin_button(_w_item, "width");
    setup_derived_spin_button(_h_item, "height");

    auto unit_menu = _tracker->create_tool_item(_("Units"), (""));
    get_widget<Gtk::Box>(_builder, "unit_menu_box").add(*unit_menu);

    // Fetch all the ToolbarMenuButtons at once from the UI file
    // Menu Button #1
    auto popover_box1 = &get_widget<Gtk::Box>(_builder, "popover_box1");
    auto menu_btn1 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn1");

    // Menu Button #2
    auto popover_box2 = &get_widget<Gtk::Box>(_builder, "popover_box2");
    auto menu_btn2 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn2");

    // Initialize all the ToolbarMenuButtons only after all the children of the
    // toolbar have been fetched. Otherwise, the children to be moved in the
    // popover will get mapped to a different position and it will probably
    // cause segfault.
    auto children = _toolbar->get_children();

    menu_btn1->init(1, "tag1", popover_box1, children);
    addCollapsibleButton(menu_btn1);
    menu_btn2->init(2, "tag2", popover_box2, children);
    addCollapsibleButton(menu_btn2);

    add(*_toolbar);

    _select_touch_btn.set_active(prefs->getBool("/tools/select/touch_box", false));
    _select_touch_btn.signal_toggled().connect(sigc::mem_fun(*this, &SelectToolbar::toggle_touch));

    _tracker->addUnit(Util::UnitTable::get().getUnit("%"));
    _tracker->setActiveUnit(desktop->getNamedView()->display_units);

    // Use StyleContext to check if the child is a context item.
    for (auto child : _toolbar->get_children()) {
        bool const is_context_item = child->has_css_class("context_item");
        if (is_context_item) {
            _context_items.push_back(child);
        }
    }

    _transform_stroke_btn.set_active(prefs->getBool("/options/transform/stroke", true));
    _transform_stroke_btn.signal_toggled().connect(sigc::mem_fun(*this, &SelectToolbar::toggle_stroke));

    _transform_corners_btn.set_active(prefs->getBool("/options/transform/rectcorners", true));
    _transform_corners_btn.signal_toggled().connect(sigc::mem_fun(*this, &SelectToolbar::toggle_corners));

    _transform_gradient_btn.set_active(prefs->getBool("/options/transform/gradient", true));
    _transform_gradient_btn.signal_toggled().connect(sigc::mem_fun(*this, &SelectToolbar::toggle_gradient));

    _transform_pattern_btn.set_active(prefs->getBool("/options/transform/pattern", true));
    _transform_pattern_btn.signal_toggled().connect(sigc::mem_fun(*this, &SelectToolbar::toggle_pattern));

    _lock_btn.signal_toggled().connect(sigc::mem_fun(*this, &SelectToolbar::toggle_lock));

    assert(desktop);
    auto *selection = desktop->getSelection();

    // Force update when selection changes.
    _connections.emplace_back( //
        selection->connectModified(sigc::mem_fun(*this, &SelectToolbar::on_inkscape_selection_modified)));
    _connections.emplace_back(
        selection->connectChanged(sigc::mem_fun(*this, &SelectToolbar::on_inkscape_selection_changed)));

    // Update now.
    layout_widget_update(selection);

    // Set context items insensitive.
    _connections.emplace_back(Glib::signal_idle().connect(
        [this] {
            for (auto item : _context_items) {
                if (item->is_sensitive()) {
                    item->set_sensitive(false);
                }
            }

            return false;
        },
        Glib::PRIORITY_HIGH));

    show_all();
}

SelectToolbar::~SelectToolbar() = default;

void SelectToolbar::on_unrealize()
{
    _connections.clear();

    parent_type::on_unrealize();
}

void SelectToolbar::setup_derived_spin_button(Inkscape::UI::Widget::SpinButton &btn, Glib::ustring const &name)
{
    const Glib::ustring path = "/tools/select/" + name;
    auto const val = Preferences::get()->getDouble(path, 0.0);
    auto adj = btn.get_adjustment();
    adj->set_value(val);
    adj->signal_value_changed().connect(sigc::bind(sigc::mem_fun(*this, &SelectToolbar::any_value_changed), adj));
    _tracker->addAdjustment(adj->gobj());

    btn.addUnitTracker(_tracker.get());
    btn.set_defocus_widget(_desktop->getCanvas());
}

void SelectToolbar::any_value_changed(Glib::RefPtr<Gtk::Adjustment> &adj)
{
    if (_update) {
        return;
    }

    if ( !_tracker || _tracker->isUpdating() ) {
        /*
         * When only units are being changed, don't treat changes
         * to adjuster values as object changes.
         */
        return;
    }
    _update = true;

    auto prefs = Inkscape::Preferences::get();
    auto selection = _desktop->getSelection();
    auto document = _desktop->getDocument();
    auto &pm = document->getPageManager();
    auto page = pm.getSelectedPageRect();
    auto page_correction = prefs->getBool("/options/origincorrection/page", true);

    document->ensureUpToDate();

    Geom::OptRect bbox_vis = selection->visualBounds();
    Geom::OptRect bbox_geom = selection->geometricBounds();
    Geom::OptRect bbox_user = selection->preferredBounds();

    if ( !bbox_user ) {
        _update = false;
        return;
    }

    Unit const *unit = _tracker->getActiveUnit();
    g_return_if_fail(unit != nullptr);

    gdouble old_w = bbox_user->dimensions()[Geom::X];
    gdouble old_h = bbox_user->dimensions()[Geom::Y];
    gdouble new_w, new_h, new_x, new_y = 0;

    auto _adj_x = _x_item.get_adjustment();
    auto _adj_y = _y_item.get_adjustment();
    auto _adj_w = _w_item.get_adjustment();
    auto _adj_h = _h_item.get_adjustment();

    if (unit->type == Inkscape::Util::UNIT_TYPE_LINEAR) {
        new_w = Quantity::convert(_adj_w->get_value(), unit, "px");
        new_h = Quantity::convert(_adj_h->get_value(), unit, "px");
        new_x = Quantity::convert(_adj_x->get_value(), unit, "px");
        new_y = Quantity::convert(_adj_y->get_value(), unit, "px");

    } else {
        gdouble old_x = bbox_user->min()[Geom::X] + (old_w * selection->anchor_x);
        gdouble old_y = bbox_user->min()[Geom::Y] + (old_h * selection->anchor_y);

        // Adjust against selected page, so later correction isn't broken.
        if (page_correction) {
            old_x -= page.left();
            old_y -= page.top();
        }

        new_x = old_x * (_adj_x->get_value() / 100 / unit->factor);
        new_y = old_y * (_adj_y->get_value() / 100 / unit->factor);
        new_w = old_w * (_adj_w->get_value() / 100 / unit->factor);
        new_h = old_h * (_adj_h->get_value() / 100 / unit->factor);
    }

    // Adjust depending on the selected anchor.
    gdouble x0 = (new_x - (old_w * selection->anchor_x)) - ((new_w - old_w) * selection->anchor_x);
    gdouble y0 = (new_y - (old_h * selection->anchor_y)) - ((new_h - old_h) * selection->anchor_y);

    // Adjust according to the selected page, if needed
    if (page_correction) {
        x0 += page.left();
        y0 += page.top();
    }

    gdouble x1 = x0 + new_w;
    gdouble xrel = new_w / old_w;
    gdouble y1 = y0 + new_h;
    gdouble yrel = new_h / old_h;

    // Keep proportions if lock is on
    if (_lock_btn.get_active()) {
        if (adj == _adj_h) {
            x1 = x0 + yrel * bbox_user->dimensions()[Geom::X];
        } else if (adj == _adj_w) {
            y1 = y0 + xrel * bbox_user->dimensions()[Geom::Y];
        }
    }

    // scales and moves, in px
    double mh = fabs(x0 - bbox_user->min()[Geom::X]);
    double sh = fabs(x1 - bbox_user->max()[Geom::X]);
    double mv = fabs(y0 - bbox_user->min()[Geom::Y]);
    double sv = fabs(y1 - bbox_user->max()[Geom::Y]);

    // unless the unit is %, convert the scales and moves to the unit
    if (unit->type == Inkscape::Util::UNIT_TYPE_LINEAR) {
        mh = Quantity::convert(mh, "px", unit);
        sh = Quantity::convert(sh, "px", unit);
        mv = Quantity::convert(mv, "px", unit);
        sv = Quantity::convert(sv, "px", unit);
    }

    char const *const actionkey = get_action_key(mh, sh, mv, sv);

    if (actionkey != nullptr) {

        bool transform_stroke = prefs->getBool("/options/transform/stroke", true);
        bool preserve = prefs->getBool("/options/preservetransform/value", false);

        Geom::Affine scaler;
        if (prefs->getInt("/tools/bounding_box") == 0) { // SPItem::VISUAL_BBOX
            scaler = get_scale_transform_for_variable_stroke (*bbox_vis, *bbox_geom, transform_stroke, preserve, x0, y0, x1, y1);
        } else {
            // 1) We could have use the newer get_scale_transform_for_variable_stroke() here, but to avoid regressions
            // we'll just use the old get_scale_transform_for_uniform_stroke() for now.
            // 2) get_scale_transform_for_uniform_stroke() is intended for visual bounding boxes, not geometrical ones!
            // we'll trick it into using a geometric bounding box though, by setting the stroke width to zero
            scaler = get_scale_transform_for_uniform_stroke (*bbox_geom, 0, 0, false, false, x0, y0, x1, y1);
        }

        selection->applyAffine(scaler);
        DocumentUndo::maybeDone(document, actionkey, _("Transform by toolbar"), INKSCAPE_ICON("tool-pointer"));
    }

    _update = false;
}

void SelectToolbar::layout_widget_update(Inkscape::Selection *sel)
{
    if (_update) {
        return;
    }

    _update = true;
    using Geom::X;
    using Geom::Y;
    if ( sel && !sel->isEmpty() ) {
        Geom::OptRect const bbox(sel->preferredBounds());
        if ( bbox ) {
            Unit const *unit = _tracker->getActiveUnit();
            g_return_if_fail(unit != nullptr);

            auto width = bbox->dimensions()[X];
            auto height = bbox->dimensions()[Y];
            auto x = bbox->min()[X] + (width * sel->anchor_x);
            auto y = bbox->min()[Y] + (height * sel->anchor_y);

            if (Preferences::get()->getBool("/options/origincorrection/page", true)) {
                auto &pm = _desktop->getDocument()->getPageManager();
                auto page = pm.getSelectedPageRect();
                x -= page.left();
                y -= page.top();
            }

            auto _adj_x = _x_item.get_adjustment();
            auto _adj_y = _y_item.get_adjustment();
            auto _adj_w = _w_item.get_adjustment();
            auto _adj_h = _h_item.get_adjustment();

            if (unit->type == Inkscape::Util::UNIT_TYPE_DIMENSIONLESS) {
                double const val = unit->factor * 100;
                _adj_x->set_value(val);
                _adj_y->set_value(val);
                _adj_w->set_value(val);
                _adj_h->set_value(val);
                _tracker->setFullVal( _adj_x->gobj(), x );
                _tracker->setFullVal( _adj_y->gobj(), y );
                _tracker->setFullVal( _adj_w->gobj(), width );
                _tracker->setFullVal( _adj_h->gobj(), height );
            } else {
                _adj_x->set_value(Quantity::convert(x, "px", unit));
                _adj_y->set_value(Quantity::convert(y, "px", unit));
                _adj_w->set_value(Quantity::convert(width, "px", unit));
                _adj_h->set_value(Quantity::convert(height, "px", unit));
            }
        }
    }

    _update = false;
}

void SelectToolbar::on_inkscape_selection_modified(Inkscape::Selection *selection, guint flags)
{
    assert(_desktop->getSelection() == selection);
    if ((flags & (SP_OBJECT_MODIFIED_FLAG        |
                     SP_OBJECT_PARENT_MODIFIED_FLAG |
                     SP_OBJECT_CHILD_MODIFIED_FLAG   )))
    {
        layout_widget_update(selection);
    }
}

void SelectToolbar::on_inkscape_selection_changed(Inkscape::Selection *selection)
{
    assert(_desktop->getSelection() == selection);
    {
        bool setActive = (selection && !selection->isEmpty());

        for (auto item : _context_items) {
            if ( setActive != item->get_sensitive() ) {
                item->set_sensitive(setActive);
            }
        }

        layout_widget_update(selection);
    }
}

char const *SelectToolbar::get_action_key(double mh, double sh, double mv, double sv)
{
    // do the action only if one of the scales/moves is greater than half the last significant
    // digit in the spinbox (currently spinboxes have 3 fractional digits, so that makes 0.0005). If
    // the value was changed by the user, the difference will be at least that much; otherwise it's
    // just rounding difference between the spinbox value and actual value, so no action is
    // performed
    double const threshold = 5e-4;
    char const *const action = ( mh > threshold ? "move:horizontal:" :
                                 sh > threshold ? "scale:horizontal:" :
                                 mv > threshold ? "move:vertical:" :
                                 sv > threshold ? "scale:vertical:" : nullptr );
    if (!action) {
        return nullptr;
    }
    _action_key = _action_prefix + action;
    return _action_key.c_str();
}

void SelectToolbar::toggle_lock()
{
    // use this roundabout way of changing image to make sure its size is preserved
    auto btn = static_cast<Gtk::ToggleButton *>(_lock_btn.get_child());
    auto image = static_cast<Gtk::Image *>(btn->get_child());
    if (!image) {
        g_warning("No GTK image in toolbar button 'lock'");
        return;
    }

    auto size = image->get_pixel_size();
    if (_lock_btn.get_active()) {
        image->set_from_icon_name("object-locked");
    } else {
        image->set_from_icon_name("object-unlocked");
    }
    image->set_pixel_size(size);
}

void SelectToolbar::toggle_touch()
{
    Preferences::get()->setBool("/tools/select/touch_box", _select_touch_btn.get_active());
}

void SelectToolbar::toggle_stroke()
{
    bool active = _transform_stroke_btn.get_active();
    Preferences::get()->setBool("/options/transform/stroke", active);
    if ( active ) {
        _desktop->messageStack()->flash(Inkscape::INFORMATION_MESSAGE, _("Now <b>stroke width</b> is <b>scaled</b> when objects are scaled."));
    } else {
        _desktop->messageStack()->flash(Inkscape::INFORMATION_MESSAGE, _("Now <b>stroke width</b> is <b>not scaled</b> when objects are scaled."));
    }
}

void SelectToolbar::toggle_corners()
{
    bool active = _transform_corners_btn.get_active();
    Preferences::get()->setBool("/options/transform/rectcorners", active);
    if ( active ) {
        _desktop->messageStack()->flash(Inkscape::INFORMATION_MESSAGE, _("Now <b>rounded rectangle corners</b> are <b>scaled</b> when rectangles are scaled."));
    } else {
        _desktop->messageStack()->flash(Inkscape::INFORMATION_MESSAGE, _("Now <b>rounded rectangle corners</b> are <b>not scaled</b> when rectangles are scaled."));
    }
}

void SelectToolbar::toggle_gradient()
{
    bool active = _transform_gradient_btn.get_active();
    Preferences::get()->setBool("/options/transform/gradient", active);
    if ( active ) {
        _desktop->messageStack()->flash(Inkscape::INFORMATION_MESSAGE, _("Now <b>gradients</b> are <b>transformed</b> along with their objects when those are transformed (moved, scaled, rotated, or skewed)."));
    } else {
        _desktop->messageStack()->flash(Inkscape::INFORMATION_MESSAGE, _("Now <b>gradients</b> remain <b>fixed</b> when objects are transformed (moved, scaled, rotated, or skewed)."));
    }
}

void SelectToolbar::toggle_pattern()
{
    bool active = _transform_pattern_btn.get_active();
    Preferences::get()->setInt("/options/transform/pattern", active);
    if ( active ) {
        _desktop->messageStack()->flash(Inkscape::INFORMATION_MESSAGE, _("Now <b>patterns</b> are <b>transformed</b> along with their objects when those are transformed (moved, scaled, rotated, or skewed)."));
    } else {
        _desktop->messageStack()->flash(Inkscape::INFORMATION_MESSAGE, _("Now <b>patterns</b> remain <b>fixed</b> when objects are transformed (moved, scaled, rotated, or skewed)."));
    }
}

} // namespace Inkscape::UI::Toolbar

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
