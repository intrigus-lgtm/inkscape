// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A notebook with RGB, CMYK, CMS, HSL, and Wheel pages
 *//*
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Tomasz Boczkowski <penginsbacon@gmail.com> (c++-sification)
 *
 * Copyright (C) 2001-2014 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "color-notebook.h"

#undef SPCS_PREVIEW
#define noDUMP_CHANGE_INFO

#include <glibmm/i18n.h>
#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/stack.h>
#include <gtkmm/stackswitcher.h>

#include "desktop.h"
#include "document.h"
#include "inkscape.h"
#include "preferences.h"
#include "profile-manager.h"
#include "color/cms-system.h"
#include "object/color-profile.h"
#include "ui/dialog-events.h"
#include "ui/icon-loader.h"
#include "ui/pack.h"
#include "ui/tools/dropper-tool.h"
#include "ui/util.h"
#include "ui/widget/color-entry.h"
#include "ui/widget/color-scales.h"
#include "ui/widget/icon-combobox.h"

using Inkscape::CMSSystem;

static constexpr int XPAD = 2;
static constexpr int YPAD = 1;

namespace Inkscape::UI::Widget {

ColorNotebook::ColorNotebook(SelectedColor &color, bool no_alpha)
    : Gtk::Grid()
    , _selected_color(color)
{
    set_name("ColorNotebook");

    _initUI(no_alpha);

    _selected_color.signal_changed.connect(sigc::mem_fun(*this, &ColorNotebook::_onSelectedColorChanged));
    _selected_color.signal_dragged.connect(sigc::mem_fun(*this, &ColorNotebook::_onSelectedColorChanged));

    auto desktop = SP_ACTIVE_DESKTOP;
    _doc_replaced_connection = desktop->connectDocumentReplaced(sigc::hide<0>(sigc::mem_fun(*this, &ColorNotebook::setDocument)));
    setDocument(desktop->getDocument());
}

ColorNotebook::~ColorNotebook()
{
    if (_onetimepick)
        _onetimepick.disconnect();
    _doc_replaced_connection.disconnect();
    setDocument(nullptr);
}

ColorNotebook::Page::Page(std::unique_ptr<Inkscape::UI::ColorSelectorFactory> selector_factory, const char* icon)
    : selector_factory(std::move(selector_factory)), icon_name(icon)
{
}

void ColorNotebook::setDocument(SPDocument *document)
{
    _document = document;
    _icc_changed_connection.disconnect();
    if (document) {
        _icc_changed_connection = document->connectResourcesChanged("iccprofile", [this]() {
            _selected_color.emitIccChanged();
        });
    }
}

void ColorNotebook::set_label(const Glib::ustring& label) {
    _label->set_markup(label);
}

void ColorNotebook::_initUI(bool no_alpha)
{
    guint row = 0;

    _book = Gtk::make_managed<Gtk::Stack>();
    _book->set_visible(true);
    _book->set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    _book->set_transition_duration(130);

    // mode selection switcher widget shows all buttons for color mode selection, side by side
    _switcher = Gtk::make_managed<Gtk::StackSwitcher>();
    _switcher->set_stack(*_book);
    // cannot leave it homogeneous - in some themes switcher gets very wide
    // TODO: GTK4: Figure out whether this is still needed / possible to do
    //_switcher->set_homogeneous(false);
    _switcher->set_halign(Gtk::Align::CENTER);
    _switcher->set_visible(true);
    attach(*_switcher, 0, row++, 2);

    _buttonbox = Gtk::make_managed<Gtk::Box>();
    _buttonbox->set_visible(true);

    // combo mode selection is compact and only shows one entry (active)
    _combo = Gtk::make_managed<IconComboBox>();
    _combo->set_focusable(false);
    _combo->set_visible();
    _combo->set_tooltip_text(_("Choose style of color selection"));

    for (auto&& picker : get_color_pickers()) {
        auto page = Page(std::move(picker.factory), picker.icon);
        _addPage(page, no_alpha, picker.visibility_path);
    }

    _label = Gtk::make_managed<Gtk::Label>();
    _label->set_visible();
    UI::pack_start(*_buttonbox, *_label, false, true);
    UI::pack_end(*_buttonbox, *_combo, false, false);
    _combo->signal_changed().connect([=](){ _setCurrentPage(_combo->get_active_row_id(), false); });

    _buttonbox->set_margin_start(XPAD);
    _buttonbox->set_margin_end(XPAD);
    _buttonbox->set_margin_top(YPAD);
    _buttonbox->set_margin_bottom(YPAD);
    _buttonbox->set_hexpand();
    _buttonbox->set_valign(Gtk::Align::START);
    attach(*_buttonbox, 0, row, 2);

    row++;

    _book->set_margin_start(XPAD);
    _book->set_margin_end(XPAD);
    _book->set_margin_top(YPAD);
    _book->set_margin_bottom(YPAD);
    _book->set_hexpand();
    _book->set_vexpand();
    attach(*_book, 0, row, 2, 1);

    // restore the last active page
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring page_name = prefs->getString("/colorselector/page", "");
    _setCurrentPage(getPageIndex(page_name), true);
    row++;

    _observer = prefs->createObserver("/colorselector/switcher", [=](const Preferences::Entry& new_value) {
        _switcher->set_visible(!new_value.getBool());
        _buttonbox->set_visible(new_value.getBool());
    });
    _observer->call();

    GtkWidget *rgbabox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    auto const rgbabox_box = GTK_BOX(rgbabox);

    /* Create color management icons */
    _colormanaged = sp_get_icon_image("color-management", GTK_ICON_SIZE_NORMAL);
    gtk_widget_set_tooltip_text(_colormanaged, _("Color Managed"));
    gtk_widget_set_sensitive(_colormanaged, false);
    gtk_box_append(rgbabox_box, _colormanaged);

    _outofgamut = sp_get_icon_image("out-of-gamut-icon", GTK_ICON_SIZE_NORMAL);
    gtk_widget_set_tooltip_text(_outofgamut, _("Out of gamut!"));
    gtk_widget_set_sensitive(_outofgamut, false);
    gtk_box_append(rgbabox_box, _outofgamut);

    _toomuchink = sp_get_icon_image("too-much-ink-icon", GTK_ICON_SIZE_NORMAL);
    gtk_widget_set_tooltip_text(_toomuchink, _("Too much ink!"));
    gtk_widget_set_sensitive(_toomuchink, false);
    gtk_box_append(rgbabox_box, _toomuchink);

    /* Color picker */
    GtkWidget *picker = sp_get_icon_image("color-picker", GTK_ICON_SIZE_NORMAL);
    _btn_picker = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(_btn_picker), false);
    gtk_box_append(GTK_BOX(_btn_picker), picker);
    gtk_widget_set_tooltip_text(_btn_picker, _("Pick colors from image"));
    gtk_box_append(rgbabox_box, _btn_picker);
    g_signal_connect(G_OBJECT(_btn_picker), "clicked", G_CALLBACK(ColorNotebook::_onPickerClicked), this);

