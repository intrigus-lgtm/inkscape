// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * SVG <ellipse> and related implementations
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Mitsuru Oka
 *   Tavmjong Bah
 *
 * Copyright (C) 1999-2002 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 * Copyright (C) 2013 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_SP_ELLIPSE_H
#define SEEN_SP_ELLIPSE_H

#include "svg/svg-length.h"
#include "sp-shape.h"

enum GenericEllipseType {
    SP_GENERIC_ELLIPSE_UNDEFINED, // FIXME shouldn't exist
    SP_GENERIC_ELLIPSE_ARC,
    SP_GENERIC_ELLIPSE_CIRCLE,
    SP_GENERIC_ELLIPSE_ELLIPSE
};

enum GenericEllipseArcType {
    SP_GENERIC_ELLIPSE_ARC_TYPE_SLICE, // Default
    SP_GENERIC_ELLIPSE_ARC_TYPE_ARC,
    SP_GENERIC_ELLIPSE_ARC_TYPE_CHORD
};

class SPGenericEllipse final : public SPShape {
public:
    SPGenericEllipse();
    ~SPGenericEllipse() override;
    int tag() const override { return tag_of<decltype(*this)>; }

    // Regardless of type, the ellipse/circle/arc is stored
    // internally with these variables. (Circle radius is rx).
    SVGLength cx;
    SVGLength cy;
    SVGLength rx;
    SVGLength ry;

    // Return slice, chord, or arc.
    GenericEllipseArcType arcType() { return arc_type; };
    void setArcType(GenericEllipseArcType type) { arc_type = type; };

    double start, end;
    GenericEllipseType type;
    GenericEllipseArcType arc_type;

    void tag_name_changed(gchar const* oldname, gchar const* newname) override;

    void build(SPDocument *document, Inkscape::XML::Node *repr) override;

    void set(SPAttr key, char const *value) override;
    void update(SPCtx *ctx, unsigned int flags) override;

    Inkscape::XML::Node *write(Inkscape::XML::Document *xml_doc, Inkscape::XML::Node *repr, unsigned int flags) override;
    const char *typeName() const override;
    const char *displayName() const override;

    void set_shape() override;
    void update_patheffect(bool write) override;
    Geom::Affine set_transform(Geom::Affine const &xform) override;

    void snappoints(std::vector<Inkscape::SnapCandidatePoint> &p, Inkscape::SnapPreferences const *snapprefs) const override;

    void modified(unsigned int flags) override;

    /**
     * @brief Makes sure that start and end lie between 0 and 2 * PI.
     */
    void normalize();

    Geom::Point getPointAtAngle(double arg) const;

    bool set_elliptical_path_attribute(Inkscape::XML::Node *repr);
    void position_set(double x, double y, double rx, double ry);

    double getVisibleRx() const;
    void setVisibleRx(double rx);

    double getVisibleRy() const;
    void setVisibleRy(double ry);

    bool is_whole() const { return !_isSlice(); }

protected:
    /**
     * @brief Determines whether the shape is a part of an ellipse.
     */
    bool _isSlice() const;
};

#endif

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8 :
