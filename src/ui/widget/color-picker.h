// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Color picker button and window.
 */
/* Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Ralf Stephan <ralf@ark.in-berlin.de>
 *
 * Copyright (C) Authors 2000-2005
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_INKSCAPE_COLOR_PICKER_H
#define SEEN_INKSCAPE_COLOR_PICKER_H

#include "labelled.h"

#include <cstdint>
#include <utility>

#include "ui/selected-color.h"
#include "ui/widget/color-preview.h"
#include <gtkmm/button.h>
#include <gtkmm/dialog.h>
#include <sigc++/signal.h>

struct SPColorSelector;

namespace Inkscape::UI::Widget {

class ColorNotebook;

class ColorPicker : public Gtk::Button {
public:
    [[nodiscard]] ColorPicker(Glib::ustring const &title,
                              Glib::ustring const &tip,
                              std::uint32_t rgba,
                              bool undo,
                              Gtk::Button *external_button = nullptr);

    ~ColorPicker() override;

    void setRgba32(std::uint32_t rgba);
    void setSensitive(bool sensitive) { set_sensitive(sensitive); }
    void open();
    void closeWindow();

    sigc::connection connectChanged(sigc::slot<void (std::uint32_t)> slot)
    {
        return _changed_signal.connect(std::move(slot));
    }

    void use_transparency(bool enable);
    [[nodiscard]] std::uint32_t get_current_color() const;

protected:
    void _onSelectedColorChanged();
    void on_clicked() override;
    virtual void on_changed(std::uint32_t);

    ColorPreview *_preview = nullptr;

    Glib::ustring _title;
    sigc::signal<void (std::uint32_t)> _changed_signal;
    std::uint32_t _rgba     = 0    ;
    bool          _undo     = false;
    bool          _updating = false;

    Gtk::Window _colorSelectorDialog;

    SelectedColor _selected_color;

private:
    void set_preview(std::uint32_t rgba);

    ColorNotebook *_color_selector = nullptr;
    bool _ignore_transparency = false;
};


class LabelledColorPicker : public Labelled {
public:
    [[nodiscard]] LabelledColorPicker(Glib::ustring const &label,
                                      Glib::ustring const &title,
                                      Glib::ustring const &tip,
                                      std::uint32_t rgba,
                                      bool undo)
        : Labelled(label, tip, new ColorPicker(title, tip, rgba, undo)) {}

    void setRgba32(std::uint32_t const rgba)
    {
        static_cast<ColorPicker *>(getWidget())->setRgba32(rgba);
    }

    void closeWindow()
    {
        static_cast<ColorPicker *>(getWidget())->closeWindow();
    }

    sigc::connection connectChanged(sigc::slot<void (std::uint32_t)> slot)
    {
        return static_cast<ColorPicker*>(getWidget())->connectChanged(std::move(slot));
    }
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_INKSCAPE_COLOR_PICKER_H

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
