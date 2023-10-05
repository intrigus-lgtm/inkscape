// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) Jabiertxof <jabier.arraiza@marker.es>
 * this class handle satellites of a lpe as a parameter
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "satellitearray.h"

#include <algorithm>
#include <string>
#include <utility>
#include <glibmm/i18n.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/cellrenderertoggle.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/treestore.h>
#include <gtkmm/treeview.h>

#include "inkscape.h"
#include "selection.h"
#include "live_effects/effect.h"
#include "live_effects/lpeobject.h"
#include "object/sp-lpe-item.h"
#include "ui/clipboard.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/pack.h"

namespace Inkscape::LivePathEffect {

class SatelliteArrayParam::ModelColumns : public Gtk::TreeModel::ColumnRecord
{
public:
    ModelColumns()
    {
        add(_colObject);
        add(_colLabel);
        add(_colActive);
    }

    Gtk::TreeModelColumn<Glib::ustring> _colObject;
    Gtk::TreeModelColumn<Glib::ustring> _colLabel;
    Gtk::TreeModelColumn<bool> _colActive;
};

SatelliteArrayParam::SatelliteArrayParam(const Glib::ustring &label, const Glib::ustring &tip, const Glib::ustring &key,
                                         Inkscape::UI::Widget::Registry *wr, Effect *effect, bool visible)
    : ArrayParam<std::shared_ptr<SatelliteReference>>(label, tip, key, wr, effect)
    , _visible(visible)
{
    param_widget_is_visible(_visible);

    if (_visible) {
        initui();
        oncanvas_editable = true;
    }
}

SatelliteArrayParam::~SatelliteArrayParam() {
    _vector.clear();
    if (_store.get() && _model) {
        _model.reset();
    }
    quit_listening();
}

void SatelliteArrayParam::initui()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (!desktop) {
        return;
    }

    if (!_tree) {
        _tree = std::make_unique<Gtk::TreeView>();
        _model = std::make_unique<ModelColumns>();
        _store = Gtk::TreeStore::create(*_model);
        _tree->set_model(_store);

        _tree->set_reorderable(true);
        _tree->enable_model_drag_dest(Gdk::DragAction::MOVE);

        auto const toggle_active = Gtk::make_managed<Gtk::CellRendererToggle>();
        int activeColNum = _tree->append_column(_("Active"), *toggle_active) - 1;
        Gtk::TreeViewColumn *col_active = _tree->get_column(activeColNum);
        toggle_active->set_activatable(true);
        toggle_active->signal_toggled().connect(sigc::mem_fun(*this, &SatelliteArrayParam::on_active_toggled));
        col_active->add_attribute(toggle_active->property_active(), _model->_colActive);

        auto const text_renderer = Gtk::make_managed<Gtk::CellRendererText>();
        int nameColNum = _tree->append_column(_("Name"), *text_renderer) - 1;
        auto const name_column = _tree->get_column(nameColNum);
        name_column->add_attribute(text_renderer->property_text(), _model->_colLabel);

        _tree->set_expander_column(*_tree->get_column(nameColNum));
        _tree->set_search_column(_model->_colLabel);

        _scroller = std::make_unique<Gtk::ScrolledWindow>();
        // quick little hack -- newer versions of gtk gave the item zero space allotment
        _scroller->set_size_request(-1, 120);
        _scroller->set_child(*_tree);
        _scroller->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    }
    param_readSVGValue(param_getSVGValue().c_str());
}

void SatelliteArrayParam::start_listening()
{
    quit_listening();
    for (auto const &ref : _vector) {
        if (ref && ref->isAttached()) {
            auto item = cast<SPItem>(ref->getObject());
            if (item) {
                linked_connections.emplace_back(item->connectRelease(
                    sigc::hide(sigc::mem_fun(*this, &SatelliteArrayParam::updatesignal))));
                linked_connections.emplace_back(item->connectModified(
                    sigc::mem_fun(*this, &SatelliteArrayParam::linked_modified)));
                linked_connections.emplace_back(item->connectTransformed(
                    sigc::hide(sigc::hide(sigc::mem_fun(*this, &SatelliteArrayParam::updatesignal)))));
                linked_connections.emplace_back(ref->changedSignal().connect(
                    sigc::hide(sigc::hide(sigc::mem_fun(*this, &SatelliteArrayParam::updatesignal)))));
            }
        }
    }
}

