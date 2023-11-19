// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * From the code of Liam P.White from his Power Stroke Knot dialog
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_DIALOG_FILLET_CHAMFER_PROPERTIES_H
#define INKSCAPE_DIALOG_FILLET_CHAMFER_PROPERTIES_H

#include <glibmm/ustring.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/window.h>

#include <2geom/point.h>

#include "live_effects/parameter/nodesatellitesarray.h"

class SPDesktop;

namespace Inkscape::UI::Dialog {

class FilletChamferPropertiesDialog final : public Gtk::Window
{
public:
    static void showDialog(SPDesktop *desktop, double amount,
                           LivePathEffect::FilletChamferKnotHolderEntity *knot, bool use_distance,
                           bool approx_radius, NodeSatellite nodesatellite);

protected:
    FilletChamferPropertiesDialog();

    LivePathEffect::FilletChamferKnotHolderEntity *_knotpoint = nullptr;

    Gtk::Box _mainbox;
    Gtk::Box _buttonbox;

    Gtk::Label _fillet_chamfer_position_label;
    Gtk::SpinButton _fillet_chamfer_position_numeric;
    Gtk::CheckButton _fillet_chamfer_type_fillet;
    Gtk::CheckButton _fillet_chamfer_type_inverse_fillet;
    Gtk::CheckButton _fillet_chamfer_type_chamfer;
    Gtk::CheckButton _fillet_chamfer_type_inverse_chamfer;
    Gtk::Label _fillet_chamfer_chamfer_subdivisions_label;
    Gtk::SpinButton _fillet_chamfer_chamfer_subdivisions;

    Gtk::Grid _layout_table;
    bool _position_visible = false;

    Gtk::Button _close_button;
    Gtk::Button _apply_button;

    void _setNodeSatellite(NodeSatellite nodesatellite);
    void _prepareLabelRenderer(Gtk::TreeModel::const_iterator const &row);

    void _apply();

    bool _flexible;
    NodeSatellite _nodesatellite;
    bool _use_distance;
    double _amount;
    bool _approx;

    friend class LivePathEffect::FilletChamferKnotHolderEntity;
};

} // namespace Inkscape::UI::Dialog

#endif // INKSCAPE_DIALOG_FILLET_CHAMFER_PROPERTIES_H

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