    /* Create RGBA entry and color preview */
    _rgbal = gtk_label_new_with_mnemonic(_("RGBA_:"));
    gtk_widget_set_halign(_rgbal, GTK_ALIGN_END);
    gtk_widget_set_hexpand(_rgbal, TRUE);
    gtk_box_append(rgbabox_box, _rgbal);

    auto const rgba_entry = Gtk::make_managed<ColorEntry>(_selected_color);
    auto const rgba_entry_widget = rgba_entry->Gtk::Widget::gobj();
    sp_dialog_defocus_on_enter(rgba_entry_widget);
    gtk_box_append(rgbabox_box, rgba_entry_widget);
    gtk_label_set_mnemonic_widget(GTK_LABEL(_rgbal), rgba_entry_widget);

    // the "too much ink" icon is initially hidden
    gtk_widget_set_visible(_toomuchink, false);

    gtk_widget_set_margin_start(rgbabox, XPAD);
    gtk_widget_set_margin_end(rgbabox, XPAD);
    gtk_widget_set_margin_top(rgbabox, YPAD);
    gtk_widget_set_margin_bottom(rgbabox, YPAD);
    attach(*Glib::wrap(rgbabox), 0, row, 2, 1);

    // remember the page we switched to
    _book->property_visible_child_name().signal_changed().connect([=]() {
        // We don't want to remember auto cms selection
        Glib::ustring name = _book->get_visible_child_name();
        if (get_visible() && !name.empty() && name != "CMS") {
            Inkscape::Preferences *prefs = Inkscape::Preferences::get();
            prefs->setString("/colorselector/page", name);
        }
    });

#ifdef SPCS_PREVIEW
    _p = sp_color_preview_new(0xffffffff);
    gtk_widget_set_visible(_p, true);
    attach(*Glib::wrap(_p), 2, 3, row, row + 1, Gtk::FILL, Gtk::FILL, XPAD, YPAD);
#endif
}

