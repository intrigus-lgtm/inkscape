// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Context menu
 *
 * Authors:
 *   Tavmjong Bah
 *
 * Copyright (C) 2022 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_CONTEXTMENU_H
#define SEEN_CONTEXTMENU_H

#include <vector>
#include <glibmm/refptr.h>
#include <gtkmm/popovermenu.h>

namespace Gio {
class SimpleActionGroup;
} // namespace Gio

class SPDesktop;
class SPDocument;
class SPObject;
class SPItem;

/**
 * Implements the Inkscape context menu.
 */
class ContextMenu final : public Gtk::PopoverMenu
{
public:
    ContextMenu(SPDesktop *desktop, SPObject *object, bool hide_layers_and_objects_menu_item = false);

private:
    // Used for unlock and unhide actions
    Glib::RefPtr<Gio::SimpleActionGroup> action_group;
    std::vector<SPItem *> items_under_cursor;
    void unhide_or_unlock(SPDocument* document, bool unhide);
};
#endif // SEEN_CONTEXT_MENU_H

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
