// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * anchor-selector.cpp
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "ui/widget/alignment-selector.h"

#include <utility>
#include <gtkmm/image.h>
#include <sigc++/adaptors/bind.h>
#include <sigc++/functors/mem_fun.h>

#include "ui/icon-loader.h"
#include "ui/icon-names.h"

namespace Inkscape::UI::Widget {

void AlignmentSelector::setupButton(const Glib::ustring& icon, Gtk::Button& button) {
    auto const buttonIcon = Gtk::manage(sp_get_icon_image(icon, Gtk::IconSize::NORMAL));
    button.set_has_frame(false);
    button.set_child(*buttonIcon);
    button.set_focusable(false);
}

AlignmentSelector::AlignmentSelector()
{
    set_halign(Gtk::Align::CENTER);
    // clang-format off
    setupButton(INKSCAPE_ICON("boundingbox_top_left"),     _buttons[0]);
    setupButton(INKSCAPE_ICON("boundingbox_top"),          _buttons[1]);
    setupButton(INKSCAPE_ICON("boundingbox_top_right"),    _buttons[2]);
    setupButton(INKSCAPE_ICON("boundingbox_left"),         _buttons[3]);
    setupButton(INKSCAPE_ICON("boundingbox_center"),       _buttons[4]);
    setupButton(INKSCAPE_ICON("boundingbox_right"),        _buttons[5]);
    setupButton(INKSCAPE_ICON("boundingbox_bottom_left"),  _buttons[6]);
    setupButton(INKSCAPE_ICON("boundingbox_bottom"),       _buttons[7]);
    setupButton(INKSCAPE_ICON("boundingbox_bottom_right"), _buttons[8]);
    // clang-format on

    _container.set_row_homogeneous();
    _container.set_column_homogeneous(true);

    for (std::size_t i = 0; i < _buttons.size(); ++i) {
        _buttons[i].signal_clicked().connect(
            sigc::bind(sigc::mem_fun(*this, &AlignmentSelector::btn_activated), i));

        _container.attach(_buttons[i], i % 3, i / 3, 1, 1);
    }

    append(_container);
}

sigc::connection AlignmentSelector::connectAlignmentClicked(sigc::slot<void (int)> slot)
{
    return _alignmentClicked.connect(std::move(slot));
}

void AlignmentSelector::btn_activated(int index)
{
    _alignmentClicked.emit(index);
}

} // namespace Inkscape::UI::Widget

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
