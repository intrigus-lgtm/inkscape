// SPDX-License-Identifier: GPL-2.0-or-later
/** * @file
 * Text aux toolbar
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
 *   Tavmjong Bah <tavmjong@free.fr>
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Copyright (C) 2004 David Turner
 * Copyright (C) 2003 MenTaLguY
 * Copyright (C) 2001-2002 Ximian, Inc.
 * Copyright (C) 1999-2013 authors
 * Copyright (C) 2017 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "text-toolbar.h"

#include <boost/range/adaptor/reversed.hpp>
#include <glibmm/i18n.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/frame.h>
#include <gtkmm/grid.h>
#include <gtkmm/listbox.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/separator.h>

#include "desktop-style.h"
#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "inkscape.h"
#include "selection.h"
#include "selection-chemistry.h"
#include "libnrtype/font-lister.h"
#include "object/sp-flowdiv.h"
#include "object/sp-flowtext.h"
#include "object/sp-root.h"
#include "object/sp-string.h"
#include "object/sp-text.h"
#include "object/sp-tspan.h"
#include "svg/css-ostringstream.h"
#include "ui/builder-utils.h"
#include "ui/dialog/dialog-container.h"
#include "ui/icon-names.h"
#include "ui/tools/select-tool.h"
#include "ui/tools/text-tool.h"
#include "ui/util.h"
#include "ui/widget/canvas.h" // Focus
#include "ui/widget/combo-box-entry-tool-item.h"
#include "ui/widget/combo-tool-item.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/toolbar-menu-button.h"
#include "ui/widget/unit-tracker.h"
#include "util/font-collections.h"
#include "util-string/ustring-format.h"
#include "widgets/style-utils.h"

using Inkscape::DocumentUndo;
using Inkscape::Util::Unit;
using Inkscape::Util::Quantity;
using Inkscape::UI::Widget::UnitTracker;

//#define DEBUG_TEXT

//########################
//##    Text Toolbox    ##
//########################

// Functions for debugging:
#ifdef DEBUG_TEXT
static void sp_print_font(SPStyle *query)
{
    bool family_set   = query->font_family.set;
    bool style_set    = query->font_style.set;
    bool fontspec_set = query->font_specification.set;

    std::cout << "    Family set? " << family_set
              << "    Style set? "  << style_set
              << "    FontSpec set? " << fontspec_set
              << std::endl;
}

static void       sp_print_fontweight( SPStyle *query ) {
    const gchar* names[] = {"100", "200", "300", "400", "500", "600", "700", "800", "900",
                            "NORMAL", "BOLD", "LIGHTER", "BOLDER", "Out of range"};
    // Missing book = 380
    int index = query->font_weight.computed;
    if (index < 0 || index > 13)
        index = 13;
    std::cout << "    Weight: " << names[ index ]
              << " (" << query->font_weight.computed << ")" << std::endl;
}

static void       sp_print_fontstyle( SPStyle *query ) {

    const gchar* names[] = {"NORMAL", "ITALIC", "OBLIQUE", "Out of range"};
    int index = query->font_style.computed;
    if( index < 0 || index > 3 ) index = 3;
    std::cout << "    Style:  " << names[ index ] << std::endl;

}
#endif

static bool is_relative( Unit const *unit ) {
    return (unit->abbr == "" || unit->abbr == "em" || unit->abbr == "ex" || unit->abbr == "%");
}

static bool is_relative(SPCSSUnit const unit)
{
    return (unit == SP_CSS_UNIT_NONE || unit == SP_CSS_UNIT_EM || unit == SP_CSS_UNIT_EX ||
            unit == SP_CSS_UNIT_PERCENT);
}

// Set property for object, but unset all descendents
// Should probably be moved to desktop_style.cpp
static void recursively_set_properties(SPObject *object, SPCSSAttr *css, bool unset_descendents = true)
{
    object->changeCSS (css, "style");

    SPCSSAttr *css_unset = sp_repr_css_attr_unset_all( css );
    std::vector<SPObject *> children = object->childList(false);
    for (auto i: children) {
        recursively_set_properties(i, unset_descendents ? css_unset : css);
    }
    sp_repr_css_attr_unref (css_unset);
}

static Glib::RefPtr<Gtk::ListStore> create_sizes_store_uncached(int unit)
{
    // List of font sizes for dropdown menu
    constexpr int sizes[] = {
        4, 6, 8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 22, 24, 28,
        32, 36, 40, 48, 56, 64, 72, 144
    };

    // Array must be same length as SPCSSUnit in style.h
    constexpr float ratios[] = {1, 1, 1, 10, 4, 40, 100, 16, 8, 0.16};

    struct Columns : Gtk::TreeModelColumnRecord
    {
        Gtk::TreeModelColumn<Glib::ustring> str;
        Columns() { add(str); }
    };
    static Columns const columns;

    auto store = Gtk::ListStore::create(columns);

    for (int i : sizes) {
        store->append()->set_value(columns.str, Inkscape::ustring::format_classic(i / ratios[unit]));
    }

    return store;
}

/**
 * Create a ListStore containing the default list of font sizes scaled for the given unit.
 */
static Glib::RefPtr<Gtk::ListStore> create_sizes_store(int unit)
{
    static std::unordered_map<int, Glib::RefPtr<Gtk::ListStore>> cache;

    auto &result = cache[unit];

    if (!result) {
        result = create_sizes_store_uncached(unit);
    }

    return result;
}

// TODO: possibly share with font-selector by moving most code to font-lister (passing family name)
static void sp_text_toolbox_select_cb(Gtk::Entry const &entry)
{
    auto const family = entry.get_buffer()->get_text();
    // std::cout << "text_toolbox_missing_font_cb: selecting: " << family << std::endl;

    // Get all items with matching font-family set (not inherited!).
    std::vector<SPItem *> selectList;

    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    SPDocument *document = desktop->getDocument();
    auto allList = get_all_items(document->getRoot(), desktop, false, false, true);
    for (auto item : boost::adaptors::reverse(allList)) {
        auto style = item->style;
        if (!style) {
            continue;
        }

        Glib::ustring family_style;
        if (style->font_family.set) {
            family_style = style->font_family.value();
            // std::cout << " family style from font_family: " << family_style << std::endl;
        } else if (style->font_specification.set) {
            family_style = style->font_specification.value();
            // std::cout << " family style from font_spec: " << family_style << std::endl;
        }

        if (family_style.compare(family) == 0) {
            // std::cout << "   found: " << item->getId() << std::endl;
            selectList.push_back(item);
        }
    }

    // Update selection
    auto selection = desktop->getSelection();
    selection->clear();
    // std::cout << "   list length: " << selectList.size() << std::endl;
    selection->setList(selectList);
}

