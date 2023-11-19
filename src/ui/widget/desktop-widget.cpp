// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * Desktop widget implementation
 */
/* Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   MenTaLguY <mental@rydia.net>
 *   bulia byak <buliabyak@users.sf.net>
 *   Ralf Stephan <ralf@ark.in-berlin.de>
 *   John Bintz <jcoswell@coswellproductions.org>
 *   Johan Engelen <j.b.c.engelen@ewi.utwente.nl>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 2007 Johan Engelen
 * Copyright (C) 2006 John Bintz
 * Copyright (C) 2004 MenTaLguY
 * Copyright (C) 1999-2002 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "desktop-widget.h"

#include <algorithm>
#include <string>
#include <2geom/rect.h>
#include <glibmm/i18n.h>
#include <glibmm/ustring.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/paned.h>
#include <gtkmm/widget.h>

#include "conn-avoid-ref.h"
#include "desktop.h"
#include "display/control/canvas-item-drawing.h"
#include "document.h"
#include "document-undo.h"
#include "enums.h"
#include "inkscape.h"
#include "inkscape-window.h"
#include "object/sp-image.h"
#include "object/sp-namedview.h"
#include "ui/builder-utils.h"
#include "ui/dialog/dialog-container.h"
#include "ui/dialog/dialog-multipaned.h"
#include "ui/dialog/dialog-window.h"
#include "ui/dialog-run.h"
#include "ui/dialog/swatches.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/monitor.h"   // Monitor aspect ratio
#include "ui/pack.h"
#include "ui/shortcuts.h"
#include "ui/themes.h"
#include "ui/toolbar/command-toolbar.h"
#include "ui/toolbar/snap-toolbar.h"
#include "ui/toolbar/toolbar-constants.h"
#include "ui/toolbar/toolbars.h"
#include "ui/toolbar/tool-toolbar.h"
#include "ui/util.h"
#include "ui/widget/canvas-grid.h"
#include "ui/widget/canvas.h"
#include "ui/widget/color-palette.h"
#include "ui/widget/combo-tool-item.h"
#include "ui/widget/ink-ruler.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/status-bar.h"
#include "ui/widget/unit-tracker.h"
#include "util/units.h"
#include "widgets/widget-sizes.h"

using Inkscape::DocumentUndo;
using Inkscape::UI::Dialog::DialogContainer;
using Inkscape::UI::Dialog::DialogMultipaned;
using Inkscape::UI::Dialog::DialogWindow;
using Inkscape::UI::Widget::UnitTracker;

//---------------------------------------------------------------------
/* SPDesktopWidget */

SPDesktopWidget::SPDesktopWidget(InkscapeWindow *inkscape_window, SPDocument *document)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , _window{inkscape_window}
{
    set_name("SPDesktopWidget");

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    /* Status bar */
    _statusbar = Gtk::make_managed<Inkscape::UI::Widget::StatusBar>();
    Inkscape::UI::pack_end(*this, *_statusbar, false, true);

    /* Swatch Bar */
    _panels = Gtk::make_managed<Inkscape::UI::Dialog::SwatchesPanel>(true, "/embedded/swatches");
    _panels->set_vexpand(false);
    Inkscape::UI::pack_end(*this, *_panels, false, true);

    /* DesktopHBox (Vertical toolboxes, canvas) */
    _hbox = Gtk::make_managed<Gtk::Box>();
    _hbox->set_name("DesktopHbox");

    _tbbox = Gtk::make_managed<Gtk::Paned>(Gtk::Orientation::HORIZONTAL);
    _tbbox->set_name("ToolboxCanvasPaned");
    Inkscape::UI::pack_start(*_hbox, *_tbbox, true, true);

    Inkscape::UI::pack_end(*this, *_hbox, true, true);

    _top_toolbars = Gtk::make_managed<Gtk::Grid>();
    _top_toolbars->set_name("TopToolbars");
    Inkscape::UI::pack_end(*this, *_top_toolbars, false, true);

    /* Toolboxes */
    tool_toolbars = std::make_unique<Inkscape::UI::Toolbar::Toolbars>();
    _top_toolbars->attach(*tool_toolbars, 0, 1);

    tool_toolbox = Gtk::make_managed<Inkscape::UI::Toolbar::ToolToolbar>(inkscape_window);
    _tbbox->set_start_child(*tool_toolbox);
    _tbbox->set_resize_start_child(false);
    _tbbox->set_shrink_start_child(false);
    auto adjust_pos = [=, this](){
        int minimum_width, natural_width;
        int ignore;
        tool_toolbox->measure(Gtk::Orientation::HORIZONTAL, -1, minimum_width, natural_width, ignore, ignore);
        if (minimum_width > 0) {
            int pos = _tbbox->get_position();
            int new_pos = pos + minimum_width / 2;
            const auto max = 5; // max buttons in a row
            new_pos = std::min(new_pos - new_pos % minimum_width, max * minimum_width);
            if (pos != new_pos) _tbbox->set_position(new_pos);
        }
    };
    _tbbox->property_position().signal_changed().connect([=](){ adjust_pos(); });

    snap_toolbar = std::make_unique<Inkscape::UI::Toolbar::SnapToolbar>();
    Inkscape::UI::pack_end(*_hbox, *snap_toolbar, false, true); // May moved later.

    _tb_snap_pos = prefs->createObserver("/toolbox/simplesnap", sigc::mem_fun(*this, &SPDesktopWidget::repack_snaptoolbar));
    repack_snaptoolbar();

    auto tbox_width = prefs->getEntry("/toolbox/tools/width");
    if (tbox_width.isValid()) {
        _tbbox->set_position(tbox_width.getIntLimited(32, 8, 500));
    }

    auto set_toolbar_prefs = [=, this]() {
        int min = Inkscape::UI::Toolbar::min_pixel_size;
        int max = Inkscape::UI::Toolbar::max_pixel_size;
        int s = prefs->getIntLimited(Inkscape::UI::Toolbar::tools_icon_size, min, min, max);
        Inkscape::UI::set_icon_sizes(tool_toolbox->gobj(), s);
        adjust_pos();
    };

    // watch for changes
    _tb_icon_sizes1 = prefs->createObserver(Inkscape::UI::Toolbar::tools_icon_size,    [=]() { set_toolbar_prefs(); });
    _tb_icon_sizes2 = prefs->createObserver(Inkscape::UI::Toolbar::ctrlbars_icon_size, [this]() { apply_ctrlbar_settings(); });

    // restore preferences
    set_toolbar_prefs();

    /* Canvas Grid (canvas, rulers, scrollbars, etc.) */
    // desktop widgets owns it
    _canvas_grid = std::make_unique<Inkscape::UI::Widget::CanvasGrid>(this);

    /* Canvas */
    _canvas = _canvas_grid->GetCanvas();

    _ds_sticky_zoom = prefs->createObserver("/options/stickyzoom/value", [this]() { sticky_zoom_updated(); });
    sticky_zoom_updated();

    /* Dialog Container */
    _container = std::make_unique<DialogContainer>(inkscape_window);
    _columns = _container->get_columns();
    _columns->set_dropzone_sizes(2, -1);
    _tbbox->set_end_child(*_container);
    _tbbox->set_resize_end_child(true);
    _tbbox->set_shrink_end_child(true);

    _canvas_grid->set_hexpand(true);
    _canvas_grid->set_vexpand(true);
    _columns->append(*_canvas_grid.get());

    // ------------------ Finish Up -------------------- //
    _canvas_grid->ShowCommandPalette(false);

    _canvas->grab_focus();

    snap_toolbar->mode_update(); // Hide/show parts.

    SPNamedView *namedview = document->getNamedView();
    _dt2r = 1. / namedview->display_units->factor;

    // ---------- Desktop Dependent Setup -------------- //
    // This section seems backwards!
    _desktop = std::make_unique<SPDesktop>();
    _desktop->init(namedview, _canvas, this);
    _canvas->set_desktop(_desktop.get());
    INKSCAPE.add_desktop(_desktop.get());

    // Initialize the command toolbar only after contructing the desktop. Else, it'll crash.
    command_toolbar = std::make_unique<Inkscape::UI::Toolbar::CommandToolbar>(_desktop.get());
    _top_toolbars->attach(*command_toolbar, 0, 0);

    // Add the shape geometry to libavoid for autorouting connectors.
    // This needs desktop set for its spacing preferences.
    init_avoided_shape_geometry(_desktop.get());

    _statusbar->set_desktop(_desktop.get());

    /* Once desktop is set, we can update rulers */
    _canvas_grid->updateRulers();

    /* Listen on namedview modification */
    modified_connection = namedview->connectModified(sigc::mem_fun(*this, &SPDesktopWidget::namedviewModified));

    // tool_toolbars is an empty Gtk::Box at this point, fill it.
    tool_toolbars->create_toolbars(_desktop.get());

    layoutWidgets();

    _panels->setDesktop(_desktop.get());
}

