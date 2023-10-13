// SPDX-License-Identifier: GPL-2.0-or-later

#include "choose-file.h"

#include <glib/gi18n.h>
#include <gtkmm/filechooser.h>
#include <gtkmm/filechooserdialog.h>
#include <glibmm/miscutils.h>

#include "ui/dialog-run.h"

namespace Inkscape {

std::string choose_file_save(Glib::ustring const &title, Gtk::Window *parent,
                             Glib::ustring const &mime_type,
                             Glib::ustring const &file_name,
                             std::string &current_folder)
{
    if (!parent) return {};

    if (current_folder.empty()) {
        current_folder = Glib::get_home_dir();
    }

    Gtk::FileChooserDialog dlg(*parent, title, Gtk::FileChooser::Action::SAVE);
    constexpr int save_id = Gtk::ResponseType::OK;
    dlg.add_button(_("Cancel"), Gtk::ResponseType::CANCEL);
    dlg.add_button(_("Save"), save_id);
    dlg.set_default_response(save_id);
    auto filter = Gtk::FileFilter::create();
    filter->add_mime_type(mime_type);
    dlg.set_filter(filter);
    dlg.set_current_folder(current_folder);
    dlg.set_current_name(file_name);
    dlg.set_do_overwrite_confirmation();

    auto id = UI::dialog_run(dlg);
    if (id != save_id) return {};

    auto fname = dlg.get_filename();
    if (fname.empty()) return {};

    current_folder = dlg.get_current_folder();

    return fname;
}

std::string _choose_file_open(Glib::ustring const &title, Gtk::Window *parent,
                              std::vector<std::pair<Glib::ustring, Glib::ustring>> const &filters,
                              std::vector<Glib::ustring> const &mime_types,
                              std::string &current_folder)
{

    if (!parent) return {};

    if (current_folder.empty()) {
        current_folder = Glib::get_home_dir();
    }

    Gtk::FileChooserDialog dlg(*parent, title, Gtk::FileChooser::Action::OPEN);
    constexpr int open_id = Gtk::ResponseType::OK;
    dlg.add_button(_("Cancel"), Gtk::ResponseType::CANCEL);
    dlg.add_button(_("Open"), open_id);
    dlg.set_default_response(open_id);

    if (!filters.empty()) {
        auto all_supported = Gtk::FileFilter::create();
        all_supported->set_name(_("All Supported Formats"));
        if (filters.size() > 1) dlg.add_filter(all_supported);

        for (auto const &f : filters) {
            auto filter = Gtk::FileFilter::create();
            filter->set_name(f.first);
            filter->add_pattern(f.second);
            all_supported->add_pattern(f.second);
            dlg.add_filter(filter);
        }
    }
    else {
        auto filter = Gtk::FileFilter::create();
        for (auto const &t : mime_types) {
            filter->add_mime_type(t);
        }
        dlg.set_filter(filter);
    }

    dlg.set_current_folder(current_folder);

    auto id = UI::dialog_run(dlg);
    if (id != open_id) return {};

    auto fname = dlg.get_filename();
    if (fname.empty()) return {};

    current_folder = dlg.get_current_folder();

    return fname;
}

std::string choose_file_open(Glib::ustring const &title, Gtk::Window *parent,
                             std::vector<Glib::ustring> const &mime_types,
                             std::string &current_folder)
{
    return _choose_file_open(title, parent, {}, mime_types, current_folder);
}

std::string choose_file_open(Glib::ustring const &title, Gtk::Window *parent,
                             std::vector<std::pair<Glib::ustring, Glib::ustring>> const &filters,
                             std::string &current_folder)
{
    return _choose_file_open(title, parent, filters, {}, current_folder);
}

} // namespace Inkscape

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:
