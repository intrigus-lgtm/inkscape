// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Carl Hetherington <inkscape@carlh.net>
 *   Derek P. Moore <derekm@hackunix.org>
 *   Bryce Harrington <bryce@bryceharrington.org>
 *
 * Copyright (C) 2004 Carl Hetherington
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "random.h"

#include <glibmm/i18n.h>
#include <gtkmm/button.h>
#include <gtkmm/image.h>

#include "ui/icon-loader.h"
#include "ui/pack.h"

namespace Inkscape::UI::Widget {

Random::Random(Glib::ustring const &label, Glib::ustring const &tooltip,
               Glib::ustring const &icon,
               bool mnemonic)
    : Scalar(label, tooltip, icon, mnemonic)
{
    startseed = 0;
    addReseedButton();
}

Random::Random(Glib::ustring const &label, Glib::ustring const &tooltip,
               unsigned digits,
               Glib::ustring const &icon,
               bool mnemonic)
    : Scalar(label, tooltip, digits, icon, mnemonic)
{
    startseed = 0;
    addReseedButton();
}

Random::Random(Glib::ustring const &label, Glib::ustring const &tooltip,
               Glib::RefPtr<Gtk::Adjustment> adjust,
               unsigned digits,
               Glib::ustring const &icon,
               bool mnemonic)
    : Scalar(label, tooltip, std::move(adjust), digits, icon, mnemonic)
{
    startseed = 0;
    addReseedButton();
}

long Random::getStartSeed() const
{
    return startseed;
}

void Random::setStartSeed(long newseed)
{
    startseed = newseed;
}

void Random::addReseedButton()
{
    auto const pButton = Gtk::make_managed<Gtk::Button>();
    pButton->set_has_frame(false);
    pButton->set_image_from_icon_name("randomize", Gtk::IconSize::NORMAL);
    pButton->set_visible(true);
    pButton->signal_clicked().connect(sigc::mem_fun(*this, &Random::onReseedButtonClick));
    pButton->set_tooltip_text(_("Reseed the random number generator; this creates a different sequence of random numbers."));

    UI::pack_start(*this, *pButton, UI::PackOptions::shrink);
}

void
Random::onReseedButtonClick()
{
    startseed = g_random_int();
    signal_reseeded.emit();
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
