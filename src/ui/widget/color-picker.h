#ifndef __COLOR_PICKER_H__
#define __COLOR_PICKER_H__

/**
 * \file  Color picker button \& window.
 */
/*
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Ralf Stephan <ralf@ark.in-berlin.de>
 *
 * Copyright (C) Authors 2000-2005
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include <sigc++/sigc++.h>
#include <gtkmm/button.h>
#include <gtkmm/tooltips.h>
#include "ui/widget/color-preview.h"

class SPColorSelector;
namespace Inkscape {
    namespace UI {
        namespace Widget {
            class ColorPickerWindow;

class ColorPicker : public Gtk::Button {
public:
    ColorPicker (const Glib::ustring& title, const Glib::ustring& tip, const guint32 rgba, bool undo);
    virtual ~ColorPicker();
    void setRgba32 (guint32 rgba);
    sigc::connection connectChanged (const sigc::slot<void,guint>& slot) 
        { return _changed_signal.connect (slot); }

protected:
    friend void sp_color_picker_color_mod(SPColorSelector *csel, GObject *cp);
    virtual void on_clicked();
    virtual void on_changed (guint32);
    
    ColorPreview        _preview;
    Gtk::Tooltips       _tt;
    ColorPickerWindow   *_window;
    
    Glib::ustring       *_title;
    sigc::signal<void,guint32> _changed_signal;
    guint32             _rgba;
    bool                _undo;
};

}}}

#endif /* !__COLOR_PICKER_H__ */

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=99 :
