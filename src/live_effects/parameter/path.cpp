// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) Johan Engelen 2007 <j.b.c.engelen@utwente.nl>
 *   Abhishek Sharma
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "path.h"

#include <2geom/d2.h>
#include <2geom/pathvector.h>
#include <2geom/sbasis-to-bezier.h>
#include <2geom/svg-path-parser.h>
#include <glibmm/i18n.h>
#include <glibmm/utility.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>

#include "bad-uri-exception.h"
#include "desktop.h"
#include "document.h"
#include "document-undo.h"
#include "inkscape.h"
#include "selection.h"

#include "actions/actions-tools.h"
#include "display/curve.h"
#include "live_effects/effect.h"
#include "live_effects/lpeobject.h"
#include "object/sp-item.h"
#include "object/sp-shape.h"
#include "object/sp-text.h"
#include "object/uri.h"
#include "svg/svg.h"
#include "ui/clipboard.h" // clipboard support
#include "ui/icon-loader.h"
#include "ui/pack.h"
#include "ui/tool/multi-path-manipulator.h"
#include "ui/tool/shape-record.h"
#include "ui/tools/node-tool.h"

namespace Inkscape {
namespace LivePathEffect {

PathParam::PathParam( const Glib::ustring& label, const Glib::ustring& tip,
                      const Glib::ustring& key, Inkscape::UI::Widget::Registry* wr,
                      Effect* effect, const gchar * default_value)
    : Parameter(label, tip, key, wr, effect),
      changed(true),
      _pathvector(),
      _pwd2(),
      must_recalculate_pwd2(false),
      href(nullptr),
      ref( (SPObject*)effect->getLPEObj() )
{
    defvalue = g_strdup(default_value);
    param_readSVGValue(defvalue);
    oncanvas_editable = true;
    _from_original_d = false;
    _edit_button  = true;
    _copy_button  = true;
    _paste_button = true;
    _link_button  = true;
    ref_changed_connection = ref.changedSignal().connect(sigc::mem_fun(*this, &PathParam::ref_changed));
}

PathParam::~PathParam() {
    unlink();
    quit_listening();
    g_free(defvalue);
}

void PathParam::reload() {
    setUpdating(false);
    start_listening(getItem());
    connect_selection_changed();
    SPItem *item = nullptr;
    if ((item = getItem())) {
        item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
    }
}

Geom::Affine 
PathParam::get_relative_affine() {
    Geom::Affine affine = Geom::identity();
    if (auto item = getItem()) {
        std::vector<SPLPEItem *> lpeitems = param_effect->getCurrrentLPEItems();
        if (lpeitems.size() == 1) {
            param_effect->sp_lpe_item = lpeitems[0];
        }
        affine = item->getRelativeTransform(param_effect->sp_lpe_item);
    }
    return affine;
}

Geom::PathVector const &
PathParam::get_pathvector() const
{
    return _pathvector;
}

Geom::Piecewise<Geom::D2<Geom::SBasis> > const &
PathParam::get_pwd2()
{
    ensure_pwd2();
    return _pwd2;
}

void
PathParam::param_set_default()
{
    param_readSVGValue(defvalue);
}

void
PathParam::param_set_and_write_default()
{
    param_write_to_repr(defvalue);
}

std::vector<SPObject *> PathParam::param_get_satellites()
{
    
    std::vector<SPObject *> objs;
    if (ref.isAttached()) {
        // we reload connexions in case are lost for example item recreation on ungroup
        if (!linked_transformed_connection) {
            write_to_SVG();
        }

        SPObject * linked_obj = ref.getObject();
        if (linked_obj) {
            objs.push_back(linked_obj);
        }
    }
    return objs;
}

bool
PathParam::param_readSVGValue(const gchar * strvalue)
{
    if (strvalue) {
        _pathvector.clear();
        unlink();
        must_recalculate_pwd2 = true;

        
        if (strvalue[0] == '#') {
            bool write = false;
            SPObject * old_ref = param_effect->getSPDoc()->getObjectByHref(strvalue);
            Glib::ustring id_tmp;
            if (old_ref) {
                SPObject * tmpsuccessor = old_ref->_tmpsuccessor;
                // study add setListener() in LPE that generate items from 0
                if (tmpsuccessor && tmpsuccessor->getId()) {
                    id_tmp = tmpsuccessor->getId();
                    id_tmp.insert(id_tmp.begin(), '#');
                    write = true;
                }
            }
            if (href)
                g_free(href);
            href = g_strdup(id_tmp.empty() ? strvalue : id_tmp.c_str());

            // Now do the attaching, which emits the changed signal.
            try {
                ref.attach(Inkscape::URI(href));
                //lp:1299948
                SPItem* i = ref.getObject();
                if (i) {
                    linked_modified_callback(i, SP_OBJECT_MODIFIED_FLAG);
                } // else: document still processing new events. Repr of the linked object not created yet.
            } catch (Inkscape::BadURIException &e) {
                g_warning("%s", e.what());
                ref.detach();
                _pathvector = sp_svg_read_pathv(defvalue);
            }
            if (write) {
                auto full = param_getSVGValue();
                param_write_to_repr(full.c_str());
            }
        } else {
            _pathvector = sp_svg_read_pathv(strvalue);
        }

        emit_changed();
        return true;
    }

    return false;
}

Glib::ustring
PathParam::param_getSVGValue() const
{
    if (href) {
        return href;
    } else {
        return sp_svg_write_path(_pathvector);
    }
}

Glib::ustring
PathParam::param_getDefaultSVGValue() const
{
    return defvalue;
}

void
PathParam::set_buttons(bool edit_button, bool copy_button, bool paste_button, bool link_button)
{
    _edit_button  = edit_button;
    _copy_button  = copy_button;
    _paste_button = paste_button;
    _link_button  = link_button;
}

Gtk::Widget *
PathParam::param_newWidget()
{
    auto const _widget = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);