void SatelliteArrayParam::linked_modified(SPObject *linked_obj, guint flags) {
    if (!_updating && (!SP_ACTIVE_DESKTOP || SP_ACTIVE_DESKTOP->getSelection()->includes(linked_obj)) &&
        (!param_effect->is_load || ownerlocator || !SP_ACTIVE_DESKTOP ) && 
        param_effect->_lpe_action == LPE_NONE &&
        param_effect->isReady() &&
        flags & (SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG |
                 SP_OBJECT_CHILD_MODIFIED_FLAG | SP_OBJECT_VIEWPORT_MODIFIED_FLAG))
    {
        param_effect->processObjects(LPE_UPDATE);
    }
}

void SatelliteArrayParam::updatesignal()
{
    if (!_updating && 
        (!param_effect->is_load || ownerlocator || !SP_ACTIVE_DESKTOP ) && 
        param_effect->_lpe_action == LPE_NONE 
        && param_effect->isReady()) 
    {
        param_effect->processObjects(LPE_UPDATE);
    }
}

void SatelliteArrayParam::quit_listening()
{
    linked_connections.clear();
};

void SatelliteArrayParam::on_active_toggled(const Glib::ustring &item)
{
    int i = 0;
    for (auto const &w : _vector) {
        if (w && w->isAttached() && w->getObject()) {
            auto row =  *_store->get_iter(std::to_string(i));
            Glib::ustring id = w->getObject()->getId() ? w->getObject()->getId() : "";
            if (id == row.get_value(_model->_colObject)) {
                auto active = row[_model->_colActive];
                active = !active;
                w->setActive(active);
                i++;
                break;
            }
        }
    }
    param_effect->makeUndoDone(_("Active switched"));
}

bool SatelliteArrayParam::param_readSVGValue(char const * const strvalue)
{
    if (strvalue) {
        bool changed = !linked_connections.size() || !param_effect->is_load;
        if (!ArrayParam::param_readSVGValue(strvalue)) {
            return false;
        }
        auto lpeitems = param_effect->getCurrrentLPEItems();
        if (!lpeitems.size() && !param_effect->is_applied && !param_effect->getSPDoc()->isSeeking()) {
            size_t pos = 0;
            for (auto const &w : _vector) {
                if (w) {
                    SPObject * tmp = w->getObject();
                    if (tmp) {
                        SPObject * tmpsuccessor = tmp->_tmpsuccessor;
                        unlink(tmp);
                        if (tmpsuccessor && tmpsuccessor->getId()) {
                            link(tmpsuccessor,pos);
                        }
                    }
                }
                pos ++;
            }
            param_write_to_repr(param_getSVGValue().c_str());
            update_satellites();
        }
        if (_store.get()) {
            _store->clear();
            for (auto const &w : _vector) {
                if (w) {
                    auto const iter = _store->append();
                    Gtk::TreeModel::Row row = *iter;
                    if (auto obj = w->getObject()) {
                        row[_model->_colObject] = Glib::ustring(obj->getId());
                        row[_model->_colLabel]  = obj->label() ? obj->label() : obj->getId();
                        row[_model->_colActive] = w->getActive();
                    }
                }
            }
        }
        if (changed) {
            start_listening();
        }
        return true;
    }
    return false;
}

bool SatelliteArrayParam::_selectIndex(const Gtk::TreeModel::iterator &iter, int *i)
{
    if ((*i)-- <= 0) {
        _tree->get_selection()->select(iter);
        return true;
    }
    return false;
}

