// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A widget for controlling object compositing (filter, opacity, etc.)
 *
 * Authors:
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *   Gustav Broberg <broberg@kth.se>
 *   Niko Kiirala <niko@kiirala.com>
 *   Abhishek Sharma
 *
 * Copyright (C) 2004--2008 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "object-composite-settings.h"

#include <utility>

#include "desktop.h"
#include "desktop-style.h"
#include "document-undo.h"
#include "filter-chemistry.h"
#include "style.h"
#include "svg/css-ostringstream.h"
#include "ui/pack.h"
#include "ui/widget/style-subject.h"

constexpr double BLUR_MULTIPLIER = 4.0;

namespace Inkscape {
namespace UI {
namespace Widget {

ObjectCompositeSettings::ObjectCompositeSettings(Glib::ustring icon_name, char const *history_prefix, int flags)
: Gtk::Box(Gtk::Orientation::VERTICAL),
  _icon_name(std::move(icon_name)),
  _blend_tag(Glib::ustring(history_prefix) + ":blend"),
  _blur_tag(Glib::ustring(history_prefix) + ":blur"),
  _opacity_tag(Glib::ustring(history_prefix) + ":opacity"),
  _isolation_tag(Glib::ustring(history_prefix) + ":isolation"),
  _filter_modifier(flags),
  _blocked(false)
{
    set_name( "ObjectCompositeSettings");

    // Filter Effects
    UI::pack_start(*this, _filter_modifier, false, false, 2);

    _filter_modifier.signal_blend_changed().connect(sigc::mem_fun(*this, &ObjectCompositeSettings::_blendBlurValueChanged));
    _filter_modifier.signal_blur_changed().connect(sigc::mem_fun(*this, &ObjectCompositeSettings::_blendBlurValueChanged));
    _filter_modifier.signal_opacity_changed().connect(sigc::mem_fun(*this, &ObjectCompositeSettings::_opacityValueChanged));
    _filter_modifier.signal_isolation_changed().connect(
        sigc::mem_fun(*this, &ObjectCompositeSettings::_isolationValueChanged));
}

ObjectCompositeSettings::~ObjectCompositeSettings() {
    setSubject(nullptr);
}

void ObjectCompositeSettings::setSubject(StyleSubject *subject) {
    _subject_changed.disconnect();
    if (subject) {
        _subject = subject;
        _subject_changed = _subject->connectChanged(sigc::mem_fun(*this, &ObjectCompositeSettings::_subjectChanged));
    }
}

// We get away with sharing one callback for blend and blur as this is used by
//  * the Layers dialog where only one layer can be selected at a time,
//  * the Fill and Stroke dialog where only blur is used.
// If both blend and blur are used in a dialog where more than one object can
// be selected then this should be split into separate functions for blend and
// blur (like in the Objects dialog).
void
ObjectCompositeSettings::_blendBlurValueChanged()
{
    if (!_subject) {
        return;
    }

    SPDesktop *desktop = _subject->getDesktop();
    if (!desktop) {
        return;
    }
    SPDocument *document = desktop->getDocument();

    if (_blocked)
        return;
    _blocked = true;

    Geom::OptRect bbox = _subject->getBounds(SPItem::GEOMETRIC_BBOX);
    double radius;
    if (bbox) {
        double perimeter = bbox->dimensions()[Geom::X] + bbox->dimensions()[Geom::Y];   // fixme: this is only half the perimeter, is that correct?
        double blur_value = _filter_modifier.get_blur_value() / 100.0;
        radius = blur_value * blur_value * perimeter / BLUR_MULTIPLIER;
    } else {
        radius = 0;
    }

    //apply created filter to every selected item
    std::vector<SPObject*> sel = _subject->list();
    for (auto i : sel) {
        if (!is<SPItem>(i)) {
            continue;
        }
        auto item = cast<SPItem>(i);
        SPStyle *style = item->style;
        g_assert(style != nullptr);
        bool change_blend = set_blend_mode(item, _filter_modifier.get_blend_mode());

        if (radius == 0 && item->style->filter.set && item->style->getFilter()
            && filter_is_single_gaussian_blur(item->style->getFilter())) {
            remove_filter(item, false);
        } else if (radius != 0) {
            SPFilter *filter = modify_filter_gaussian_blur_from_item(document, item, radius);
            filter->update_filter_region(item);
            sp_style_set_property_url(item, "filter", filter, false);
        } 
        if (change_blend) {
            ; // update done already
        } else {
            item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG);
        }
    }

    DocumentUndo::maybeDone(document, _blur_tag.c_str(), _("Change blur/blend filter"), _icon_name);