    auto const pLabel = Gtk::make_managed<Gtk::Label>(param_label);
    UI::pack_start(*_widget, *pLabel, true, true);
    pLabel->set_tooltip_text(param_tooltip);
    Gtk::Image * pIcon = nullptr;
    Gtk::Button * pButton = nullptr;
    if (_edit_button) {
        pIcon = Gtk::manage(sp_get_icon_image("tool-node-editor", Gtk::IconSize::NORMAL));
        pButton = Gtk::make_managed<Gtk::Button>();
        pButton->set_has_frame(false);
        pButton->set_child(*pIcon);
        pButton->signal_clicked().connect(sigc::mem_fun(*this, &PathParam::on_edit_button_click));
        UI::pack_start(*_widget, *pButton, true, true);
        pButton->set_tooltip_text(_("Edit on-canvas"));
    }

    if (_copy_button) {
        pIcon = Gtk::manage(sp_get_icon_image("edit-copy", Gtk::IconSize::NORMAL));
        pButton = Gtk::make_managed<Gtk::Button>();
        pButton->set_has_frame(false);
        pButton->set_child(*pIcon);

        pButton->signal_clicked().connect(sigc::mem_fun(*this, &PathParam::on_copy_button_click));
        UI::pack_start(*_widget, *pButton, true, true);
        pButton->set_tooltip_text(_("Copy path"));
    }

    if (_paste_button) {
        pIcon = Gtk::manage(sp_get_icon_image("edit-paste", Gtk::IconSize::NORMAL));
        pButton = Gtk::make_managed<Gtk::Button>();
        pButton->set_has_frame(false);
        pButton->set_child(*pIcon);
        pButton->signal_clicked().connect(sigc::mem_fun(*this, &PathParam::on_paste_button_click));
        UI::pack_start(*_widget, *pButton, true, true);
        pButton->set_tooltip_text(_("Paste path"));
    }
    if (_link_button) {
        pIcon = Gtk::manage(sp_get_icon_image("edit-clone", Gtk::IconSize::NORMAL));
        pButton = Gtk::make_managed<Gtk::Button>();
        pButton->set_has_frame(false);
        pButton->set_child(*pIcon);
        pButton->signal_clicked().connect(sigc::mem_fun(*this, &PathParam::on_link_button_click));
        UI::pack_start(*_widget, *pButton, true, true);
        pButton->set_tooltip_text(_("Link to path in clipboard"));
    }

