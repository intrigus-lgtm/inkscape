// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Generic object attribute editor
 *//*
 * Authors:
 * see git history
 * Kris De Gussem <Kris.DeGussem@gmail.com>
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_DIALOGS_OBJECT_ATTRIBUTES_H
#define SEEN_DIALOGS_OBJECT_ATTRIBUTES_H

#include <glibmm/ustring.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/widget.h>
#include <memory>
#include <string>
#include <map>

#include "desktop.h"
#include "object/sp-object.h"
#include "ui/dialog/dialog-base.h"
#include "ui/operation-blocker.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/style-swatch.h"
#include "ui/widget/unit-tracker.h"

class SPAttributeTable;
class SPItem;

namespace Inkscape::UI::Dialog {

namespace details {

class AttributesPanel {
public:
    AttributesPanel();
    virtual ~AttributesPanel() = default;

    void update_panel(SPObject* object, SPDesktop* desktop);
    Gtk::Widget& widget() { if(!_widget) throw "crap"; return *_widget; }
    Glib::ustring get_title() const { return _title; }
    bool supports_fill_stroke() const {return _show_fill_stroke; }

protected:
    virtual void update(SPObject* object) = 0;
    // value with units changed by the user; modify current object
    void change_value_px(SPObject* object, const Glib::RefPtr<Gtk::Adjustment>& adj, const char* attr, std::function<void (double)>&& setter);
    // angle in degrees changed by the user; modify current object
    void change_angle(SPObject* object, const Glib::RefPtr<Gtk::Adjustment>& adj, std::function<void (double)>&& setter);
    // modify current object
    void change_value(SPObject* object, const Glib::RefPtr<Gtk::Adjustment>& adj, std::function<void (double)>&& setter);

    SPDesktop* _desktop = nullptr;
    OperationBlocker _update;
    bool _show_fill_stroke = true;
    Glib::ustring _title;
    Gtk::Widget* _widget = nullptr;
    std::unique_ptr<UI::Widget::UnitTracker> _tracker;
};

} // namespace details

/**
 * A dialog widget to show object attributes (currently for images and links).
 */
class ObjectAttributes : public DialogBase
{
public:
    ObjectAttributes();
    ~ObjectAttributes() override = default;

    void selectionChanged(Selection *selection) override;
    void selectionModified(Selection *selection, guint flags) override;

    void desktopReplaced() override;

    /**
     * Updates entries and other child widgets on selection change, object modification, etc.
     */
    void widget_setup();

private:
    Glib::RefPtr<Gtk::Builder> _builder;

    void create_panels();
    std::map<std::string, std::unique_ptr<details::AttributesPanel>> _panels;
    details::AttributesPanel* get_panel(SPObject* object);
    void update_panel(SPObject* object);

    details::AttributesPanel* _current_panel = nullptr;
    OperationBlocker _update;
    Gtk::Box& _main_panel;
    Gtk::Label& _obj_title;
    // Contains a pointer to the currently selected item (NULL in case nothing is or multiple objects are selected).
    SPItem* _current_item = nullptr;
    Inkscape::UI::Widget::StyleSwatch _style_swatch;
};

} // namespace Inkscape::UI::Dialog

#endif // SEEN_DIALOGS_OBJECT_ATTRIBUTES_H

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
