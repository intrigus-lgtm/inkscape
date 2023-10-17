// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * SVG Fonts dialog - implementation.
 */
/* Authors:
 *   Felipe C. da S. Sanches <juca@members.fsf.org>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 2008 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "svg-fonts-dialog.h"

#include <algorithm>
#include <exception>
#include <iomanip>
#include <sstream>
#include <utility>
#include <glibmm/i18n.h>
#include <glibmm/markup.h>
#include <glibmm/stringutils.h>
#include <giomm/themedicon.h>
#include <gtkmm/expander.h>
#include <gtkmm/image.h>
#include <gtkmm/liststore.h>
#include <gtkmm/notebook.h>
#include <gtkmm/scale.h>
#include <gtkmm/snapshot.h>
#include <gtkmm/togglebutton.h>
#include <sigc++/functors/mem_fun.h>

#include "desktop.h"
#include "document.h"
#include "document-undo.h"
#include "layer-manager.h"
#include "message-stack.h"
#include "selection.h"
#include "display/nr-svgfonts.h"
#include "object/sp-defs.h"
#include "object/sp-font-face.h"
#include "object/sp-font.h"
#include "object/sp-glyph-kerning.h"
#include "object/sp-glyph.h"
#include "object/sp-guide.h"
#include "object/sp-missing-glyph.h"
#include "object/sp-path.h"
#include "svg/svg.h"
#include "ui/column-menu-builder.h"
#include "ui/pack.h"
#include "ui/util.h"
#include "ui/widget/popover-menu.h"
#include "ui/widget/popover-menu-item.h"
#include "util/units.h"
#include "xml/repr.h"

SvgFontDrawingArea::SvgFontDrawingArea()
{
    set_name("SVGFontDrawingArea");
    set_draw_func(sigc::mem_fun(*this, &SvgFontDrawingArea::draw_func));
}

void SvgFontDrawingArea::set_svgfont(SvgFont* svgfont){
    _svgfont = svgfont;
}

void SvgFontDrawingArea::set_text(Glib::ustring text){
    _text = std::move(text);
    redraw();
}

void SvgFontDrawingArea::set_size(int x, int y){
    _x = x;
    _y = y;
    set_size_request(_x, _y);
}

void SvgFontDrawingArea::redraw(){
    queue_draw();
}

void SvgFontDrawingArea::draw_func(Cairo::RefPtr<Cairo::Context> const &cr,
                                   int /*width*/, int /*height*/)
{
  if (!_svgfont) return;

    cr->set_font_face( Cairo::RefPtr<Cairo::FontFace>(new Cairo::FontFace(_svgfont->get_font_face(), false /* does not have reference */)) );
    cr->set_font_size (_y-20);
    cr->move_to (10, 10);
    auto const fg = get_color();
    cr->set_source_rgb(fg.get_red(), fg.get_green(), fg.get_blue());
    // crash on macos: https://gitlab.com/inkscape/inkscape/-/issues/266
    try {
        cr->show_text(_text.c_str());
    } catch (std::exception const &ex) {
        g_warning("Error drawing custom SVG font text: %s", ex.what());
    }
}

