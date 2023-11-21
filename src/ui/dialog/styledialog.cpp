// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A dialog for CSS styles
 */
/* Authors:
 *   Kamalpreet Kaur Grewal
 *   Tavmjong Bah
 *   Jabiertxof
 *
 * Copyright (C) Kamalpreet Kaur Grewal 2016 <grewalkamal005@gmail.com>
 * Copyright (C) Tavmjong Bah 2017 <tavmjong@free.fr>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "styledialog.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <regex>
#include <string>
#include <utility>

#include <glibmm/i18n.h>
#include <glibmm/regex.h>
#include <gdk/gdkkeysyms.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/cellrenderertoggle.h>
#include <gtkmm/dialog.h>
#include <gtkmm/entry.h>
#include <gtkmm/eventcontrollerfocus.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/treestore.h>
#include <gtkmm/treeviewcolumn.h>
#include <gtkmm/treeview.h>
#include <sigc++/adaptors/bind.h>
#include <sigc++/functors/mem_fun.h>

#include "attribute-rel-svg.h"
#include "attributes.h"
#include "document.h"
#include "document-undo.h"
#include "inkscape.h"
#include "selection.h"
#include "style.h"
#include "style-enums.h"
#include "style-internal.h"

#include "io/resource.h"
#include "object/sp-object.h"
#include "svg/svg-color.h"
#include "ui/builder-utils.h"
#include "ui/controller.h"
#include "ui/icon-loader.h"
#include "ui/pack.h"
#include "ui/util.h"
#include "ui/widget/iconrenderer.h"
#include "util/trim.h"
#include "xml/attribute-record.h"
#include "xml/node-observer.h"
#include "xml/sp-css-attr.h"

// G_MESSAGES_DEBUG=DEBUG_STYLEDIALOG  gdb ./inkscape
// #define DEBUG_STYLEDIALOG
// #define G_LOG_DOMAIN "STYLEDIALOG"

using std::size_t;
using Inkscape::DocumentUndo;

namespace Inkscape::UI::Dialog {

/**
 * Get the first <style> element's first text node. If no such node exists and
 * `create_if_missing` is false, then return NULL.
 *
 * Only finds <style> elements in root or in root-level <defs>.
 */
XML::Node *get_first_style_text_node(XML::Node *root, bool create_if_missing)
{
    static GQuark const CODE_svg_style = g_quark_from_static_string("svg:style");
    static GQuark const CODE_svg_defs = g_quark_from_static_string("svg:defs");

    XML::Node *styleNode = nullptr;
    XML::Node *textNode = nullptr;

    if (!root) {
        return nullptr;
    }

    for (auto *node = root->firstChild(); node; node = node->next()) {
        if (node->code() == CODE_svg_defs) {
            textNode = get_first_style_text_node(node, false);
            if (textNode != nullptr) {
                return textNode;
            }
        }

        if (node->code() == CODE_svg_style) {
            styleNode = node;
            break;
        }
    }

    if (styleNode == nullptr) {
        if (!create_if_missing)
            return nullptr;

        styleNode = root->document()->createElement("svg:style");
        root->addChild(styleNode, nullptr);
        Inkscape::GC::release(styleNode);
    }

    for (auto *node = styleNode->firstChild(); node; node = node->next()) {
        if (node->type() == XML::NodeType::TEXT_NODE) {
            textNode = node;
            break;
        }
    }

    if (textNode == nullptr) {
        if (!create_if_missing)
            return nullptr;

        textNode = root->document()->createTextNode("");
        styleNode->appendChild(textNode);
        Inkscape::GC::release(textNode);
    }

    return textNode;
}

// Keeps a watch on style element
class StyleDialog::NodeObserver : public Inkscape::XML::NodeObserver {
  public:
    NodeObserver(StyleDialog *styledialog)
        : _styledialog(styledialog)
    {
        g_debug("StyleDialog::NodeObserver: Constructor");
    };

    void notifyContentChanged(Inkscape::XML::Node &node, Inkscape::Util::ptr_shared old_content,
                              Inkscape::Util::ptr_shared new_content) override;

    StyleDialog *_styledialog;
};


void StyleDialog::NodeObserver::notifyContentChanged(Inkscape::XML::Node & /*node*/,
                                                     Inkscape::Util::ptr_shared /*old_content*/,
                                                     Inkscape::Util::ptr_shared /*new_content*/)
{

    g_debug("StyleDialog::NodeObserver::notifyContentChanged");
    _styledialog->_updating = false;
    _styledialog->readStyleElement();
}

// Keeps a watch for new/removed/changed nodes
// (Must update objects that selectors match.)
class StyleDialog::NodeWatcher : public Inkscape::XML::NodeObserver {
    StyleDialog *_styledialog;

public:
    NodeWatcher(StyleDialog *styledialog)
        : _styledialog(styledialog)
    {
        g_debug("StyleDialog::NodeWatcher: Constructor");
    };

    void notifyChildAdded(Inkscape::XML::Node & /*node*/, Inkscape::XML::Node &child,
                          Inkscape::XML::Node * /*prev*/) override
    {
        _styledialog->_nodeAdded(child);
    }

    void notifyChildRemoved(Inkscape::XML::Node & /*node*/, Inkscape::XML::Node &child,
                            Inkscape::XML::Node * /*prev*/) override
    {
        _styledialog->_nodeRemoved(child);
    }

    void notifyAttributeChanged(Inkscape::XML::Node &node, GQuark qname, Util::ptr_shared /*old_value*/,
                                Util::ptr_shared /*new_value*/) override
    {
        static GQuark const CODE_id = g_quark_from_static_string("id");
        static GQuark const CODE_class = g_quark_from_static_string("class");
        static GQuark const CODE_style = g_quark_from_static_string("style");

        if (qname == CODE_id || qname == CODE_class || qname == CODE_style) {
            _styledialog->_nodeChanged(node);
        }
    }
};

void StyleDialog::_nodeAdded(Inkscape::XML::Node &node)
{
    if (!getShowing()) {
        return;
    }
    readStyleElement();
}

void StyleDialog::_nodeRemoved(Inkscape::XML::Node &repr)
{
    if (!getShowing()) {
        return;
    }
    if (_textNode == &repr) {
        _textNode = nullptr;
    }

    readStyleElement();
}

void StyleDialog::_nodeChanged(Inkscape::XML::Node &object)
{
    if (!getShowing()) {
        return;
    }
    g_debug("StyleDialog::_nodeChanged");
    readStyleElement();
}

/**
 * Constructor
 * A treeview and a set of two buttons are added to the dialog. _addSelector
 * adds selectors to treeview. _delSelector deletes the selector from the dialog.
 * Any addition/deletion of the selectors updates XML style element accordingly.
 */
