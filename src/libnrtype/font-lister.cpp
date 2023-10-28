// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Font selection widgets
 */
/*
 * Authors:
 *   Chris Lahey <clahey@ximian.com>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Tavmjong Bah <tavmjong@free.fr>
 *   See Git history
 *
 * Copyright (C) 1999-2001 Ximian, Inc.
 * Copyright (C) 2002 Lauris Kaplinski
 * Copyright (C) 2013 Tavmjong Bah
 * Copyright (C) 2018+ Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "font-lister.h"

#include <glibmm/markup.h>
#include <glibmm/regex.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/settings.h>
#include <libnrtype/font-instance.h>

#include "font-factory.h"
#include "desktop.h"
#include "desktop-style.h"
#include "document.h"
#include "inkscape.h"
#include "preferences.h"
#include "object/sp-object.h"
// Following are needed to limit the source of updating font data to text and containers.
#include "object/sp-root.h"
#include "object/sp-anchor.h"
#include "object/sp-text.h"
#include "object/sp-tspan.h"
#include "object/sp-textpath.h"
#include "object/sp-tref.h"
#include "object/sp-flowtext.h"
#include "object/sp-flowdiv.h"
#include "util/font-collections.h"
#include "util/recently-used-fonts.h"
#include "util/document-fonts.h"
#include "xml/repr.h"

//#define DEBUG_FONT

// CSS dictates that font family names are case insensitive.
// This should really implement full Unicode case unfolding.
bool familyNamesAreEqual(const Glib::ustring &a, const Glib::ustring &b)
{
    return (a.casefold().compare(b.casefold()) == 0);
}

namespace Inkscape {

FontLister::FontLister()
{
    // Create default styles for use when font-family is unknown on system.
    default_styles = std::make_shared<Styles>(Styles{
        {"Normal"},
        {"Italic"},
        {"Bold"},
        {"Bold Italic"}
    });

    pango_family_map = FontFactory::get().GetUIFamilies();
    init_font_families();

    style_list_store = Gtk::ListStore::create(font_style_list);
    init_default_styles();

    // Watch gtk for the fonts-changed signal and refresh our pango configuration
    if (auto settings = Gtk::Settings::get_default()) {
        settings->property_gtk_fontconfig_timestamp().signal_changed().connect([this]() {
            FontFactory::get().refreshConfig();
            pango_family_map = FontFactory::get().GetUIFamilies();
            init_font_families(-1);
            new_fonts_signal.emit();
        });
    }
}

FontLister::~FontLister() = default;

bool FontLister::font_installed_on_system(Glib::ustring const &font) const
{
    return pango_family_map.find(font) != pango_family_map.end();
}

void FontLister::init_font_families(int group_offset, int group_size)
{
    static bool first_call = true;

    if (first_call)
    {
        font_list_store = Gtk::ListStore::create(font_list);
        first_call = false;
    }

    if (group_offset <= 0) {
        font_list_store->clear();
        if (group_offset == 0)
            insert_font_family("sans-serif");
    }

    font_list_store->freeze_notify();

    // Traverse through the family names and set up the list store
    for (auto const &key_val : pango_family_map) {
        if (!key_val.first.empty()) {
            auto row = *font_list_store->append();
            row[font_list.family] = key_val.first;
            // we don't set this now (too slow) but the style will be cached if the user
            // ever decides to use this font
            row[font_list.styles] = nullptr;
            // store the pango representation for generating the style
            row[font_list.pango_family] = key_val.second;
            row[font_list.onSystem] = true;
        }
    }

    font_list_store->thaw_notify();
}

void FontLister::init_default_styles()
{
    // Initialize style store with defaults
    style_list_store->freeze_notify();
    style_list_store->clear();
    for (auto const &style : *default_styles) {
        auto row = *style_list_store->append();
        row[font_style_list.cssStyle] = style.css_name;
        row[font_style_list.displayStyle] = style.display_name;
    }
    style_list_store->thaw_notify();
    update_signal.emit();
}

std::string FontLister::get_font_count_label() const
{
    std::string label;

    int size = font_list_store->children().size();
    int total_families = get_font_families_size();

    if (size >= total_families) {
        label += _("All Fonts");
    } else {
        label += _("Fonts ");
        label += std::to_string(size);
        label += "/";
        label += std::to_string(total_families);
    }

    return label;
}

FontLister *FontLister::get_instance()
{
    static FontLister instance;
    return &instance;
}

/// Try to find in the Haystack the Needle - ignore case
bool FontLister::find_string_case_insensitive(std::string const &text, std::string const &pat)
{
    auto it = std::search(
        text.begin(), text.end(),
        pat.begin(),   pat.end(),
        [] (unsigned char ch1, unsigned char ch2) { return std::toupper(ch1) == std::toupper(ch2); }
    );

    return it != text.end();
}

void FontLister::show_results(Glib::ustring const &search_text)
{
    // Clear currently selected collections.
    Inkscape::FontCollections::get()->clear_selected_collections();

    if (search_text == "") {
        init_font_families();
        init_default_styles();
        add_document_fonts_at_top(SP_ACTIVE_DOCUMENT);
        return;
    }

    // Clear the list store.
    font_list_store->freeze_notify();
    font_list_store->clear();

    // Start iterating over the families.
    // Take advantage of sorted families to speed up the search.
    for (auto const &[family_str, pango_family] : pango_family_map) {
        if (find_string_case_insensitive(family_str, search_text)) {
            auto row = *font_list_store->append();
            row.set_value(font_list.family, Glib::ustring{family_str});

            // we don't set this now (too slow) but the style will be cached if the user
            // ever decides to use this font
            // row.set_value(FontList.styles, nullptr); // not needed: default on new row

            // store the pango representation for generating the style
            row.set_value(font_list.pango_family, pango_family);
            row.set_value(font_list.onSystem, true);
        }
    }

    // selected_fonts_count = count;
    add_document_fonts_at_top(SP_ACTIVE_DOCUMENT);
    font_list_store->thaw_notify();
    init_default_styles();

    // To update the count of fonts in the label.
    // update_signal.emit ();
}

void FontLister::apply_collections(std::set <Glib::ustring>& selected_collections)
{
    // Get the master set of fonts present in all the selected collections.
    std::set <Glib::ustring> fonts;

    FontCollections *font_collections = Inkscape::FontCollections::get();

    for (auto const &col : selected_collections) {
        if (col.raw() == Inkscape::DOCUMENT_FONTS) {
            DocumentFonts* document_fonts = Inkscape::DocumentFonts::get();
            for (auto const &font : document_fonts->get_fonts()) {
                fonts.insert(font);
            }
        } else if (col.raw() == Inkscape::RECENTLY_USED_FONTS) {
            RecentlyUsedFonts *recently_used = Inkscape::RecentlyUsedFonts::get();
            for (auto const &font : recently_used->get_fonts()) {
                fonts.insert(font);
            }
        } else {
            for (auto const &font : font_collections->get_fonts(col)) {
                fonts.insert(std::move(font));
            }
        }
    }

    // Freeze the font list.
    font_list_store->freeze_notify();
    font_list_store->clear();

    if (fonts.empty()) {
        // Re-initialize the font list if
        // initialize_font_list();
        init_font_families();
        init_default_styles();
        add_document_fonts_at_top(SP_ACTIVE_DOCUMENT);
        return;
    }

    for (auto const &f : fonts) {
        auto row = *font_list_store->append();
        row[font_list.family] = f;

        // we don't set this now (too slow) but the style will be cached if the user
        // ever decides to use this font
        row[font_list.styles] = nullptr;

        // store the pango representation for generating the style
        row[font_list.pango_family] = pango_family_map[f];
        row[font_list.onSystem] = true;
    }

    add_document_fonts_at_top(SP_ACTIVE_DOCUMENT);
    font_list_store->thaw_notify();
    init_default_styles();

    // To update the count of fonts in the label.
    update_signal.emit();
}

// Ensures the style list for a particular family has been created.
void FontLister::ensureRowStyles(Gtk::TreeModel::iterator iter)
{
    auto &row = *iter;
    if (row.get_value(font_list.styles)) {
        return;
    }

    if (row[font_list.pango_family]) {
        row[font_list.styles] = std::make_shared<Styles>(FontFactory::get().GetUIStyles(row[font_list.pango_family]));
    } else {
        row[font_list.styles] = default_styles;
    }
}

Glib::ustring FontLister::get_font_family_markup(Gtk::TreeModel::const_iterator const &iter) const
{
    auto const &row = *iter;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    Glib::ustring family = row[font_list.family];
    bool onSystem        = row[font_list.onSystem];

    Glib::ustring family_escaped = Glib::Markup::escape_text( family );
    Glib::ustring markup;

    if (!onSystem) {
        markup = "<span font-weight='bold'>";

        // See if font-family is on system (separately for each family in font stack).
        std::vector<Glib::ustring> tokens = Glib::Regex::split_simple("\\s*,\\s*", family);

        for (auto const &token: tokens) {
            if (font_installed_on_system(token)) {
                markup += Glib::Markup::escape_text (token);
                markup += ", ";
            } else {
                markup += "<span strikethrough=\"true\" strikethrough_color=\"red\">";
                markup += Glib::Markup::escape_text (token);
                markup += "</span>";
                markup += ", ";
            }
        }

        // Remove extra comma and space from end.
        if (markup.size() >= 2) {
            markup.resize(markup.size() - 2);
        }
        markup += "</span>";

    } else {
        markup = family_escaped;
    }

    int show_sample = prefs->getInt("/tools/text/show_sample_in_list", 1);
    if (show_sample) {
        Glib::ustring sample = prefs->getString("/tools/text/font_sample");
        // we setup a small line height to avoid semi hidden fonts (one line height rendering overlap without padding)
#if PANGO_VERSION_CHECK(1,50,0)
        markup += "  <span foreground='gray' line-height='0.6' font-size='100%' font_family='";
#else
        markup += "  <span foreground='gray' font_family='";
#endif
        markup += family_escaped;
        markup += "'>";
        markup += sample;
        markup += "</span>";
    }

    // std::cout << "Markup: " << markup << std::endl;
    return markup;
}

// Example of how to use "foreach_iter"
// bool
// FontLister::print_document_font(Gtk::TreeModel::const_iterator const &iter)
// {
//   auto const &row = *iter;
//   if(!row[FontList.onSystem]) {
//       std::cout << " Not on system: " << row[FontList.family] << std::endl;
//       return false;
//   }
//   return true;
// }
// font_list_store->foreach_iter( sigc::mem_fun(*this, &FontLister::print_document_font ));

// Used to insert a font that was not in the document and not on the system into the font list.
void FontLister::insert_font_family(Glib::ustring const &new_family)
{
    auto styles = default_styles;

    // In case this is a fallback list, check if first font-family on system.
    std::vector<Glib::ustring> tokens = Glib::Regex::split_simple(",", new_family);
    if (!tokens.empty() && !tokens[0].empty()) {
        for (auto &row : font_list_store->children()) {
            auto row_styles = row.get_value(font_list.styles);

            if (row[font_list.onSystem] && familyNamesAreEqual(tokens[0], row[font_list.family])) {
                if (!row_styles) {
                    row_styles = std::make_shared<Styles>(FontFactory::get().GetUIStyles(row[font_list.pango_family]));
                }
                styles = row_styles;
                break;
            }
        }
    }

    auto row = *font_list_store->prepend();
    row[font_list.family] = new_family;
    row[font_list.styles] = styles;
    row[font_list.onSystem] = false;
    row[font_list.pango_family] = nullptr;

    current_family = new_family;
    current_family_row = 0;
    current_style = "Normal";

    emit_update();
}

int FontLister::add_document_fonts_at_top(SPDocument *document)
{
    if (!document) {
        return 0;
    }

    auto root = document->getRoot();
    if (!root) {
        return 0;
    }

    // Clear all old document font-family entries.
    {
        auto children = font_list_store->children();
        for (auto iter = children.begin(), end = children.end(); iter != end;) {
            if (!iter->get_value(font_list.onSystem)) {
                // std::cout << " Not on system: " << row[FontList.family] << std::endl;
                iter = font_list_store->erase(iter);
            } else {
                // std::cout << " First on system: " << row[FontList.family] << std::endl;
                break;
            }
        }
    }

    // Get "font-family"s and styles used in document.
    std::map<Glib::ustring, std::set<Glib::ustring>> font_data;
    update_font_data_recursive(*root, font_data);

    // Insert separator
    if (!font_data.empty()) {
        auto row = *font_list_store->prepend();
        row[font_list.family] = "#";
    }

    // Insert the document's font families into the store.
    for (auto const &[data_family, data_styleset] : font_data) {
        // Ensure the font family is non-empty, and get the part up to the first comma.
        auto const i = data_family.find_first_of(',');
        if (i == 0) {
            continue;
        }
        auto const fam = data_family.substr(0, i);

        // Return the system font matching the given family name.
        auto find_matching_system_font = [this] (Glib::ustring const &fam) -> Gtk::TreeIter<Gtk::TreeRow> {
            for (auto &row : font_list_store->children()) {
                if (row[font_list.onSystem] && familyNamesAreEqual(fam, row[font_list.family])) {
                    return row.get_iter();
                }
            }
            return {};
        };

        // Initialise an empty Styles for this font.
        Styles data_styles;

        // Populate with the styles of the matching system font, if any.
        if (auto const iter = find_matching_system_font(fam)) {
            auto const row = *iter;
            ensureRowStyles(iter);
            data_styles = *row.get_value(font_list.styles);
        }

        // Add additional styles from 'font-variation-settings'; these may not be included in the system font's styles.
        for (auto const &data_style : data_styleset) {
            // std::cout << "  Inserting: " << j << std::endl;

            bool exists = false;
            for (auto const &style : data_styles) {
                if (style.css_name.compare(data_style) == 0) {
                    exists = true;
                    break;
                }
            }

            if (!exists) {
                data_styles.emplace_back(data_style, data_style);
            }
        }

        auto row = *font_list_store->prepend();
        row[font_list.family] = data_family;
        row[font_list.styles] = std::make_shared<Styles>(std::move(data_styles));
        /* These are not needed as they are the default values.
        row.set_value(font_list.onSystem, false);    // false if document font
        row.set_value(font_list.pango_family, nullptr); // CHECK ME (set to pango_family if on system?)
        */
    }

    // For document fonts.
    auto document_fonts = Inkscape::DocumentFonts::get();
    document_fonts->update_document_fonts(font_data);
    auto recently_used = Inkscape::RecentlyUsedFonts::get();
    recently_used->prepend_to_list(current_family);

    return font_data.size();
}

