// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Helpers for using Gtk::Boxes, encapsulating large changes between GTK3 & GTK4
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

// The hilarious pack() herein replicates how GTK3ʼs Box can have start or end-
// packed children, in a way that will be forward-compatible with GTK4, wherein
// Box is far simpler & just prepends/appends to a single group of children. We
// cannot replace pack_start|end() with prepend|append(), since not only do they
// lose the expand/fill args, but also the 2 sets of methods order children in
// reverse order to each other, & GTK4 does not separate the 2 sets of children.
// Here, I fix this by retaining an unordered_map from known Boxes to start-side
// children, adding/removing in same when any start-side child is added/removed…
// then when asked to pack a child at either side, using the count of start-side
// chldren to determine the appropriate position at which to add() that child.
// GTK3 child properties are emulated by normal properties on the child widget.

#include "ui/pack.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <gtkmm/box.h>

#include "helper/auto-connection.h"
#include "ui/util.h"

namespace Inkscape::UI {

enum class PackType {start, end};

using BoxChildren = std::unordered_map<Gtk::Widget *, auto_connection>;
static auto s_box_children = std::unordered_map<Gtk::Box *, BoxChildren>{};

static void set_expand(Gtk::Widget &widget, Gtk::Orientation const orientation,
                       bool const expand)
{
    switch (orientation) {
        case Gtk::Orientation::HORIZONTAL: widget.set_hexpand(expand); break;
        case Gtk::Orientation::VERTICAL  : widget.set_vexpand(expand); break;
        default: std::abort();
    }
}

static void set_align(Gtk::Widget &widget, Gtk::Orientation const orientation,
                      Gtk::Align const align)
{
    switch (orientation) {
        case Gtk::Orientation::HORIZONTAL: widget.set_halign(align); break;
        case Gtk::Orientation::VERTICAL  : widget.set_valign(align); break;
        default: std::abort();
    }
}

[[nodiscard]] static auto to_align(PackType const pack_type)
{
    switch (pack_type) {
        case PackType::start: return Gtk::Align::START;
        case PackType::end  : return Gtk::Align::END  ;
        default: std::abort();
    }
}

static void set_fill(Gtk::Widget &widget, Gtk::Orientation const orientation,
                     bool const fill, PackType const pack_type)
{
    auto const align = fill ? Gtk::Align::FILL : to_align(pack_type);
    set_align(widget, orientation, align);
}

static void set_padding(Gtk::Widget &widget, Gtk::Orientation const orientation,
                        int const margin_start, int const margin_end)
{
    switch (orientation) {
        case Gtk::Orientation::HORIZONTAL:
            widget.set_margin_start(widget.get_margin_start() + margin_start);
            widget.set_margin_end  (widget.get_margin_end  () + margin_end  );
            break;
        case Gtk::Orientation::VERTICAL:
            widget.set_margin_top   (widget.get_margin_top   () + margin_start);
            widget.set_margin_bottom(widget.get_margin_bottom() + margin_end  );
            break;
        default: std::abort();
    }
}

static void add(Gtk::Box &box, PackType const pack_type, Gtk::Widget &child)
{
    auto const [it, inserted] = s_box_children.emplace(&box, BoxChildren{});
    // macOS runner errors if lambda captures structured binding. C++ Defect Report says this is OK
    auto &starts = it->second;

    if (inserted) {
        box.signal_destroy().connect([&]{ s_box_children.erase(&box); });
    }

    if (starts.empty()) {
        box.prepend(child); // Prepend so PackType::end arranges children from end-to-start as GTK3
    } else {
        auto const position = starts.size();
        auto &previous = get_nth_child(box, position - 1);
        box.append(child);
        box.reorder_child_after(child, previous);
    }

    if (pack_type != PackType::start) return;

    // Add the child to our list of start ones, and! connect ::parent changed to remove that later.
    auto const erase_child = [&]{ starts.erase(&child); };
    auto connection = child.property_parent().signal_changed().connect(erase_child);
    starts.emplace(&child, std::move(connection));
}

static void pack(PackType const pack_type,
                 Gtk::Box &box, Gtk::Widget &child, bool const expand, bool const fill,
                 unsigned const padding)
{
    auto const orientation = box.get_orientation();
    set_expand (child, orientation, expand            );
    set_fill   (child, orientation, fill   , pack_type);
    set_padding(child, orientation, padding, padding  );
    add(box, pack_type, child);
}

static void pack(PackType const pack_type,
                 Gtk::Box &box, Gtk::Widget &child, PackOptions const options,
                 unsigned const padding)
{
    auto const expand = options != PackOptions::shrink       ;
    auto const fill   = options == PackOptions::expand_widget;
    pack(pack_type, box, child, expand, fill, padding);
}

void pack_start(Gtk::Box &box, Gtk::Widget &child, bool const expand, bool const fill,
                unsigned const padding)
{
    pack(PackType::start, box, child, expand, fill, padding);
}

void pack_start(Gtk::Box &box, Gtk::Widget &child, PackOptions const options,
                unsigned const padding)
{
    pack(PackType::start, box, child, options, padding);
}

void pack_end(Gtk::Box &box, Gtk::Widget &child, bool const expand, bool const fill,
              unsigned const padding)
{
    pack(PackType::end, box, child, expand, fill, padding);
}

void pack_end(Gtk::Box &box, Gtk::Widget &child, PackOptions const options,
              unsigned const padding)
{
    pack(PackType::end, box, child, options, padding);
}

} // namespace Inkscape::UI

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
