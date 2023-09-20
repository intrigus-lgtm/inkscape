// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A bare minimum example of deriving from Inkscape::UI:Widget::Panel.
 *
 * Author:
 *   Tavmjong Bah
 *
 * Copyright (C) Tavmjong Bah <tavmjong@free.fr>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifdef DEBUG

#include "prototype.h"

#include "document.h"
#include "inkscape-application.h"

// Only for use in demonstration widget.
#include "object/sp-root.h"

namespace Inkscape {
namespace UI {
namespace Dialog {

Prototype::Prototype()
    : DialogBase("/dialogs/prototype", "Prototype")
{
    // A widget for demonstration that displays the current SVG's id.
    _label = Gtk::make_managed<Gtk::Label>(_name);
    _label->set_wrap();

    _debug_button.set_name("PrototypeDebugButton");
    _debug_button.set_hexpand();
    _debug_button.signal_clicked().connect(sigc::mem_fun(*this, &Prototype::on_click));

    _debug_button.add(*_label);
    add(_debug_button);
}

void Prototype::documentReplaced()
{
    if (document && document->getRoot()) {
        const gchar *root_id = document->getRoot()->getId();
        Glib::ustring label_string("Document's SVG id: ");
        label_string += (root_id ? root_id : "null");
        _label->set_label(label_string);
    }
}

void Prototype::selectionChanged(Inkscape::Selection *selection)
{
    if (!selection) {
        return;
    }

    // Update demonstration widget.
    Glib::ustring label = _label->get_text() + "\nSelection changed to ";
    SPObject* object = selection->single();
    if (object) {
        label = label + object->getId();
    } else {
        object = selection->activeContext();

        if (object) {
            label = label + object->getId();
        } else {
            label = label + "unknown";
        }
    }

    _label->set_label(label);
}

void Prototype::on_click()
{
    auto window = dynamic_cast<Gtk::Window*>(get_root());
    if (window) {
        std::cerr << "Dialog is part of: " << window->get_name() << "  (" << window->get_title() << ")" << std::endl;
    } else {
        std::cerr << "Prototype::on_click(): Dialog not attached to window!" << std::endl;
    }
}

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif // DEBUG

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