namespace Inkscape::UI::Toolbar {

TextToolbar::TextToolbar(SPDesktop *desktop)
    : Toolbar(desktop)
    , _freeze(false)
    , _text_style_from_prefs(false)
    , _outer(true)
    , _updating(false)
    , _tracker(new UnitTracker(Inkscape::Util::UNIT_TYPE_LINEAR))
    , _tracker_fs(new UnitTracker(Inkscape::Util::UNIT_TYPE_LINEAR))
    , _cusor_numbers(0)
    , _builder(create_builder("toolbar-text.ui"))
    , _font_collections_list(get_widget<Gtk::ListBox>(_builder, "_font_collections_list"))
    , _line_height_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_line_height_item"))
    , _superscript_btn(get_widget<Gtk::ToggleButton>(_builder, "_superscript_btn"))
    , _subscript_btn(get_widget<Gtk::ToggleButton>(_builder, "_subscript_btn"))
    , _word_spacing_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_word_spacing_item"))
    , _letter_spacing_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_letter_spacing_item"))
    , _dx_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_dx_item"))
    , _dy_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_dy_item"))
    , _rotation_item(get_derived_widget<UI::Widget::SpinButton>(_builder, "_rotation_item"))
{
    _toolbar = &get_widget<Gtk::Box>(_builder, "text-toolbar");

    auto prefs = Preferences::get();

    // Line height unit tracker.
    auto const &unit_table = Util::UnitTable::get();
    _tracker->prependUnit(unit_table.getUnit("")); // Ratio
    _tracker->addUnit(unit_table.getUnit("%"));
    _tracker->addUnit(unit_table.getUnit("em"));
    _tracker->addUnit(unit_table.getUnit("ex"));
    _tracker->setActiveUnit(unit_table.getUnit(""));

    // We change only the display value
    _tracker->changeLabel("lines", 0, true);
    _tracker_fs->setActiveUnit(unit_table.getUnit("mm"));

    // Setup the spin buttons.
    // TODO: Take care of the line-height pref settings.
    setup_derived_spin_button(_line_height_item, "line-height", 1.25, &TextToolbar::lineheight_value_changed);
    setup_derived_spin_button(_letter_spacing_item, "letterspacing", 0.0, &TextToolbar::letterspacing_value_changed);
    setup_derived_spin_button(_word_spacing_item, "wordspacing", 0.0, &TextToolbar::wordspacing_value_changed);
    setup_derived_spin_button(_dx_item, "dx", 0.0, &TextToolbar::dx_value_changed);
    setup_derived_spin_button(_dy_item, "dy", 0.0, &TextToolbar::dy_value_changed);
    setup_derived_spin_button(_rotation_item, "rotation", 0.0, &TextToolbar::rotation_value_changed);

    // Configure alignment mode buttons
    configure_mode_buttons(_alignment_buttons, get_widget<Gtk::Box>(_builder, "alignment_buttons_box"), "align_mode",
                           &TextToolbar::align_mode_changed);
    configure_mode_buttons(_writing_buttons, get_widget<Gtk::Box>(_builder, "writing_buttons_box"), "writing_mode",
                           &TextToolbar::writing_mode_changed);
    configure_mode_buttons(_orientation_buttons, get_widget<Gtk::Box>(_builder, "orientation_buttons_box"),
                           "orientation_mode", &TextToolbar::orientation_changed);
    configure_mode_buttons(_direction_buttons, get_widget<Gtk::Box>(_builder, "direction_buttons_box"),
                           "direction_mode", &TextToolbar::direction_changed);

    // Font family
    {
        // Font list
        auto fontlister = Inkscape::FontLister::get_instance();
        fontlister->update_font_list(desktop->getDocument());
        auto store = fontlister->get_font_list();

        // Keep font list up to date with document fonts when refreshed.
        _fonts_updated_signal = fontlister->connectNewFonts([=](){
            fontlister->update_font_list(desktop->getDocument());
        });

        _font_family_item = Gtk::make_managed<UI::Widget::ComboBoxEntryToolItem>(
            "TextFontFamilyAction",
            _("Font Family"),
            _("Select Font Family (Alt-X to access)"),
            store,
            -1, // Entry width
            50, // Extra list width
            &font_lister_cell_data_func2, // Cell layout
            &font_lister_separator_func,
            desktop->getCanvas() // Focus widget
        );

        _font_family_item->popup_enable(); // Enable entry completion

        _font_family_item->set_info(_("Select all text with this font-family")); // Show selection icon
        _font_family_item->set_info_cb(&sp_text_toolbox_select_cb);

        _font_family_item->set_warning(_("Font not found on system")); // Show icon w/ tooltip if font missing
        _font_family_item->set_warning_cb(&sp_text_toolbox_select_cb);

        _font_family_item->connectChanged([this](){ fontfamily_value_changed(); });
        get_widget<Gtk::Box>(_builder, "font_list_box").append(*_font_family_item);

        _font_family_item->focus_on_click(false);
    }

    // Font styles
    {
        auto fontlister = Inkscape::FontLister::get_instance();
        auto store = fontlister->get_style_list();

        _font_style_item = Gtk::manage(new UI::Widget::ComboBoxEntryToolItem(
            "TextFontStyleAction",
            _("Font Style"),
            _("Font style"),
            store,
            12, // Width in characters
            0, // Extra list width
            {}, // Cell layout
            {}, // Separator
            desktop->getCanvas()
        )); // Focus widget

        _font_style_item->connectChanged([this] { fontstyle_value_changed(); });
        _font_style_item->focus_on_click(false);
        get_widget<Gtk::Box>(_builder, "styles_list_box").append(*_font_style_item);
    }

    // Font size
    {
        // List of font sizes for drop-down menu
        int unit = prefs->getInt("/options/font/unitType", SP_CSS_UNIT_PT);

        auto unit_str = sp_style_get_css_unit_string(unit);
        Glib::ustring tooltip = Glib::ustring::format(_("Font size"), " (", unit_str, ")");

        _font_size_item = Gtk::manage(new UI::Widget::ComboBoxEntryToolItem(
            "TextFontSizeAction",
            _("Font Size"),
            tooltip,
            create_sizes_store(unit),
            8, // Width in characters
            0, // Extra list width
            {}, // Cell layout
            {}, // Separator
            desktop->getCanvas() // Focus widget
        ));

        _font_size_item->connectChanged([this] { fontsize_value_changed(); });
        _font_size_item->focus_on_click(false);
        get_widget<Gtk::Box>(_builder, "font_size_box").append(*_font_size_item);
    }

    // Font_ size units
    {
        _font_size_units_item = _tracker_fs->create_tool_item(_("Units"), (""));
        _font_size_units_item->signal_changed_after().connect(
            sigc::mem_fun(*this, &TextToolbar::fontsize_unit_changed));
        _font_size_units_item->focus_on_click(false);
        get_widget<Gtk::Box>(_builder, "unit_menu_box").append(*_font_size_units_item);
    }

    // Line height units
    {
        _line_height_units_item = _tracker->create_tool_item( _("Units"), (""));
        _line_height_units_item->signal_changed_after().connect(sigc::mem_fun(*this, &TextToolbar::lineheight_unit_changed));
        _line_height_units_item->focus_on_click(false);
        get_widget<Gtk::Box>(_builder, "line_height_unit_box").append(*_line_height_units_item);
    }

    // Superscript button.
    _superscript_btn.signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &TextToolbar::script_changed), 0));
    _superscript_btn.set_active(prefs->getBool("/tools/text/super", false));

    // Subscript button.
    _subscript_btn.signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &TextToolbar::script_changed), 1));

    _subscript_btn.set_active(prefs->getBool("/tools/text/sub", false));

    // Fetch all the ToolbarMenuButtons at once from the UI file
    // Menu Button #1
    auto popover_box1 = &get_widget<Gtk::Box>(_builder, "popover_box1");
    auto menu_btn1 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn1");

    // Menu Button #2
    auto popover_box2 = &get_widget<Gtk::Box>(_builder, "popover_box2");
    auto menu_btn2 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn2");

    // Menu Button #3
    auto popover_box3 = &get_widget<Gtk::Box>(_builder, "popover_box3");
    auto menu_btn3 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn3");

    // Menu Button #4
    auto popover_box4 = &get_widget<Gtk::Box>(_builder, "popover_box4");
    auto menu_btn4 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn4");

    // Menu Button #5
    auto popover_box5 = &get_widget<Gtk::Box>(_builder, "popover_box5");
    auto menu_btn5 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn5");

    // Menu Button #4
    auto popover_box6 = &get_widget<Gtk::Box>(_builder, "popover_box6");
    auto menu_btn6 = &get_derived_widget<UI::Widget::ToolbarMenuButton>(_builder, "menu_btn6");

    // Initialize all the ToolbarMenuButtons only after all the children of the
    // toolbar have been fetched. Otherwise, the children to be moved in the
    // popover will get mapped to a different position and it will probably
    // cause segfault.
    auto children = UI::get_children(*_toolbar);

    menu_btn1->init(1, "tag1", popover_box1, children);
    addCollapsibleButton(menu_btn1);

    menu_btn2->init(2, "tag2", popover_box2, children);
    addCollapsibleButton(menu_btn2);

    menu_btn3->init(3, "tag3", popover_box3, children);
    addCollapsibleButton(menu_btn3);

    menu_btn4->init(4, "tag4", popover_box4, children);
    addCollapsibleButton(menu_btn4);

    menu_btn5->init(5, "tag5", popover_box5, children);
    addCollapsibleButton(menu_btn5);

    menu_btn6->init(6, "tag6", popover_box6, children);
    addCollapsibleButton(menu_btn6);

    set_child(*_toolbar);

    // Font collections signals.
    auto *font_collections = Inkscape::FontCollections::get();

    get_widget<Gtk::Popover>(_builder, "font_collections_popover")
        .signal_show()
        .connect([this]() { display_font_collections(); }, false);

    // This signal will keep both the Text and Font dialog and
    // TextToolbar popovers in sync with each other.
    fc_changed_selection = font_collections->connect_selection_update([this]() { display_font_collections(); });

    // This one will keep the text toolbar Font Collections
    // updated in case of any change in the Font Collections.
    fc_update = font_collections->connect_update([this]() { display_font_collections(); });

    get_widget<Gtk::Button>(_builder, "fc_dialog_btn").signal_clicked().connect([this]() {
        TextToolbar::on_fcm_button_pressed();
    });

    get_widget<Gtk::Button>(_builder, "reset_btn").signal_clicked().connect([this]() {
        TextToolbar::on_reset_button_pressed();
    });

    // We emit a selection change on tool switch to text.
    desktop->connectEventContextChanged(sigc::mem_fun(*this, &TextToolbar::watch_ec));
}

TextToolbar::~TextToolbar() = default;

void TextToolbar::setup_derived_spin_button(UI::Widget::SpinButton &btn, Glib::ustring const &name,
                                            double default_value, ValueChangedMemFun const value_changed_mem_fun)
{
    const Glib::ustring path = "/tools/text/" + name;
    auto const val = Preferences::get()->getDouble(path, default_value);
    auto adj = btn.get_adjustment();
    adj->set_value(val);
    adj->signal_value_changed().connect(sigc::mem_fun(*this, value_changed_mem_fun));

    /*
    if (name == "line-height") {
        //_tracker->addAdjustment(_line_height_adj->gobj()); // (Alex V) Why is this commented out?
    }
    */

    btn.set_defocus_widget(_desktop->getCanvas());
}

void TextToolbar::configure_mode_buttons(std::vector<Gtk::ToggleButton *> &buttons, Gtk::Box &box,
                                         Glib::ustring const &name, ModeChangedMemFun const mode_changed_mem_fun)
{
    int btn_index = 0;

    for_each_child(box, [this, mode_changed_mem_fun, &btn_index, &buttons](Gtk::Widget &item) {
        auto &btn = dynamic_cast<Gtk::ToggleButton &>(item);
        buttons.push_back(&btn);
        btn.signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, mode_changed_mem_fun), btn_index++));

        return ForEachResult::_continue;
    });

    // Set the active button after all the buttons have been pushed.
    const Glib::ustring path = "/tools/text/" + name;
    const int active_button_index = Preferences::get()->getInt(path, 0);
    buttons[active_button_index < buttons.size() ? active_button_index : 0]->set_active(true);
}

/*
 * Set the style, depending on the inner or outer text being selected
 */
void TextToolbar::text_outer_set_style(SPCSSAttr *css)
{
    // Calling sp_desktop_set_style will result in a call to TextTool::_styleSet() which
    // will set the style on selected text inside the <text> element. If we want to set
    // the style on the outer <text> objects we need to bypass this call.
    SPDesktop *desktop = _desktop;
    if(_outer) {
        // Apply css to parent text objects directly.
        for (auto item : desktop->getSelection()->items()) {
            if (is<SPText>(item) || is<SPFlowtext>(item)) {
                // Scale by inverse of accumulated parent transform
                SPCSSAttr *css_set = sp_repr_css_attr_new();
                sp_repr_css_merge(css_set, css);
                Geom::Affine const local(item->i2doc_affine());
                double const ex(local.descrim());
                if ((ex != 0.0) && (ex != 1.0)) {
                    sp_css_attr_scale(css_set, 1 / ex);
                }
                recursively_set_properties(item, css_set);
                sp_repr_css_attr_unref(css_set);
            }
        }
    } else {
        // Apply css to selected inner objects.
        sp_desktop_set_style (desktop, css, true, false);
    }
}

void
TextToolbar::fontfamily_value_changed()
{
#ifdef DEBUG_TEXT
    std::cout << std::endl;
    std::cout << "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM" << std::endl;
    std::cout << "sp_text_fontfamily_value_changed: " << std::endl;
#endif

     // quit if run by the _changed callbacks
    if (_freeze) {
#ifdef DEBUG_TEXT
        std::cout << "sp_text_fontfamily_value_changed: frozen... return" << std::endl;
        std::cout << "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM\n" << std::endl;
#endif
        return;
    }
    _freeze = true;

    Glib::ustring new_family = _font_family_item->get_active_text();
    css_font_family_unquote( new_family ); // Remove quotes around font family names.

    // TODO: Think about how to handle handle multiple selections. While
    // the font-family may be the same for all, the styles might be different.
    // See: TextEdit::onApply() for example of looping over selected items.
    Inkscape::FontLister *fontlister = Inkscape::FontLister::get_instance();
#ifdef DEBUG_TEXT
    std::cout << "  Old family: " << fontlister->get_font_family() << std::endl;
    std::cout << "  New family: " << new_family << std::endl;
    std::cout << "  Old active: " << fontlister->get_font_family_row() << std::endl;
    // std::cout << "  New active: " << act->active << std::endl;
#endif
    if( new_family.compare( fontlister->get_font_family() ) != 0 ) {
        // Changed font-family

        if( _font_family_item->get_active() == -1 ) {
            // New font-family, not in document, not on system (could be fallback list)
            fontlister->insert_font_family( new_family );

            // This just sets a variable in the ComboBoxEntryAction object...
            // shouldn't we also set the actual active row in the combobox?
            _font_family_item->set_active(0); // New family is always at top of list.
        }

        fontlister->set_font_family( _font_family_item->get_active() );
        // active text set in sp_text_toolbox_selection_changed()

        SPCSSAttr *css = sp_repr_css_attr_new ();
        fontlister->fill_css( css );

        if (mergeDefaultStyle(css)) {
            // If there is a selection, update
            DocumentUndo::done(_desktop->getDocument(), _("Text: Change font family"), INKSCAPE_ICON("draw-text"));
        }
        sp_repr_css_attr_unref (css);
    }

    // unfreeze
    _freeze = false;

    SPDocument *document = _desktop->getDocument();
    fontlister->add_document_fonts_at_top(document);

#ifdef DEBUG_TEXT
    std::cout << "sp_text_toolbox_fontfamily_changes: exit"  << std::endl;
    std::cout << "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM" << std::endl;
    std::cout << std::endl;
#endif
}