void SatelliteArrayParam::move_up_down(int const delta, Glib::ustring const &word)
{
    auto const iter_selected = _tree->get_selection()->get_selected();
    if (!iter_selected) return;

    int i = 0;
    for (auto const &w : _vector) {
        if (w && w->isAttached() && w->getObject()) {
            auto const iter =  _store->get_iter(std::to_string(i));
            if (iter_selected == iter && i > 0) {
                std::swap(_vector[i],_vector[i + delta]);
                i += delta;
                break;
            }
            i++;
        }
    }

    // Translators: %1 is the translated version of "up" or "down".
    param_effect->makeUndoDone(Glib::ustring::compose(_("Move item %1"), word));
    _store->foreach_iter(sigc::bind(sigc::mem_fun(*this, &SatelliteArrayParam::_selectIndex), &i));
}

void SatelliteArrayParam::on_up_button_click()
{
    move_up_down(-1, _("up"));
}

void SatelliteArrayParam::on_down_button_click()
{
    move_up_down(+1, _("down"));
}

void SatelliteArrayParam::on_remove_button_click()
{
    auto const iter = _tree->get_selection()->get_selected();
    if (iter) {
        Gtk::TreeModel::Row row = *iter;
        unlink(param_effect->getSPDoc()->getObjectById((const Glib::ustring&)(row[_model->_colObject])));
        param_effect->makeUndoDone(_("Remove item"));
    }
}

void SatelliteArrayParam::on_link_button_click()
{
    Inkscape::UI::ClipboardManager *cm = Inkscape::UI::ClipboardManager::get();

    std::vector<Glib::ustring> itemsid;
    // Here we ignore auto clipboard group wrapper
    std::vector<Glib::ustring> itemsids = cm->getElementsOfType(SP_ACTIVE_DESKTOP, "*", 2);
    std::vector<Glib::ustring> containers = cm->getElementsOfType(SP_ACTIVE_DESKTOP, "*", 1);

    for (auto &&item : std::move(itemsids)) {
        bool cont = false;
        for (auto const &citems : containers) {
            if (citems == item) {
                cont = true;
            }
        }
        if (!cont) {
            itemsid.push_back(std::move(item));
        }
    }

    if (itemsid.empty()) {
        return;
    }

    auto hreflist = param_effect->getLPEObj()->hrefList;
    if (hreflist.size()) {
        auto sp_lpe_item = cast<SPLPEItem>(*hreflist.begin());
        if (sp_lpe_item) {
            for (auto &&itemid : std::move(itemsid)) {
                SPObject *added = param_effect->getSPDoc()->getObjectById(itemid);
                if (added && sp_lpe_item != added) {
                    itemid.insert(itemid.begin(), '#');
                    auto satellitereference =
                        std::make_shared<SatelliteReference>(param_effect->getLPEObj(), _visible);
                    try {
                        satellitereference->attach(Inkscape::URI(itemid.c_str()));
                        satellitereference->setActive(true);
                        _vector.push_back(std::move(satellitereference));
                    } catch (Inkscape::BadURIException &e) {
                        g_warning("%s", e.what());
                        satellitereference->detach();
                    }
                }
            }
        }
    }
    param_effect->makeUndoDone(_("Link itemarray parameter to item"));
}