void FontLister::update_font_list(SPDocument *document)
{
    auto root = document->getRoot();
    if (!root) {
        return;
    }

    font_list_store->freeze_notify();

    /* Find if current row is in document or system part of list */
    bool row_is_system = false;
    if (current_family_row > -1) {
        Gtk::TreePath path;
        path.push_back(current_family_row);
        Gtk::TreeModel::iterator iter = font_list_store->get_iter(path);
        if (iter) {
            row_is_system = (*iter)[font_list.onSystem];
            // std::cout << "  In:  row: " << current_family_row << "  " << (*iter)[FontList.family] << std::endl;
        }
    }

    int font_data_size = add_document_fonts_at_top(document);

    font_family_row_update(row_is_system ? font_data_size : 0);
    // std::cout << "  Out: row: " << current_family_row << "  " << current_family << std::endl;

    font_list_store->thaw_notify();
    emit_update();
}

void FontLister::update_font_data_recursive(SPObject &r, std::map<Glib::ustring, std::set<Glib::ustring>> &font_data)
{
    // Text nodes (i.e. the content of <text> or <tspan>) do not have their own style.
    if (r.getRepr()->type() == Inkscape::XML::NodeType::TEXT_NODE) {
        return;
    }

    PangoFontDescription* descr = ink_font_description_from_style(r.style);
    auto font_family_char = pango_font_description_get_family(descr);
    if (font_family_char) {
        Glib::ustring font_family(font_family_char);
        pango_font_description_unset_fields(descr, PANGO_FONT_MASK_FAMILY);

        auto font_style_char = pango_font_description_to_string(descr);
        Glib::ustring font_style(font_style_char);
        g_free(font_style_char);

        if (!font_family.empty() && !font_style.empty()) {
            font_data[font_family].insert(font_style);
        }
    } else {
        // We're starting from root and looking at all elements... we should probably white list text/containers.
        std::cerr << "FontLister::update_font_data_recursive: descr without font family! " << (r.getId()?r.getId():"null") << std::endl;
    }
    pango_font_description_free(descr);

    if (is<SPGroup>(&r)    ||
        is<SPAnchor>(&r)   ||
        is<SPRoot>(&r)     ||
        is<SPText>(&r)     ||
        is<SPTSpan>(&r)    ||
        is<SPTextPath>(&r) ||
        is<SPTRef>(&r)     ||
        is<SPFlowtext>(&r) ||
        is<SPFlowdiv>(&r)  ||
        is<SPFlowpara>(&r) ||
        is<SPFlowline>(&r))
    {
        for (auto &child: r.children) {
            update_font_data_recursive(child, font_data);
        }
    }
}

