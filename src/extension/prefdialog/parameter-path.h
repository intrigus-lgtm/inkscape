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

#ifndef INK_EXTENSION_PARAM_PATH_H_SEEN
#define INK_EXTENSION_PARAM_PATH_H_SEEN

#include <vector>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>

#include "parameter.h"

namespace Gio {
class AsyncResult;
class File;
} // namespace Gio

namespace Gtk {
class FileDialog;
} // namespace Gtk

namespace Inkscape::Extension {

class ParamPathEntry;

class ParamPath : public InxParameter {
public:
    enum class Mode {
        file, folder, file_new, folder_new
    };

    ParamPath(Inkscape::XML::Node *xml, Inkscape::Extension::Extension *ext);

    /** \brief  Returns \c _value, with a \i const to protect it. */
    const std::string& get() const { return _value; }
    const std::string &set(const std::string &in) override;

    Gtk::Widget *get_widget(sigc::signal<void ()> *changeSignal) override;

    std::string value_to_string() const override;
    void string_to_value(const std::string &in) override;

private:
    /** \brief  Internal value. */
    std::string _value;

    /** selection mode for the file chooser: files or folders? */
    Mode _mode = Mode::file;

    /** selection mode for the file chooser: multiple items? */
    bool _select_multiple = false;

    /** filetypes that should be selectable in file chooser */
    std::vector<Glib::ustring> _filetypes;

    /** pointer to the parameters text entry
      * keep this around, so we can update the value accordingly in \a on_button_clicked() */
    ParamPathEntry *_entry;

    void on_button_clicked();
    void on_file_dialog_response(Glib::RefPtr<Gio::AsyncResult> const &result     ,
                                 Glib::RefPtr<Gtk::FileDialog > const &file_dialog);
};

} // namespace Inkscape::Extension

#endif /* INK_EXTENSION_PARAM_PATH_H_SEEN */

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