void SPDesktopWidget::apply_ctrlbar_settings() {
    Inkscape::Preferences* prefs = Inkscape::Preferences::get();
    int min = Inkscape::UI::Toolbar::min_pixel_size;
    int max = Inkscape::UI::Toolbar::max_pixel_size;
    int size = prefs->getIntLimited(Inkscape::UI::Toolbar::ctrlbars_icon_size, min, min, max);
    Inkscape::UI::set_icon_sizes(snap_toolbar.get(), size);
    Inkscape::UI::set_icon_sizes(command_toolbar.get(), size);
    Inkscape::UI::set_icon_sizes(tool_toolbars.get(), size);
}

void
SPDesktopWidget::setMessage (Inkscape::MessageType type, const gchar *message)
{
    _statusbar->set_message (type, message);
}

/**
 * Called before SPDesktopWidget destruction.
 * (Might be called more than once)
 */
void
SPDesktopWidget::on_unrealize()
{
    if (_tbbox) {
        Inkscape::Preferences::get()->setInt("/toolbox/tools/width", _tbbox->get_position());
    }

    if (_desktop) {
        // Canvas
        _canvas->set_drawing(nullptr); // Ensures deactivation
        _canvas->set_desktop(nullptr); // Todo: Remove desktop dependency.

        _panels->setDesktop(nullptr);

        _container.reset(); // will unrealize _canvas

        INKSCAPE.remove_desktop(_desktop.get()); // clears selection and event_context
        modified_connection.disconnect();
        _desktop->destroy();
        _desktop.reset();
    }

    parent_type::on_unrealize();
}

SPDesktopWidget::~SPDesktopWidget() = default;

/**
 * Set the title in the desktop-window (if desktop has an own window).
 *
 * The title has form file name: desktop number - Inkscape.
 * The desktop number is only shown if it's 2 or higher,
 */
void
SPDesktopWidget::updateTitle(gchar const* uri)
{
    if (_window) {
        auto const doc = _desktop->doc();
        auto namedview = doc->getNamedView();

        std::string Name;
        if (doc->isModifiedSinceSave()) {
            Name += "*";
        }

        Name += uri;

        if (namedview->viewcount > 1) {
            Name += ": ";
            Name += std::to_string(namedview->viewcount);
        }
        Name += " (";

        auto const canvas = _desktop->getCanvas();
        auto const render_mode = canvas->get_render_mode();
        auto const color_mode  = canvas->get_color_mode();

        if (render_mode == Inkscape::RenderMode::OUTLINE) {
            Name += N_("outline");
        } else if (render_mode == Inkscape::RenderMode::NO_FILTERS) {
            Name += N_("no filters");
        } else if (render_mode == Inkscape::RenderMode::VISIBLE_HAIRLINES) {
            Name += N_("enhance thin lines");
        } else if (render_mode == Inkscape::RenderMode::OUTLINE_OVERLAY) {
            Name += N_("outline overlay");
        }

        if (color_mode != Inkscape::ColorMode::NORMAL &&
            render_mode != Inkscape::RenderMode::NORMAL) {
            Name += ", ";
        }

        if (color_mode == Inkscape::ColorMode::GRAYSCALE) {
            Name += N_("grayscale");
        } else if (color_mode == Inkscape::ColorMode::PRINT_COLORS_PREVIEW) {
            Name += N_("print colors preview");
        }

        if (*Name.rbegin() == '(') {  // Can not use C++11 .back() or .pop_back() with ustring!
            Name.erase(Name.size() - 2);
        } else {
            Name += ")";
        }

        Name += " - Inkscape";

        // Name += " (";
        // Name += Inkscape::version_string;
        // Name += ")";

        _window->set_title(Name);
    }
}

