// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * From the code of Liam P.White from his Power Stroke Knot dialog
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "lpe-fillet-chamfer-properties.h"

#include <string>
#include <boost/lexical_cast.hpp>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <gtkmm/box.h>
#include <sigc++/adaptors/bind.h>
#include <sigc++/adaptors/hide.h>
#include <sigc++/functors/mem_fun.h>

#include "desktop.h"

namespace Inkscape::UI::Dialog {

FilletChamferPropertiesDialog::FilletChamferPropertiesDialog()
    : _mainbox(Gtk::Orientation::VERTICAL)
    , _close_button(_("_Cancel"), true)
{
    set_name("FilletChamferPropertiesDialog");

    set_child(_mainbox);
    _mainbox.set_margin(2);
    _mainbox.set_spacing(4);

    _layout_table.set_row_spacing(4);
    _layout_table.set_column_spacing(4);

    // Layer name widgets
    _fillet_chamfer_position_numeric.set_digits(4);
    _fillet_chamfer_position_numeric.set_increments(1,1);
    //todo: get the max allowable infinity freeze the widget
    _fillet_chamfer_position_numeric.set_range(0., SCALARPARAM_G_MAXDOUBLE);
    _fillet_chamfer_position_numeric.set_hexpand();
    _fillet_chamfer_position_label.set_label(_("Radius (pixels):"));
    _fillet_chamfer_position_label.set_halign(Gtk::Align::END);
    _fillet_chamfer_position_label.set_valign(Gtk::Align::CENTER);

    _layout_table.attach(_fillet_chamfer_position_label, 0, 0, 1, 1);
    _layout_table.attach(_fillet_chamfer_position_numeric, 1, 0, 1, 1);
    _fillet_chamfer_chamfer_subdivisions.set_digits(0);
    _fillet_chamfer_chamfer_subdivisions.set_increments(1,1);
    //todo: get the max allowable infinity freeze the widget
    _fillet_chamfer_chamfer_subdivisions.set_range(0, SCALARPARAM_G_MAXDOUBLE);
    _fillet_chamfer_chamfer_subdivisions.set_hexpand();
    _fillet_chamfer_chamfer_subdivisions_label.set_label(_("Chamfer subdivisions:"));
    _fillet_chamfer_chamfer_subdivisions_label.set_halign(Gtk::Align::END);
    _fillet_chamfer_chamfer_subdivisions_label.set_valign(Gtk::Align::CENTER);

    _layout_table.attach(_fillet_chamfer_chamfer_subdivisions_label, 0, 1, 1, 1);
    _layout_table.attach(_fillet_chamfer_chamfer_subdivisions, 1, 1, 1, 1);
    _fillet_chamfer_type_fillet.set_label(_("Fillet"));
    _fillet_chamfer_type_fillet.set_expand();
    _fillet_chamfer_type_inverse_fillet.set_label(_("Inverse fillet"));
    _fillet_chamfer_type_inverse_fillet.set_group(_fillet_chamfer_type_fillet);
    _fillet_chamfer_type_inverse_fillet.set_expand();
    _fillet_chamfer_type_chamfer.set_label(_("Chamfer"));
    _fillet_chamfer_type_chamfer.set_group(_fillet_chamfer_type_fillet);
    _fillet_chamfer_type_chamfer.set_expand();
    _fillet_chamfer_type_inverse_chamfer.set_label(_("Inverse chamfer"));
    _fillet_chamfer_type_inverse_chamfer.set_group(_fillet_chamfer_type_fillet);
    _fillet_chamfer_type_inverse_chamfer.set_expand();

    _mainbox.append(_layout_table);
    _mainbox.append(_fillet_chamfer_type_fillet);
    _mainbox.append(_fillet_chamfer_type_inverse_fillet);
    _mainbox.append(_fillet_chamfer_type_chamfer);
    _mainbox.append(_fillet_chamfer_type_inverse_chamfer);

    _mainbox.append(_buttonbox);
    _buttonbox.set_halign(Gtk::Align::END);
    _buttonbox.set_homogeneous();
    _buttonbox.set_spacing(4);

    _close_button.signal_clicked().connect([this] { destroy(); });
    _apply_button.signal_clicked().connect([this] { _apply(); });

    _close_button.set_receives_default();
    _apply_button.set_use_underline(true);
    _apply_button.set_receives_default();
    _buttonbox.append(_close_button);
    _buttonbox.append(_apply_button);

    set_default_widget(_apply_button);

    set_focus(_fillet_chamfer_position_numeric);
}

void FilletChamferPropertiesDialog::showDialog(SPDesktop *desktop, double amount,
                                               LivePathEffect::FilletChamferKnotHolderEntity *knot,
                                               bool use_distance, bool approx_radius, NodeSatellite nodesatellite)
{
    auto dialog = Gtk::manage(new FilletChamferPropertiesDialog());

    dialog->_use_distance = use_distance;
    dialog->_approx = approx_radius;
    dialog->_amount = amount;
    dialog->_setNodeSatellite(nodesatellite);
    dialog->_knotpoint = knot;

    dialog->set_title(_("Modify Fillet-Chamfer"));
    dialog->_apply_button.set_label(_("_Modify"));

    dialog->set_modal(true);
    desktop->setWindowTransient(dialog->gobj());
    dialog->property_destroy_with_parent() = true;

    dialog->present();
}

void FilletChamferPropertiesDialog::_apply()
{
    double d_pos =  _fillet_chamfer_position_numeric.get_value();
    if (d_pos >= 0) {
        if (_fillet_chamfer_type_fillet.get_active() == true) {
            _nodesatellite.nodesatellite_type = FILLET;
        } else if (_fillet_chamfer_type_inverse_fillet.get_active() == true) {
            _nodesatellite.nodesatellite_type = INVERSE_FILLET;
        } else if (_fillet_chamfer_type_inverse_chamfer.get_active() == true) {
            _nodesatellite.nodesatellite_type = INVERSE_CHAMFER;
        } else {
            _nodesatellite.nodesatellite_type = CHAMFER;
        }
        if (_flexible) {
            if (d_pos > 99.99999 || d_pos < 0) {
                d_pos = 0;
            }
            d_pos = d_pos / 100;
        }
        _nodesatellite.amount = d_pos;
        size_t steps = (size_t)_fillet_chamfer_chamfer_subdivisions.get_value();
        if (steps < 1) {
            steps = 1;
        }
        _nodesatellite.steps = steps;
        _knotpoint->knot_set_offset(_nodesatellite);
    }
    destroy();
}

void FilletChamferPropertiesDialog::_setNodeSatellite(NodeSatellite nodesatellite)
{
    double position;

    std::string distance_or_radius = _("Radius");
    if (_approx) {
        distance_or_radius = _("Radius approximated");
    }
    if (_use_distance) {
        distance_or_radius = _("Knot distance");
    }

    if (nodesatellite.is_time) {
        position = _amount * 100;
        _flexible = true;
        _fillet_chamfer_position_label.set_label(_("Position (%):"));
    } else {
        _flexible = false;
        auto posConcat = Glib::ustring::compose (_("%1:"), distance_or_radius);
        _fillet_chamfer_position_label.set_label(_(posConcat.c_str()));
        position = _amount;
    }

    _fillet_chamfer_position_numeric.set_value(position);
    _fillet_chamfer_chamfer_subdivisions.set_value(nodesatellite.steps);

    if (nodesatellite.nodesatellite_type == FILLET) {
        _fillet_chamfer_type_fillet.set_active(true);
    } else if (nodesatellite.nodesatellite_type == INVERSE_FILLET) {
        _fillet_chamfer_type_inverse_fillet.set_active(true);
    } else if (nodesatellite.nodesatellite_type == CHAMFER) {
        _fillet_chamfer_type_chamfer.set_active(true);
    } else if (nodesatellite.nodesatellite_type == INVERSE_CHAMFER) {
        _fillet_chamfer_type_inverse_chamfer.set_active(true);
    }
    _nodesatellite = nodesatellite;
}

} // namespace Inkscape::UI::Dialog

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99
