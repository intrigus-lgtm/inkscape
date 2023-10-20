// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * anchor-selector.cpp
 *
 *  Created on: Mar 22, 2012
 *      Author: denis
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#include "ui/widget/anchor-selector.h"

#include <utility>
#include <gtkmm/image.h>
#include <sigc++/adaptors/bind.h>
#include <sigc++/functors/mem_fun.h>

#include "ui/icon-loader.h"
#include "ui/icon-names.h"

namespace Inkscape::UI::Widget {

void AnchorSelector::setupButton(const Glib::ustring& icon, Gtk::ToggleButton& button) {
    auto const buttonIcon = Gtk::manage(sp_get_icon_image(icon, Gtk::IconSize::NORMAL));
    button.set_has_frame(false);
    button.set_child(*buttonIcon);
    button.set_focusable(false);
}

AnchorSelector::AnchorSelector()
{
    set_halign(Gtk::Align::CENTER);
    setupButton(INKSCAPE_ICON("boundingbox_top_left"), _buttons[0]);
    setupButton(INKSCAPE_ICON("boundingbox_top"), _buttons[1]);
    setupButton(INKSCAPE_ICON("boundingbox_top_right"), _buttons[2]);
    setupButton(INKSCAPE_ICON("boundingbox_left"), _buttons[3]);
    setupButton(INKSCAPE_ICON("boundingbox_center"), _buttons[4]);
    setupButton(INKSCAPE_ICON("boundingbox_right"), _buttons[5]);
    setupButton(INKSCAPE_ICON("boundingbox_bottom_left"), _buttons[6]);
    setupButton(INKSCAPE_ICON("boundingbox_bottom"), _buttons[7]);
    setupButton(INKSCAPE_ICON("boundingbox_bottom_right"), _buttons[8]);

    _container.set_row_homogeneous();
    _container.set_column_homogeneous(true);

    for (std::size_t i = 0; i < _buttons.size(); ++i) {
        _buttons[i].signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &AnchorSelector::btn_activated), i));

        _container.attach(_buttons[i], i % 3, i / 3, 1, 1);
    }

    _selection = 4;
    _buttons[_selection].set_active();

    append(_container);
}

sigc::connection AnchorSelector::connectSelectionChanged(sigc::slot<void ()> slot)
{
    return _selectionChanged.connect(std::move(slot));
}

void AnchorSelector::btn_activated(int index)
{
    if (_selection == index && _buttons[index].get_active() == false) {
        _buttons[index].set_active(true);
    }
    else if (_selection != index && _buttons[index].get_active()) {
        int old_selection = _selection;
        _selection = index;
        _buttons[old_selection].set_active(false);
        _selectionChanged.emit();
    }
}

void AnchorSelector::setAlignment(int horizontal, int vertical)
{
    int index = 3 * vertical + horizontal;
    if (index >= 0 && index < 9) {
        _buttons[index].set_active(!_buttons[index].get_active());
    }
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