DialogContainer *SPDesktopWidget::getDialogContainer()
{
    return _container.get();
}

void SPDesktopWidget::showNotice(Glib::ustring const &msg, unsigned timeout)
{
    _canvas_grid->showNotice(msg, timeout);
}

/**
 * Callback to realize desktop widget.
 */
void SPDesktopWidget::on_realize()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    parent_type::on_realize();

    auto const d = Geom::Rect::from_xywh(Geom::Point{0, 0}, _desktop->doc()->getDimensions());
    if (d.width() < 1.0 || d.height() < 1.0) return;

    _desktop->set_display_area(d, 10);

    updateNamedview();

    auto const window = dynamic_cast<Gtk::Window *>(get_root());
    if (window) {
        auto const dark = INKSCAPE.themecontext->isCurrentThemeDark(window);
        prefs->setBool("/theme/darkTheme", dark);
        INKSCAPE.themecontext->getChangeThemeSignal().emit();
        INKSCAPE.themecontext->add_gtk_css(true);
    }
}

/* This is just to provide access to common functionality from sp_desktop_widget_realize() above
   as well as from SPDesktop::change_document() */
void SPDesktopWidget::updateNamedview()
{
    // Listen on namedview modification
    // originally (prior to the sigc++ conversion) the signal was simply
    // connected twice rather than disconnecting the first connection

    modified_connection = _desktop->getNamedView()->connectModified(sigc::mem_fun(*this, &SPDesktopWidget::namedviewModified));

    namedviewModified(_desktop->getNamedView(), SP_OBJECT_MODIFIED_FLAG);

    updateTitle(_desktop->doc()->getDocumentName());
}

void
SPDesktopWidget::update_guides_lock()
{
    bool down = _canvas_grid->GetGuideLock()->get_active();
    auto const nv = _desktop->getNamedView();
    bool lock = nv->getLockGuides();

    if (down != lock) {
        nv->toggleLockGuides();
        setMessage(Inkscape::NORMAL_MESSAGE, down ? _("Locked all guides") : _("Unlocked all guides"));
    }
}

void
SPDesktopWidget::enableInteraction()
{
  g_return_if_fail(_interaction_disabled_counter > 0);

  _interaction_disabled_counter--;

  if (_interaction_disabled_counter == 0) {
    this->set_sensitive();
  }
}

void
SPDesktopWidget::disableInteraction()
{
  if (_interaction_disabled_counter == 0) {
    this->set_sensitive(false);
  }

  _interaction_disabled_counter++;
}

void
SPDesktopWidget::setCoordinateStatus(Geom::Point p)
{
    _statusbar->set_coordinate(_dt2r * p);
}

void
SPDesktopWidget::letRotateGrabFocus()
{
    _statusbar->rotate_grab_focus();
}

void
SPDesktopWidget::letZoomGrabFocus()
{
    _statusbar->zoom_grab_focus();
}

void SPDesktopWidget::getWindowGeometry(int &x, int &y, int &w, int &h)
{
    if (_window) {
        // Note: On Wayland, which is GTK4's main platform, getting x and y is impossible.
        // GTK4 therefore removed support for it despite being possible on other platforms.
        // The code is left in place for the future in case the ability comes back.
        x = 0;
        y = 0;
        w = _window->get_width();
        h = _window->get_height();
    }
}

void SPDesktopWidget::setWindowPosition(Geom::Point)
{
    // See comment in getWindowGeometry.
}

void SPDesktopWidget::setWindowSize(int w, int h)
{
    if (_window) {
        _window->set_default_size(w, h);
    }
}

/**
 * \note transientizing does not work on windows; when you minimize a document
 * and then open it back, only its transient emerges and you cannot access
 * the document window. The document window must be restored by rightclicking
 * the taskbar button and pressing "Restore"
 */
void
SPDesktopWidget::setWindowTransient (void *p, int transient_policy)
{
    if (_window) {
        auto const w = GTK_WINDOW(_window->gobj());
        gtk_window_set_transient_for (GTK_WINDOW(p), w);

        /*
         * This enables "aggressive" transientization,
         * i.e. dialogs always emerging on top when you switch documents. Note
         * however that this breaks "click to raise" policy of a window
         * manager because the switched-to document will be raised at once
         * (so that its transients also could raise)
         */
        if (transient_policy == PREFS_DIALOGS_WINDOWS_AGGRESSIVE)
            // without this, a transient window not always emerges on top
            gtk_window_present (w);
    }
}