void
TextToolbar::fontsize_value_changed()
{
    // quit if run by the _changed callbacks
    if (_freeze) {
        return;
    }
    _freeze = true;

    auto active_text = _font_size_item->get_active_text();
    char const *text = active_text.c_str();
    gchar *endptr;
    gdouble size = g_strtod( text, &endptr );
    if (endptr == text) {  // Conversion failed, non-numeric input.
        g_warning( "Conversion of size text to double failed, input: %s\n", text );
        _freeze = false;
        return;
    }

    auto prefs = Inkscape::Preferences::get();
    int max_size = prefs->getInt("/dialogs/textandfont/maxFontSize", 10000); // somewhat arbitrary, but text&font preview freezes with too huge fontsizes

    if (size > max_size)
        size = max_size;

    // Set css font size.
    SPCSSAttr *css = sp_repr_css_attr_new ();
    Inkscape::CSSOStringStream osfs;
    int unit = prefs->getInt("/options/font/unitType", SP_CSS_UNIT_PT);
    if (prefs->getBool("/options/font/textOutputPx", true)) {
        osfs << sp_style_css_size_units_to_px(size, unit) << sp_style_get_css_unit_string(SP_CSS_UNIT_PX);
    } else {
        osfs << size << sp_style_get_css_unit_string(unit);
    }
    sp_repr_css_set_property (css, "font-size", osfs.str().c_str());
    double factor = size / selection_fontsize;

    // Apply font size to selected objects.
    text_outer_set_style(css);

    Unit const *unit_lh = _tracker->getActiveUnit();
    g_return_if_fail(unit_lh != nullptr);
    if (!is_relative(unit_lh) && _outer) {
        double lineheight = _line_height_item.get_adjustment()->get_value();
        _freeze = false;
        _line_height_item.get_adjustment()->set_value(lineheight * factor);
        _freeze = true;
    }

    if (mergeDefaultStyle(css)) {
        DocumentUndo::maybeDone(_desktop->getDocument(), "ttb:size", _("Text: Change font size"), INKSCAPE_ICON("draw-text"));
    }

    sp_repr_css_attr_unref(css);

    _freeze = false;
}

void TextToolbar::fontstyle_value_changed()
{
    // quit if run by the _changed callbacks
    if (_freeze) {
        return;
    }
    _freeze = true;

    Glib::ustring new_style = _font_style_item->get_active_text();

    Inkscape::FontLister* fontlister = Inkscape::FontLister::get_instance();

    if( new_style.compare( fontlister->get_font_style() ) != 0 ) {

        fontlister->set_font_style( new_style );
        // active text set in sp_text_toolbox_seletion_changed()

        SPCSSAttr *css = sp_repr_css_attr_new ();
        fontlister->fill_css( css );

        SPDesktop   *desktop    = _desktop;
        sp_desktop_set_style (desktop, css, true, true);

        if (mergeDefaultStyle(css)) {
            DocumentUndo::done(desktop->getDocument(), _("Text: Change font style"), INKSCAPE_ICON("draw-text"));
        }

        sp_repr_css_attr_unref (css);

    }

    _freeze = false;
}

// Handles both Superscripts and Subscripts
void TextToolbar::script_changed(int mode)
{
    // quit if run by the _changed callbacks
    if (_freeze) {
        return;
    }

    _freeze = true;

    // Called by Superscript or Subscript button?

#ifdef DEBUG_TEXT
    std::cout << "TextToolbar::script_changed: " << mode << std::endl;
#endif

    // Query baseline
    SPStyle query(_desktop->getDocument());
    int result_baseline = sp_desktop_query_style (_desktop, &query, QUERY_STYLE_PROPERTY_BASELINES);

    bool setSuper = false;
    bool setSub   = false;

    if (Inkscape::is_query_style_updateable(result_baseline)) {
        // If not set or mixed, turn on superscript or subscript
        if (mode == 0) {
            setSuper = true;
        } else {
            setSub = true;
        }
    } else {
        // Superscript
        gboolean superscriptSet = (query.baseline_shift.set &&
                                   query.baseline_shift.type == SP_BASELINE_SHIFT_LITERAL &&
                                   query.baseline_shift.literal == SP_CSS_BASELINE_SHIFT_SUPER );

        // Subscript
        gboolean subscriptSet = (query.baseline_shift.set &&
                                 query.baseline_shift.type == SP_BASELINE_SHIFT_LITERAL &&
                                 query.baseline_shift.literal == SP_CSS_BASELINE_SHIFT_SUB );

        setSuper = !superscriptSet && mode == 0;
        setSub = !subscriptSet && mode == 1;
    }

    // Set css properties
    SPCSSAttr *css = sp_repr_css_attr_new ();
    if( setSuper || setSub ) {
        // Openoffice 2.3 and Adobe use 58%, Microsoft Word 2002 uses 65%, LaTex about 70%.
        // 58% looks too small to me, especially if a superscript is placed on a superscript.
        // If you make a change here, consider making a change to baseline-shift amount
        // in style.cpp.
        sp_repr_css_set_property (css, "font-size", "65%");
    } else {
        sp_repr_css_set_property (css, "font-size", "");
    }
    if( setSuper ) {
        sp_repr_css_set_property (css, "baseline-shift", "super");
    } else if( setSub ) {
        sp_repr_css_set_property (css, "baseline-shift", "sub");
    } else {
        sp_repr_css_set_property (css, "baseline-shift", "baseline");
    }

    // Apply css to selected objects.
    SPDesktop *desktop = _desktop;
    sp_desktop_set_style (desktop, css, true, false);

    // Save for undo
    if(result_baseline != QUERY_STYLE_NOTHING) {
        DocumentUndo::maybeDone(_desktop->getDocument(), "ttb:script", _("Text: Change superscript or subscript"), INKSCAPE_ICON("draw-text"));
    }
    _freeze = false;
}

void TextToolbar::align_mode_changed(int mode)
{
    // quit if run by the _changed callbacks
    if (_freeze) {
        return;
    }
    _freeze = true;

    Preferences::get()->setInt("/tools/text/align_mode", mode);

    SPDesktop *desktop = _desktop;

    // move the x of all texts to preserve the same bbox
    Inkscape::Selection *selection = desktop->getSelection();
    auto itemlist= selection->items();
    for (auto i : itemlist) {
        auto text = cast<SPText>(i);
        // auto flowtext = cast<SPFlowtext>(i);
        if (text) {
            SPItem *item = i;

            unsigned writing_mode = item->style->writing_mode.value;
            // below, variable names suggest horizontal move, but we check the writing direction
            // and move in the corresponding axis
            Geom::Dim2 axis;
            if (writing_mode == SP_CSS_WRITING_MODE_LR_TB || writing_mode == SP_CSS_WRITING_MODE_RL_TB) {
                axis = Geom::X;
            } else {
                axis = Geom::Y;
            }

            Geom::OptRect bbox = item->geometricBounds();
            if (!bbox)
                continue;
            double width = bbox->dimensions()[axis];
            // If you want to align within some frame, other than the text's own bbox, calculate
            // the left and right (or top and bottom for tb text) slacks of the text inside that
            // frame (currently unused)
            double left_slack = 0;
            double right_slack = 0;
            unsigned old_align = item->style->text_align.value;
            double move = 0;
            if (old_align == SP_CSS_TEXT_ALIGN_START || old_align == SP_CSS_TEXT_ALIGN_LEFT) {
                switch (mode) {
                    case 0:
                        move = -left_slack;
                        break;
                    case 1:
                        move = width/2 + (right_slack - left_slack)/2;
                        break;
                    case 2:
                        move = width + right_slack;
                        break;
                }
            } else if (old_align == SP_CSS_TEXT_ALIGN_CENTER) {
                switch (mode) {
                    case 0:
                        move = -width/2 - left_slack;
                        break;
                    case 1:
                        move = (right_slack - left_slack)/2;
                        break;
                    case 2:
                        move = width/2 + right_slack;
                        break;
                }
            } else if (old_align == SP_CSS_TEXT_ALIGN_END || old_align == SP_CSS_TEXT_ALIGN_RIGHT) {
                switch (mode) {
                    case 0:
                        move = -width - left_slack;
                        break;
                    case 1:
                        move = -width/2 + (right_slack - left_slack)/2;
                        break;
                    case 2:
                        move = right_slack;
                        break;
                }
            }
            Geom::Point XY = cast<SPText>(item)->attributes.firstXY();
            if (axis == Geom::X) {
                XY = XY + Geom::Point (move, 0);
            } else {
                XY = XY + Geom::Point (0, move);
            }
            cast<SPText>(item)->attributes.setFirstXY(XY);
            item->updateRepr();
            item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
        }
    }

    SPCSSAttr *css = sp_repr_css_attr_new ();
    switch (mode)
    {
        case 0:
        {
            sp_repr_css_set_property (css, "text-anchor", "start");
            sp_repr_css_set_property (css, "text-align", "start");
            break;
        }
        case 1:
        {
            sp_repr_css_set_property (css, "text-anchor", "middle");
            sp_repr_css_set_property (css, "text-align", "center");
            break;
        }

        case 2:
        {
            sp_repr_css_set_property (css, "text-anchor", "end");
            sp_repr_css_set_property (css, "text-align", "end");
            break;
        }

        case 3:
        {
            sp_repr_css_set_property (css, "text-anchor", "start");
            sp_repr_css_set_property (css, "text-align", "justify");
            break;
        }
    }

    if (mergeDefaultStyle(css)) {
        DocumentUndo::done(_desktop->getDocument(), _("Text: Change alignment"), INKSCAPE_ICON("draw-text"));
    }
    sp_repr_css_attr_unref (css);

    desktop->getCanvas()->grab_focus();

    _freeze = false;
}

void TextToolbar::writing_mode_changed(int mode)
{
    // quit if run by the _changed callbacks
    if (_freeze) {
        return;
    }
    _freeze = true;

    Preferences::get()->setInt("/tools/text/writing_mode", mode);

    SPCSSAttr   *css        = sp_repr_css_attr_new ();
    switch (mode)
    {
        case 0:
            {
                sp_repr_css_set_property (css, "writing-mode", "lr-tb");
                break;
            }

        case 1:
            {
                sp_repr_css_set_property (css, "writing-mode", "tb-rl");
                break;
            }

        case 2:
            {
                sp_repr_css_set_property (css, "writing-mode", "vertical-lr");
                break;
            }
    }

    if (mergeDefaultStyle(css)) {
        DocumentUndo::done(_desktop->getDocument(), _("Text: Change writing mode"), INKSCAPE_ICON("draw-text"));
    }
    sp_repr_css_attr_unref (css);

    _desktop->getCanvas()->grab_focus();

    _freeze = false;
}

void TextToolbar::orientation_changed(int mode)
{
    // quit if run by the _changed callbacks
    if (_freeze) {
        return;
    }
    _freeze = true;

    Preferences::get()->setInt("/tools/text/orientation_mode", mode);

    SPCSSAttr   *css        = sp_repr_css_attr_new ();
    switch (mode)
    {
        case 0:
        {
            sp_repr_css_set_property (css, "text-orientation", "auto");
            break;
        }

        case 1:
        {
            sp_repr_css_set_property (css, "text-orientation", "upright");
            break;
        }

        case 2:
        {
            sp_repr_css_set_property (css, "text-orientation", "sideways");
            break;
        }
    }

    if (mergeDefaultStyle(css)) {
        DocumentUndo::done(_desktop->getDocument(), _("Text: Change orientation"), INKSCAPE_ICON("draw-text"));
    }
    sp_repr_css_attr_unref (css);
    _desktop->getCanvas()->grab_focus();

    _freeze = false;
}

void TextToolbar::direction_changed(int mode)
{
    // quit if run by the _changed callbacks
    if (_freeze) {
        return;
    }
    _freeze = true;

    Preferences::get()->setInt("/tools/text/direction_mode", mode);

    SPCSSAttr   *css        = sp_repr_css_attr_new ();
    switch (mode)
    {
        case 0:
        {
            sp_repr_css_set_property (css, "direction", "ltr");
            break;
        }

        case 1:
        {
            sp_repr_css_set_property (css, "direction", "rtl");
            break;
        }
    }

    if (mergeDefaultStyle(css)) {
        DocumentUndo::done(_desktop->getDocument(), _("Text: Change direction"), INKSCAPE_ICON("draw-text"));
    }
    sp_repr_css_attr_unref (css);

    _desktop->getCanvas()->grab_focus();

    _freeze = false;
}

void TextToolbar::lineheight_value_changed()
{
    // quit if run by the _changed callbacks or is not text tool
    if (_freeze || !SP_TEXT_CONTEXT(_desktop->getTool())) {
        return;
    }

    _freeze = true;
    SPDesktop *desktop = _desktop;
    // Get user selected unit and save as preference
    Unit const *unit = _tracker->getActiveUnit();
    // @Tav same disabled unit
    g_return_if_fail(unit != nullptr);

    // This nonsense is to get SP_CSS_UNIT_xx value corresponding to unit so
    // we can save it (allows us to adjust line height value when unit changes).

    // Set css line height.
    SPCSSAttr *css = sp_repr_css_attr_new ();
    Inkscape::CSSOStringStream osfs;
    if ( is_relative(unit) ) {
        osfs << _line_height_item.get_adjustment()->get_value() << unit->abbr;
    } else {
        // Inside SVG file, always use "px" for absolute units.
        osfs << Quantity::convert(_line_height_item.get_adjustment()->get_value(), unit, "px") << "px";
    }

    sp_repr_css_set_property (css, "line-height", osfs.str().c_str());

    Inkscape::Selection *selection = desktop->getSelection();
    auto itemlist = selection->items();
    if (_outer) {
        // Special else makes this different from other uses of text_outer_set_style
        text_outer_set_style(css);
    } else {
        auto parent = itemlist.front();
        SPStyle *parent_style = parent->style;
        SPCSSAttr *parent_cssatr = sp_css_attr_from_style(parent_style, SP_STYLE_FLAG_IFSET);
        Glib::ustring parent_lineheight = sp_repr_css_property(parent_cssatr, "line-height", "1.25");
        SPCSSAttr *cssfit = sp_repr_css_attr_new();
        sp_repr_css_set_property(cssfit, "line-height", parent_lineheight.c_str());
        double minheight = 0;
        if (parent_style) {
            minheight = parent_style->line_height.computed;
        }
        if (minheight) {
            for (auto i : parent->childList(false)) {
                auto child = cast<SPItem>(i);
                if (!child) {
                    continue;
                }
                recursively_set_properties(child, cssfit);
            }
        }
        sp_repr_css_set_property(cssfit, "line-height", "0");
        parent->changeCSS(cssfit, "style");
        subselection_wrap_toggle(true);
        sp_desktop_set_style(desktop, css, true, true);
        subselection_wrap_toggle(false);
        sp_repr_css_attr_unref(cssfit);
    }
    // Only need to save for undo if a text item has been changed.
    itemlist = selection->items();
    bool modmade = false;
    for (auto i : itemlist) {
        auto text = cast<SPText>(i);
        auto flowtext = cast<SPFlowtext>(i);
        if (text || flowtext) {
            modmade = true;
            break;
        }
    }

    // Save for undo
    if (modmade) {
        // Call ensureUpToDate() causes rebuild of text layout (with all proper style
        // cascading, etc.). For multi-line text with sodipodi::role="line", we must explicitly
        // save new <tspan> 'x' and 'y' attribute values by calling updateRepr().
        // Partial fix for bug #1590141.

        desktop->getDocument()->ensureUpToDate();
        for (auto i : itemlist) {
            auto text = cast<SPText>(i);
            auto flowtext = cast<SPFlowtext>(i);
            if (text || flowtext) {
                (i)->updateRepr();
            }
        }
        if (!_outer) {
            prepare_inner();
        }
        DocumentUndo::maybeDone(desktop->getDocument(), "ttb:line-height", _("Text: Change line-height"), INKSCAPE_ICON("draw-text"));
    }

    mergeDefaultStyle(css);

    sp_repr_css_attr_unref (css);

    _freeze = false;
}

/**
 * Merge the style into either the tool or the desktop style depending on
 * which one the user has decided to use in the preferences.
 *
 * @returns true if style was set to an object.
 */
bool TextToolbar::mergeDefaultStyle(SPCSSAttr *css)
{
    // If no selected objects, set default.
    SPStyle query(_desktop->getDocument());
    int result_numbers = sp_desktop_query_style(_desktop, &query, QUERY_STYLE_PROPERTY_FONTNUMBERS);
    if (result_numbers == QUERY_STYLE_NOTHING) {
        Preferences::get()->mergeStyle("/tools/text/style", css);
    }
    // This updates the global style
    sp_desktop_set_style (_desktop, css, true, true);
    return result_numbers != QUERY_STYLE_NOTHING;
}

void TextToolbar::lineheight_unit_changed(int /* Not Used */)
{
    // quit if run by the _changed callbacks or is not text tool
    if (_freeze || !SP_TEXT_CONTEXT(_desktop->getTool())) {
        return;
    }
    _freeze = true;

    // Get old saved unit
    int old_unit = _lineheight_unit;

    // Get user selected unit and save as preference
    Unit const *unit = _tracker->getActiveUnit();
    g_return_if_fail(unit != nullptr);

    // This nonsense is to get SP_CSS_UNIT_xx value corresponding to unit.
    SPILength temp_length;
    Inkscape::CSSOStringStream temp_stream;
    temp_stream << 1 << unit->abbr;
    temp_length.read(temp_stream.str().c_str());
    Preferences::get()->setInt("/tools/text/lineheight/display_unit", temp_length.unit);
    if (old_unit == temp_length.unit) {
        _freeze = false;
        return;
    } else {
        _lineheight_unit = temp_length.unit;
    }

    // Read current line height value
    auto line_height_adj = _line_height_item.get_adjustment();
    double line_height = line_height_adj->get_value();
    SPDesktop *desktop = _desktop;
    Inkscape::Selection *selection = desktop->getSelection();
    auto itemlist = selection->items();

    // Convert between units
    double font_size = 0;
    double doc_scale = 1;
    int count = 0;

    for (auto i : itemlist) {
        auto text = cast<SPText>(i);
        auto flowtext = cast<SPFlowtext>(i);
        if (text || flowtext) {
            doc_scale = Geom::Affine(i->i2dt_affine()).descrim();
            font_size += i->style->font_size.computed * doc_scale;
            ++count;
        }
    }
    if (count > 0) {
        font_size /= count;
    } else {
        // ideally use default font-size.
        font_size = 20;
    }
    if ((unit->abbr == "" || unit->abbr == "em") && (old_unit == SP_CSS_UNIT_NONE || old_unit == SP_CSS_UNIT_EM)) {
        // Do nothing
    } else if ((unit->abbr == "" || unit->abbr == "em") && old_unit == SP_CSS_UNIT_EX) {
        line_height *= 0.5;
    } else if ((unit->abbr) == "ex" && (old_unit == SP_CSS_UNIT_EM || old_unit == SP_CSS_UNIT_NONE)) {
        line_height *= 2.0;
    } else if ((unit->abbr == "" || unit->abbr == "em") && old_unit == SP_CSS_UNIT_PERCENT) {
        line_height /= 100.0;
    } else if ((unit->abbr) == "%" && (old_unit == SP_CSS_UNIT_EM || old_unit == SP_CSS_UNIT_NONE)) {
        line_height *= 100;
    } else if ((unit->abbr) == "ex" && old_unit == SP_CSS_UNIT_PERCENT) {
        line_height /= 50.0;
    } else if ((unit->abbr) == "%" && old_unit == SP_CSS_UNIT_EX) {
        line_height *= 50;
    } else if (is_relative(unit)) {
        // Convert absolute to relative... for the moment use average font-size
        if (old_unit == SP_CSS_UNIT_NONE) old_unit = SP_CSS_UNIT_EM;
        line_height = Quantity::convert(line_height, sp_style_get_css_unit_string(old_unit), "px");

        if (font_size > 0) {
            line_height /= font_size;
        }
        if ((unit->abbr) == "%") {
            line_height *= 100;
        } else if ((unit->abbr) == "ex") {
            line_height *= 2;
        }
    } else if (old_unit == SP_CSS_UNIT_NONE || old_unit == SP_CSS_UNIT_PERCENT || old_unit == SP_CSS_UNIT_EM ||
            old_unit == SP_CSS_UNIT_EX) {
        // Convert relative to absolute... for the moment use average font-size
        if (old_unit == SP_CSS_UNIT_PERCENT) {
            line_height /= 100.0;
        } else if (old_unit == SP_CSS_UNIT_EX) {
            line_height /= 2.0;
        }
        line_height *= font_size;
        line_height = Quantity::convert(line_height, "px", unit);
    } else {
        // Convert between different absolute units (only used in GUI)
        line_height = Quantity::convert(line_height, sp_style_get_css_unit_string(old_unit), unit);
    }
    // Set css line height.
    SPCSSAttr *css = sp_repr_css_attr_new ();
    Inkscape::CSSOStringStream osfs;
    // Set css line height.
    if ( is_relative(unit) ) {
        osfs << line_height << unit->abbr;
    } else {
        osfs << Quantity::convert(line_height, unit, "px") << "px";
    }
    sp_repr_css_set_property (css, "line-height", osfs.str().c_str());

    // Update GUI with line_height value.
    line_height_adj->set_value(line_height);
    // Update "climb rate"  The custom action has a step property but no way to set it.
    if (unit->abbr == "%") {
        line_height_adj->set_step_increment(1.0);
        line_height_adj->set_page_increment(10.0);
    } else {
        line_height_adj->set_step_increment(0.1);
        line_height_adj->set_page_increment(1.0);
    }
    // Internal function to set line-height which is spacing mode dependent.
    SPItem *parent = itemlist.empty() ? nullptr : itemlist.front();
    SPStyle *parent_style = nullptr;
    if (parent) {
        parent_style = parent->style;
    }
    bool inside = false;
    if (_outer) {
        if (!selection->singleItem() || !parent_style || parent_style->line_height.computed != 0) {
            for (auto i = itemlist.begin(); i != itemlist.end(); ++i) {
                if (is<SPText>(*i) || is<SPFlowtext>(*i)) {
                    SPItem *item = *i;
                    // Scale by inverse of accumulated parent transform
                    SPCSSAttr *css_set = sp_repr_css_attr_new();
                    sp_repr_css_merge(css_set, css);
                    Geom::Affine const local(item->i2doc_affine());
                    double const ex(local.descrim());
                    if ((ex != 0.0) && (ex != 1.0)) {
                        sp_css_attr_scale(css_set, 1 / ex);
                    }
                    recursively_set_properties(item, css_set);
                    sp_repr_css_attr_unref(css_set);
                }
            }
        } else {
            inside = true;
        }
    }
    if (!_outer || inside) {
        SPCSSAttr *parent_cssatr = sp_css_attr_from_style(parent_style, SP_STYLE_FLAG_IFSET);
        Glib::ustring parent_lineheight = sp_repr_css_property(parent_cssatr, "line-height", "1.25");
        SPCSSAttr *cssfit = sp_repr_css_attr_new();
        sp_repr_css_set_property(cssfit, "line-height", parent_lineheight.c_str());
        double minheight = 0;
        if (parent_style) {
            minheight = parent_style->line_height.computed;
        }
        if (minheight) {
            for (auto i : parent->childList(false)) {
                auto child = cast<SPItem>(i);
                if (!child) {
                    continue;
                }
                recursively_set_properties(child, cssfit);
            }
        }
        sp_repr_css_set_property(cssfit, "line-height", "0");
        parent->changeCSS(cssfit, "style");
        subselection_wrap_toggle(true);
        sp_desktop_set_style(desktop, css, true, true);
        subselection_wrap_toggle(false);
        sp_repr_css_attr_unref(cssfit);
    }
    itemlist= selection->items();
    // Only need to save for undo if a text item has been changed.
    bool modmade = false;
    for (auto i : itemlist) {
        auto text = cast<SPText>(i);
        auto flowtext = cast<SPFlowtext>(i);
        if (text || flowtext) {
            modmade = true;
            break;
        }
    }
    // Save for undo
    if(modmade) {
        // Call ensureUpToDate() causes rebuild of text layout (with all proper style
        // cascading, etc.). For multi-line text with sodipodi::role="line", we must explicitly
        // save new <tspan> 'x' and 'y' attribute values by calling updateRepr().
        // Partial fix for bug #1590141.

        desktop->getDocument()->ensureUpToDate();
        for (auto i : itemlist) {
            auto text = cast<SPText>(i);
            auto flowtext = cast<SPFlowtext>(i);
            if (text || flowtext) {
                (i)->updateRepr();
            }
        }
        if (_outer) {
            prepare_inner();
        }
        DocumentUndo::maybeDone(_desktop->getDocument(), "ttb:line-height", _("Text: Change line-height unit"), INKSCAPE_ICON("draw-text"));
    }

    mergeDefaultStyle(css);

    sp_repr_css_attr_unref (css);

    _freeze = false;
}