namespace Inkscape::UI::Dialog {

void SvgGlyphRenderer::snapshot_vfunc(
    Glib::RefPtr<Gtk::Snapshot> const &snapshot, Gtk::Widget& widget,
    const Gdk::Rectangle& background_area, const Gdk::Rectangle& cell_area, Gtk::CellRendererState flags) {

    if (!_font || !_tree) return;

    auto const cr = snapshot->append_cairo(background_area);

    cr->set_font_face(Cairo::RefPtr<Cairo::FontFace>(new Cairo::FontFace(_font->get_font_face(), false /* does not have reference */)));
    cr->set_font_size(_font_size);
    Glib::ustring glyph = _property_glyph.get_value();
    Cairo::TextExtents ext;
    cr->get_text_extents(glyph, ext);
    cr->move_to(cell_area.get_x() + (_width - ext.width) / 2, cell_area.get_y() + 1);

    auto const selected = (flags & Gtk::CellRendererState::SELECTED) != Gtk::CellRendererState{};
    auto const css_class = selected ? "theme_selected_bg_color" : "";
    auto const fg = get_color_with_class(*_tree, css_class);
    cr->set_source_rgb(fg.get_red(), fg.get_green(), fg.get_blue());

    // crash on macos: https://gitlab.com/inkscape/inkscape/-/issues/266
    try {
        cr->show_text(glyph);
    } catch (std::exception const &ex) {
        g_warning("Error drawing custom SVG font glyphs: %s", ex.what());
    }
}

bool SvgGlyphRenderer::activate_vfunc(
    Glib::RefPtr<Gdk::Event const> const &event,
    Gtk::Widget &widget, Glib::ustring const &path, Gdk::Rectangle const &background_area,
    const Gdk::Rectangle& cell_area, Gtk::CellRendererState flags) {

    Glib::ustring glyph = _property_glyph.get_value();
    _signal_clicked.emit(event, glyph);
    return false;
}

SvgFontsDialog::AttrEntry::AttrEntry(SvgFontsDialog* d, gchar* lbl, Glib::ustring tooltip, const SPAttr attr)
{
    this->dialog = d;
    this->attr = attr;
    entry.set_tooltip_text(tooltip);
    _label = Gtk::make_managed<Gtk::Label>(lbl);
    _label->set_visible(true);
    _label->set_halign(Gtk::Align::START);
    entry.signal_changed().connect(sigc::mem_fun(*this, &SvgFontsDialog::AttrEntry::on_attr_changed));
}

void SvgFontsDialog::AttrEntry::set_text(const char* t){
    if (!t) return;
    entry.set_text(t);
}

// 'font-family' has a problem as it is also a presentation attribute for <text>
void SvgFontsDialog::AttrEntry::on_attr_changed(){
    if (dialog->_update.pending()) return;

    SPObject* o = nullptr;
    for (auto& node: dialog->get_selected_spfont()->children) {
        switch(this->attr){
            case SPAttr::FONT_FAMILY:
                if (is<SPFontFace>(&node)){
                    o = &node;
                    continue;
                }
                break;
            default:
                o = nullptr;
        }
    }

    const gchar* name = (const gchar*)sp_attribute_name(this->attr);
    if(name && o) {
        o->setAttribute((const gchar*) name, this->entry.get_text());
        o->parent->requestModified(SP_OBJECT_MODIFIED_FLAG);

        Glib::ustring undokey = "svgfonts:";
        undokey += name;
        DocumentUndo::maybeDone(o->document, undokey.c_str(), _("Set SVG Font attribute"), "");
    }

}

SvgFontsDialog::AttrSpin::AttrSpin(SvgFontsDialog* d, gchar* lbl, Glib::ustring tooltip, const SPAttr attr)
{
    this->dialog = d;
    this->attr = attr;
    spin.set_tooltip_text(tooltip);
    spin.set_visible(true);
    _label = Gtk::make_managed<Gtk::Label>(lbl);
    _label->set_visible(true);
    _label->set_halign(Gtk::Align::START);
    spin.set_range(0, 4096);
    spin.set_increments(10, 0);
    spin.signal_value_changed().connect(sigc::mem_fun(*this, &SvgFontsDialog::AttrSpin::on_attr_changed));
}

void SvgFontsDialog::AttrSpin::set_range(double low, double high){
    spin.set_range(low, high);
}

void SvgFontsDialog::AttrSpin::set_value(double v){
    spin.set_value(v);
}

void SvgFontsDialog::AttrSpin::on_attr_changed(){
    if (dialog->_update.pending()) return;

    SPObject* o = nullptr;
    switch (this->attr) {

        // <font> attributes
        case SPAttr::HORIZ_ORIGIN_X:
        case SPAttr::HORIZ_ORIGIN_Y:
        case SPAttr::HORIZ_ADV_X:
        case SPAttr::VERT_ORIGIN_X:
        case SPAttr::VERT_ORIGIN_Y:
        case SPAttr::VERT_ADV_Y:
            o = this->dialog->get_selected_spfont();
            break;

        // <font-face> attributes
        case SPAttr::UNITS_PER_EM:
        case SPAttr::ASCENT:
        case SPAttr::DESCENT:
        case SPAttr::CAP_HEIGHT:
        case SPAttr::X_HEIGHT:
            for (auto& node: dialog->get_selected_spfont()->children){
                if (is<SPFontFace>(&node)){
                    o = &node;
                    continue;
                }
            }
            break;

        default:
            o = nullptr;
    }

    const gchar* name = (const gchar*)sp_attribute_name(this->attr);
    if(name && o) {
        std::ostringstream temp;
        temp << this->spin.get_value();
        o->setAttribute(name, temp.str());
        o->parent->requestModified(SP_OBJECT_MODIFIED_FLAG);

        Glib::ustring undokey = "svgfonts:";
        undokey += name;
        DocumentUndo::maybeDone(o->document, undokey.c_str(), _("Set SVG Font attribute"), "");
    }

}

Gtk::Box* SvgFontsDialog::AttrCombo(gchar* lbl, const SPAttr /*attr*/){
    auto const hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
    hbox->append(*Gtk::make_managed<Gtk::Label>(lbl));
    hbox->append(*Gtk::make_managed<Gtk::ComboBox>());
    return hbox;
}

GlyphMenuButton::GlyphMenuButton()
    : _menu{std::make_unique<UI::Widget::PopoverMenu>(*this, Gtk::PositionType::BOTTOM)}
{
    _label.set_width_chars(2);

    auto const arrow = Gtk::make_managed<Gtk::Image>(Gio::ThemedIcon::create("pan-down-symbolic"));
    arrow->add_css_class("arrow");

    auto const box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
    box->append(_label);
    box->append(*arrow);
    set_child(*box);

    set_popover(*_menu);
}

GlyphMenuButton::~GlyphMenuButton() = default;

void GlyphMenuButton::update(SPFont const * const spfont)
{
    set_sensitive(false);
    _label.set_label({});
    _menu->delete_all();

    if (!spfont) return;

    auto const &font_nodes = spfont->children;
    if (font_nodes.empty()) return;

    // TODO: GTK4: probably nicer to use GtkGridView.
    Inkscape::UI::ColumnMenuBuilder builder{*_menu, 4};
    Glib::ustring active_label;

    for (auto const &node: font_nodes) {
        if (!is<SPGlyph>(&node)) continue;

        Glib::ustring text = static_cast<SPGlyph const *>(&node)->unicode;
        if (text.empty()) continue;

        builder.add_item(text, {}, {}, true, false,
                         [=, this]{ _label.set_label(text); });
        if (active_label.empty()) active_label = std::move(text);
    }

    set_sensitive(true);
    _label.set_label(active_label);
}

Glib::ustring GlyphMenuButton::get_active_text() const
{
    return _label.get_label();
}

void SvgFontsDialog::on_kerning_value_changed(){
    if (!get_selected_kerning_pair()) {
        return;
    }

    //TODO: I am unsure whether this is the correct way of calling SPDocumentUndo::maybe_done
    Glib::ustring undokey = "svgfonts:hkern:k:";
    undokey += this->kerning_pair->u1->attribute_string();
    undokey += ":";
    undokey += this->kerning_pair->u2->attribute_string();

    //slider values increase from right to left so that they match the kerning pair preview

    //XML Tree being directly used here while it shouldn't be.
    this->kerning_pair->setAttribute("k", Glib::Ascii::dtostr(get_selected_spfont()->horiz_adv_x - kerning_slider->get_value()));
    DocumentUndo::maybeDone(getDocument(), undokey.c_str(), _("Adjust kerning value"), "");

    //populate_kerning_pairs_box();
    kerning_preview.redraw();
    _font_da.redraw();
}

void SvgFontsDialog::sort_glyphs(SPFont* font) {
    if (!font) return;

    {
        auto scoped(_update.block());
        font->sort_glyphs();
    }
    update_glyphs();
}

// return U+<code> ... string
Glib::ustring create_unicode_name(const Glib::ustring& unicode, int max_chars) {
    std::ostringstream ost;
    if (unicode.empty()) {
        ost << "-";
    }
    else {
        auto it = unicode.begin();
        for (int i = 0; i < max_chars && it != unicode.end(); ++i) {
            if (i > 0) {
                ost << " ";
            }
            unsigned int code = *it++;
            ost << "U+" << std::hex << std::uppercase << std::setw(6) << std::setfill('0') << code;
        }
        if (it != unicode.end()) {
            ost << "..."; // there's more, but we skip them
        }
    }
    return ost.str();
}

// synthetic name consists for unicode hex numbers derived from glyph's "unicode" attribute
Glib::ustring get_glyph_synthetic_name(const SPGlyph& glyph) {
    auto unicode_name = create_unicode_name(glyph.unicode, 3);
    // U+<code> plus character
    return unicode_name + " " + glyph.unicode;
}

// full name consists of user-defined name combined with synthetic one
Glib::ustring get_glyph_full_name(const SPGlyph& glyph) {
    auto name = get_glyph_synthetic_name(glyph);
    if (!glyph.glyph_name.empty()) {
        // unicode name first, followed by user name - for sorting layers
        return name + " " + glyph.glyph_name;
    }
    else {
        return name;
    }
}

// look for a layer by its label; looking only in direct sublayers of  'root_layer'
SPItem* find_layer(SPDesktop* desktop, SPObject* root_layer, const Glib::ustring& name) {
    if (!desktop) return nullptr;

    const auto& layers = desktop->layerManager();
    auto root = root_layer == nullptr ? layers.currentRoot() : root_layer;
    if (!root) return nullptr;

    // check only direct child layers
    auto it = std::find_if(root->children.begin(), root->children.end(), [&](SPObject& obj) {
        return layers.isLayer(&obj) && obj.label() && strcmp(obj.label(), name.c_str()) == 0;
    });
    if (it != root->children.end()) {
        return static_cast<SPItem*>(&*it);
    }

    return nullptr; // not found
}

std::vector<SPGroup*> get_direct_sublayers(SPObject* layer) {
    std::vector<SPGroup*> layers;
    if (!layer) return layers;

    for (auto&& item : layer->children) {
        if (auto l = LayerManager::asLayer(&item)) {
            layers.push_back(l);
        }
    }

    return layers;
}

void rename_glyph_layer(SPDesktop* desktop, SPItem* layer, const Glib::ustring& font, const Glib::ustring& name) {
    if (!desktop || !layer || font.empty() || name.empty()) return;

    auto parent_layer = find_layer(desktop, desktop->layerManager().currentRoot(), font);
    if (!parent_layer) return;

    // before renaming the layer find new place to move it into to keep sorted order intact
    auto glyph_layers = get_direct_sublayers(parent_layer);

    auto it = std::lower_bound(glyph_layers.rbegin(), glyph_layers.rend(), name, [&](auto&& layer, const Glib::ustring n) {
        auto label = layer->label();
        if (!label) return false;

        Glib::ustring temp(label);
        return std::lexicographical_compare(temp.begin(), temp.end(), n.begin(), n.end());
    });
    SPObject* after = nullptr;
    if (it != glyph_layers.rend()) {
        after = *it;
    }

    // SPItem changeOrder messes up inserting into first position, so dropping to Node level
    if (layer != after && parent_layer->getRepr() && layer->getRepr()) {
        parent_layer->getRepr()->changeOrder(layer->getRepr(), after ? after->getRepr() : nullptr);
    }

    desktop->layerManager().renameLayer(layer, name.c_str(), false);
}

SPItem* get_layer_for_glyph(SPDesktop* desktop, const Glib::ustring& font, const Glib::ustring& name) {
    if (!desktop || name.empty() || font.empty()) return nullptr;

    auto parent_layer = find_layer(desktop, desktop->layerManager().currentRoot(), font);
    if (!parent_layer) return nullptr;

    return find_layer(desktop, parent_layer, name);
}

SPItem* get_or_create_layer_for_glyph(SPDesktop* desktop, const Glib::ustring& font, const Glib::ustring& name) {
    if (!desktop || name.empty() || font.empty()) return nullptr;

    auto& layers = desktop->layerManager();
    auto parent_layer = find_layer(desktop, layers.currentRoot(), font);
    if (!parent_layer) {
        // create a new layer for a font
        parent_layer = static_cast<SPItem*>(create_layer(layers.currentRoot(), layers.currentRoot(), Inkscape::LayerRelativePosition::LPOS_CHILD));
        if (!parent_layer) return nullptr;

        layers.renameLayer(parent_layer, font.c_str(), false);
    }

    if (auto layer = find_layer(desktop, parent_layer, name)) {
        return layer;
    }

    // find the right place for a new layer, so they appear sorted
    auto glyph_layers = get_direct_sublayers(parent_layer);
    // auto& glyph_layers = parent_layer->children;
    auto it = std::lower_bound(glyph_layers.rbegin(), glyph_layers.rend(), name, [&](auto&& layer, const Glib::ustring n) {
        auto label = layer->label();
        if (!label) return false;

        Glib::ustring temp(label);
        return std::lexicographical_compare(temp.begin(), temp.end(), n.begin(), n.end());
    });
    SPObject* insert = parent_layer;
    Inkscape::LayerRelativePosition pos = Inkscape::LayerRelativePosition::LPOS_ABOVE;
    if (it != glyph_layers.rend()) {
        insert = *it;
    }
    else {
        // auto first = std::find_if(glyph_layers.begin(), glyph_layers.end(), [&](auto&& obj) {
            // return layers.isLayer(&obj);
        // });
        if (!glyph_layers.empty()) {
            insert = glyph_layers.front();
            pos = Inkscape::LayerRelativePosition::LPOS_BELOW;
        }
    }

    // create a new layer for a glyph
    auto layer = create_layer(parent_layer, insert, pos);
    if (!layer) return nullptr;

    layers.renameLayer(layer, name.c_str(), false);

    DocumentUndo::done(desktop->getDocument(), _("Add layer"), "");
    return cast<SPItem>(layer);
}

void SvgFontsDialog::update_sensitiveness(){
    if (get_selected_spfont()){
        _grid.set_sensitive(true);
        glyphs_vbox.set_sensitive(true);
        kerning_vbox.set_sensitive(true);
    } else {
        _grid.set_sensitive(false);
        glyphs_vbox.set_sensitive(false);
        kerning_vbox.set_sensitive(false);
    }
}

Glib::ustring get_font_label(SPFont* font) {
    if (!font) return Glib::ustring();

    const gchar* label = font->label();
    const gchar* id = font->getId();
    return Glib::ustring(label ? label : (id ? id : "font"));
};

/** Add all fonts in the getDocument() to the combobox.
 * This function is called when new document is selected as well as when SVG "definition" section changes.
 * Try to detect if font(s) have actually been modified to eliminate some expensive refreshes.
 */
void SvgFontsDialog::update_fonts(bool document_replaced)
{
    std::vector<SPObject*> fonts;
    if (auto document = getDocument()) {
        fonts = document->getResourceList( "font" );
    }

    auto children = _model->children();
    bool equal = false;
    bool selected_font = false;

    // compare model and resources
    if (!document_replaced && children.size() == fonts.size()) {
        equal = true; // assume they are the same
        auto it = fonts.begin();
        for (auto&& node : children) {
            SPFont* sp_font = node[_columns.spfont];
            if (it == fonts.end() || *it != sp_font) {
                // difference detected; update model
                equal = false;
                break;
            }
            ++it;
        }
    }

    // rebuild model if list of fonts is different
    if (!equal) {
        _model->clear();
        for (auto font : fonts) {
            Gtk::TreeModel::Row row = *_model->append();
            auto f = cast<SPFont>(font);
            row[_columns.spfont] = f;
            row[_columns.svgfont] = new SvgFont(f);
            row[_columns.label] = get_font_label(f);
        }
        if (!fonts.empty()) {
            // select a font, this dialog is disabled without a font
            auto selection = _FontsList.get_selection();
            if (selection) {
                selection->select(_model->get_iter("0"));
                selected_font = true;
            }
        }
    }
    else {
        // list of fonts is the same, but attributes may have changed
        auto it = fonts.begin();
        for (auto&& node : children) {
            if (auto font = cast<SPFont>(*it++)) {
                node[_columns.label] = get_font_label(font);
            }
        }
    }

    if (document_replaced && !selected_font) {
        // replace fonts, they are stale
        font_selected(nullptr, nullptr);
    }
    else {
        update_sensitiveness();
    }
}

void SvgFontsDialog::on_preview_text_changed(){
    _font_da.set_text(_preview_entry.get_text());
}

void SvgFontsDialog::on_kerning_pair_selection_changed(){
    SPGlyphKerning* kern = get_selected_kerning_pair();
    if (!kern) {
        kerning_preview.set_text("");
        return;
    }
    Glib::ustring str;
    str += kern->u1->sample_glyph();
    str += kern->u2->sample_glyph();

    kerning_preview.set_text(str);
    this->kerning_pair = kern;

    //slider values increase from right to left so that they match the kerning pair preview
    kerning_slider->set_value(get_selected_spfont()->horiz_adv_x - kern->k);
}

void SvgFontsDialog::update_global_settings_tab(){
    SPFont* font = get_selected_spfont();
    if (!font) {
        //TODO: perhaps reset all values when there's no font
        _familyname_entry->set_text("");
        return;
    }

    _horiz_adv_x_spin->set_value(font->horiz_adv_x);
    _horiz_origin_x_spin->set_value(font->horiz_origin_x);
    _horiz_origin_y_spin->set_value(font->horiz_origin_y);

    for (auto& obj: font->children) {
        if (is<SPFontFace>(&obj)){
            _familyname_entry->set_text((cast<SPFontFace>(&obj))->font_family);
            _units_per_em_spin->set_value((cast<SPFontFace>(&obj))->units_per_em);
            _ascent_spin->set_value((cast<SPFontFace>(&obj))->ascent);
            _descent_spin->set_value((cast<SPFontFace>(&obj))->descent);
            _x_height_spin->set_value((cast<SPFontFace>(&obj))->x_height);
            _cap_height_spin->set_value((cast<SPFontFace>(&obj))->cap_height);
        }
    }
}

void SvgFontsDialog::font_selected(SvgFont* svgfont, SPFont* spfont) {
    // in update
    auto scoped(_update.block());

    first_glyph.update(spfont);
    second_glyph.update(spfont);
    kerning_preview.set_svgfont(svgfont);
    _font_da.set_svgfont(svgfont);
    _font_da.redraw();
    _glyph_renderer->set_svg_font(svgfont);
    _glyph_cell_renderer->set_svg_font(svgfont);

    kerning_slider->set_range(0, spfont ? spfont->horiz_adv_x : 0);
    kerning_slider->set_draw_value(false);
    kerning_slider->set_value(0);

    update_global_settings_tab();
    populate_glyphs_box();
    populate_kerning_pairs_box();
    update_sensitiveness();
}

void SvgFontsDialog::on_font_selection_changed(){
    SPFont* spfont = get_selected_spfont();
    SvgFont* svgfont = get_selected_svgfont();
    font_selected(svgfont, spfont);
}

SPGlyphKerning* SvgFontsDialog::get_selected_kerning_pair()
{
    Gtk::TreeModel::iterator i = _KerningPairsList.get_selection()->get_selected();
    if(i)
        return (*i)[_KerningPairsListColumns.spnode];
    return nullptr;
}

SvgFont* SvgFontsDialog::get_selected_svgfont()
{
    Gtk::TreeModel::iterator i = _FontsList.get_selection()->get_selected();
    if(i)
        return (*i)[_columns.svgfont];
    return nullptr;
}

SPFont* SvgFontsDialog::get_selected_spfont()
{
    Gtk::TreeModel::iterator i = _FontsList.get_selection()->get_selected();
    if(i)
        return (*i)[_columns.spfont];
    return nullptr;
}

Gtk::TreeModel::iterator SvgFontsDialog::get_selected_glyph_iter() {
    if (_GlyphsListScroller.get_visible()) {
        if (auto selection = _GlyphsList.get_selection()) {
            Gtk::TreeModel::iterator it = selection->get_selected();
            return it;
        }
    }
    else {
        std::vector<Gtk::TreePath> selected = _glyphs_grid.get_selected_items();
        if (selected.size() == 1) {
            Gtk::ListStore::iterator it = _GlyphsListStore->get_iter(selected.front());
            return it;
        }
    }
    return Gtk::TreeModel::iterator();
}

SPGlyph* SvgFontsDialog::get_selected_glyph()
{
    if (auto it = get_selected_glyph_iter()) {
        return (*it)[_GlyphsListColumns.glyph_node];
    }
    return nullptr;
}

void SvgFontsDialog::set_selected_glyph(SPGlyph* glyph) {
    if (!glyph) return;

    _GlyphsListStore->foreach_iter([=, this](Gtk::TreeModel::iterator const &it) {
        if (it->get_value(_GlyphsListColumns.glyph_node) == glyph) {
            if (auto selection = _GlyphsList.get_selection()) {
                selection->select(it);
            }
            auto selected_item = _GlyphsListStore->get_path(it);
            _glyphs_grid.select_path(selected_item);
            return true; // stop
        }
        return false; // continue
    });
}

SPGuide* get_guide(SPDocument& doc, const Glib::ustring& id) {
    auto object = doc.getObjectById(id);
    if (!object) return nullptr;

    // get guide line
    if (auto guide = cast<SPGuide>(object)) {
        return guide;
    }

    // remove colliding object
    object->deleteObject();
    return nullptr;
}

SPGuide* create_guide(SPDocument& doc, double x0, double y0, double x1, double y1) {
    return SPGuide::createSPGuide(&doc, Geom::Point(x0, y1), Geom::Point(x1, y1));
}

void set_up_typography_canvas(SPDocument* document, double em, double asc, double cap, double xheight, double des) {
    if (!document || em <= 0) return;

    // set size and viewbox
    auto size = Inkscape::Util::Quantity(em, "px");
    bool change_size = false;
    document->setWidthAndHeight(size, size, change_size);
    document->setViewBox(Geom::Rect::from_xywh(0, 0, em, em));

    // baseline
    double base = des;
    double ascPos = base + asc;
    double capPos = base + cap;
    double xPos = base + xheight;
    double desPos = base - des;

    if (!document->is_yaxisdown()) {
        base = size.quantity - des;
        ascPos = base - asc;
        capPos = base - cap;
        xPos = base - xheight;
        desPos = base + des;
    }

    // add/move guide lines
    struct { double pos; const char* name; const char* id; } guides[5] = {
        {ascPos, _("ascender"), "ink-font-guide-ascender"},
        {capPos, _("caps"), "ink-font-guide-caps"},
        {xPos, _("x-height"), "ink-font-guide-x-height"},
        {base, _("baseline"), "ink-font-guide-baseline"},
        {desPos, _("descender"), "ink-font-guide-descender"},
    };

    double left = 0;
    double right = em;

    for (auto&& g : guides) {
        double y = em - g.pos;
        auto guide = get_guide(*document, g.id);
        if (guide) {
            guide->set_locked(false, true);
            guide->moveto(Geom::Point(left, y), true);
        }
        else {
            guide = create_guide(*document, left, y, right, y);
            guide->getRepr()->setAttributeOrRemoveIfEmpty("id", g.id);
        }
        guide->set_label(g.name, true);
        guide->set_locked(true, true);
    }

    DocumentUndo::done(document, _("Set up typography canvas"), "");
}

const int MARGIN_SPACE = 4;

Gtk::Box* SvgFontsDialog::global_settings_tab(){
    _fonts_scroller.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    _fonts_scroller.set_child(_FontsList);
    _fonts_scroller.set_hexpand();
    _fonts_scroller.set_visible(true);

    _header_box.set_column_spacing(MARGIN_SPACE);
    _header_box.set_row_spacing(MARGIN_SPACE);
    _header_box.attach(_fonts_scroller, 0, 0, 1, 3);
    _header_box.attach(*Gtk::make_managed<Gtk::Label>(), 1, 0);
    _header_box.attach(_font_add, 1, 1);
    _header_box.attach(_font_remove, 1, 2);
    _header_box.set_margin_bottom(MARGIN_SPACE);
    _header_box.set_margin_end(MARGIN_SPACE);

    _font_add.set_valign(Gtk::Align::CENTER);
    _font_add.set_image_from_icon_name("list-add", Gtk::IconSize::NORMAL);
    _font_add.signal_clicked().connect(sigc::mem_fun(*this, &SvgFontsDialog::add_font));

    _font_remove.set_valign(Gtk::Align::CENTER);
    _font_remove.set_halign(Gtk::Align::CENTER);
    _font_remove.set_image_from_icon_name("list-remove", Gtk::IconSize::NORMAL);
    _font_remove.signal_clicked().connect([this]{ remove_selected_font(); });

    global_vbox.set_name("SVGFontsGlobalSettingsTab");
    UI::pack_start(global_vbox, _header_box, false, false);

    _font_label          = Gtk::make_managed<Gtk::Label>(Glib::ustring::compose("<b>%1</b>", _("Font Attributes")), Gtk::Align::START, Gtk::Align::CENTER);
    _horiz_adv_x_spin    = std::make_unique <AttrSpin  >(this, _("Horizontal advance X:"), _("Default glyph width for horizontal text"                            ), SPAttr::HORIZ_ADV_X   );
    _horiz_origin_x_spin = std::make_unique <AttrSpin  >(this, _("Horizontal origin X:" ), _("Default X-coordinate of the origin of a glyph (for horizontal text)"), SPAttr::HORIZ_ORIGIN_X);
    _horiz_origin_y_spin = std::make_unique <AttrSpin  >(this, _("Horizontal origin Y:" ), _("Default Y-coordinate of the origin of a glyph (for horizontal text)"), SPAttr::HORIZ_ORIGIN_Y);
    _font_face_label     = Gtk::make_managed<Gtk::Label>(Glib::ustring::compose("<b>%1</b>", _("Font face attributes")), Gtk::Align::START, Gtk::Align::CENTER);
    _familyname_entry    = std::make_unique <AttrEntry >(this, _("Family name:"         ), _("Name of the font as it appears in font selectors and css font-family properties"), SPAttr::FONT_FAMILY );
    _units_per_em_spin   = std::make_unique <AttrSpin  >(this, _("Em-size:"             ), _("Display units per <italic>em</italic> (nominally width of 'M' character)"       ), SPAttr::UNITS_PER_EM);
    _ascent_spin         = std::make_unique <AttrSpin  >(this, _("Ascender:"            ), _("Amount of space taken up by ascenders like the tall line on the letter 'h'"     ), SPAttr::ASCENT      );
    _cap_height_spin     = std::make_unique <AttrSpin  >(this, _("Caps height:"         ), _("The height of a capital letter above the baseline like the letter 'H' or 'I'"   ), SPAttr::CAP_HEIGHT  );
    _x_height_spin       = std::make_unique <AttrSpin  >(this, _("x-height:"            ), _("The height of a lower-case letter above the baseline like the letter 'x'"       ), SPAttr::X_HEIGHT    );
    _descent_spin        = std::make_unique <AttrSpin  >(this, _("Descender:"           ), _("Amount of space taken up by descenders like the tail on the letter 'g'"         ), SPAttr::DESCENT     );

    _font_label->set_use_markup();
    _font_face_label->set_use_markup();

    _grid.set_column_spacing(MARGIN_SPACE);
    _grid.set_row_spacing(MARGIN_SPACE);
    _grid.set_margin_start(MARGIN_SPACE);
    _grid.set_margin_bottom(MARGIN_SPACE);
    const int indent = 2 * MARGIN_SPACE;
    int row = 0;

    _grid.attach(*_font_label, 0, row++, 2);
    for (auto const spin: {_horiz_adv_x_spin.get(),
                           _horiz_origin_x_spin.get(), _horiz_origin_y_spin.get()})
    {
        spin->get_label()->set_margin_start(indent);
        _grid.attach(*spin->get_label(), 0, row);
        _grid.attach(*spin->getSpin(), 1, row++);
    }

    _grid.attach(*_font_face_label, 0, row++, 2);
    _familyname_entry->get_label()->set_margin_start(indent);
    _familyname_entry->get_entry()->set_margin_end(MARGIN_SPACE);
    _grid.attach(*_familyname_entry->get_label(), 0, row);
    _grid.attach(*_familyname_entry->get_entry(), 1, row++, 2);

    for (auto const &spin : {_units_per_em_spin.get(), _ascent_spin.get(), _cap_height_spin.get(),
                             _x_height_spin.get(), _descent_spin.get()}) {
        spin->get_label()->set_margin_start(indent);
        _grid.attach(*spin->get_label(), 0, row);
        _grid.attach(*spin->getSpin(), 1, row++);
    }

    auto const setup = Gtk::make_managed<Gtk::Button>(_("Set up canvas"));
    _grid.attach(*setup, 0, row++, 2);
    setup->set_halign(Gtk::Align::START);
    setup->signal_clicked().connect([this]{
        // set up typography canvas
        set_up_typography_canvas(
            getDocument(),
            _units_per_em_spin->getSpin()->get_value(),
            _ascent_spin->getSpin()->get_value(),
            _cap_height_spin->getSpin()->get_value(),
            _x_height_spin->getSpin()->get_value(),
            _descent_spin->getSpin()->get_value()
        );
    });

    global_vbox.set_margin(2);
    UI::pack_start(global_vbox, _grid, false, true);
    return &global_vbox;
}

void SvgFontsDialog::set_glyph_row(Gtk::TreeRow &row, SPGlyph &glyph)
{
    auto unicode_name = create_unicode_name(glyph.unicode, 3);
    row[_GlyphsListColumns.glyph_node] = &glyph;
    row[_GlyphsListColumns.glyph_name] = glyph.glyph_name;
    row[_GlyphsListColumns.unicode]    = glyph.unicode;
    row[_GlyphsListColumns.UplusCode]  = unicode_name;
    row[_GlyphsListColumns.advance]    = glyph.horiz_adv_x;
    row[_GlyphsListColumns.name_markup] = "<small>" + Glib::Markup::escape_text(get_glyph_synthetic_name(glyph)) + "</small>";
}

void
SvgFontsDialog::populate_glyphs_box()
{
    if (!_GlyphsListStore) return;

    _GlyphsListStore->freeze_notify();

    // try to keep selected glyph
    Gtk::TreeModel::Path selected_item;
    if (auto selected = get_selected_glyph_iter()) {
        selected_item = _GlyphsListStore->get_path(selected);
    }
    _GlyphsListStore->clear();

    SPFont* spfont = get_selected_spfont();
    _glyphs_observer.set(spfont);

    if (spfont) {
        for (auto& node: spfont->children) {
            if (is<SPGlyph>(&node)) {
                auto& glyph = static_cast<SPGlyph&>(node);
                auto row = *_GlyphsListStore->append();
                set_glyph_row(row, glyph);
            }
        }

        if (!selected_item.empty()) {
            if (auto selection = _GlyphsList.get_selection()) {
                selection->select(selected_item);
                _GlyphsList.scroll_to_row(selected_item);
            }
            _glyphs_grid.select_path(selected_item);
        }
    }

    _GlyphsListStore->thaw_notify();
}

void
SvgFontsDialog::populate_kerning_pairs_box()
{
    if (!_KerningPairsListStore) return;

    _KerningPairsListStore->clear();

    if (SPFont* spfont = get_selected_spfont()) {
        for (auto& node: spfont->children) {
            if (is<SPHkern>(&node)){
                Gtk::TreeModel::Row row = *(_KerningPairsListStore->append());
                row[_KerningPairsListColumns.first_glyph] = (static_cast<SPGlyphKerning*>(&node))->u1->attribute_string().c_str();
                row[_KerningPairsListColumns.second_glyph] = (static_cast<SPGlyphKerning*>(&node))->u2->attribute_string().c_str();
                row[_KerningPairsListColumns.kerning_value] = (static_cast<SPGlyphKerning*>(&node))->k;
                row[_KerningPairsListColumns.spnode] = static_cast<SPGlyphKerning*>(&node);
            }
        }
    }
}

// update existing glyph in the tree model
void SvgFontsDialog::update_glyph(SPGlyph* glyph) {
    if (_update.pending() || !glyph) return;

    _GlyphsListStore->foreach_iter([&](const Gtk::TreeModel::iterator& it) {
        if (it->get_value(_GlyphsListColumns.glyph_node) == glyph) {
            Gtk::TreeRow row = *it;
            set_glyph_row(row, *glyph);
            return true; // stop
        }
        return false; // continue
    });
}

void SvgFontsDialog::update_glyphs(SPGlyph* changed_glyph) {
    if (_update.pending()) return;

    SPFont* font = get_selected_spfont();
    if (!font) return;

    if (changed_glyph) {
        update_glyph(changed_glyph);
    }
    else {
        populate_glyphs_box();
    }

    populate_kerning_pairs_box();
    refresh_svgfont();
}

void SvgFontsDialog::refresh_svgfont() {
    if (auto font = get_selected_svgfont()) {
        font->refresh();
    }
    _font_da.redraw();
}

void SvgFontsDialog::add_glyph(){
    auto document = getDocument();
    if (!document) return;
    auto font = get_selected_spfont();
    if (!font) return;

    auto glyphs = _GlyphsListStore->children();
    // initialize "unicode" field; if there are glyphs look for the last one and take next unicode
    gunichar unicode = ' ';
    if (!glyphs.empty()) {
        const auto& last = glyphs[glyphs.size() - 1];
        if (SPGlyph* last_glyph = last[_GlyphsListColumns.glyph_node]) {
            const Glib::ustring& code = last_glyph->unicode;
            if (!code.empty()) {
                auto value = code[0];
                // skip control chars 7f-9f
                if (value == 0x7e) value = 0x9f;
                // wrap around
                if (value == 0x10ffff) value = 0x1f;
                unicode = value + 1;
            }
        }
    }
    auto str = Glib::ustring(1, unicode);

    // empty name to begin with
    SPGlyph* glyph = font->create_new_glyph("", str.c_str());
    DocumentUndo::done(document, _("Add glyph"), "");

    // select newly added glyph
    set_selected_glyph(glyph);
}

double get_font_units_per_em(const SPFont* font) {
    double units_per_em = 0.0;
    if (font) {
        for (auto& obj: font->children) {
            if (is<SPFontFace>(&obj)){
                //XML Tree being directly used here while it shouldn't be.
                units_per_em = obj.getRepr()->getAttributeDouble("units-per-em", units_per_em);
                break;
            }
        }
    }
    return units_per_em;
}

Geom::PathVector flip_coordinate_system(Geom::PathVector pathv, const SPFont* font, double units_per_em) {
    if (!font) return pathv;

    if (units_per_em <= 0) {
        g_warning("Units per em not defined, path will be misplaced.");
    }

    double baseline_offset = units_per_em - font->horiz_origin_y;
    // This matrix flips y-axis and places the origin at baseline
    Geom::Affine m(1, 0, 0, -1, 0, baseline_offset);
    return pathv * m;
}

void SvgFontsDialog::set_glyph_description_from_selected_path() {
    auto font = get_selected_spfont();
    if (!font) return;

    auto selection = getSelection();
    if (!selection)
        return;

    Inkscape::MessageStack *msgStack = getDesktop()->getMessageStack();
    if (selection->isEmpty()){
        char *msg = _("Select a <b>path</b> to define the curves of a glyph");
        msgStack->flash(Inkscape::ERROR_MESSAGE, msg);
        return;
    }

    Inkscape::XML::Node* node = selection->xmlNodes().front();
    if (!node) return;//TODO: should this be an assert?
    if (!node->matchAttributeName("d") || !node->attribute("d")){
        char *msg = _("The selected object does not have a <b>path</b> description.");
        msgStack->flash(Inkscape::ERROR_MESSAGE, msg);
        return;
    } //TODO: //Is there a better way to tell it to to the user?

    SPGlyph* glyph = get_selected_glyph();
    if (!glyph){
        char *msg = _("No glyph selected in the SVGFonts dialog.");
        msgStack->flash(Inkscape::ERROR_MESSAGE, msg);
        return;
    }

    Geom::PathVector pathv = sp_svg_read_pathv(node->attribute("d"));

    auto units_per_em = get_font_units_per_em(font);
    //XML Tree being directly used here while it shouldn't be.
    glyph->setAttribute("d", sp_svg_write_path(flip_coordinate_system(pathv, font, units_per_em)));
    DocumentUndo::done(getDocument(), _("Set glyph curves"), "");

    update_glyphs(glyph);
}

void SvgFontsDialog::missing_glyph_description_from_selected_path(){
    auto font = get_selected_spfont();
    if (!font) return;

    auto selection = getSelection();
    if (!selection)
        return;

    Inkscape::MessageStack *msgStack = getDesktop()->getMessageStack();
    if (selection->isEmpty()){
        char *msg = _("Select a <b>path</b> to define the curves of a glyph");
        msgStack->flash(Inkscape::ERROR_MESSAGE, msg);
        return;
    }

    Inkscape::XML::Node* node = selection->xmlNodes().front();
    if (!node) return;//TODO: should this be an assert?
    if (!node->matchAttributeName("d") || !node->attribute("d")){
        char *msg = _("The selected object does not have a <b>path</b> description.");
        msgStack->flash(Inkscape::ERROR_MESSAGE, msg);
        return;
    } //TODO: //Is there a better way to tell it to the user?

    Geom::PathVector pathv = sp_svg_read_pathv(node->attribute("d"));

    auto units_per_em = get_font_units_per_em(font);
    for (auto& obj: font->children) {
        if (is<SPMissingGlyph>(&obj)){
            //XML Tree being directly used here while it shouldn't be.
            obj.setAttribute("d", sp_svg_write_path(flip_coordinate_system(pathv, font, units_per_em)));
            DocumentUndo::done(getDocument(),  _("Set glyph curves"), "");
        }
    }

    refresh_svgfont();
}

void SvgFontsDialog::reset_missing_glyph_description(){
    for (auto& obj: get_selected_spfont()->children) {
        if (is<SPMissingGlyph>(&obj)){
            //XML Tree being directly used here while it shouldn't be.
            obj.setAttribute("d", "M0,0h1000v1024h-1000z");
            DocumentUndo::done(getDocument(), _("Reset missing-glyph"), "");
        }
    }
    refresh_svgfont();
}

void change_glyph_attribute(SPDesktop* desktop, SPGlyph& glyph, std::function<void ()> change) {
    assert(glyph.parent);

    auto name = get_glyph_full_name(glyph);
    auto font_label = glyph.parent->label();
    auto layer = get_layer_for_glyph(desktop, font_label, name);

    change();

    if (!layer) return;

    name = get_glyph_full_name(glyph);
    font_label = glyph.parent->label();
    rename_glyph_layer(desktop, layer, font_label, name);
}

void SvgFontsDialog::glyph_name_edit(const Glib::ustring&, const Glib::ustring& str){
    SPGlyph* glyph = get_selected_glyph();
    if (!glyph) return;

    if (glyph->glyph_name == str) return; // no change

    change_glyph_attribute(getDesktop(), *glyph, [=, this]{
        //XML Tree being directly used here while it shouldn't be.
        glyph->setAttribute("glyph-name", str);

        DocumentUndo::done(getDocument(), _("Edit glyph name"), "");
        update_glyphs(glyph);
    });
}

void SvgFontsDialog::glyph_unicode_edit(const Glib::ustring&, const Glib::ustring& str){
    SPGlyph* glyph = get_selected_glyph();
    if (!glyph) return;

    if (glyph->unicode == str) return; // no change

    change_glyph_attribute(getDesktop(), *glyph, [=, this]{
        // XML Tree being directly used here while it shouldn't be.
        glyph->setAttribute("unicode", str);

        DocumentUndo::done(getDocument(), _("Set glyph unicode"), "");
        update_glyphs(glyph);
    });
}

void SvgFontsDialog::glyph_advance_edit(const Glib::ustring&, const Glib::ustring& str){
    SPGlyph* glyph = get_selected_glyph();
    if (!glyph) return;

    if (auto val = glyph->getAttribute("horiz-adv-x")) {
        if (str == val) return; // no change
    }

    //XML Tree being directly used here while it shouldn't be.
    std::istringstream is(str.raw());
    double value;
    // Check if input valid
    if ((is >> value)) {
        glyph->setAttribute("horiz-adv-x", str);
        DocumentUndo::done(getDocument(),  _("Set glyph advance"), "");

        update_glyphs(glyph);
    } else {
        std::cerr << "SvgFontDialog::glyph_advance_edit: Error in input: " << str.raw() << std::endl;
    }
}

void SvgFontsDialog::remove_selected_font(){
    SPFont* font = get_selected_spfont();
    if (!font) return;

    //XML Tree being directly used here while it shouldn't be.
    sp_repr_unparent(font->getRepr());
    DocumentUndo::done(getDocument(), _("Remove font"), "");

    update_fonts(false);
}

void SvgFontsDialog::remove_selected_glyph(){
    SPGlyph* glyph = get_selected_glyph();
    if (!glyph) return;

    //XML Tree being directly used here while it shouldn't be.
    sp_repr_unparent(glyph->getRepr());
    DocumentUndo::done(getDocument(), _("Remove glyph"), "");

    update_glyphs();
}

void SvgFontsDialog::remove_selected_kerning_pair() {
    SPGlyphKerning* pair = get_selected_kerning_pair();
    if (!pair) return;

    //XML Tree being directly used here while it shouldn't be.
    sp_repr_unparent(pair->getRepr());
    DocumentUndo::done(getDocument(), _("Remove kerning pair"), "");

    update_glyphs();
}

Inkscape::XML::Node* create_path_from_glyph(const SPGlyph& glyph) {
    Geom::PathVector pathv = sp_svg_read_pathv(glyph.getAttribute("d"));
    auto path = glyph.document->getReprDoc()->createElement("svg:path");
    // auto path = new SPPath();
    auto font = cast<SPFont>(glyph.parent);
    auto units_per_em = get_font_units_per_em(font);
    path->setAttribute("d", sp_svg_write_path(flip_coordinate_system(pathv, font, units_per_em)));
    return path;
}

// switch to a glyph layer (and create this dedicated layer if necessary)
void SvgFontsDialog::edit_glyph(SPGlyph* glyph) {
    if (!glyph || !glyph->parent) return;

    auto desktop = getDesktop();
    if (!desktop) return;
    auto document = getDocument();
    if (!document) return;

    // glyph's full name to match layer name
    auto name = get_glyph_full_name(*glyph);
    if (name.empty()) return;
    // font's name to match parent layer name
    auto font_label = get_font_label(cast<SPFont>(glyph->parent));
    if (font_label.empty()) return;

    auto layer = get_or_create_layer_for_glyph(desktop, font_label, name);
    if (!layer) return;

    // is layer empty?
    if (!layer->hasChildren()) {
        // since layer is empty try to initialize it by copying font glyph into it
        auto path = create_path_from_glyph(*glyph);
        if (path) {
            // layer->attach(path, nullptr);
            layer->addChild(path);
        }
    }

    auto& layers = desktop->layerManager();
    // set layer as "solo" - only one visible and unlocked
    if (layers.isLayer(layer) && layer != layers.currentRoot()) {
        layers.setCurrentLayer(layer, true);
        layers.toggleLayerSolo(layer, true);
        layers.toggleLockOtherLayers(layer, true);
        DocumentUndo::done(document, _("Toggle layer solo"), "");
    }
}

void SvgFontsDialog::set_glyphs_view_mode(bool list) {
    if (list) {
        _glyphs_icon_scroller.set_visible(false);
        _GlyphsListScroller.set_visible(true);
    }
    else {
        _GlyphsListScroller.set_visible(false);
        _glyphs_icon_scroller.set_visible(true);
    }
}

Gtk::Box* SvgFontsDialog::glyphs_tab() {

    glyphs_vbox.set_name("SVGFontsGlyphsTab");
    glyphs_vbox.set_margin(4);
    glyphs_vbox.set_spacing(4);

    auto const missing_glyph_button = Gtk::make_managed<Gtk::Button>(_("From selection"));
    missing_glyph_button->set_margin_top(MARGIN_SPACE);
    missing_glyph_button->signal_clicked().connect(sigc::mem_fun(*this, &SvgFontsDialog::missing_glyph_description_from_selected_path));

    auto const missing_glyph_reset_button = Gtk::make_managed<Gtk::Button>(_("Reset"));
    missing_glyph_reset_button->set_margin_top(MARGIN_SPACE);
    missing_glyph_reset_button->signal_clicked().connect(sigc::mem_fun(*this, &SvgFontsDialog::reset_missing_glyph_description));

    auto const missing_glyph_hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    missing_glyph_hbox->set_hexpand(false);
    UI::pack_start(*missing_glyph_hbox, *missing_glyph_button, false,false);
    UI::pack_start(*missing_glyph_hbox, *missing_glyph_reset_button, false,false);

    auto const missing_glyph = Gtk::make_managed<Gtk::Expander>();
    missing_glyph->set_label(_("Missing glyph"));
    missing_glyph->set_child(*missing_glyph_hbox);
    missing_glyph->set_valign(Gtk::Align::CENTER);

    _GlyphsListScroller.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::ALWAYS);
    _GlyphsListScroller.set_child(_GlyphsList);
    fix_inner_scroll(&_GlyphsListScroller);
    _GlyphsList.set_model(_GlyphsListStore);
    _GlyphsList.set_enable_search(false);

