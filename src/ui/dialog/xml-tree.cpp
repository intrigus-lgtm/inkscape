// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * XML editor.
 */
/* Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   MenTaLguY <mental@rydia.net>
 *   bulia byak <buliabyak@users.sf.net>
 *   Johan Engelen <goejendaagh@zonnet.nl>
 *   David Turner
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *   Mike Kowalski
 *
 * Copyright (C) 1999-2006 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 *
 */

#include "xml-tree.h"

#include <initializer_list>
#include <memory>
#include <glibmm/i18n.h>
#include <glibmm/ustring.h>
#include <glibmm/variant.h>
#include <giomm/simpleactiongroup.h>
#include <gtkmm/button.h>
#include <gtkmm/box.h>
#include <gtkmm/enums.h>
#include <gtkmm/entry.h>
#include <gtkmm/image.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/object.h>
#include <gtkmm/paned.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/treeview.h>

#include "desktop.h"
#include "document.h"
#include "document-undo.h"
#include "inkscape.h"
#include "layer-manager.h"
#include "message-context.h"
#include "message-stack.h"
#include "selection.h"
#include "preferences.h"
#include "object/sp-root.h"
#include "object/sp-string.h"
#include "ui/builder-utils.h"
#include "ui/dialog-events.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/menuize.h"
#include "ui/pack.h"
#include "ui/syntax.h"
#include "ui/tools/tool-base.h"
#include "ui/util.h"
#include "util/trim.h"
#include "widgets/sp-xmlview-tree.h"

namespace {

/**
 * Set the orientation of `paned` to vertical or horizontal, and make the first child resizable
 * if vertical, and the second child resizable if horizontal.
 * @pre `paned` has two children
 */
void paned_set_vertical(Gtk::Paned &paned, bool vertical)
{
    auto& first = *paned.get_child1();
    auto& second = *paned.get_child2();
    const int space = 1;
    paned.child_property_resize(first) = vertical;
    first.set_margin_bottom(vertical ? space : 0);
    first.set_margin_end(vertical ? 0 : space);
    second.set_margin_top(vertical ? space : 0);
    second.set_margin_start(vertical ? 0 : space);
    assert(paned.child_property_resize(second));
    paned.set_orientation(vertical ? Gtk::Orientation::VERTICAL : Gtk::Orientation::HORIZONTAL);
}

} // namespace