StyleDialog::StyleDialog()
    : DialogBase("/dialogs/style", "Style")
    , m_nodewatcher     {std::make_unique<StyleDialog::NodeWatcher >(this)}
    , m_styletextwatcher{std::make_unique<StyleDialog::NodeObserver>(this)}
{
    g_debug("StyleDialog::StyleDialog");

    UI::pack_start(_mainBox, _scrolledWindow, UI::PackOptions::expand_widget);
    _scrolledWindow.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);

    _styleBox.set_name("StyleBox");
    _styleBox.set_orientation(Gtk::Orientation::VERTICAL);
    _styleBox.set_valign(Gtk::Align::START);

    _scrolledWindow.set_child(_styleBox);
    _scrolledWindow.set_overlay_scrolling(false);
    _vadj = _scrolledWindow.get_vadjustment();
    _vadj->signal_value_changed().connect(sigc::mem_fun(*this, &StyleDialog::_vscroll));

    _mainBox.set_orientation(Gtk::Orientation::VERTICAL);

    UI::pack_start(*this, _mainBox, UI::PackOptions::expand_widget);
}

StyleDialog::~StyleDialog()
{
    removeObservers();
}

void StyleDialog::_vscroll()
{
    if (!_scrollock) {
        _scrollpos = _vadj->get_value();
    } else {
        _vadj->set_value(_scrollpos);
        _scrollock = false;
    }
}

Glib::ustring StyleDialog::fixCSSSelectors(Glib::ustring selector)
{
    g_debug("SelectorsDialog::fixCSSSelectors");
    Util::trim(selector);
    std::vector<Glib::ustring> tokens = Glib::Regex::split_simple("[,]+", selector);
    CRSelector *cr_selector = cr_selector_parse_from_buf((guchar const *)selector.c_str(), CR_UTF_8);
    for (auto &token : tokens) {
        Util::trim(token);
        std::vector<Glib::ustring> subtokens = Glib::Regex::split_simple("[ ]+", token);
        for (auto &subtoken : subtokens) {
            Util::trim(subtoken);
            CRSelector *cr_selector = cr_selector_parse_from_buf((guchar const *)subtoken.c_str(), CR_UTF_8);
            gchar *selectorchar = reinterpret_cast<gchar *>(cr_selector_to_string(cr_selector));
            if (selectorchar) {
                Glib::ustring tag(selectorchar);
                g_free(selectorchar);
                if (tag.size() > 1 && tag[0] != '.' && tag[0] != '#') {
                    auto const i = std::min(tag.find("#"), tag.find("."));
                    if (i != std::string::npos) {
                        tag.resize(i);
                    }
                    if (!SPAttributeRelSVG::isSVGElement(tag)) {
                        if (tokens.size() == 1) {
                            tag.insert(0, 1, '.');
                            return tag;
                        } else {
                            return {};
                        }
                    }
                }
            }
        }
    }
    if (cr_selector) {
        return selector;
    }
    return {};
}

void StyleDialog::_reload() { readStyleElement(); }

/**
 * @return Inkscape::XML::Node* pointing to a style element's text node.
 * Returns the style element's text node. If there is no style element, one is created.
 * Ditto for text node.
 */
Inkscape::XML::Node *StyleDialog::_getStyleTextNode(bool create_if_missing)
{
    g_debug("StyleDialog::_getStyleTextNoded");

    auto const textNode = get_first_style_text_node(m_root, create_if_missing);

    if (_textNode != textNode) {
        if (_textNode) {
            _textNode->removeObserver(*m_styletextwatcher);
        }

        _textNode = textNode;

        if (_textNode) {
            _textNode->addObserver(*m_styletextwatcher);
        }
    }

    return textNode;
}

void StyleDialog::setCurrentSelector(Glib::ustring current_selector)
{
    g_debug("StyleDialog::setCurrentSelector");
    _current_selector = std::move(current_selector);
    readStyleElement();
}

// copied from style.cpp:1499
static bool is_url(char const *p)
{
    if (p == nullptr)
        return false;
    /** \todo
     * FIXME: I'm not sure if this applies to SVG as well, but CSS2 says any URIs
     * in property values must start with 'url('.
     */
    return (g_ascii_strncasecmp(p, "url(", 4) == 0);
}

/**
 * Fill the Gtk::TreeStore from the svg:style element.
 */
