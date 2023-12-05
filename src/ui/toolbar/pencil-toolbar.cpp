// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Pencil and pen toolbars
 */
/* Authors:
 *   MenTaLguY <mental@rydia.net>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Frank Felfe <innerspace@iname.com>
 *   John Cliff <simarilius@yahoo.com>
 *   David Turner <novalis@gnu.org>
 *   Josh Andler <scislac@scislac.com>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *   Tavmjong Bah <tavmjong@free.fr>
 *   Abhishek Sharma
 *   Kris De Gussem <Kris.DeGussem@gmail.com>
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Copyright (C) 2004 David Turner
 * Copyright (C) 2003 MenTaLguY
 * Copyright (C) 1999-2011 authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "pencil-toolbar.h"

#include <glibmm/i18n.h>
#include <gtkmm/togglebutton.h>

#include "desktop.h"
#include "display/curve.h"
#include "live_effects/lpe-bendpath.h"
#include "live_effects/lpe-bspline.h"
#include "live_effects/lpe-patternalongpath.h"
#include "live_effects/lpe-powerstroke.h"
#include "live_effects/lpe-simplify.h"
#include "live_effects/lpe-spiro.h"
#include "live_effects/lpeobject-reference.h"
#include "live_effects/lpeobject.h"
#include "object/sp-shape.h"
#include "selection.h"
#include "ui/builder-utils.h"
#include "ui/tools/freehand-base.h"
#include "ui/tools/pen-tool.h"
#include "ui/util.h"
#include "ui/widget/canvas.h"
#include "ui/widget/combo-tool-item.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/toolbar-menu-button.h"

namespace Inkscape {
namespace UI {
namespace Toolbar {

PencilToolbar::PencilToolbar(SPDesktop *desktop, bool pencil_mode)
    : Toolbar(desktop)
    , _tool_is_pencil(pencil_mode)
    , _builder(create_builder("toolbar-pencil.ui"))
    , _flatten_spiro_bspline_btn(get_widget<Gtk::Button>(_builder, "_flatten_spiro_bspline_btn"))
    , _usepressure_btn(get_widget<Gtk::ToggleButton>(_builder, "_usepressure_btn"))
    , _minpressure_box(get_widget<Gtk::Box>(_builder, "_minpressure_box"))
    , _minpressure_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_minpressure_item"))
    , _maxpressure_box(get_widget<Gtk::Box>(_builder, "_maxpressure_box"))
    , _maxpressure_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_maxpressure_item"))
    , _tolerance_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_tolerance_item"))
    , _simplify_btn(get_widget<Gtk::ToggleButton>(_builder, "_simplify_btn"))
    , _flatten_simplify_btn(get_widget<Gtk::Button>(_builder, "_flatten_simplify_btn"))
    , _shapescale_box(get_widget<Gtk::Box>(_builder, "_shapescale_box"))
    , _shapescale_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_shapescale_item"))
{
    _toolbar = &get_widget<Gtk::Box>(_builder, "pencil-toolbar");

    auto prefs = Preferences::get();

    // Configure mode buttons
    int btn_index = 0;
    for_each_child(get_widget<Gtk::Box>(_builder, "mode_buttons_box"), [&](Gtk::Widget &item){
        auto &btn = dynamic_cast<Gtk::ToggleButton &>(item);
        _mode_buttons.push_back(&btn);
        btn.signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &PencilToolbar::mode_changed), btn_index++));
        return ForEachResult::_continue;
    });

    // Configure LPE bspline spiro flatten button.
    _flatten_spiro_bspline_btn.signal_clicked().connect(sigc::mem_fun(*this, &PencilToolbar::flatten_spiro_bspline));

    int freehandMode = prefs->getInt(
        (_tool_is_pencil ? "/tools/freehand/pencil/freehand-mode" : "/tools/freehand/pen/freehand-mode"), 0);

    // freehandMode range is (0,5] for the pen tool, (0,3] for the pencil tool
    // freehandMode = 3 is an old way of signifying pressure, set it to 0.
    _mode_buttons[(freehandMode < _mode_buttons.size()) ? freehandMode : 0]->set_active();

    if (_tool_is_pencil) {
        // Setup the spin buttons.
        setup_derived_spin_button(_minpressure_item, "minpressure", 0, &PencilToolbar::minpressure_value_changed);
        setup_derived_spin_button(_maxpressure_item, "maxpressure", 30, &PencilToolbar::maxpressure_value_changed);
        setup_derived_spin_button(_tolerance_item, "tolerance", 3.0, &PencilToolbar::tolerance_value_changed);

        // Configure usepressure button.
        bool pressure = prefs->getBool("/tools/freehand/pencil/pressure", false);
        _usepressure_btn.set_active(pressure);
        _usepressure_btn.signal_toggled().connect(sigc::mem_fun(*this, &PencilToolbar::use_pencil_pressure));

        // Powerstoke combo item.
        add_powerstroke_cap();

        // Configure LPE simplify based tolerance button.
        _simplify_btn.set_active(prefs->getInt("/tools/freehand/pencil/simplify", 0));
        _simplify_btn.signal_toggled().connect(sigc::mem_fun(*this, &PencilToolbar::simplify_lpe));

        // Configure LPE simplify flatten button.
        _flatten_simplify_btn.signal_clicked().connect(sigc::mem_fun(*this, &PencilToolbar::simplify_flatten));
    }

    // Advanced shape options.
    add_shape_option();

    // Setup the spin buttons.
    setup_derived_spin_button(_shapescale_item, "shapescale", 2.0, &PencilToolbar::shapewidth_value_changed);

    // Fetch all the ToolbarMenuButtons at once from the UI file
    // Menu Button #1
    auto popover_box1 = &get_widget<Gtk::Box>(_builder, "popover_box1");
    auto menu_btn1 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn1");

    // Initialize all the ToolbarMenuButtons only after all the children of the
    // toolbar have been fetched. Otherwise, the children to be moved in the
    // popover will get mapped to a different position and it will probably
    // cause segfault.
    auto children = UI::get_children(*_toolbar);

    menu_btn1->init(1, "tag1", popover_box1, children);
    addCollapsibleButton(menu_btn1);

    set_child(*_toolbar);

    hide_extra_widgets();
}

void PencilToolbar::add_powerstroke_cap()
{
    // Powerstroke cap combo tool item.
    UI::Widget::ComboToolItemColumns columns;

    Glib::RefPtr<Gtk::ListStore> store = Gtk::ListStore::create(columns);

    std::vector<gchar *> powerstroke_cap_items_list = {const_cast<gchar *>(C_("Cap", "Butt")), _("Square"), _("Round"),
                                                       _("Peak"), _("Zero width")};
    for (auto item : powerstroke_cap_items_list) {
        Gtk::TreeModel::Row row = *(store->append());
        row[columns.col_label] = item;
        row[columns.col_sensitive] = true;
    }

    _cap_item = Gtk::manage(UI::Widget::ComboToolItem::create(
        _("Caps"), _("Line endings when drawing with pressure-sensitive PowerPencil"), "Not Used", store));

    auto prefs = Inkscape::Preferences::get();

    int cap = prefs->getInt("/live_effects/powerstroke/powerpencilcap", 2);
    _cap_item->set_active(cap);
    _cap_item->use_group_label(true);

    _cap_item->signal_changed().connect(sigc::mem_fun(*this, &PencilToolbar::change_cap));

    get_widget<Gtk::Box>(_builder, "powerstroke_cap_box").append(*_cap_item);
}

void PencilToolbar::add_shape_option()
{
    UI::Widget::ComboToolItemColumns columns;

    Glib::RefPtr<Gtk::ListStore> store = Gtk::ListStore::create(columns);

    std::vector<gchar *> freehand_shape_dropdown_items_list = {const_cast<gchar *>(C_("Freehand shape", "None")),
                                                               _("Triangle in"),
                                                               _("Triangle out"),
                                                               _("Ellipse"),
                                                               _("From clipboard"),
                                                               _("Bend from clipboard"),
                                                               _("Last applied")};

    for (auto item : freehand_shape_dropdown_items_list) {
        Gtk::TreeModel::Row row = *(store->append());
        row[columns.col_label] = item;
        row[columns.col_sensitive] = true;
    }

    _shape_item = Gtk::manage(
        UI::Widget::ComboToolItem::create(_("Shape"), _("Shape of new paths drawn by this tool"), "Not Used", store));
    _shape_item->use_group_label(true);

    int shape =
        Preferences::get()->getInt((_tool_is_pencil ? "/tools/freehand/pencil/shape" : "/tools/freehand/pen/shape"), 0);
    _shape_item->set_active(shape);

    _shape_item->signal_changed().connect(sigc::mem_fun(*this, &PencilToolbar::change_shape));
    get_widget<Gtk::Box>(_builder, "shape_box").append(*_shape_item);
}

void PencilToolbar::setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name,
                                              double default_value, ValueChangedMemFun const value_changed_mem_fun)
{
    auto prefs = Inkscape::Preferences::get();
    const Glib::ustring path = "/tools/freehand/pencil/" + name;
    auto const val = prefs->getDouble(path, default_value);

    auto adj = btn.get_adjustment();
    adj->set_value(val);
    adj->signal_value_changed().connect(sigc::mem_fun(*this, value_changed_mem_fun));

    if (name == "shapescale") {
        int shape = prefs->getInt((_tool_is_pencil ? "/tools/freehand/pencil/shape" : "/tools/freehand/pen/shape"), 0);
        update_width_value(shape);
    }

    btn.set_defocus_widget(_desktop->getCanvas());
}

void PencilToolbar::hide_extra_widgets()
{
    std::vector<Gtk::Widget *> pen_only_items;
    pen_only_items.push_back(&get_widget<Gtk::ToggleButton>(_builder, "zigzag_btn"));
    pen_only_items.push_back(&get_widget<Gtk::ToggleButton>(_builder, "paraxial_btn"));

    std::vector<Gtk::Widget *> pencil_only_items;
    pencil_only_items.push_back(&get_widget<Gtk::Box>(_builder, "pencil_only_box"));

    // Attach signals to handle the visibility of the widgets.
    for (auto child : pen_only_items) {
        child->set_visible(false);
        child->signal_show().connect(
            [=]() {
                if (_tool_is_pencil) {
                    child->set_visible(false);
                }
            },
            false);
    }

    for (auto child : pencil_only_items) {
        child->set_visible(false);
        child->signal_show().connect(
            [=]() {
                if (!_tool_is_pencil) {
                    child->set_visible(false);
                }
            },
            false);
    }

    // Elements must be hidden after being initially visible.
    int freehandMode = Preferences::get()->getInt(
        (_tool_is_pencil ? "/tools/freehand/pencil/freehand-mode" : "/tools/freehand/pen/freehand-mode"), 0);

    if (freehandMode != 1 && freehandMode != 2) {
        _flatten_spiro_bspline_btn.set_visible(false);
    }
    if (_tool_is_pencil) {
        use_pencil_pressure();
    }
}

PencilToolbar::~PencilToolbar()
{
    if(_repr) {
        GC::release(_repr);
        _repr = nullptr;
    }
}

void PencilToolbar::mode_changed(int mode)
{
    Preferences::get()->setInt(freehand_tool_name() + "/freehand-mode", mode);

    if (mode == 1 || mode == 2) {
        _flatten_spiro_bspline_btn.set_visible(true);
    } else {
        _flatten_spiro_bspline_btn.set_visible(false);
    }

    bool visible = (mode != 2);

    _simplify_btn.set_visible(visible);
    _flatten_simplify_btn.set_visible(visible && _simplify_btn.get_active());

    // Recall, the PencilToolbar is also used as the PenToolbar with minor changes.
    auto *pt = dynamic_cast<Inkscape::UI::Tools::PenTool *>(_desktop->getTool());
    if (pt) {
        pt->setPolylineMode();
    }
}

/* This is used in generic functions below to share large portions of code between pen and pencil tool */
Glib::ustring const PencilToolbar::freehand_tool_name()
{
    return _tool_is_pencil ? "/tools/freehand/pencil" : "/tools/freehand/pen";
}

