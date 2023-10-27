// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * Gradient vector and position widget
 */
/* Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright (C) 2001-2002 Lauris Kaplinski
 * Copyright (C) 2001 Ximian, Inc.
 * Copyright (C) 2010 Jon A. Cruz
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <string>
#include <glibmm/i18n.h>
#include <gtkmm/liststore.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/treeview.h>
#include <sigc++/adaptors/bind.h>
#include <sigc++/functors/mem_fun.h>

#include "actions/actions-tools.h" // Invoke gradient tool
#include "document.h"
#include "document-undo.h"
#include "gradient-chemistry.h"
#include "id-clash.h"
#include "inkscape.h"
#include "object/sp-defs.h"
#include "preferences.h"
#include "ui/controller.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/pack.h"
#include "ui/widget/gradient-vector-selector.h"

namespace Inkscape::UI::Widget {

void GradientSelector::style_button(Gtk::Button *btn, char const *iconName)
{
    btn->set_image_from_icon_name(iconName, Gtk::IconSize::NORMAL); // Previously GTK_ICON_SIZE_SMALL_TOOLBAR
    btn->set_has_frame(false);
}

GradientSelector::GradientSelector()
    : _blocked(false)
    , _mode(MODE_LINEAR)
    , _gradientUnits(SP_GRADIENT_UNITS_USERSPACEONUSE)
    , _gradientSpread(SP_GRADIENT_SPREAD_PAD)
{
    set_orientation(Gtk::Orientation::VERTICAL);

    /* Vectors */
    _vectors = Gtk::make_managed<GradientVectorSelector>(nullptr, nullptr);
    _store = _vectors->get_store();
    _columns = _vectors->get_columns();

    _treeview = Gtk::make_managed<Gtk::TreeView>();
    _treeview->set_model(_store);
    _treeview->set_headers_clickable(true);
    _treeview->set_search_column(1);
    _treeview->set_vexpand();
    _icon_renderer = Gtk::make_managed<Gtk::CellRendererPixbuf>();
    _text_renderer = Gtk::make_managed<Gtk::CellRendererText>();

    _treeview->append_column(_("Gradient"), *_icon_renderer);
    auto icon_column = _treeview->get_column(0);
    icon_column->add_attribute(_icon_renderer->property_pixbuf(), _columns->pixbuf);
    icon_column->set_sort_column(_columns->color);
    icon_column->set_clickable(true);

    _treeview->append_column(_("Name"), *_text_renderer);
    auto name_column = _treeview->get_column(1);
    _text_renderer->property_editable() = true;
    name_column->add_attribute(_text_renderer->property_text(), _columns->name);
    name_column->set_min_width(180);
    name_column->set_clickable(true);
    name_column->set_resizable(true);

    _treeview->append_column("#", _columns->refcount);
    auto count_column = _treeview->get_column(2);
    count_column->set_clickable(true);
    count_column->set_resizable(true);

    Controller::add_key<&GradientSelector::onKeyPressed>(*_treeview, *this);

    _treeview->set_visible(true);

    icon_column->signal_clicked().connect(sigc::mem_fun(*this, &GradientSelector::onTreeColorColClick));
    name_column->signal_clicked().connect(sigc::mem_fun(*this, &GradientSelector::onTreeNameColClick));
    count_column->signal_clicked().connect(sigc::mem_fun(*this, &GradientSelector::onTreeCountColClick));

    auto tree_select_connection = _treeview->get_selection()->signal_changed().connect(sigc::mem_fun(*this, &GradientSelector::onTreeSelection));
    _vectors->set_tree_select_connection(tree_select_connection);
    _text_renderer->signal_edited().connect(sigc::mem_fun(*this, &GradientSelector::onGradientRename));

    _scrolled_window = Gtk::make_managed<Gtk::ScrolledWindow>();
    _scrolled_window->set_child(*_treeview);
    _scrolled_window->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    _scrolled_window->set_has_frame(true);
    _scrolled_window->set_size_request(0, 180);
    _scrolled_window->set_hexpand();
    _scrolled_window->set_visible(true);

    UI::pack_start(*this, *_scrolled_window, true, true, 4);


    /* Create box for buttons */
    auto const hb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    UI::pack_start(*this, *hb, false, false);

    _add = Gtk::make_managed<Gtk::Button>();
    style_button(_add, INKSCAPE_ICON("list-add"));

    _nonsolid.push_back(_add);
    UI::pack_start(*hb, *_add, false, false);

    _add->signal_clicked().connect(sigc::mem_fun(*this, &GradientSelector::add_vector_clicked));
    _add->set_sensitive(false);
    _add->set_has_frame(false);
    _add->set_tooltip_text(_("Create a duplicate gradient"));

    _del2 = Gtk::make_managed<Gtk::Button>();
    style_button(_del2, INKSCAPE_ICON("list-remove"));

    _nonsolid.push_back(_del2);
    UI::pack_start(*hb, *_del2, false, false);
    _del2->signal_clicked().connect(sigc::mem_fun(*this, &GradientSelector::delete_vector_clicked_2));
    _del2->set_sensitive(false);
    _del2->set_has_frame(false);
    _del2->set_tooltip_text(_("Delete unused gradient"));

    // The only use of this button is hidden!
    _edit = Gtk::make_managed<Gtk::Button>();
    style_button(_edit, INKSCAPE_ICON("edit"));

    _nonsolid.push_back(_edit);
    UI::pack_start(*hb, *_edit, false, false);
    _edit->signal_clicked().connect(sigc::mem_fun(*this, &GradientSelector::edit_vector_clicked));
    _edit->set_sensitive(false);
    _edit->set_has_frame(false);
    _edit->set_tooltip_text(_("Edit gradient"));
    _edit->set_visible(false);

    _del = Gtk::make_managed<Gtk::Button>();
    style_button(_del, INKSCAPE_ICON("list-remove"));

    _swatch_widgets.push_back(_del);
    UI::pack_start(*hb, *_del, false, false);
    _del->signal_clicked().connect(sigc::mem_fun(*this, &GradientSelector::delete_vector_clicked));
    _del->set_sensitive(false);
    _del->set_has_frame(false);
    _del->set_tooltip_text(_("Delete swatch"));
}

GradientSelector::~GradientSelector() = default;

void GradientSelector::setSpread(SPGradientSpread spread)
{
    _gradientSpread = spread;
    // gtk_combo_box_set_active (GTK_COMBO_BOX(this->spread), gradientSpread);
}

void GradientSelector::setMode(SelectorMode mode)
{
    if (mode != _mode) {
        _mode = mode;
        if (mode == MODE_SWATCH) {
            for (auto &it : _nonsolid) {
                it->set_visible(false);
            }
            for (auto &swatch_widget : _swatch_widgets) {
                swatch_widget->set_visible(true);
            }

            auto icon_column = _treeview->get_column(0);
            icon_column->set_title(_("Swatch"));

            _vectors->setSwatched();
        } else {
            for (auto &it : _nonsolid) {
                it->set_visible(true);
            }
            for (auto &swatch_widget : _swatch_widgets) {
                swatch_widget->set_visible(false);
            }
            auto icon_column = _treeview->get_column(0);
            icon_column->set_title(_("Gradient"));
        }
    }
}

void GradientSelector::setUnits(SPGradientUnits units) { _gradientUnits = units; }

SPGradientUnits GradientSelector::getUnits() { return _gradientUnits; }

SPGradientSpread GradientSelector::getSpread() { return _gradientSpread; }

void GradientSelector::onGradientRename(const Glib::ustring &path_string, const Glib::ustring &new_text)
{
    Gtk::TreePath path(path_string);
    auto iter = _store->get_iter(path);

    if (iter) {
        Gtk::TreeModel::Row row = *iter;
        if (row) {
            SPObject *obj = row[_columns->data];
            if (obj) {
                if (!new_text.empty() && new_text != gr_prepare_label(obj)) {
                    obj->setLabel(new_text.c_str());
                    Inkscape::DocumentUndo::done(obj->document, _("Rename gradient"), INKSCAPE_ICON("color-gradient"));
                }
                row[_columns->name] = gr_prepare_label(obj);
            }
        }
    }
}

void GradientSelector::onTreeColorColClick()
{
    auto column = _treeview->get_column(0);
    column->set_sort_column(_columns->color);
}

void GradientSelector::onTreeNameColClick()
{
    auto column = _treeview->get_column(1);
    column->set_sort_column(_columns->name);
}


void GradientSelector::onTreeCountColClick()
{
    auto column = _treeview->get_column(2);
    column->set_sort_column(_columns->refcount);
}

