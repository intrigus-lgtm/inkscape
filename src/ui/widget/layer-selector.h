// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Inkscape::UI::Widget::LayerSelector - layer selector widget
 *
 * Authors:
 *   MenTaLguY <mental@rydia.net>
 *
 * Copyright (C) 2004 MenTaLguY
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_INKSCAPE_WIDGETS_LAYER_SELECTOR
#define SEEN_INKSCAPE_WIDGETS_LAYER_SELECTOR

#include <memory>
#include <glibmm/refptr.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <gtkmm/togglebutton.h>

#include "helper/auto-connection.h"
#include "xml/helper-observer.h"

namespace Glib {
class ustring;
} // namespace Glib

namespace Gtk {
class CssProvider;
} // namespace Gtk

class SPDesktop;
class SPDocument;
class SPGroup;

namespace Inkscape::UI::Widget {

class AlternateIcons;

class LayerSelector final : public Gtk::Box {
public:
    LayerSelector(SPDesktop *desktop = nullptr);
    ~LayerSelector() final;

    void setDesktop(SPDesktop *desktop);

private:
    int const _instance_number;

    SPDesktop *_desktop = nullptr;
    SPGroup   *_layer   = nullptr;

    Gtk::ToggleButton _eye_toggle;
    Gtk::ToggleButton _lock_toggle;
    Gtk::Button _layer_name;
    Gtk::Label _layer_label;
    Glib::RefPtr<Gtk::CssProvider> _label_style;

    AlternateIcons *_eye_label  = nullptr;
    AlternateIcons *_lock_label = nullptr;

    auto_connection _layer_changed;
    auto_connection _hide_layer_connection;
    auto_connection _lock_layer_connection;
    std::unique_ptr<Inkscape::XML::SignalObserver> _observer;

    void _layerChanged(SPGroup *layer);
    void _layerModified();
    void _selectLayer();
    void _hideLayer();
    void _lockLayer();
    void _layerChoose();
    Glib::ustring getThisCssClass() const;
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_INKSCAPE_WIDGETS_LAYER_SELECTOR

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