void TextToolbar::fontsize_unit_changed(int /* Not Used */)
{
    // quit if run by the _changed callbacks
    Unit const *unit = _tracker_fs->getActiveUnit();
    g_return_if_fail(unit != nullptr);

    // This nonsense is to get SP_CSS_UNIT_xx value corresponding to unit.
    SPILength temp_size;
    Inkscape::CSSOStringStream temp_size_stream;
    temp_size_stream << 1 << unit->abbr;
    temp_size.read(temp_size_stream.str().c_str());
    Preferences::get()->setInt("/options/font/unitType", temp_size.unit);
    //selection_changed(_desktop->getSelection());
}

void TextToolbar::wordspacing_value_changed()
{
    // quit if run by the _changed callbacks
    if (_freeze) {
        return;
    }
    _freeze = true;

    // At the moment this handles only numerical values (i.e. no em unit).
    // Set css word-spacing
    SPCSSAttr *css = sp_repr_css_attr_new ();
    Inkscape::CSSOStringStream osfs;
    osfs << _word_spacing_item.get_adjustment()->get_value() << "px"; // For now always use px
    sp_repr_css_set_property (css, "word-spacing", osfs.str().c_str());
    text_outer_set_style(css);

    if (mergeDefaultStyle(css)) {
        DocumentUndo::maybeDone(_desktop->getDocument(), "ttb:word-spacing", _("Text: Change word-spacing"), INKSCAPE_ICON("draw-text"));
    }

    sp_repr_css_attr_unref (css);

    _freeze = false;
}

void TextToolbar::letterspacing_value_changed()
{
    // quit if run by the _changed callbacks
    if (_freeze) {
        return;
    }
    _freeze = true;

    // At the moment this handles only numerical values (i.e. no em unit).
    // Set css letter-spacing
    SPCSSAttr *css = sp_repr_css_attr_new ();
    Inkscape::CSSOStringStream osfs;
    osfs << _letter_spacing_item.get_adjustment()->get_value() << "px"; // For now always use px
    sp_repr_css_set_property (css, "letter-spacing", osfs.str().c_str());
    text_outer_set_style(css);

    if (mergeDefaultStyle(css)) {
        DocumentUndo::maybeDone(_desktop->getDocument(), "ttb:letter-spacing", _("Text: Change letter-spacing"), INKSCAPE_ICON("draw-text"));
    }

    sp_repr_css_attr_unref (css);

    _freeze = false;
}

void TextToolbar::dx_value_changed()
{
    // quit if run by the _changed callbacks
    if (_freeze) {
        return;
    }
    _freeze = true;

    gdouble new_dx = _dx_item.get_adjustment()->get_value();
    bool modmade = false;

    if (auto tc = SP_TEXT_CONTEXT(_desktop->getTool())) {
        unsigned char_index = -1;
        if (auto attributes = text_tag_attributes_at_position(tc->textItem(), std::min(tc->text_sel_start, tc->text_sel_end), &char_index)) {
            double old_dx = attributes->getDx(char_index);
            double delta_dx = new_dx - old_dx;
            sp_te_adjust_dx(tc->textItem(), tc->text_sel_start, tc->text_sel_end, _desktop, delta_dx);
            modmade = true;
        }
    }

    if(modmade) {
        // Save for undo
        DocumentUndo::maybeDone(_desktop->getDocument(), "ttb:dx", _("Text: Change dx (kern)"), INKSCAPE_ICON("draw-text"));
    }
    _freeze = false;
}

void TextToolbar::dy_value_changed()
{
    // quit if run by the _changed callbacks
    if (_freeze) {
        return;
    }
    _freeze = true;

    gdouble new_dy = _dy_item.get_adjustment()->get_value();
    bool modmade = false;

    if (auto tc = SP_TEXT_CONTEXT(_desktop->getTool())) {
        unsigned char_index = -1;
        if (auto attributes = text_tag_attributes_at_position(tc->textItem(), std::min(tc->text_sel_start, tc->text_sel_end), &char_index)) {
            double old_dy = attributes->getDy(char_index);
            double delta_dy = new_dy - old_dy;
            sp_te_adjust_dy(tc->textItem(), tc->text_sel_start, tc->text_sel_end, _desktop, delta_dy);
            modmade = true;
        }
    }

    if(modmade) {
        // Save for undo
        DocumentUndo::maybeDone(_desktop->getDocument(), "ttb:dy", _("Text: Change dy"), INKSCAPE_ICON("draw-text"));
    }

    _freeze = false;
}

void TextToolbar::rotation_value_changed()
{
    // quit if run by the _changed callbacks
    if (_freeze) {
        return;
    }
    _freeze = true;

    gdouble new_degrees = _rotation_item.get_adjustment()->get_value();

    bool modmade = false;
    if (auto tc = SP_TEXT_CONTEXT(_desktop->getTool())) {
        unsigned char_index = -1;
        if (auto attributes = text_tag_attributes_at_position(tc->textItem(), std::min(tc->text_sel_start, tc->text_sel_end), &char_index)) {
            double old_degrees = attributes->getRotate(char_index);
            double delta_deg = new_degrees - old_degrees;
            sp_te_adjust_rotation(tc->textItem(), tc->text_sel_start, tc->text_sel_end, _desktop, delta_deg);
            modmade = true;
        }
    }

    // Save for undo
    if(modmade) {
        DocumentUndo::maybeDone(_desktop->getDocument(), "ttb:rotate", _("Text: Change rotate"), INKSCAPE_ICON("draw-text"));
    }

    _freeze = false;
}

void TextToolbar::selection_modified_select_tool(Inkscape::Selection *selection, guint flags)
{
    auto prefs = Inkscape::Preferences::get();
    double factor = prefs->getDouble("/options/font/scaleLineHeightFromFontSIze", 1.0);
    if (factor != 1.0) {
        Unit const *unit_lh = _tracker->getActiveUnit();
        g_return_if_fail(unit_lh != nullptr);
        if (!is_relative(unit_lh) && _outer) {
            double lineheight = _line_height_item.get_adjustment()->get_value();
            bool is_freeze = _freeze;
            _freeze = false;
            _line_height_item.get_adjustment()->set_value(lineheight * factor);
            _freeze = is_freeze;
        }
        prefs->setDouble("/options/font/scaleLineHeightFromFontSIze", 1.0);
    }
}