void StyleDialog::readStyleElement()
{
    g_debug("StyleDialog::readStyleElement");

    auto document = getDocument();
    if (_updating || !document || _deletion)
        return; // Don't read if we wrote style element.
    _updating = true;
    _scrollock = true;
    Inkscape::XML::Node *textNode = _getStyleTextNode();

    // Get content from style text node.
    std::string content = (textNode && textNode->content()) ? textNode->content() : "";

    // Remove end-of-lines (check it works on Windoze).
    content.erase(std::remove(content.begin(), content.end(), '\n'), content.end());

    // Remove comments (/* xxx */)

    bool breakme = false;
    size_t start = content.find("/*");
    size_t open = content.find("{", start + 1);
    size_t close = content.find("}", start + 1);
    size_t end = content.find("*/", close + 1);
    while (!breakme) {
        if (open == std::string::npos || close == std::string::npos || end == std::string::npos) {
            breakme = true;
            break;
        }
        while (open < close) {
            open = content.find("{", close + 1);
            close = content.find("}", close + 1);
            end = content.find("*/", close + 1);
            size_t reopen = content.find("{", close + 1);
            if (open == std::string::npos || end == std::string::npos || end < reopen) {
                if (end < reopen) {
                    content = content.erase(start, end - start + 2);
                } else {
                    breakme = true;
                }
                break;
            }
        }
        start = content.find("/*", start + 1);
        open = content.find("{", start + 1);
        close = content.find("}", start + 1);
        end = content.find("*/", close + 1);
    }

    // First split into selector/value chunks.
    // An attempt to use Glib::Regex failed. A C++11 version worked but
    // reportedly has problems on Windows. Using split_simple() is simpler
    // and probably faster.
    //
    // Glib::RefPtr<Glib::Regex> regex1 =
    //   Glib::Regex::create("([^\\{]+)\\{([^\\{]+)\\}");
    //
    // Glib::MatchInfo minfo;
    // regex1->match(content, minfo);

    // Split on curly brackets. Even tokens are selectors, odd are values.
    std::vector<Glib::ustring> tokens = Glib::Regex::split_simple("[}{]", content.c_str()); // Must use c_str() as "content" is a std::string which cannot
                                                                                            // be converted directly to a Glib::UStringView in Gtk4.
    _owner_style.clear();
    // If text node is empty, return (avoids problem with negative below).

    UI::remove_all_children(_styleBox);

    Inkscape::Selection *selection = getSelection();
    SPObject *obj = nullptr;
    if (selection->objects().size() == 1) {
        obj = selection->objects().back();
    }
    if (!obj) {
        obj = document->getXMLDialogSelectedObject();
        if (obj && !obj->getRepr()) {
            obj = nullptr; // treat detached object as no selection
        }
    }

    // Currently selected object's properties set via style element.
    auto builder = create_builder("dialog-css.glade");
    auto css_selector_container = &get_widget<Gtk::Box>     (builder, "CSSSelectorContainer");
    auto css_selector           = &get_widget<Gtk::Label>   (builder, "CSSSelector");
    auto css_tree               = &get_widget<Gtk::TreeView>(builder, "CSSTree");
    auto css_button             = &get_widget<Gtk::Button>  (builder, "CSSSelectorAddButton");

    css_selector->set_text("element");

    css_tree->add_css_class("style_element");
    Glib::RefPtr<Gtk::TreeStore> store = Gtk::TreeStore::create(_mColumns);
    css_tree->set_model(store);
    _addTreeViewHandlers(*css_tree); // TODO: GTK4: Just add one on self as weʼll get events there?

    unsigned selectorpos = 0;
    css_button->signal_clicked().connect(
        sigc::bind(
            sigc::mem_fun(*this, &StyleDialog::_addRow), store, css_tree, "style_properties", selectorpos));

    auto const addRenderer = Gtk::make_managed<UI::Widget::IconRenderer>();
    addRenderer->add_icon("edit-delete");
    int addCol = css_tree->append_column(" ", *addRenderer) - 1;
    Gtk::TreeViewColumn *col = css_tree->get_column(addCol);
    if (col) {
        addRenderer->signal_activated().connect(
            sigc::bind(sigc::mem_fun(*this, &StyleDialog::_onPropDelete), store));
    }

    auto const label = Gtk::make_managed<Gtk::CellRendererText>();
    label->property_placeholder_text() = _("property");
    label->property_editable() = true;
    label->signal_edited().connect(sigc::bind(
        sigc::mem_fun(*this, &StyleDialog::_nameEdited), store, css_tree));
    label->signal_editing_started().connect(sigc::mem_fun(*this, &StyleDialog::_startNameEdit));
    addCol = css_tree->append_column(" ", *label) - 1;
    col = css_tree->get_column(addCol);
    if (col) {
        col->set_resizable(true);
        col->add_attribute(label->property_text(), _mColumns._colName);
    }

    auto const value = Gtk::make_managed<Gtk::CellRendererText>();
    value->property_placeholder_text() = _("value");
    value->property_editable() = true;
    value->signal_edited().connect(
        sigc::bind(sigc::mem_fun(*this, &StyleDialog::_valueEdited), store));
    value->signal_editing_started().connect(
        sigc::bind(sigc::mem_fun(*this, &StyleDialog::_startValueEdit), store));
    addCol = css_tree->append_column(" ", *value) - 1;
    col = css_tree->get_column(addCol);
    if (col) {
        col->add_attribute(value->property_text(), _mColumns._colValue);
        col->set_expand(true);
        col->add_attribute(value->property_strikethrough(), _mColumns._colStrike);
    }

    auto const urlRenderer = Gtk::make_managed<UI::Widget::IconRenderer>();
    urlRenderer->add_icon("empty-icon");
    urlRenderer->add_icon("edit-redo");

    int urlCol = css_tree->append_column(" ", *urlRenderer) - 1;
    Gtk::TreeViewColumn *urlcol = css_tree->get_column(urlCol);
    if (urlcol) {
        urlcol->set_min_width(40);
        urlcol->set_max_width(40);
        urlRenderer->signal_activated().connect(sigc::bind(sigc::mem_fun(*this, &StyleDialog::_onLinkObj), store));
        urlcol->add_attribute(urlRenderer->property_icon(), _mColumns._colLinked);
    }

    AttrProp attr_prop;
    Gtk::TreeModel::Path path;
    bool empty = true;
    if (obj && obj->getRepr()->attribute("style")) {
        Glib::ustring style = obj->getRepr()->attribute("style");
        attr_prop = parseStyle(std::move(style));

        for (auto iter : obj->style->properties()) {
            auto const &name = iter->name();
            auto const found = attr_prop.find(name);
            if (found == attr_prop.end()) continue;

            auto const &value = found->second;
            empty = false;
            Gtk::TreeModel::Row row = *(store->prepend());
            row[_mColumns._colSelector] = "style_properties";
            row[_mColumns._colSelectorPos] = 0;
            row[_mColumns._colActive] = true;
            row[_mColumns._colName] = name;
            row[_mColumns._colValue] = value;
            row[_mColumns._colStrike] = false;
            row[_mColumns._colOwner] = _("Current value");
            row[_mColumns._colHref] = nullptr;
            row[_mColumns._colLinked] = false;
            if (is_url(value.c_str())) {
                auto id = value.substr(5, value.size() - 6);
                SPObject *elemref = nullptr;
                if ((elemref = document->getObjectById(id.c_str()))) {
                    row[_mColumns._colHref] = elemref;
                    row[_mColumns._colLinked] = true;
                }
            }
            _addOwnerStyle(name, _("Used in style attribute"));
        }

        // this is to fix a bug on cairo win:
        // https://gitlab.freedesktop.org/cairo/cairo/issues/338
        // TODO: check if inkscape min cairo version has applied the patch proposed and remove (3 times)
        if (empty) {
            css_tree->set_visible(false);
        }
        UI::pack_start(_styleBox, *css_selector_container, UI::PackOptions::expand_widget);
    }

    selectorpos++;

    if (tokens.size() == 0) {
        _updating = false;
        return;
    }

    // Loop over selectors.
    for (size_t i = 0; i < tokens.size() - 1; i += 2) {
        auto selector = std::move(tokens[i]);
        Util::trim(selector); // Remove leading/trailing spaces
        // Get list of objects selector matches
        std::vector<Glib::ustring> selectordata = Glib::Regex::split_simple(";", selector);
        Glib::ustring selector_orig = selector;
        if (!selectordata.empty()) {
            selector = selectordata.back();
        }
        std::vector<SPObject *> objVec = _getObjVec(selector);
        if (obj) {
            bool stop = true;
            for (auto objel : objVec) {
                if (objel == obj) {
                    stop = false;
                }
            }
            if (stop) {
                _updating = false;
                selectorpos++;
                continue;
            }
        }
        if (!obj && !_current_selector.empty() && _current_selector != selector) {
            _updating = false;
            selectorpos++;
            continue;
        }
        if (!obj) {
            bool present = false;
            for (auto objv : objVec) {
                for (auto objsel : selection->objects()) {
                    if (objv == objsel) {
                        present = true;
                        break;
                    }
                }
                if (present) {
                    break;
                }
            }
            if (!present) {
                _updating = false;
                selectorpos++;
                continue;
            }
        }

        Glib::ustring properties;
        // Check to make sure we do have a value to match selector.
        if ((i + 1) < tokens.size()) {
            properties = std::move(tokens[i + 1]);
        } else {
            std::cerr << "StyleDialog::readStyleElement: Missing values "
                         "for last selector!"
                      << std::endl;
        }

        // Create new builder each loop.
        auto builder = create_builder("dialog-css.glade");
        auto css_selector_container = &get_widget<Gtk::Box>     (builder, "CSSSelectorContainer");
        auto css_selector           = &get_widget<Gtk::Label>   (builder, "CSSSelector");
        auto css_tree               = &get_widget<Gtk::TreeView>(builder, "CSSTree");
        auto css_button             = &get_widget<Gtk::Button>  (builder, "CSSSelectorAddButton");

        css_selector->set_text(selector);

        css_tree->add_css_class("style_sheet");
        Glib::RefPtr<Gtk::TreeStore> store = Gtk::TreeStore::create(_mColumns);
        css_tree->set_model(store);
        _addTreeViewHandlers(*css_tree); // TODO: GTK4: Just add one on self as weʼll get events there?

        css_button->signal_clicked().connect(
            sigc::bind(
                sigc::mem_fun(*this, &StyleDialog::_addRow), store, css_tree, selector_orig, selectorpos));

        auto const addRenderer = Gtk::make_managed<UI::Widget::IconRenderer>();
        addRenderer->add_icon("edit-delete");
        int addCol = css_tree->append_column(" ", *addRenderer) - 1;
        Gtk::TreeViewColumn *col = css_tree->get_column(addCol);
        if (col) {
            addRenderer->signal_activated().connect(
                sigc::bind(sigc::mem_fun(*this, &StyleDialog::_onPropDelete), store));
        }

        auto const isactive = Gtk::make_managed<Gtk::CellRendererToggle>();
        isactive->property_activatable() = true;
        addCol = css_tree->append_column(" ", *isactive) - 1;
        col = css_tree->get_column(addCol);
        if (col) {
            col->add_attribute(isactive->property_active(), _mColumns._colActive);
            isactive->signal_toggled().connect(
                sigc::bind(sigc::mem_fun(*this, &StyleDialog::_activeToggled), store));
        }

        auto const label = Gtk::make_managed<Gtk::CellRendererText>();
        label->property_placeholder_text() = _("property");
        label->property_editable() = true;
        label->signal_edited().connect(sigc::bind(
            sigc::mem_fun(*this, &StyleDialog::_nameEdited), store, css_tree));
        label->signal_editing_started().connect(sigc::mem_fun(*this, &StyleDialog::_startNameEdit));
        addCol = css_tree->append_column(" ", *label) - 1;
        col = css_tree->get_column(addCol);
        if (col) {
            col->set_resizable(true);
            col->add_attribute(label->property_text(), _mColumns._colName);
        }

        auto const value = Gtk::make_managed<Gtk::CellRendererText>();
        value->property_editable() = true;
        value->property_placeholder_text() = _("value");
        value->signal_edited().connect(
            sigc::bind(sigc::mem_fun(*this, &StyleDialog::_valueEdited), store));
        value->signal_editing_started().connect(
            sigc::bind(sigc::mem_fun(*this, &StyleDialog::_startValueEdit), store));
        addCol = css_tree->append_column(" ", *value) - 1;
        col = css_tree->get_column(addCol);
        if (col) {
            col->add_attribute(value->property_text(), _mColumns._colValue);
            col->add_attribute(value->property_strikethrough(), _mColumns._colStrike);
        }

        Glib::ustring comments;
        for (size_t beg = 0, end = 0;
             (beg = properties.find("/*", beg    )) != properties.npos &&
             (end = properties.find("*/", beg + 2)) != properties.npos;)
        {
            comments.append(properties, beg + 2, end - beg - 2);
            properties.erase(beg, end - beg + 2);
        }

        std::map<Glib::ustring, std::pair<Glib::ustring, bool>> result_props;
        auto const move_to_result = [&](AttrProp &&src_props, bool const active)
        {
            while (!src_props.empty()) {
                auto &&node = src_props.extract(src_props.begin());
                result_props[std::move(node.key())] = {std::move(node.mapped()), active};
            }
        };
        move_to_result(parseStyle(std::move(properties)), true );
        move_to_result(parseStyle(std::move(comments  )), false);
        empty = result_props.empty();


        for (auto const &[name, pair] : result_props) {
            auto const &[value, active] = pair;

            Gtk::TreeModel::iterator iterstore = obj ? store->append() : store->prepend();
            Gtk::TreeModel::Row row = *(iterstore);
            row[_mColumns._colSelector] = selector_orig;
            row[_mColumns._colSelectorPos] = selectorpos;
            row[_mColumns._colActive] = active;
            row[_mColumns._colName] = name;
            row[_mColumns._colValue] = value;

            if (!obj) {
                row[_mColumns._colOwner] = _("Stylesheet value");
                continue;
            }

            if (!active) {
                row[_mColumns._colStrike] = true;
                row[_mColumns._colOwner] = _("This value is commented out.");
                continue;
            }

            auto val = Glib::ustring{};
            for (auto iterprop : obj->style->properties()) {
                if (iterprop->style_src != SPStyleSrc::UNSET && iterprop->name() == name) {
                    val = iterprop->get_value();
                    break;
                }
            }
            guint32 r1 = 0; // if there's no color, return black
            r1 = sp_svg_read_color(value.c_str(), r1);
            guint32 r2 = 0; // if there's no color, return black
            r2 = sp_svg_read_color(val.c_str(), r2);
            if ((r1 == 0 || r1 != r2) && value != val || attr_prop.count(name)) {
                row[_mColumns._colStrike] = true;
            } else {
                row[_mColumns._colOwner] = _("Current value");
                // Translators: %1 is a CSS selector.
                _addOwnerStyle(name, Glib::ustring::compose(_("Used in %1"), selector));
            }
        }

        if (empty) {
            css_tree->set_visible(false);
        }

        UI::pack_start(_styleBox, *css_selector_container, UI::PackOptions::expand_widget);

        selectorpos++;
    }

    // Currently selected object's properties set via attributes.
    // Create new builder and get a new set of widgets.
    builder = create_builder("dialog-css.glade");
    css_selector_container = &get_widget<Gtk::Box>     (builder, "CSSSelectorContainer");
    css_selector           = &get_widget<Gtk::Label>   (builder, "CSSSelector");
    css_tree               = &get_widget<Gtk::TreeView>(builder, "CSSTree");
    css_button             = &get_widget<Gtk::Button>  (builder, "CSSSelectorAddButton");

    css_selector->set_text("element.attributes");

    css_tree->add_css_class("style_attribute");
    store = Gtk::TreeStore::create(_mColumns);
    css_tree->set_model(store);
    _addTreeViewHandlers(*css_tree); // TODO: GTK4: Just add one on self as weʼll get events there?

    css_button->signal_clicked().connect(
        sigc::bind(
            sigc::mem_fun(*this, &StyleDialog::_addRow), store, css_tree, "attributes", selectorpos));

    bool hasattributes = false;
    empty = true;
    if (obj) {
        for (auto iter : obj->style->properties()) {
            if (iter->style_src != SPStyleSrc::UNSET) {
                auto key = iter->id();
                if (key != SPAttr::FONT && key != SPAttr::D && key != SPAttr::MARKER) {
                    const gchar *attr = obj->getRepr()->attribute(iter->name().c_str());
                    if (attr) {
                        if (!hasattributes) {
                            auto const addRenderer = Gtk::make_managed<UI::Widget::IconRenderer>();
                            addRenderer->add_icon("edit-delete");
                            int addCol = css_tree->append_column(" ", *addRenderer) - 1;
                            Gtk::TreeViewColumn *col = css_tree->get_column(addCol);
                            if (col) {
                                addRenderer->signal_activated().connect(sigc::bind(
                                    sigc::mem_fun(*this, &StyleDialog::_onPropDelete), store));
                            }

                            auto const label = Gtk::make_managed<Gtk::CellRendererText>();
                            label->property_placeholder_text() = _("property");
                            label->property_editable() = true;
                            label->signal_edited().connect(sigc::bind(
                                sigc::mem_fun(*this, &StyleDialog::_nameEdited), store, css_tree));
                            label->signal_editing_started().connect(sigc::mem_fun(*this, &StyleDialog::_startNameEdit));
                            addCol = css_tree->append_column(" ", *label) - 1;
                            col = css_tree->get_column(addCol);
                            if (col) {
                                col->set_resizable(true);
                                col->add_attribute(label->property_text(), _mColumns._colName);
                            }

                            auto const value = Gtk::make_managed<Gtk::CellRendererText>();
                            value->property_placeholder_text() = _("value");
                            value->property_editable() = true;
                            value->signal_edited().connect(sigc::bind(
                                sigc::mem_fun(*this, &StyleDialog::_valueEdited), store));
                            value->signal_editing_started().connect(sigc::bind(
                                sigc::mem_fun(*this, &StyleDialog::_startValueEdit), store));
                            addCol = css_tree->append_column(" ", *value) - 1;
                            col = css_tree->get_column(addCol);
                            if (col) {
                                col->add_attribute(value->property_text(), _mColumns._colValue);
                                col->add_attribute(value->property_strikethrough(), _mColumns._colStrike);
                            }
                        }

                        empty = false;
                        Gtk::TreeModel::iterator iterstore = store->prepend();
                        Gtk::TreeModel::Path path = (Gtk::TreeModel::Path)iterstore;
                        Gtk::TreeModel::Row row = *(iterstore);
                        row[_mColumns._colSelector] = "attributes";
                        row[_mColumns._colSelectorPos] = selectorpos;
                        row[_mColumns._colActive] = true;
                        row[_mColumns._colName] = iter->name();
                        row[_mColumns._colValue] = attr;
                        if (_owner_style.find(iter->name()) != _owner_style.end()) {
                            row[_mColumns._colStrike] = true;
                            row[_mColumns._colOwner] = Glib::ustring{};
                        } else {
                            row[_mColumns._colStrike] = false;
                            row[_mColumns._colOwner] = _("Current value");
                            _addOwnerStyle(iter->name(), _("Used in inline attributes"));
                        }
                        hasattributes = true;
                    }
                }
            }
        }

        if (empty) {
            css_tree->set_visible(false);
        }

        if (!hasattributes) {
            UI::remove_all_children(*css_selector_container);
        }

        UI::pack_start(_styleBox, *css_selector_container, UI::PackOptions::expand_widget);
    }

    for (auto const selector : UI::get_children(_styleBox)) {
        if (auto const box = dynamic_cast<Gtk::Box *>(selector)) {
            auto const childs = UI::get_children(*box);
            if (childs.size() > 1) {
                if (auto const css_tree = dynamic_cast<Gtk::TreeView *>(childs[1])) {
                    if (auto const &model = css_tree->get_model()) {
                        model->foreach_iter(sigc::mem_fun(*this, &StyleDialog::_on_foreach_iter));
                    }
                }
            }
        }
    }

    if (obj) {
        obj->style->readFromObject(obj);
        obj->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG);
    }

    _updating = false;
}