namespace Inkscape::UI::Dialog {

XmlTree::XmlTree()
    : DialogBase("/dialogs/xml/", "XMLEditor")
    , _builder(create_builder("dialog-xml.glade"))
    , _paned(get_widget<Gtk::Paned>(_builder, "pane"))
    , xml_element_new_button(get_widget<Gtk::Button>(_builder, "new-elem"))
    , xml_text_new_button(get_widget<Gtk::Button>(_builder, "new-text"))
    , xml_node_delete_button(get_widget<Gtk::Button>(_builder, "del"))
    , xml_node_duplicate_button(get_widget<Gtk::Button>(_builder, "dup"))
    , unindent_node_button(get_widget<Gtk::Button>(_builder, "unindent"))
    , indent_node_button(get_widget<Gtk::Button>(_builder, "indent"))
    , lower_node_button(get_widget<Gtk::Button>(_builder, "lower"))
    , raise_node_button(get_widget<Gtk::Button>(_builder, "raise"))
    , _syntax_theme("/theme/syntax-color-theme")
    , _mono_font("/dialogs/xml/mono-font", false)
{
    /* tree view */
    tree = SP_XMLVIEW_TREE(sp_xmlview_tree_new(nullptr, nullptr, nullptr));
    gtk_widget_set_tooltip_text( GTK_WIDGET(tree), _("Drag to reorder nodes") );

    Gtk::ScrolledWindow& tree_scroller = get_widget<Gtk::ScrolledWindow>(_builder, "tree-wnd");
    _treemm = Gtk::manage(Glib::wrap(GTK_TREE_VIEW(tree)));
    tree_scroller.add(*Gtk::manage(Glib::wrap(GTK_WIDGET(tree))));
    fix_inner_scroll(&tree_scroller);

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    /* attributes */
    attributes = Gtk::make_managed<AttrDialog>();
    attributes->set_margin_top(0);
    attributes->set_margin_bottom(0);
    attributes->set_margin_start(0);
    attributes->set_margin_end(0);
    attributes->get_scrolled_window().set_has_frame(true);
    attributes->set_visible(true);
    attributes->get_status_box().set_visible(false);
    attributes->get_status_box().set_no_show_all();
    _paned.pack2(*attributes, true, false);

    /* Signal handlers */
    _treemm->get_selection()->signal_changed().connect([=]() {
        if (blocked || !getDesktop())
            return;
        if (!_tree_select_idle) {
            // Defer the update after all events have been processed.
            _tree_select_idle = Glib::signal_idle().connect(sigc::mem_fun(*this, &XmlTree::deferred_on_tree_select_row));
        }
    });
    tree->connectTreeMove([=]() {
        if (auto doc = getDocument()) {
            DocumentUndo::done(doc, Q_("Undo History / XML Editor|Drag XML subtree"), INKSCAPE_ICON("dialog-xml-editor"));
        }
    });

    xml_element_new_button.signal_clicked().connect(sigc::mem_fun(*this, &XmlTree::cmd_new_element_node));
    xml_text_new_button.signal_clicked().connect(sigc::mem_fun(*this, &XmlTree::cmd_new_text_node));
    xml_node_duplicate_button.signal_clicked().connect(sigc::mem_fun(*this, &XmlTree::cmd_duplicate_node));
    xml_node_delete_button.signal_clicked().connect(sigc::mem_fun(*this, &XmlTree::cmd_delete_node));
    unindent_node_button.signal_clicked().connect(sigc::mem_fun(*this, &XmlTree::cmd_unindent_node));
    indent_node_button.signal_clicked().connect(sigc::mem_fun(*this, &XmlTree::cmd_indent_node));
    raise_node_button.signal_clicked().connect(sigc::mem_fun(*this, &XmlTree::cmd_raise_node));
    lower_node_button.signal_clicked().connect(sigc::mem_fun(*this, &XmlTree::cmd_lower_node));

    set_name("XMLAndAttributesDialog");
    set_spacing(0);
    show_all();

    int panedpos = prefs->getInt("/dialogs/xml/panedpos", 200);
    _paned.property_position() = panedpos;
    _paned.property_position().signal_changed().connect(sigc::mem_fun(*this, &XmlTree::_resized));

    UI::pack_start(*this, get_widget<Gtk::Box>(_builder, "main"), true, true);

    int min_width = 0, dummy;
    get_preferred_width(min_width, dummy);

    auto auto_arrange_panels = [=](Gtk::Allocation const &alloc) {
        // skip bogus sizes
        if (alloc.get_width() < 10 || alloc.get_height() < 10) return;

        // minimal width times fudge factor to arrive at "narrow" dialog with automatic vertical layout:
        const bool narrow = alloc.get_width() < min_width * 1.5;
        paned_set_vertical(_paned, narrow);
    };

    auto arrange_panels = [=](DialogLayout layout){
        switch (layout) {
            case Auto:
                auto_arrange_panels(get_allocation());
                break;
            case Horizontal:
                paned_set_vertical(_paned, false);
                break;
            case Vertical:
                paned_set_vertical(_paned, true);
                break;
        }
        // ensure_size();
    };

    signal_size_allocate().connect([=] (Gtk::Allocation const &alloc) {
        arrange_panels(_layout);
    });

    auto& popup = get_widget<Gtk::MenuButton>(_builder, "layout-btn");
    popup.set_has_tooltip();
    popup.signal_query_tooltip().connect([=](int x, int y, bool kbd, const Glib::RefPtr<Gtk::Tooltip>& tooltip){
        auto tip = "";
        switch (_layout) {
            case Auto: tip = _("Automatic panel layout:\nchanges with dialog size");
                break;
            case Horizontal: tip = _("Horizontal panel layout");
                break;
            case Vertical: tip = _("Vertical panel layout");
                break;
        }
        tooltip->set_text(tip);
        return true;
    });
    UI::menuize_popover(*popup.get_popover());

    auto set_layout = [=](DialogLayout layout){
        Glib::ustring icon = "layout-auto";
        if (layout == Horizontal) {
            icon = "layout-horizontal";
        } else if (layout == Vertical) {
            icon = "layout-vertical";
        }
        get_widget<Gtk::Image>(_builder, "layout-img").set_from_icon_name(icon + "-symbolic");
        prefs->setInt("/dialogs/xml/layout", layout);
        arrange_panels(layout);
        _layout = layout;
    };

    _layout = static_cast<DialogLayout>(prefs->getIntLimited("/dialogs/xml/layout", Auto, Auto, Vertical));
    auto action_group = Gio::SimpleActionGroup::create();
    auto action = action_group->add_action_radio_integer("layout", _layout);
    action->property_state().signal_changed().connect([=, &popup]
    {
        popup.set_active(false);
        int target; action->get_state(target); set_layout(static_cast<DialogLayout>(target));
    });
    insert_action_group("xml-tree", std::move(action_group));
    set_layout(_layout);

    // establish initial layout to prevent unwanted panels resize in auto layout mode
    paned_set_vertical(_paned, true);

    _syntax_theme.action = [=]() {
        setSyntaxStyle(Inkscape::UI::Syntax::build_xml_styles(_syntax_theme));
        // rebuild tree to change markup
        rebuildTree();
    };

    setSyntaxStyle(Inkscape::UI::Syntax::build_xml_styles(_syntax_theme));

    _mono_font.action = [=]() {
        Glib::ustring mono("mono-font");
        if (_mono_font) {
            _treemm->get_style_context()->add_class(mono);
        } else {
            _treemm->get_style_context()->remove_class(mono);
        }
        attributes->set_mono_font(_mono_font);
    };
    _mono_font.action();

    tree->renderer->signal_editing_canceled().connect([=]() {
        stopNodeEditing(false, "", "");
    });
    tree->renderer->signal_edited().connect([=](const Glib::ustring& path, const Glib::ustring& name) {
        stopNodeEditing(true, path, name);
    });
    tree->renderer->signal_editing_started().connect([=](Gtk::CellEditable* cell, const Glib::ustring& path) {
        startNodeEditing(cell, path);
    });
}

XmlTree::~XmlTree()
{
    unsetDocument();
}

void XmlTree::rebuildTree()
{
    sp_xmlview_tree_set_repr(tree, nullptr);
    if (auto document = getDocument()) {
        set_tree_repr(document->getReprRoot());
    }
}

void XmlTree::_resized()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setInt("/dialogs/xml/panedpos", _paned.property_position());
}

