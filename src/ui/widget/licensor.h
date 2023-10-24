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

#ifndef INKSCAPE_UI_WIDGET_LICENSOR_H
#define INKSCAPE_UI_WIDGET_LICENSOR_H

#include <memory>
#include <vector>
#include <gtkmm/box.h>

class SPDocument;
struct rdf_license_t;

namespace Gtk {
class CheckButton;
} // namespace Gtk

namespace Inkscape::UI::Widget {

class EntityEntry;
class LicenseItem;
class Registry;

/**
 * Widget for specifying a document's license; part of document
 * preferences dialog.
 */
class Licensor final : public Gtk::Box {
public:
    Licensor();
    ~Licensor() final;

    void init (Registry&);
    LicenseItem *add_item(Registry &wr, rdf_license_t const &license,
                          Gtk::CheckButton *group);
    void update (SPDocument *doc);

private:
    std::unique_ptr<EntityEntry> _eentry;
    std::vector<LicenseItem *> _items;
};

} // namespace Inkscape::UI::Widget

#endif // INKSCAPE_UI_WIDGET_LICENSOR_H

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
