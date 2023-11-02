// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A class that can be inherited to access GTK4ʼs Widget.css_changed & .focus vfuncs, not in gtkmm4
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "ui/widget/widget-vfuncs-class-init.h"

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <glib.h>
#include <gtk/gtk.h>

namespace Inkscape::UI::Widget {

namespace {

using CFuncs = WidgetVfuncsClassInit::CFuncs;

// Save the original C vfunc implementations since we (might) still need to chain up to them later.
std::unordered_map<GtkWidgetClass const *, CFuncs> originals;
// We need to map C instance pointers to C++ ones to call ours.
std::unordered_map<GtkWidget *, WidgetVfuncsClassInit *> instances;

extern "C"
{

// Save the original C vfunc implementations since we (might) still need to chain up to them later.
static void class_init(void * const g_class, void * class_data)
{
    g_assert(GTK_IS_WIDGET_CLASS(g_class));
    auto const klass = static_cast<GtkWidgetClass *>(g_class);
    g_assert(class_data);
    auto const &new_funcs = *reinterpret_cast<CFuncs const *>(class_data);
    auto original = CFuncs{};

    original.css_changed = std::exchange(klass->css_changed, new_funcs.css_changed);
    original.focus       = std::exchange(klass->focus      , new_funcs.focus      );

    [[maybe_unused]] auto const [it, inserted] = originals.try_emplace(klass, original);
    g_assert(inserted);
}

// This is needed because gobj() is null in our ctor. Yes, itʼs ugly – but OK as GTK is 1-threaded.
static WidgetVfuncsClassInit *initing_this = nullptr;

static void instance_init(GTypeInstance * const instance, void * /* g_class */)
{
    auto const widget = GTK_WIDGET(instance);
    g_assert(widget);
    [[maybe_unused]] auto const [it, inserted] = instances.emplace(widget, initing_this);
    g_assert(inserted);
    initing_this = nullptr;
}

} // extern "C"

// Get the original CFuncs & the C++ instance of self.
[[nodiscard]] auto get(GtkWidget * const widget)
{
    g_assert(GTK_IS_WIDGET(widget));
    auto const klass = GTK_WIDGET_GET_CLASS(widget);
    g_assert(klass);
    auto const &original = originals[klass];

    auto const self = instances[widget];
    if (!self) {
        // probably indicates error. https://gitlab.gnome.org/GNOME/gtkmm/-/issues/147#note_1862470
        g_warning("WidgetVfuncs has no C++ wrapper, so it was deleted, but C instance was not(?)");
    }

    return std::make_pair(std::cref(original), self);
}

} // anonymous namespace

WidgetVfuncsClassInit::WidgetVfuncsClassInit()
    : Glib::ExtraClassInit{class_init,
                           reinterpret_cast<void *>(const_cast<CFuncs *>(&cfuncs)),
                           instance_init}
{
    g_assert(!initing_this);
    initing_this = this;
}

WidgetVfuncsClassInit::~WidgetVfuncsClassInit()
{
    // gobj() is also null here in our dtor, hence the weird search.
    // map is kept keyed on GtkWidget* as itʼs looked-up more often.
    auto const it = std::find_if(instances.begin(), instances.end(),
                                 [this](auto const &pair){ return pair.second == this; });
    g_assert(it != instances.end());
    instances.erase(it);
}

void WidgetVfuncsClassInit::_css_changed(GtkWidget * const widget,
                                         GtkCssStyleChange * const change)
{
    auto const &[original, self] = get(widget);
    g_assert(original.css_changed);

    // If the vfunc returns nothing we can just call the C++ one after the C one
    // We always call the C one: differs from normal overriding, but much easier

    original.css_changed(widget, change);

    if (self) self->css_changed(change);
}

gboolean WidgetVfuncsClassInit::_focus(GtkWidget * const widget,
                                       GtkDirectionType const direction)
{
    auto const &[original, self] = get(widget);
    g_assert(original.focus);

    // If it returns, itʼs more complex. Use nullopt for ‘donʼt really override’
    // – avoiding sublasses having to state whether they override for each vfunc
    if (self) {
        if (auto const optional = self->focus(Gtk::DirectionType{direction})) {
            return *optional;
        }
    }

    // If so, only call/return from original if we didnʼt have any C++ override.
    return original.focus(widget, direction);
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
