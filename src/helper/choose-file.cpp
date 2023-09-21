// SPDX-License-Identifier: GPL-2.0-or-later

#include "choose-file.h"

#include <glib/gi18n.h>
#include <glibmm/main.h>
#include <glibmm/ustring.h>
#include <giomm/asyncresult.h>
#include <giomm/file.h>
#include <giomm/liststore.h>
#include <gtkmm/error.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/filefilter.h>
#include <glibmm/miscutils.h>

namespace Inkscape {

Glib::RefPtr<Gtk::FileDialog> create_file_dialog(Glib::ustring const &title,
                                                 Glib::ustring const &accept_label)
{
    auto const file_dialog = Gtk::FileDialog::create();
    file_dialog->set_title(title);
    file_dialog->set_accept_label(accept_label);
    return file_dialog;
}

void set_filters(Gtk::FileDialog &file_dialog,
                 Glib::RefPtr<Gio::ListStore<Gtk::FileFilter>> const &filters)
{
    file_dialog.set_filters(filters);
    if (filters->get_n_items() > 0) {
        file_dialog.set_default_filter(filters->get_item(0));
    }
}

void set_filter(Gtk::FileDialog &file_dialog, Glib::RefPtr<Gtk::FileFilter> const &filter)
{
    auto const filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(filter);
    set_filters(file_dialog, filters);
}

using StartMethod = void (Gtk::FileDialog::*)
                    (Gtk::Window &, Gio::SlotAsyncReady const &,
                     Glib::RefPtr<Gio::Cancellable> const &);

using FinishMethod = Glib::RefPtr<Gio::File> (Gtk::FileDialog::*)
                     (Glib::RefPtr<Gio::AsyncResult> const &);

[[nodiscard]] static auto run(Gtk::FileDialog &file_dialog, Gtk::Window &parent,
                              std::string &current_folder,
                              StartMethod const start, FinishMethod const finish)
{
    file_dialog.set_initial_folder(Gio::File::create_for_path(current_folder));

    bool responded = false;
    std::string file_path;

    (file_dialog.*start)(parent, [&](Glib::RefPtr<Gio::AsyncResult> const &result)
    {
        try {
            responded = true;

            auto const file = (file_dialog.*finish)(result);
            if (!file) return;

            file_path = file->get_path();
            if (file_path.empty()) return;

            current_folder = file->get_parent()->get_path();
        } catch (Gtk::DialogError const &error) {
            if (error.code() == Gtk::DialogError::Code::DISMISSED) {
                responded = true;
            } else {
                throw;
            }
        }
    }, Glib::RefPtr<Gio::Cancellable>{});

    for (auto const main_context = Glib::MainContext::get_default();
         !responded && main_context->iteration(false);) {} // Await resp.

    return file_path;
}

std::string choose_file_save(Glib::ustring const &title, Gtk::Window *parent,
                             Glib::ustring const &mime_type,
                             Glib::ustring const &file_name,
                             std::string &current_folder)
{
    if (!parent) return {};

    if (current_folder.empty()) {
        current_folder = Glib::get_home_dir();
    }

    auto const file_dialog = create_file_dialog(title, _("Save"));

    auto filter = Gtk::FileFilter::create();
    filter->add_mime_type(mime_type);
    set_filter(*file_dialog, filter);

    file_dialog->set_initial_name(file_name);

    return run(*file_dialog, *parent, current_folder,
               &Gtk::FileDialog::save, &Gtk::FileDialog::save_finish);
}

static std::string _choose_file_open(Glib::ustring const &title, Gtk::Window *parent,
                                     std::vector<std::pair<Glib::ustring, Glib::ustring>> const &filters,
                                     std::vector<Glib::ustring> const &mime_types,
                                     std::string &current_folder)
{       
    if (!parent) return {};

    if (current_folder.empty()) {
        current_folder = Glib::get_home_dir();
    }

    auto const file_dialog = create_file_dialog(title, _("Open"));

    auto filters_model = Gio::ListStore<Gtk::FileFilter>::create();
    if (!filters.empty()) {
        auto all_supported = Gtk::FileFilter::create();
        all_supported->set_name(_("All Supported Formats"));
        if (filters.size() > 1) filters_model->append(all_supported);

        for (auto const &f : filters) {
            auto filter = Gtk::FileFilter::create();
            filter->set_name(f.first);
            filter->add_pattern(f.second);
            all_supported->add_pattern(f.second);
            filters_model->append(filter);
        }
    }
    else {
        auto filter = Gtk::FileFilter::create();
        for (auto const &t : mime_types) {
            filter->add_mime_type(t);
        }
        filters_model->append(filter);
    }
    set_filters(*file_dialog, filters_model);

    return run(*file_dialog, *parent, current_folder,
               &Gtk::FileDialog::open, &Gtk::FileDialog::open_finish);
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
