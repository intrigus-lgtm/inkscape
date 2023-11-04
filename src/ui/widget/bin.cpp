// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Bin: widget that can hold one child, useful as a base class of custom widgets
 */
/*
 * Authors:
 *   Daniel Boles <dboles.src+inkscape@gmail.com>
 *
 * Copyright (C) 2023 Daniel Boles
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "bin.h"
#include <cassert>

namespace Inkscape::UI::Widget {

Bin::Bin(Gtk::Widget *child)
{
    set_child(child);
    _connectDestroy();
}

Bin::Bin(BaseObjectType *cobject, Glib::RefPtr<Gtk::Builder> const &)
    : Gtk::Widget(cobject)
{
    _connectDestroy();

    // Add child from builder file. (For custom types, C++ wrapper must already be instantiated.)
    _child = get_first_child();
    assert(!_child || !_child->get_next_accessible_sibling());
}

void Bin::_connectDestroy()
{
    // This signal may fire just before destruction to tell us to unparent all children.
    // It can also fire after destruction, which is UB, hence the auto_connection.
    _destroy_conn = signal_destroy().connect([this]{ unset_child(); });
}

Bin::~Bin()
{
    unset_child();
}

void Bin::set_child(Gtk::Widget *child)
{
    if (child == _child || (child && child->get_parent())) {
        return;
    }

    if (_child) {
        _child->unparent();
    }

    _child = child;

    if (_child) {
        _child->set_parent(*this);
    }
}

void Bin::on_size_allocate(int width, int height, int baseline)
{
    Gtk::Widget::size_allocate_vfunc(width, height, baseline);

    if (_child && _child->get_visible()) {
        _child->size_allocate({0, 0, width, height}, baseline);
    }
}

Gtk::SizeRequestMode Bin::get_request_mode_vfunc() const
{
    return _child ? _child->get_request_mode() : Gtk::SizeRequestMode::CONSTANT_SIZE;
}

void Bin::measure_vfunc(Gtk::Orientation orientation, int const for_size,
                        int &minimum, int &natural,
                        int &minimum_baseline, int &natural_baseline) const
{
    if (_child && _child->get_visible()) {
        _child->measure(orientation, for_size,
                        minimum, natural, minimum_baseline, natural_baseline);
    } else {
        minimum = natural = minimum_baseline = natural_baseline = 0;
    }
}

void Bin::size_allocate_vfunc(int const width, int const height, int const baseline)
{
    _signal_before_resize.emit(width, height, baseline);
    on_size_allocate(width, height, baseline);
    _signal_after_resize.emit(width, height, baseline);
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
