// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) Johan Engelen 2007 <j.b.c.engelen@utwente.nl>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "powerstrokepointarray.h"

#include <glibmm/i18n.h>

#include <2geom/sbasis-2d.h>
#include <2geom/bezier-to-sbasis.h>
#include <2geom/piecewise.h>
#include <2geom/sbasis-geometric.h>

#include "helper/geom.h"
#include "live_effects/effect.h"
#include "live_effects/lpe-powerstroke.h"
#include "object/sp-lpe-item.h"
#include "ui/dialog/lpe-powerstroke-properties.h"
#include "ui/knot/knot-holder.h"


#include "preferences.h" // for proportional stroke/path scaling behavior


namespace Inkscape {

namespace LivePathEffect {

PowerStrokePointArrayParam::PowerStrokePointArrayParam( const Glib::ustring& label, const Glib::ustring& tip,
                        const Glib::ustring& key, Inkscape::UI::Widget::Registry* wr,
                        Effect* effect)
    : ArrayParam<Geom::Point>(label, tip, key, wr, effect, 0)
    , knot_shape(Inkscape::CANVAS_ITEM_CTRL_SHAPE_DIAMOND)
    , knot_color(0xff88ff00)
{
}

PowerStrokePointArrayParam::~PowerStrokePointArrayParam() = default;

Gtk::Widget *
PowerStrokePointArrayParam::param_newWidget()
{
    return nullptr;
}

void PowerStrokePointArrayParam::param_transform_multiply(Geom::Affine const &postmul, bool /*set*/)
{
    // Check if proportional stroke-width scaling is on
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool transform_stroke = prefs ? prefs->getBool("/options/transform/stroke", true) : true;
    if (transform_stroke) {
        std::vector<Geom::Point> result;
        result.reserve(_vector.size()); // reserve space for the points that will be added in the for loop
        for (auto point_it : _vector)
        {
            // scale each width knot with the average scaling in X and Y
            Geom::Coord const A = point_it[Geom::Y] * postmul.descrim();
            result.emplace_back(point_it[Geom::X], A);
        }
        param_set_and_write_new_value(result);
    }
}

/** call this method to recalculate the controlpoints such that they stay at the same location relative to the new path. Useful after adding/deleting nodes to the path.*/
void
PowerStrokePointArrayParam::recalculate_controlpoints(Geom::PathVector pv)
{
    Inkscape::LivePathEffect::LPEPowerStroke *lpe = dynamic_cast<Inkscape::LivePathEffect::LPEPowerStroke *>(param_effect);
    Geom::PathVector prev_pv = path_from_piecewise(last_pwd2, 0.01);
    if (lpe) {
        std::vector<Geom::Point> points;
        for (size_t index = 0; index < _vector.size(); index++) {
            knot_reposition(index, pv);
        }
        write_to_SVG();
    }
}

Geom::Point
PowerStrokePointArrayParam::knot_get(size_t index)
{   
    using namespace Geom;

    Piecewise<D2<SBasis> > pwd2 = get_pwd2();
    Piecewise<D2<SBasis> > n = get_pwd2_normal();
    Point offset_point = _vector.at(index);
    if (offset_point[X] > path_from_piecewise(pwd2,0.1).curveCount() || offset_point[X] < 0) {
        g_warning("Broken powerstroke point at %f, I won't try to add that", offset_point[X]);
        return Point(infinity(), infinity());
    }
    Point canvas_point = pwd2.valueAt(offset_point[X]) + (offset_point[Y] * _scale_width) * n.valueAt(offset_point[X]);
    return canvas_point;
}

size_t sp_calculate_origin(size_t i, Geom::PathVector pathv) {
    size_t pos = 0;
    size_t origin = 0;
    for (auto path : pathv) {
        if (pos == i) {
            break;
        }
        size_t psize = count_pathvector_curves(path);
        origin += psize;
        pos ++;
    }
    return origin;
}

Geom::Point
PowerStrokePointArrayParam::knot_reposition(size_t index, Geom::PathVector pathv) {
    Geom::Point p = knot_get(index);
    std::optional<Geom::PathVectorTime> pos = pathv.nearestTime(p);
    if (pos) {
        _vector.at(index)[Geom::X] = (double)sp_calculate_origin((*pos).path_index, pathv) + (double)(*pos).curve_index + (*pos).t;
    }
    return p;
}
 
/** call this method to recalculate the controlpoints when path is reversed.*/
std::vector<Geom::Point>
PowerStrokePointArrayParam::reverse_controlpoints(bool write)
{
    std::vector<Geom::Point> controlpoints;
    if (!last_pwd2.empty()) {
        Geom::Piecewise<Geom::D2<Geom::SBasis> > const & pwd2_in_reverse = reverse(last_pwd2);
        for (auto & i : _vector) {
            Geom::Point control_pos = last_pwd2.valueAt(i[Geom::X]);
            double new_pos = Geom::nearest_time(control_pos, pwd2_in_reverse);
            controlpoints.emplace_back(new_pos,i[Geom::Y]);
            i[Geom::X] = new_pos;
        }
        if (write) {
            write_to_SVG();
            _vector.clear();
            _vector = controlpoints;
            controlpoints.clear();
            write_to_SVG();
            return _vector;
        }
    }
    return controlpoints;
}

float PowerStrokePointArrayParam::median_width()
{
    auto const size = _vector.size();

    if (size == 0) {
        return 1;
    }

    if (size % 2 == 0) {
        return (_vector[size / 2 - 1].y() + _vector[size / 2].y()) / 2;
    }

    return _vector[size / 2].y();
}

void
PowerStrokePointArrayParam::set_pwd2(Geom::Piecewise<Geom::D2<Geom::SBasis> > const & pwd2_in, Geom::Piecewise<Geom::D2<Geom::SBasis> > const & pwd2_normal_in)
{
    last_pwd2 = pwd2_in;
    last_pwd2_normal = pwd2_normal_in;
}

void PowerStrokePointArrayParam::set_oncanvas_looks(CanvasItemCtrlShape shape, uint32_t color)
{
    knot_shape = shape;
    knot_color = color;
}

/*
class PowerStrokePointArrayParamKnotHolderEntity : public KnotHolderEntity {
public:
    PowerStrokePointArrayParamKnotHolderEntity(PowerStrokePointArrayParam *p, unsigned int index);
    virtual ~PowerStrokePointArrayParamKnotHolderEntity() {}

    virtual void knot_set(Geom::Point const &p, Geom::Point const &origin, guint state);
    virtual Geom::Point knot_get() const;
    virtual void knot_click(guint state);

    // Checks whether the index falls within the size of the parameter's vector
    bool valid_index(unsigned int index) const {
        return (_pparam->_vector.size() > index);
    };

private:
    PowerStrokePointArrayParam *_pparam;
    unsigned int _index;
};*/

PowerStrokePointArrayParamKnotHolderEntity::PowerStrokePointArrayParamKnotHolderEntity(PowerStrokePointArrayParam *p, unsigned int index) 
  : _pparam(p), 
    _index(index)
{ 
}

void
PowerStrokePointArrayParamKnotHolderEntity::knot_set(Geom::Point const &p, Geom::Point const &/*origin*/, guint state)
{
    using namespace Geom;

    if (!valid_index(_index)) {
        return;
    }
    _pparam->current_path  = true;
    LPEPowerStroke *ps = dynamic_cast<LPEPowerStroke *>(_pparam->param_effect);
    ps->knotdragging = true;
    static gint prev_index = 0;
    Piecewise<D2<SBasis> > const & pwd2 = _pparam->get_pwd2();
    Piecewise<D2<SBasis> > pwd2port = _pparam->get_pwd2();
    Geom::Point s = snap_knot_position(p, state);    
    double t2 = _pparam->_vector.at(_index)[Geom::X];
    Geom::PathVector pathv = path_from_piecewise(pwd2port, 0.001);
    Geom::Path pathin = pathv.pathAt(t2);
    Geom::Coord begin = 0;
    Geom::Coord end = 0;
    // TODO: add this to 2Geom
    size_t psize = 0;
    size_t i = 0;
    for (auto path : pathv) {
        psize = count_pathvector_curves(path);
        if (pathin == path) {
            end = begin + psize;
            _pparam->current_path = i;
            break;
        }
        i++;
        if (path.closed()) {
            i++;
        }
        begin += psize;
    }
    /// @todo how about item transforms???
    
    Piecewise<D2<SBasis> > const & n = _pparam->get_pwd2_normal();
    double pos = 0;
    if (_pparam->unplaced ) {
        pos = nearest_time(s, pwd2);
    } else {
        pos = nearest_time(s, pwd2, begin, end);
    }
    gint index = std::floor(pos);
    if (pos == begin || pos == end) {
        if (pathv.size() > 1 && knot->is_grabbed()) {
            _pparam->unplaced  = true;
        }
    }
    bool bigjump = false;
    if (std::abs(prev_index - index) > 1 && pathv.pathAt(prev_index) == pathv.pathAt(index)) {
        bigjump = true;
    } else {
        prev_index = index;
    }
    
    double t = 0;
    if (_pparam->unplaced ) {
        t = nearest_time(s, pwd2port);
    } else {
        t = nearest_time(s, pwd2port, begin, end);
    }
    double offset = 0.0;
    if (ps && ps->not_jump && bigjump) {
        t = _pparam->_vector.at(_index)[Geom::X];
    }
    offset = dot(s - pwd2.valueAt(t), n.valueAt(t));
    _pparam->_vector.at(_index) = Geom::Point(t, offset/_pparam->_scale_width);
    if (_pparam->_vector.size() == 1 ) {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        prefs->setDouble("/live_effects/powerstroke/width", offset);
    }
    sp_lpe_item_update_patheffect(cast<SPLPEItem>(item), false, false);
}

Geom::Point
PowerStrokePointArrayParamKnotHolderEntity::knot_get() const
{
    if (!valid_index(_index)) {
        return Geom::Point(Geom::infinity(), Geom::infinity());
    }
    return _pparam->knot_get(_index);
}

void
PowerStrokePointArrayParamKnotHolderEntity::knot_ungrabbed(Geom::Point const &p, Geom::Point const &origin, guint state)
{
    if (_pparam->unplaced ) {
        _pparam->unplaced  = false;
        _pparam->current_path = Glib::ustring::npos;
    }
    dynamic_cast<LPEPowerStroke *>(_pparam->param_effect)->knotdragging = false;
    _pparam->param_effect->makeUndoDone(_("Move handle"));
}

void PowerStrokePointArrayParamKnotHolderEntity::knot_set_offset(Geom::Point offset)
{
    _pparam->_vector.at(_index) = Geom::Point(offset.x(), offset.y() / 2);
    this->parent_holder->knot_ungrabbed_handler(this->knot, 0);
}

void
PowerStrokePointArrayParamKnotHolderEntity::knot_click(guint state)
{
    if (state & GDK_CONTROL_MASK) {
        if (state & GDK_ALT_MASK) {
            // delete the clicked knot
            std::vector<Geom::Point> & vec = _pparam->_vector;
            if (vec.size() > 1) { //Force don't remove last knot
                vec.erase(vec.begin() + _index);
                _pparam->param_set_and_write_new_value(vec);
                // shift knots down one index
                for(auto & ent : parent_holder->entity) {
                    PowerStrokePointArrayParamKnotHolderEntity *pspa_ent = dynamic_cast<PowerStrokePointArrayParamKnotHolderEntity *>(ent);
                    if ( pspa_ent && pspa_ent->_pparam == this->_pparam ) {  // check if the knotentity belongs to this powerstrokepointarray parameter
                        if (pspa_ent->_index > this->_index) {
                            --pspa_ent->_index;
                        }
                    }
                };
                // temporary hide, when knotholder were recreated it finally drop
                this->knot->hide();
            }
            return;
        } else {
            // add a knot to XML
            std::vector<Geom::Point> & vec = _pparam->_vector;
            vec.insert(vec.begin() + _index, 1, vec.at(_index)); // this clicked knot is duplicated
            _pparam->param_set_and_write_new_value(vec);

            // shift knots up one index
            for(auto & ent : parent_holder->entity) {
                PowerStrokePointArrayParamKnotHolderEntity *pspa_ent = dynamic_cast<PowerStrokePointArrayParamKnotHolderEntity *>(ent);
                if ( pspa_ent && pspa_ent->_pparam == this->_pparam ) {  // check if the knotentity belongs to this powerstrokepointarray parameter
                    if (pspa_ent->_index > this->_index) {
                        ++pspa_ent->_index;
                    }
                }
            };
            // add knot to knotholder
            PowerStrokePointArrayParamKnotHolderEntity *e = new PowerStrokePointArrayParamKnotHolderEntity(_pparam, _index+1);
            e->create(this->desktop, this->item, parent_holder, Inkscape::CANVAS_ITEM_CTRL_TYPE_LPE, "LPE:PowerStroke",
                      _("<b>Stroke width control point</b>: drag to alter the stroke width. <b>Ctrl+click</b> adds a "
                        "control point, <b>Ctrl+Alt+click</b> deletes it, <b>Shift+click</b> launches width dialog."),
                      _pparam->knot_color);
            parent_holder->add(e);
        }
    } else if ((state & GDK_ALT_MASK) || (state & GDK_SHIFT_MASK)) {
        Geom::Point offset = Geom::Point(_pparam->_vector.at(_index).x(), _pparam->_vector.at(_index).y() * 2);
        Inkscape::UI::Dialog::PowerstrokePropertiesDialog::showDialog(this->desktop, offset, this);
    } 
}

void PowerStrokePointArrayParam::addKnotHolderEntities(KnotHolder *knotholder, SPItem *item)
{
    for (unsigned int i = 0; i < _vector.size(); ++i) {
        PowerStrokePointArrayParamKnotHolderEntity *e = new PowerStrokePointArrayParamKnotHolderEntity(this, i);
        e->create(nullptr, item, knotholder, Inkscape::CANVAS_ITEM_CTRL_TYPE_LPE, "LPE:PowerStroke",
                  _("<b>Stroke width control point</b>: drag to alter the stroke width. <b>Ctrl+click</b> adds a "
                    "control point, <b>Ctrl+Alt+click</b> deletes it, <b>Shift+click</b> launches width dialog."),
                  knot_color);
        knotholder->add(e);
    }
}

} /* namespace LivePathEffect */

} /* namespace Inkscape */

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