bool StyleDialog::_on_foreach_iter(const Gtk::TreeModel::iterator &iter)
{
    g_debug("StyleDialog::_on_foreach_iter");

    Gtk::TreeModel::Row row = *(iter);
    auto const &owner = row.get_value(_mColumns._colOwner);
    if (!owner.empty()) return false;

    auto const it = _owner_style.find(row.get_value(_mColumns._colName));
    if (it != _owner_style.end()) {
        row[_mColumns._colOwner] = it->second;
        row[_mColumns._colStrike] = true;
    } else {
        row[_mColumns._colOwner] = _("Current value");
        row[_mColumns._colStrike] = false;
    }
    return false;
}

void StyleDialog::_onLinkObj(Glib::ustring const &path, Glib::RefPtr<Gtk::TreeStore> const& store)
{
    g_debug("StyleDialog::_onLinkObj");

    Gtk::TreeModel::Row row = *store->get_iter(path);
    if (row && row[_mColumns._colLinked]) {
        SPObject *linked = row[_mColumns._colHref];
        if (linked) {
            auto selection = getSelection();
            getDocument()->setXMLDialogSelectedObject(linked);
            selection->clear();
            selection->set(linked);
        }
    }
}

/**
 * @brief StyleDialog::_onPropDelete
 * @param event
 * @return true
 * Delete the attribute from the style
 */