void GradientSelector::moveSelection(int amount, bool down, bool toEnd)
{
    auto select = _treeview->get_selection();
    auto iter = select->get_selected();

    if (amount < 0) {
        down = !down;
        amount = -amount;
    }

    auto canary = iter;
    if (down) {
        ++canary;
    } else {
        --canary;
    }
    while (canary && (toEnd || amount > 0)) {
        --amount;
        if (down) {
            ++canary;
            ++iter;
        } else {
            --canary;
            --iter;
        }
    }

    select->select(iter);
    _treeview->scroll_to_row(_store->get_path(iter), 0.5);
}

bool GradientSelector::onKeyPressed(GtkEventControllerKey const * controller,
                                    unsigned /*keyval*/, unsigned const keycode,
                                    GdkModifierType const state)
{
    auto display = Gdk::Display::get_default();
    auto key = 0u;
    gdk_display_translate_key(gdk_display_get_default(),
                              keycode,
                              state,
                              0,
                              &key,
                              nullptr,
                              nullptr,
                              nullptr);
    switch (key) {
        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
            moveSelection(-1);
            return true;

        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
            moveSelection(1);
            return true;

        case GDK_KEY_Page_Up:
        case GDK_KEY_KP_Page_Up:
            moveSelection(-5);
            return true;

        case GDK_KEY_Page_Down:
        case GDK_KEY_KP_Page_Down:
            moveSelection(5);
            return true;

        case GDK_KEY_End:
        case GDK_KEY_KP_End:
            moveSelection(0, true, true);
            return true;

        case GDK_KEY_Home:
        case GDK_KEY_KP_Home:
            moveSelection(0, false, true);
            return true;
    }

    return false;
}

void GradientSelector::onTreeSelection()
{
    if (!_treeview) {
        return;
    }

    if (_blocked) {
        return;
    }

    if (!_treeview->has_focus()) {
        /* Workaround for GTK bug on Windows/OS X
         * When the treeview initially doesn't have focus and is clicked
         * sometimes get_selection()->signal_changed() has the wrong selection
         */
        _treeview->grab_focus();
    }

    const auto sel = _treeview->get_selection();
    if (!sel) {
        return;
    }

    SPGradient *obj = nullptr;
    /* Single selection */
    auto iter = sel->get_selected();
    if (iter) {
        Gtk::TreeModel::Row row = *iter;
        obj = row[_columns->data];
    }

    if (obj) {
        vector_set(obj);
    }

    check_del_button();
}

void GradientSelector::check_del_button() {
    const auto sel = _treeview->get_selection();
    if (!sel) {
        return;
    }

    SPGradient *obj = nullptr;
    /* Single selection */
    auto iter = sel->get_selected();
    if (iter) {
        Gtk::TreeModel::Row row = *iter;
        obj = row[_columns->data];
    }
    if (_del2) {
        _del2->set_sensitive(obj && sp_get_gradient_refcount(obj->document, obj) < 2 && _store->children().size() > 1);
    }
}

bool GradientSelector::_checkForSelected(const Gtk::TreePath &path, const Gtk::TreeModel::iterator &iter, SPGradient *vector)
{
    bool found = false;

    Gtk::TreeModel::Row row = *iter;
    if (vector == row[_columns->data]) {
        _treeview->scroll_to_row(path, 0.5);
        auto select = _treeview->get_selection();
        bool wasBlocked = _blocked;
        _blocked = true;
        select->select(iter);
        _blocked = wasBlocked;
        found = true;
    }

    return found;
}

void GradientSelector::selectGradientInTree(SPGradient *vector)
{
    _store->foreach (sigc::bind(sigc::mem_fun(*this, &GradientSelector::_checkForSelected), vector));
}

void GradientSelector::setVector(SPDocument *doc, SPGradient *vector)
{
    g_return_if_fail(!vector || (vector->document == doc));

    if (vector && !vector->hasStops()) {
        return;
    }

    _vectors->set_gradient(doc, vector);

    selectGradientInTree(vector);

    if (vector) {
        if ((_mode == MODE_SWATCH) && vector->isSwatch()) {
            if (vector->isSolid()) {
                for (auto &it : _nonsolid) {
                    it->set_visible(false);
                }
            } else {
                for (auto &it : _nonsolid) {
                    it->set_visible(true);
                }
            }
        } else if (_mode != MODE_SWATCH) {

            for (auto &swatch_widget : _swatch_widgets) {
                swatch_widget->set_visible(false);
            }
            for (auto &it : _nonsolid) {
                it->set_visible(true);
            }
        }

        if (_edit) {
            _edit->set_sensitive(true);
        }
        if (_add) {
            _add->set_sensitive(true);
        }
        if (_del) {
            _del->set_sensitive(true);
        }
        check_del_button();
    } else {
        if (_edit) {
            _edit->set_sensitive(false);
        }
        if (_add) {
            _add->set_sensitive(doc != nullptr);
        }
        if (_del) {
            _del->set_sensitive(false);
        }
        if (_del2) {
            _del2->set_sensitive(false);
        }
    }
}