    _glyph_renderer = Gtk::make_managed<SvgGlyphRenderer>();
    const int size = 20; // arbitrarily chosen to keep glyphs small but still legible
    _glyph_renderer->set_font_size(size * 9 / 10);
    _glyph_renderer->set_cell_size(size * 3 / 2, size);
    _glyph_renderer->set_tree(&_GlyphsList);
    _glyph_renderer->signal_clicked().connect([this](Glib::RefPtr<Gdk::Event const> const &/*event*/,
                                                     Glib::ustring const &unicodes)
    {
        // set preview: show clicked glyph only
        _preview_entry.set_text(unicodes);
    });
    auto col_index = _GlyphsList.append_column(_("Glyph"), *_glyph_renderer) - 1;
    if (auto column = _GlyphsList.get_column(col_index)) {
        column->add_attribute(_glyph_renderer->property_glyph(), _GlyphsListColumns.unicode);
    }
    _GlyphsList.append_column_editable(_("Name"), _GlyphsListColumns.glyph_name);
    _GlyphsList.append_column_editable(_("Characters"), _GlyphsListColumns.unicode);
    _GlyphsList.append_column(_("Unicode"), _GlyphsListColumns.UplusCode);
    _GlyphsList.append_column_numeric_editable(_("Advance"), _GlyphsListColumns.advance, "%.2f");
    _GlyphsList.set_visible(true);
    _GlyphsList.signal_row_activated().connect([this](Gtk::TreeModel::Path const &path, Gtk::TreeViewColumn*) {
        edit_glyph(get_selected_glyph());
    });