void FontLister::emit_update()
{
    if (block) return;

    block = true;
    update_signal.emit();
    block = false;
}

Glib::ustring FontLister::canonize_fontspec(Glib::ustring const &fontspec) const
{
    // Pass fontspec to and back from Pango to get a the fontspec in
    // canonical form.  -inkscape-font-specification relies on the
    // Pango constructed fontspec not changing form. If it does,
    // this is the place to fix it.
    PangoFontDescription *descr = pango_font_description_from_string(fontspec.c_str());
    gchar *canonized = pango_font_description_to_string(descr);
    Glib::ustring Canonized = canonized;
    g_free(canonized);
    pango_font_description_free(descr);

    // Pango canonized strings remove space after comma between family names. Put it back.
    // But don't add a space inside a 'font-variation-settings' declaration (this breaks Pango).
    size_t i = 0;
    while ((i = Canonized.find_first_of(",@", i)) != std::string::npos ) {
        if (Canonized[i] == '@') // Found start of 'font-variation-settings'.
            break;
        Canonized.replace(i, 1, ", ");
        i += 2;
    }

    return Canonized;
}

Glib::ustring FontLister::system_fontspec(Glib::ustring const &fontspec)
{
    // Find what Pango thinks is the closest match.
    Glib::ustring out = fontspec;

    PangoFontDescription *descr = pango_font_description_from_string(fontspec.c_str());
    auto res = FontFactory::get().Face(descr);
    if (res) {
        auto nFaceDesc = pango_font_describe(res->get_font());
        out = sp_font_description_get_family(nFaceDesc);
    }
    pango_font_description_free(descr);

    return out;
}