void StyleDialog::_onPropDelete(Glib::ustring const &path, Glib::RefPtr<Gtk::TreeStore> const &store)
{
    g_debug("StyleDialog::_onPropDelete");
    auto iter = store->get_iter(path);
    auto row = *iter;
    if (row) {
        Glib::ustring selector = row[_mColumns._colSelector];
        row[_mColumns._colName] = Glib::ustring{};
        _deleted_pos = row[_mColumns._colSelectorPos];
        store->erase(iter);
        _deletion = true;
        _writeStyleElement(store, selector);
        _deletion = false;
    }
}

void StyleDialog::_addOwnerStyle(Glib::ustring name, Glib::ustring selector)
{
    g_debug("StyleDialog::_addOwnerStyle");

    if (_owner_style.find(name) == _owner_style.end()) {
        _owner_style.emplace(std::move(name), std::move(selector));
    }
}


/**
 * @brief StyleDialog::parseStyle
 *
 * Convert a style string into a vector map. This should be moved to style.cpp
 *
 */
StyleDialog::AttrProp StyleDialog::parseStyle(Glib::ustring style_string)
{
    g_debug("StyleDialog::parseStyle");

    Util::trim(style_string); // We'd use const, but we need to trip spaces

    AttrProp ret;

    static auto const r_props = Glib::Regex::create("\\s*;\\s*");
    std::vector<Glib::ustring> props = r_props->split(style_string);

    for (auto &&token : props) {
        Util::trim(token);
        if (token.empty())
            break;

        static auto const r_pair = Glib::Regex::create("\\s*:\\s*");
        std::vector<Glib::ustring> pair = r_pair->split(token);

        if (pair.size() > 1) {
            ret[std::move(pair[0])] = std::move(pair[1]);
        }
    }
    return ret;
}