    _blocked = false;
}

void
ObjectCompositeSettings::_opacityValueChanged()
{
    if (!_subject) {
        return;
    }

    SPDesktop *desktop = _subject->getDesktop();
    if (!desktop) {
        return;
    }

    if (_blocked)
        return;
    _blocked = true;

    SPCSSAttr *css = sp_repr_css_attr_new ();

    Inkscape::CSSOStringStream os;
    os << CLAMP (_filter_modifier.get_opacity_value() / 100, 0.0, 1.0);
    sp_repr_css_set_property (css, "opacity", os.str().c_str());

    _subject->setCSS(css);

    sp_repr_css_attr_unref (css);

    DocumentUndo::maybeDone(desktop->getDocument(), _opacity_tag.c_str(), _("Change opacity"), _icon_name);

    _blocked = false;
}

void ObjectCompositeSettings::_isolationValueChanged()
{
    if (!_subject) {
        return;
    }

    SPDesktop *desktop = _subject->getDesktop();
    if (!desktop) {
        return;
    }

    if (_blocked)
        return;
    _blocked = true;

    for (auto item : _subject->list()) {
        item->style->isolation.set = TRUE;
        item->style->isolation.value = _filter_modifier.get_isolation_mode();
        if (item->style->isolation.value == SP_CSS_ISOLATION_ISOLATE) {
            item->style->mix_blend_mode.set = TRUE;
            item->style->mix_blend_mode.value = SP_CSS_BLEND_NORMAL;
        }
        item->updateRepr(SP_OBJECT_WRITE_NO_CHILDREN | SP_OBJECT_WRITE_EXT);
    }

    DocumentUndo::maybeDone(desktop->getDocument(), _isolation_tag.c_str(), _("Change isolation"), _icon_name);

    _blocked = false;
}

void
ObjectCompositeSettings::_subjectChanged() {
    if (!_subject) {
        return;
    }

    SPDesktop *desktop = _subject->getDesktop();
    if (!desktop) {
        return;
    }

    if (_blocked)
        return;
    _blocked = true;
    SPStyle query(desktop->getDocument());
    int result = _subject->queryStyle(&query, QUERY_STYLE_PROPERTY_MASTEROPACITY);

    switch (result) {
        case QUERY_STYLE_NOTHING:
            break;
        case QUERY_STYLE_SINGLE:
        case QUERY_STYLE_MULTIPLE_AVERAGED: // TODO: treat this slightly differently
        case QUERY_STYLE_MULTIPLE_SAME:
            _filter_modifier.set_opacity_value(100 * SP_SCALE24_TO_FLOAT(query.opacity.value));
            break;
    }

    //query now for current filter mode and average blurring of selection
    const int isolation_result = _subject->queryStyle(&query, QUERY_STYLE_PROPERTY_ISOLATION);
    switch (isolation_result) {
        case QUERY_STYLE_NOTHING:
            _filter_modifier.set_isolation_mode(SP_CSS_ISOLATION_AUTO, false);
            break;
        case QUERY_STYLE_SINGLE:
        case QUERY_STYLE_MULTIPLE_SAME:
            _filter_modifier.set_isolation_mode(query.isolation.value, true); // here dont work mix_blend_mode.set
            break;
        case QUERY_STYLE_MULTIPLE_DIFFERENT:
            _filter_modifier.set_isolation_mode(SP_CSS_ISOLATION_AUTO, false);
            // TODO: set text
            break;
    }

    // query now for current filter mode and average blurring of selection
    const int blend_result = _subject->queryStyle(&query, QUERY_STYLE_PROPERTY_BLEND);
    switch(blend_result) {
        case QUERY_STYLE_NOTHING:
            _filter_modifier.set_blend_mode(SP_CSS_BLEND_NORMAL, false);
            break;
        case QUERY_STYLE_SINGLE:
        case QUERY_STYLE_MULTIPLE_SAME:
            _filter_modifier.set_blend_mode(query.mix_blend_mode.value, true); // here dont work mix_blend_mode.set
            break;
        case QUERY_STYLE_MULTIPLE_DIFFERENT:
            _filter_modifier.set_blend_mode(SP_CSS_BLEND_NORMAL, false);
            break;
    }

    int blur_result = _subject->queryStyle(&query, QUERY_STYLE_PROPERTY_BLUR);
    switch (blur_result) {
        case QUERY_STYLE_NOTHING: // no blurring
            _filter_modifier.set_blur_value(0);
            break;
        case QUERY_STYLE_SINGLE:
        case QUERY_STYLE_MULTIPLE_AVERAGED:
        case QUERY_STYLE_MULTIPLE_SAME:
            Geom::OptRect bbox = _subject->getBounds(SPItem::GEOMETRIC_BBOX);
            if (bbox) {
                double perimeter =
                    bbox->dimensions()[Geom::X] +
                    bbox->dimensions()[Geom::Y]; // fixme: this is only half the perimeter, is that correct?
                // update blur widget value
                float radius = query.filter_gaussianBlur_deviation.value;
                float percent = std::sqrt(radius * BLUR_MULTIPLIER / perimeter) * 100;
                _filter_modifier.set_blur_value(percent);
            }
            break;
    }

    // If we have nothing selected, disable dialog.
    if (result       == QUERY_STYLE_NOTHING &&
        blend_result == QUERY_STYLE_NOTHING ) {
        _filter_modifier.set_sensitive( false );
    } else {
        _filter_modifier.set_sensitive( true );
    }

    _blocked = false;
}

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