void TextToolbar::selection_changed(Inkscape::Selection *selection) // don't bother to update font list if subsel
                                                                         // changed
{
#ifdef DEBUG_TEXT
    static int count = 0;
    ++count;
    std::cout << std::endl;
    std::cout << "&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&" << std::endl;
    std::cout << "sp_text_toolbox_selection_changed: start " << count << std::endl;
#endif

    // quit if run by the _changed callbacks
    if (_freeze) {

#ifdef DEBUG_TEXT
        std::cout << "    Frozen, returning" << std::endl;
        std::cout << "sp_text_toolbox_selection_changed: exit " << count << std::endl;
        std::cout << "&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&" << std::endl;
        std::cout << std::endl;
#endif
        return;
    }
    _freeze = true;

    // selection defined as argument but not used, argh!!!
    SPDesktop *desktop = _desktop;
    SPDocument *document = _desktop->getDocument();
    selection = desktop->getSelection();
    auto itemlist = selection->items();

#ifdef DEBUG_TEXT
    for(auto i : itemlist) {
        const gchar* id = i->getId();
        std::cout << "    " << id << std::endl;
    }
    Glib::ustring selected_text = sp_text_get_selected_text(_desktop->getTool());
    std::cout << "  Selected text: |" << selected_text << "|" << std::endl;
#endif

    // Only flowed text can be justified, only normal text can be kerned...
    // Find out if we have flowed text now so we can use it several places
    gboolean isFlow = false;
    std::vector<SPItem *> to_work;
    for (auto i : itemlist) {
        auto text = cast<SPText>(i);
        auto flowtext = cast<SPFlowtext>(i);
        if (text || flowtext) {
            to_work.push_back(i);
        }
        if (flowtext ||
            (text && text->style && text->style->shape_inside.set)) {
            isFlow = true;
        }
    }
    bool outside = false;
    if (selection && to_work.size() == 0) {
        outside = true;
    }

    Inkscape::FontLister *fontlister = Inkscape::FontLister::get_instance();
    fontlister->selection_update();
    // Update font list, but only if widget already created.
    _font_family_item->set_active_text(fontlister->get_font_family().c_str(), fontlister->get_font_family_row());
    _font_style_item->set_active_text(fontlister->get_font_style().c_str());

    /*
     * Query from current selection:
     *   Font family (font-family)
     *   Style (font-weight, font-style, font-stretch, font-variant, font-align)
     *   Numbers (font-size, letter-spacing, word-spacing, line-height, text-anchor, writing-mode)
     *   Font specification (Inkscape private attribute)
     */
    SPStyle query(document);
    SPStyle query_fallback(document);
    int result_family = sp_desktop_query_style(desktop, &query, QUERY_STYLE_PROPERTY_FONTFAMILY);
    int result_style = sp_desktop_query_style(desktop, &query, QUERY_STYLE_PROPERTY_FONTSTYLE);
    int result_baseline = sp_desktop_query_style(desktop, &query, QUERY_STYLE_PROPERTY_BASELINES);
    int result_wmode = sp_desktop_query_style(desktop, &query, QUERY_STYLE_PROPERTY_WRITINGMODES);

    // Calling sp_desktop_query_style will result in a call to TextTool::_styleQueried().
    // This returns the style of the selected text inside the <text> element... which
    // is often the style of one or more <tspan>s. If we want the style of the outer
    // <text> objects then we need to bypass the call to TextTool::_styleQueried().
    // The desktop selection never includes the elements inside the <text> element.
    int result_numbers = 0;
    int result_numbers_fallback = 0;
    if (!outside) {
        if (_outer && this->_sub_active_item) {
            std::vector<SPItem *> qactive{ this->_sub_active_item };
            auto parent = cast<SPItem>(this->_sub_active_item->parent);
            std::vector<SPItem *> qparent{ parent };
            result_numbers =
                sp_desktop_query_style_from_list(qactive, &query, QUERY_STYLE_PROPERTY_FONTNUMBERS);
            result_numbers_fallback =
                sp_desktop_query_style_from_list(qparent, &query_fallback, QUERY_STYLE_PROPERTY_FONTNUMBERS);
        } else if (_outer) {
            result_numbers = sp_desktop_query_style_from_list(to_work, &query, QUERY_STYLE_PROPERTY_FONTNUMBERS);
        } else {
            result_numbers = sp_desktop_query_style(desktop, &query, QUERY_STYLE_PROPERTY_FONTNUMBERS);
        }
    } else {
        result_numbers =
                sp_desktop_query_style(desktop, &query, QUERY_STYLE_PROPERTY_FONTNUMBERS);
    }

    auto prefs = Inkscape::Preferences::get();
    /*
     * If no text in selection (querying returned nothing), read the style from
     * the /tools/text preferences (default style for new texts). Return if
     * tool bar already set to these preferences.
     */
    if (result_family  == QUERY_STYLE_NOTHING ||
        result_style   == QUERY_STYLE_NOTHING ||
        result_numbers == QUERY_STYLE_NOTHING ||
        result_wmode   == QUERY_STYLE_NOTHING ) {

        // There are no texts in selection, read from preferences.
        if (prefs->getBool("/tools/text/usecurrent")) {
            query.mergeCSS(sp_desktop_get_style(desktop, true));
        } else {
            query.readFromPrefs("/tools/text");
        }

#ifdef DEBUG_TEXT
        std::cout << "    read style from prefs:" << std::endl;
        sp_print_font( &query );
#endif
        if (_text_style_from_prefs) {
            // Do not reset the toolbar style from prefs if we already did it last time
            _freeze = false;
#ifdef DEBUG_TEXT
            std::cout << "    text_style_from_prefs: toolbar already set" << std:: endl;
            std::cout << "sp_text_toolbox_selection_changed: exit " << count << std::endl;
            std::cout << "&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&" << std::endl;
            std::cout << std::endl;
#endif
            return;
        }

        // To ensure the value of the combobox is properly set on start-up, only mark
        // the prefs set if the combobox has already been constructed.
        _text_style_from_prefs = true;
    } else {
        _text_style_from_prefs = false;
    }

    // If we have valid query data for text (font-family, font-specification) set toolbar accordingly.
    {
        // Size (average of text selected)

        int unit = prefs->getInt("/options/font/unitType", SP_CSS_UNIT_PT);
        double size = 0;
        if (!size && _cusor_numbers != QUERY_STYLE_NOTHING) {
            size = sp_style_css_size_px_to_units(_query_cursor.font_size.computed, unit);
        }
        if (!size && result_numbers != QUERY_STYLE_NOTHING) {
            size = sp_style_css_size_px_to_units(query.font_size.computed, unit);
        }
        if (!size && result_numbers_fallback != QUERY_STYLE_NOTHING) {
            size = sp_style_css_size_px_to_units(query_fallback.font_size.computed, unit);
        }
        if (!size && _text_style_from_prefs) {
            size = sp_style_css_size_px_to_units(query.font_size.computed, unit);
        }

        auto unit_str = sp_style_get_css_unit_string(unit);
        Glib::ustring tooltip = Glib::ustring::format(_("Font size"), " (", unit_str, ")");

        _font_size_item->set_tooltip(tooltip.c_str());

        Inkscape::CSSOStringStream os;
        // We don't want to parse values just show

        _tracker_fs->setActiveUnitByAbbr(sp_style_get_css_unit_string(unit));
        int rounded_size = std::round(size);
        if (std::abs((size - rounded_size)/size) < 0.0001) {
            // We use rounded_size to avoid rounding errors when, say, converting stored 'px' values to displayed 'pt' values.
            os << rounded_size;
            selection_fontsize = rounded_size;
        } else {
            os << size;
            selection_fontsize = size;
        }

        // Freeze to ignore callbacks.
        //g_object_freeze_notify( G_OBJECT( fontSizeAction->combobox ) );
        _font_size_item->set_model(create_sizes_store(unit));
        //g_object_thaw_notify( G_OBJECT( fontSizeAction->combobox ) );

        _font_size_item->set_active_text( os.str().c_str() );

        // Superscript
        gboolean superscriptSet =
            ((result_baseline == QUERY_STYLE_SINGLE || result_baseline == QUERY_STYLE_MULTIPLE_SAME ) &&
             query.baseline_shift.set &&
             query.baseline_shift.type == SP_BASELINE_SHIFT_LITERAL &&
             query.baseline_shift.literal == SP_CSS_BASELINE_SHIFT_SUPER );

        _superscript_btn.set_active(superscriptSet);

        // Subscript
        gboolean subscriptSet =
            ((result_baseline == QUERY_STYLE_SINGLE || result_baseline == QUERY_STYLE_MULTIPLE_SAME ) &&
             query.baseline_shift.set &&
             query.baseline_shift.type == SP_BASELINE_SHIFT_LITERAL &&
             query.baseline_shift.literal == SP_CSS_BASELINE_SHIFT_SUB );

        _subscript_btn.set_active(subscriptSet);

        // Alignment

        // Note: SVG 1.1 doesn't include text-align, SVG 1.2 Tiny doesn't include text-align="justify"
        // text-align="justify" was a draft SVG 1.2 item (along with flowed text).
        // Only flowed text can be left and right justified at the same time.
        // Disable button if we don't have flowed text.

        _alignment_buttons[3]->set_sensitive(isFlow);

        int activeButton = 0;
        if (query.text_align.computed == SP_CSS_TEXT_ALIGN_START || query.text_align.computed == SP_CSS_TEXT_ALIGN_LEFT) {
            activeButton = 0;
        } else if (query.text_align.computed == SP_CSS_TEXT_ALIGN_CENTER) {
            activeButton = 1;
        } else if (query.text_align.computed == SP_CSS_TEXT_ALIGN_END || query.text_align.computed == SP_CSS_TEXT_ALIGN_RIGHT) {
            activeButton = 2;
        } else if (query.text_align.computed == SP_CSS_TEXT_ALIGN_JUSTIFY) {
            activeButton = 3;
        }
        _alignment_buttons[activeButton]->set_active(true);

        double height = 0;
        gint line_height_unit = 0;

        if (!height && _cusor_numbers != QUERY_STYLE_NOTHING) {
            height = _query_cursor.line_height.value;
            line_height_unit = _query_cursor.line_height.unit;
        }

        if (!height && result_numbers != QUERY_STYLE_NOTHING) {
            height = query.line_height.value;
            line_height_unit = query.line_height.unit;
        }

        if (!height && result_numbers_fallback != QUERY_STYLE_NOTHING) {
            height = query_fallback.line_height.value;
            line_height_unit = query_fallback.line_height.unit;
        }

        if (!height && _text_style_from_prefs) {
            height = query.line_height.value;
            line_height_unit = query.line_height.unit;
        }

        if (line_height_unit == SP_CSS_UNIT_PERCENT) {
            height *= 100.0; // Inkscape store % as fraction in .value
        }

        // We dot want to parse values just show
        if (!is_relative(SPCSSUnit(line_height_unit))) {
            gint curunit = prefs->getInt("/tools/text/lineheight/display_unit", 1);
            // For backwards comaptibility
            if (is_relative(SPCSSUnit(curunit))) {
                prefs->setInt("/tools/text/lineheight/display_unit", 1);
                curunit = 1;
            }
            height = Quantity::convert(height, "px", sp_style_get_css_unit_string(curunit));
            line_height_unit = curunit;
        }
        auto line_height_adj = _line_height_item.get_adjustment();
        line_height_adj->set_value(height);

        // Update "climb rate"
        if (line_height_unit == SP_CSS_UNIT_PERCENT) {
            line_height_adj->set_step_increment(1.0);
            line_height_adj->set_page_increment(10.0);
        } else {
            line_height_adj->set_step_increment(0.1);
            line_height_adj->set_page_increment(1.0);
        }

        if( line_height_unit == SP_CSS_UNIT_NONE ) {
            // Function 'sp_style_get_css_unit_string' returns 'px' for unit none.
            // We need to avoid this.
            _tracker->setActiveUnitByAbbr("");
        } else {
            _tracker->setActiveUnitByAbbr(sp_style_get_css_unit_string(line_height_unit));
        }

        // Save unit so we can do conversions between new/old units.
        _lineheight_unit = line_height_unit;
        // Word spacing
        double wordSpacing;
        if (query.word_spacing.normal) wordSpacing = 0.0;
        else wordSpacing = query.word_spacing.computed; // Assume no units (change in desktop-style.cpp)

        _word_spacing_item.get_adjustment()->set_value(wordSpacing);

        // Letter spacing
        double letterSpacing;
        if (query.letter_spacing.normal) letterSpacing = 0.0;
        else letterSpacing = query.letter_spacing.computed; // Assume no units (change in desktop-style.cpp)

        _letter_spacing_item.get_adjustment()->set_value(letterSpacing);

        // Writing mode
        int activeButton2 = 0;
        if (query.writing_mode.computed == SP_CSS_WRITING_MODE_LR_TB) activeButton2 = 0;
        if (query.writing_mode.computed == SP_CSS_WRITING_MODE_TB_RL) activeButton2 = 1;
        if (query.writing_mode.computed == SP_CSS_WRITING_MODE_TB_LR) activeButton2 = 2;

        _writing_buttons[activeButton2]->set_active(true);

        // Orientation
        int activeButton3 = 0;
        if (query.text_orientation.computed == SP_CSS_TEXT_ORIENTATION_MIXED   ) activeButton3 = 0;
        if (query.text_orientation.computed == SP_CSS_TEXT_ORIENTATION_UPRIGHT ) activeButton3 = 1;
        if (query.text_orientation.computed == SP_CSS_TEXT_ORIENTATION_SIDEWAYS) activeButton3 = 2;

        _orientation_buttons[activeButton3]->set_active(true);

        // Disable text orientation for horizontal text...
        for (auto btn : _orientation_buttons) {
            btn->set_sensitive(activeButton2 != 0);
        }

        // Direction
        int activeButton4 = 0;
        if (query.direction.computed == SP_CSS_DIRECTION_LTR ) activeButton4 = 0;
        if (query.direction.computed == SP_CSS_DIRECTION_RTL ) activeButton4 = 1;
        _direction_buttons[activeButton4]->set_active(true);
    }

#ifdef DEBUG_TEXT
    std::cout << "    GUI: fontfamily.value: "     << query.font_family.value()  << std::endl;
    std::cout << "    GUI: font_size.computed: "   << query.font_size.computed   << std::endl;
    std::cout << "    GUI: font_weight.computed: " << query.font_weight.computed << std::endl;
    std::cout << "    GUI: font_style.computed: "  << query.font_style.computed  << std::endl;
    std::cout << "    GUI: text_anchor.computed: " << query.text_anchor.computed << std::endl;
    std::cout << "    GUI: text_align.computed:  " << query.text_align.computed  << std::endl;
    std::cout << "    GUI: line_height.computed: " << query.line_height.computed
              << "  line_height.value: "    << query.line_height.value
              << "  line_height.unit: "     << query.line_height.unit  << std::endl;
    std::cout << "    GUI: word_spacing.computed: " << query.word_spacing.computed
              << "  word_spacing.value: "    << query.word_spacing.value
              << "  word_spacing.unit: "     << query.word_spacing.unit  << std::endl;
    std::cout << "    GUI: letter_spacing.computed: " << query.letter_spacing.computed
              << "  letter_spacing.value: "    << query.letter_spacing.value
              << "  letter_spacing.unit: "     << query.letter_spacing.unit  << std::endl;
    std::cout << "    GUI: writing_mode.computed: " << query.writing_mode.computed << std::endl;
#endif

    // Kerning (xshift), yshift, rotation.  NB: These are not CSS attributes.
    if (auto tc = SP_TEXT_CONTEXT(_desktop->getTool())) {
        unsigned char_index = -1;
        if (auto attributes = text_tag_attributes_at_position(tc->textItem(), std::min(tc->text_sel_start, tc->text_sel_end), &char_index)) {
            // Dx
            double dx = attributes->getDx(char_index);
            _dx_item.get_adjustment()->set_value(dx);

            // Dy
            double dy = attributes->getDy(char_index);
            _dy_item.get_adjustment()->set_value(dy);

            // Rotation
            double rotation = attributes->getRotate(char_index);
            /* SVG value is between 0 and 360 but we're using -180 to 180 in widget */
            if (rotation > 180.0) {
                rotation -= 360.0;
            }
            _rotation_item.get_adjustment()->set_value(rotation);

#ifdef DEBUG_TEXT
            std::cout << "    GUI: Dx: " << dx << std::endl;
            std::cout << "    GUI: Dy: " << dy << std::endl;
            std::cout << "    GUI: Rotation: " << rotation << std::endl;
#endif
        }
    }

    {
        // Set these here as we don't always have kerning/rotating attributes
        _dx_item.set_sensitive(!isFlow);
        _dy_item.set_sensitive(!isFlow);
        _rotation_item.set_sensitive(!isFlow);
    }

#ifdef DEBUG_TEXT
    std::cout << "sp_text_toolbox_selection_changed: exit " << count << std::endl;
    std::cout << "&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&" << std::endl;
    std::cout << std::endl;
#endif

    _freeze = false;
}