/**
 * Update the content of the style element as selectors (or objects) are added/removed.
 */
void StyleDialog::_writeStyleElement(Glib::RefPtr<Gtk::TreeStore> const &store,
                                     Glib::ustring selector, Glib::ustring const &new_selector)
{
    g_debug("StyleDialog::_writeStyleElemen");
    auto selection = getSelection();
    if (_updating && selection)
        return;
    _scrollock = true;
    SPObject *obj = nullptr;
    if (selection->objects().size() == 1) {
        obj = selection->objects().back();
    }
    if (!obj) {
        obj = getDocument()->getXMLDialogSelectedObject();
    }
    if (selection->objects().size() < 2 && !obj) {
        readStyleElement();
        return;
    }
    _updating = true;
    int selectorpos = 0;
    std::string styleContent;
    if (selector != "style_properties" && selector != "attributes") {
        if (!new_selector.empty()) {
            selector = new_selector;
        }
        std::vector<Glib::ustring> selectordata = Glib::Regex::split_simple(";", selector);
        for (auto selectoritem : selectordata) {
            if (selectordata[selectordata.size() - 1] == selectoritem) {
                selector = selectoritem;
            } else {
                styleContent = styleContent + selectoritem + ";\n";
            }
        }
        styleContent.append("\n").append(selector.raw()).append(" { \n");
    }
    selectorpos = _deleted_pos;
    for (auto &row : store->children()) {
        selector = row[_mColumns._colSelector];
        selectorpos = row[_mColumns._colSelectorPos];
        const char *opencomment = "";
        const char *closecomment = "";
        if (selector != "style_properties" && selector != "attributes") {
            opencomment = row[_mColumns._colActive] ? "    " : "  /*";
            closecomment = row[_mColumns._colActive] ? "\n" : "*/\n";
        }
        Glib::ustring const &name = row[_mColumns._colName];
        Glib::ustring const &value = row[_mColumns._colValue];
        if (!(name.empty() && value.empty())) {
            styleContent = styleContent + opencomment + name.raw() + ":" + value.raw() + ";" + closecomment;
        }
    }
    if (selector != "style_properties" && selector != "attributes") {
        styleContent = styleContent + "}";
    }
    if (selector == "style_properties") {
        _updating = true;
        obj->getRepr()->setAttribute("style", styleContent);
        _updating = false;
    } else if (selector == "attributes") {
        for (auto iter : obj->style->properties()) {
            auto key = iter->id();
            if (key != SPAttr::FONT && key != SPAttr::D && key != SPAttr::MARKER) {
                const gchar *attr = obj->getRepr()->attribute(iter->name().c_str());
                if (attr) {
                    _updating = true;
                    obj->getRepr()->removeAttribute(iter->name());
                    _updating = false;
                }
            }
        }
        for (auto &row : store->children()) {
            Glib::ustring const &name = row[_mColumns._colName];
            Glib::ustring const &value = row[_mColumns._colValue];
            if (!(name.empty() && value.empty())) {
                _updating = true;
                obj->getRepr()->setAttribute(name, value);
                _updating = false;
            }
        }
    } else if (!selector.empty()) { // styleshet
        // We could test if styleContent is empty and then delete the style node here but there is no
        // harm in keeping it around ...

        std::string pos = std::to_string(selectorpos);
        std::string selectormatch = "(";
        for (; selectorpos > 1; selectorpos--) {
            selectormatch = selectormatch + "[^\\}]*?\\}";
        }
        selectormatch = selectormatch + ")([^\\}]*?\\})((.|\n)*)";

        Inkscape::XML::Node *textNode = _getStyleTextNode(true);
        std::regex e(selectormatch.c_str());
        std::string content = (textNode->content() ? textNode->content() : "");
        std::string result;
        std::regex_replace(std::back_inserter(result), content.begin(), content.end(), e, "$1" + styleContent + "$3");
        bool empty = false;
        if (result.empty()) {
            empty = true;
            result = "* > .inkscapehacktmp{}";
        }
        textNode->setContent(result.c_str());
        if (empty) {
            textNode->setContent("");
        }
    }
    _updating = false;
    readStyleElement();
    for (auto iter : getDocument()->getObjectsBySelector(selector)) {
        iter->style->readFromObject(iter);
        iter->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG);
    }
    DocumentUndo::done(SP_ACTIVE_DOCUMENT, _("Edited style element."), "");

    g_debug("StyleDialog::_writeStyleElement(): | %s |", styleContent.c_str());
}

void StyleDialog::_addRow(Glib::RefPtr<Gtk::TreeStore> const &store, Gtk::TreeView *const css_tree,
                          Glib::ustring const &selector, int const pos)
{
    g_debug("StyleDialog::_addRow");

    Gtk::TreeModel::iterator iter = store->prepend();
    Gtk::TreeModel::Path path = (Gtk::TreeModel::Path)iter;
    Gtk::TreeModel::Row row = *(iter);
    row[_mColumns._colSelector] = selector;
    row[_mColumns._colSelectorPos] = pos;
    row[_mColumns._colActive] = true;

    auto const col = pos < 1 ? 1 : 2;
    css_tree->set_visible(true);
    css_tree->set_cursor(path, *(css_tree->get_column(col)), true);
    grab_focus();
}

void StyleDialog::_setAutocompletion(Gtk::Entry *entry, SPStyleEnum const cssenum[])
{
    g_debug("StyleDialog::_setAutocompletion");

    Glib::RefPtr<Gtk::ListStore> completionModel = Gtk::ListStore::create(_mCSSData);
    Glib::RefPtr<Gtk::EntryCompletion> entry_completion = Gtk::EntryCompletion::create();
    entry_completion->set_model(completionModel);
    entry_completion->set_text_column (_mCSSData._colCSSData);
    entry_completion->set_minimum_key_length(0);
    entry_completion->set_popup_completion(true);
    int counter = 0;
    const char * key = cssenum[counter].key;
    while (key) {
        Gtk::TreeModel::Row row = *(completionModel->prepend());
        row[_mCSSData._colCSSData] = Glib::ustring(key);
        counter++;
        key = cssenum[counter].key;
    }
    entry->set_completion(entry_completion);
}

/*Hardcode values non in enum*/
void StyleDialog::_setAutocompletion(Gtk::Entry *entry, Glib::ustring name)
{
    g_debug("StyleDialog::_setAutocompletion");

    Glib::RefPtr<Gtk::ListStore> completionModel = Gtk::ListStore::create(_mCSSData);
    Glib::RefPtr<Gtk::EntryCompletion> entry_completion = Gtk::EntryCompletion::create();
    entry_completion->set_model(completionModel);
    entry_completion->set_text_column(_mCSSData._colCSSData);
    entry_completion->set_minimum_key_length(0);
    entry_completion->set_popup_completion(true);
    if (name == "paint-order") {
        Gtk::TreeModel::Row row = *(completionModel->append());
        row[_mCSSData._colCSSData] = Glib::ustring("fill markers stroke");
        row = *(completionModel->append());
        row[_mCSSData._colCSSData] = Glib::ustring("fill stroke markers");
        row = *(completionModel->append());
        row[_mCSSData._colCSSData] = Glib::ustring("stroke markers fill");
        row = *(completionModel->append());
        row[_mCSSData._colCSSData] = Glib::ustring("stroke fill markers");
        row = *(completionModel->append());
        row[_mCSSData._colCSSData] = Glib::ustring("markers fill stroke");
        row = *(completionModel->append());
        row[_mCSSData._colCSSData] = Glib::ustring("markers stroke fill");
    }
    entry->set_completion(entry_completion);
}

void
StyleDialog::_startValueEdit(Gtk::CellEditable* cell, const Glib::ustring& path, Glib::RefPtr<Gtk::TreeStore> store)
{
    g_debug("StyleDialog::_startValueEdit");

    _scrollock = true;

    Gtk::TreeModel::Row row = *store->get_iter(path);
    if (row) {
        Gtk::Entry *entry = dynamic_cast<Gtk::Entry *>(cell);

        Glib::ustring name = row[_mColumns._colName];
        if (name == "paint-order") {
            _setAutocompletion(entry, name);
        } else if (name == "fill-rule") {
            _setAutocompletion(entry, enum_fill_rule);
        } else if (name == "stroke-linecap") {
            _setAutocompletion(entry, enum_stroke_linecap);
        } else if (name == "stroke-linejoin") {
            _setAutocompletion(entry, enum_stroke_linejoin);
        } else if (name == "font-style") {
            _setAutocompletion(entry, enum_font_style);
        } else if (name == "font-variant") {
            _setAutocompletion(entry, enum_font_variant);
        } else if (name == "font-weight") {
            _setAutocompletion(entry, enum_font_weight);
        } else if (name == "font-stretch") {
            _setAutocompletion(entry, enum_font_stretch);
        } else if (name == "font-variant-position") {
            _setAutocompletion(entry, enum_font_variant_position);
        } else if (name == "text-align") {
            _setAutocompletion(entry, enum_text_align);
        } else if (name == "text-transform") {
            _setAutocompletion(entry, enum_text_transform);
        } else if (name == "text-anchor") {
            _setAutocompletion(entry, enum_text_anchor);
        } else if (name == "white-space") {
            _setAutocompletion(entry, enum_white_space);
        } else if (name == "direction") {
            _setAutocompletion(entry, enum_direction);
        } else if (name == "baseline-shift") {
            _setAutocompletion(entry, enum_baseline_shift);
        } else if (name == "visibility") {
            _setAutocompletion(entry, enum_visibility);
        } else if (name == "overflow") {
            _setAutocompletion(entry, enum_overflow);
        } else if (name == "display") {
            _setAutocompletion(entry, enum_display);
        } else if (name == "shape-rendering") {
            _setAutocompletion(entry, enum_shape_rendering);
        } else if (name == "color-rendering") {
            _setAutocompletion(entry, enum_color_rendering);
        } else if (name == "clip-rule") {
            _setAutocompletion(entry, enum_clip_rule);
        } else if (name == "color-interpolation") {
            _setAutocompletion(entry, enum_color_interpolation);
        }

        _setEditingEntry(entry, ";");
    }
}

void StyleDialog::_startNameEdit(Gtk::CellEditable *cell, const Glib::ustring &path)
{
    g_debug("StyleDialog::_startNameEdit");

    _scrollock = true;

    Glib::RefPtr<Gtk::ListStore> completionModel = Gtk::ListStore::create(_mCSSData);
    Glib::RefPtr<Gtk::EntryCompletion> entry_completion = Gtk::EntryCompletion::create();
    entry_completion->set_model(completionModel);
    entry_completion->set_text_column(_mCSSData._colCSSData);
    entry_completion->set_minimum_key_length(1);
    entry_completion->set_popup_completion(true);

    for (auto prop : sp_attribute_name_list(true)) {
        Gtk::TreeModel::Row row = *(completionModel->append());
        row[_mCSSData._colCSSData] = prop;
    }

    Gtk::Entry *entry = dynamic_cast<Gtk::Entry *>(cell);
    entry->set_completion(entry_completion);
    _setEditingEntry(entry, ":=");
}

gboolean sp_styledialog_store_move_to_next(gpointer data)
{
    StyleDialog *styledialog = reinterpret_cast<StyleDialog *>(data);
    auto selection = styledialog->_current_css_tree->get_selection();
    Gtk::TreeModel::iterator iter = selection->get_selected();
    if (!iter) {
        return false;
    }
    Gtk::TreeModel::Path model = (Gtk::TreeModel::Path)iter;
    if (model == styledialog->_current_path) {
        styledialog->_current_css_tree->set_cursor(styledialog->_current_path, *styledialog->_current_value_col,
                                                   true);
    }
    return false;
}

/**
 * @brief StyleDialog::nameEdited
 * @param event
 * Called when the name is edited in the TreeView editable column
 */
void StyleDialog::_nameEdited(const Glib::ustring &path, const Glib::ustring &name, Glib::RefPtr<Gtk::TreeStore> store,
                              Gtk::TreeView *css_tree)
{
    g_debug("StyleDialog::_nameEdited");

    _scrollock = true;

    auto iter = store->get_iter(path);
    auto row = *iter;
    _current_path = store->get_path(iter);

    if (!row) return;

    _current_css_tree = css_tree;

    Glib::ustring finalname = name;
    auto i = finalname.find_first_of(";:=");
    if (i != std::string::npos) {
        finalname.erase(i, name.size() - i);
    }

    auto const pos = row.get_value(_mColumns._colSelectorPos);
    auto const write =  row.get_value(_mColumns._colName ) != finalname &&
                       !row.get_value(_mColumns._colValue).empty();

    Glib::ustring selector = row[_mColumns._colSelector];
    Glib::ustring value = row[_mColumns._colValue];
    bool is_attr = selector == "attributes";

    Glib::ustring old_name = row[_mColumns._colName];
    row[_mColumns._colName] = finalname;

    if (finalname.empty() && value.empty()) {
        _deleted_pos = row[_mColumns._colSelectorPos];
        store->erase(iter);
    }

    auto const col = pos < 1 || is_attr ? 2 : 3;
    _current_value_col = css_tree->get_column(col);

    if (write && old_name != name) {
        _writeStyleElement(store, selector);
    } else {
        g_timeout_add(50, &sp_styledialog_store_move_to_next, this);
        grab_focus();
    }
}