void XmlTree::unsetDocument()
{
    _tree_select_idle.disconnect();
}

void XmlTree::documentReplaced()
{
    unsetDocument();
    if (auto document = getDocument()) {
        // TODO: Why is this a document property?
        document->setXMLDialogSelectedObject(nullptr);

        set_tree_repr(document->getReprRoot());
    } else {
        set_tree_repr(nullptr);
    }
}

void XmlTree::selectionChanged(Selection *selection)
{
    if (!blocked++) {
        Inkscape::XML::Node *node = get_dt_select();
        set_tree_select(node);
    }
    blocked--;
}

void XmlTree::set_tree_repr(Inkscape::XML::Node *repr)
{
    if (repr == selected_repr) {
        return;
    }

    sp_xmlview_tree_set_repr(tree, repr);
    if (repr) {
        set_tree_select(get_dt_select());
    } else {
        set_tree_select(nullptr);
    }

    propagate_tree_select(selected_repr);
}

/**
 * Expand all parent nodes of `repr`
 */
static void expand_parents(SPXMLViewTree *tree, Inkscape::XML::Node *repr)
{
    auto parentrepr = repr->parent();
    if (!parentrepr) {
        return;
    }

    expand_parents(tree, parentrepr);

    GtkTreeIter node;
    if (sp_xmlview_tree_get_repr_node(tree, parentrepr, &node)) {
        GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(tree->store), &node);
        if (path) {
            gtk_tree_view_expand_row(GTK_TREE_VIEW(tree), path, false);
        }
    }
}

