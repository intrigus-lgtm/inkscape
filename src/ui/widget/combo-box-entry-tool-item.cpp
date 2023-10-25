// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A class derived from Gtk::ToolItem that wraps a GtkComboBoxEntry.
 * Features:
 *   Setting GtkEntryBox width in characters.
 *   Passing a function for formatting cells.
 *   Displaying a warning if entry text isn't in list.
 *   Check comma separated values in text against list. (Useful for font-family fallbacks.)
 *   Setting names for GtkComboBoxEntry and GtkEntry (actionName_combobox, actionName_entry)
 *     to allow setting resources.
 *
 * Author(s):
 *   Tavmjong Bah
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright (C) 2010 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

/*
 * We must provide for both a toolbar item and a menu item.
 * As we don't know which widgets are used (or even constructed),
 * we must keep track of things like active entry ourselves.
 */

#include "combo-box-entry-tool-item.h"

#include <cassert>
#include <cstring>
#include <utility>
#include <gdk/gdkkeysyms.h>
#include <glibmm/main.h>
#include <glibmm/regex.h>
#include <gdkmm/display.h>
#include <gtkmm/combobox.h>
#include <gtkmm/entry.h>
#include <gtkmm/eventcontrollerkey.h>

#include "libnrtype/font-lister.h"
#include "ui/controller.h"
#include "ui/icon-names.h"