void PencilToolbar::minpressure_value_changed()
{
    assert(_tool_is_pencil);
    // quit if run by the attr_changed listener
    if (_freeze) {
        return;
    }

    Preferences::get()->setDouble("/tools/freehand/pencil/minpressure",
                                  _minpressure_item.get_adjustment()->get_value());
}

void PencilToolbar::maxpressure_value_changed()
{
    assert(_tool_is_pencil);
    // quit if run by the attr_changed listener
    if (_freeze) {
        return;
    }

    Preferences::get()->setDouble("/tools/freehand/pencil/maxpressure",
                                  _maxpressure_item.get_adjustment()->get_value());
}

void PencilToolbar::shapewidth_value_changed()
{
    // quit if run by the attr_changed listener
    if (_freeze) {
        return;
    }

    auto prefs = Inkscape::Preferences::get();
    Inkscape::Selection *selection = _desktop->getSelection();
    SPItem *item = selection->singleItem();
    SPLPEItem *lpeitem = nullptr;
    if (item) {
        lpeitem = cast<SPLPEItem>(item);
    }
    using namespace Inkscape::LivePathEffect;
    double width = _shapescale_item.get_adjustment()->get_value();
    switch (_shape_item->get_active()) {
        case Inkscape::UI::Tools::TRIANGLE_IN:
        case Inkscape::UI::Tools::TRIANGLE_OUT:
            prefs->setDouble("/live_effects/powerstroke/width", width);
            if (lpeitem) {
                LPEPowerStroke *effect = dynamic_cast<LPEPowerStroke *>(lpeitem->getFirstPathEffectOfType(POWERSTROKE));
                if (effect) {
                    std::vector<Geom::Point> points = effect->offset_points.data();
                    if (points.size() == 1) {
                        points[0][Geom::Y] = width;
                        effect->offset_points.param_set_and_write_new_value(points);
                    }
                }
            }
            break;
        case Inkscape::UI::Tools::ELLIPSE:
        case Inkscape::UI::Tools::CLIPBOARD:
            // The scale of the clipboard isn't known, so getting it to the right size isn't possible.
            prefs->setDouble("/live_effects/skeletal/width", width);
            if (lpeitem) {
                LPEPatternAlongPath *effect =
                    dynamic_cast<LPEPatternAlongPath *>(lpeitem->getFirstPathEffectOfType(PATTERN_ALONG_PATH));
                if (effect) {
                    effect->prop_scale.param_set_value(width);
                    sp_lpe_item_update_patheffect(lpeitem, false, true);
                }
            }
            break;
        case Inkscape::UI::Tools::BEND_CLIPBOARD:
            prefs->setDouble("/live_effects/bend_path/width", width);
            if (lpeitem) {
                LPEBendPath *effect = dynamic_cast<LPEBendPath *>(lpeitem->getFirstPathEffectOfType(BEND_PATH));
                if (effect) {
                    effect->prop_scale.param_set_value(width);
                    sp_lpe_item_update_patheffect(lpeitem, false, true);
                }
            }
            break;
        case Inkscape::UI::Tools::NONE:
        case Inkscape::UI::Tools::LAST_APPLIED:
        default:
            break;
    }
}