    auto const glyph_from_path_button = Gtk::make_managed<Gtk::Button>();
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
    box->append(*Gtk::make_managed<Gtk::Image>(Gio::ThemedIcon::create("glyph-copy-from")));
    box->append(*Gtk::make_managed<Gtk::Label>(_("Get curves")));
    glyph_from_path_button->set_child(*box);
    glyph_from_path_button->set_tooltip_text(_("Get curves from selection to replace current glyph"));
    glyph_from_path_button->signal_clicked().connect(sigc::mem_fun(*this, &SvgFontsDialog::set_glyph_description_from_selected_path));

    auto const edit = Gtk::make_managed<Gtk::Button>();
    box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
    box->append(*Gtk::make_managed<Gtk::Image>(Gio::ThemedIcon::create("edit")));
    box->append(*Gtk::make_managed<Gtk::Label>(_("Edit")));
    edit->set_child(*box);
    edit->set_tooltip_text(_("Switch to a layer with the same name as current glyph"));
    edit->signal_clicked().connect([this]{ edit_glyph(get_selected_glyph()); });

    auto const sort_glyphs_button = Gtk::make_managed<Gtk::Button>(_("Sort glyphs"));
    sort_glyphs_button->set_tooltip_text(_("Sort glyphs in Unicode order"));
    sort_glyphs_button->signal_clicked().connect([this]{ sort_glyphs(get_selected_spfont()); });

    auto const add_glyph_button = Gtk::make_managed<Gtk::Button>();
    add_glyph_button->set_image_from_icon_name("list-add");
    add_glyph_button->set_tooltip_text(_("Add new glyph"));
    add_glyph_button->signal_clicked().connect(sigc::mem_fun(*this, &SvgFontsDialog::add_glyph));

    auto const remove_glyph_button = Gtk::make_managed<Gtk::Button>();
    remove_glyph_button->set_image_from_icon_name("list-remove");
    remove_glyph_button->set_tooltip_text(_("Delete current glyph"));
    remove_glyph_button->signal_clicked().connect([this]{ remove_selected_glyph(); });

    auto const hb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    UI::pack_start(*hb, *glyph_from_path_button, false, false);
    UI::pack_start(*hb, *edit, false, false);
    UI::pack_start(*hb, *sort_glyphs_button, false, false);
    UI::pack_end(*hb, *remove_glyph_button, false, false);
    UI::pack_end(*hb, *add_glyph_button, false, false);

    _glyph_cell_renderer = Gtk::make_managed<SvgGlyphRenderer>();
    _glyph_cell_renderer->set_tree(&_glyphs_grid);
    const int cell_width = 70;
    const int cell_height = 50;
    _glyph_cell_renderer->set_cell_size(cell_width, cell_height);
    _glyph_cell_renderer->set_font_size(cell_height * 8 / 10); // font size: 80% of height
    _glyphs_icon_scroller.set_child(_glyphs_grid);
    _glyphs_icon_scroller.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    _glyphs_grid.set_name("GlyphsGrid");
    _glyphs_grid.set_model(_GlyphsListStore);
    _glyphs_grid.set_item_width(cell_width);
    _glyphs_grid.set_selection_mode(Gtk::SelectionMode::SINGLE);
    _glyphs_grid.set_margin(0);
    _glyphs_grid.set_item_padding(0);
    _glyphs_grid.set_row_spacing(0);
    _glyphs_grid.set_column_spacing(0);
    _glyphs_grid.set_columns(-1);
    _glyphs_grid.set_markup_column(_GlyphsListColumns.name_markup);
    _glyphs_grid.pack_start(*_glyph_cell_renderer);
    _glyphs_grid.add_attribute(*_glyph_cell_renderer, "glyph", _GlyphsListColumns.unicode);
    _glyphs_grid.set_visible(true);
    _glyphs_grid.signal_item_activated().connect([this](Gtk::TreeModel::Path const &path) {
        edit_glyph(get_selected_glyph());
    });

