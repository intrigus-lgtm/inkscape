// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Gradient vector selection widget
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   MenTaLguY <mental@rydia.net>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 2001-2002 Lauris Kaplinski
 * Copyright (C) 2001 Ximian, Inc.
 * Copyright (C) 2004 Monash University
 * Copyright (C) 2004 David Turner
 * Copyright (C) 2006 MenTaLguY
 * Copyright (C) 2010 Jon A. Cruz
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 *
 */

#include "ui/widget/gradient-vector-selector.h"

#include <set>

#include <glibmm.h>
#include <glibmm/i18n.h>
#include <gdkmm/pixbuf.h>

#include "document.h"
#include "gradient-chemistry.h"
#include "preferences.h"

#include "object/sp-defs.h"
#include "object/sp-stop.h"

#include "ui/selected-color.h"
#include "ui/widget/gradient-image.h"

using Inkscape::UI::SelectedColor;

void gr_get_usage_counts(SPDocument *doc, std::map<SPGradient *, gint> *mapUsageCount );
unsigned long sp_gradient_to_hhssll(SPGradient *gr);

// TODO FIXME kill these globals!!!
static Glib::ustring const prefs_path = "/dialogs/gradienteditor/";

namespace Inkscape {
namespace UI {
namespace Widget {

GradientVectorSelector::GradientVectorSelector(SPDocument *doc, SPGradient *gr)
{
    _columns = new GradientSelector::ModelColumns();
    _store = Gtk::ListStore::create(*_columns);
    set_orientation(Gtk::Orientation::VERTICAL);

    if (doc) {
        set_gradient(doc, gr);
    } else {
        rebuild_gui_full();
    }
}

void GradientVectorSelector::set_gradient(SPDocument *doc, SPGradient *gr)
{
//     g_message("sp_gradient_vector_selector_set_gradient(%p, %p, %p) [%s] %d %d", gvs, doc, gr,
//               (gr ? gr->getId():"N/A"),
//               (gr ? gr->isSwatch() : -1),
//               (gr ? gr->isSolid() : -1));
    static gboolean suppress = FALSE;

    g_return_if_fail(!gr || (doc != nullptr));
    g_return_if_fail(!gr || (gr->document == doc));
    g_return_if_fail(!gr || gr->hasStops());

    if (doc != _doc) {
        /* Disconnect signals */
        if (_gr) {
            _gradient_release_connection.disconnect();
            _gr = nullptr;
        }
        if (_doc) {
            _defs_release_connection.disconnect();
            _defs_modified_connection.disconnect();
            _doc = nullptr;
        }

        // Connect signals
        if (doc) {
            _defs_release_connection = doc->getDefs()->connectRelease(sigc::mem_fun(*this, &GradientVectorSelector::defs_release));
            _defs_modified_connection = doc->getDefs()->connectModified(sigc::mem_fun(*this, &GradientVectorSelector::defs_modified));
        }
        if (gr) {
            _gradient_release_connection = gr->connectRelease(sigc::mem_fun(*this, &GradientVectorSelector::gradient_release));
        }
        _doc = doc;
        _gr = gr;
        rebuild_gui_full();
        if (!suppress) _signal_vector_set.emit(gr);
    } else if (gr != _gr) {
        // Harder case - keep document, rebuild list and stuff
        // fixme: (Lauris)
        suppress = TRUE;
        set_gradient(nullptr, nullptr);
        set_gradient(doc, gr);
        suppress = FALSE;
        _signal_vector_set.emit(gr);
    }
    /* The case of setting NULL -> NULL is not very interesting */
}

void
GradientVectorSelector::gradient_release(SPObject * /*obj*/)
{
    /* Disconnect gradient */
    if (_gr) {
        _gradient_release_connection.disconnect();
        _gr = nullptr;
    }

    /* Rebuild GUI */
    rebuild_gui_full();
}

void
GradientVectorSelector::defs_release(SPObject * /*defs*/)
{
    _doc = nullptr;

    _defs_release_connection.disconnect();
    _defs_modified_connection.disconnect();

    /* Disconnect gradient as well */
    if (_gr) {
        _gradient_release_connection.disconnect();
        _gr = nullptr;
    }

    /* Rebuild GUI */
    rebuild_gui_full();
}

void
GradientVectorSelector::defs_modified(SPObject *defs, guint flags)
{
    /* fixme: We probably have to check some flags here (Lauris) */
    rebuild_gui_full();
}

void
GradientVectorSelector::rebuild_gui_full()
{
    _tree_select_connection.block();

    /* Clear old list, if there is any */
    _store->clear();

    /* Pick up all gradients with vectors */
    std::vector<SPGradient *> gl;
    if (_gr) {
        auto gradients = _gr->document->getResourceList("gradient");
        for (auto gradient : gradients) {
            auto grad = cast<SPGradient>(gradient);
            if ( grad->hasStops() && (grad->isSwatch() == _swatched) ) {
                gl.push_back(cast<SPGradient>(gradient));
            }
        }
    }

    /* Get usage count of all the gradients */
    std::map<SPGradient *, gint> usageCount;
    gr_get_usage_counts(_doc, &usageCount);

    if (!_doc) {
        Gtk::TreeModel::Row row = *(_store->append());
        row[_columns->name] = _("No document selected");

    } else if (gl.empty()) {
        Gtk::TreeModel::Row row = *(_store->append());
        row[_columns->name] = _("No gradients in document");

    } else if (!_gr) {
        Gtk::TreeModel::Row row = *(_store->append());
        row[_columns->name] =  _("No gradient selected");

    } else {
        for (auto gr:gl) {
            unsigned long hhssll = sp_gradient_to_hhssll(gr);
            GdkPixbuf *pixb = sp_gradient_to_pixbuf (gr, _pix_width, _pix_height);
            Glib::ustring label = gr_prepare_label(gr);

            Gtk::TreeModel::Row row = *(_store->append());
            row[_columns->name] = label.c_str();
            row[_columns->color] = hhssll;
            row[_columns->refcount] = usageCount[gr];
            row[_columns->data] = gr;
            row[_columns->pixbuf] = Glib::wrap(pixb);
        }
    }

    _tree_select_connection.unblock();
}

void
GradientVectorSelector::setSwatched()
{
    _swatched = true;
    rebuild_gui_full();
}

void GradientVectorSelector::set_pixmap_size(int width, int height) {
    _pix_width = width;
    _pix_height = height;
}

} // namespace Widget
} // namespace UI
} // namespace Inkscape

