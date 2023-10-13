// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * TODO: insert short description here
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "swatch-selector.h"

#include <glibmm/i18n.h>

#include "document-undo.h"
#include "gradient-chemistry.h"

#include "object/sp-stop.h"
#include "ui/icon-names.h"
#include "ui/pack.h"
#include "ui/widget/color-notebook.h"
#include "ui/widget/gradient-selector.h"

namespace Inkscape {
namespace UI {
namespace Widget {

SwatchSelector::SwatchSelector()
    : Gtk::Box(Gtk::Orientation::VERTICAL)
{
    _gsel = Gtk::make_managed<GradientSelector>();
    _gsel->setMode(GradientSelector::MODE_SWATCH);
    _gsel->set_visible(true);
    UI::pack_start(*this, *_gsel);

    auto const color_selector = Gtk::make_managed<ColorNotebook>(_selected_color);
    color_selector->set_label(_("Swatch color"));
    color_selector->set_visible(true);
    UI::pack_start(*this, *color_selector);

    _selected_color.signal_dragged.connect(sigc::mem_fun(*this, &SwatchSelector::_changedCb));
    _selected_color.signal_released.connect(sigc::mem_fun(*this, &SwatchSelector::_changedCb));
    // signal_changed doesn't get called if updating shape with colour.
    _selected_color.signal_changed.connect(sigc::mem_fun(*this, &SwatchSelector::_changedCb));
}

void SwatchSelector::_changedCb()
{
    if (_updating_color) {
        return;
    }
    // TODO might have to block cycles

    if (_gsel && _gsel->getVector()) {
        SPGradient *gradient = _gsel->getVector();
        SPGradient *ngr = sp_gradient_ensure_vector_normalized(gradient);
        if (ngr != gradient) {
            /* Our master gradient has changed */
            // TODO replace with proper - sp_gradient_vector_widget_load_gradient(GTK_WIDGET(swsel->_gsel), ngr);
        }

        ngr->ensureVector();

        if (auto stop = ngr->getFirstStop()) {
            stop->setColor(_selected_color.color(), _selected_color.alpha());
            DocumentUndo::done(ngr->document, _("Change swatch color"), INKSCAPE_ICON("color-gradient"));
        }
    }
}

void SwatchSelector::setVector(SPDocument */*doc*/, SPGradient *vector)
{
    _gsel->setVector(vector ? vector->document : nullptr, vector);

    if (vector && vector->isSolid()) {
        _updating_color = true;
        auto stop = vector->getFirstStop();
        _selected_color.setColorAlpha(stop->getColor(), stop->getOpacity(), true);
        _updating_color = false;
    }
}

} // namespace Widget
} // namespace UI
} // namespace Inkscape

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