void TextToolbar::watch_ec(SPDesktop *desktop, Inkscape::UI::Tools::ToolBase *tool)
{
    bool is_text_toolbar = dynamic_cast<const Inkscape::UI::Tools::TextTool*>(tool);
    bool is_select_toolbar = !is_text_toolbar && dynamic_cast<const Inkscape::UI::Tools::SelectTool*>(tool);
    if (is_text_toolbar) {
        // Watch selection
        // Ensure FontLister is updated here first..................
        c_selection_changed =
            desktop->getSelection()->connectChangedFirst(sigc::mem_fun(*this, &TextToolbar::selection_changed));
        c_selection_modified = desktop->getSelection()->connectModifiedFirst(sigc::mem_fun(*this, &TextToolbar::selection_modified));
        c_subselection_changed = desktop->connect_text_cursor_moved([this](void* sender, Inkscape::UI::Tools::TextTool* tool){
            subselection_changed(tool);
        });
        this->_sub_active_item = nullptr;
        this->_cusor_numbers = 0;
        selection_changed(desktop->getSelection());
    } else if (is_select_toolbar) {
        c_selection_modified_select_tool = desktop->getSelection()->connectModifiedFirst(
            sigc::mem_fun(*this, &TextToolbar::selection_modified_select_tool));
    }


    if (!is_text_toolbar) {
        c_selection_changed.disconnect();
        c_selection_modified.disconnect();
        c_subselection_changed.disconnect();
    }

    if (!is_select_toolbar) {
        c_selection_modified_select_tool.disconnect();
    }
}

void TextToolbar::selection_modified(Inkscape::Selection *selection, guint /*flags*/)
{
    this->_sub_active_item = nullptr;
    selection_changed(selection);

}

void TextToolbar::subselection_wrap_toggle(bool start)
{
    if (auto tc = SP_TEXT_CONTEXT(_desktop->getTool())) {
        _updating = true;
        if (te_get_layout(tc->textItem())) {
            std::swap(tc->text_sel_start, wrap_start);
            std::swap(tc->text_sel_end, wrap_end);
        }
        _updating = start;
    }
}

