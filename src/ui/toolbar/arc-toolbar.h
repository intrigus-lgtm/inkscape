// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_ARC_TOOLBAR_H
#define SEEN_ARC_TOOLBAR_H

/**
 * @file
 * 3d box aux toolbar
 */
/* Authors:
 *   MenTaLguY <mental@rydia.net>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Frank Felfe <innerspace@iname.com>
 *   John Cliff <simarilius@yahoo.com>
 *   David Turner <novalis@gnu.org>
 *   Josh Andler <scislac@scislac.com>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *   Tavmjong Bah <tavmjong@free.fr>
 *   Abhishek Sharma
 *   Kris De Gussem <Kris.DeGussem@gmail.com>
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Copyright (C) 2004 David Turner
 * Copyright (C) 2003 MenTaLguY
 * Copyright (C) 1999-2011 authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "toolbar.h"
#include "xml/node-observer.h"

namespace Gtk {
class Button;
class Builder;
class Label;
class ToggleButton;
} // namespace Gtk

class SPDesktop;
class SPItem;

namespace Inkscape {
class Selection;

namespace XML {
class Node;
}

namespace UI {
namespace Tools {
class ToolBase;
}

namespace Widget {
class SpinButton;
class UnitTracker;
}

namespace Toolbar {

class ArcToolbar final
    : public Toolbar
    , private XML::NodeObserver
{
public:
    ArcToolbar(SPDesktop *desktop);
    ~ArcToolbar() override;

private:
    Glib::RefPtr<Gtk::Builder> _builder;
    std::unique_ptr<UI::Widget::UnitTracker> _tracker;

    UI::Widget::SpinButton &_rx_item;
    UI::Widget::SpinButton &_ry_item;
    UI::Widget::SpinButton &_start_item;
    UI::Widget::SpinButton &_end_item;

    Gtk::Label &_mode_item;

    std::vector<Gtk::ToggleButton *> _type_buttons;
    Gtk::Button &_make_whole;

    bool _freeze{false};
    bool _single;

    XML::Node *_repr{nullptr};
    SPItem *_item;

    void setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name);
    void setup_startend_button(UI::Widget::SpinButton &btn, Glib::ustring const &name);
    void value_changed(Glib::RefPtr<Gtk::Adjustment> &adj, Glib::ustring const &value_name);
    void startend_value_changed(Glib::RefPtr<Gtk::Adjustment> &adj, Glib::ustring const &value_name,
                                Glib::RefPtr<Gtk::Adjustment> &other_adj);
    void type_changed( int type );
    void defaults();
    void sensitivize( double v1, double v2 );
    void check_ec(SPDesktop* desktop, Inkscape::UI::Tools::ToolBase* tool);
    void selection_changed(Inkscape::Selection *selection);

    sigc::connection _changed;

    void notifyAttributeChanged(Inkscape::XML::Node &node, GQuark name, Inkscape::Util::ptr_shared old_value,
                                Inkscape::Util::ptr_shared new_value) final;
};

}
}
}

#endif /* !SEEN_ARC_TOOLBAR_H */
