// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Undo History dialog
 */
/* Author:
 *   Gustav Broberg <broberg@kth.se>
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright (C) 2014 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_DIALOG_UNDO_HISTORY_H
#define INKSCAPE_UI_DIALOG_UNDO_HISTORY_H

#include <string>
#include <utility>
#include <glibmm/property.h>
#include <glibmm/propertyproxy.h>
#include <glibmm/refptr.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/treeselection.h>

#include "event-log.h"
#include "ui/dialog/dialog-base.h"

namespace Inkscape::UI::Dialog {

/* Custom cell renderers */

class CellRendererInt : public Gtk::CellRendererText {
public:
    struct Filter
    {
        virtual bool operator()(const int&) const = 0;
    };

    CellRendererInt(const Filter &filter = no_filter) :
        Glib::ObjectBase{"CellRendererInt"},
        Gtk::CellRendererText(),
        _property_number(*this, "number", 0),
        _filter (filter)
    {
        auto const set_text = [this]{
            Glib::ustring text;
            if (auto const value = _property_number.get_value(); _filter(value)) {
                text = std::to_string(value);
            }
            property_text().set_value(std::move(text));
        };
        set_text();
        property_number().signal_changed().connect(set_text);
    }

    Glib::PropertyProxy<int>
    property_number() { return _property_number.get_proxy(); }

    static const Filter &no_filter;

private:
    Glib::Property<int> _property_number;
    const Filter &_filter;

    struct NoFilter : Filter { bool operator()(const int &/*x*/) const override { return true; } };
};

/**
 * \brief Dialog for presenting document change history
 *
 * This dialog allows the user to undo and redo multiple events in a more convenient way
 * than repateaded ctrl-z, ctrl-shift-z.
 */
class UndoHistory : public DialogBase
{
public:
    UndoHistory();
    ~UndoHistory() override;

    void documentReplaced() override;

private:
    EventLog *_event_log = nullptr;

    Gtk::ScrolledWindow _scrolled_window;

    Glib::RefPtr<Gtk::TreeModel> _event_list_store;
    Gtk::TreeView _event_list_view;
    Glib::RefPtr<Gtk::TreeSelection> _event_list_selection;

    EventLog::CallbackMap _callback_connections;

    static void _handleEventLogDestroyCB(sigc::notifiable *data);

    void disconnectEventLog();
    void connectEventLog();

    void _handleEventLogDestroy();
    void _onListSelectionChange();
    void _onExpandEvent(const Gtk::TreeModel::iterator &iter, const Gtk::TreeModel::Path &path);
    void _onCollapseEvent(const Gtk::TreeModel::iterator &iter, const Gtk::TreeModel::Path &path);

    struct GreaterThan : CellRendererInt::Filter
    {
        GreaterThan(int _i) : i (_i) {}
        bool operator()(const int &x) const override { return x > i; }
        int i;
    };

    static const CellRendererInt::Filter &greater_than_1;
};

} // namespace Inkscape::UI::Dialog

#endif //INKSCAPE_UI_DIALOG_UNDO_HISTORY_H

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