void PencilToolbar::use_pencil_pressure()
{
    assert(_tool_is_pencil);
    bool pressure = _usepressure_btn.get_active();
    auto prefs = Inkscape::Preferences::get();
    prefs->setBool("/tools/freehand/pencil/pressure", pressure);
    if (pressure) {
        _minpressure_box.set_visible(true);
        _maxpressure_box.set_visible(true);
        _cap_item->set_visible(true);
        _shape_item->set_visible(false);
        _shapescale_box.set_visible(false);
        _simplify_btn.set_visible(false);
        _flatten_spiro_bspline_btn.set_visible(false);
        _flatten_simplify_btn.set_visible(false);
        for (auto button : _mode_buttons) {
            button->set_sensitive(false);
        }
    } else {
        int freehandMode = prefs->getInt("/tools/freehand/pencil/freehand-mode", 0);

        _minpressure_box.set_visible(false);
        _maxpressure_box.set_visible(false);
        _cap_item->set_visible(false);
        _shape_item->set_visible(true);
        _shapescale_box.set_visible(true);
        bool simplify_visible = freehandMode != 2;
        _simplify_btn.set_visible(simplify_visible);
        _flatten_simplify_btn.set_visible(simplify_visible && _simplify_btn.get_active());
        if (freehandMode == 1 || freehandMode == 2) {
            _flatten_spiro_bspline_btn.set_visible(true);
        }
        for (auto button : _mode_buttons) {
            button->set_sensitive(true);
        }
    }
}