void
SPDesktopWidget::presentWindow()
{
    if (_window) {
        _window->present();
    }
}

void SPDesktopWidget::showInfoDialog(Glib::ustring const &message)
{
    if (!_window) return;

    Gtk::MessageDialog dialog{*_window, message, false, Gtk::MessageType::INFO, Gtk::ButtonsType::OK};
    dialog.property_destroy_with_parent() = true;
    dialog.set_name("InfoDialog");
    dialog.set_title(_("Note:")); // probably want to take this as a parameter.
    Inkscape::UI::dialog_run(dialog);
}

bool SPDesktopWidget::warnDialog (Glib::ustring const &text)
{
    Gtk::MessageDialog dialog{*_window, text, false, Gtk::MessageType::WARNING, Gtk::ButtonsType::OK_CANCEL};
    auto const response = Inkscape::UI::dialog_run(dialog);
    return response == Gtk::ResponseType::OK;
}

void
SPDesktopWidget::fullscreen()
{
    GtkWindow *topw = GTK_WINDOW(gtk_widget_get_root(_canvas->Gtk::Widget::gobj()));
    if (GTK_IS_WINDOW(topw)) {
        if (_desktop->is_fullscreen()) {
            gtk_window_unfullscreen(topw);
            // widget layout is triggered by the resulting window_state_event
        } else {
            gtk_window_fullscreen(topw);
            // widget layout is triggered by the resulting window_state_event
        }
    }
}

/**
 * Hide whatever the user does not want to see in the window.
 * Also move command toolbar to top or side as required.
 */
void SPDesktopWidget::layoutWidgets()
{
    Glib::ustring pref_root;
    auto prefs = Inkscape::Preferences::get();

    if (_desktop && _desktop->is_focusMode()) {
        pref_root = "/focus/";
    } else if (_desktop && _desktop->is_fullscreen()) {
        pref_root = "/fullscreen/";
    } else {
        pref_root = "/window/";
    }

    command_toolbar->set_visible(prefs->getBool(pref_root + "commands/state", true));

    snap_toolbar->set_visible(prefs->getBool(pref_root + "snaptoolbox/state", true));

    tool_toolbars->set_visible(prefs->getBool(pref_root + "toppanel/state", true));

    tool_toolbox->set_visible(prefs->getBool(pref_root + "toolbox/state", true));

    _statusbar->set_visible(prefs->getBool(pref_root + "statusbar/state", true));
    _statusbar->update_visibility(); // Individual items in bar

    _panels->set_visible(prefs->getBool(pref_root + "panels/state", true));

    _canvas_grid->ShowScrollbars(prefs->getBool(pref_root + "scrollbars/state", true));
    _canvas_grid->ShowRulers(    prefs->getBool(pref_root + "rulers/state",     true));

    // Move command toolbar as required.

    // If interface_mode unset, use screen aspect ratio. Needs to be synced with "canvas-interface-mode" action.
    Gdk::Rectangle monitor_geometry = Inkscape::UI::get_monitor_geometry_primary();
    double const width  = monitor_geometry.get_width();
    double const height = monitor_geometry.get_height();
    bool widescreen = (height > 0 && width/height > 1.65);
    widescreen = prefs->getBool(pref_root + "interface_mode", widescreen);

    // Unlink command toolbar.
    remove_from_top_toolbar_or_hbox(*command_toolbar);

    // Link command toolbar back.
    auto orientation_c = GTK_ORIENTATION_HORIZONTAL;
    if (!widescreen) {
        _top_toolbars->attach(*command_toolbar, 0, 0); // Always first in Grid
        command_toolbar->set_hexpand(true);
        orientation_c = GTK_ORIENTATION_HORIZONTAL;
    } else {
        _hbox->append(*command_toolbar);
        orientation_c = GTK_ORIENTATION_VERTICAL;
        command_toolbar->set_hexpand(false);
    }
    // Toolbar is actually child:
    Inkscape::UI::for_each_child(*command_toolbar, [=, this](Gtk::Widget &widget) {
        if (auto toolbar = dynamic_cast<Gtk::Box *>(&widget)) {
            gtk_orientable_set_orientation(GTK_ORIENTABLE(toolbar->gobj()), orientation_c); // Missing in C++interface!
        }
        return Inkscape::UI::ForEachResult::_continue;
    });

    repack_snaptoolbar();

    Inkscape::UI::resize_widget_children(_top_toolbars);
}