Glib::ustring gr_prepare_label(SPObject *obj)
{
    const gchar *id = obj->label() ? obj->label() : obj->getId();
    if (!id) {
        id = obj->getRepr()->name();
    }

    if (strlen(id) > 14 && (!strncmp (id, "linearGradient", 14) || !strncmp (id, "radialGradient", 14)))
        return gr_ellipsize_text(id+14, 35);
    return gr_ellipsize_text (id, 35);
}

/*
 * Ellipse text if longer than maxlen, "50% start text + ... + ~50% end text"
 * Text should be > length 8 or just return the original text
 */
Glib::ustring gr_ellipsize_text(Glib::ustring const &src, size_t maxlen)
{
    if (src.length() > maxlen && maxlen > 8) {
        size_t p1 = (size_t) maxlen / 2;
        size_t p2 = (size_t) src.length() - (maxlen - p1 - 1);
        return src.substr(0, p1) + "…" + src.substr(p2);
    }
    return src;
}


/*
 *  Return a "HHSSLL" version of the first stop color so we can sort by it
 */
unsigned long sp_gradient_to_hhssll(SPGradient *gr)
{
    SPStop *stop = gr->getFirstStop();
    unsigned long rgba = stop->get_rgba32();
    float hsl[3];
    SPColor::rgb_to_hsl_floatv (hsl, SP_RGBA32_R_F(rgba), SP_RGBA32_G_F(rgba), SP_RGBA32_B_F(rgba));

    return ((int)(hsl[0]*100 * 10000)) + ((int)(hsl[1]*100 * 100)) + ((int)(hsl[2]*100 * 1));
}

/*
 * Map each gradient to its usage count for both fill and stroke styles
 */
void gr_get_usage_counts(SPDocument *doc, std::map<SPGradient *, gint> *mapUsageCount )
{
    if (!doc)
        return;

    for (auto item : sp_get_all_document_items(doc)) {
        if (!item->getId())
            continue;
        SPGradient *gr = nullptr;
        gr = sp_item_get_gradient(item, true); // fill
        if (gr) {
            mapUsageCount->count(gr) > 0 ? (*mapUsageCount)[gr] += 1 : (*mapUsageCount)[gr] = 1;
        }
        gr = sp_item_get_gradient(item, false); // stroke
        if (gr) {
            mapUsageCount->count(gr) > 0 ? (*mapUsageCount)[gr] += 1 : (*mapUsageCount)[gr] = 1;
        }
    }
}

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
