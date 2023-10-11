// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A class that can be inherited to access the GTK4 Widget.css_changed vfunc, not wrapped in gtkmm4
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "ui/widget/css-changed-class-init.h"

#include <unordered_map>
#include <glib.h>
#include <gtk/gtk.h>

namespace Inkscape::UI::Widget {

namespace {

using CssChangedFunc = void (GtkWidget *, GtkCssStyleChange *);
// Save the original css_changed vfunc implementation, as weʼll still need to chain up to it later.
std::unordered_map<GtkWidgetClass const *, CssChangedFunc *> originals;
// We need to map C instance pointers to C++ ones to call ours.
std::unordered_map<GObject *, CssChangedClassInit *> instances;

extern "C"
{

// Save the original css_changed vfunc implementation, as weʼll still need to chain up to it later.
static void class_init(void * const g_class, void * class_data)
{
    g_return_if_fail(GTK_IS_WIDGET_CLASS(g_class));
    auto const klass = static_cast<GtkWidgetClass *>(g_class);
    auto const call_cpp = reinterpret_cast<CssChangedFunc *>(class_data);
    auto const original = std::exchange(klass->css_changed, call_cpp);
    g_assert(original);
    [[maybe_unused]] auto const [it, inserted] = originals.try_emplace(klass, original);
    g_assert(inserted);
}

static void instance_init(GTypeInstance * const instance, void * /* g_class */)
{
    g_return_if_fail(GTK_IS_WIDGET(instance));
}

} // extern "C"

} // anonymous namespace

CssChangedClassInit::CssChangedClassInit()
    : Glib::ExtraClassInit{class_init,
                           reinterpret_cast<void *>(&CssChangedClassInit::_css_changed),
                           instance_init}
{
    [[maybe_unused]] auto const [it, inserted] = instances.emplace(gobj(), this);
    g_assert(inserted);
}

CssChangedClassInit::~CssChangedClassInit()
{
    [[maybe_unused]] auto const count = instances.erase(gobj());
    g_assert(count == 1);
}

void CssChangedClassInit::_css_changed(GtkWidget * const widget, GtkCssStyleChange * const change)
{
    g_return_if_fail(GTK_IS_WIDGET(widget));
    auto const klass = GTK_WIDGET_GET_CLASS(widget);

    // Call the original, C, css_changed vfunc implementation
    auto const &original = originals[klass];
    g_assert(original);
    original(widget, change);

    // Get our C++ instance and call our C++ virtual function
    auto const gobj = G_OBJECT(widget);
    g_assert(gobj);
    auto const self = instances[gobj];
    if (!self) {
        // probably indicates error. https://gitlab.gnome.org/GNOME/gtkmm/-/issues/147#note_1862470
        g_warning("_css_changed called after C++ wrapper deleted, but underlying C instance not");
        return;
    }
    self->css_changed(change);
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
