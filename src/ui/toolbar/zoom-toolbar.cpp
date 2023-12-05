// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Zoom aux toolbar: Temp until we convert all toolbars to ui files with Gio::Actions.
 */
/* Authors:
 *   Tavmjong Bah <tavmjong@free.fr>
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Copyright (C) 2019 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "zoom-toolbar.h"

#include "ui/builder-utils.h"

namespace Inkscape::UI::Toolbar {

ZoomToolbar::ZoomToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    , _builder(create_builder("toolbar-zoom.ui"))
{
    _toolbar = &get_widget<Gtk::Box>(_builder, "zoom-toolbar");

    set_child(*_toolbar);
}

ZoomToolbar::~ZoomToolbar() = default;

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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