void XmlTree::set_tree_select(Inkscape::XML::Node *repr, bool edit)
{
    if (selected_repr) {
        Inkscape::GC::release(selected_repr);
    }
    selected_repr = repr;
    if (selected_repr) {
        Inkscape::GC::anchor(selected_repr);
    }
    if (auto document = getDocument()) {
        document->setXMLDialogSelectedObject(nullptr);
    }
    if (repr) {
        GtkTreeIter node;

        Inkscape::GC::anchor(selected_repr);

        expand_parents(tree, repr);

        if (sp_xmlview_tree_get_repr_node(SP_XMLVIEW_TREE(tree), repr, &node)) {

            GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
            gtk_tree_selection_unselect_all (selection);

            GtkTreePath* path = gtk_tree_model_get_path(GTK_TREE_MODEL(tree->store), &node);
            gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(tree), path, nullptr, TRUE, 0.66, 0.0);
            gtk_tree_selection_select_iter(selection, &node);
            auto col = gtk_tree_view_get_column(&tree->tree, 0);
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree), path, edit ? col : nullptr, edit);
            gtk_tree_path_free(path);

        } else {
            g_message("XmlTree::set_tree_select : Couldn't find repr node");
        }
    } else {
        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
        gtk_tree_selection_unselect_all (selection);

        on_tree_unselect_row_disable();
    }
    propagate_tree_select(repr);
}



void XmlTree::propagate_tree_select(Inkscape::XML::Node *repr)
{
    if (repr &&
       (repr->type() == Inkscape::XML::NodeType::ELEMENT_NODE ||
        repr->type() == Inkscape::XML::NodeType::TEXT_NODE ||
        repr->type() == Inkscape::XML::NodeType::COMMENT_NODE))
    {
        attributes->setRepr(repr);
    } else {
        attributes->setRepr(nullptr);
    }
}


Inkscape::XML::Node *XmlTree::get_dt_select()
{
    if (auto selection = getSelection()) {
        return selection->singleRepr();
    }
    return nullptr;
}


/**
 * Like SPDesktop::isLayer(), but ignores SPGroup::effectiveLayerMode().
 */
static bool isRealLayer(SPObject const *object)
{
    auto group = cast<SPGroup>(object);
    return group && group->layerMode() == SPGroup::LAYER;
}

void XmlTree::set_dt_select(Inkscape::XML::Node *repr)
{
    auto document = getDocument();
    if (!document)
        return;

    SPObject *object;
    if (repr) {
        while ( ( repr->type() != Inkscape::XML::NodeType::ELEMENT_NODE )
                && repr->parent() )
        {
            repr = repr->parent();
        } // end of while loop

        object = document->getObjectByRepr(repr);
    } else {
        object = nullptr;
    }

    blocked++;

    if (!object || !in_dt_coordsys(*object)) {
        // object not on canvas
    } else if (isRealLayer(object)) {
        getDesktop()->layerManager().setCurrentLayer(object);
    } else {
        if (is<SPGroup>(object->parent)) {
            getDesktop()->layerManager().setCurrentLayer(object->parent);
        }

        getSelection()->set(cast<SPItem>(object));
    }

    document->setXMLDialogSelectedObject(object);
    blocked--;
}

bool XmlTree::deferred_on_tree_select_row()
{
    GtkTreeIter   iter;
    GtkTreeModel *model;

    if (selected_repr) {
        Inkscape::GC::release(selected_repr);
        selected_repr = nullptr;
    }

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));

    if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
        // Nothing selected, update widgets
        propagate_tree_select(nullptr);
        set_dt_select(nullptr);
        on_tree_unselect_row_disable();
        return false;
    }

    Inkscape::XML::Node *repr = sp_xmlview_tree_node_get_repr(model, &iter);
    g_assert(repr != nullptr);


    selected_repr = repr;
    Inkscape::GC::anchor(selected_repr);

    propagate_tree_select(selected_repr);
    set_dt_select(selected_repr);
    on_tree_select_row_enable(&iter);

    return FALSE;
}

void XmlTree::_set_status_message(Inkscape::MessageType /*type*/, const gchar *message, GtkWidget *widget)
{
    if (widget) {
        gtk_label_set_markup(GTK_LABEL(widget), message ? message : "");
    }
}

void XmlTree::on_tree_select_row_enable(GtkTreeIter *node)
{
    if (!node) {
        return;
    }

    Inkscape::XML::Node *repr = sp_xmlview_tree_node_get_repr(GTK_TREE_MODEL(tree->store), node);
    Inkscape::XML::Node *parent=repr->parent();

    //on_tree_select_row_enable_if_mutable
    xml_node_duplicate_button.set_sensitive(xml_tree_node_mutable(node));
    xml_node_delete_button.set_sensitive(xml_tree_node_mutable(node));

    //on_tree_select_row_enable_if_element
    if (repr->type() == Inkscape::XML::NodeType::ELEMENT_NODE) {
        xml_element_new_button.set_sensitive(true);
        xml_text_new_button.set_sensitive(true);

    } else {
        xml_element_new_button.set_sensitive(false);
        xml_text_new_button.set_sensitive(false);
    }

    //on_tree_select_row_enable_if_has_grandparent
    {
        GtkTreeIter parent;
        if (gtk_tree_model_iter_parent(GTK_TREE_MODEL(tree->store), &parent, node)) {
            GtkTreeIter grandparent;
            if (gtk_tree_model_iter_parent(GTK_TREE_MODEL(tree->store), &grandparent, &parent)) {
                unindent_node_button.set_sensitive(true);
            } else {
                unindent_node_button.set_sensitive(false);
            }
        } else {
            unindent_node_button.set_sensitive(false);
        }
    }
    // on_tree_select_row_enable_if_indentable
    gboolean indentable = FALSE;

    if (xml_tree_node_mutable(node)) {
        Inkscape::XML::Node *prev;

        if ( parent && repr != parent->firstChild() ) {
            g_assert(parent->firstChild());

            // skip to the child just before the current repr
            for ( prev = parent->firstChild() ;
                  prev && prev->next() != repr ;
                  prev = prev->next() ){};

            if (prev && (prev->type() == Inkscape::XML::NodeType::ELEMENT_NODE)) {
                indentable = TRUE;
            }
        }
    }

    indent_node_button.set_sensitive(indentable);

    //on_tree_select_row_enable_if_not_first_child
    {
        if ( parent && repr != parent->firstChild() ) {
            raise_node_button.set_sensitive(true);
        } else {
            raise_node_button.set_sensitive(false);
        }
    }

    //on_tree_select_row_enable_if_not_last_child
    {
        if ( parent && (parent->parent() && repr->next())) {
            lower_node_button.set_sensitive(true);
        } else {
            lower_node_button.set_sensitive(false);
        }
    }
}


gboolean XmlTree::xml_tree_node_mutable(GtkTreeIter *node)
{
    // top-level is immutable, obviously
    GtkTreeIter parent;
    if (!gtk_tree_model_iter_parent(GTK_TREE_MODEL(tree->store), &parent, node)) {
        return false;
    }


    // if not in base level (where namedview, defs, etc go), we're mutable
    GtkTreeIter child;
    if (gtk_tree_model_iter_parent(GTK_TREE_MODEL(tree->store), &child, &parent)) {
        return true;
    }

    Inkscape::XML::Node *repr;
    repr = sp_xmlview_tree_node_get_repr(GTK_TREE_MODEL(tree->store), node);
    g_assert(repr);

    // don't let "defs" or "namedview" disappear
    if ( !strcmp(repr->name(),"svg:defs") ||
         !strcmp(repr->name(),"sodipodi:namedview") ) {
        return false;
    }

    // everyone else is okay, I guess.  :)
    return true;
}



void XmlTree::on_tree_unselect_row_disable()
{
    xml_text_new_button.set_sensitive(false);
    xml_element_new_button.set_sensitive(false);
    xml_node_delete_button.set_sensitive(false);
    xml_node_duplicate_button.set_sensitive(false);
    unindent_node_button.set_sensitive(false);
    indent_node_button.set_sensitive(false);
    raise_node_button.set_sensitive(false);
    lower_node_button.set_sensitive(false);
}

void XmlTree::onCreateNameChanged()
{
    Glib::ustring text = name_entry->get_text();
    /* TODO: need to do checking a little more rigorous than this */
    create_button->set_sensitive(!text.empty());
}

void XmlTree::cmd_new_element_node()
{
    auto document = getDocument();
    if (!document)
        return;

    // enable in-place node name editing
    tree->renderer->property_editable() = true;

    auto dummy = ""; // this element has no corresponding SP* object and its construction is silent
    auto xml_doc = document->getReprDoc();
    _dummy = xml_doc->createElement(dummy); // create dummy placeholder so we can have a new temporary row in xml tree
    _node_parent = selected_repr;   // remember where the node is inserted
    selected_repr->appendChild(_dummy);
    set_tree_select(_dummy, true); // enter in-place node name editing
}

