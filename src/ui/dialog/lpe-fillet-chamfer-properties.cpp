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

#include "inkscape.h"
#include "desktop.h"
#include "document-undo.h"
#include "layer-manager.h"
#include "message-stack.h"
#include "selection-chemistry.h"
#include "ui/pack.h"

namespace Inkscape::UI::Dialog {

FilletChamferPropertiesDialog::FilletChamferPropertiesDialog()
    : _knotpoint(nullptr),
      _position_visible(false),
      _close_button(_("_Cancel"), true)
{
    Gtk::Box *mainVBox = get_content_area();
    mainVBox->set_homogeneous(false);
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
    _fillet_chamfer_type_fillet.set_group(_fillet_chamfer_type_group);
    _fillet_chamfer_type_inverse_fillet.set_label(_("Inverse fillet"));
    _fillet_chamfer_type_inverse_fillet.set_group(_fillet_chamfer_type_group);
    _fillet_chamfer_type_chamfer.set_label(_("Chamfer"));
    _fillet_chamfer_type_chamfer.set_group(_fillet_chamfer_type_group);
    _fillet_chamfer_type_inverse_chamfer.set_label(_("Inverse chamfer"));
    _fillet_chamfer_type_inverse_chamfer.set_group(_fillet_chamfer_type_group);

    UI::pack_start(*mainVBox, _layout_table, true, true, 4);
    UI::pack_start(*mainVBox, _fillet_chamfer_type_fillet, true, true, 4);
    UI::pack_start(*mainVBox, _fillet_chamfer_type_inverse_fillet, true, true, 4);
    UI::pack_start(*mainVBox, _fillet_chamfer_type_chamfer, true, true, 4);
    UI::pack_start(*mainVBox, _fillet_chamfer_type_inverse_chamfer, true, true, 4);


    _close_button.set_can_default();

    _apply_button.set_use_underline(true);
    _apply_button.set_can_default();

    _close_button.signal_clicked()
        .connect(sigc::mem_fun(*this, &FilletChamferPropertiesDialog::_close));

    _apply_button.signal_clicked()
        .connect(sigc::mem_fun(*this, &FilletChamferPropertiesDialog::_apply));

    signal_delete_event().connect(sigc::bind_return(
                                      sigc::hide(sigc::mem_fun(*this, &FilletChamferPropertiesDialog::_close)),
                                      true));

    add_action_widget(_close_button, Gtk::ResponseType::CLOSE);
    add_action_widget(_apply_button, Gtk::ResponseType::APPLY);

    _apply_button.grab_default();

    set_focus(_fillet_chamfer_position_numeric);
}

void FilletChamferPropertiesDialog::showDialog(SPDesktop *desktop, double _amount,
                                               const Inkscape::LivePathEffect::FilletChamferKnotHolderEntity *pt,
                                               bool _use_distance, bool _aprox_radius, NodeSatellite _nodesatellite)
{
    FilletChamferPropertiesDialog *dialog = new FilletChamferPropertiesDialog();

    dialog->_setUseDistance(_use_distance);
    dialog->_setAprox(_aprox_radius);
    dialog->_setAmount(_amount);
    dialog->_setNodeSatellite(_nodesatellite);
    dialog->_setPt(pt);

    dialog->set_title(_("Modify Fillet-Chamfer"));
    dialog->_apply_button.set_label(_("_Modify"));

    dialog->set_modal(true);
    desktop->setWindowTransient(dialog->gobj());
    dialog->property_destroy_with_parent() = true;

    dialog->set_visible(true);
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
    _close();
}

void FilletChamferPropertiesDialog::_close()
{
    destroy_();
    Glib::signal_idle().connect([this] { delete this; return false; });
}

void FilletChamferPropertiesDialog::_setNodeSatellite(NodeSatellite nodesatellite)
{
    double position;

    std::string distance_or_radius = _("Radius");
    if (_aprox) {
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

void FilletChamferPropertiesDialog::_setPt(
    const Inkscape::LivePathEffect::
    FilletChamferKnotHolderEntity *pt)
{
    _knotpoint = const_cast<
                 Inkscape::LivePathEffect::FilletChamferKnotHolderEntity *>(
                     pt);
}

void FilletChamferPropertiesDialog::_setAmount(double amount)
{
    _amount = amount;
}

void FilletChamferPropertiesDialog::_setUseDistance(bool use_knot_distance)
{
    _use_distance = use_knot_distance;
}

void FilletChamferPropertiesDialog::_setAprox(bool _aprox_radius)
{
    _aprox = _aprox_radius;
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
