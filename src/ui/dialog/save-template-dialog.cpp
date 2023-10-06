// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * TODO: insert short description here
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "save-template-dialog.h"

#include <glibmm/i18n.h>
#include <gtkmm/builder.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/dialog.h>
#include <gtkmm/entry.h>
#include <gtkmm/window.h>

#include "file.h"
#include "io/resource.h"
#include "ui/builder-utils.h"
#include "ui/dialog-run.h"

namespace Inkscape::UI::Dialog {

SaveTemplate::SaveTemplate(Gtk::Window &parent)
  : builder(UI::create_builder("dialog-save-template.glade"))
  , dialog               (UI::get_widget<Gtk::Dialog>     (builder, "dialog"))
  , name                 (UI::get_widget<Gtk::Entry>      (builder, "name"))
  , author               (UI::get_widget<Gtk::Entry>      (builder, "author"))
  , description          (UI::get_widget<Gtk::Entry>      (builder, "description"))
  , keywords             (UI::get_widget<Gtk::Entry>      (builder, "keywords"))
  , set_default_template (UI::get_widget<Gtk::CheckButton>(builder, "set-default"))
{
    name.signal_changed().connect(sigc::mem_fun(*this, &SaveTemplate::on_name_changed));

    dialog.add_button(_("Cancel"), Gtk::ResponseType::CANCEL);
    dialog.add_button(_("Save"), Gtk::ResponseType::OK);

    dialog.set_response_sensitive(Gtk::ResponseType::OK, false);
    dialog.set_default_response(Gtk::ResponseType::CANCEL);

    dialog.set_transient_for(parent);
    dialog.set_visible(true);
}

void SaveTemplate::on_name_changed() {

    bool has_text = name.get_text_length() != 0;
    dialog.set_response_sensitive(Gtk::ResponseType::OK, has_text);
}

void SaveTemplate::save_template(Gtk::Window &parent) {

    sp_file_save_template(parent, name.get_text(), author.get_text(), description.get_text(),
        keywords.get_text(), set_default_template.get_active());
}

void SaveTemplate::save_document_as_template(Gtk::Window &parent) {

    SaveTemplate dialog(parent);
    int response = dialog_run(dialog.dialog);

    switch (response) {
    case Gtk::ResponseType::OK:
        dialog.save_template(parent);
        break;
    default:
        break;
    }

    dialog.dialog.close();
}

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
