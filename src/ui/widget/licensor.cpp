// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Widget for specifying a document's license; part of document
 * preferences dialog.
 */
/*
 * Authors:
 *   bulia byak <buliabyak@users.sf.net>
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Jon Phillips <jon@rejon.org>
 *   Ralf Stephan <ralf@ark.in-berlin.de> (Gtkmm)
 *   Abhishek Sharma
 *
 * Copyright (C) 2000 - 2005 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "licensor.h"

#include <algorithm>
#include <cassert>
#include <gtkmm/entry.h>
#include <gtkmm/checkbutton.h>

#include "desktop.h"
#include "document.h"
#include "document-undo.h"
#include "rdf.h"
#include "ui/pack.h"
#include "ui/widget/entity-entry.h"
#include "ui/widget/registry.h"

namespace Inkscape::UI::Widget {

const struct rdf_license_t _proprietary_license = 
  {_("Proprietary"), "", nullptr};

const struct rdf_license_t _other_license = 
  {Q_("MetadataLicence|Other"), "", nullptr};

class LicenseItem final : public Gtk::CheckButton {
public:
    LicenseItem (struct rdf_license_t const* license, EntityEntry* entity, Registry &wr, Gtk::CheckButton *group);
    [[nodiscard]] rdf_license_t const *get_license() const { return _lic; }

private:
    void on_toggled() final;
    struct rdf_license_t const *_lic;
    EntityEntry                *_eep;
    Registry                   &_wr;
};

LicenseItem::LicenseItem (struct rdf_license_t const* license, EntityEntry* entity, Registry &wr, Gtk::CheckButton *group)
: Gtk::CheckButton(_(license->name)), _lic(license), _eep(entity), _wr(wr)
{
    if (group) {
        set_group (*group);
    }
}

/// \pre it is assumed that the license URI entry is a Gtk::Entry
void LicenseItem::on_toggled()
{
    if (_wr.isUpdating() || !_wr.desktop())
        return;

    _wr.setUpdating (true);
    SPDocument *doc = _wr.desktop()->getDocument();
    rdf_set_license (doc, _lic->details ? _lic : nullptr);
    if (doc->isSensitive()) {
        DocumentUndo::done(doc, _("Document license updated"), "");
    }
    _wr.setUpdating (false);
    static_cast<Gtk::Entry*>(_eep->_packable)->set_text (_lic->uri);
    _eep->on_changed();
}

Licensor::Licensor()
: Gtk::Box{Gtk::Orientation::VERTICAL, 4}
{
}

Licensor::~Licensor() = default;

void Licensor::init (Registry& wr)
{
    /* add license-specific metadata entry areas */
    rdf_work_entity_t* entity = rdf_find_entity ( "license_uri" );
    _eentry.reset(EntityEntry::create(entity, wr));

    wr.setUpdating (true);

    auto const item = add_item(wr, _proprietary_license, nullptr);
    item->set_active(true);

    for (auto license = rdf_licenses; license && license->name; ++license) {
        add_item(wr, *license, item);
    }

    // add Other at the end before the URI field for the confused ppl.
    add_item(wr, _other_license, item);

    wr.setUpdating (false);

    auto const box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
    UI::pack_start(*this, *box, true, true, 0);
    UI::pack_start(*box, _eentry->_label, false, false, 5);
    UI::pack_start(*box, *_eentry->_packable, true, true, 0);
}

LicenseItem *Licensor::add_item(Registry &wr, rdf_license_t const &license,
                                Gtk::CheckButton * const group)
{
    assert(_eentry);
    auto const item = Gtk::make_managed<LicenseItem>(&license, _eentry.get(), wr, group);
    append(*item);
    _items.push_back(item);
    return item;
}

void Licensor::update(SPDocument *doc)
{
    assert(_eentry);
    assert(!_items.empty());

    /* identify the license info */
    constexpr bool read_only = false;
    auto const license = rdf_get_license(doc, read_only);

    // Set the active licenseʼs button to active/checked.
    auto item = std::find_if(_items.begin(), _items.end(),
                             [=](auto const item){ return item->get_license() == license; });
    // If we canʼt match license, just activate 1st.
    if (item == _items.end()) item = _items.begin();
    (*item)->set_active(true);

    /* update the URI */
    _eentry->update(doc, read_only);
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