std::pair<Glib::ustring, Glib::ustring> FontLister::ui_from_fontspec(Glib::ustring const &fontspec) const
{
    PangoFontDescription *descr = pango_font_description_from_string(fontspec.c_str());
    const gchar *family = pango_font_description_get_family(descr);
    if (!family)
        family = "sans-serif";
    Glib::ustring Family = family;

    // PANGO BUG...
    //   A font spec of Delicious, 500 Italic should result in a family of 'Delicious'
    //   and a style of 'Medium Italic'. It results instead with: a family of
    //   'Delicious, 500' with a style of 'Medium Italic'. We chop of any weight numbers
    //   at the end of the family:  match ",[1-9]00^".
    Glib::RefPtr<Glib::Regex> weight = Glib::Regex::create(",[1-9]00$");
    Family = weight->replace(Family, 0, "", Glib::Regex::MatchFlags::PARTIAL);

    // Pango canonized strings remove space after comma between family names. Put it back.
    size_t i = 0;
    while ((i = Family.find(",", i)) != std::string::npos) {
        Family.replace(i, 1, ", ");
        i += 2;
    }

    pango_font_description_unset_fields(descr, PANGO_FONT_MASK_FAMILY);
    gchar *style = pango_font_description_to_string(descr);
    Glib::ustring Style = style;
    pango_font_description_free(descr);
    g_free(style);

    return std::make_pair(Family, Style);
}

/* Now we do a song and dance to find the correct row as the row corresponding
 * to the current_family may have changed. We can't simply search for the
 * family name in the list since it can occur twice, once in the document
 * font family part and once in the system font family part. Above we determined
 * which part it is in.
 */
void FontLister::font_family_row_update(int start)
{
    if (this->current_family_row > -1 && start > -1) {
        int length = this->font_list_store->children().size();
        for (int i = 0; i < length; ++i) {
            int row = i + start;
            if (row >= length)
                row -= length;
            Gtk::TreePath path;
            path.push_back(row);
            if (auto iter = font_list_store->get_iter(path)) {
                if (familyNamesAreEqual(current_family, (*iter)[font_list.family])) {
                    current_family_row = row;
                    break;
                }
            }
        }
    }
}

std::pair<Glib::ustring, Glib::ustring> FontLister::selection_update()
{
#ifdef DEBUG_FONT
    std::cout << "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << std::endl;
    std::cout << "FontLister::selection_update: entrance" << std::endl;
#endif
    // Get fontspec from a selection, preferences, or thin air.
    Glib::ustring fontspec;
    SPStyle query(SP_ACTIVE_DOCUMENT);

    // Directly from stored font specification.
    int result =
        sp_desktop_query_style(SP_ACTIVE_DESKTOP, &query, QUERY_STYLE_PROPERTY_FONT_SPECIFICATION);

    //std::cout << "  Attempting selected style" << std::endl;
    if (result != QUERY_STYLE_NOTHING && query.font_specification.set) {
        fontspec = query.font_specification.value();
        //std::cout << "   fontspec from query   :" << fontspec << ":" << std::endl;
    }

    // From style
    if (fontspec.empty()) {
        //std::cout << "  Attempting desktop style" << std::endl;
        int rfamily = sp_desktop_query_style(SP_ACTIVE_DESKTOP, &query, QUERY_STYLE_PROPERTY_FONTFAMILY);
        int rstyle = sp_desktop_query_style(SP_ACTIVE_DESKTOP, &query, QUERY_STYLE_PROPERTY_FONTSTYLE);

        // Must have text in selection
        if (rfamily != QUERY_STYLE_NOTHING && rstyle != QUERY_STYLE_NOTHING) {
            fontspec = fontspec_from_style(&query);
        }
        //std::cout << "   fontspec from style   :" << fontspec << ":" << std::endl;
    }

    // From preferences
    if (fontspec.empty()) {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        if (prefs->getBool("/tools/text/usecurrent")) {
            query.mergeCSS(sp_desktop_get_style(SP_ACTIVE_DESKTOP, true));
        } else {
            query.readFromPrefs("/tools/text");
        }
        fontspec = fontspec_from_style(&query);
    }

    // From thin air
    if (fontspec.empty()) {
        //std::cout << "  Attempting thin air" << std::endl;
        fontspec = current_family + ", " + current_style;
        //std::cout << "   fontspec from thin air   :" << fontspec << ":" << std::endl;
    }

    // Need to update font family row too
    // Consider the count of document fonts before setting the start point
    int font_data_size = add_document_fonts_at_top(SP_ACTIVE_DOCUMENT);
    font_family_row_update(font_data_size);

    std::pair<Glib::ustring, Glib::ustring> ui = ui_from_fontspec(fontspec);
    set_font_family(ui.first);
    set_font_style(ui.second);

#ifdef DEBUG_FONT
    std::cout << "   family_row:           :" << current_family_row << ":" << std::endl;
    std::cout << "   family:               :" << current_family << ":" << std::endl;
    std::cout << "   style:                :" << current_style << ":" << std::endl;
    std::cout << "FontLister::selection_update: exit" << std::endl;
    std::cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n" << std::endl;
#endif

    emit_update();

    return std::make_pair(current_family, current_style);
}


