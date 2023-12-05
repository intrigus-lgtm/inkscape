// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A toolbar for the Builder tool.
 *
 * Authors:
 *   Martin Owens
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Copyright (C) 2022 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "ui/toolbar/booleans-toolbar.h"

#include <gtkmm/adjustment.h>
#include <gtkmm/builder.h>
#include <gtkmm/button.h>

#include "desktop.h"
#include "ui/builder-utils.h"
#include "ui/tools/booleans-tool.h"

namespace Inkscape::UI::Toolbar {

BooleansToolbar::BooleansToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    , _builder(create_builder("toolbar-booleans.ui"))
{
    _toolbar = &get_widget<Gtk::Box>(_builder, "booleans-toolbar");

    auto adj_opacity = get_object<Gtk::Adjustment>(_builder, "opacity_adj");

    get_widget<Gtk::Button>(_builder, "confirm_btn").signal_clicked().connect([=] {
        auto const tool = dynamic_cast<Tools::InteractiveBooleansTool *>(desktop->getTool());
        tool->shape_commit();
    });

    get_widget<Gtk::Button>(_builder, "cancel_btn").signal_clicked().connect([=] {
        auto const tool = dynamic_cast<Tools::InteractiveBooleansTool *>(desktop->getTool());
        tool->shape_cancel();
    });

    set_child(*_toolbar);

    auto prefs = Inkscape::Preferences::get();

    adj_opacity->set_value(prefs->getDouble("/tools/booleans/opacity", 0.5) * 100);
    adj_opacity->signal_value_changed().connect([=]() {
        auto const tool = dynamic_cast<Tools::InteractiveBooleansTool *>(desktop->getTool());
        double value = (double)adj_opacity->get_value() / 100;
        prefs->setDouble("/tools/booleans/opacity", value);
        tool->set_opacity(value);
    });
}

BooleansToolbar::~BooleansToolbar() = default;

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
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:
