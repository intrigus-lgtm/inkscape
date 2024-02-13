// SPDX-License-Identifier: GPL-2.0-or-later
/* Authors:
 *   Jon A. Cruz
 *   John Bintz
 *   Abhishek Sharma
 *   PBS <pbs3141@gmail.com>
 *
 * Copyright (C) 2005 Jon A. Cruz
 * Copyright (C) 2008 John Bintz
 * Copyright (C) 2022 PBS
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "swatches.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <utility>
#include <string>
#include <variant>
#include <vector>
#include <giomm/file.h>
#include <giomm/inputstream.h>
#include <glibmm/convert.h>
#include <glibmm/i18n.h>
#include <glibmm/ustring.h>
#include <glibmm/utility.h>
#include <gtkmm/box.h>
#include <gtkmm/builder.h>
#include <gtkmm/button.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/label.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/searchentry2.h>
#include <gtkmm/sizegroup.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/window.h>
#include <pangomm/layout.h>

#include "desktop.h"
#include "desktop-style.h"
#include "document.h"
#include "preferences.h"
#include "style.h"
#include "object/sp-defs.h"
#include "object/sp-gradient.h"
#include "object/sp-gradient-reference.h"
#include "ui/builder-utils.h"
#include "ui/column-menu-builder.h"
#include "ui/controller.h"
#include "ui/dialog/color-item.h"
#include "ui/dialog/global-palettes.h"
#include "ui/util.h" // ellipsize()
#include "ui/widget/color-palette.h"
#include "ui/widget/color-palette-preview.h"
#include "ui/widget/popover-menu-item.h"
#include "util/variant-visitor.h"
#include "widgets/paintdef.h"

namespace Inkscape::UI::Dialog {

static constexpr auto auto_id = "Auto";

/*
 * Lifecycle
 */

SwatchesPanel::SwatchesPanel(bool compact, char const *prefsPath)
    : DialogBase(prefsPath, "Swatches"),
    _builder(create_builder("dialog-swatches.glade")),
    _list_btn(get_widget<Gtk::ToggleButton>(_builder, "list")),
    _grid_btn(get_widget<Gtk::ToggleButton>(_builder, "grid")),
    _selector(get_widget<Gtk::MenuButton>(_builder, "selector")),
    _selector_label(get_widget<Gtk::Label>(_builder, "selector-label")),
    _selector_menu{compact ? nullptr
                   : std::make_unique<UI::Widget::PopoverMenu>(_selector, Gtk::PositionType::BOTTOM)},
    _new_btn(get_widget<Gtk::Button>(_builder, "new")),
    _edit_btn(get_widget<Gtk::Button>(_builder, "edit")),
    _delete_btn(get_widget<Gtk::Button>(_builder, "delete"))
{
    // hide edit buttons - this functionality is not implemented
    _new_btn.set_visible(false);
    _edit_btn.set_visible(false);
    _delete_btn.set_visible(false);

    _palette = Gtk::make_managed<Inkscape::UI::Widget::ColorPalette>();
    _palette->set_visible();
    if (compact) {
        append(*_palette);
    } else {
        get_widget<Gtk::Box>(_builder, "content").append(*_palette);

        _palette->set_settings_visibility(false);

        get_widget<Gtk::MenuButton>(_builder, "settings").set_popover(_palette->get_settings_popover());

        _palette->set_filter([this](Dialog::ColorItem const &color){
            return filter_callback(color);
        });
        auto& search = get_widget<Gtk::SearchEntry2>(_builder, "search");
        search.signal_search_changed().connect([this, &search]{
            if (search.get_text().length() == 0) {
                clear_filter();
            } else {
                filter_colors(search.get_text());
            }
        });
    }

    auto prefs = Inkscape::Preferences::get();
    _current_palette_id = prefs->getString(_prefs_path + "/palette");
    if (auto p = get_palette(_current_palette_id)) {
        _current_palette_id = p->id; // Canonicalise to id, in case name lookup was used.
    } else {
        _current_palette_id = auto_id; // Fall back to auto palette.
    }
    auto path = prefs->getString(_prefs_path + "/palette-path");
    auto loaded = load_swatches(Glib::filename_from_utf8(path));

    update_palettes(compact);

    if (!compact) {
        if (loaded) {
            update_loaded_palette_entry();
        }

        g_assert(_selector_menu);
        setup_selector_menu();
        update_selector_menu();
        update_selector_label(_current_palette_id);
    }

    bool embedded = compact;
    _palette->set_compact(embedded);

    // restore palette settings
    _palette->set_tile_size(prefs->getInt(_prefs_path + "/tile_size", 16));
    _palette->set_aspect(prefs->getDoubleLimited(_prefs_path + "/tile_aspect", 0.0, -2, 2));
    _palette->set_tile_border(prefs->getInt(_prefs_path + "/tile_border", 1));
    _palette->set_rows(prefs->getInt(_prefs_path + "/rows", 1));
    _palette->enable_stretch(prefs->getBool(_prefs_path + "/tile_stretch", false));
    _palette->set_large_pinned_panel(embedded && prefs->getBool(_prefs_path + "/enlarge_pinned", true));
    _palette->enable_labels(!embedded && prefs->getBool(_prefs_path + "/show_labels", true));

    // save settings when they change
    _palette->get_settings_changed_signal().connect([=, this]{
        prefs->setInt(_prefs_path + "/tile_size", _palette->get_tile_size());
        prefs->setDouble(_prefs_path + "/tile_aspect", _palette->get_aspect());
        prefs->setInt(_prefs_path + "/tile_border", _palette->get_tile_border());
        prefs->setInt(_prefs_path + "/rows", _palette->get_rows());
        prefs->setBool(_prefs_path + "/tile_stretch", _palette->is_stretch_enabled());
        prefs->setBool(_prefs_path + "/enlarge_pinned", _palette->is_pinned_panel_large());
        prefs->setBool(_prefs_path + "/show_labels", !embedded && _palette->are_labels_enabled());
    });

    _list_btn.signal_toggled().connect([this]{
        _palette->enable_labels(true);
    });
    _grid_btn.signal_toggled().connect([this]{
        _palette->enable_labels(false);
    });
    (_palette->are_labels_enabled() ? _list_btn : _grid_btn).set_active();

    // Watch for pinned palette options.
    _pinned_observer = prefs->createObserver(_prefs_path + "/pinned/", [this]() {
        rebuild();
    });

    rebuild();

    if (compact) {
        // Respond to requests from the palette widget to change palettes.
        _palette->get_palette_selected_signal().connect([this] (Glib::ustring name) {
            set_palette(name);
        });
    } else {
        append(get_widget<Gtk::Box>(_builder, "main"));

        get_widget<Gtk::Button>(_builder, "open").signal_clicked().connect([this]{
            // load a color palette file selected by the user
            if (load_swatches()) {
                update_loaded_palette_entry();
                update_selector_menu();
                update_selector_label(_loaded_palette.id);
            }
        });;
    }
}

SwatchesPanel::~SwatchesPanel()
{
    untrack_gradients();
}

/*
 * Activation
 */

// Note: The "Auto" palette shows the list of gradients that are swatches. When this palette is
// shown (and we have a document), we therefore need to track both addition/removal of gradients
// and changes to the isSwatch() status to keep the palette up-to-date.

void SwatchesPanel::documentReplaced()
{
    if (getDocument()) {
        if (_current_palette_id == auto_id) {
            track_gradients();
        }
    } else {
        untrack_gradients();
    }

    if (_current_palette_id == auto_id) {
        rebuild();
    }
}

void SwatchesPanel::desktopReplaced()
{
    documentReplaced();
}

void SwatchesPanel::set_palette(const Glib::ustring& id) {
    auto prefs = Preferences::get();
    prefs->setString(_prefs_path + "/palette", id);
    select_palette(id);
}

const PaletteFileData* SwatchesPanel::get_palette(const Glib::ustring& id) {
    if (auto p = GlobalPalettes::get().find_palette(id)) return p;

    if (_loaded_palette.id == id) return &_loaded_palette;

    return nullptr;
}

void SwatchesPanel::select_palette(const Glib::ustring& id) {
    if (_current_palette_id == id) return;

    _current_palette_id = id;

    bool edit = false;
    if (id == auto_id) {
        if (getDocument()) {
            track_gradients();
            edit = false; /*TODO: true; when swatch editing is ready */
        }
    } else {
        untrack_gradients();
    }

    update_selector_label(_current_palette_id);

    _new_btn.set_visible(edit);
    _edit_btn.set_visible(edit);
    _delete_btn.set_visible(edit);

    rebuild();
}

void SwatchesPanel::track_gradients()
{
    auto doc = getDocument();

    // Subscribe to the addition and removal of gradients.
    conn_gradients.disconnect();
    conn_gradients = doc->connectResourcesChanged("gradient", [this] {
        gradients_changed = true;
        queue_resize();
    });

    // Subscribe to child modifications of the defs section. We will use this to monitor
    // each gradient for whether its isSwatch() status changes.
    conn_defs.disconnect();
    conn_defs = doc->getDefs()->connectModified([this] (SPObject*, unsigned flags) {
        if (flags & SP_OBJECT_CHILD_MODIFIED_FLAG) {
            defs_changed = true;
            queue_resize();
        }
    });

    gradients_changed = false;
    defs_changed = false;
    rebuild_isswatch();
}

void SwatchesPanel::untrack_gradients()
{
    conn_gradients.disconnect();
    conn_defs.disconnect();
    gradients_changed = false;
    defs_changed = false;
}

/*
 * Updating
 */

void SwatchesPanel::selectionChanged(Selection*)
{
    selection_changed = true;
    queue_resize();
}

void SwatchesPanel::selectionModified(Selection*, guint flags)
{
    if (flags & SP_OBJECT_STYLE_MODIFIED_FLAG) {
        selection_changed = true;
        queue_resize();
    }
}

// Document updates are handled asynchronously by setting a flag and queuing a resize. This results in
// the following function being run at the last possible moment before the widget will be repainted.
// This ensures that multiple document updates only result in a single UI update.
void SwatchesPanel::size_allocate_vfunc(int const width, int const height, int const baseline)
{
    if (gradients_changed) {
        assert(_current_palette_id == auto_id);
        // We are in the "Auto" palette, and a gradient was added or removed.
        // The list of widgets has therefore changed, and must be completely rebuilt.
        // We must also rebuild the tracking information for each gradient's isSwatch() status.
        rebuild_isswatch();
        rebuild();
    } else if (defs_changed) {
        assert(_current_palette_id == auto_id);
        // We are in the "Auto" palette, and a gradient's isSwatch() status was possibly modified.
        // Check if it has; if so, then the list of widgets has changed, and must be rebuilt.
        if (update_isswatch()) {
            rebuild();
        }
    }

    if (selection_changed) {
        update_fillstroke_indicators();
    }

    selection_changed = false;
    gradients_changed = false;
    defs_changed = false;

    // Necessary to perform *after* the above widget modifications, so GTK can process the new layout.
    DialogBase::size_allocate_vfunc(width, height, baseline);
}

void SwatchesPanel::rebuild_isswatch()
{
    auto grads = getDocument()->getResourceList("gradient");

    isswatch.resize(grads.size());

    for (int i = 0; i < grads.size(); i++) {
        isswatch[i] = static_cast<SPGradient*>(grads[i])->isSwatch();
    }
}

bool SwatchesPanel::update_isswatch()
{
    auto grads = getDocument()->getResourceList("gradient");

    // Should be guaranteed because we catch all size changes and call rebuild_isswatch() instead.
    assert(isswatch.size() == grads.size());

    bool modified = false;

    for (int i = 0; i < grads.size(); i++) {
        if (isswatch[i] != static_cast<SPGradient*>(grads[i])->isSwatch()) {
            isswatch[i].flip();
            modified = true;
        }
    }

    return modified;
}

static auto spcolor_to_rgb(SPColor const &color)
{
    float rgbf[3];
    color.get_rgb_floatv(rgbf);

    std::array<unsigned, 3> rgb;
    for (int i = 0; i < 3; i++) {
        rgb[i] = SP_COLOR_F_TO_U(rgbf[i]);
    };

    return rgb;
}

void SwatchesPanel::update_fillstroke_indicators()
{
    auto doc = getDocument();
    auto style = SPStyle(doc);

    // Get the current fill or stroke as a ColorKey.
    auto current_color = [&, this] (bool fill) -> std::optional<ColorKey> {
        switch (sp_desktop_query_style(getDesktop(), &style, fill ? QUERY_STYLE_PROPERTY_FILL : QUERY_STYLE_PROPERTY_STROKE))
        {
            case QUERY_STYLE_SINGLE:
            case QUERY_STYLE_MULTIPLE_AVERAGED:
            case QUERY_STYLE_MULTIPLE_SAME:
                break;
            default:
                return {};
        }

        auto attr = style.getFillOrStroke(fill);
        if (!attr->set) {
            return {};
        }

        if (attr->isNone()) {
            return std::monostate{};
        } else if (attr->isColor()) {
            return spcolor_to_rgb(attr->value.color);
        } else if (attr->isPaintserver()) {
            if (auto grad = cast<SPGradient>(fill ? style.getFillPaintServer() : style.getStrokePaintServer())) {
                if (grad->isSwatch()) {
                    return grad;
                } else if (grad->ref) {
                    if (auto ref = grad->ref->getObject(); ref && ref->isSwatch()) {
                        return ref;
                    }
                }
            }
        }

        return {};
    };

    for (auto w : current_fill) w->set_fill(false);
    for (auto w : current_stroke) w->set_stroke(false);

    current_fill.clear();
    current_stroke.clear();

    if (auto fill = current_color(true)) {
        auto range = widgetmap.equal_range(*fill);
        for (auto it = range.first; it != range.second; ++it) {
            current_fill.emplace_back(it->second);
        }
    }

    if (auto stroke = current_color(false)) {
        auto range = widgetmap.equal_range(*stroke);
        for (auto it = range.first; it != range.second; ++it) {
            current_stroke.emplace_back(it->second);
        }
    }

    for (auto w : current_fill) w->set_fill(true);
    for (auto w : current_stroke) w->set_stroke(true);
}

[[nodiscard]] static auto to_palette_t(PaletteFileData const &p)
{
    UI::Widget::palette_t palette;
    palette.name = p.name;
    palette.id = p.id;
    for (auto const &c : p.colors) {
        std::visit(VariantVisitor {
            [&](const PaletteFileData::Color& c) {
                auto [r, g, b] = c.rgb;
                palette.colors.push_back({r / 255.0, g / 255.0, b / 255.0});
            },
            [](...) {}
        });
    }
    return palette;
}

/**
 * Process the list of available palettes and update the list in the _palette widget.
 */
void SwatchesPanel::update_palettes(bool compact) {
    std::vector<UI::Widget::palette_t> palettes;
    palettes.reserve(1 + GlobalPalettes::get().palettes().size());

    // The first palette in the list is always the "Auto" palette. Although this
    // will contain colors when selected, the preview we show for it is empty.
    palettes.push_back({_("Document swatches"), auto_id, {}});

    // The remaining palettes in the list are the global palettes.
    for (auto &p : GlobalPalettes::get().palettes()) {
        auto palette = to_palette_t(p);
        palettes.emplace_back(std::move(palette));
    }

    _palette->set_palettes(palettes);

    _palettes.clear();
    _palettes.reserve(palettes.size());
    std::transform(palettes.begin(), palettes.end(), std::back_inserter(_palettes),
                   [](auto &&palette){ return PaletteLoaded{std::move(palette), false}; });
}

/**
 * Rebuild the list of color items shown by the palette.
 */
void SwatchesPanel::rebuild()
{
    std::vector<ColorItem*> palette;

    // The pointers in widgetmap are to widgets owned by the ColorPalette. It is assumed it will not
    // delete them unless we ask, via the call to set_colors() later in this function.
    widgetmap.clear();
    current_fill.clear();
    current_stroke.clear();

    // Add the "remove-color" color.
    auto const w = Gtk::make_managed<ColorItem>(PaintDef(), this);
    w->set_pinned_pref(_prefs_path);
    palette.emplace_back(w);
    widgetmap.emplace(std::monostate{}, w);
    _palette->set_page_size(0);
    if (auto pal = get_palette(_current_palette_id)) {
        _palette->set_page_size(pal->columns);
        palette.reserve(palette.size() + pal->colors.size());
        for (auto &c : pal->colors) {
            auto dialog = this;
            auto w = std::visit(VariantVisitor {
                [](const PaletteFileData::SpacerItem&) {
                    return Gtk::make_managed<ColorItem>("");
                },
                [](const PaletteFileData::GroupStart& g) {
                    return Gtk::make_managed<ColorItem>(g.name);
                },
                [=, this](const PaletteFileData::Color& c) {
                    auto w = Gtk::make_managed<ColorItem>(PaintDef(c.rgb, c.name, c.definition), dialog);
                    w->set_pinned_pref(_prefs_path);
                    widgetmap.emplace(c.rgb, w);
                    return w;
                },
            }, c);

            palette.emplace_back(w);
        }
    } else if (_current_palette_id == auto_id && getDocument()) {
        auto grads = getDocument()->getResourceList("gradient");
        for (auto obj : grads) {
            auto grad = cast_unsafe<SPGradient>(obj);
            if (grad->isSwatch()) {
                auto const w = Gtk::make_managed<ColorItem>(grad, this);
                palette.emplace_back(w);
                widgetmap.emplace(grad, w);
                // Rebuild if the gradient gets pinned or unpinned
                w->signal_pinned().connect([this]{
                    rebuild();
                });
            }
        }
    }

    if (getDocument()) {
        update_fillstroke_indicators();
    }

    _palette->set_colors(palette);
    _palette->set_selected(_current_palette_id);
}

bool SwatchesPanel::load_swatches() {
    auto window = dynamic_cast<Gtk::Window *>(get_root());
    auto file = choose_palette_file(window);
    auto loaded = false;
    if (load_swatches(file)) {
        auto prefs = Preferences::get();
        prefs->setString(_prefs_path + "/palette", _loaded_palette.id);
        prefs->setString(_prefs_path + "/palette-path", file);
        select_palette(_loaded_palette.id);
        loaded = true;
    }
    return loaded;
}

bool SwatchesPanel::load_swatches(std::string const &path)
{
    if (path.empty()) return false;

    // load colors
    auto res = load_palette(path);
    if (res.palette) {
        // use loaded palette
        _loaded_palette = std::move(*res.palette);
        return true;
    } else if (auto desktop = getDesktop()) {
        desktop->showNotice(res.error_message);
    }

    return false;
}

void SwatchesPanel::update_loaded_palette_entry() {
    // add or update last entry in a store to match loaded palette
    if (_palettes.empty() || !_palettes.back().second) { // last palette !loaded
        _palettes.emplace_back();
    }
    auto &[palette, loaded] = _palettes.back();
    palette = to_palette_t(_loaded_palette);
    loaded = true;
}

void SwatchesPanel::setup_selector_menu()
{
    _selector.set_popover(*_selector_menu);
    Controller::add_key<&SwatchesPanel::on_selector_key_pressed>(_selector, *this);
}