void XmlTree::startNodeEditing(Gtk::CellEditable* cell, const Glib::ustring& path)
{
    if (!cell) {
        return;
    }
    // remove dummy element name so user can start with an empty name
    auto entry = dynamic_cast<Gtk::Entry *>(cell);
    entry->get_buffer()->set_text("");
}

void XmlTree::stopNodeEditing(bool ok, const Glib::ustring& path, Glib::ustring element)
{
    tree->renderer->property_editable() = false;

    auto document = getDocument();
    if (!document) {
        return;
    }
    // delete dummy node
    if (_dummy) {
        document->setXMLDialogSelectedObject(nullptr);

        auto parent = _dummy->parent();
        Inkscape::GC::release(_dummy);
        sp_repr_unparent(_dummy);
        if (parent) {
            auto parentobject = document->getObjectByRepr(parent);
            if (parentobject) {
                parentobject->requestDisplayUpdate(SP_OBJECT_CHILD_MODIFIED_FLAG);
            }
        }

        _dummy = nullptr;
    }

    Util::trim(element);
    if (!ok || element.empty() || !_node_parent) {
        return;
    }

    Inkscape::XML::Document* xml_doc = document->getReprDoc();
    // Extract tag name
    {
        static auto const extract_tagname = Glib::Regex::create("^<?\\s*(\\w[\\w:\\-\\d]*)");
        Glib::MatchInfo match_info;
        extract_tagname->match(element, match_info);
        if (!match_info.matches()) {
            return;
        }
        element = match_info.fetch(1);
    }

    // prepend "svg:" namespace if none is given
    if (element.find(':') == Glib::ustring::npos) {
        element = "svg:" + element;
    }
    auto repr = xml_doc->createElement(element.c_str());
    Inkscape::GC::release(repr);
    _node_parent->appendChild(repr);
    set_dt_select(repr);
    set_tree_select(repr, true);
    _node_parent = nullptr;

    DocumentUndo::done(document, Q_("Undo History / XML Editor|Create new element node"), INKSCAPE_ICON("dialog-xml-editor"));
}

void XmlTree::cmd_new_text_node()
{
    auto document = getDocument();
    if (!document)
        return;

    g_assert(selected_repr != nullptr);

    Inkscape::XML::Document *xml_doc = document->getReprDoc();
    Inkscape::XML::Node *text = xml_doc->createTextNode("");
    selected_repr->appendChild(text);

    DocumentUndo::done(document, Q_("Undo History / XML Editor|Create new text node"), INKSCAPE_ICON("dialog-xml-editor"));

    set_tree_select(text);
    set_dt_select(text);
}

void XmlTree::cmd_duplicate_node()
{
    auto document = getDocument();
    if (!document)
        return;

    g_assert(selected_repr != nullptr);

    Inkscape::XML::Node *parent = selected_repr->parent();
    Inkscape::XML::Node *dup = selected_repr->duplicate(parent->document());
    parent->addChild(dup, selected_repr);

    DocumentUndo::done(document, Q_("Undo History / XML Editor|Duplicate node"), INKSCAPE_ICON("dialog-xml-editor"));

    GtkTreeIter node;

    if (sp_xmlview_tree_get_repr_node(SP_XMLVIEW_TREE(tree), dup, &node)) {
        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
        gtk_tree_selection_select_iter(selection, &node);
    }
}

void XmlTree::cmd_delete_node()
{
    auto document = getDocument();
    if (!document)
        return;

    g_assert(selected_repr != nullptr);

    document->setXMLDialogSelectedObject(nullptr);

    Inkscape::XML::Node *parent = selected_repr->parent();

    sp_repr_unparent(selected_repr);

    if (parent) {
        auto parentobject = document->getObjectByRepr(parent);
        if (parentobject) {
            parentobject->requestDisplayUpdate(SP_OBJECT_CHILD_MODIFIED_FLAG);
        }
    }

    DocumentUndo::done(document, Q_("Undo History / XML Editor|Delete node"), INKSCAPE_ICON("dialog-xml-editor"));
}