    // keep selection in sync between the two views: list and grid
    _glyphs_grid.signal_selection_changed().connect([this]{
        if (_glyphs_icon_scroller.get_visible()) {
            if (auto selected = get_selected_glyph_iter()) {
                if (auto selection = _GlyphsList.get_selection()) {
                    selection->select(selected);
                }
            }
        }
    });
    if (auto selection = _GlyphsList.get_selection()) {
        selection->signal_changed().connect([this]{
            if (_GlyphsListScroller.get_visible()) {
                if (auto selected = get_selected_glyph_iter()) {
                    auto selected_item = _GlyphsListStore->get_path(selected);
                    _glyphs_grid.select_path(selected_item);
                }
            }
        });
    }

    // display mode switching buttons
    auto const hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    auto const list = Gtk::make_managed<Gtk::ToggleButton>();
    list->set_icon_name("glyph-list");
    list->set_tooltip_text(_("Glyph list view"));
    list->set_valign(Gtk::Align::START);
    list->signal_toggled().connect([this]{ set_glyphs_view_mode(true); });
    auto const grid = Gtk::make_managed<Gtk::ToggleButton>();
    grid->set_group(*list);
    grid->set_icon_name("glyph-grid");
    grid->set_tooltip_text(_("Glyph grid view"));
    grid->set_valign(Gtk::Align::START);
    grid->signal_toggled().connect([this] { set_glyphs_view_mode(false); });
    UI::pack_start(*hbox, *missing_glyph);
    UI::pack_end(*hbox, *grid, false, false);
    UI::pack_end(*hbox, *list, false, false);

