// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) Johan Engelen 2012 <j.b.c.engelen@alumnus.utwente.nl>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "live_effects/parameter/originalsatellite.h"

#include <glibmm/i18n.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>

#include "desktop.h"
#include "display/curve.h"
#include "inkscape.h"
#include "live_effects/effect.h"
#include "live_effects/parameter/satellite-reference.h"
#include "object/uri.h"
#include "selection.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/pack.h"

namespace Inkscape {
namespace LivePathEffect {

OriginalSatelliteParam::OriginalSatelliteParam(const Glib::ustring &label, const Glib::ustring &tip,
                                               const Glib::ustring &key, Inkscape::UI::Widget::Registry *wr,
                                               Effect *effect)
    : SatelliteParam(label, tip, key, wr, effect)
{
}

Gtk::Widget *OriginalSatelliteParam::param_newWidget()
{
    auto const _widget = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);

    { // Label
        auto const pLabel = Gtk::make_managed<Gtk::Label>(param_label);
        UI::pack_start(*_widget, *pLabel, true, true);
        pLabel->set_tooltip_text(param_tooltip);
    }

    { // Paste item to link button
        Gtk::Image *pIcon = Gtk::manage(sp_get_icon_image("edit-paste", Gtk::IconSize::NORMAL));
        auto const pButton = Gtk::make_managed<Gtk::Button>();
        pButton->set_has_frame(false);
        pButton->set_child(*pIcon);
        pButton->signal_clicked().connect(sigc::mem_fun(*this, &OriginalSatelliteParam::on_link_button_click));
        UI::pack_start(*_widget, *pButton, true, true);
        pButton->set_tooltip_text(_("Link to item"));
    }

    { // Select original button
        Gtk::Image *pIcon = Gtk::manage(sp_get_icon_image("edit-select-original", Gtk::IconSize::NORMAL));
        auto const pButton = Gtk::make_managed<Gtk::Button>();
        pButton->set_has_frame(false);
        pButton->set_child(*pIcon);
        pButton->signal_clicked().connect(
            sigc::mem_fun(*this, &OriginalSatelliteParam::on_select_original_button_click));
        UI::pack_start(*_widget, *pButton, true, true);
        pButton->set_tooltip_text(_("Select original"));
    }

    return _widget;
}

void OriginalSatelliteParam::on_select_original_button_click()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    auto original = cast<SPItem>(lperef->getObject());
    if (desktop == nullptr || original == nullptr) {
        return;
    }
    Inkscape::Selection *selection = desktop->getSelection();
    selection->clear();
    selection->set(original);
}

} /* namespace LivePathEffect */
} /* namespace Inkscape */

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