Gtk::Widget *SatelliteArrayParam::param_newWidget()
{
    if (!_visible) {
        return nullptr;
    }

    auto const vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    auto const hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);

    _tree.reset();
    _scroller.reset();
    _model.reset();

    initui();

    UI::pack_start(*vbox, *_scroller, UI::PackOptions::expand_widget);

    { // Paste item to link button
        auto const pIcon = Gtk::manage(sp_get_icon_image("edit-clone", Gtk::IconSize::NORMAL));
        auto const pButton = Gtk::make_managed<Gtk::Button>();
        pButton->set_has_frame(false);
        pButton->set_child(*pIcon);
        pButton->signal_clicked().connect(sigc::mem_fun(*this, &SatelliteArrayParam::on_link_button_click));
        UI::pack_start(*hbox, *pButton, UI::PackOptions::shrink);
        pButton->set_tooltip_text(_("Link to item"));
    }

    { // Remove linked item
        auto const pIcon = Gtk::manage(sp_get_icon_image("list-remove", Gtk::IconSize::NORMAL));
        auto const pButton = Gtk::make_managed<Gtk::Button>();
        pButton->set_has_frame(false);
        pButton->set_child(*pIcon);
        pButton->signal_clicked().connect(sigc::mem_fun(*this, &SatelliteArrayParam::on_remove_button_click));
        UI::pack_start(*hbox, *pButton, UI::PackOptions::shrink);
        pButton->set_tooltip_text(_("Remove Item"));
    }

    { // Move Down
        auto const pIcon = Gtk::manage(sp_get_icon_image("go-down", Gtk::IconSize::NORMAL));
        auto const pButton = Gtk::make_managed<Gtk::Button>();
        pButton->set_has_frame(false);
        pButton->set_child(*pIcon);
        pButton->signal_clicked().connect(sigc::mem_fun(*this, &SatelliteArrayParam::on_down_button_click));
        UI::pack_end(*hbox, *pButton, UI::PackOptions::shrink);
        pButton->set_tooltip_text(_("Move Down"));
    }

    { // Move Down
        auto const pIcon = Gtk::manage(sp_get_icon_image("go-up", Gtk::IconSize::NORMAL));
        auto const pButton = Gtk::make_managed<Gtk::Button>();
        pButton->set_has_frame(false);
        pButton->set_child(*pIcon);
        pButton->signal_clicked().connect(sigc::mem_fun(*this, &SatelliteArrayParam::on_up_button_click));
        UI::pack_end(*hbox, *pButton, UI::PackOptions::shrink);
        pButton->set_tooltip_text(_("Move Up"));
    }

    UI::pack_end(*vbox, *hbox, UI::PackOptions::shrink);

    return vbox;
}

std::vector<SPObject *> SatelliteArrayParam::param_get_satellites()
{
    std::vector<SPObject *> objs;
    for (auto &iter : _vector) {
        if (iter && iter->isAttached()) {
            SPObject *obj = iter->getObject();
            if (obj) {
                objs.push_back(obj);
            }
        }
    }
    return objs;
}

/*
 * This function link a satellite writing into XML directly
 * @param obj: object to link
 * @param obj: position in vector
 */
void SatelliteArrayParam::link(SPObject *obj, size_t pos)
{
    if (obj && obj->getId()) {
        Glib::ustring itemid = "#";
        itemid += obj->getId();
        auto satellitereference =
            std::make_shared<SatelliteReference>(param_effect->getLPEObj(), _visible);
        try {
            satellitereference->attach(Inkscape::URI(itemid.c_str()));
            if (_visible) {
                satellitereference->setActive(true);
            }
            if (_vector.size() == pos || pos == Glib::ustring::npos) {
                _vector.push_back(std::move(satellitereference));
            } else {
                _vector[pos] = std::move(satellitereference);
            }
        } catch (Inkscape::BadURIException &e) {
            g_warning("%s", e.what());
            satellitereference->detach();
        }
    }
}

void SatelliteArrayParam::unlink(SPObject *obj)
{
    if (!obj) {
        return;
    }

    auto const it = std::find_if(_vector.begin(), _vector.end(),
                                 [=](auto const &w){ return w && w->getObject() == obj; });
    if (it != _vector.end()) it->reset();
}

void SatelliteArrayParam::unlink(std::shared_ptr<SatelliteReference> const &to)
{
    unlink(to->getObject());
}

void SatelliteArrayParam::clear()
{
    _vector.clear();
}

} // namespace Inkscape::LivePathEffect

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