    return _widget;
}

void
PathParam::param_editOncanvas(SPItem *item, SPDesktop * dt)
{
    SPDocument *document = dt->getDocument();
    DocumentUndo::ScopedInsensitive _no_undo(document);
    using namespace Inkscape::UI;

    auto nt = dynamic_cast<Inkscape::UI::Tools::NodeTool *>(dt->getTool());
    if (!nt) {
        set_active_tool(dt, "Node");
        nt = dynamic_cast<Inkscape::UI::Tools::NodeTool *>(dt->getTool());
    }

    std::set<ShapeRecord> shapes;
    ShapeRecord r;

    r.role = SHAPE_ROLE_LPE_PARAM;
    r.edit_transform = item->i2dt_affine(); // TODO is it right?
    if (!href) {
        r.object = param_effect->getLPEObj();
        r.lpe_key = param_key;
        Geom::PathVector stored_pv =  _pathvector;
        if (_pathvector.empty()) {
            param_write_to_repr("M0,0 L1,0");
        } else {
            param_write_to_repr(sp_svg_write_path(stored_pv).c_str());
        }
    } else {
        r.object = ref.getObject();
    }
    shapes.insert(r);
    nt->_multipath->setItems(shapes);
}

void
PathParam::param_setup_nodepath(Inkscape::NodePath::Path *)
{
    // TODO this method should not exist at all!
}

void
PathParam::addCanvasIndicators(SPLPEItem const*/*lpeitem*/, std::vector<Geom::PathVector> &hp_vec)
{
    hp_vec.push_back(_pathvector);
}

/*
 * Only applies transform when not referring to other path!
 */
void
PathParam::param_transform_multiply(Geom::Affine const& postmul, bool /*set*/)
{
    // only apply transform when not referring to other path
    if (!href) {
        set_new_value( _pathvector * postmul, true );
    }
}

/*
 * See comments for set_new_value(Geom::PathVector).
 */
void
PathParam::set_new_value (Geom::Piecewise<Geom::D2<Geom::SBasis> > const & newpath, bool write_to_svg)
{
    unlink();
    
    _pathvector = Geom::path_from_piecewise(newpath, LPE_CONVERSION_TOLERANCE);

    if (write_to_svg) {
        if (param_effect->isOnClipboard()) {
            return;
        }
        param_write_to_repr(sp_svg_write_path(_pathvector).c_str());

        // After the whole "writing to svg avalanche of function calling": force value upon pwd2 and don't recalculate.
        _pwd2 = newpath;
        must_recalculate_pwd2 = false;
    } else {
        _pwd2 = newpath;
        must_recalculate_pwd2 = false;
        emit_changed();
    }
}

/*
 * This method sets new path data.
 * If this PathParam refers to another path, this link is removed (and replaced with explicit path data).
 *
 * If write_to_svg = true :
 *          The new path data is written to SVG. In this case the signal_path_changed signal
 *          is not directly emitted in this method, because writing to SVG
 *          triggers the LPEObject to which this belongs to call Effect::setParameter which calls
 *          PathParam::readSVGValue, which finally emits the signal_path_changed signal.
 * If write_to_svg = false :
 *          The new path data is not written to SVG. This method will emit the signal_path_changed signal.
 */
void
PathParam::set_new_value (Geom::PathVector const &newpath, bool write_to_svg)
{
    unlink();
    if (newpath.empty()) {
        param_set_and_write_default();
        return;
    } else {
        _pathvector = newpath;
    }
    must_recalculate_pwd2 = true;

    if (write_to_svg) {
        param_write_to_repr(sp_svg_write_path(_pathvector).c_str());
    } else {
        emit_changed();
    }
}

void
PathParam::ensure_pwd2()
{
    if (must_recalculate_pwd2) {
        _pwd2.clear();
        for (const auto & i : _pathvector) {
            _pwd2.concat( i.toPwSb() );
        }

        must_recalculate_pwd2 = false;
    }
}

void
PathParam::emit_changed()
{
    changed = true;
    signal_path_changed.emit();
}

void
PathParam::start_listening(SPObject * to)
{
    if ( to == nullptr ) {
        return;
    }
    quit_listening();
    linked_deleted_connection = to->connectDelete(sigc::mem_fun(*this, &PathParam::linked_deleted));
    linked_modified_connection = to->connectModified(sigc::mem_fun(*this, &PathParam::linked_modified));
    if (is<SPItem>(to)) {
        linked_transformed_connection = cast<SPItem>(to)->connectTransformed(sigc::mem_fun(*this, &PathParam::linked_transformed));
    }
    linked_modified(to, SP_OBJECT_MODIFIED_FLAG); // simulate linked_modified signal, so that path data is updated
}

void
PathParam::quit_listening()
{
    linked_modified_connection.disconnect();
    linked_deleted_connection.disconnect();
    linked_transformed_connection.disconnect();
}

void
PathParam::ref_changed(SPObject */*old_ref*/, SPObject *new_ref)
{
    quit_listening();
    if ( new_ref ) {
        start_listening(new_ref);
    }
}

void PathParam::unlink()
{
    if (href) {
        ref.detach();
        g_free(href);
        href = nullptr;
    }
}

// Why release signal is not fired sometimes and need delete one?
void
PathParam::linked_deleted(SPObject *deleted)
{
    Geom::PathVector pv = _pathvector;
    quit_listening();
    set_new_value (pv, true);
}

void PathParam::linked_modified(SPObject *linked_obj, guint flags)
{
    if (flags & (SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG |
                 SP_OBJECT_CHILD_MODIFIED_FLAG | SP_OBJECT_VIEWPORT_MODIFIED_FLAG)) 
    {
        linked_modified_callback(linked_obj, flags);
    }
}

void PathParam::linked_transformed(Geom::Affine const *rel_transf, SPItem *moved_item)
{
    linked_modified_callback(moved_item, SP_OBJECT_MODIFIED_FLAG);
}

void
PathParam::linked_modified_callback(SPObject *linked_obj, guint flags)
{
    if (!_updating && flags & (SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG |
                 SP_OBJECT_CHILD_MODIFIED_FLAG | SP_OBJECT_VIEWPORT_MODIFIED_FLAG)) 
    {
        std::optional<SPCurve> curve;
        if (auto shape = cast<SPShape>(linked_obj)) {
            if (_from_original_d) {
                curve = SPCurve::ptr_to_opt(shape->curveForEdit());
            } else {
                curve = SPCurve::ptr_to_opt(shape->curve());
            }
        }

        auto text = cast<SPText>(linked_obj);
        if (text) {
            bool hidden = text->isHidden();
            if (hidden) {
                if (_pathvector.empty()) {
                    text->setHidden(false);
                    curve = text->getNormalizedBpath();
                    text->setHidden(true);
                } else {
                    if (!curve) {
                        curve.emplace();
                    }
                    curve->set_pathvector(_pathvector);
                }
            } else {
                curve = text->getNormalizedBpath();
            }
        }

        if (!curve) {
            // curve invalid, set default value
            _pathvector = sp_svg_read_pathv(defvalue);
        } else {
            _pathvector = curve->get_pathvector();
        }

        must_recalculate_pwd2 = true;
        emit_changed();
        if (!param_effect->is_load || ownerlocator || (!SP_ACTIVE_DESKTOP && param_effect->isReady())) {
            param_effect->getLPEObj()->requestModified(SP_OBJECT_MODIFIED_FLAG);
        }
    }
}

void
PathParam::param_update_default(const gchar * default_value){
    defvalue = strdup(default_value);
}

/* CALLBACK FUNCTIONS FOR THE BUTTONS */
void
PathParam::on_edit_button_click()
{
    SPItem * item = SP_ACTIVE_DESKTOP->getSelection()->singleItem();
    if (item != nullptr) {
        param_editOncanvas(item, SP_ACTIVE_DESKTOP);
    }
}

void
PathParam::paste_param_path(const char *svgd)
{
    // only recognize a non-null, non-empty string
    if (svgd && *svgd) {
        // remove possible link to path
        unlink();
        SPItem * item = SP_ACTIVE_DESKTOP->getSelection()->singleItem();
        std::string svgd_new;
        if (item != nullptr) {
            Geom::PathVector path_clipboard =  sp_svg_read_pathv(svgd);
            path_clipboard *= item->i2doc_affine().inverse();
            svgd_new = sp_svg_write_path(path_clipboard);
            svgd = svgd_new.c_str();
        }

        param_write_to_repr(svgd);
        signal_path_pasted.emit();
    }
}

void
PathParam::on_paste_button_click()
{
    Inkscape::UI::ClipboardManager *cm = Inkscape::UI::ClipboardManager::get();
    Glib::ustring svgd = cm->getPathParameter(SP_ACTIVE_DESKTOP);
    paste_param_path(svgd.data());
    param_effect->makeUndoDone(_("Paste path parameter"));
}

void
PathParam::on_copy_button_click()
{
    Inkscape::UI::ClipboardManager *cm = Inkscape::UI::ClipboardManager::get();
    cm->copyPathParameter(this);
}

void
PathParam::linkitem(Glib::ustring pathid)
{
    if (pathid.empty()) {
        return;
    }

    // add '#' at start to make it an uri.
    pathid.insert(pathid.begin(), '#');
    if ( href && strcmp(pathid.c_str(), href) == 0 ) {
        // no change, do nothing
        return;
    } else {
        // TODO:
        // check if id really exists in document, or only in clipboard document: if only in clipboard then invalid
        // check if linking to object to which LPE is applied (maybe delegated to PathReference
        param_write_to_repr(pathid.c_str());
        param_effect->makeUndoDone(_("Link path parameter to path"));
    }
}

void
PathParam::on_link_button_click()
{
    Inkscape::UI::ClipboardManager *cm = Inkscape::UI::ClipboardManager::get();
    Glib::ustring pathid = cm->getShapeOrTextObjectId(SP_ACTIVE_DESKTOP);

    linkitem(pathid);
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
