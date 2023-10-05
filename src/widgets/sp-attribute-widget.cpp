// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Widget that listens and modifies repr attributes.
 */
/* Authors:
 *  Lauris Kaplinski <lauris@ximian.com>
 *  Abhishek Sharma
 *  Kris De Gussem <Kris.DeGussem@gmail.com>
 *
 * Copyright (C) 2001 Ximian, Inc.
 * Copyright (C) 2002,2011-2012 authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "sp-attribute-widget.h"

#include <glibmm/i18n.h>
#include <gtkmm/entry.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <sigc++/adaptors/bind.h>

#include "document-undo.h"
#include "object/sp-object.h"
#include "xml/node.h"

using Inkscape::DocumentUndo;

/**
 * Callback for user input in one of the entries.
 *
 * sp_attribute_table_entry_changed set the object property
 * to the new value and updates history. It is a callback from
 * the entries created by SPAttributeTable.
 * 
 * @param editable pointer to the entry box.
 * @param spat pointer to the SPAttributeTable instance.
 */
static void sp_attribute_table_entry_changed (Gtk::Entry *editable, SPAttributeTable *spat);
/**
 * Callback for a modification of the selected object (size, color, properties, etc.).
 *
 * sp_attribute_table_object_modified rereads the object properties
 * and shows the values in the entry boxes. It is a callback from a
 * connection of the SPObject.
 * 
 * @param object the SPObject to which this instance is referring to.
 * @param flags gives the applied modifications
 * @param spat pointer to the SPAttributeTable instance.
 */
static void sp_attribute_table_object_modified(SPObject *object, unsigned flags, SPAttributeTable *spaw);
/**
 * Callback for the deletion of the selected object.
 *
 * sp_attribute_table_object_release invalidates all data of 
 * SPAttributeTable and disables the widget.
 */
static void sp_attribute_table_object_release (SPObject */*object*/, SPAttributeTable *spat);

static constexpr int XPAD = 4;
static constexpr int YPAD = 2;

SPAttributeTable::SPAttributeTable(SPObject * const object,
                                   std::vector<Glib::ustring> const &labels,
                                   std::vector<Glib::ustring> const &attributes,
                                   GtkWidget * const parent)
{
    set_object(object, labels, attributes, parent);
}

void SPAttributeTable::clear()
{
    if (table)
    {
        table.reset();

        _attributes.clear();
        _entries.clear();
    }

    change_object(nullptr);
}

void SPAttributeTable::set_object(SPObject * const object,
                                  std::vector<Glib::ustring> const &labels,
                                  std::vector<Glib::ustring> const &attributes,
                                  GtkWidget * const parent)
{
    g_return_if_fail (!object || !labels.empty() || !attributes.empty());
    g_return_if_fail (labels.size() == attributes.size());

    clear();

    _object = object;

    if (!object) return;

    blocked = true;

    modified_connection = object->connectModified(sigc::bind<2>(&sp_attribute_table_object_modified, this));
    release_connection  = object->connectRelease (sigc::bind<1>(&sp_attribute_table_object_release , this));

    table = std::make_unique<Gtk::Grid>();

    if (parent) {
        gtk_container_add(GTK_CONTAINER(parent), table->Gtk::Widget::gobj());
    }
    
    // Fill rows
    _attributes = attributes;
    for (std::size_t i = 0; i < attributes.size(); ++i) {
        auto const ll = Gtk::make_managed<Gtk::Label>(_(labels[i].c_str()));
        ll->set_halign(Gtk::Align::START);
        ll->set_valign(Gtk::Align::CENTER);
        ll->set_vexpand(false);
        ll->set_margin_end(XPAD);
        ll->set_margin_top(YPAD);
        ll->set_margin_bottom(YPAD);
        table->attach(*ll, 0, i, 1, 1);

        auto const ee = Gtk::make_managed<Gtk::Entry>();
        auto const val = object->getRepr()->attribute(attributes[i].c_str());
        ee->set_text(val ? val : "");
        ee->set_hexpand();
        ee->set_vexpand(false);
        ee->set_margin_start(XPAD);
        ee->set_margin_top(YPAD);
        ee->set_margin_bottom(YPAD);
        table->attach(*ee, 1, i, 1, 1);

        _entries.push_back(ee);

        ee->signal_changed().connect([ee, this](){
            sp_attribute_table_entry_changed(ee, this);
        });
    }

    blocked = false;
}

void SPAttributeTable::change_object(SPObject *object)
{
    if (_object == object) return;

    if (_object)
    {
        modified_connection.disconnect();
        release_connection.disconnect();
        _object = nullptr;
    }

    _object = object;

    if (!_object) return;

    blocked = true;

    // Set up object
    modified_connection = _object->connectModified(sigc::bind<2>(&sp_attribute_table_object_modified, this));
    release_connection  = _object->connectRelease (sigc::bind<1>(&sp_attribute_table_object_release , this));
    for (std::size_t i = 0; i < _attributes.size(); ++i) {
        auto const val = _object->getRepr()->attribute(_attributes[i].c_str());
        _entries[i]->set_text(val ? val : "");
    }
    
    blocked = false;
}

void SPAttributeTable::reread_properties()
{
    blocked = true;
    for (std::size_t i = 0; i < _attributes.size(); ++i) {
        auto const val = _object->getRepr()->attribute(_attributes[i].c_str());
        _entries[i]->set_text(val ? val : "");
    }
    blocked = false;
}

static void sp_attribute_table_object_modified(SPObject */*object*/,
                                               unsigned const flags,
                                               SPAttributeTable *const spat)
{
    if (!(flags & SP_OBJECT_MODIFIED_FLAG)) return;

    auto const &attributes = spat->get_attributes();
    auto const &entries = spat->get_entries();
    for (std::size_t i = 0; i < attributes.size(); ++i) {
        auto const e = entries[i];
        auto const val = spat->_object->getRepr()->attribute(attributes[i].c_str());
        auto const new_text = Glib::ustring{val ? val : ""};
        if (e->get_text() != new_text) {
            // We are different
            spat->blocked = true;
            e->set_text(new_text);
            spat->blocked = false;
        }
    }
}

static void sp_attribute_table_entry_changed(Gtk::Entry * const editable,
                                             SPAttributeTable * const spat)
{
    if (spat->blocked) return;

    auto const &attributes = spat->get_attributes();
    auto const &entries = spat->get_entries();
    for (std::size_t i = 0; i < attributes.size(); ++i) {
        auto const e = entries[i];
        if (editable->Gtk::Widget::gobj() == e->Gtk::Widget::gobj()) {
            spat->blocked = true;
            if (spat->_object) {
                auto const &text = e->get_text ();
                spat->_object->getRepr()->setAttribute(attributes[i], text);
                DocumentUndo::done(spat->_object->document, _("Set attribute"), "");
            }
            spat->blocked = false;
            return;
        }
    }

    g_warning ("file %s: line %d: Entry signalled change, but there is no such entry", __FILE__, __LINE__);
}

static void sp_attribute_table_object_release (SPObject */*object*/, SPAttributeTable *spat)
{
    std::vector<Glib::ustring> labels;
    std::vector<Glib::ustring> attributes;
    spat->set_object (nullptr, labels, attributes, nullptr);
}

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