    UI::pack_start(glyphs_vbox, *hb, false, false);
    UI::pack_start(glyphs_vbox, _GlyphsListScroller, true, true);
    UI::pack_start(glyphs_vbox, _glyphs_icon_scroller, true, true);
    UI::pack_start(glyphs_vbox, *hbox, false,false);

    _GlyphsListScroller.set_visible(false);
    _glyphs_icon_scroller.set_visible(false);
    (_show_glyph_list ? list : grid)->set_active();
    set_glyphs_view_mode(_show_glyph_list);

    for (auto&& col : _GlyphsList.get_columns()) {
        col->set_resizable();
    }

    static_cast<Gtk::CellRendererText*>(_GlyphsList.get_column_cell_renderer(ColName))->signal_edited().connect(
        sigc::mem_fun(*this, &SvgFontsDialog::glyph_name_edit));

    static_cast<Gtk::CellRendererText*>(_GlyphsList.get_column_cell_renderer(ColString))->signal_edited().connect(
        sigc::mem_fun(*this, &SvgFontsDialog::glyph_unicode_edit));

    static_cast<Gtk::CellRendererText*>(_GlyphsList.get_column_cell_renderer(ColAdvance))->signal_edited().connect(
        sigc::mem_fun(*this, &SvgFontsDialog::glyph_advance_edit));

    _glyphs_observer.signal_changed().connect([this]{ update_glyphs(); });

    return &glyphs_vbox;
}

void SvgFontsDialog::add_kerning_pair() {
    if (first_glyph.get_active_text() == "" ||
        second_glyph.get_active_text() == "") return;
    //look for this kerning pair on the currently selected font
    this->kerning_pair = nullptr;
    for (auto& node: get_selected_spfont()->children) {
        //TODO: It is not really correct to get only the first byte of each string.
        //TODO: We should also support vertical kerning
        if(is<SPHkern>(&node) &&
           (static_cast<SPGlyphKerning*>(&node))->u1->contains((gchar) first_glyph.get_active_text().c_str()[0]) &&
           (static_cast<SPGlyphKerning*>(&node))->u2->contains((gchar) second_glyph.get_active_text().c_str()[0])) {
            this->kerning_pair = static_cast<SPGlyphKerning*>(&node);
            return;
        }
    }


    Inkscape::XML::Document *xml_doc = getDocument()->getReprDoc();

    // create a new hkern node
    Inkscape::XML::Node *repr = xml_doc->createElement("svg:hkern");

    repr->setAttribute("u1", first_glyph.get_active_text());
    repr->setAttribute("u2", second_glyph.get_active_text());
    repr->setAttribute("k", "0");

    // Append the new hkern node to the current font
    get_selected_spfont()->getRepr()->appendChild(repr);
    Inkscape::GC::release(repr);

    // get corresponding object
    kerning_pair = cast<SPHkern>(getDocument()->getObjectByRepr(repr));

    // select newly added pair
    if (auto selection = _KerningPairsList.get_selection()) {
        _KerningPairsListStore->foreach_iter([=, this](Gtk::TreeModel::iterator const &it) {
            if (it->get_value(_KerningPairsListColumns.spnode) == kerning_pair) {
                selection->select(it);
                return true; // stop
            }
            return false; // continue
        });
    }

    DocumentUndo::done(getDocument(), _("Add kerning pair"), "");
}

