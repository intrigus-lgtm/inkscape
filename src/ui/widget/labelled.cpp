// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Carl Hetherington <inkscape@carlh.net>
 *   Derek P. Moore <derekm@hackunix.org>
 *
 * Copyright (C) 2004 Carl Hetherington
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "labelled.h"

#include <gtkmm/image.h>
#include <gtkmm/label.h>

#include "ui/icon-loader.h"
#include "ui/pack.h"

namespace Inkscape::UI::Widget {

Labelled::Labelled(Glib::ustring const &label, Glib::ustring const &tooltip,
                   Gtk::Widget *widget,
                   Glib::ustring const &icon,
                   bool mnemonic)
    : Gtk::Box{Gtk::Orientation::HORIZONTAL, 6}
    , _widget{widget}
    , _label{Gtk::make_managed<Gtk::Label>(label, Gtk::Align::START, Gtk::Align::CENTER, mnemonic)}
{
    g_assert(widget);
    g_assert(g_utf8_validate(icon.c_str(), -1, nullptr));

    _widget->drag_dest_unset();

    if (!icon.empty()) {
        auto const image = sp_get_icon_image(icon, Gtk::ICON_SIZE_LARGE_TOOLBAR);
        UI::pack_start(*this, *image, UI::PackOptions::shrink);
    }

    UI::pack_start(*this, *_label, UI::PackOptions::shrink);
    UI::pack_start(*this, *Gtk::manage(_widget), UI::PackOptions::shrink);

    if (mnemonic) {
        _label->set_mnemonic_widget(*_widget);
    }

    set_tooltip_markup(tooltip);
}

bool Labelled::on_mnemonic_activate(bool const group_cycling)
{
    return _widget->mnemonic_activate(group_cycling);
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
