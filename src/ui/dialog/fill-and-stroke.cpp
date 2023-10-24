// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Fill and Stroke dialog - implementation.
 *
 * Based on the old sp_object_properties_dialog.
 */
/* Authors:
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *   Gustav Broberg <broberg@kth.se>
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright (C) 2004--2007 Authors
 * Copyright (C) 2010 Jon A. Cruz
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "fill-and-stroke.h"

#include <gtkmm/box.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>

#include "desktop-style.h"
#include "preferences.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/pack.h"
#include "ui/widget/fill-style.h"
#include "ui/widget/stroke-style.h"
#include "ui/widget/notebook-page.h"

namespace Inkscape::UI::Dialog {

FillAndStroke::FillAndStroke()
    : DialogBase("/dialogs/fillstroke", "FillStroke")
    , _page_fill(Gtk::make_managed<UI::Widget::NotebookPage>(1, 1, true, true))
    , _page_stroke_paint(Gtk::make_managed<UI::Widget::NotebookPage>(1, 1, true, true))
    , _page_stroke_style(Gtk::make_managed<UI::Widget::NotebookPage>(1, 1, true, true))
    , _composite_settings(INKSCAPE_ICON("dialog-fill-and-stroke"),
                          "fillstroke",
                          UI::Widget::SimpleFilterModifier::ISOLATION |
                          UI::Widget::SimpleFilterModifier::BLEND |
                          UI::Widget::SimpleFilterModifier::BLUR |
                          UI::Widget::SimpleFilterModifier::OPACITY)
    , fillWdgt(nullptr)
    , strokeWdgt(nullptr)
{
    set_spacing(2);
    UI::pack_start(*this, _notebook, true, true);

    _notebook.append_page(*_page_fill, _createPageTabLabel(_("_Fill"), INKSCAPE_ICON("object-fill")));
    _notebook.append_page(*_page_stroke_paint, _createPageTabLabel(_("Stroke _paint"), INKSCAPE_ICON("object-stroke")));
    _notebook.append_page(*_page_stroke_style, _createPageTabLabel(_("Stroke st_yle"), INKSCAPE_ICON("object-stroke-style")));
    _notebook.set_vexpand(true);

    _notebook.signal_switch_page().connect(sigc::mem_fun(*this, &FillAndStroke::_onSwitchPage));

    _layoutPageFill();
    _layoutPageStrokePaint();
    _layoutPageStrokeStyle();

    UI::pack_end(*this, _composite_settings, UI::PackOptions::shrink);

    _composite_settings.setSubject(&_subject);
}

FillAndStroke::~FillAndStroke()
{
    // Disconnect signals from composite settings
    _composite_settings.setSubject(nullptr);
    fillWdgt->setDesktop(nullptr);
    strokeWdgt->setDesktop(nullptr);
    strokeStyleWdgt->setDesktop(nullptr);
    _subject.setDesktop(nullptr);
}

void FillAndStroke::selectionChanged(Selection *selection)
{
    if (fillWdgt) {
        fillWdgt->performUpdate();
    }
    if (strokeWdgt) {
        strokeWdgt->performUpdate();
    }
    if (strokeStyleWdgt) {
        strokeStyleWdgt->selectionChangedCB();
    }
}

void FillAndStroke::selectionModified(Selection *selection, guint flags)
{
    if (fillWdgt) {
        fillWdgt->selectionModifiedCB(flags);
    }
    if (strokeWdgt) {
        strokeWdgt->selectionModifiedCB(flags);
    }
    if (strokeStyleWdgt) {
        strokeStyleWdgt->selectionModifiedCB(flags);
    }
}

void FillAndStroke::desktopReplaced()
{
    if (fillWdgt) {
        fillWdgt->setDesktop(getDesktop());
    }
    if (strokeWdgt) {
        strokeWdgt->setDesktop(getDesktop());
    }
    if (strokeStyleWdgt) {
        strokeStyleWdgt->setDesktop(getDesktop());
    }
    _subject.setDesktop(getDesktop());
}

void FillAndStroke::_onSwitchPage(Gtk::Widget * /*page*/, guint pagenum)
{
    _savePagePref(pagenum);
}

void
FillAndStroke::_savePagePref(guint page_num)
{
    // remember the current page
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setInt("/dialogs/fillstroke/page", page_num);
}

void
FillAndStroke::_layoutPageFill()
{
    fillWdgt = Gtk::make_managed<UI::Widget::FillNStroke>(FILL);
    _page_fill->table().attach(*fillWdgt, 0, 0, 1, 1);
}

void
FillAndStroke::_layoutPageStrokePaint()
{
    strokeWdgt = Gtk::make_managed<UI::Widget::FillNStroke>(STROKE);
    _page_stroke_paint->table().attach(*strokeWdgt, 0, 0, 1, 1);
}

void
FillAndStroke::_layoutPageStrokeStyle()
{
    strokeStyleWdgt = Gtk::make_managed<UI::Widget::StrokeStyle>();
    strokeStyleWdgt->set_hexpand();
    strokeStyleWdgt->set_halign(Gtk::Align::START);
    _page_stroke_style->table().attach(*strokeStyleWdgt, 0, 0, 1, 1);
}

void
FillAndStroke::showPageFill()
{
    blink();
    _notebook.set_current_page(0);
    _savePagePref(0);

}

void
FillAndStroke::showPageStrokePaint()
{
    blink();
    _notebook.set_current_page(1);
    _savePagePref(1);
}

void
FillAndStroke::showPageStrokeStyle()
{
    blink();
    _notebook.set_current_page(2);
    _savePagePref(2);

}

Gtk::Box&
FillAndStroke::_createPageTabLabel(const Glib::ustring& label, const char *label_image)
{
    auto const _tab_label_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);

    auto img = Gtk::manage(sp_get_icon_image(label_image, Gtk::IconSize::NORMAL));
    _tab_label_box->append(*img);

    auto const _tab_label = Gtk::make_managed<Gtk::Label>(label, true);
    _tab_label_box->append(*_tab_label);

    return *_tab_label_box;
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