Gtk::Box* SvgFontsDialog::kerning_tab(){
    auto const add_kernpair_button = Gtk::make_managed<Gtk::Button>(_("Add pair"));
    add_kernpair_button->signal_clicked().connect(sigc::mem_fun(*this, &SvgFontsDialog::add_kerning_pair));

    auto const remove_kernpair_button = Gtk::make_managed<Gtk::Button>(_("Remove pair"));
    remove_kernpair_button->signal_clicked().connect(sigc::mem_fun(*this, &SvgFontsDialog::remove_selected_kerning_pair));

    auto const kerning_selector = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, MARGIN_SPACE);
    kerning_selector->append(*Gtk::make_managed<Gtk::Label>(_("Select glyphs:")));
    kerning_selector->append(first_glyph);
    kerning_selector->append(second_glyph);
    kerning_selector->append(*add_kernpair_button);
    kerning_selector->append(*remove_kernpair_button);

    _KerningPairsList.set_model(_KerningPairsListStore);
    _KerningPairsList.append_column(_("First glyph"), _KerningPairsListColumns.first_glyph);
    _KerningPairsList.append_column(_("Second glyph"), _KerningPairsListColumns.second_glyph);
    _KerningPairsList.get_selection()->signal_changed().connect(sigc::mem_fun(*this, &SvgFontsDialog::on_kerning_pair_selection_changed));

    _KerningPairsListScroller.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::ALWAYS);
    _KerningPairsListScroller.set_child(_KerningPairsList);

    kerning_slider->signal_value_changed().connect(sigc::mem_fun(*this, &SvgFontsDialog::on_kerning_value_changed));

    // kerning_slider has a big handle. Extra padding added
    auto const kerning_amount_hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    UI::pack_start(*kerning_amount_hbox, *Gtk::make_managed<Gtk::Label>(_("Kerning value:")), false,false);
    UI::pack_start(*kerning_amount_hbox, *kerning_slider, true,true);

    kerning_preview.set_size(-1, 150 + 20);
    _font_da.set_size(-1, 60 + 20);

    kerning_vbox.set_name("SVGFontsKerningTab");
    kerning_vbox.set_margin(4);
    kerning_vbox.set_spacing(4);
    UI::pack_start(kerning_vbox, *kerning_selector, false,false);
    UI::pack_start(kerning_vbox, _KerningPairsListScroller, true,true);
    UI::pack_start(kerning_vbox, (Gtk::Widget&) kerning_preview, false,false);
    UI::pack_start(kerning_vbox, *kerning_amount_hbox, false,false);
    return &kerning_vbox;
}

SPFont *new_font(SPDocument *document)
{
    g_return_val_if_fail(document != nullptr, NULL);

    SPDefs *defs = document->getDefs();

    Inkscape::XML::Document *xml_doc = document->getReprDoc();

    // create a new font
    Inkscape::XML::Node *repr = xml_doc->createElement("svg:font");

    //By default, set the horizontal advance to 1000 units
    repr->setAttribute("horiz-adv-x", "1000");

    // Append the new font node to defs
    defs->getRepr()->appendChild(repr);

    // add some default values
    Inkscape::XML::Node *fontface;
    fontface = xml_doc->createElement("svg:font-face");
    fontface->setAttribute("units-per-em", "1000");
    fontface->setAttribute("ascent", "750");
    fontface->setAttribute("cap-height", "600");
    fontface->setAttribute("x-height", "400");
    fontface->setAttribute("descent", "200");
    repr->appendChild(fontface);

    //create a missing glyph
    Inkscape::XML::Node *mg;
    mg = xml_doc->createElement("svg:missing-glyph");
    mg->setAttribute("d", "M0,0h1000v1000h-1000z");
    repr->appendChild(mg);

    // get corresponding object
    auto f = cast<SPFont>( document->getObjectByRepr(repr) );

    g_assert(f != nullptr);
    Inkscape::GC::release(mg);
    Inkscape::GC::release(repr);
    return f;
}

void set_font_family(SPFont* font, char* str){
    if (!font) return;
    for (auto& obj: font->children) {
        if (is<SPFontFace>(&obj)){
            //XML Tree being directly used here while it shouldn't be.
            obj.setAttribute("font-family", str);
        }
    }

    DocumentUndo::done(font->document, _("Set font family"), "");
}

void SvgFontsDialog::add_font(){
    SPDocument* doc = this->getDesktop()->getDocument();
    SPFont* font = new_font(doc);

    const int count = _model->children().size();
    std::ostringstream os, os2;
    os << _("font") << " " << count;
    font->setLabel(os.str().c_str());

    os2 << "SVGFont " << count;
    for (auto& obj: font->children) {
        if (is<SPFontFace>(&obj)){
            //XML Tree being directly used here while it shouldn't be.
            obj.setAttribute("font-family", os2.str());
        }
    }

    update_fonts(false);
    on_font_selection_changed();

    DocumentUndo::done(doc, _("Add font"), "");
}

SvgFontsDialog::SvgFontsDialog()
 : DialogBase("/dialogs/svgfonts", "SVGFonts")
 , global_vbox(Gtk::Orientation::VERTICAL)
 , glyphs_vbox(Gtk::Orientation::VERTICAL)
 , kerning_vbox(Gtk::Orientation::VERTICAL)
{
    kerning_slider = Gtk::make_managed<Gtk::Scale>(Gtk::Orientation::HORIZONTAL);

    // kerning pairs store
    _KerningPairsListStore = Gtk::ListStore::create(_KerningPairsListColumns);

    // list of glyphs in a current font; this store is reused if there are multiple fonts
    _GlyphsListStore = Gtk::ListStore::create(_GlyphsListColumns);

    // List of SVGFonts declared in a document:
    _model = Gtk::ListStore::create(_columns);
    _FontsList.set_model(_model);
    _FontsList.set_enable_search(false);
    _FontsList.append_column_editable(_("_Fonts"), _columns.label);
    _FontsList.get_selection()->signal_changed().connect(sigc::mem_fun(*this, &SvgFontsDialog::on_font_selection_changed));
    // connect to the cell renderer's edit signal; there's also model's row_changed, but it is less specific
    if (auto renderer = dynamic_cast<Gtk::CellRendererText*>(_FontsList.get_column_cell_renderer(0))) {
        // commit font names when user edits them
        renderer->signal_edited().connect([this](Glib::ustring const &path, Glib::ustring const &new_name) {
            if (auto it = _model->get_iter(path)) {
                auto font = it->get_value(_columns.spfont);
                font->setLabel(new_name.c_str());
                Glib::ustring undokey = "svgfonts:fontName";
                DocumentUndo::maybeDone(font->document, undokey.c_str(), _("Set SVG font name"), "");
            }
        });
    }

    auto const tabs = Gtk::make_managed<Gtk::Notebook>();
    tabs->set_scrollable();

    tabs->append_page(*global_settings_tab(), _("_Global settings"), true);
    tabs->append_page(*glyphs_tab(), _("_Glyphs"), true);
    tabs->append_page(*kerning_tab(), _("_Kerning"), true);
    tabs->signal_switch_page().connect([this](Gtk::Widget*, guint page) {
        if (page == 2) {
            // update kerning glyph combos
            if (SPFont* font = get_selected_spfont()) {
                first_glyph.update(font);
                second_glyph.update(font);
            }
        }
    });

    UI::pack_start(*this, *tabs, true, true);

    // Text Preview:
    _preview_entry.signal_changed().connect(sigc::mem_fun(*this, &SvgFontsDialog::on_preview_text_changed));
    UI::pack_start(*this, _font_da, false, false);
    _preview_entry.set_text(_("Sample text"));
    _font_da.set_text(_("Sample text"));

    auto const preview_entry_hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, MARGIN_SPACE);
    UI::pack_start(*this, *preview_entry_hbox, false, false); // Non-latin characters may need more height.
    UI::pack_start(*preview_entry_hbox, *Gtk::make_managed<Gtk::Label>(_("Preview text:")), false, false);
    UI::pack_start(*preview_entry_hbox, _preview_entry, true, true);
    preview_entry_hbox->set_margin_bottom(MARGIN_SPACE);
    preview_entry_hbox->set_margin_start(MARGIN_SPACE);
    preview_entry_hbox->set_margin_end(MARGIN_SPACE);

    set_visible(true);
}

SvgFontsDialog::~SvgFontsDialog() = default;

void SvgFontsDialog::documentReplaced()
{
    _defs_observer_connection.disconnect();
    if (auto document = getDocument()) {
        _defs_observer.set(document->getDefs());
        _defs_observer_connection = _defs_observer.signal_changed().connect([this]{ update_fonts(false); });
    }
    update_fonts(true);
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
