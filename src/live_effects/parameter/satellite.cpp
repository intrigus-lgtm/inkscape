// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) Johan Engelen 2007 <j.b.c.engelen@utwente.nl>
 *   Abhishek Sharma
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "live_effects/parameter/satellite.h"

#include <glibmm/i18n.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>

#include "bad-uri-exception.h"
#include "inkscape.h"

#include "live_effects/effect.h"
#include "live_effects/lpeobject.h"
#include "object/sp-item.h"
#include "object/uri.h"
#include "ui/clipboard.h"
#include "ui/icon-loader.h"
#include "ui/pack.h"

namespace Inkscape {
namespace LivePathEffect {

SatelliteParam::SatelliteParam(const Glib::ustring &label, const Glib::ustring &tip, const Glib::ustring &key,
                               Inkscape::UI::Widget::Registry *wr, Effect *effect)
    : Parameter(label, tip, key, wr, effect)
    , lperef(std::make_shared<SatelliteReference>(param_effect->getLPEObj(), false))
    , last_transform(Geom::identity())
{}

SatelliteParam::~SatelliteParam() {
    quit_listening();
}

std::vector<SPObject *> SatelliteParam::param_get_satellites()
{
    std::vector<SPObject *> objs;
    // we reload connexions in case are lost for example item recreation on ungroup
    if (!linked_transformed_connection) {
        write_to_SVG();
    }

    SPObject * linked_obj = lperef->getObject();
    if (linked_obj) {
        objs.push_back(linked_obj);
    }
    return objs;
}

bool SatelliteParam::param_readSVGValue(const gchar *strvalue)
{
    if (strvalue) {
        bool write = false;
        auto lpeitems = param_effect->getCurrrentLPEItems();
        Glib::ustring id_tmp;
        if (lpeitems.empty() && !param_effect->is_applied && !param_effect->getSPDoc()->isSeeking()) {
            SPObject * old_ref = param_effect->getSPDoc()->getObjectByHref(strvalue);
            if (old_ref) {
                SPObject * tmpsuccessor = old_ref->_tmpsuccessor;
                // cast to effect is not possible now
                if (!g_strcmp0("clone_original", param_effect->getLPEObj()->getAttribute("effect"))) {
                    id_tmp = strvalue;
                }
                if (tmpsuccessor && tmpsuccessor->getId()) {
                    id_tmp = tmpsuccessor->getId();
                    id_tmp.insert(id_tmp.begin(), '#');
                    write = true;
                }
                strvalue = id_tmp.c_str();
            }
        }
        SPObject *old_ref = lperef->getObject();
        if (old_ref) {
            unlink();
        }
        if (strvalue[0] == '#') {
            try {
                lperef->attach(Inkscape::URI(g_strdup(strvalue)));
                // lp:1299948
                SPObject *new_ref = lperef->getObject();
                if (new_ref) {
                    linked_changed(old_ref, new_ref);
                    // linked_modified(new_ref, SP_OBJECT_STYLESHEET_MODIFIED_FLAG);
                } // else: document still processing new events. Repr of the linked object not created yet.
            } catch (Inkscape::BadURIException &e) {
                g_warning("%s", e.what());
                lperef->detach();
            }
        } else if (lpeitems.empty() && !param_effect->is_applied && !param_effect->getSPDoc()->isSeeking()) {
            param_write_to_repr("");
        }
        if (write) {
            param_write_to_repr(param_getSVGValue().c_str());
        }
        return true;
    }

    return false;
}

bool SatelliteParam::linksToItem() const
{
    return lperef->isAttached();
}

SPObject *SatelliteParam::getObject() const
{
    return lperef->isAttached() ? lperef->getObject() : nullptr;
}

Glib::ustring SatelliteParam::param_getSVGValue() const
{
    if (lperef->getURI()) {
        return lperef->getURI()->str();
    }
    return "";
}

Glib::ustring SatelliteParam::param_getDefaultSVGValue() const
{
    return "";
}

void SatelliteParam::param_set_default()
{
    param_readSVGValue("");
}

void SatelliteParam::unlink()
{
    quit_listening();
    if (linksToItem()) {
        lperef->detach();
    }
}

void SatelliteParam::link(Glib::ustring itemid)
{
    if (itemid.empty()) {
        return;
    }
    auto *document = param_effect->getSPDoc();
    SPObject *object = document->getObjectById(itemid);
    if (object && object != getObject()) {
        itemid.insert(itemid.begin(), '#');
        param_write_to_repr(itemid.c_str());
    } else {
        param_write_to_repr("");
    }
    param_effect->makeUndoDone(_("Link item parameter to path"));
}

// SIGNALS

void SatelliteParam::start_listening(SPObject *to)
{
    if (!to) {
        return;
    }
    quit_listening();
    linked_changed_connection = lperef->changedSignal().connect(sigc::mem_fun(*this, &SatelliteParam::linked_changed));
    auto item = cast<SPItem>(to);
    if (item) {
        linked_released_connection = item->connectRelease(sigc::mem_fun(*this, &SatelliteParam::linked_released));
        linked_modified_connection = item->connectModified(sigc::mem_fun(*this, &SatelliteParam::linked_modified));
        linked_transformed_connection =
            item->connectTransformed(sigc::mem_fun(*this, &SatelliteParam::linked_transformed));
        if (!param_effect->is_load) {
            linked_modified(item, SP_OBJECT_MODIFIED_FLAG);
        }
    }
}

void SatelliteParam::quit_listening()
{
    if (linked_changed_connection) {
        linked_changed_connection.disconnect();
    }
    if (linked_released_connection) {
        linked_released_connection.disconnect();
    }
    if (linked_modified_connection) {
        linked_modified_connection.disconnect();
    }
    if (linked_transformed_connection) {
        linked_transformed_connection.disconnect();
    }
}

void SatelliteParam::linked_changed(SPObject *old_obj, SPObject *new_obj)
{
    quit_listening();
    if (new_obj) {
        start_listening(new_obj);
    }
}

void SatelliteParam::linked_released(SPObject *released)
{
    if (param_effect->getLPEObj()) {
        unlink();
        param_effect->processObjects(LPE_UPDATE);
    }
}


void SatelliteParam::linked_modified(SPObject *linked_obj, guint flags)
{
    if (!_updating && flags & (SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG |
                 SP_OBJECT_CHILD_MODIFIED_FLAG | SP_OBJECT_VIEWPORT_MODIFIED_FLAG)) {
        if (!param_effect->is_load || ownerlocator || (!SP_ACTIVE_DESKTOP && param_effect->isReady())) {
            param_effect->getLPEObj()->requestModified(SP_OBJECT_MODIFIED_FLAG);
        }
        last_transform = Geom::identity();
        if (effectType() != CLONE_ORIGINAL) {
            update_satellites();
        }
    }
}

void SatelliteParam::linked_transformed(Geom::Affine const *rel_transf, SPItem *moved_item)
{
    if (!_updating) {
        update_satellites();
    }
}

// UI

void SatelliteParam::addCanvasIndicators(SPLPEItem const * /*lpeitem*/, std::vector<Geom::PathVector> &hp_vec) {}

void SatelliteParam::on_link_button_click()
{
    Inkscape::UI::ClipboardManager *cm = Inkscape::UI::ClipboardManager::get();
    // here prevent item is reseted transform on link
    if (effectType() == CLONE_ORIGINAL) {
        param_effect->is_load = false;
    }
    Glib::ustring itemid;
    if (lookup) {
        std::vector<Glib::ustring> clones = cm->getElementsOfType(nullptr, "svg:use", 2);
        if (!clones.empty()) {
            itemid = clones[0];
        }
    }
    if (itemid.empty()) {
        itemid = cm->getFirstObjectID();
    }
    if (itemid.empty()) {
        return;
    }

    link(itemid);
}

Gtk::Widget *SatelliteParam::param_newWidget()
{
    auto const _widget = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
    Gtk::Image *pIcon = Gtk::manage(sp_get_icon_image("edit-clone", Gtk::IconSize::NORMAL));
    auto const pButton = Gtk::make_managed<Gtk::Button>();
    auto const pLabel = Gtk::make_managed<Gtk::Label>(param_label);
    UI::pack_start(*_widget, *pLabel, true, true);
    pLabel->set_tooltip_text(param_tooltip);
    pButton->set_has_frame(false);
    pButton->set_child(*pIcon);
    pButton->signal_clicked().connect(sigc::mem_fun(*this, &SatelliteParam::on_link_button_click));
    UI::pack_start(*_widget, *pButton, true, true);
    pButton->set_tooltip_text(_("Link to item on clipboard"));

    return _widget;
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