Gtk::Box *SPDesktopWidget::get_toolbar_by_name(const Glib::ustring &name)
{
    // The name is actually attached to the GtkGrid that contains
    // the toolbar, so we need to get the grid first
    auto widget = Inkscape::UI::find_widget_by_name(*tool_toolbars, name);
    auto grid = dynamic_cast<Gtk::Grid*>(widget);

    if (!grid) return nullptr;

    auto child = grid->get_child_at(0,0);
    auto tb = dynamic_cast<Gtk::Box *>(child);

    return tb;
}

void
SPDesktopWidget::setToolboxFocusTo (const gchar* label)
{
    // Look for a named widget
    auto hb = Inkscape::UI::find_widget_by_name(*tool_toolbars, label);
    if (hb) {
        hb->grab_focus();
    }
}

void
SPDesktopWidget::setToolboxAdjustmentValue (gchar const *id, double value)
{
    // Look for a named widget
    auto hb = Inkscape::UI::find_widget_by_name(*tool_toolbars, id);
    if (hb) {
        auto sb = dynamic_cast<Inkscape::UI::Widget::SpinButton *>(hb);
        auto a = sb->get_adjustment();

        if(a) a->set_value(value);
    } else {
        g_warning ("Could not find GtkAdjustment for %s\n", id);
    }
}

bool
SPDesktopWidget::isToolboxButtonActive(char const * const id) const
{
    auto const widget = const_cast<Gtk::Widget const *>(
        Inkscape::UI::find_widget_by_name(*tool_toolbars, id));

    if (!widget) {
        //g_message( "Unable to locate item for {%s}", id );
        return false;
    }

    if (auto const button = dynamic_cast<Gtk::ToggleButton const *>(widget)) {
        return button->get_active();
    }

    //g_message( "Item for {%s} is of an unsupported type", id );
    return false;
}

/**
 * Choose where to pack the snap toolbar.
 * Hiding/unhiding is done in the SnapToolbar widget.
 */
void SPDesktopWidget::repack_snaptoolbar()
{
    auto prefs = Inkscape::Preferences::get();
    bool is_perm = prefs->getInt("/toolbox/simplesnap", 1) == 2;
    auto &aux = *tool_toolbars;
    auto &snap = *snap_toolbar;

    // Only remove from the parent if the status has changed
    auto parent = snap.get_parent();
    if (parent && ((is_perm && parent != _hbox) || (!is_perm && parent != _top_toolbars))) {
        remove_from_top_toolbar_or_hbox(snap);
    }

    // Only repack if there's no parent widget now.
    if (!snap.get_parent()) {
        if (is_perm) {
            Inkscape::UI::pack_end(*_hbox, snap, false, true);
        } else {
            _top_toolbars->attach(snap, 1, 0, 1, 2);
        }
    }

    // Always reset the various constraints, even if not repacked.
    if (is_perm) {
        snap.set_valign(Gtk::Align::START);
        return;
    }

    // This ensures that the Snap toolbox is on the top and only takes the needed space.
    _top_toolbars->remove(aux);
    _top_toolbars->remove(snap);
    if (Inkscape::UI::get_children(*_top_toolbars).size() == 3 && command_toolbar->get_visible()) {
        _top_toolbars->attach(aux, 0, 1, 2, 1);
        _top_toolbars->attach(snap, 1, 0, 1, 2);
        snap.set_valign(Gtk::Align::START);
    } else {
        _top_toolbars->attach(aux, 0, 1, 1, 1);
        _top_toolbars->attach(snap, 1, 0, 2, 2);
        snap.set_valign(Gtk::Align::CENTER);
    }
}

