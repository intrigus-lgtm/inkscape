// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Path parameter for extensions
 *//*
 * Authors:
 *   Patrick Storz <eduard.braun2@gmx.de>
 *
 * Copyright (C) 2019 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "parameter-path.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <utility>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/join.hpp>
#include <glibmm/fileutils.h>
#include <glibmm/i18n.h>
#include <glibmm/miscutils.h>
#include <glibmm/regex.h>
#include <giomm/file.h>
#include <giomm/liststore.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/dialog.h>
#include <gtkmm/entry.h>
#include <gtkmm/error.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/filefilter.h>
#include <gtkmm/label.h>
#include <sigc++/adaptors/bind.h>
#include <sigc++/functors/mem_fun.h>

#include "extension/extension.h"
#include "helper/choose-file.h"
#include "preferences.h"
#include "ui/pack.h"
#include "xml/node.h"

namespace Inkscape::Extension {

ParamPath::ParamPath(Inkscape::XML::Node *xml, Inkscape::Extension::Extension *ext)
    : InxParameter(xml, ext)
{
    // get value
    const char *value = nullptr;
    if (xml->firstChild()) {
        value = xml->firstChild()->content();
    }

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    _value = prefs->getString(pref_name()).raw();

    if (_value.empty() && value) {
        _value = value;
    }

    // parse selection mode
    const char *mode = xml->attribute("mode");
    if (mode) {
        if (!strcmp(mode, "file")) {
            _mode = Mode::file;
        } else if (!strcmp(mode, "files")) {
            _mode = Mode::file;
            _select_multiple = true;
        } else if (!strcmp(mode, "folder")) {
            _mode = Mode::folder;
        } else if (!strcmp(mode, "folders")) {
            _mode = Mode::folder;
            _select_multiple = true;
        } else if (!strcmp(mode, "file_new")) {
            _mode = Mode::file_new;
        } else if (!strcmp(mode, "folder_new")) {
            _mode = Mode::folder_new;
        } else {
            g_warning("Invalid value ('%s') for mode of parameter '%s' in extension '%s'",
                      mode, _name, _extension->get_id());
        }
    }

    // parse filetypes
    const char *filetypes = xml->attribute("filetypes");
    if (filetypes) {
        _filetypes = Glib::Regex::split_simple("," , filetypes);
    }
}

/**
 * A function to set the \c _value.
 *
 * This function sets the internal value, but it also sets the value
 * in the preferences structure.  To put it in the right place \c pref_name() is used.
 *
 * @param  in   The value to set to.
 */
const std::string& ParamPath::set(const std::string &in)
{
    _value = in;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setString(pref_name(), _value);

    return _value;
}

std::string ParamPath::value_to_string() const
{
    if (!Glib::path_is_absolute(_value) && !_value.empty()) {
        return Glib::build_filename(_extension->get_base_directory(), _value);
    } else {
        return _value;
    }
}

void ParamPath::string_to_value(const std::string &in)
{
    _value = in;
}

/** A special type of Gtk::Entry to handle path parameters. */
class ParamPathEntry : public Gtk::Entry {
private:
    ParamPath *_pref;
    sigc::signal<void ()> *_changeSignal;
public:
    /**
     * Build a string preference for the given parameter.
     * @param  pref  Where to get the string from, and where to put it
     *                when it changes.
     */
    ParamPathEntry(ParamPath *pref, sigc::signal<void ()> *changeSignal)
        : Gtk::Entry()
        , _pref(pref)
        , _changeSignal(changeSignal)
    {
        this->set_text(_pref->get());
        this->signal_changed().connect(sigc::mem_fun(*this, &ParamPathEntry::changed_text));
    };
    void changed_text();
};


/**
 * Respond to the text box changing.
 *
 * This function responds to the box changing by grabbing the value
 * from the text box and putting it in the parameter.
 */
void ParamPathEntry::changed_text()
{
    auto data = this->get_text();
    _pref->set(data.c_str());
    if (_changeSignal != nullptr) {
        _changeSignal->emit();
    }
}

/**
 * Creates a text box for the string parameter.
 *
 * Builds a hbox with a label and a text box in it.
 */
Gtk::Widget *ParamPath::get_widget(sigc::signal<void ()> *changeSignal)
{
    if (_hidden) {
        return nullptr;
    }

    auto const hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, GUI_PARAM_WIDGETS_SPACING);
    auto const label = Gtk::make_managed<Gtk::Label>(_text, Gtk::Align::START);
    label->set_visible(true);
    UI::pack_start(*hbox, *label, false, false);

    auto const textbox = Gtk::make_managed<ParamPathEntry>(this, changeSignal);
    textbox->set_visible(true);
    UI::pack_start(*hbox, *textbox, true, true);
    _entry = textbox;

    auto const button = Gtk::make_managed<Gtk::Button>("…");
	button->set_visible(true);
    UI::pack_end(*hbox, *button, false, false);
    button->signal_clicked().connect(sigc::mem_fun(*this, &ParamPath::on_button_clicked));

    hbox->set_visible(true);
    return hbox;
}

/**
 * Create and show the file chooser dialog when the "…" button is clicked
 * Then set the value of the ParamPathEntry holding the current value accordingly
 */
