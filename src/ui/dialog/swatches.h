// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief Color swatches dialog
 */
/* Authors:
 *   Jon A. Cruz
 *   PBS <pbs3141@gmail.com>
 *
 * Copyright (C) 2022 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef UI_DIALOG_SWATCHES_H
#define UI_DIALOG_SWATCHES_H

#include <array>
#include <utility>
#include <variant>
#include <vector>
#include <boost/unordered_map.hpp>

#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/widget.h>

#include "preferences.h"  // PrefObserver
#include "ui/dialog/dialog-base.h"
#include "ui/dialog/global-palettes.h"
#include "ui/widget/palette_t.h"

namespace Gtk {
class Builder;
class Button;
class Label;
class MenuButton;
class ToggleButton;
} // namespace Gtk

class SPGradient;

namespace Inkscape::UI {

namespace Widget {
class ColorPalette;
class PopoverMenu;
} // namespace Widget

namespace Dialog {

class ColorItem;

/**
 * A dialog that displays paint swatches.
 *
 * It comes in two flavors, depending on the prefsPath argument passed to
 * the constructor: the default "/dialog/swatches" is just a regular dialog;
 * the "/embedded/swatches" is the horizontal color palette at the bottom
 * of the window.
 */
class SwatchesPanel final : public DialogBase
{
public:
    SwatchesPanel(bool compact, char const *prefsPath = "/dialogs/swatches");
    ~SwatchesPanel() final;

private:
    void documentReplaced() final;
    void desktopReplaced() final;
    void selectionChanged(Selection *selection) final;
    void selectionModified(Selection *selection, guint flags) final;

    void size_allocate_vfunc(int width, int height, int baseline) final;

    void update_palettes(bool compact);
    void rebuild();
    bool load_swatches();
    bool load_swatches(std::string const &path);
    void update_loaded_palette_entry();
    void setup_selector_menu();
    bool on_selector_key_pressed(GtkEventControllerKey const * controller,
                                 unsigned keyval, unsigned keycode, GdkModifierType state);
    void update_selector_menu();
    void update_selector_label(Glib::ustring const &active_id);
    void clear_filter();
    void filter_colors(const Glib::ustring& text);
    bool filter_callback(const Dialog::ColorItem& color) const;

    Inkscape::UI::Widget::ColorPalette *_palette;

    Glib::ustring _current_palette_id;
    void set_palette(const Glib::ustring& id);
    void select_palette(const Glib::ustring& id);
    const PaletteFileData* get_palette(const Glib::ustring& id);

    // Asynchronous update mechanism.
    sigc::connection conn_gradients;
    sigc::connection conn_defs;
    bool gradients_changed = false;
    bool defs_changed = false;
    bool selection_changed = false;
    void track_gradients();
    void untrack_gradients();

    // For each gradient, whether or not it is a swatch. Used to track when isSwatch() changes.
    std::vector<bool> isswatch;
    void rebuild_isswatch();
    bool update_isswatch();

    // A map from colors to their respective widgets. Used to quickly find the widgets corresponding
    // to the current fill/stroke color, in order to update their fill/stroke indicators.
    using ColorKey = std::variant<std::monostate, std::array<unsigned, 3>, SPGradient *>;
    boost::unordered_multimap<ColorKey, ColorItem*> widgetmap; // need boost for array hash
    std::vector<ColorItem*> current_fill;
    std::vector<ColorItem*> current_stroke;
    void update_fillstroke_indicators();

    Inkscape::PrefObserver _pinned_observer;
    Glib::RefPtr<Gtk::Builder> _builder;
    Gtk::ToggleButton &_list_btn;
    Gtk::ToggleButton &_grid_btn;
    PaletteFileData _loaded_palette;

    Gtk::MenuButton &_selector;
    std::unique_ptr<UI::Widget::PopoverMenu> _selector_menu;
    Gtk::Label &_selector_label;

    using PaletteLoaded = std::pair<UI::Widget::palette_t, bool>;
    std::vector<PaletteLoaded> _palettes;

    Glib::ustring _color_filter_text;
    Gtk::Button& _new_btn;
    Gtk::Button& _edit_btn;
    Gtk::Button& _delete_btn;
};

} // namespace Dialog

} // namespace Inkscape::UI

#endif // UI_DIALOG_SWATCHES_H

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
