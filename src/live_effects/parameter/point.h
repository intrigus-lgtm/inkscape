// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Inkscape::LivePathEffectParameters
 *
 * Copyright (C) Johan Engelen 2007 <j.b.c.engelen@utwente.nl>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_LIVEPATHEFFECT_PARAMETER_POINT_H
#define INKSCAPE_LIVEPATHEFFECT_PARAMETER_POINT_H

#include <cstdint>
#include <optional>
#include <2geom/point.h>
#include <glibmm/ustring.h>

#include "display/control/canvas-item-enums.h"
#include "live_effects/parameter/parameter.h"
#include "ui/widget/registered-widget.h"

namespace Gtk {
class GestureClick;
class Widget;
} // namespace Gtk

class KnotHolder;
class KnotHolderEntity;

namespace Inkscape::LivePathEffect {

class PointParamKnotHolderEntity;

class PointParam final : public Geom::Point, public Parameter {
public:
    PointParam( const Glib::ustring& label,
                const Glib::ustring& tip,
                const Glib::ustring& key,
                Inkscape::UI::Widget::Registry* wr,
                Effect* effect,
                std::optional<Glib::ustring> handle_tip = {}, ///< tip for automatically associated on-canvas handle
                Geom::Point default_value = Geom::Point(0,0), 
                bool live_update = true );
    ~PointParam() override;
    PointParam(const PointParam&) = delete;
    PointParam& operator=(const PointParam&) = delete;

    Gtk::Widget * param_newWidget() final;

    bool param_readSVGValue(char const *strvalue) final;
    Glib::ustring param_getSVGValue() const final;
    Glib::ustring param_getDefaultSVGValue() const final;

    inline char const *handleTip() const { return handle_tip ? handle_tip->c_str() : param_tooltip.c_str(); }

    void param_setValue(Geom::Point newpoint, bool write = false);
    void param_set_default() final;
    void param_hide_knot(bool hide);

    Geom::Point param_get_default() const;

    void param_set_liveupdate(bool live_update);

    void param_update_default(Geom::Point default_point);
    void param_update_default(char const *default_point) final;

    void param_transform_multiply(Geom::Affine const & /*postmul*/, bool set) final;

    void set_oncanvas_looks(Inkscape::CanvasItemCtrlShape shape, std::uint32_t color);

    bool providesKnotHolderEntities() const final { return true; }
    void addKnotHolderEntities(KnotHolder *knotholder, SPItem *item) final;
    ParamType paramType() const final { return ParamType::POINT; };

    friend class PointParamKnotHolderEntity;

private:
    void on_value_changed();

    Geom::Point defvalue;
    bool liveupdate;
    KnotHolderEntity *_knot_entity = nullptr;
    Inkscape::CanvasItemCtrlShape knot_shape = Inkscape::CANVAS_ITEM_CTRL_SHAPE_DIAMOND;
    std::uint32_t knot_color = 0xffffff00;
    std::optional<Glib::ustring> handle_tip;
};

} // namespace Inkscape::LivePathEffect

#endif // INKSCAPE_LIVEPATHEFFECT_PARAMETER_POINT_H

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