/**
 * @brief StyleDialog::valueEdited
 * @param event
 * @return
 * Called when the value is edited in the TreeView editable column
 */
void StyleDialog::_valueEdited(const Glib::ustring &path, const Glib::ustring &value,
                               Glib::RefPtr<Gtk::TreeStore> store)
{
    g_debug("StyleDialog::_valueEdited");

    _scrollock = true;

    auto iter = store->get_iter(path);
    auto row = *iter;
    if (row) {
        Glib::ustring finalvalue = value;
        auto i = std::min(finalvalue.find(";"), finalvalue.find(":"));
        if (i != std::string::npos) {
            finalvalue.erase(i, finalvalue.size() - i);
        }
        Glib::ustring old_value = row[_mColumns._colValue];
        if (old_value == finalvalue) {
            return;
        }
        row[_mColumns._colValue] = finalvalue;
        Glib::ustring selector = row[_mColumns._colSelector];
        Glib::ustring name = row[_mColumns._colName];
        if (name.empty() && finalvalue.empty()) {
            _deleted_pos = row[_mColumns._colSelectorPos];
            store->erase(iter);
        }
        _writeStyleElement(store, selector);
        if (selector != "style_properties" && selector != "attributes") {
            std::vector<SPObject *> objs = _getObjVec(selector);
            for (auto obj : objs) {
                auto css_str = Glib::ustring{};
                SPCSSAttr *css = sp_repr_css_attr_new();
                sp_repr_css_attr_add_from_string(css, obj->getRepr()->attribute("style"));
                css->removeAttribute(name);
                sp_repr_css_write_string(css, css_str);
                obj->getRepr()->setAttribute("style", css_str);
                obj->style->readFromObject(obj);
                obj->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG);
            }
        }
    }
}

void StyleDialog::_activeToggled(const Glib::ustring &path, Glib::RefPtr<Gtk::TreeStore> const &store)
{
    g_debug("StyleDialog::_activeToggled");

    _scrollock = true;
    Gtk::TreeModel::Row row = *store->get_iter(path);
    if (row) {
        row[_mColumns._colActive] = !row[_mColumns._colActive];
        Glib::ustring selector = row[_mColumns._colSelector];
        _writeStyleElement(store, selector);
    }
}

void StyleDialog::_addTreeViewHandlers(Gtk::TreeView &treeview)
{
    // This seems needed to get Tab to work as it usually does OotB, in e.g. AttrDialog. Unsure why
    Controller::add_key<nullptr, &StyleDialog::_onTreeViewKeyReleased>(treeview, *this);

    // and since the above somehow doesnʼt fire on focus-out of final cell, we have to do this too…
    auto focus = Gtk::EventControllerFocus::create();
    focus->set_propagation_phase(Gtk::PropagationPhase::BUBBLE);
    treeview.add_controller(focus);
    focus->signal_leave().connect([this] { _onTreeViewFocusLeave(); });

    // If TreeView can-focus, above arenʼt needed, BUT it causes CRITICALs… so just be Good Enough™
    // Doing that also means we need 2 presses of Tab, the 1st to dismiss the completion: not ideal
}

void StyleDialog::_setEditingEntry(Gtk::Entry * const entry, Glib::ustring endChars)
{
    g_debug("StyleDialog::_setEditingEntry: _editingEntry = %p", static_cast<void *>(entry));

    _editingEntry = entry;

    if (entry == nullptr) return;

    // Using entry, not _editingEntry, avoids lifetime confusion/crashes via signal emission order.
    entry->property_text().signal_changed().connect( [entry, endChars = std::move(endChars)]
    {
        g_debug("StyleDialog::_setEditingEntry: Entry:text changed");

        auto text = entry->get_text();
        if (text.empty()) return;
        auto const endChar = text[text.size() - 1];
        if (endChars.find(endChar) == endChars.npos) return;

        text.resize(text.size() - 1);
        entry->set_text(text);
        entry->editing_done();
    });

    entry->signal_editing_done().connect([this]{ _setEditingEntry(nullptr, {}); });
}

bool StyleDialog::_onTreeViewKeyReleased(GtkEventControllerKey const * /*controller*/,
                                         unsigned keyval, unsigned /*keycode*/,
                                         GdkModifierType /*state*/)
{
    g_debug("StyleDialog::_onTreeViewKeyReleased");

    if (_editingEntry != nullptr && (keyval == GDK_KEY_Tab or keyval == GDK_KEY_KP_Tab))
    {
        g_debug("StyleDialog::_onTreeViewKeyReleased: _editingEntry != nullptr && Tab");

        _editingEntry->editing_done();
    }

    return false;
}

void StyleDialog::_onTreeViewFocusLeave()
{
    g_debug("StyleDialog::_onTreeViewFocusLeave");

    if (_editingEntry) {
        g_debug("StyleDialog::_onTreeViewFocus: _editingEntry != nullptr");

        // If !change, entry stays visible after Tab, but remove_widget() crashes so… Donʼt Do That
        _editingEntry->editing_done();
    }
}

/**
 * @param selector: a valid CSS selector string.
 * @return objVec: a vector of pointers to SPObject's the selector matches.
 * Return a vector of all objects that selector matches.
 */
std::vector<SPObject *> StyleDialog::_getObjVec(Glib::ustring selector)
{
    g_debug("StyleDialog::_getObjVec");

    g_assert(selector.find(";") == Glib::ustring::npos);

    return getDocument()->getObjectsBySelector(selector);
}

void StyleDialog::_closeDialog(Gtk::Dialog *textDialogPtr) { textDialogPtr->response(Gtk::ResponseType::OK); }


void StyleDialog::removeObservers()
{
    if (_textNode) {
        _textNode->removeObserver(*m_styletextwatcher);
        _textNode = nullptr;
    }
    if (m_root) {
        m_root->removeSubtreeObserver(*m_nodewatcher);
        m_root = nullptr;
    }
}

/**
 * Handle document replaced. (Happens when a default document is immediately replaced by another
 * document in a new window.)
 */
void StyleDialog::documentReplaced()
{
    removeObservers();
    if (auto document = getDocument()) {
        m_root = document->getReprRoot();
        m_root->addSubtreeObserver(*m_nodewatcher);
    }
    readStyleElement();
}

/*
 * Handle a change in which objects are selected in a document.
 */
void StyleDialog::selectionChanged(Selection * /*selection*/)
{
    _scrollpos = 0;
    _vadj->set_value(0);
    // Sometimes the selection changes because inkscape is closing.
    if (getDesktop()) {
        readStyleElement();
    }
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
