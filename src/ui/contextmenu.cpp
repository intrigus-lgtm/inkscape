// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Context menu
 */
/* Authors:
 *   Tavmjong Bah
 *
 * Rewrite of code authored by:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Frank Felfe <innerspace@iname.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *   Kris De Gussem <Kris.DeGussem@gmail.com>
 *
 * Copyright (C) 2022 Tavmjong Bah
 * Copyright (C) 2012 Kris De Gussem
 * Copyright (C) 2010 authors
 * Copyright (C) 1999-2005 authors
 * Copyright (C) 2004 David Turner
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "contextmenu.h"

#include <glibmm/i18n.h>
#include <giomm/icon.h>
#include <giomm/menu.h>
#include <giomm/menuitem.h>
#include <giomm/simpleactiongroup.h>
#include <gtkmm/image.h>
#include <gtkmm/window.h>

#include "desktop.h"
#include "document.h"
#include "document-undo.h"
#include "layer-manager.h"
#include "page-manager.h"
#include "selection.h"
#include "object/sp-anchor.h"
#include "object/sp-image.h"
#include "object/sp-page.h"
#include "object/sp-shape.h"
#include "object/sp-text.h"
#include "ui/desktop/menu-set-tooltips-shift-icons.h"
#include "ui/menuize.h"
#include "ui/util.h"
#include "ui/widget/desktop-widget.h"

static void
AppendItemFromAction(Glib::RefPtr<Gio::Menu> const &gmenu,
                     Glib::ustring const &action,
                     Glib::ustring const &label,
                     Glib::ustring const &icon = {})
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool show_icons = prefs->getInt("/theme/menuIcons", true);

    auto menu_item = Gio::MenuItem::create(label, action);
    if (icon != "" && show_icons) {
        auto _icon = Gio::Icon::create(icon);
        menu_item->set_icon(_icon);
    }
    gmenu->append_item(menu_item);
}

/** @brief Create a menu section containing the standard editing actions:
 *         Cut, Copy, Paste.
 *
 *  @param paste_only If true, only the Paste action will be included.
 *  @return A new menu containing the requested actions.
 */
static Glib::RefPtr<Gio::Menu> create_clipboard_actions(bool const paste_only = false)
{
    auto result = Gio::Menu::create();
    if (!paste_only) {
        AppendItemFromAction(result, "app.cut",  _("Cu_t"),  "edit-cut");
        AppendItemFromAction(result, "app.copy", _("_Copy"), "edit-copy");
    }
    AppendItemFromAction(result, "win.paste", _("_Paste"), "edit-paste");
    return result;
}

/// Recursively force all Image descendants with :storage-type other than EMPTY to :visible = TRUE.
/// We have to do this this if using Gio::Menu with icons as GTK, in its vast genius, doesnʼt think
/// those should ever actually be visible in the majority of cases. So, we just have to fight it 🤷
/// We donʼt show images if :storage-type == EMPTY so that shift_icons() can select on :only-child.
void show_all_images(Gtk::Widget &parent)
{
    Inkscape::UI::for_each_descendant(parent, [=](Gtk::Widget &child)
    {
        if (auto const image = dynamic_cast<Gtk::Image *>(&child);
            image && image->get_storage_type() != Gtk::Image::Type::EMPTY)
        {
            image->set_visible(true);
        }
        return Inkscape::UI::ForEachResult::_continue;
    });
}