void SPDesktopWidget::namedviewModified(SPObject *obj, guint flags)
{
    if (!(flags & SP_OBJECT_MODIFIED_FLAG)) return;

    auto nv = cast<SPNamedView>(obj);
    _dt2r = 1. / nv->display_units->factor;

    _canvas_grid->GetVRuler()->set_unit(nv->getDisplayUnit());
    _canvas_grid->GetHRuler()->set_unit(nv->getDisplayUnit());
    _canvas_grid->GetVRuler()->set_tooltip_text(gettext(nv->display_units->name_plural.c_str()));
    _canvas_grid->GetHRuler()->set_tooltip_text(gettext(nv->display_units->name_plural.c_str()));
    _canvas_grid->updateRulers();

    /* This loops through all the grandchildren of tool toolbars,
     * and for each that it finds, it performs an Inkscape::UI::find_widget_by_name(*),
     * looking for widgets named "unit-tracker" (this is used by
     * all toolboxes to refer to the unit selector). The default document units
     * is then selected within these unit selectors.
     *
     * This should solve: https://bugs.launchpad.net/inkscape/+bug/362995
     */
    for (auto const i : Inkscape::UI::get_children(*tool_toolbars)) {
        for (auto const j : Inkscape::UI::get_children(*i)) {
            // Don't apply to text toolbar. We want to be able to
            // use different units for text. (Bug 1562217)
            const Glib::ustring name = j->get_name();
            if ( name == "TextToolbar" || name == "MeasureToolbar" || name == "CalligraphicToolbar" )
                continue;

            auto const tracker = dynamic_cast<Inkscape::UI::Widget::ComboToolItem*>
                                             (Inkscape::UI::find_widget_by_name(*j, "unit-tracker"));
            if (tracker) { // it's null when inkscape is first opened
                if (auto ptr = static_cast<UnitTracker*>(tracker->get_data(Glib::Quark("unit-tracker")))) {
                    ptr->setActiveUnit(nv->display_units);
                }
            }
        } // grandchildren
    } // children
}

// We make the desktop window with focus active. Signal is connected in inkscape-window.cpp
void SPDesktopWidget::onFocus(bool const has_focus)
{
    if (!has_focus) return;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (prefs->getBool("/options/bitmapautoreload/value", true)) {
        auto const &imageList = _desktop->doc()->getResourceList("image");
        for (auto it : imageList) {
            auto image = cast<SPImage>(it);
            image->refresh_if_outdated();
        }
    }

    INKSCAPE.activate_desktop(_desktop.get());
}

// ------------------------ Zoom ------------------------

void
SPDesktopWidget::sticky_zoom_toggled()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setBool("/options/stickyzoom/value", _canvas_grid->GetStickyZoom()->get_active());
}

void
SPDesktopWidget::sticky_zoom_updated()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    _canvas_grid->GetStickyZoom()->set_active(prefs->getBool("/options/stickyzoom/value", false));
}

void
SPDesktopWidget::update_zoom()
{
    _statusbar->update_zoom();
}

// ---------------------- Rotation ------------------------

void
SPDesktopWidget::update_rotation()
{
    _statusbar->update_rotate();
}

// --------------- Rulers/Scrollbars/Etc. -----------------

void
SPDesktopWidget::toggle_command_palette() {
    // TODO: Turn into action and remove this function.
    _canvas_grid->ToggleCommandPalette();
}

void
SPDesktopWidget::toggle_rulers()
{
    // TODO: Turn into action and remove this function.
    _canvas_grid->ToggleRulers();
}

void
SPDesktopWidget::toggle_scrollbars()
{
    // TODO: Turn into action and remove this function.
    _canvas_grid->ToggleScrollbars();
}

Gio::ActionMap* SPDesktopWidget::get_action_map() {
    return _window;
}

void SPDesktopWidget::remove_from_top_toolbar_or_hbox(Gtk::Widget &widget)
{
    g_assert(_top_toolbars);
    g_assert(_hbox        );

    auto const parent = widget.get_parent();
    if (!parent) return;

    if (parent == _top_toolbars) {
        _top_toolbars->remove(widget);
    } else if (parent == _hbox) {
        _hbox->remove(widget);
    } else {
        g_critical("SPDesktopWidget::remove_from_top_toolbar_or_hbox(): unexpected parent!");
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
