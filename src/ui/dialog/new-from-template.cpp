// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief New From Template main dialog - implementation
 */
/* Authors:
 *   Jan Darowski <jan.darowski@gmail.com>, supervised by Krzysztof Kosiński
 *
 * Copyright (C) 2013 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "new-from-template.h"

#include <glibmm/i18n.h>

#include "desktop.h"
#include "file.h"
#include "inkscape-application.h"
#include "inkscape-window.h"
#include "inkscape.h"
#include "object/sp-namedview.h"
#include "ui/dialog-run.h"
#include "ui/pack.h"
#include "ui/widget/template-list.h"

namespace Inkscape {
namespace UI {

NewFromTemplate::NewFromTemplate()
    : _create_template_button(_("Create from template"))
{
    set_title(_("New From Template"));
    set_default_size(750, 500);

    templates = Gtk::make_managed<Inkscape::UI::Widget::TemplateList>();
    UI::pack_start(*get_content_area(), *templates);
    templates->init(Inkscape::Extension::TEMPLATE_NEW_FROM);

    _create_template_button.set_halign(Gtk::Align::END);
    _create_template_button.set_valign(Gtk::Align::END);
    _create_template_button.set_margin_end(15);

    UI::pack_end(*get_content_area(), _create_template_button, UI::PackOptions::shrink);
    
    _create_template_button.signal_clicked().connect(
    sigc::mem_fun(*this, &NewFromTemplate::_createFromTemplate));
    _create_template_button.set_sensitive(false);

    templates->connectItemSelected([=]() { _create_template_button.set_sensitive(true); });
    templates->connectItemActivated(sigc::mem_fun(*this, &NewFromTemplate::_createFromTemplate));
    templates->signal_switch_page().connect([=](Gtk::Widget *const widget, int num) {
        _create_template_button.set_sensitive(templates->has_selected_preset());
    });

    set_visible(true);
}

void NewFromTemplate::_createFromTemplate()
{
    SPDesktop *old_desktop = SP_ACTIVE_DESKTOP;

    auto doc = templates->new_document();

    // Cancel button was pressed.
    if (!doc)
        return;

    auto app = InkscapeApplication::instance();
    InkscapeWindow *win = app->window_open(doc);
    SPDesktop *new_desktop = win->get_desktop();
    sp_namedview_window_from_document(new_desktop);

    if (old_desktop)
        old_desktop->clearWaitingCursor();

    _onClose();
}

void NewFromTemplate::_onClose()
{
    response(0);
}

void NewFromTemplate::load_new_from_template()
{
    NewFromTemplate dl;
    Inkscape::UI::dialog_run(dl);
}

}
}
