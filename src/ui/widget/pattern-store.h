// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef INKSCAPE_UI_WIDGET_PATTERN_STORE_H
#define INKSCAPE_UI_WIDGET_PATTERN_STORE_H
/*
 * Copyright (C) 2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <optional>
#include <2geom/transforms.h>
#include <giomm/liststore.h>
#include <gtkmm/widget.h>
#include "color.h"
#include "ui/filtered-store.h"

class SPDocument;

namespace Inkscape {
namespace UI {
namespace Widget {

// pattern parameters
class PatternItem : public Glib::Object
{
public:
    Cairo::RefPtr<Cairo::Surface> pix;
    std::string id;
    std::string label;
    bool stock = false;
    bool uniform_scale = false;
    Geom::Affine transform;
    Geom::Point offset;
    std::optional<SPColor> color;
    Geom::Scale gap;
    SPDocument* collection = nullptr;

    bool operator == (const PatternItem& item) const {
        // compare all attributes apart from pixmap preview
        return
            id == item.id &&
            label == item.label &&
            stock == item.stock &&
            uniform_scale == item.uniform_scale &&
            transform == item.transform &&
            offset == item.offset &&
            color == item.color &&
            gap == item.gap &&
            collection == item.collection;
    }

    static Glib::RefPtr<PatternItem> create() {
        return Glib::make_refptr_for_instance(new PatternItem());
    }

protected:
    PatternItem() = default;
};

struct PatternStore {
    Inkscape::FilteredStore<PatternItem> store;
    std::map<Gtk::Widget*, Glib::RefPtr<PatternItem>> widgets_to_pattern;
};

}
}
}

#endif
