// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Gtk builder utilities
 */
/* Authors:
 *   Michael Kowalski <michal_kowalski@hotmail.com>
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2021 Michael Kowalski <michal_kowalski@hotmail.com>
 * Copyright (C) 2023 Daniel Boles
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <stdexcept>
#include <glibmm/error.h>
#include <glibmm/ustring.h>

#include "builder-utils.h"
#include "io/resource.h"

namespace Inkscape {
namespace UI {

namespace Detail {

void throw_missing(const char* object_type, const char* id)
{
    // TODO: GTK4: Include builder->get_current_object()
    auto what = Glib::ustring::compose(
        "Missing %1 `%2` in Gtk::Builder glade/ui resource file", object_type, id);
    throw std::runtime_error{what.raw()};
}

} // namespace Detail

Glib::RefPtr<Gtk::Builder> create_builder(const char* filename) {
    auto glade = Inkscape::IO::Resource::get_filename(Inkscape::IO::Resource::UIS, filename);
    Glib::RefPtr<Gtk::Builder> builder;
    try {
        return Gtk::Builder::create_from_file(glade);
    }
    catch (Glib::Error& ex) {
        g_error("Cannot load glade file: %s", ex.what());
        throw;
    }
}

} } // namespace Inkscape::UI

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