// Set fontspec. If check is false, best style match will not be done.
void FontLister::set_fontspec(Glib::ustring const &new_fontspec, bool /*check*/)
{
    auto const &[new_family, new_style] = ui_from_fontspec(new_fontspec);

#ifdef DEBUG_FONT
    std::cout << "FontLister::set_fontspec: family: " << new_family
              << "   style:" << new_style << std::endl;
#endif

    set_font_family(new_family, false, false);
    set_font_style(new_style, false);

    emit_update();
}


// TODO: use to determine font-selector best style
// TODO: create new function new_font_family(Gtk::TreeModel::iterator iter)
std::pair<Glib::ustring, Glib::ustring> FontLister::new_font_family(Glib::ustring const &new_family, bool /*check_style*/)
{
#ifdef DEBUG_FONT
    std::cout << "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << std::endl;
    std::cout << "FontLister::new_font_family: " << new_family << std::endl;
#endif

    // No need to do anything if new family is same as old family.
    if (familyNamesAreEqual(new_family, current_family)) {
#ifdef DEBUG_FONT
        std::cout << "FontLister::new_font_family: exit: no change in family." << std::endl;
        std::cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n" << std::endl;
#endif
        return std::make_pair(current_family, current_style);
    }

    // We need to do two things:
    // 1. Update style list for new family.
    // 2. Select best valid style match to old style.

    // For finding style list, use list of first family in font-family list.
    std::shared_ptr<Styles> styles;
    for (auto row : font_list_store->children()) {
        if (familyNamesAreEqual(new_family, row[font_list.family])) {
            auto row_styles = row.get_value(font_list.styles);
            if (!row_styles) {
                row_styles = std::make_shared<Styles>(FontFactory::get().GetUIStyles(row[font_list.pango_family]));
            }
            styles = std::move(row_styles);
            break;
        }
    }

    // Newly typed in font-family may not yet be in list... use default list.
    // TODO: if font-family is list, check if first family in list is on system
    // and set style accordingly.
    if (!styles) {
        styles = default_styles;
    }

    // Update style list.
    style_list_store->freeze_notify();
    style_list_store->clear();

    for (auto const &style : *styles) {
        auto row = *style_list_store->append();
        row[font_style_list.cssStyle] = style.css_name;
        row[font_style_list.displayStyle] = style.display_name;
    }

    style_list_store->thaw_notify();

    // Find best match to the style from the old font-family to the
    // styles available with the new font.
    // TODO: Maybe check if an exact match exists before using Pango.
    Glib::ustring best_style = get_best_style_match(new_family, current_style);

#ifdef DEBUG_FONT
    std::cout << "FontLister::new_font_family: exit: " << new_family << " " << best_style << std::endl;
    std::cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n" << std::endl;
#endif
    return std::make_pair(new_family, best_style);
}

void FontLister::set_dragging_family(const Glib::ustring &new_family)
{
    dragging_family = new_family;
}

std::pair<Glib::ustring, Glib::ustring> FontLister::set_font_family(Glib::ustring const &new_family, bool const check_style,
                                                                    bool emit)
{

#ifdef DEBUG_FONT
    std::cout << "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << std::endl;
    std::cout << "FontLister::set_font_family: " << new_family << std::endl;
#endif

    std::pair<Glib::ustring, Glib::ustring> ui = new_font_family(new_family, check_style);
    current_family = ui.first;
    current_style = ui.second;
    RecentlyUsedFonts *recently_used = Inkscape::RecentlyUsedFonts::get();
    recently_used->prepend_to_list(current_family);

#ifdef DEBUG_FONT
    std::cout << "   family_row:           :" << current_family_row << ":" << std::endl;
    std::cout << "   family:               :" << current_family << ":" << std::endl;
    std::cout << "   style:                :" << current_style << ":" << std::endl;
    std::cout << "FontLister::set_font_family: end" << std::endl;
    std::cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n" << std::endl;
#endif
    if (emit) {
        emit_update();
    }
    return ui;
}


std::pair<Glib::ustring, Glib::ustring> FontLister::set_font_family(int row, bool check_style, bool emit)
{

#ifdef DEBUG_FONT
    std::cout << "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << std::endl;
    std::cout << "FontLister::set_font_family( row ): " << row << std::endl;
#endif

    current_family_row = row;
    Gtk::TreePath path;
    path.push_back(row);
    Glib::ustring new_family = current_family;
    if (auto iter = font_list_store->get_iter(path)) {
        new_family = (*iter)[font_list.family];
    }

    std::pair<Glib::ustring, Glib::ustring> ui = set_font_family(new_family, check_style, emit);

#ifdef DEBUG_FONT
    std::cout << "FontLister::set_font_family( row ): end" << std::endl;
    std::cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n" << std::endl;
#endif
    return ui;
}


