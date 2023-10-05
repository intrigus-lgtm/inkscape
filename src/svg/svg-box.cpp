// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Martin Owens <doctormo@geek-2.com>
 *
 * Copyright (C) 2022 Martin Owens
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cstring>
#include <string>
#include <iostream>

#include <glib.h>
#include <glibmm/regex.h>

#include "svg/svg-box.h"
#include "util/units.h"

// Match a side to it's fallback index,
// top->bottom, top->right, right->left
#define FALLBACK(i) ((i - 2 >= 0) ? (i - 2) : 0)

/**
 * An svg box is a type of css/html type which contains up to 4 svg lengths.
 * Usally representing widths, margins, padding of the html box model.
 */
SVGBox::SVGBox()
{}

/**
 * Read in the value, may be an array of four
 */
bool SVGBox::read(const std::string &value, const Geom::Scale &doc_scale)
{
    return fromString(value, "", doc_scale);
}

/**
 * Update box with em, ex and percentage scaling.
 */
void SVGBox::update(double em, double ex, double width, double height)
{
    _value[0].update(em, ex, height);
    _value[1].update(em, ex, width);
    _value[2].update(em, ex, height);
    _value[3].update(em, ex, width);
}

/**
 * Write out the values into a compact form.
 */
std::string SVGBox::write() const
{
    return toString("", Geom::Scale(1));
}

/**
 * Write as specific unit for user display
 */
std::string SVGBox::toString(const std::string &unit, const Geom::Scale &doc_scale, std::optional<unsigned int> precision, bool add_unit) const
{
    std::string ret = "";
    bool write = false;
    for (int i = 3; i >= 0; i--) {
        SVGLength val = _value[i];
        SVGLength fallback = _value[FALLBACK(i)];
        if (i == BOX_TOP || (val != fallback) || write) {
            if (unit.size()) {
                auto axis_scale = doc_scale[get_scale_axis((BoxSide)i)];
                ret = std::string(val.toString(unit, axis_scale, precision, add_unit)) + " " + ret;
            } else {
                ret = std::string(val.write()) + " " + ret;
            }
            write = true;
        }
    }
    ret.pop_back();
    return ret;
}

/**
 * Set the svg box from user input, with a default unit
 */
bool SVGBox::fromString(const std::string &value, const std::string &unit, const Geom::Scale &doc_scale)
{
    if (!value.size()) return false;

    // A. Split by spaces.
    std::vector<Glib::ustring> elements = Glib::Regex::split_simple("\\s*[,\\s]\\s*", value.c_str());
    
    // Take item zero
    for (int i = 0; i < 4; i++) {
        if ((i == BOX_TOP || (int)elements.size() >= i+1) && elements[i].size() > 0) {
            if (!fromString((BoxSide)i, elements[i], unit, doc_scale)) {
                return false; // One position failed.
            }
        } else {
            _value[i] = _value[FALLBACK(i)];
        }
    }

    _is_set = true;
    return true;
}

/**
 * Parse a single side from a string and unit combo (pass through to SVGLength.fromString)
 *
 * @param side - The side of the box to set
 * @param value - The string value entered by the user
 * @param unit - The default units the context is using
 * @param doc_scale - The document scale factor, for when units are being parsed
 */
bool SVGBox::fromString(BoxSide side, const std::string &value, const std::string &unit, const Geom::Scale &doc_scale)
{
    double axis_scale = doc_scale[get_scale_axis(side)];
    return _value[side].fromString(value, unit, axis_scale);
}

/**
 * Returns true if the box is set, but all values are zero
 */
bool SVGBox::isZero() const
{
    return _value[0] == 0.0
        && _value[1] == 0.0
        && _value[2] == 0.0
        && _value[3] == 0.0;
}

/**
 * Set values into this box model.
 */
void SVGBox::set(double top, double right, double bottom, double left) {
    set(BOX_TOP, top);
    set(BOX_RIGHT, right);
    set(BOX_BOTTOM, bottom);
    set(BOX_LEFT, left);
}

/**
 * Set the value of the side, retaining it's original unit.
 *
 * confine - If true, will set any OTHER sides which are the same.
 */
void SVGBox::set(BoxSide side, double px, bool confine) {
    // Unit gets destroyed here delibrately. Units are not ok in the svg.
    SVGLength original = _value[side];
    for (int i = 0; i < 4; i++) {
        if (i == (int)side || (confine && _value[i] == original)) {
            _value[i].set(SVGLength::PX, px, px);
        }
    }
    _is_set = true;
}

void SVGBox::unset() {
    _is_set = false;
}

void SVGBox::readOrUnset(gchar const *value, const Geom::Scale &doc_scale) {
    if (!value || !read(value, doc_scale)) {
        unset();
    }
}

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
