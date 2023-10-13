// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Inkscape::Widgets::LayerSelector - layer selector widget
 *
 * Authors:
 *   MenTaLguY <mental@rydia.net>
 *   Abhishek Sharma
 *
 * Copyright (C) 2004 MenTaLguY
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "layer-selector.h"

#include <string>
#include <glibmm/i18n.h>
#include <glibmm/ustring.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/image.h>
#include <gtkmm/stylecontext.h>
#include <sigc++/functors/mem_fun.h>

#include "desktop.h"
#include "document-undo.h"
#include "layer-manager.h"
#include "object/sp-item-group.h"
#include "ui/dialog/dialog-container.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/pack.h"
#include "ui/util.h"

namespace Inkscape::UI::Widget {

class AlternateIcons final : public Gtk::Box {
public:
    AlternateIcons(Gtk::BuiltinIconSize size, Glib::ustring const &a, Glib::ustring const &b)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL)
    {
        set_name("AlternateIcons");

        if (!a.empty()) {
            _a = Gtk::manage(sp_get_icon_image(a, size));
            _a->set_no_show_all(true);
            add(*_a);
        }

        if (!b.empty()) {
            _b = Gtk::manage(sp_get_icon_image(b, size));
            _b->set_no_show_all(true);
            add(*_b);
        }

        setState(false);
    }

    bool state() const { return _state; }
    void setState(bool state) {
        _state = state;

        if (_state) {
            if (_a) _a->set_visible(false);
            if (_b) _b->set_visible(true);
        } else {
            if (_a) _a->set_visible(true);
            if (_b) _b->set_visible(false);
        }
    }

private:
    Gtk::Image *_a = nullptr;
    Gtk::Image *_b = nullptr;
    bool _state    = false  ;
};

static constexpr auto cssName = "LayerSelector";

LayerSelector::LayerSelector(SPDesktop *desktop)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL)
    , _label_style{Gtk::CssProvider::create()}
    , _observer{std::make_unique<Inkscape::XML::SignalObserver>()}
{
    set_name(cssName);
    get_style_context()->add_class(getThisCssClass());

    _layer_name.signal_clicked().connect(sigc::mem_fun(*this, &LayerSelector::_layerChoose));
    _layer_name.set_relief(Gtk::RELIEF_NONE);
    _layer_name.set_tooltip_text(_("Current layer"));
    UI::pack_start(*this, _layer_name, UI::PackOptions::expand_widget);

    _eye_label = Gtk::make_managed<AlternateIcons>(Gtk::ICON_SIZE_MENU,
        INKSCAPE_ICON("object-visible"), INKSCAPE_ICON("object-hidden"));
    _eye_toggle.add(*_eye_label);
    _hide_layer_connection = _eye_toggle.signal_toggled().connect(sigc::mem_fun(*this, &LayerSelector::_hideLayer));

    _eye_toggle.set_relief(Gtk::RELIEF_NONE);
    _eye_toggle.set_tooltip_text(_("Toggle current layer visibility"));
    UI::pack_start(*this, _eye_toggle, UI::PackOptions::expand_padding);

    _lock_label = Gtk::make_managed<AlternateIcons>(Gtk::ICON_SIZE_MENU,
        INKSCAPE_ICON("object-unlocked"), INKSCAPE_ICON("object-locked"));
    _lock_toggle.add(*_lock_label);
    _lock_layer_connection = _lock_toggle.signal_toggled().connect(sigc::mem_fun(*this, &LayerSelector::_lockLayer));

    _lock_toggle.set_relief(Gtk::RELIEF_NONE);
    _lock_toggle.set_tooltip_text(_("Lock or unlock current layer"));
    UI::pack_start(*this, _lock_toggle, UI::PackOptions::expand_padding);

    _layer_name.add(_layer_label);
    _layer_label.set_max_width_chars(16);
    _layer_label.set_ellipsize(Pango::EllipsizeMode::END);
    _layer_label.set_markup("<i>Unset</i>");
    _layer_label.set_valign(Gtk::Align::CENTER);
    Gtk::StyleContext::add_provider_for_screen(_layer_label.get_screen(), _label_style,
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    _observer->signal_changed().connect(sigc::mem_fun(*this, &LayerSelector::_layerModified));
    setDesktop(desktop);
}

LayerSelector::~LayerSelector() {
    setDesktop(nullptr);
}

void LayerSelector::setDesktop(SPDesktop *desktop) {
    if ( desktop == _desktop )
        return;

    _layer_changed.disconnect();
    _desktop = desktop;

    if (_desktop) {
        _layer_changed = _desktop->layerManager().connectCurrentLayerChanged(sigc::mem_fun(*this, &LayerSelector::_layerChanged));
        _layerChanged(_desktop->layerManager().currentLayer());
    }
}

/**
 * Selects the given layer in the widget.
 */
void LayerSelector::_layerChanged(SPGroup *layer)
{
    _layer = layer;
    _observer->set(layer);
    _layerModified();
}

/**
 * If anything happens to the layer, refresh it.
 */
void LayerSelector::_layerModified()
{
    auto root = _desktop->layerManager().currentRoot();
    bool active = _layer && _layer != root;

    auto color_str = std::string("white");

    if (active) {
        _layer_label.set_text(_layer->defaultLabel());
        color_str = SPColor(_layer->highlight_color()).toString();
    } else {
        _layer_label.set_markup(_layer ? "<i>[root]</i>" : "<i>nothing</i>");
    }

    // Other border properties are set in share/ui/style.css
    _label_style->load_from_data(Glib::ustring::compose("#%1.%2 label { border-color: %3; }",
                                                        cssName, getThisCssClass(), color_str));

    _hide_layer_connection.block();
    _lock_layer_connection.block();
    _eye_toggle.set_sensitive(active);
    _lock_toggle.set_sensitive(active);
    _eye_label->setState(active && _layer->isHidden());
    _eye_toggle.set_active(active && _layer->isHidden());
    _lock_label->setState(active && _layer->isLocked());
    _lock_toggle.set_active(active && _layer->isLocked());
    _hide_layer_connection.unblock();
    _lock_layer_connection.unblock();
}

void LayerSelector::_lockLayer()
{
    bool lock = _lock_toggle.get_active();
    if (auto layer = _desktop->layerManager().currentLayer()) {
        layer->setLocked(lock);
        DocumentUndo::done(_desktop->getDocument(), lock ? _("Lock layer") : _("Unlock layer"), "");
    }
}

void LayerSelector::_hideLayer()
{
    bool hide = _eye_toggle.get_active();
    if (auto layer = _desktop->layerManager().currentLayer()) {
        layer->setHidden(hide);
        DocumentUndo::done(_desktop->getDocument(), hide ? _("Hide layer") : _("Unhide layer"), "");
    }
}

void LayerSelector::_layerChoose()
{
    _desktop->getContainer()->new_dialog("Objects");
}

Glib::ustring LayerSelector::getThisCssClass() const
{
    return "this" + Glib::ustring::format(this);
}

} // namespace Inkscape::UI::Widget

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