void XmlTree::cmd_raise_node()
{
    auto document = getDocument();
    if (!document)
        return;

    g_assert(selected_repr != nullptr);

    Inkscape::XML::Node *parent = selected_repr->parent();
    g_return_if_fail(parent != nullptr);
    g_return_if_fail(parent->firstChild() != selected_repr);

    Inkscape::XML::Node *ref = nullptr;
    Inkscape::XML::Node *before = parent->firstChild();
    while (before && (before->next() != selected_repr)) {
        ref = before;
        before = before->next();
    }

    parent->changeOrder(selected_repr, ref);

    DocumentUndo::done(document, Q_("Undo History / XML Editor|Raise node"), INKSCAPE_ICON("dialog-xml-editor"));

    set_tree_select(selected_repr);
    set_dt_select(selected_repr);
}



void XmlTree::cmd_lower_node()
{
    auto document = getDocument();
    if (!document)
        return;

    g_assert(selected_repr != nullptr);

    g_return_if_fail(selected_repr->next() != nullptr);
    Inkscape::XML::Node *parent = selected_repr->parent();

    parent->changeOrder(selected_repr, selected_repr->next());

    DocumentUndo::done(document, Q_("Undo History / XML Editor|Lower node"), INKSCAPE_ICON("dialog-xml-editor"));

    set_tree_select(selected_repr);
    set_dt_select(selected_repr);
}

void XmlTree::cmd_indent_node()
{
    auto document = getDocument();
    if (!document)
        return;

    Inkscape::XML::Node *repr = selected_repr;
    g_assert(repr != nullptr);

    Inkscape::XML::Node *parent = repr->parent();
    g_return_if_fail(parent != nullptr);
    g_return_if_fail(parent->firstChild() != repr);

    Inkscape::XML::Node* prev = parent->firstChild();
    while (prev && (prev->next() != repr)) {
        prev = prev->next();
    }
    g_return_if_fail(prev != nullptr);
    g_return_if_fail(prev->type() == Inkscape::XML::NodeType::ELEMENT_NODE);

    Inkscape::XML::Node* ref = nullptr;
    if (prev->firstChild()) {
        for( ref = prev->firstChild() ; ref->next() ; ref = ref->next() ){};
    }

    parent->removeChild(repr);
    prev->addChild(repr, ref);

    DocumentUndo::done(document, Q_("Undo History / XML Editor|Indent node"), INKSCAPE_ICON("dialog-xml-editor"));
    set_tree_select(repr);
    set_dt_select(repr);

} // end of cmd_indent_node()



void XmlTree::cmd_unindent_node()
{
    auto document = getDocument();
    if (!document)
        return;

    Inkscape::XML::Node *repr = selected_repr;
    g_assert(repr != nullptr);

    Inkscape::XML::Node *parent = repr->parent();
    g_return_if_fail(parent);
    Inkscape::XML::Node *grandparent = parent->parent();
    g_return_if_fail(grandparent);

    parent->removeChild(repr);
    grandparent->addChild(repr, parent);

    DocumentUndo::done(document, Q_("Undo History / XML Editor|Unindent node"), INKSCAPE_ICON("dialog-xml-editor"));

    set_tree_select(repr);
    set_dt_select(repr);

} // end of cmd_unindent_node()

/** Returns true iff \a item is suitable to be included in the selection, in particular
    whether it has a bounding box in the desktop coordinate system for rendering resize handles.

    Descendents of <defs> nodes (markers etc.) return false, for example.
*/
bool XmlTree::in_dt_coordsys(SPObject const &item)
{
    /* Definition based on sp_item_i2doc_affine. */
    SPObject const *child = &item;
    while (is<SPItem>(child)) {
        SPObject const * const parent = child->parent;
        if (parent == nullptr) {
            g_assert(is<SPRoot>(child));
            if (child == &item) {
                // item is root
                return false;
            }
            return true;
        }
        child = parent;
    }
    g_assert(!is<SPRoot>(child));
    return false;
}

void XmlTree::desktopReplaced() {
    // subdialog does not receive desktopReplace calls, we need to propagate desktop change
    if (attributes) {
        attributes->setDesktop(getDesktop());
    }
}

void XmlTree::setSyntaxStyle(Inkscape::UI::Syntax::XMLStyles const &new_style)
{
    tree->formatter->setStyle(new_style);
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
