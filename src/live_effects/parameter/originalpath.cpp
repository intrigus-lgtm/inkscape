// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) Johan Engelen 2012 <j.b.c.engelen@alumnus.utwente.nl>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "live_effects/parameter/originalpath.h"

#include <glibmm/i18n.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>

#include "desktop.h"
#include "display/curve.h"
#include "inkscape.h"
#include "live_effects/effect.h"
#include "live_effects/lpeobject.h"
#include "object/sp-shape.h"
#include "object/sp-text.h"
#include "object/uri.h"
#include "selection.h"
#include "ui/icon-names.h"
#include "ui/pack.h"

namespace Inkscape {
namespace LivePathEffect {

OriginalPathParam::OriginalPathParam( const Glib::ustring& label, const Glib::ustring& tip,
                      const Glib::ustring& key, Inkscape::UI::Widget::Registry* wr,
                      Effect* effect)
    : PathParam(label, tip, key, wr, effect, "")
{
    oncanvas_editable = false;
    _from_original_d = false;
}

Gtk::Widget *
OriginalPathParam::param_newWidget()
{
    auto const _widget = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);

    { // Label
        auto const pLabel = Gtk::make_managed<Gtk::Label>(param_label);
        UI::pack_start(*_widget, *pLabel, true, true);
        pLabel->set_tooltip_text(param_tooltip);
    }

    { // Paste path to link button
        auto const pIcon = Gtk::make_managed<Gtk::Image>();
        pIcon->set_from_icon_name("edit-clone");
        auto const pButton = Gtk::make_managed<Gtk::Button>();
        pButton->set_has_frame(false);
        pButton->set_child(*pIcon);
        pButton->signal_clicked().connect(sigc::mem_fun(*this, &OriginalPathParam::on_link_button_click));
        UI::pack_start(*_widget, *pButton, true, true);
        pButton->set_tooltip_text(_("Link to path in clipboard"));
    }

    { // Select original button
        auto const pIcon = Gtk::make_managed<Gtk::Image>();
        pIcon->set_from_icon_name("edit-select-original");
        auto const pButton = Gtk::make_managed<Gtk::Button>();
        pButton->set_has_frame(false);
        pButton->set_child(*pIcon);
        pButton->signal_clicked().connect(sigc::mem_fun(*this, &OriginalPathParam::on_select_original_button_click));
        UI::pack_start(*_widget, *pButton, true, true);
        pButton->set_tooltip_text(_("Select original"));
    }

    return _widget;
}

void
OriginalPathParam::on_select_original_button_click()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    SPItem *original = ref.getObject();
    if (desktop == nullptr || original == nullptr) {
        return;
    }
    Inkscape::Selection *selection = desktop->getSelection();
    selection->clear();
    selection->set(original);
    param_effect->getLPEObj()->requestModified(SP_OBJECT_MODIFIED_FLAG);
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