/*
* This function parses the just created line height in one or more lines of a text subselection.
* It can describe 2 kinds of input because when we store a text element we apply a fallback that change
* structure. This visually is not reflected but user maybe want to change a part of this subselection
* once the fallback is created, so we need more complex logic here to fill the gap.
* Basically, we have a line height changed in the new wrapper element/s between wrap_start and wrap_end.
* These variables store starting iterator of first char in line and last char in line in a subselection.
* These elements are styled well but we can have orphaned text nodes before and after the subselection.
* So, normally 3 elements are inside a container as direct child of a text element.
* We need to apply the container style to the optional first and last text nodes,
* wrapping into a new element that gets the container style (this is not part to the sub-selection).
* After wrapping, we unindent all children of the container and remove the container.
*
*/
void TextToolbar::prepare_inner()
{
    Inkscape::UI::Tools::TextTool *const tc = SP_TEXT_CONTEXT(_desktop->getTool());
    if (!tc) {
        return;
    }
    Inkscape::Text::Layout *layout = const_cast<Inkscape::Text::Layout *>(te_get_layout(tc->textItem()));
    if (!layout) {
      return;
    }
    auto doc = _desktop->getDocument();
    auto spobject = tc->textItem();
    auto spitem = tc->textItem();
    auto text = cast<SPText>(tc->textItem());
    auto flowtext = cast<SPFlowtext>(tc->textItem());
    Inkscape::XML::Document *xml_doc = doc->getReprDoc();
    if (!spobject) {
        return;
    }

    // We check for external files with text nodes direct children of text element
    // and wrap it into a tspan elements as inkscape do.
    if (text) {
        bool changed = false;
        std::vector<SPObject *> childs = spitem->childList(false);
        for (auto child : childs) {
            auto spstring = cast<SPString>(child);
            if (spstring) {
                Glib::ustring content = spstring->string;
                if (content != "\n") {
                    Inkscape::XML::Node *rstring = xml_doc->createTextNode(content.c_str());
                    Inkscape::XML::Node *rtspan  = xml_doc->createElement("svg:tspan");
                    //Inkscape::XML::Node *rnl     = xml_doc->createTextNode("\n");
                    rtspan->setAttribute("sodipodi:role", "line");
                    rtspan->addChild(rstring, nullptr);
                    text->getRepr()->addChild(rtspan, child->getRepr());
                    Inkscape::GC::release(rstring);
                    Inkscape::GC::release(rtspan);
                    text->getRepr()->removeChild(spstring->getRepr());
                    changed = true;
                }
            }
        }
        if (changed) {
            // proper rebuild happens later,
            // this just updates layout to use now, avoids use after free
            text->rebuildLayout();
        }
    }

    std::vector<SPObject *> containers;
    {
        // populate `containers` with objects that will be modified.

        // Temporarily remove the shape so Layout calculates
        // the position of wrap_end and wrap_start, even if
        // one of these are hidden because the previous line height was changed
        if (text) {
            text->hide_shape_inside();
        } else if (flowtext) {
            flowtext->fix_overflow_flowregion(false);
        }
        SPObject *rawptr_start = nullptr;
        SPObject *rawptr_end = nullptr;
        layout->validateIterator(&wrap_start);
        layout->validateIterator(&wrap_end);
        layout->getSourceOfCharacter(wrap_start, &rawptr_start);
        layout->getSourceOfCharacter(wrap_end, &rawptr_end);
        if (text) {
            text->show_shape_inside();
        } else if (flowtext) {
            flowtext->fix_overflow_flowregion(true);
        }
        if (!rawptr_start || !rawptr_end) {
            return;
        }

        // Loop through parents of start and end till we reach
        // first children of the text element.
        // Get all objects between start and end (inclusive)
        SPObject *start = rawptr_start;
        SPObject *end   = rawptr_end;
        while (start->parent != spobject) {
            start = start->parent;
        }
        while (end->parent != spobject) {
            end = end->parent;
        }

        while (start && start != end) {
            containers.push_back(start);
            start = start->getNext();
        }
        if (start) {
            containers.push_back(start);
        }
    }

    for (auto container : containers) {
        Inkscape::XML::Node *prevchild = container->getRepr();
        std::vector<SPObject*> childs = container->childList(false);
        for (auto child : childs) {
            auto spstring = cast<SPString>(child);
            auto flowtspan = cast<SPFlowtspan>(child);
            auto tspan = cast<SPTSpan>(child);
            // we need to upper all flowtspans to container level
            // to do this we need to change the element from flowspan to flowpara
            if (flowtspan) {
                Inkscape::XML::Node *flowpara = xml_doc->createElement("svg:flowPara");
                std::vector<SPObject*> fts_childs = flowtspan->childList(false);
                bool hascontent = false;
                // we need to move the contents to the new created element
                // maybe we can move directly but it is safer for me to duplicate,
                // inject into the new element and delete original
                for (auto fts_child : fts_childs) {
                    // is this check necessary?
                    if (fts_child) {
                        Inkscape::XML::Node *fts_child_node = fts_child->getRepr()->duplicate(xml_doc);
                        flowtspan->getRepr()->removeChild(fts_child->getRepr());
                        flowpara->addChild(fts_child_node, nullptr);
                        Inkscape::GC::release(fts_child_node);
                        hascontent = true;
                    }
                }
                // if no contents we dont want to add
                if (hascontent) {
                    flowpara->setAttribute("style", flowtspan->getRepr()->attribute("style"));
                    spobject->getRepr()->addChild(flowpara, prevchild);
                    Inkscape::GC::release(flowpara);
                    prevchild = flowpara;
                }
                container->getRepr()->removeChild(flowtspan->getRepr());
            } else if (tspan) {
                if (child->childList(false).size()) {
                    child->getRepr()->setAttribute("sodipodi:role", "line");
                    // maybe we need to move unindent function here
                    // to be the same as other here
                    prevchild = unindent_node(child->getRepr(), prevchild);
                } else {
                    // if no contents we dont want to add
                    container->getRepr()->removeChild(child->getRepr());
                }
            } else if (spstring) {
                // we are on a text node, we act different if in a text or flowtext.
                // wrap a duplicate of the element and unindent after the prevchild
                // and finally delete original
                Inkscape::XML::Node *string_node = xml_doc->createTextNode(spstring->string.c_str());
                if (text) {
                    Inkscape::XML::Node *tspan_node = xml_doc->createElement("svg:tspan");
                    tspan_node->setAttribute("style", container->getRepr()->attribute("style"));
                    tspan_node->addChild(string_node, nullptr);
                    tspan_node->setAttribute("sodipodi:role", "line");
                    text->getRepr()->addChild(tspan_node, prevchild);
                    Inkscape::GC::release(string_node);
                    Inkscape::GC::release(tspan_node);
                    prevchild = tspan_node;
                } else if (flowtext) {
                    Inkscape::XML::Node *flowpara_node = xml_doc->createElement("svg:flowPara");
                    flowpara_node->setAttribute("style", container->getRepr()->attribute("style"));
                    flowpara_node->addChild(string_node, nullptr);
                    flowtext->getRepr()->addChild(flowpara_node, prevchild);
                    Inkscape::GC::release(string_node);
                    Inkscape::GC::release(flowpara_node);
                    prevchild = flowpara_node;
                }
                container->getRepr()->removeChild(spstring->getRepr());
            }
        }
        tc->textItem()->getRepr()->removeChild(container->getRepr());
    }
}

Inkscape::XML::Node *TextToolbar::unindent_node(Inkscape::XML::Node *repr, Inkscape::XML::Node *prevchild)
{
    g_assert(repr != nullptr);

    Inkscape::XML::Node *parent = repr->parent();
    if (parent) {
        Inkscape::XML::Node *grandparent = parent->parent();
        if (grandparent) {
            SPDocument *doc = _desktop->getDocument();
            Inkscape::XML::Document *xml_doc = doc->getReprDoc();
            Inkscape::XML::Node *newrepr = repr->duplicate(xml_doc);
            parent->removeChild(repr);
            grandparent->addChild(newrepr, prevchild);
            Inkscape::GC::release(newrepr);
            newrepr->setAttribute("sodipodi:role", "line");
            return newrepr;
        }
    }
    std::cerr << "TextToolbar::unindent_node error: node has no (grand)parent, nothing done.\n";
    return repr;
}

void TextToolbar::display_font_collections()
{
    UI::remove_all_children(_font_collections_list);

    FontCollections *font_collections = Inkscape::FontCollections::get();

    // Insert system collections.
    for(auto const& col: font_collections->get_collections(true)) {
        auto const btn = Gtk::make_managed<Gtk::CheckButton>(col);
        btn->set_margin_bottom(2);
        btn->set_active(font_collections->is_collection_selected(col));
        btn->signal_toggled().connect([font_collections, col](){
            // toggle font system collection
            font_collections->update_selected_collections(col);
        });
// g_message("tag: %s", tag.display_name.c_str());
        auto const row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_focusable(false);
        row->set_child(*btn);
        _font_collections_list.append(*row);
    }

    // Insert row separator.
    auto const sep = Gtk::make_managed<Gtk::Separator>();
    sep->set_margin_bottom(2);
    auto const sep_row = Gtk::make_managed<Gtk::ListBoxRow>();
    sep_row->set_focusable(false);
    sep_row->set_child(*sep);
    _font_collections_list.append(*sep_row);

    // Insert user collections.
    for (auto const& col: font_collections->get_collections()) {
        auto const btn = Gtk::make_managed<Gtk::CheckButton>(col);
        btn->set_margin_bottom(2);
        btn->set_active(font_collections->is_collection_selected(col));
        btn->signal_toggled().connect([font_collections, col](){
            // toggle font collection
            font_collections->update_selected_collections(col);
        });
// g_message("tag: %s", tag.display_name.c_str());
        auto const row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_focusable(false);
        row->set_child(*btn);
        _font_collections_list.append(*row);
    }
}

void TextToolbar::on_fcm_button_pressed()
{
    // Inkscape::UI::Dialog::FontCollectionsManager::getInstance();
    if(auto desktop = SP_ACTIVE_DESKTOP) {
        if (auto container = desktop->getContainer()) {
            container->new_floating_dialog("FontCollections");
        }
    }
}

void TextToolbar::on_reset_button_pressed()
{
    FontCollections *font_collections = Inkscape::FontCollections::get();
    font_collections->clear_selected_collections();

    Inkscape::FontLister* font_lister = Inkscape::FontLister::get_instance();
    font_lister->init_font_families();
    font_lister->init_default_styles();

    SPDocument *document = _desktop->getDocument();

    if(!document) {
        return;
    }

    font_lister->add_document_fonts_at_top(document);
}

void TextToolbar::subselection_changed(Inkscape::UI::Tools::TextTool* tc)
{
#ifdef DEBUG_TEXT
    std::cout << std::endl;
    std::cout << "&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&" << std::endl;
    std::cout << "subselection_changed: start " << std::endl;
#endif
    // quit if run by the _changed callbacks
    this->_sub_active_item = nullptr;
    if (_updating) {
        return;
    }
    if (tc) {
        Inkscape::Text::Layout const *layout = te_get_layout(tc->textItem());
        if (layout) {
            Inkscape::Text::Layout::iterator start           = layout->begin();
            Inkscape::Text::Layout::iterator end             = layout->end();
            Inkscape::Text::Layout::iterator start_selection = tc->text_sel_start;
            Inkscape::Text::Layout::iterator end_selection   = tc->text_sel_end;
#ifdef DEBUG_TEXT
            std::cout << "    GUI: Start of text: " << layout->iteratorToCharIndex(start) << std::endl;
            std::cout << "    GUI: End of text: " << layout->iteratorToCharIndex(end) << std::endl;
            std::cout << "    GUI: Start of selection: " << layout->iteratorToCharIndex(start_selection) << std::endl;
            std::cout << "    GUI: End of selection: " << layout->iteratorToCharIndex(end_selection) << std::endl;
            std::cout << "    GUI: Loop Subelements: " << std::endl;
            std::cout << "    ::::::::::::::::::::::::::::::::::::::::::::: " << std::endl;
#endif
            gint startline = layout->paragraphIndex(start_selection);
            if (start_selection == end_selection) {
                this->_outer = true;
                gint counter = 0;
                for (auto child : tc->textItem()->childList(false)) {
                    auto item = cast<SPItem>(child);
                    if (item && counter == startline) {
                        this->_sub_active_item = item;
                        int origin_selection = layout->iteratorToCharIndex(start_selection);
                        Inkscape::Text::Layout::iterator next = layout->charIndexToIterator(origin_selection + 1);
                        Inkscape::Text::Layout::iterator prev = layout->charIndexToIterator(origin_selection - 1);
                        //TODO: find a better way to init
                        _updating = true;
                        SPStyle query(_desktop->getDocument());
                        _query_cursor = query;
                        Inkscape::Text::Layout::iterator start_line = tc->text_sel_start;
                        start_line.thisStartOfLine();
                        if (tc->text_sel_start == start_line) {
                            tc->text_sel_start = next;
                        } else {
                            tc->text_sel_start = prev;
                        }
                        _cusor_numbers = sp_desktop_query_style(_desktop, &_query_cursor, QUERY_STYLE_PROPERTY_FONTNUMBERS);
                        tc->text_sel_start = start_selection;
                        wrap_start = tc->text_sel_start;
                        wrap_end = tc->text_sel_end;
                        wrap_start.thisStartOfLine();
                        wrap_end.thisEndOfLine();
                        _updating = false;
                        break;
                    }
                    ++counter;
                }
                selection_changed(nullptr);
            } else if ((start_selection == start && end_selection == end) ||
                       (start_selection == end && end_selection == start)) {
                // full subselection
                _cusor_numbers = 0;
                this->_outer = true;
                selection_changed(nullptr);
            } else {
                _cusor_numbers = 0;
                this->_outer = false;
                wrap_start = tc->text_sel_start;
                wrap_end = tc->text_sel_end;
                if (tc->text_sel_start > tc->text_sel_end) {
                    wrap_start.thisEndOfLine();
                    wrap_end.thisStartOfLine();
                } else {
                    wrap_start.thisStartOfLine();
                    wrap_end.thisEndOfLine();
                }
                selection_changed(nullptr);
            }
        }
    }
#ifdef DEBUG_TEXT
    std::cout << "subselection_changed: exit " << std::endl;
    std::cout << "&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&" << std::endl;
    std::cout << std::endl;
#endif

}

} // namespace Inkscape::UI::Toolbar

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8 :