ContextMenu::ContextMenu(SPDesktop *desktop, SPObject *object, bool hide_layers_and_objects_menu_item)
{
    set_name("ContextMenu");

    auto item = cast<SPItem>(object);

    // std::cout << "ContextMenu::ContextMenu: " << (item ? item->getId() : "no item") << std::endl;
    action_group = Gio::SimpleActionGroup::create();
    insert_action_group("ctx", action_group);
    auto document = desktop->getDocument();
    action_group->add_action("unhide-objects-below-cursor", sigc::bind(sigc::mem_fun(*this, &ContextMenu::unhide_or_unlock), document, true));
    action_group->add_action("unlock-objects-below-cursor", sigc::bind(sigc::mem_fun(*this, &ContextMenu::unhide_or_unlock), document, false));

    auto gmenu         = Gio::Menu::create(); // Main menu
    auto gmenu_section = Gio::Menu::create(); // Section (used multiple times)

    auto layer = Inkscape::LayerManager::asLayer(item);  // Layers have their own context menu in the Object and Layers dialog.
    auto root = desktop->layerManager().currentRoot();

    // Get a list of items under the cursor, used for unhiding and unlocking.
    auto point_win = desktop->point() * desktop->d2w();
    items_under_cursor = document->getItemsAtPoints(desktop->dkey, {point_win}, true, false);
    bool has_hidden_below_cursor = false;
    bool has_locked_below_cursor = false;
    for (auto item : items_under_cursor) {
        if (item->isHidden()) {
            has_hidden_below_cursor = true;
        }
        if (item->isLocked()) {
            has_locked_below_cursor = true;
        }
    }
    // std::cout << "Items below cursor: " << items_under_cursor.size()
    //           << "  hidden: " << std::boolalpha << has_hidden_below_cursor
    //           << "  locked: " << std::boolalpha << has_locked_below_cursor
    //           << std::endl;

    // clang-format off

    // Undo/redo
    // gmenu_section = Gio::Menu::create();
    // AppendItemFromAction(gmenu_section, "doc.undo",      _("Undo"),       "edit-undo");
    // AppendItemFromAction(gmenu_section, "doc.redo",      _("Redo"),       "edit-redo");
    // gmenu->append_section(gmenu_section);

    if (auto page = cast<SPPage>(object)) {
        auto &page_manager = document->getPageManager();
        page_manager.selectPage(page);

        gmenu_section = Gio::Menu::create();
        AppendItemFromAction(gmenu_section, "doc.page-new", _("_New Page"), "pages-add");
        gmenu->append_section(gmenu_section);

        gmenu_section = Gio::Menu::create();
        AppendItemFromAction(gmenu_section, "doc.page-delete", _("_Delete Page"), "pages-remove");
        AppendItemFromAction(gmenu_section, "doc.page-move-backward", _("Move Page _Backward"), "pages-order-backwards");
        AppendItemFromAction(gmenu_section, "doc.page-move-forward", _("Move Page _Forward"), "pages-order-forwards");
        gmenu->append_section(gmenu_section);

    } else if (!layer || desktop->getSelection()->includes(layer)) {
        // "item" is the object that was under the mouse when right-clicked. It determines what is shown
        // in the menu thus it makes the most sense that it is either selected or part of the current
        // selection.
        auto selection = desktop->getSelection();
        bool selection_under_cursor = std::any_of(items_under_cursor.begin(), items_under_cursor.end(),
                [selection](auto item) { return selection->includes(item); });
        if (object && !selection_under_cursor) {
            selection->set(object);
        }

        if (!item) {
            // Even when there's no item, we should still have the Paste action on top
            // (see https://gitlab.com/inkscape/inkscape/-/issues/4150)
            gmenu->append_section(create_clipboard_actions(true));

            gmenu_section = Gio::Menu::create();
            AppendItemFromAction(gmenu_section, "win.dialog-open('DocumentProperties')", _("Document Properties..."), "document-properties");
            gmenu->append_section(gmenu_section);
        } else {
            // When an item is selected, show all three of Cut, Copy and Paste.
            gmenu->append_section(create_clipboard_actions());

            gmenu_section = Gio::Menu::create();
            AppendItemFromAction(gmenu_section, "app.duplicate", _("Duplic_ate"), "edit-duplicate");
            AppendItemFromAction(gmenu_section, "app.clone", _("_Clone"), "edit-clone");
            AppendItemFromAction(gmenu_section, "app.delete-selection", _("_Delete"), "edit-delete");
            gmenu->append_section(gmenu_section);

            // Dialogs
            auto gmenu_dialogs = Gio::Menu::create();
            if (!hide_layers_and_objects_menu_item) { // Hidden when context menu is popped up in Layers and Objects dialog!
                AppendItemFromAction(gmenu_dialogs, "win.dialog-open('Objects')",                   _("Layers and Objects..."), "dialog-objects"             );
            }
            AppendItemFromAction(gmenu_dialogs,     "win.dialog-open('ObjectProperties')",          _("_Object Properties..."), "dialog-object-properties"   );

            if (is<SPShape>(item) || is<SPText>(item) || is<SPGroup>(item)) {
                AppendItemFromAction(gmenu_dialogs, "win.dialog-open('FillStroke')",                _("_Fill and Stroke..."),   "dialog-fill-and-stroke"     );
            }

            // Image dialogs (mostly).
            if (auto image = cast<SPImage>(item)) {
                AppendItemFromAction(     gmenu_dialogs, "win.dialog-open('ObjectAttributes')",          _("Image _Properties..."),  "dialog-fill-and-stroke");
                AppendItemFromAction(     gmenu_dialogs, "win.dialog-open('Trace')",                     _("_Trace Bitmap..."),      "bitmap-trace"          );

                if (image->getClipObject()) {
                    AppendItemFromAction( gmenu_dialogs, "app.element-image-crop",                       _("Crop Image to Clip"),    ""                      );
                }
                if (strncmp(image->href, "data", 4) == 0) {
                    // Image is embedded.
                    AppendItemFromAction( gmenu_dialogs, "app.org.inkscape.filter.extract-image",        _("Extract Image..."),      ""                      );
                } else {
                    // Image is linked.
                    AppendItemFromAction( gmenu_dialogs, "app.org.inkscape.filter.selected.embed-image", _("Embed Image"),           ""                      );
                    AppendItemFromAction( gmenu_dialogs, "app.element-image-edit",                       _("Edit Externally..."),    ""                      );
                }
            }

            // Text dialogs.
            if (is<SPText>(item)) {
                AppendItemFromAction(     gmenu_dialogs, "win.dialog-open('Text')",                      _("_Text and Font..."),     "dialog-text-and-font"  );
                AppendItemFromAction(     gmenu_dialogs, "win.dialog-open('Spellcheck')",                _("Check Spellin_g..."),    "tools-check-spelling"  );
            }
            gmenu->append_section(gmenu_dialogs); // We might add to it later...

            if (!is<SPAnchor>(item)) {
                // Item menu

                // Selection
                gmenu_section = Gio::Menu::create();
                auto gmenu_submenu = Gio::Menu::create();
                AppendItemFromAction(     gmenu_submenu, "win.select-same-fill-and-stroke",     _("Fill _and Stroke"),      "edit-select-same-fill-and-stroke");
                AppendItemFromAction(     gmenu_submenu, "win.select-same-fill",                _("_Fill Color"),           "edit-select-same-fill"           );
                AppendItemFromAction(     gmenu_submenu, "win.select-same-stroke-color",        _("_Stroke Color"),         "edit-select-same-stroke-color"   );
                AppendItemFromAction(     gmenu_submenu, "win.select-same-stroke-style",        _("Stroke St_yle"),         "edit-select-same-stroke-style"   );
                AppendItemFromAction(     gmenu_submenu, "win.select-same-object-type",         _("_Object Type"),          "edit-select-same-object-type"    );
                gmenu_section->append_submenu(_("Select Sa_me"), gmenu_submenu);
                gmenu->append_section(gmenu_section);

                // Groups and Layers
                gmenu_section = Gio::Menu::create();
                AppendItemFromAction(     gmenu_section, "win.selection-move-to-layer",         _("_Move to Layer..."),     ""                                );
                AppendItemFromAction(     gmenu_section, "app.selection-link",                  _("Create Anchor (Hyperlink)"),   ""                          );
                AppendItemFromAction(     gmenu_section, "app.selection-group",                 _("_Group"),                ""                                );
                if (is<SPGroup>(item)) {
                    AppendItemFromAction( gmenu_section, "app.selection-ungroup",               _("_Ungroup"),              ""                                );
                    Glib::ustring label = Glib::ustring::compose(_("Enter Group %1"), item->defaultLabel());
                    AppendItemFromAction( gmenu_section, "win.selection-group-enter",           label,                      ""                                );
                    if (!layer && (item->getParentGroup()->isLayer() || item->getParentGroup() == root)) {
                        // A layer should be a child of root or another layer.
                        AppendItemFromAction( gmenu_section, "win.layer-from-group",            _("Group to Layer"),        ""                                );
                    }
                }
                auto group = cast<SPGroup>(item->parent);
                if (group && !group->isLayer()) {
                    AppendItemFromAction( gmenu_section, "win.selection-group-exit",            _("Exit Group"),            ""                                );
                    AppendItemFromAction( gmenu_section, "app.selection-ungroup-pop",           _("_Pop Selection out of Group"), ""                          );
                }
                gmenu->append_section(gmenu_section);

                // Clipping and Masking
                gmenu_section = Gio::Menu::create();
                if (selection->size() > 1) {
                    AppendItemFromAction( gmenu_section, "app.object-set-clip",                 _("Set Cl_ip"),             ""                                );
                }
                if (item->getClipObject()) {
                    AppendItemFromAction( gmenu_section, "app.object-release-clip",             _("Release C_lip"),         ""                                );
                } else {
                    AppendItemFromAction( gmenu_section, "app.object-set-clip-group",           _("Set Clip G_roup"),       ""                                );
                }
                if (selection->size() > 1) {
                    AppendItemFromAction( gmenu_section, "app.object-set-mask",                 _("Set Mask"),              ""                                );
                }
                if (item->getMaskObject()) {
                    AppendItemFromAction( gmenu_section, "app.object-release-mask",             _("Release Mask"),          ""                                );
                }
                gmenu->append_section(gmenu_section);

                // Hide and Lock
                gmenu_section = Gio::Menu::create();
                AppendItemFromAction(     gmenu_section, "app.selection-hide",                  _("Hide Selected Objects"), ""                                );
                AppendItemFromAction(     gmenu_section, "app.selection-lock",                  _("Lock Selected Objects"), ""                                );
                gmenu->append_section(gmenu_section);

            } else {
                // Anchor menu
                gmenu_section = Gio::Menu::create();
                AppendItemFromAction(     gmenu_section, "win.dialog-open('ObjectAttributes')", _("Link _Properties..."),   ""                                );
                AppendItemFromAction(     gmenu_section, "app.element-a-open-link",             _("_Open Link in Browser"), ""                                );
                AppendItemFromAction(     gmenu_section, "app.selection-ungroup",               _("_Remove Link"),          ""                                );
                AppendItemFromAction(     gmenu_section, "win.selection-group-enter",           _("Enter Group"),           ""                                );
                gmenu->append_section(gmenu_section);
            }
        }

        // Hidden or locked beneath cursor
        gmenu_section = Gio::Menu::create();
        if (has_hidden_below_cursor) {
            AppendItemFromAction( gmenu_section, "ctx.unhide-objects-below-cursor",     _("Unhide Objects Below Cursor"),  ""                         );
        }
        if (has_locked_below_cursor) {
            AppendItemFromAction( gmenu_section, "ctx.unlock-objects-below-cursor",     _("Unlock Objects Below Cursor"),  ""                         );
        }
        gmenu->append_section(gmenu_section);
    } else {
        // Layers: Only used in "Layers and Objects" dialog.

        gmenu_section = Gio::Menu::create();
        AppendItemFromAction(gmenu_section,     "win.layer-new",                _("_Add Layer..."),              "layer-new");
        AppendItemFromAction(gmenu_section,     "win.layer-duplicate",          _("D_uplicate Layer"),           "layer-duplicate");
        AppendItemFromAction(gmenu_section,     "win.layer-delete",             _("_Delete Layer"),              "layer-delete");
        AppendItemFromAction(gmenu_section,     "win.layer-rename",             _("Re_name Layer..."),           "layer-rename");
        AppendItemFromAction(gmenu_section,     "win.layer-to-group",           _("Layer to _Group"),            "dialog-objects");
        gmenu->append_section(gmenu_section);

        gmenu_section = Gio::Menu::create();
        AppendItemFromAction(gmenu_section,     "win.layer-raise",              _("_Raise Layer"),               "layer-raise");
        AppendItemFromAction(gmenu_section,     "win.layer-lower",              _("_Lower Layer"),               "layer-lower");
        gmenu->append_section(gmenu_section);

        gmenu_section = Gio::Menu::create();
        AppendItemFromAction(gmenu_section,     "win.layer-hide-toggle-others", _("_Hide/Show Other Layers"),    "");
        AppendItemFromAction(gmenu_section,     "win.layer-hide-all",           _("_Hide All Layers"),           "");
        AppendItemFromAction(gmenu_section,     "win.layer-unhide-all",         _("_Show All Layers"),           "");
        gmenu->append_section(gmenu_section);

        gmenu_section = Gio::Menu::create();
        AppendItemFromAction(gmenu_section,     "win.layer-lock-toggle-others", _("_Lock/Unlock Other Layers"),  "");
        AppendItemFromAction(gmenu_section,     "win.layer-lock-all",           _("_Lock All Layers"),           "");
        AppendItemFromAction(gmenu_section,     "win.layer-unlock-all",         _("_Unlock All Layers"),         "");
        gmenu->append_section(gmenu_section);
    }
    // clang-format on

    auto const widget = desktop->getDesktopWidget();
    g_assert(widget);
    set_parent(*widget);
    set_menu_model(gmenu);
    set_position(Gtk::PositionType::BOTTOM);
    set_has_arrow(false);
    show_all_images(*this);
    Inkscape::UI::menuize_popover(*this);

    // Do not install this CSS provider; it messes up menus with icons (like popup menu with all dialogs).
    // It doesn't work well with context menu either, introducing disturbing visual glitch 
    // where menu shifts upon opening.
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool const shift_icons = prefs->getInt("/theme/shiftIcons", true);
    set_tooltips_and_shift_icons(*this, shift_icons);
    // Set the style and icon theme of the new menu based on the desktop
    if (Gtk::Window *window = desktop->getToplevel()) {
        if (window->has_css_class("dark")) {
            add_css_class("dark");
        } else {
            add_css_class("bright");
        }
        if (prefs->getBool("/theme/symbolicIcons", false)) {
            add_css_class("symbolic");
        } else {
            add_css_class("regular");
        }
    }
}

void
ContextMenu::unhide_or_unlock(SPDocument* document, bool unhide)
{
    for (auto item : items_under_cursor) {
        if (unhide) {
            if (item->isHidden()) {
                item->setHidden(false);
            }
        } else {
            if (item->isLocked()) {
                item->setLocked(false);
            }
        }
    }

    // We wouldn't be here if we didn't make a change.
    Inkscape::DocumentUndo::done(document, (unhide ? _("Unhid objects") : _("Unlocked objects")), "");
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