void FontLister::set_font_style(Glib::ustring new_style, bool emit)
{
// TODO: Validate input using Pango. If Pango doesn't recognize a style it will
// attach the "invalid" style to the font-family.

#ifdef DEBUG_FONT
    std::cout << "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << std::endl;
    std::cout << "FontLister:set_font_style: " << new_style << std::endl;
#endif

    current_style = std::move(new_style);

#ifdef DEBUG_FONT
    std::cout << "   family:                " << current_family << std::endl;
    std::cout << "   style:                 " << current_style << std::endl;
    std::cout << "FontLister::set_font_style: end" << std::endl;
    std::cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n" << std::endl;
#endif
    if (emit) {
        emit_update();
    }
}


// We do this ourselves as we can't rely on FontFactory.
void FontLister::fill_css(SPCSSAttr *css, Glib::ustring fontspec)
{
    if (fontspec.empty()) {
        fontspec = get_fontspec();
    }

    std::pair<Glib::ustring, Glib::ustring> ui = ui_from_fontspec(fontspec);

    Glib::ustring family = ui.first;


    // Font spec is single quoted... for the moment
    Glib::ustring fontspec_quoted(fontspec);
    css_quote(fontspec_quoted);
    sp_repr_css_set_property(css, "-inkscape-font-specification", fontspec_quoted.c_str());

    // Font families needs to be properly quoted in CSS (used unquoted in font-lister)
    css_font_family_quote(family);
    sp_repr_css_set_property(css, "font-family", family.c_str());

    PangoFontDescription *desc = pango_font_description_from_string(fontspec.c_str());
    PangoWeight weight = pango_font_description_get_weight(desc);
    switch (weight) {
        case PANGO_WEIGHT_THIN:
            sp_repr_css_set_property(css, "font-weight", "100");
            break;
        case PANGO_WEIGHT_ULTRALIGHT:
            sp_repr_css_set_property(css, "font-weight", "200");
            break;
        case PANGO_WEIGHT_LIGHT:
            sp_repr_css_set_property(css, "font-weight", "300");
            break;
        case PANGO_WEIGHT_SEMILIGHT:
            sp_repr_css_set_property(css, "font-weight", "350");
            break;
        case PANGO_WEIGHT_BOOK:
            sp_repr_css_set_property(css, "font-weight", "380");
            break;
        case PANGO_WEIGHT_NORMAL:
            sp_repr_css_set_property(css, "font-weight", "normal");
            break;
        case PANGO_WEIGHT_MEDIUM:
            sp_repr_css_set_property(css, "font-weight", "500");
            break;
        case PANGO_WEIGHT_SEMIBOLD:
            sp_repr_css_set_property(css, "font-weight", "600");
            break;
        case PANGO_WEIGHT_BOLD:
            sp_repr_css_set_property(css, "font-weight", "bold");
            break;
        case PANGO_WEIGHT_ULTRABOLD:
            sp_repr_css_set_property(css, "font-weight", "800");
            break;
        case PANGO_WEIGHT_HEAVY:
            sp_repr_css_set_property(css, "font-weight", "900");
            break;
        case PANGO_WEIGHT_ULTRAHEAVY:
            sp_repr_css_set_property(css, "font-weight", "1000");
            break;
    }

    PangoStyle style = pango_font_description_get_style(desc);
    switch (style) {
        case PANGO_STYLE_NORMAL:
            sp_repr_css_set_property(css, "font-style", "normal");
            break;
        case PANGO_STYLE_OBLIQUE:
            sp_repr_css_set_property(css, "font-style", "oblique");
            break;
        case PANGO_STYLE_ITALIC:
            sp_repr_css_set_property(css, "font-style", "italic");
            break;
    }

    PangoStretch stretch = pango_font_description_get_stretch(desc);
    switch (stretch) {
        case PANGO_STRETCH_ULTRA_CONDENSED:
            sp_repr_css_set_property(css, "font-stretch", "ultra-condensed");
            break;
        case PANGO_STRETCH_EXTRA_CONDENSED:
            sp_repr_css_set_property(css, "font-stretch", "extra-condensed");
            break;
        case PANGO_STRETCH_CONDENSED:
            sp_repr_css_set_property(css, "font-stretch", "condensed");
            break;
        case PANGO_STRETCH_SEMI_CONDENSED:
            sp_repr_css_set_property(css, "font-stretch", "semi-condensed");
            break;
        case PANGO_STRETCH_NORMAL:
            sp_repr_css_set_property(css, "font-stretch", "normal");
            break;
        case PANGO_STRETCH_SEMI_EXPANDED:
            sp_repr_css_set_property(css, "font-stretch", "semi-expanded");
            break;
        case PANGO_STRETCH_EXPANDED:
            sp_repr_css_set_property(css, "font-stretch", "expanded");
            break;
        case PANGO_STRETCH_EXTRA_EXPANDED:
            sp_repr_css_set_property(css, "font-stretch", "extra-expanded");
            break;
        case PANGO_STRETCH_ULTRA_EXPANDED:
            sp_repr_css_set_property(css, "font-stretch", "ultra-expanded");
            break;
    }

    PangoVariant variant = pango_font_description_get_variant(desc);
    switch (variant) {
        case PANGO_VARIANT_NORMAL:
            sp_repr_css_set_property(css, "font-variant", "normal");
            break;
        case PANGO_VARIANT_SMALL_CAPS:
            sp_repr_css_set_property(css, "font-variant", "small-caps");
            break;
    }

    // Convert Pango variations string to CSS format
    const char* str = pango_font_description_get_variations(desc);

    std::string variations;

    if (str) {

        std::vector<Glib::ustring> tokens = Glib::Regex::split_simple(",", str);

        Glib::RefPtr<Glib::Regex> regex = Glib::Regex::create("(\\w{4})=([-+]?\\d*\\.?\\d+([eE][-+]?\\d+)?)");
        Glib::MatchInfo matchInfo;
        for (auto const &token: tokens) {
            regex->match(token, matchInfo);
            if (matchInfo.matches()) {
                variations += "'";
                variations += matchInfo.fetch(1).raw();
                variations += "' ";
                variations += matchInfo.fetch(2).raw();
                variations += ", ";
            }
        }
        if (variations.length() >= 2) { // Remove last comma/space
            variations.pop_back();
            variations.pop_back();
        }
    }

    if (!variations.empty()) {
        sp_repr_css_set_property(css, "font-variation-settings", variations.c_str());
    } else {
        sp_repr_css_unset_property(css,  "font-variation-settings" );
    }
    pango_font_description_free(desc);
}

