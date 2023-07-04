// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Gtk builder utilities
 */
/* Authors:
 *   Michael Kowalski <michal_kowalski@hotmail.com>
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2021 Michael Kowalski
 * Copyright (C) 2023 Daniel Boles
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_BUILDER_UTILS_H
#define SEEN_BUILDER_UTILS_H

#include <utility>
#include <glibmm/refptr.h>
#include <gtkmm/builder.h>

namespace Inkscape {
namespace UI {

// load glade file from share/ui folder and return builder; throws on errors
Glib::RefPtr<Gtk::Builder> create_builder(const char* filename);

namespace Detail {
// Throw a std::runtime_error with text explaining that a widget/object is missing & its ID.
void throw_missing(const char* object_type, const char* id);
} // namespace Detail

// get widget from builder or throw
template<class W> W& get_widget(const Glib::RefPtr<Gtk::Builder>& builder, const char* id) {
    auto const widget = builder->get_widget<W>(id);
    if (!widget) {
        Detail::throw_missing("widget", id);
    }
    return *widget;
}

template<class W, typename... Args>
W& get_derived_widget(const Glib::RefPtr<Gtk::Builder>& builder, const char* id, Args&&... args) {
    auto const widget = builder->get_widget_derived<W>(builder, id, std::forward<Args>(args)...);
    if (!widget) {
        Detail::throw_missing("widget", id);
    }
    return *widget;
}

template<class Ob> Glib::RefPtr<Ob> get_object(Glib::RefPtr<Gtk::Builder>& builder, const char* id) {
    auto const object = std::dynamic_pointer_cast<Ob>(builder->get_object(id));
    if (!object) {
        Detail::throw_missing("object", id);
    }
    return object;
}

/**
 * This version of get_object is needed for Gtk::CellRenderer objects which can not be
 * put into Glib::RefPtr by the compiler, but are somehow passed to us as RefPtrs anyway.
 */
template <class Ob>
Ob &get_object_raw(Glib::RefPtr<Gtk::Builder> &builder, const char *id)
{
    auto object = dynamic_cast<Ob *>(builder->get_object(id).get());
    if (!object) {
        Detail::throw_missing("object", id);
    }
    return *object;
}

} } // namespace Inkscape::UI

#endif // SEEN_BUILDER_UTILS_H

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