bool SwatchesPanel::on_selector_key_pressed(GtkEventControllerKey const * controller,
                                            unsigned const keyval, unsigned /*keycode*/,
                                            GdkModifierType const state)
{
    // We act like GtkComboBox in that we only move the active item if no modifier key was pressed:
    if (Controller::has_flag(state, gtk_accelerator_get_default_mod_mask())) return false;

    auto const begin = _palettes.cbegin(), end = _palettes.cend();
    auto it = std::find_if(begin, end, [&](auto &p){ return p.first.id == _current_palette_id; });
    if (it == end) return false;

    int const old_index = std::distance(begin, it), back = _palettes.size() - 1;
    int       new_index = old_index;

    switch (keyval) {
        case GDK_KEY_Up  : --new_index       ; break;
        case GDK_KEY_Down: ++new_index       ; break;
        case GDK_KEY_Home:   new_index = 0   ; break;
        case GDK_KEY_End :   new_index = back; break;
        default: return false;
    }

    new_index = std::clamp(new_index, 0, back);
    if (new_index != old_index) {
        it = begin + new_index;
        set_palette(it->first.id);
    }
    return true;
}

[[nodiscard]] static auto make_selector_item(UI::Widget::palette_t const &palette)
{
    static constexpr int max_chars = 35; // Make PopoverMenuItems ellipsize long labels, in middle.

    auto const label = Gtk::make_managed<Gtk::Label>(palette.name, true);
    label->set_xalign(0.0);
    UI::ellipsize(*label, max_chars, Pango::EllipsizeMode::MIDDLE);

    auto const box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 1);
    box->append(*label);
    box->append(*Gtk::make_managed<UI::Widget::ColorPalettePreview>(palette.colors));

    auto const item = Gtk::make_managed<UI::Widget::PopoverMenuItem>();
    item->set_child(*box);

    return std::pair{item, label};
}

void SwatchesPanel::update_selector_menu()
{
    g_assert(_selector_menu);

    _selector.set_sensitive(false);
    _selector_label.set_label({});
    _selector_menu->delete_all();

    if (_palettes.empty()) return;

    // TODO: GTK4: probably nicer to use GtkGridView.
    Inkscape::UI::ColumnMenuBuilder builder{*_selector_menu, 2};
    // Items are put in a SizeGroup to keep the two columnsʼ widths homogeneous
    auto const size_group = Gtk::SizeGroup::create(Gtk::SizeGroup::Mode::HORIZONTAL);
    auto const add_item = [&](UI::Widget::palette_t const &palette){
        auto const [item, label] = make_selector_item(palette);
        item->signal_activate().connect([id = palette.id, this]{ set_palette(id); });
        size_group->add_widget(*label);
        builder.add_item(*item);
    };
    // Acrobatics are done to sort down columns, not over rows
    auto const size = _palettes.size(), half = (size + 1) / 2;
    for (std::size_t left = 0; left < half; ++left) {
        add_item(_palettes.at(left).first);
        if (auto const right = left + half; right < size) {
            add_item(_palettes.at(right).first);
        }
    }

    _selector.set_sensitive(true);
    size_group->add_widget(_selector_label);
}

void SwatchesPanel::update_selector_label(Glib::ustring const &active_id)
{
    // Set the new paletteʼs name as label of the selector menubutton.
    auto const it = std::find_if(_palettes.cbegin(), _palettes.cend(),
                                 [&](auto const &pair){ return pair.first.id == active_id; });
    g_assert(it != _palettes.cend());
    _selector_label.set_label(it->first.name);
}

void SwatchesPanel::clear_filter() {
    if (_color_filter_text.empty()) return;

    _color_filter_text.erase();
    _palette->apply_filter();
}

void SwatchesPanel::filter_colors(const Glib::ustring& text) {
    auto search = text.lowercase();
    if (_color_filter_text == search) return;

    _color_filter_text = search;
    _palette->apply_filter();
}

bool SwatchesPanel::filter_callback(const Dialog::ColorItem& color) const {
    if (_color_filter_text.empty()) return true;

    // let's hide group headers and fillers when searching for a matching color
    if (color.is_filler() || color.is_group()) return false;

    auto text = color.get_description().lowercase();
    return text.find(_color_filter_text) != Glib::ustring::npos;
}

} // namespace Inkscape::UI::Dialog

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
