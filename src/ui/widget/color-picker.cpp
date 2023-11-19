// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Ralf Stephan <ralf@ark.in-berlin.de>
 *   Abhishek Sharma
 *
 * Copyright (C) Authors 2000-2005
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "color-picker.h"

#include "desktop.h"
#include "document-undo.h"
#include "inkscape.h"

#include "ui/dialog-events.h"
#include "ui/widget/color-notebook.h"

static bool _in_use = false;

namespace Inkscape::UI::Widget {

ColorPicker::ColorPicker(Glib::ustring const &title,
                         Glib::ustring const &tip,
                         std::uint32_t const rgba,
                         bool const undo,
                         Gtk::Button * const external_button)
    : _preview(Gtk::make_managed<ColorPreview>(rgba))
    , _title(title)
    , _rgba(rgba)
    , _undo(undo)
{
    auto button = external_button ? external_button : this;
    button->set_child(*_preview);
    // set tooltip if given, otherwise leave original tooltip in place (from external button)
    if (!tip.empty()) {
        button->set_tooltip_text(tip);
    }
    _selected_color.signal_changed.connect(sigc::mem_fun(*this, &ColorPicker::_onSelectedColorChanged));
    _selected_color.signal_dragged.connect(sigc::mem_fun(*this, &ColorPicker::_onSelectedColorChanged));
    _selected_color.signal_released.connect(sigc::mem_fun(*this, &ColorPicker::_onSelectedColorChanged));

    if (external_button) {
        external_button->signal_clicked().connect([this] { on_clicked(); });
    }
    
    sp_transientize(_colorSelectorDialog);
    _colorSelectorDialog.set_title(title);
    _colorSelectorDialog.set_hide_on_close();
}

ColorPicker::~ColorPicker() = default;

void ColorPicker::setRgba32(std::uint32_t const rgba)
{
    if (_in_use) return;

    set_preview(rgba);
    _rgba = rgba;

    if (_color_selector)
    {
        _updating = true;
        _selected_color.setValue(rgba);
        _updating = false;
    }
}

void ColorPicker::closeWindow()
{
    _colorSelectorDialog.set_visible(false);
}

void ColorPicker::open() {
    on_clicked();
}

void ColorPicker::on_clicked()
{
    if (!_color_selector) {
        _color_selector = Gtk::make_managed<ColorNotebook>(_selected_color, _ignore_transparency);
        _color_selector->set_label(_title);
        _color_selector->set_margin(4);
        _colorSelectorDialog.set_child(*_color_selector);
    }

    _updating = true;
    _selected_color.setValue(_rgba);
    _updating = false;

    _colorSelectorDialog.present();
}

void ColorPicker::on_changed(std::uint32_t)
{
}

void ColorPicker::_onSelectedColorChanged() {
    if (_updating) {
        return;
    }

    if (_in_use) {
        return;
    } else {
        _in_use = true;
    }

    auto const rgba = _selected_color.value();
    set_preview(rgba);

    if (_undo && SP_ACTIVE_DESKTOP) {
        DocumentUndo::done(SP_ACTIVE_DESKTOP->getDocument(), /* TODO: annotate */ "color-picker.cpp:129", "");
    }

    on_changed(rgba);
    _in_use = false;
    _rgba = rgba;
    _changed_signal.emit(rgba);
}

void ColorPicker::set_preview(std::uint32_t const rgba)
{
    _preview->setRgba32(_ignore_transparency ? rgba | 0xff : rgba);
}

void ColorPicker::use_transparency(bool enable) {
    _ignore_transparency = !enable;
    set_preview(_rgba);
}

std::uint32_t ColorPicker::get_current_color() const
{
    return _rgba;
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