Glib::ustring FontLister::fontspec_from_style(SPStyle *style) const
{
    PangoFontDescription* descr = ink_font_description_from_style( style );
    Glib::ustring fontspec = pango_font_description_to_string( descr );
    pango_font_description_free(descr);

    //std::cout << "FontLister:fontspec_from_style: " << fontspec << std::endl;

    return fontspec;
}

Gtk::TreeModel::Row FontLister::get_row_for_font(Glib::ustring const &family)
{
    for (auto const &row : font_list_store->children()) {
        if (familyNamesAreEqual(family, row[font_list.family])) {
            return row;
        }
    }

    throw Exception::FAMILY_NOT_FOUND;
}

Gtk::TreePath FontLister::get_path_for_font(Glib::ustring const &family)
{
    return font_list_store->get_path(get_row_for_font(family).get_iter());
}

bool FontLister::is_path_for_font(Gtk::TreePath path, Glib::ustring family)
{
    if (auto iter = font_list_store->get_iter(path)) {
        return familyNamesAreEqual(family, (*iter)[font_list.family]);
    }

    return false;
}

Gtk::TreeModel::Row FontLister::get_row_for_style(Glib::ustring const &style)
{
    for (auto const &row : font_list_store->children()) {
        if (familyNamesAreEqual(style, row[font_style_list.cssStyle])) {
            return row;
        }
    }

    throw Exception::STYLE_NOT_FOUND;
}

static int compute_distance(PangoFontDescription const *a, PangoFontDescription const *b)
{
    // Weight: multiples of 100
    gint distance = abs(pango_font_description_get_weight(a) -
                        pango_font_description_get_weight(b));

    distance += 10000 * abs(pango_font_description_get_stretch(a) -
                            pango_font_description_get_stretch(b));

    PangoStyle style_a = pango_font_description_get_style(a);
    PangoStyle style_b = pango_font_description_get_style(b);
    if (style_a != style_b) {
        if ((style_a == PANGO_STYLE_OBLIQUE && style_b == PANGO_STYLE_ITALIC) ||
            (style_b == PANGO_STYLE_OBLIQUE && style_a == PANGO_STYLE_ITALIC)) {
            distance += 1000; // Oblique and italic are almost the same
        } else {
            distance += 100000; // Normal vs oblique/italic, not so similar
        }
    }

    // Normal vs small-caps
    distance += 1000000 * abs(pango_font_description_get_variant(a) -
                              pango_font_description_get_variant(b));
    return distance;
}

// This is inspired by pango_font_description_better_match, but that routine
// always returns false if variant or stretch are different. This means, for
// example, that PT Sans Narrow with style Bold Condensed is never matched
// to another font-family with Bold style.
gboolean font_description_better_match(PangoFontDescription *target, PangoFontDescription *old_desc, PangoFontDescription *new_desc)
{
    if (old_desc == nullptr)
        return true;
    if (new_desc == nullptr)
        return false;

    int old_distance = compute_distance(target, old_desc);
    int new_distance = compute_distance(target, new_desc);
    //std::cout << "font_description_better_match: old: " << old_distance << std::endl;
    //std::cout << "                               new: " << new_distance << std::endl;

    return (new_distance < old_distance);
}

// void
// font_description_dump( PangoFontDescription* target ) {
//   std::cout << "  Font:      " << pango_font_description_to_string( target ) << std::endl;
//   std::cout << "    style:   " << pango_font_description_get_style(   target ) << std::endl;
//   std::cout << "    weight:  " << pango_font_description_get_weight(  target ) << std::endl;
//   std::cout << "    variant: " << pango_font_description_get_variant( target ) << std::endl;
//   std::cout << "    stretch: " << pango_font_description_get_stretch( target ) << std::endl;
//   std::cout << "    gravity: " << pango_font_description_get_gravity( target ) << std::endl;
// }