namespace Inkscape::UI::Widget {

ComboBoxEntryToolItem::ComboBoxEntryToolItem(Glib::ustring name,
                                             Glib::ustring label,
                                             Glib::ustring tooltip,
                                             Glib::RefPtr<Gtk::TreeModel> model,
                                             int           entry_width,
                                             int           extra_width,
                                             CellDataFunc  cell_data_func,
                                             SeparatorFunc separator_func,
                                             Gtk::Widget  *focusWidget)
    : _label(std::move(label))
    , _tooltip(std::move(tooltip))
    , _model(std::move(model))
    , _combobox(_model, true)
    , _entry_width(entry_width)
    , _extra_width(extra_width)
    , _cell_data_func(std::move(cell_data_func))
    , _focusWidget(focusWidget)
{
    set_name(name);

    _combobox.set_entry_text_column(0);
    _combobox.set_name(name + "_combobox"); // Name it so we can muck with it using an RC file
    _combobox.set_halign(Gtk::Align::START);
    _combobox.set_hexpand(false);
    _combobox.set_vexpand(false);
    append(_combobox);
    _combobox.set_active(false); // ink_comboboxentry_action->active
    _combobox.signal_changed().connect([this] { combo_box_changed_cb(); });

    // Optionally add separator function...
    if (separator_func) {
        _combobox.set_row_separator_func(separator_func);
    }

    // Optionally add formatting...
    if (_cell_data_func) {
        _combobox.set_popup_fixed_width(false);
        _cell.emplace();
        _cell->set_fixed_size(-1, 30);
        _combobox.clear();
        _combobox.pack_start(*_cell, true);
        _combobox.set_cell_data_func(*_cell, [this] (auto &iter) { _cell_data_func(*_cell, iter, false); }); // without markup
        g_signal_connect(_combobox.Glib::Object::gobj(), "popup", G_CALLBACK(+[] (GtkComboBox *, ComboBoxEntryToolItem *self) { // Fixme: Why is there no Gtkmm signal?
            self->idle_conn = Glib::signal_idle().connect([self] {
                self->_combobox.set_cell_data_func(*self->_cell, [self] (auto &iter) { self->_cell_data_func(*self->_cell, iter, true); }); // with markup
                return false; // one-shot
             });
        }), this);
    }

    // Optionally widen the combobox width... which widens the drop-down list in list mode.
    if (_extra_width > 0) {
        Gtk::Requisition req, ignore;
        _combobox.get_preferred_size(req, ignore);
        _combobox.set_size_request(req.get_width() + _extra_width, -1);
    }

    _entry = dynamic_cast<Gtk::Entry *>(_combobox.get_first_child());
    if (_entry) {
        _entry->set_name(name + "_entry"); // Name it so we can muck with it using an RC file

        // Change width
        if (_entry_width > 0) {
            _entry->set_width_chars(_entry_width);
        }

        // Add pop-up entry completion if required
        if (_popup) {
            popup_enable();
        }

        // Add signal for GtkEntry to check if finished typing.
        _entry->signal_activate().connect(sigc::mem_fun(*this, &ComboBoxEntryToolItem::entry_activate_cb));
        Controller::add_key<&ComboBoxEntryToolItem::keypress_cb>(*_entry, *this);
    }

    set_tooltip(_tooltip.c_str());
}

// Setters/Getters ---------------------------------------------------

/*
 * For the font-family list we need to handle two cases:
 *   Text is in list store:
 *     In this case we use row number as the font-family list can have duplicate
 *     entries, one in the document font part and one in the system font part. In
 *     order that scrolling through the list works properly we must distinguish
 *     between the two.
 *   Text is not in the list store (i.e. default font-family is not on system):
 *     In this case we have a row number of -1, and the text must be set by hand.
 */
bool ComboBoxEntryToolItem::set_active_text(Glib::ustring text, int row)
{
    _text = std::move(text);

    // Get active row or -1 if none
    if (row < 0) {
        row = get_active_row_from_text(_text);
    }
    _active = row;

    _combobox.set_active(_active);

    // Fiddle with entry
    if (_entry) {
        // Explicitly set text in GtkEntry box (won't be set if text not in list).
        _entry->set_text(_text);

        // Show or hide warning  -- this might be better moved to text-toolbox.cpp
        if (_info_cb_id && !_info_cb_blocked) {
            _info_cb_id.block();
            _info_cb_blocked = true;
        }
        if (_warning_cb_id && !_warning_cb_blocked) {
            _warning_cb_id.block();
            _warning_cb_blocked = true;
        }

        bool set = false;
        if (!_warning.empty()) {
            Glib::ustring missing = check_comma_separated_text();
            if (!missing.empty()) {
                _entry->set_icon_from_icon_name(INKSCAPE_ICON("dialog-warning"), Gtk::Entry::IconPosition::SECONDARY);
                // Can't add tooltip until icon set
                auto const warning = _warning + ": " + missing;
                _entry->set_icon_tooltip_text(_warning + ": " + missing, Gtk::Entry::IconPosition::SECONDARY);

                if (_warning_cb) {
                    // Add callback if we haven't already
                    if (!_warning_cb_id) {
                        _warning_cb_id = _entry->signal_icon_press().connect([this] (auto) { _warning_cb(*_entry); });
                    }
                    // Unblock signal
                    if (_warning_cb_blocked) {
                        _warning_cb_id.unblock();
                        _warning_cb_blocked = false;
                    }
                }
                set = true;
            }
        }

        if (!set && !_info.empty()) {
            _entry->set_icon_from_icon_name(INKSCAPE_ICON("edit-select-all"), Gtk::Entry::IconPosition::SECONDARY);
            _entry->set_icon_tooltip_text(_info, Gtk::Entry::IconPosition::SECONDARY);

            if (_info_cb) {
                // Add callback if we haven't already
                if (!_info_cb_id) {
                    _info_cb_id = _entry->signal_icon_press().connect([this] (auto) { _info_cb(*_entry); });
                }
                // Unblock signal
                if (_info_cb_blocked) {
                    _info_cb_id.unblock();
                    _info_cb_blocked = false;
                }
            }
            set = true;
        }

        if (!set) {
            _entry->unset_icon(Gtk::Entry::IconPosition::SECONDARY);
        }
    }

    // Return if active text in list
    return _active != -1;
}

void ComboBoxEntryToolItem::set_entry_width(int entry_width)
{
    // Clamp to limits
    _entry_width = std::clamp(entry_width, -1, 100);

    // Widget may not have been created....
    if (_entry) {
        _entry->set_width_chars(_entry_width);
    }
}

void ComboBoxEntryToolItem::set_extra_width(int extra_width)
{
    // Clamp to limits
    _extra_width = std::clamp(extra_width, -1, 500);

    Gtk::Requisition req, ignore;
    _combobox.get_preferred_size(req, ignore);
    _combobox.set_size_request(req.get_width() + _extra_width, -1);
}

void ComboBoxEntryToolItem::focus_on_click(bool focus_on_click)
{
    _combobox.set_focus_on_click(focus_on_click);
}

void ComboBoxEntryToolItem::popup_enable()
{
    _popup = true;

    // Widget may not have been created....
    if (!_entry) {
        return;
    }

    // Check we don't already have a GtkEntryCompletion
    if (_entry_completion) {
        return;
    }

    _entry_completion = Gtk::EntryCompletion::create();

    _entry->set_completion(_entry_completion);
    _entry_completion->set_model(_model);
    _entry_completion->set_text_column(0);
    _entry_completion->set_popup_completion(true);
    _entry_completion->set_inline_completion(false);
    _entry_completion->set_inline_selection(true);

    _entry_completion->signal_match_selected().connect(sigc::mem_fun(*this, &ComboBoxEntryToolItem::match_selected_cb), true);
}

void ComboBoxEntryToolItem::popup_disable()
{
    _popup = false;
    _entry_completion.reset();
}

void ComboBoxEntryToolItem::set_tooltip(Glib::ustring const &tooltip)
{
    set_tooltip_text(tooltip);
    _combobox.set_tooltip_text(tooltip);

    // Widget may not have been created....
    if (_entry) {
        _entry->set_tooltip_text(tooltip);
    }
}

void ComboBoxEntryToolItem::set_info(Glib::ustring info)
{
    _info = std::move(info);

    // Widget may not have been created....
    if (_entry) {
        _entry->set_icon_tooltip_text(_info, Gtk::Entry::IconPosition::SECONDARY);
    }
}

void ComboBoxEntryToolItem::set_info_cb(InfoCallback info_cb)
{
    _info_cb = info_cb;
}

void ComboBoxEntryToolItem::set_warning(Glib::ustring warning)
{
    _warning = std::move(warning);

    // Widget may not have been created....
    if (_entry) {
        _entry->set_icon_tooltip_text(_warning, Gtk::Entry::IconPosition::SECONDARY);
    }
}

void ComboBoxEntryToolItem::set_warning_cb(InfoCallback warning_cb)
{
    _warning_cb = warning_cb;
}

// Internal ---------------------------------------------------

// Return row of active text or -1 if not found. If exclude is true,
// use 3d column if available to exclude row from checking (useful to
// skip rows added for font-families included in doc and not on
// system)
int ComboBoxEntryToolItem::get_active_row_from_text(Glib::ustring const &target_text, bool exclude, bool ignore_case) const
{
    auto fontlister = FontLister::get_instance();
    bool is_font_list = _model.get() == fontlister->get_font_list().get(); // Todo: (GTK4) Remove .get().

    // Check if text in list
    int row = 0;
    for (auto iter : _model->children()) {
        // See if we should exclude a row
        if (exclude && is_font_list && !iter.get_value(fontlister->font_list.onSystem)) {
            continue;
        }

        // Get text from list entry
        Glib::ustring text;
        iter.get_value(0, text);

        if (!ignore_case) {
            // Case sensitive compare
            if (text == target_text) {
                return row;
            }
        } else {
            // Case insensitive compare
            if (text.casefold() == target_text.casefold()) {
                return row;
            }
        }

        ++row;
    }

    return -1;
}

// Checks if all comma separated text fragments are in the list and
// returns a ustring with a list of missing fragments.
// This is useful for checking if all fonts in a font-family fallback
// list are available on the system.
//
// This routine could also create a Pango Markup string to show which
// fragments are invalid in the entry box itself. See:
// http://developer.gnome.org/pango/stable/PangoMarkupFormat.html
// However... it appears that while one can retrieve the PangoLayout
// for a GtkEntry box, it is only a copy and changing it has no effect.
//   PangoLayout * pl = gtk_entry_get_layout( entry );
//   pango_layout_set_markup( pl, "NEW STRING", -1 ); // DOESN'T WORK
Glib::ustring ComboBoxEntryToolItem::check_comma_separated_text() const
{
    Glib::ustring missing;

    // Parse fallback_list using a comma as deliminator
    auto tokens = g_strsplit(_text.c_str(), ",", 0);

    bool first = true;
    for (auto it = tokens; *it; ++it) {
        auto token = *it;
        // Remove any surrounding white space.
        g_strstrip(token);

        if (get_active_row_from_text(token, true, true) == -1) {
            missing += token;
            if (first) {
                first = false;
            } else {
                missing += ", ";
            }
        }
    }

    g_strfreev(tokens);

    return missing;
}

// Callbacks ---------------------------------------------------

void ComboBoxEntryToolItem::combo_box_changed_cb()
{
    // Two things can happen to get here:
    //   An item is selected in the drop-down menu.
    //   Text is typed.
    // We only react here if an item is selected.

    // Check if item selected:
    int newActive = _combobox.get_active_row_number();
    if (newActive < 0 || newActive == _active) {
        return;
    }
    _active = newActive;

    if (auto iter = _combobox.get_active()) {
        iter->get_value(0, _text);
        _entry->set_text(_text);
    }

    // Now let the world know
    _signal_changed.emit();
}

// Get text from entry box.. check if it matches a menu entry.
void ComboBoxEntryToolItem::entry_activate_cb()
{
    // Get text
    _text = _entry->get_text();

    // Get row
    _active = get_active_row_from_text(_text);

    // Set active row
    _combobox.set_active(_active);

    // Now let the world know
    _signal_changed.emit();
}

bool ComboBoxEntryToolItem::match_selected_cb(Gtk::TreeModel::iterator const &iter)
{
    if (!_entry) {
        return false;
    }

    // Set text in ToolItem
    iter->get_value(0, _text);

    // Set text in GtkEntry
    _entry->set_text(_text);

    // Get row
    _active = get_active_row_from_text(_text);

    // Set active row
    _combobox.set_active(_active);

    // Now let the world know
    _signal_changed.emit();

    return true;
}

void ComboBoxEntryToolItem::defocus()
{
    if (_focusWidget) {
        _focusWidget->grab_focus();
    }
}

bool ComboBoxEntryToolItem::keypress_cb(GtkEventControllerKey const *, unsigned keyval, unsigned, GdkModifierType)
{
    switch (keyval) {
        case GDK_KEY_Escape:
            defocus();
            return true;

        case GDK_KEY_Tab:
            // Fire activation similar to how Return does, but also return focus to text object
            // itself
            entry_activate_cb();
            defocus();
            return true;

        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            defocus();
            break;
    }

    return false;
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