void PencilToolbar::change_shape(int shape)
{
    Preferences::get()->setInt(freehand_tool_name() + "/shape", shape);
    update_width_value(shape);
}

void PencilToolbar::update_width_value(int shape)
{
    /* Update shape width with correct width */
    auto prefs = Inkscape::Preferences::get();
    double width = 1.0;
    _shapescale_item.set_sensitive(true);
    double powerstrokedefsize = 10 / (0.265 * _desktop->getDocument()->getDocumentScale()[0] * 2.0);
    switch (shape) {
        case Inkscape::UI::Tools::TRIANGLE_IN:
        case Inkscape::UI::Tools::TRIANGLE_OUT:
            width = prefs->getDouble("/live_effects/powerstroke/width", powerstrokedefsize);
            break;
        case Inkscape::UI::Tools::ELLIPSE:
        case Inkscape::UI::Tools::CLIPBOARD:
            width = prefs->getDouble("/live_effects/skeletal/width", 1.0);
            break;
        case Inkscape::UI::Tools::BEND_CLIPBOARD:
            width = prefs->getDouble("/live_effects/bend_path/width", 1.0);
            break;
        case Inkscape::UI::Tools::NONE: // Apply width from style?
        case Inkscape::UI::Tools::LAST_APPLIED:
        default:
            _shapescale_item.set_sensitive(false);
            break;
    }
    _shapescale_item.get_adjustment()->set_value(width);
}

