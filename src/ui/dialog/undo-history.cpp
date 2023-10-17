// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Undo History dialog - implementation.
 */
/* Author:
 *   Gustav Broberg <broberg@kth.se>
 *   Abhishek Sharma
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright (C) 2014 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "undo-history.h"

#include <gtkmm/cellrendererpixbuf.h>

#include "actions/actions-tools.h"
#include "document-undo.h"
#include "document.h"
#include "inkscape.h"
#include "ui/pack.h"
#include "util/signal-blocker.h"

namespace Inkscape::UI::Dialog {

const CellRendererInt::Filter &CellRendererInt::no_filter = CellRendererInt::NoFilter();

UndoHistory::UndoHistory()
    : DialogBase("/dialogs/undo-history", "UndoHistory"),
      _event_list_selection(_event_list_view.get_selection())
{
    auto const columns = &EventLog::getColumns();

    set_size_request(-1, -1);

    UI::pack_start(*this, _scrolled_window);
    _scrolled_window.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);

    _event_list_view.set_enable_search(false);
    _event_list_view.set_headers_visible(false);

    auto const icon_renderer = Gtk::make_managed<Gtk::CellRendererPixbuf>();
    icon_renderer->property_xpad() = 2;
    icon_renderer->property_width() = 24;
    int cols_count = _event_list_view.append_column("Icon", *icon_renderer);

    auto const icon_column = _event_list_view.get_column(cols_count - 1);
    icon_column->add_attribute(icon_renderer->property_icon_name(), columns->icon_name);

    auto const children_renderer = Gtk::make_managed<CellRendererInt>(greater_than_1);
    children_renderer->property_weight() = 600; // =Pango::WEIGHT_SEMIBOLD (not defined in old versions of pangomm)
    children_renderer->property_xalign() = 1.0;
    children_renderer->property_xpad() = 2;
    children_renderer->property_width() = 24;

    cols_count = _event_list_view.append_column("Children", *children_renderer);
    auto const children_column = _event_list_view.get_column(cols_count - 1);
    children_column->add_attribute(children_renderer->property_number(), columns->child_count);

    auto const description_renderer = Gtk::make_managed<Gtk::CellRendererText>();
    description_renderer->property_ellipsize() = Pango::EllipsizeMode::END;

    cols_count = _event_list_view.append_column("Description", *description_renderer);
    auto const description_column = _event_list_view.get_column(cols_count - 1);
    description_column->add_attribute(description_renderer->property_text(), columns->description);
    description_column->set_resizable();
    description_column->set_sizing(Gtk::TreeViewColumn::Sizing::AUTOSIZE);
    description_column->set_min_width (150);

    _event_list_view.set_expander_column( *_event_list_view.get_column(cols_count - 1) );

    _scrolled_window.set_child(_event_list_view);
    _scrolled_window.set_overlay_scrolling(false);
    // connect EventLog callbacks
    _callback_connections[EventLog::CALLB_SELECTION_CHANGE] =
        _event_list_selection->signal_changed().connect(sigc::mem_fun(*this, &Inkscape::UI::Dialog::UndoHistory::_onListSelectionChange));

    _callback_connections[EventLog::CALLB_EXPAND] =
        _event_list_view.signal_row_expanded().connect(sigc::mem_fun(*this, &Inkscape::UI::Dialog::UndoHistory::_onExpandEvent));

    _callback_connections[EventLog::CALLB_COLLAPSE] =
        _event_list_view.signal_row_collapsed().connect(sigc::mem_fun(*this, &Inkscape::UI::Dialog::UndoHistory::_onCollapseEvent));
}

UndoHistory::~UndoHistory()
{
    disconnectEventLog();
}

void UndoHistory::documentReplaced()
{
    disconnectEventLog();
    if (auto document = getDocument()) {
        g_assert (document->get_event_log() != nullptr);
        SignalBlocker blocker(&_callback_connections[EventLog::CALLB_SELECTION_CHANGE]);
        _event_list_view.unset_model();
        connectEventLog();
    }
}

void UndoHistory::disconnectEventLog()
{
    if (_event_log) {
        _event_log->removeDialogConnection(&_event_list_view, &_callback_connections);
        _event_log->remove_destroy_notify_callback(this);
    }
}

void UndoHistory::connectEventLog()
{
    if (auto document = getDocument()) {
        _event_log = document->get_event_log();
        _event_log->add_destroy_notify_callback(this, &_handleEventLogDestroyCB);
        _event_list_store = _event_log->getEventListStore();
        _event_list_view.set_model(_event_list_store);
        _event_log->addDialogConnection(&_event_list_view, &_callback_connections);
        _event_list_view.scroll_to_row(_event_list_store->get_path(_event_list_selection->get_selected()));
    }
}

void UndoHistory::_handleEventLogDestroyCB(sigc::notifiable * const data)
{
    if (data) {
        UndoHistory *self = reinterpret_cast<UndoHistory*>(data);
        self->_handleEventLogDestroy();
    }
}

// called *after* _event_log has been destroyed.
void UndoHistory::_handleEventLogDestroy()
{
    if (_event_log) {
        SignalBlocker blocker(&_callback_connections[EventLog::CALLB_SELECTION_CHANGE]);

        _event_list_view.unset_model();
        _event_list_store.reset();
        _event_log = nullptr;
    }
}

void
UndoHistory::_onListSelectionChange()
{
    auto selected = _event_list_selection->get_selected();

    /* If no event is selected in the view, find the right one and select it. This happens whenever
     * a branch we're currently in is collapsed.
     */
    if (!selected) {
        auto curr_event = _event_log->getCurrEvent();
        auto const curr_event_parent = curr_event->parent();

        if (curr_event_parent) {
            _event_log->blockNotifications();

            auto const last = --curr_event_parent->children().end();
            for (; curr_event != last ; ++curr_event ) {
                DocumentUndo::redo(getDocument());
            }

            _event_log->blockNotifications(false);

            _event_log->setCurrEvent(curr_event);
            _event_list_selection->select(curr_event_parent);
        } else {  // this should not happen
            _event_list_selection->select(curr_event);
        }

        return;
    }

    /* Selecting a collapsed parent event is equal to selecting the last child
     * of that parent's branch.
     */
    if ( !selected->children().empty() &&
         !_event_list_view.row_expanded(_event_list_store->get_path(selected)) )
    {
        selected = selected->children().end();
        --selected;
    }

    // An event before the current one has been selected. Undo to the selected event.
    auto last_selected = _event_log->getCurrEvent();
    if ( _event_list_store->get_path(selected) <
         _event_list_store->get_path(last_selected) )
    {
        _event_log->blockNotifications();

        while ( selected != last_selected ) {
            DocumentUndo::undo(getDocument());

            if ( last_selected->parent() &&
                 last_selected == last_selected->parent()->children().begin() )
            {
                last_selected = last_selected->parent();
                _event_log->setCurrEventParent({});
            } else {
                --last_selected;
                if ( !last_selected->children().empty() ) {
                    _event_log->setCurrEventParent(last_selected);
                    last_selected = last_selected->children().end();
                    --last_selected;
                }
            }
        }

        _event_log->blockNotifications(false);

        _event_log->updateUndoVerbs();
    } else { // An event after the current one has been selected. Redo to the selected event.
        _event_log->blockNotifications();

        while (last_selected && selected != last_selected ) {
            DocumentUndo::redo(getDocument());

            if ( !last_selected->children().empty() ) {
                _event_log->setCurrEventParent(last_selected);
                last_selected = last_selected->children().begin();
            } else {
                ++last_selected;

                if ( last_selected->parent() &&
                     last_selected == last_selected->parent()->children().end() )
                {
                    last_selected = last_selected->parent();
                    ++last_selected;
                    _event_log->setCurrEventParent({});
                }
            }
        }

        _event_log->blockNotifications(false);

    }

    _event_log->setCurrEvent(selected);
    _event_log->updateUndoVerbs();
}

void
UndoHistory::_onExpandEvent(const Gtk::TreeModel::iterator &iter, const Gtk::TreeModel::Path &/*path*/)
{
    if ( iter == _event_list_selection->get_selected() ) {
        _event_list_selection->select(_event_log->getCurrEvent());
    }
}

void
UndoHistory::_onCollapseEvent(const Gtk::TreeModel::iterator &iter, const Gtk::TreeModel::Path &/*path*/)
{
    // Collapsing a branch we're currently in is equal to stepping to the last event in that branch
    auto const curr_event_parent = _event_log->getCurrEvent();
    if (iter == curr_event_parent) {
        _event_log->blockNotifications();

        DocumentUndo::redo(getDocument());

        auto curr_event = curr_event_parent->children().begin();
        auto const last = --curr_event_parent->children().end();
        for (; curr_event != last ; ++curr_event) {
            DocumentUndo::redo(getDocument());
        }

        _event_log->blockNotifications(false);

        _event_log->setCurrEvent(curr_event);
        _event_log->setCurrEventParent(curr_event_parent);
        _event_list_selection->select(curr_event_parent);
    }
}

const CellRendererInt::Filter &UndoHistory::greater_than_1 = UndoHistory::GreaterThan(1);

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
