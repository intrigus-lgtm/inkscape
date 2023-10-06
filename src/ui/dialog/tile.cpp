// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A simple dialog for creating grid type arrangements of selected objects
 *
 * Authors:
 *   Bob Jamison ( based off trace dialog)
 *   John Cliff
 *   Other dudes from The Inkscape Organization
 *   Abhishek Sharma
 *   Declara Denis
 *
 * Copyright (C) 2004 Bob Jamison
 * Copyright (C) 2004 John Cliff
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "tile.h"

#include <glibmm/i18n.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/notebook.h>

#include "ui/dialog/grid-arrange-tab.h"
#include "ui/dialog/polar-arrange-tab.h"
#include "ui/dialog/align-and-distribute.h"
#include "ui/icon-names.h"
#include "ui/pack.h"

namespace Inkscape::UI::Dialog {

Gtk::Box& create_tab_label(const char* label_text, const char* icon_name) {
    auto const box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    auto const image = Gtk::make_managed<Gtk::Image>();
    image->set_from_icon_name(icon_name);
    auto const label = Gtk::make_managed<Gtk::Label>(label_text, true);
    UI::pack_start(*box, *image, false, true);
    UI::pack_start(*box, *label, false, true);
    return *box;
}

ArrangeDialog::ArrangeDialog()
    : DialogBase("/dialogs/gridtiler", "AlignDistribute")
{
    _align_tab = Gtk::make_managed<AlignAndDistribute>(this);
    _arrangeBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    _arrangeBox->set_valign(Gtk::Align::START);
    _notebook = Gtk::make_managed<Gtk::Notebook>();
    _gridArrangeTab = Gtk::make_managed<GridArrangeTab>(this);
    _polarArrangeTab = Gtk::make_managed<PolarArrangeTab>(this);

    set_valign(Gtk::Align::START);

    _notebook->set_valign(Gtk::Align::START);
    _notebook->append_page(*_align_tab, create_tab_label(C_("Arrange dialog", "Align"), INKSCAPE_ICON("dialog-align-and-distribute")));
    // TRANSLATORS: "Grid" refers to grid (columns/rows) arrangement
    _notebook->append_page(*_gridArrangeTab, create_tab_label(C_("Arrange dialog", "Grid"), INKSCAPE_ICON("arrange-grid")));
    // TRANSLATORS: "Circular" refers to circular/radial arrangement
    _notebook->append_page(*_polarArrangeTab, create_tab_label(C_("Arrange dialog", "Circular"), INKSCAPE_ICON("arrange-circular")));
    UI::pack_start(*_arrangeBox, *_notebook);
    _notebook->signal_switch_page().connect([=](Widget*, guint page){
        update_arrange_btn();
    });
    UI::pack_start(*this, *_arrangeBox);

    // Add button
    _arrangeButton = Gtk::make_managed<Gtk::Button>(C_("Arrange dialog", "_Arrange"));
    _arrangeButton->signal_clicked().connect(sigc::mem_fun(*this, &ArrangeDialog::_apply));
    _arrangeButton->set_use_underline(true);
    _arrangeButton->set_tooltip_text(_("Arrange selected objects"));
    _arrangeButton->add_css_class("wide-apply-button");
    _arrangeButton->set_visible(false);

    auto const button_box = Gtk::make_managed<Gtk::Box>();
    button_box->set_halign(Gtk::Align::CENTER);
    button_box->set_spacing(6);
    button_box->set_margin(4);
    UI::pack_end(*button_box, *_arrangeButton);
    UI::pack_start(*this, *button_box);

    set_visible(true);

    update_arrange_btn();
}

void ArrangeDialog::update_arrange_btn() {
    // "align" page doesn't use "Arrange" button
    if (_notebook->get_current_page() == 0) {
        _arrangeButton->set_visible(false);
    }
    else {
        _arrangeButton->set_visible(true);
    }
}

void ArrangeDialog::_apply()
{
	switch(_notebook->get_current_page())
	{
	case 0:
        // not applicable to align panel
        break;
	case 1:
		_gridArrangeTab->arrange();
		break;
	case 2:
		_polarArrangeTab->arrange();
		break;
	}
}

void ArrangeDialog::desktopReplaced()
{
    _gridArrangeTab->setDesktop(getDesktop());
    _align_tab->desktop_changed(getDesktop());
}

} // namespace Inkscape::UI::Dialog

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