/* Returns style string */
// TODO: Remove or turn into function to be used by new_font_family.
Glib::ustring FontLister::get_best_style_match(Glib::ustring const &family, Glib::ustring const &target_style)
{

#ifdef DEBUG_FONT
    std::cout << "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << std::endl;
    std::cout << "FontLister::get_best_style_match: " << family << " : " << target_style << std::endl;
#endif

    Glib::ustring fontspec = family + ", " + target_style;

    Gtk::TreeModel::Row row;
    try {
        row = get_row_for_font(family);
    } catch (Exception) {
        std::cerr << "FontLister::get_best_style_match(): can't find family: " << family.raw() << std::endl;
        return target_style;
    }

    PangoFontDescription *target = pango_font_description_from_string(fontspec.c_str());
    PangoFontDescription *best = nullptr;

    //font_description_dump( target );

    auto styles = default_styles;
    if (row[font_list.onSystem] && !row.get_value(font_list.styles)) {
        row[font_list.styles] = std::make_shared<Styles>(FontFactory::get().GetUIStyles(row[font_list.pango_family]));
        styles = row[font_list.styles];
    }

    for (auto const &style : *styles) {
        Glib::ustring fontspec = family + ", " + style.css_name;
        PangoFontDescription *candidate = pango_font_description_from_string(fontspec.c_str());
        //font_description_dump( candidate );
        //std::cout << "           " << font_description_better_match( target, best, candidate ) << std::endl;
        if (font_description_better_match(target, best, candidate)) {
            pango_font_description_free(best);
            best = candidate;
            //std::cout << "  ... better: " << std::endl;
        } else {
            pango_font_description_free(candidate);
            //std::cout << "  ... not better: " << std::endl;
        }
    }

    Glib::ustring best_style = target_style;
    if (best) {
        pango_font_description_unset_fields(best, PANGO_FONT_MASK_FAMILY);
        best_style = pango_font_description_to_string(best);
    }

    if (target)
        pango_font_description_free(target);
    if (best)
        pango_font_description_free(best);


#ifdef DEBUG_FONT
    std::cout << "  Returning: " << best_style << std::endl;
    std::cout << "FontLister::get_best_style_match: exit" << std::endl;
    std::cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n" << std::endl;
#endif
    return best_style;
}

} // namespace Inkscape

// Helper functions

// Separator function (if true, a separator will be drawn)
bool font_lister_separator_func(Glib::RefPtr<Gtk::TreeModel> const &/*model*/,
                                Gtk::TreeModel::const_iterator const &iter)
{

    // Of what use is 'model', can we avoid using font_lister?
    Inkscape::FontLister* font_lister = Inkscape::FontLister::get_instance();
    auto row = *iter;
    Glib::ustring entry = row[font_lister->font_list.family];
    return entry == "#";
}

// do nothing on load initialy
void font_lister_cell_data_func(Gtk::CellRenderer * /*renderer*/,
                                Gtk::TreeModel::const_iterator const & /*iter*/)
{
}

// Draw system fonts in dark blue, missing fonts with red strikeout.
// Used by both FontSelector and Text toolbar.
void font_lister_cell_data_func_markup(Gtk::CellRenderer * const renderer,
                                       Gtk::TreeModel::const_iterator const &iter)
{
    Inkscape::FontLister* font_lister = Inkscape::FontLister::get_instance();
    Glib::ustring markup = font_lister->get_font_family_markup(iter);
    renderer->set_property("markup", markup);
}

// Needed until Text toolbar updated
void font_lister_cell_data_func2(Gtk::CellRenderer &cell,
                                 Gtk::TreeModel::const_iterator const &iter,
                                 bool with_markup)
{
    auto font_lister = Inkscape::FontLister::get_instance();
    Glib::ustring family = (*iter)[font_lister->font_list.family];
    bool onSystem = (*iter)[font_lister->font_list.onSystem];
    auto family_escaped = g_markup_escape_text(family.c_str(), -1);
    auto prefs = Inkscape::Preferences::get();
    bool dark = prefs->getBool("/theme/darkTheme", false);
    Glib::ustring markup;

    if (!onSystem) {
        markup = "<span font-weight='bold'>";

        /* See if font-family on system */
        std::vector<Glib::ustring> tokens = Glib::Regex::split_simple("\\s*,\\s*", family);
        for (auto const &token : tokens) {
            bool found = Inkscape::FontLister::get_instance()->font_installed_on_system(token);

            if (found) {
                markup += g_markup_escape_text(token.c_str(), -1);
                markup += ", ";
            } else {
                if (dark) {
                    markup += "<span strikethrough='true' strikethrough_color='salmon'>";
                } else {
                    markup += "<span strikethrough='true' strikethrough_color='red'>";
                }
                markup += g_markup_escape_text(token.c_str(), -1);
                markup += "</span>";
                markup += ", ";
            }
        }
        // Remove extra comma and space from end.
        if (markup.size() >= 2) {
            markup.resize(markup.size() - 2);
        }
        markup += "</span>";
        // std::cout << markup << std::endl;
    } else {
        markup = family_escaped;
    }

    int show_sample = prefs->getInt("/tools/text/show_sample_in_list", 1);
    if (show_sample) {
        Glib::ustring sample = prefs->getString("/tools/text/font_sample");
        gchar* sample_escaped = g_markup_escape_text(sample.data(), -1);
        if (with_markup) {
            markup += " <span alpha='55%";
#if PANGO_VERSION_CHECK(1,50,0)
            markup += "' font-size='100%' line-height='0.6' font_family='";
#else
            markup += "' font_family='";
#endif
            markup += family_escaped;
        } else {
            markup += " <span alpha='1";
        }
        markup += "'>";
        markup += sample_escaped;
        markup += "</span>";
        g_free(sample_escaped);
    }

    cell.set_property("markup", markup);
    g_free(family_escaped);
}

// Draw Face name with face style.
void font_lister_style_cell_data_func(Gtk::CellRenderer *const renderer,
                                      Gtk::TreeModel::const_iterator const &iter)
{
    Inkscape::FontLister* font_lister = Inkscape::FontLister::get_instance();
    auto const &row = *iter;

    Glib::ustring family = font_lister->get_font_family();
    Glib::ustring style  = row[font_lister->font_style_list.cssStyle];

    Glib::ustring style_escaped  = Glib::Markup::escape_text( style );
    Glib::ustring font_desc = family + ", " + style;
    Glib::ustring markup;

    markup = "<span font='" + font_desc + "'>" + style_escaped + "</span>";
    // std::cout << "  markup: " << markup.raw() << std::endl;

    renderer->set_property("markup", markup);
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8 :