SPGradient *GradientSelector::getVector()
{
    return _vectors->get_gradient();
}


void GradientSelector::vector_set(SPGradient *gr)
{
    if (!_blocked) {
        _blocked = true;
        gr = sp_gradient_ensure_vector_normalized(gr);
        setVector((gr) ? gr->document : nullptr, gr);
        _signal_changed.emit(gr);
        _blocked = false;
    }
}

void GradientSelector::delete_vector_clicked_2() {
    const auto selection = _treeview->get_selection();
    if (!selection) {
        return;
    }

    SPGradient *obj = nullptr;
    /* Single selection */
    Gtk::TreeModel::iterator iter = selection->get_selected();
    if (iter) {
        Gtk::TreeModel::Row row = *iter;
        obj = row[_columns->data];
    }

    if (obj) {
        if (auto repr = obj->getRepr()) {
            repr->setAttribute("inkscape:collect", "always");

            auto move = iter;
            --move;
            if (!move) {
                move = iter;
                ++move;
            }
            if (move) {
                selection->select(move);
                _treeview->scroll_to_row(_store->get_path(move), 0.5);
            }
        }
    }
}

void GradientSelector::delete_vector_clicked()
{
    const auto selection = _treeview->get_selection();
    if (!selection) {
        return;
    }

    SPGradient *obj = nullptr;
    /* Single selection */
    Gtk::TreeModel::iterator iter = selection->get_selected();
    if (iter) {
        Gtk::TreeModel::Row row = *iter;
        obj = row[_columns->data];
    }

    if (obj) {
        std::string id = obj->getId();
        sp_gradient_unset_swatch(SP_ACTIVE_DESKTOP, id);
    }
}

void GradientSelector::edit_vector_clicked()
{
    // Invoke the gradient tool.... never actually called as button is hidden in only use!
    set_active_tool(SP_ACTIVE_DESKTOP, "Gradient");
}

void GradientSelector::add_vector_clicked()
{
    auto doc = _vectors->get_document();

    if (!doc)
        return;

    auto gr = _vectors->get_gradient();
    Inkscape::XML::Document *xml_doc = doc->getReprDoc();

    Inkscape::XML::Node *repr = nullptr;

    if (gr) {
        gr->getRepr()->removeAttribute("inkscape:collect");
        repr = gr->getRepr()->duplicate(xml_doc);
        // Rename the new gradients id to be similar to the cloned gradients
        auto new_id = generate_similar_unique_id(doc, gr->getId());
        gr->setAttribute("id", new_id.c_str());
        doc->getDefs()->getRepr()->addChild(repr, nullptr);
    } else {
        repr = xml_doc->createElement("svg:linearGradient");
        Inkscape::XML::Node *stop = xml_doc->createElement("svg:stop");
        stop->setAttribute("offset", "0");
        stop->setAttribute("style", "stop-color:#000;stop-opacity:1;");
        repr->appendChild(stop);
        Inkscape::GC::release(stop);
        stop = xml_doc->createElement("svg:stop");
        stop->setAttribute("offset", "1");
        stop->setAttribute("style", "stop-color:#fff;stop-opacity:1;");
        repr->appendChild(stop);
        Inkscape::GC::release(stop);
        doc->getDefs()->getRepr()->addChild(repr, nullptr);
        gr = cast<SPGradient>(doc->getObjectByRepr(repr));
    }

    _vectors->set_gradient(doc, gr);

    selectGradientInTree(gr);

    // assign gradient to selection
    vector_set(gr);

    Inkscape::GC::release(repr);
}

void GradientSelector::show_edit_button(bool show) {
    _edit->set_visible(show);
}

void GradientSelector::set_name_col_size(int min_width) {
    auto name_column = _treeview->get_column(1);
    name_column->set_min_width(min_width);
}

void GradientSelector::set_gradient_size(int width, int height) {
    _vectors->set_pixmap_size(width, height);
}

} // namespace Inkscape::UI::Widget

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