void ParamPath::on_button_clicked()
{
    Glib::ustring dialog_title;
    if (_mode == Mode::file) {
        if (_select_multiple) {
            dialog_title = _("Select existing files");
        } else {
            dialog_title = _("Select existing file");
        }
    } else if (_mode == Mode::folder) {
        if (_select_multiple) {
            dialog_title = _("Select existing folders");
        } else {
            dialog_title = _("Select existing folder");
        }
    } else if (_mode == Mode::file_new) {
        dialog_title = _("Choose file name");
    } else if (_mode == Mode::folder_new) {
        dialog_title = _("Choose folder name");
    } else {
        g_assert_not_reached();
    }
    dialog_title += "…";

    auto const file_dialog = create_file_dialog(dialog_title, _("Select"));

    // set FileFilter according to 'filetype' attribute
    if (!_filetypes.empty() && _mode != Mode::folder && _mode != Mode::folder_new) {
        Glib::RefPtr<Gtk::FileFilter> file_filter = Gtk::FileFilter::create();

        for (auto const &filetype : _filetypes) {
            file_filter->add_pattern(Glib::ustring::compose("*.%1", filetype));
        }

        std::string filter_name = boost::algorithm::join(_filetypes, "+");
        boost::algorithm::to_upper(filter_name);
        file_filter->set_name(filter_name);

        auto file_filters = Gio::ListStore<Gtk::FileFilter>::create();
        file_filters->append(file_filter);
        set_filters(*file_dialog, file_filters);
    }

    // set current file/folder suitable for current value
    // (use basepath of first filename; relative paths are considered relative to .inx file's location)
    if (!_value.empty()) {
        auto first_filename = _value.substr(0, _value.find("|"));

        if (!Glib::path_is_absolute(first_filename)) {
            first_filename = Glib::build_filename(_extension->get_base_directory(), first_filename);
        }

        auto const dirname = Glib::path_get_dirname(first_filename);
        if (Glib::file_test(dirname, Glib::FileTest::IS_DIR)) {
            file_dialog->set_initial_folder(Gio::File::create_for_path(dirname));
        }

        if(_mode == Mode::file_new || _mode == Mode::folder_new) {
            file_dialog->set_initial_name(Glib::path_get_basename(first_filename));
        } else {
            if (Glib::file_test(first_filename, Glib::FileTest::EXISTS)) {
                // TODO: This does not seem to work (at least on Windows)
                // TODO: GTK4: It has been rewritten. Does it work now?
                file_dialog->set_initial_file(Gio::File::create_for_path(first_filename));
            }
        }
    }

    // show dialog and parse result
    // TODO: GTK4: Double-check this all works right once we can run it. It builds fine, but yʼknow
    auto slot = sigc::bind(sigc::mem_fun(*this, &ParamPath::on_file_dialog_response), file_dialog);
    switch (_mode) {
        case Mode::file:
            if (_select_multiple) {
                file_dialog->open_multiple(std::move(slot));
            } else {
                file_dialog->open(std::move(slot));
            }
            break;

        case Mode::folder:
            if (_select_multiple) {
                file_dialog->select_multiple_folders(std::move(slot));
            } else {
                file_dialog->select_folder(std::move(slot));
            }
            break;

        case Mode::file_new:
        case Mode::folder_new:
            file_dialog->save(std::move(slot));
    }
}

using Mode = ParamPath::Mode;

[[nodiscard]] static std::vector<Glib::RefPtr<Gio::File>>
get_files(Glib::RefPtr<Gio::AsyncResult> const &result     ,
          Glib::RefPtr<Gtk::FileDialog > const &file_dialog,
          Mode const mode, bool const select_multiple)
try
{
    switch (mode) {
        case Mode::file:
            if (select_multiple) {
                return file_dialog->open_multiple_finish(result);
            } else {
                return {file_dialog->open_finish(result)};
            }

        case Mode::folder:
            if (select_multiple) {
                return file_dialog->select_multiple_folders_finish(result);
            } else {
                return {file_dialog->select_folder_finish(result)};
            }

        case Mode::file_new:
        case Mode::folder_new:
            return {file_dialog->save_finish(result)};
    }

    std::abort(); // should be unreachable
} catch (Gtk::DialogError const &error) {
    if (error.code() == Gtk::DialogError::Code::DISMISSED) {
        return {}; // This is OK, not an error. Return empty
    }
    throw;
}

void ParamPath::on_file_dialog_response(Glib::RefPtr<Gio::AsyncResult> const &result     ,
                                        Glib::RefPtr<Gtk::FileDialog > const &file_dialog)
{
    auto const files = get_files(result, file_dialog, _mode, _select_multiple);
    if (files.empty()) return;

    std::vector<Glib::ustring> filenames(files.size());
    std::transform(files.cbegin(), files.cend(), filenames.begin(),
                   [](auto const &file){ return file->get_path(); });
    auto const filenames_joined = boost::algorithm::join(filenames, "|");
    _entry->set_text(filenames_joined); // let the ParamPathEntry handle the rest (including setting the preference)
}

} // namespace Inkscape::Extension

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