void PencilToolbar::change_cap(int cap)
{
    Preferences::get()->setInt("/live_effects/powerstroke/powerpencilcap", cap);
}

void PencilToolbar::simplify_lpe()
{
    bool simplify = _simplify_btn.get_active();
    Preferences::get()->setBool(freehand_tool_name() + "/simplify", simplify);
    _flatten_simplify_btn.set_visible(simplify);
}

void PencilToolbar::simplify_flatten()
{
    auto selected = _desktop->getSelection()->items();
    SPLPEItem* lpeitem = nullptr;
    for (auto it(selected.begin()); it != selected.end(); ++it){
        lpeitem = cast<SPLPEItem>(*it);
        if (lpeitem && lpeitem->hasPathEffect()){
            PathEffectList lpelist = lpeitem->getEffectList();
            PathEffectList::iterator i;
            for (i = lpelist.begin(); i != lpelist.end(); ++i) {
                LivePathEffectObject *lpeobj = (*i)->lpeobject;
                if (lpeobj) {
                    Inkscape::LivePathEffect::Effect *lpe = lpeobj->get_lpe();
                    if (dynamic_cast<Inkscape::LivePathEffect::LPESimplify *>(lpe)) {
                        auto shape = cast<SPShape>(lpeitem);
                        if(shape){
                            auto c = *shape->curveForEdit();
                            lpe->doEffect(&c);
                            lpeitem->setCurrentPathEffect(*i);
                            if (lpelist.size() > 1){
                                lpeitem->removeCurrentPathEffect(true);
                                shape->setCurveBeforeLPE(std::move(c));
                            } else {
                                lpeitem->removeCurrentPathEffect(false);
                                shape->setCurve(std::move(c));
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    if (lpeitem) {
        _desktop->getSelection()->remove(lpeitem->getRepr());
        _desktop->getSelection()->add(lpeitem->getRepr());
        sp_lpe_item_update_patheffect(lpeitem, false, false);
    }
}

void PencilToolbar::flatten_spiro_bspline()
{
    auto selected = _desktop->getSelection()->items();
    SPLPEItem* lpeitem = nullptr;

    for (auto it(selected.begin()); it != selected.end(); ++it){
        lpeitem = cast<SPLPEItem>(*it);
        if (lpeitem && lpeitem->hasPathEffect()){
            PathEffectList lpelist = lpeitem->getEffectList();
            PathEffectList::iterator i;
            for (i = lpelist.begin(); i != lpelist.end(); ++i) {
                LivePathEffectObject *lpeobj = (*i)->lpeobject;
                if (lpeobj) {
                    Inkscape::LivePathEffect::Effect *lpe = lpeobj->get_lpe();
                    if (dynamic_cast<Inkscape::LivePathEffect::LPEBSpline *>(lpe) || 
                        dynamic_cast<Inkscape::LivePathEffect::LPESpiro *>(lpe)) 
                    {
                        auto shape = cast<SPShape>(lpeitem);
                        if(shape){
                            auto c = *shape->curveForEdit();
                            lpe->doEffect(&c);
                            lpeitem->setCurrentPathEffect(*i);
                            if (lpelist.size() > 1){
                                lpeitem->removeCurrentPathEffect(true);
                                shape->setCurveBeforeLPE(std::move(c));
                            } else {
                                lpeitem->removeCurrentPathEffect(false);
                                shape->setCurve(std::move(c));
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    if (lpeitem) {
        _desktop->getSelection()->remove(lpeitem->getRepr());
        _desktop->getSelection()->add(lpeitem->getRepr());
        sp_lpe_item_update_patheffect(lpeitem, false, false);
    }
}

void PencilToolbar::tolerance_value_changed()
{
    assert(_tool_is_pencil);
    // quit if run by the attr_changed listener
    if (_freeze) {
        return;
    }

    // in turn, prevent listener from responding
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    _freeze = true;
    prefs->setDouble("/tools/freehand/pencil/tolerance", _tolerance_item.get_adjustment()->get_value());
    _freeze = false;
    auto selected = _desktop->getSelection()->items();
    for (auto it(selected.begin()); it != selected.end(); ++it){
        auto lpeitem = cast<SPLPEItem>(*it);
        if (lpeitem && lpeitem->hasPathEffect()){
            Inkscape::LivePathEffect::Effect *simplify =
                lpeitem->getFirstPathEffectOfType(Inkscape::LivePathEffect::SIMPLIFY);
            if(simplify){
                Inkscape::LivePathEffect::LPESimplify *lpe_simplify = dynamic_cast<Inkscape::LivePathEffect::LPESimplify*>(simplify->getLPEObj()->get_lpe());
                if (lpe_simplify) {
                    double tol = prefs->getDoubleLimited("/tools/freehand/pencil/tolerance", 10.0, 1.0, 100.0);
                    tol = tol/(100.0*(102.0-tol));
                    std::ostringstream ss;
                    ss << tol;
                    Inkscape::LivePathEffect::Effect *powerstroke =
                        lpeitem->getFirstPathEffectOfType(Inkscape::LivePathEffect::POWERSTROKE);
                    bool simplified = false;
                    if(powerstroke){
                        Inkscape::LivePathEffect::LPEPowerStroke *lpe_powerstroke = dynamic_cast<Inkscape::LivePathEffect::LPEPowerStroke*>(powerstroke->getLPEObj()->get_lpe());
                        if(lpe_powerstroke){
                            lpe_powerstroke->getRepr()->setAttribute("is_visible", "false");
                            sp_lpe_item_update_patheffect(lpeitem, false, false);
                            auto sp_shape = cast<SPShape>(lpeitem);
                            if (sp_shape) {
                                guint previous_curve_length = sp_shape->curve()->get_segment_count();
                                lpe_simplify->getRepr()->setAttribute("threshold", ss.str());
                                sp_lpe_item_update_patheffect(lpeitem, false, false);
                                simplified = true;
                                guint curve_length = sp_shape->curve()->get_segment_count();
                                std::vector<Geom::Point> ts = lpe_powerstroke->offset_points.data();
                                double factor = (double)curve_length/ (double)previous_curve_length;
                                for (auto & t : ts) {
                                    t[Geom::X] = t[Geom::X] * factor;
                                }
                                lpe_powerstroke->offset_points.param_setValue(ts);
                            }
                            lpe_powerstroke->getRepr()->setAttribute("is_visible", "true");
                            sp_lpe_item_update_patheffect(lpeitem, false, false);
                        }
                    }
                    if(!simplified){
                        lpe_simplify->getRepr()->setAttribute("threshold", ss.str());
                    }
                }
            }
        }
    }
}

}
}
}
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