void ColorNotebook::_onPickerClicked(GtkWidget * /*widget*/, ColorNotebook *colorbook)
{
    // Set the dropper into a "one click" mode, so it reverts to the previous tool after a click
    if (colorbook->_onetimepick) {
        colorbook->_onetimepick.disconnect();
    }
    else {
        Inkscape::UI::Tools::sp_toggle_dropper(SP_ACTIVE_DESKTOP);
        auto tool = dynamic_cast<Inkscape::UI::Tools::DropperTool *>(SP_ACTIVE_DESKTOP->getTool());
        if (tool) {
            colorbook->_onetimepick = tool->onetimepick_signal.connect(sigc::mem_fun(*colorbook, &ColorNotebook::_pickColor));
        }
    }
}

void ColorNotebook::_pickColor(ColorRGBA *color) {
    // Set color to color notebook here.
    _selected_color.setValue(color->getIntValue());
    _onSelectedColorChanged();
}

void ColorNotebook::_onSelectedColorChanged() { _updateICCButtons(); }

void ColorNotebook::_updateICCButtons()
{
    if (!_document) {
        return;
    }

    SPColor color = _selected_color.color();
    gfloat alpha = _selected_color.alpha();

    g_return_if_fail((0.0 <= alpha) && (alpha <= 1.0));

    /* update color management icon*/
    gtk_widget_set_sensitive(_colormanaged, color.hasColorProfile());
    gtk_widget_set_sensitive(_toomuchink, false);
    gtk_widget_set_sensitive(_outofgamut, false);

    if (color.hasColors()) {
        auto name = color.getColorProfile();

        // Set notebook page to cms if icc profile being used.
        _setCurrentPage(getPageIndex("CMS"), true);

        /* update out-of-gamut icon */
        Inkscape::ColorProfile *target_profile =
            _document->getProfileManager().find(name.c_str());
        if (target_profile)
            gtk_widget_set_sensitive(_outofgamut, target_profile->GamutCheck(color));

        /* update too-much-ink icon */
        Inkscape::ColorProfile *prof = _document->getProfileManager().find(name.c_str());
        if (prof && prof->isPrintColorSpace()) {
            gtk_widget_set_visible(_toomuchink, true);
            double ink_sum = 0;
            for (double i : color.getColors()) {
                ink_sum += i;
            }

            /* Some literature states that when the sum of paint values exceed 320%, it is considered to be a satured color,
                which means the paper can get too wet due to an excessive amount of ink. This may lead to several issues
                such as misalignment and poor quality of printing in general.*/
            if (ink_sum > 3.2)
                gtk_widget_set_sensitive(_toomuchink, true);
        }
        else {
            gtk_widget_set_visible(_toomuchink, false);
        }
    } else {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        auto page = prefs->getString("/colorselector/page");
        _setCurrentPage(getPageIndex(page), true);
    }
}

int ColorNotebook::getPageIndex(const Glib::ustring &name)
{
    return getPageIndex(_book->get_child_by_name(name));
}

int ColorNotebook::getPageIndex(Gtk::Widget *widget)
{
    auto const pages = UI::get_children(*_book);
    auto const n_pages = static_cast<int>(pages.size());
    for (int i = 0; i < n_pages; ++i) {
        if (pages[i] == widget) {
            return i;
        }
    }
    return 0;
}

void ColorNotebook::_setCurrentPage(int i, bool sync_combo)
{
    auto const pages = UI::get_children(*_book);
    auto const n_pages = static_cast<int>(pages.size());
    if (i >= n_pages) {
        // page index could be outside the valid range if we manipulate visible color pickers;
        // default to the first page, so we show something
        i = 0;
    }

    if (i >= 0 && i < n_pages) {
        _book->set_visible_child(*pages[i]);
        if (sync_combo) {
            _combo->set_active_by_id(i);
        }
    }
}

void ColorNotebook::_addPage(Page &page, bool no_alpha, const Glib::ustring vpath)
{
    if (auto selector_widget = page.selector_factory->createWidget(_selected_color, no_alpha)) {
        Glib::ustring mode_name = page.selector_factory->modeName();
        _book->add(*selector_widget, mode_name, mode_name);

        auto const n_pages = static_cast<int>(UI::get_children(*_book).size());
        auto const page_num = n_pages - 1;
        _combo->add_row(page.icon_name, mode_name, page_num);

        auto prefs = Inkscape::Preferences::get();
        auto obs = prefs->createObserver(vpath, [=](const Preferences::Entry& value) {
            _combo->set_row_visible(page_num, value.getBool());
            selector_widget->set_visible(value.getBool());
        });
        obs->call();
        _visibility_observers.emplace_back(std::move(obs));
    }
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
