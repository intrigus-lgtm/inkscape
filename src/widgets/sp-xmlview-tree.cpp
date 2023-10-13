// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * Specialization of GtkTreeView for the XML tree view
 *
 * Authors:
 *   MenTaLguY <mental@rydia.net>
 *
 * Copyright (C) 2002 MenTaLguY
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "sp-xmlview-tree.h"

#include <cstring>
#include <glibmm/markup.h>
#include <glibmm/property.h>
#include <glibmm/ustring.h>
#include <gmodule.h>
#include <gtkmm/cellrenderer.h>
#include <gtkmm/cellrenderertext.h>
#include <memory>
#include <string>

#include "ui/syntax.h"
#include "xml/node-observer.h"
#include "xml/node.h"

namespace {

struct NodeData
{
    SPXMLViewTree *tree;
    GtkTreeRowReference *rowref;
    Inkscape::XML::Node *repr;
    bool expanded = false; //< true if tree view has been expanded to this node
    bool dragging = false;
    std::unique_ptr<Inkscape::XML::NodeObserver> observer;

    NodeData(SPXMLViewTree *tree, GtkTreeIter *node, Inkscape::XML::Node *repr);
    ~NodeData();
};

// currently dragged node
Inkscape::XML::Node *dragging_repr = nullptr;

} // namespace

enum { STORE_TEXT_COL = 0, STORE_DATA_COL, STORE_MARKUP_COL, STORE_N_COLS };

static void sp_xmlview_tree_destroy(GtkWidget * object);

static NodeData *sp_xmlview_tree_node_get_data(GtkTreeModel *model, GtkTreeIter *iter);

static void add_node(SPXMLViewTree *tree, GtkTreeIter *parent, GtkTreeIter *before, Inkscape::XML::Node *repr);

static gboolean ref_to_sibling (NodeData *node, Inkscape::XML::Node * ref, GtkTreeIter *);
static gboolean repr_to_child (NodeData *node, Inkscape::XML::Node * repr, GtkTreeIter *);
static GtkTreeRowReference *tree_iter_to_ref(SPXMLViewTree *, GtkTreeIter *);
static gboolean tree_ref_to_iter (SPXMLViewTree * tree, GtkTreeIter* iter, GtkTreeRowReference  *ref);

static gboolean search_equal_func(GtkTreeModel *, gint column, const gchar *key, GtkTreeIter *, gpointer search_data);
static gboolean foreach_func(GtkTreeModel *, GtkTreePath *, GtkTreeIter *, gpointer user_data);

static void on_row_changed(GtkTreeModel *, GtkTreePath *, GtkTreeIter *, gpointer user_data);
static void on_drag_begin(GtkWidget *, GdkDragContext *, gpointer userdata);
static void on_drag_end(GtkWidget *, GdkDragContext *, gpointer userdata);
static gboolean do_drag_motion(GtkWidget *, GdkDragContext *, gint x, gint y, guint time, gpointer user_data);

static bool get_first_child(NodeData *data, GtkTreeIter *child_iter);
static void remove_dummy_rows(GtkTreeStore *store, GtkTreeIter *iter);
static void sp_remove_newlines_and_tabs(std::string &val, size_t const maxlen = 200);

namespace {

static auto null_to_empty(char const *str)
{
    return str ? str : "";
}

class ElementNodeObserver final : public Inkscape::XML::NodeObserver
{
    NodeData *_nodedata;

public:
    ElementNodeObserver(NodeData *nodedata)
        : _nodedata(nodedata)
    {}

    void notifyChildAdded(Inkscape::XML::Node&, Inkscape::XML::Node &child_, Inkscape::XML::Node *ref) override
    {
        GtkTreeIter before;
        Inkscape::XML::Node *child = &child_;

        if (_nodedata->tree->blocked) return;

        if (!ref_to_sibling (_nodedata, ref, &before)) {
            return;
        }

        GtkTreeIter data_iter;
        tree_ref_to_iter(_nodedata->tree, &data_iter,  _nodedata->rowref);

        if (!_nodedata->expanded) {
            auto model = GTK_TREE_MODEL(_nodedata->tree->store);
            GtkTreeIter childiter;
            if (!gtk_tree_model_iter_children(model, &childiter, &data_iter)) {
                // no children yet, add a dummy
                child = nullptr;
            } else if (sp_xmlview_tree_node_get_repr(model, &childiter) == nullptr) {
                // already has a dummy child
                return;
            }
        }

        add_node(_nodedata->tree, &data_iter, &before, child);
    }

    void notifyAttributeChanged(Inkscape::XML::Node &node, GQuark key_, Inkscape::Util::ptr_shared,
                                Inkscape::Util::ptr_shared) override
    {
        auto const key = g_quark_to_string(key_);
        if (std::strcmp(key, "id") != 0 && std::strcmp(key, "inkscape:label") != 0)
            return;
        elementAttrOrNameChangedUpdate(&node);
    }

    void notifyElementNameChanged(Inkscape::XML::Node &node, GQuark, GQuark) override
    {
        elementAttrOrNameChangedUpdate(&node);
    }

    void notifyChildOrderChanged(Inkscape::XML::Node &, Inkscape::XML::Node &child, Inkscape::XML::Node *,
                                 Inkscape::XML::Node *newref) override
    {
        GtkTreeIter before, node;

        if (_nodedata->tree->blocked)
            return;

        ref_to_sibling(_nodedata, newref, &before);
        repr_to_child(_nodedata, &child, &node);

        if (gtk_tree_store_iter_is_valid(_nodedata->tree->store, &before)) {
            gtk_tree_store_move_before(_nodedata->tree->store, &node, &before);
        } else {
            repr_to_child(_nodedata, newref, &before);
            gtk_tree_store_move_after(_nodedata->tree->store, &node, &before);
        }
    }

    void notifyChildRemoved(Inkscape::XML::Node &repr, Inkscape::XML::Node &child, Inkscape::XML::Node *) override
    {
        if (_nodedata->tree->blocked)
            return;

        GtkTreeIter iter;
        if (repr_to_child(_nodedata, &child, &iter)) {
            delete sp_xmlview_tree_node_get_data(GTK_TREE_MODEL(_nodedata->tree->store), &iter);
            gtk_tree_store_remove(_nodedata->tree->store, &iter);
        } else if (!repr.firstChild() && get_first_child(_nodedata, &iter)) {
            // remove dummy when all children gone
            remove_dummy_rows(_nodedata->tree->store, &iter);
        } else {
            return;
        }

#ifndef GTK_ISSUE_2510_IS_FIXED
        // https://gitlab.gnome.org/GNOME/gtk/issues/2510
        gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(_nodedata->tree)));
#endif
    }

    void elementAttrOrNameChangedUpdate(Inkscape::XML::Node *repr)
    {
        if (_nodedata->tree->blocked) {
            return;
        }

        auto node_name = Glib::ustring(null_to_empty(repr->name()));
        auto pos = node_name.find("svg:");
        if (pos != Glib::ustring::npos) {
            // Do not decorate element names the with default namespace "svg"; it is just visual noise.
            node_name.erase(pos, 4);
        }

        // Build a plain-text and a markup-enabled representation of the node.
        auto &formatter = *_nodedata->tree->formatter;
        auto display_text = Glib::ustring::compose("<%1", node_name);
        formatter.openTag(node_name.c_str());

        if (auto id_value = repr->attribute("id")) {
            display_text += Glib::ustring::compose(" id=\"%1\"", id_value);
            formatter.addAttribute("id", id_value);
        }
        if (auto label_value = repr->attribute("inkscape:label")) {
            display_text += Glib::ustring::compose(" inkscape:label=\"%1\"", label_value);
            formatter.addAttribute("inkscape:label", label_value);
        }
        display_text += ">";
        auto markup = formatter.finishTag();

        GtkTreeIter iter;
        if (tree_ref_to_iter(_nodedata->tree, &iter, _nodedata->rowref)) {
            gtk_tree_store_set(GTK_TREE_STORE(_nodedata->tree->store), &iter, STORE_TEXT_COL, display_text.c_str(), -1);
            gtk_tree_store_set(GTK_TREE_STORE(_nodedata->tree->store), &iter, STORE_MARKUP_COL, markup.c_str(), -1);
        }
    }
};

class TextNodeObserver final : public Inkscape::XML::NodeObserver
{
    NodeData *_nodedata;

public:
    TextNodeObserver(NodeData *nodedata)
        : _nodedata(nodedata)
    {}

    void notifyContentChanged(Inkscape::XML::Node &, Inkscape::Util::ptr_shared,
                              Inkscape::Util::ptr_shared new_content) override
    {
        if (_nodedata->tree->blocked)
            return;

        auto text_content = std::string("\"").append(null_to_empty(new_content.pointer())).append("\"");
        sp_remove_newlines_and_tabs(text_content);

        auto &formatter = *_nodedata->tree->formatter;
        auto markup = formatter.formatContent(text_content.c_str(), false);

        GtkTreeIter iter;
        if (tree_ref_to_iter(_nodedata->tree, &iter, _nodedata->rowref)) {
            gtk_tree_store_set(GTK_TREE_STORE(_nodedata->tree->store), &iter, STORE_TEXT_COL, text_content.c_str(), -1);
            gtk_tree_store_set(GTK_TREE_STORE(_nodedata->tree->store), &iter, STORE_MARKUP_COL, markup.c_str(), -1);
        }
    }
};

class CommentNodeObserver final : public Inkscape::XML::NodeObserver
{
    NodeData *_nodedata;

public:
    CommentNodeObserver(NodeData *nodedata)
        : _nodedata(nodedata)
    {}
    void notifyContentChanged(Inkscape::XML::Node &, Inkscape::Util::ptr_shared,
                              Inkscape::Util::ptr_shared new_content) override
    {
        if (_nodedata->tree->blocked)
            return;

        auto comment = std::string("<!--").append(null_to_empty(new_content.pointer())).append("-->");
        sp_remove_newlines_and_tabs(comment);

        auto &formatter = *_nodedata->tree->formatter;
        auto markup = formatter.formatComment(comment.c_str(), false);

        GtkTreeIter iter;
        if (tree_ref_to_iter(_nodedata->tree, &iter, _nodedata->rowref)) {
            gtk_tree_store_set(GTK_TREE_STORE(_nodedata->tree->store), &iter, STORE_TEXT_COL, comment.c_str(), -1);
            gtk_tree_store_set(GTK_TREE_STORE(_nodedata->tree->store), &iter, STORE_MARKUP_COL, markup.c_str(), -1);
        }
    }
};

class PINodeObserver final : public Inkscape::XML::NodeObserver
{
    NodeData *_nodedata;

public:
    PINodeObserver(NodeData *nodedata)
        : _nodedata(nodedata)
    {}
    void notifyContentChanged(Inkscape::XML::Node &repr, Inkscape::Util::ptr_shared,
                              Inkscape::Util::ptr_shared new_content) override
    {
        if (_nodedata->tree->blocked)
            return;

        auto processing_instr = std::string("<?").append(repr.name()).append(" ").append(null_to_empty(new_content.pointer())).append("?>");
        sp_remove_newlines_and_tabs(processing_instr);

        auto &formatter = *_nodedata->tree->formatter;
        auto markup = formatter.formatProlog(processing_instr.c_str());

        GtkTreeIter iter;
        if (tree_ref_to_iter(_nodedata->tree, &iter, _nodedata->rowref)) {
            gtk_tree_store_set(GTK_TREE_STORE(_nodedata->tree->store), &iter, STORE_TEXT_COL, processing_instr.c_str(), -1);
            gtk_tree_store_set(GTK_TREE_STORE(_nodedata->tree->store), &iter, STORE_MARKUP_COL, markup.c_str(), -1);
        }
    }
};

} // namespace

/**
 * Get an iterator to the first child of `data`
 * @param data handle which references a row
 * @param[out] child_iter On success: valid iterator to first child
 * @return False if the node has no children
 */
static bool get_first_child(NodeData *data, GtkTreeIter *child_iter)
{
    GtkTreeIter iter;
    return tree_ref_to_iter(data->tree, &iter, data->rowref) &&
           gtk_tree_model_iter_children(GTK_TREE_MODEL(data->tree->store), child_iter, &iter);
}

/**
 * @param iter First dummy row on that level
 * @pre all rows on the same level are dummies
 * @pre iter is valid
 * @post iter is invalid
 * @post level is empty
 */
static void remove_dummy_rows(GtkTreeStore *store, GtkTreeIter *iter)
{
    do {
        g_assert(nullptr == sp_xmlview_tree_node_get_data(GTK_TREE_MODEL(store), iter));
        gtk_tree_store_remove(store, iter);
    } while (gtk_tree_store_iter_is_valid(store, iter));
}

static gboolean on_test_expand_row( //
    GtkTreeView *tree_view,         //
    GtkTreeIter *iter,              //
    GtkTreePath *path,              //
    gpointer)
{
    auto tree = SP_XMLVIEW_TREE(tree_view);
    auto model = GTK_TREE_MODEL(tree->store);

    GtkTreeIter childiter;
    bool has_children = gtk_tree_model_iter_children(model, &childiter, iter);
    g_assert(has_children);

    if (sp_xmlview_tree_node_get_repr(model, &childiter) == nullptr) {
        NodeData *data = sp_xmlview_tree_node_get_data(model, iter);

        remove_dummy_rows(tree->store, &childiter);

        // insert real rows
        data->expanded = true;
        ElementNodeObserver e(data);
        data->repr->synthesizeEvents(e);
    }

    return false;
}

/** Node name renderer for XML tree.
 *  It knows to use markup, but falls back to plain text for selected nodes.
 */
class NodeRenderer : public Gtk::CellRendererText {
public:
    NodeRenderer():
        Glib::ObjectBase(typeid(CellRendererText)),
        Gtk::CellRendererText(),
        _property_plain_text(*this, "plain", "-") {}

    // "text" and "markup" properties from CellRendererText are in use for marked up text;
    // we need a separate property for plain text (placeholder_text could be hijacked potentially)
    Glib::Property<Glib::ustring> _property_plain_text;

    void render_vfunc(const Cairo::RefPtr<Cairo::Context>& ctx,
                      Gtk::Widget& widget,
                      const Gdk::Rectangle& background_area,
                      const Gdk::Rectangle& cell_area,
                      Gtk::CellRendererState flags) override {
        if (flags & Gtk::CellRendererState::SELECTED) {
            // use plain text instead of marked up text to render selected nodes, so they are legible
            property_text() = _property_plain_text.get_value();
        }
        Gtk::CellRendererText::render_vfunc(ctx, widget, background_area, cell_area, flags);
    }
};

GtkWidget *sp_xmlview_tree_new(Inkscape::XML::Node * repr, void * /*factory*/, void * /*data*/)
{
    SPXMLViewTree *tree = SP_XMLVIEW_TREE(g_object_new (SP_TYPE_XMLVIEW_TREE, nullptr));
    tree->_tree_move = new sigc::signal<void ()>();

    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW(tree), FALSE);
    gtk_tree_view_set_reorderable (GTK_TREE_VIEW(tree), TRUE);
    gtk_tree_view_set_enable_search (GTK_TREE_VIEW(tree), TRUE);
    gtk_tree_view_set_search_equal_func (GTK_TREE_VIEW(tree), search_equal_func, nullptr, nullptr);

    auto r = new NodeRenderer();
    tree->renderer = r;

    GtkCellRenderer* renderer = r->Gtk::CellRenderer::gobj();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("", renderer,
        "markup", STORE_MARKUP_COL, // marked up text for decorated color output
        "plain", STORE_TEXT_COL,    // plain text for searching and rendering selected nodes
        nullptr);
    gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);
    gtk_cell_renderer_set_padding (renderer, 2, 0);
    gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

    sp_xmlview_tree_set_repr (tree, repr);

    g_signal_connect(GTK_TREE_VIEW(tree), "drag-begin", G_CALLBACK(on_drag_begin), tree);
    g_signal_connect(GTK_TREE_VIEW(tree), "drag-end", G_CALLBACK(on_drag_end), tree);
    g_signal_connect(GTK_TREE_VIEW(tree), "drag-motion",  G_CALLBACK(do_drag_motion), tree);
    g_signal_connect(GTK_TREE_VIEW(tree), "test-expand-row", G_CALLBACK(on_test_expand_row), nullptr);

    tree->formatter = new Inkscape::UI::Syntax::XMLFormatter();

    return GTK_WIDGET(tree);
}

G_DEFINE_TYPE(SPXMLViewTree, sp_xmlview_tree, GTK_TYPE_TREE_VIEW);

void sp_xmlview_tree_class_init(SPXMLViewTreeClass * klass)
{
    auto widget_class = GTK_WIDGET_CLASS(klass);
    widget_class->destroy = sp_xmlview_tree_destroy;
}

void
sp_xmlview_tree_init (SPXMLViewTree * tree)
{
	tree->repr = nullptr;
	tree->blocked = 0;
}

void sp_xmlview_tree_destroy(GtkWidget * object)
{
	SPXMLViewTree * tree = SP_XMLVIEW_TREE (object);

    delete tree->renderer;
    tree->renderer = nullptr;
    delete tree->formatter;
    tree->formatter = nullptr;
    delete tree->_tree_move;
    tree->_tree_move = nullptr;

	sp_xmlview_tree_set_repr (tree, nullptr);

	GTK_WIDGET_CLASS(sp_xmlview_tree_parent_class)->destroy (object);
}

/*
 * Add a new row to the tree
 */
void
add_node (SPXMLViewTree * tree, GtkTreeIter *parent, GtkTreeIter *before, Inkscape::XML::Node * repr)
{
	g_assert (tree != nullptr);

    if (before && !gtk_tree_store_iter_is_valid(tree->store, before)) {
        before = nullptr;
    }

	GtkTreeIter iter;
    gtk_tree_store_insert_before (tree->store, &iter, parent, before);

    if (!gtk_tree_store_iter_is_valid(tree->store, &iter)) {
        return;
    }

    if (!repr) {
        // no need to store any data
        return;
    }

    auto data = new NodeData(tree, &iter, repr);

    g_assert(data != nullptr);

    gtk_tree_store_set(tree->store, &iter, STORE_DATA_COL, data, -1);

    if (repr->type() == Inkscape::XML::NodeType::TEXT_NODE) {
        data->observer = std::make_unique<TextNodeObserver>(data);
    } else if (repr->type() == Inkscape::XML::NodeType::COMMENT_NODE) {
        data->observer = std::make_unique<CommentNodeObserver>(data);
    } else if (repr->type() == Inkscape::XML::NodeType::PI_NODE) {
        data->observer = std::make_unique<PINodeObserver>(data);
    } else if (repr->type() == Inkscape::XML::NodeType::ELEMENT_NODE) {
        data->observer = std::make_unique<ElementNodeObserver>(data);
    }

    if (data->observer) {
        /* cheat a little to get the text updated on nodes without id */
        if (repr->type() == Inkscape::XML::NodeType::ELEMENT_NODE && repr->attribute("id") == nullptr) {
            data->observer->notifyAttributeChanged(*repr, g_quark_from_static_string("id"),
                                                   Inkscape::Util::ptr_shared(), Inkscape::Util::ptr_shared());
        }
        repr->addObserver(*data->observer);
        repr->synthesizeEvents(*data->observer);
    }
}

static gboolean remove_all_listeners(GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer)
{
    NodeData *data = sp_xmlview_tree_node_get_data(model, iter);
    delete data;
    return false;
}

NodeData::NodeData(SPXMLViewTree *tree, GtkTreeIter *iter, Inkscape::XML::Node *repr)
    : tree(tree)
    , rowref(tree_iter_to_ref(tree, iter))
    , repr(repr)
{
    if (repr) {
        Inkscape::GC::anchor(repr);
    }
}

NodeData::~NodeData()
{
    if (repr) {
        if (observer) {
            repr->removeObserver(*observer);
        }
        Inkscape::GC::release(repr);
    }
    gtk_tree_row_reference_free(rowref);
}

/**
 * Truncate `val` to `maxlen` unicode characters and replace newlines and tabs
 * with placeholder symbols. The string is modified in place.
 * @param[in,out] val String in UTF-8 encoding
 */
static void sp_remove_newlines_and_tabs(std::string &val, size_t const maxlen)
{
    if (g_utf8_strlen(val.data(), maxlen * 2) > maxlen) {
        size_t newlen = g_utf8_offset_to_pointer(val.data(), maxlen - 3) - val.data();
        val.resize(newlen);
        val.append("…");
    }

    struct
    {
        const char *query;
        const char *replacement;
    } replacements[] = {
        {"\r\n", "⏎"},
        {"\n", "⏎"},
        {"\t", "⇥"},
    };

    for (auto const &item : replacements) {
        for (size_t pos = 0; (pos = val.find(item.query, pos)) != std::string::npos;) {
            val.replace(pos, strlen(item.query), item.replacement);
        }
    }
}

/*
 * Save the source path on drag start, will need it in on_row_changed() when moving a row
 */
void on_drag_begin(GtkWidget *, GdkDragContext *, gpointer userdata)
{
    SPXMLViewTree *tree = static_cast<SPXMLViewTree *>(userdata);
    if (!tree) {
        return;
    }

    GtkTreeModel *model = nullptr;
    GtkTreeIter iter;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        NodeData *data = sp_xmlview_tree_node_get_data(model, &iter);
        if (data) {
            data->dragging = true;
            dragging_repr = data->repr;
        }
    }
}

/**
 * Finalize what happened in `on_row_changed` and clean up what was set up in `on_drag_begin`
 */
void on_drag_end(GtkWidget *, GdkDragContext *, gpointer userdata)
{
    if (!dragging_repr)
        return;

    auto tree = static_cast<SPXMLViewTree *>(userdata);
    auto selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    bool failed = false;

    GtkTreeIter iter;
    if (sp_xmlview_tree_get_repr_node(tree, dragging_repr, &iter)) {
        NodeData *data = sp_xmlview_tree_node_get_data(GTK_TREE_MODEL(tree->store), &iter);

        if (data && data->dragging) {
            // dragging flag was not cleared in `on_row_changed`, this indicates a failed drag
            data->dragging = false;
            failed = true;
        } else {
            // Reselect the dragged row
            gtk_tree_selection_select_iter(selection, &iter);
        }
    } else {
#ifndef GTK_ISSUE_2510_IS_FIXED
        // https://gitlab.gnome.org/GNOME/gtk/issues/2510
        gtk_tree_selection_unselect_all(selection);
#endif
    }

    dragging_repr = nullptr;

    if (!failed) {
        // Signal that a drag and drop has completed successfully
        tree->_tree_move->emit();
    }
}

/*
 * Main drag & drop function
 * Get the old and new paths, and change the Inkscape::XML::Node repr's
 */
void on_row_changed(GtkTreeModel *tree_model, GtkTreePath *path, GtkTreeIter *iter, gpointer user_data)
{
    NodeData *data = sp_xmlview_tree_node_get_data(tree_model, iter);

    if (!data || !data->dragging) {
        return;
    }
    data->dragging = false;

    SPXMLViewTree *tree = SP_XMLVIEW_TREE(user_data);

    gtk_tree_row_reference_free(data->rowref);
    data->rowref = tree_iter_to_ref(tree, iter);

    GtkTreeIter new_parent;
    if (!gtk_tree_model_iter_parent(tree_model, &new_parent, iter)) {
        //No parent of drop location
        return;
    }

    Inkscape::XML::Node *repr = sp_xmlview_tree_node_get_repr(tree_model, iter);
    Inkscape::XML::Node *before_repr = nullptr;

    // Find the sibling node before iter
    GtkTreeIter before_iter = *iter;
    if (gtk_tree_model_iter_previous(tree_model, &before_iter)) {
        before_repr = sp_xmlview_tree_node_get_repr(tree_model, &before_iter);
    }

    // Drop onto oneself causes assert in changeOrder() below, ignore
    if (repr == before_repr)
        return;

    auto repr_old_parent = repr->parent();
    auto repr_new_parent = sp_xmlview_tree_node_get_repr(tree_model, &new_parent);

    tree->blocked++;

    if (repr_old_parent == repr_new_parent) {
        repr_old_parent->changeOrder(repr, before_repr);
    } else {
        repr_old_parent->removeChild(repr);
        repr_new_parent->addChild(repr, before_repr);
    }

    NodeData *data_new_parent = sp_xmlview_tree_node_get_data(tree_model, &new_parent);
    if (data_new_parent && data_new_parent->expanded) {
        // Reselect the dragged row in `on_drag_end` instead of here, because of
        // https://gitlab.gnome.org/GNOME/gtk/-/issues/2510
    } else {
        // convert to dummy node
        delete data;
        gtk_tree_store_set(tree->store, iter, STORE_DATA_COL, nullptr, -1);
    }

    tree->blocked--;
}

/*
 * Set iter to ref or node data's child with the same repr or first child
 */
gboolean ref_to_sibling (NodeData *data, Inkscape::XML::Node *repr, GtkTreeIter *iter)
{
	if (repr) {
		if (!repr_to_child (data, repr, iter)) {
            return false;
        }
        gtk_tree_model_iter_next (GTK_TREE_MODEL(data->tree->store), iter);
	} else {
	    GtkTreeIter data_iter;
	    if (!tree_ref_to_iter(data->tree, &data_iter,  data->rowref)) {
	        return false;
	    }
	    gtk_tree_model_iter_children(GTK_TREE_MODEL(data->tree->store), iter, &data_iter);
	}
	return true;
}

/*
 * Set iter to the node data's child with the same repr
 */
gboolean repr_to_child (NodeData *data, Inkscape::XML::Node * repr, GtkTreeIter *iter)
{
    GtkTreeIter data_iter;
    GtkTreeModel *model = GTK_TREE_MODEL(data->tree->store);
    gboolean valid = false;

    if (!tree_ref_to_iter(data->tree, &data_iter, data->rowref)) {
        return false;
    }

    /*
     * The node we are looking for is likely to be the last one, so check it first.
     */
    gint n_children = gtk_tree_model_iter_n_children (model, &data_iter);
    if (n_children > 1) {
        valid = gtk_tree_model_iter_nth_child (model, iter, &data_iter, n_children-1);
        if (valid && sp_xmlview_tree_node_get_repr (model, iter) == repr) {
            //g_message("repr_to_child hit %d", n_children);
            return valid;
        }
    }

    valid = gtk_tree_model_iter_children(model, iter, &data_iter);
    while (valid && sp_xmlview_tree_node_get_repr (model, iter) != repr) {
        valid = gtk_tree_model_iter_next(model, iter);
	}

    return valid;
}

/*
 * Get a matching GtkTreeRowReference for a GtkTreeIter
 */
GtkTreeRowReference  *tree_iter_to_ref (SPXMLViewTree * tree, GtkTreeIter* iter)
{
    GtkTreePath* path = gtk_tree_model_get_path(GTK_TREE_MODEL(tree->store), iter);
    GtkTreeRowReference  *ref = gtk_tree_row_reference_new(GTK_TREE_MODEL(tree->store), path);
    gtk_tree_path_free(path);

    return ref;
}

/*
 * Get a matching GtkTreeIter for a GtkTreeRowReference
 */
gboolean tree_ref_to_iter (SPXMLViewTree * tree, GtkTreeIter* iter, GtkTreeRowReference  *ref)
{
    GtkTreePath* path = gtk_tree_row_reference_get_path(ref);
    if (!path) {
        return false;
    }
    gboolean const valid = //
        gtk_tree_model_get_iter(GTK_TREE_MODEL(tree->store), iter, path);
    gtk_tree_path_free(path);

    return valid;
}

/*
 * Disable drag and drop target on : root node and non-element nodes
 */
gboolean do_drag_motion(GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time, gpointer user_data)
{
    GtkTreePath *path = nullptr;
    GtkTreeViewDropPosition pos;
    gtk_tree_view_get_dest_row_at_pos (GTK_TREE_VIEW(widget), x, y, &path, &pos);

    int action = 0;

    if (!dragging_repr) {
        goto finally;
    }

    if (path) {
        SPXMLViewTree *tree = SP_XMLVIEW_TREE(user_data);
        GtkTreeIter iter;
        gtk_tree_model_get_iter(GTK_TREE_MODEL(tree->store), &iter, path);
        auto repr = sp_xmlview_tree_node_get_repr(GTK_TREE_MODEL(tree->store), &iter);

        bool const drop_into = pos != GTK_TREE_VIEW_DROP_BEFORE && //
                               pos != GTK_TREE_VIEW_DROP_AFTER;

        // 0. don't drop on self (also handled by on_row_changed but nice to not have drop highlight for it)
        if (repr == dragging_repr) {
            goto finally;
        }

        // 1. only xml elements can have children
        if (drop_into && repr->type() != Inkscape::XML::NodeType::ELEMENT_NODE) {
            goto finally;
        }

        // 3. elements must be at least children of the root <svg:svg> element
        if (gtk_tree_path_get_depth(path) < 2) {
            goto finally;
        }

        // 4. drag node specific limitations
        {
            // nodes which can't be re-parented (because the document holds pointers to them which must stay valid)
            static GQuark const CODE_sodipodi_namedview = g_quark_from_static_string("sodipodi:namedview");
            static GQuark const CODE_svg_defs = g_quark_from_static_string("svg:defs");

            bool const no_reparenting = dragging_repr->code() == CODE_sodipodi_namedview || //
                                        dragging_repr->code() == CODE_svg_defs;

            if (no_reparenting && (drop_into || dragging_repr->parent() != repr->parent())) {
                goto finally;
            }
        }

        action = GDK_ACTION_MOVE;
    }

finally:
    if (action == 0) {
        // remove drop highlight
        gtk_tree_view_set_drag_dest_row(GTK_TREE_VIEW(widget), nullptr, pos /* ignored */);
    }

    gtk_tree_path_free(path);
    gdk_drag_status (context, (GdkDragAction)action, time);

    return (action == 0);
}

/*
 * Set the tree selection and scroll to the row with the given repr
 */
void
sp_xmlview_tree_set_repr (SPXMLViewTree * tree, Inkscape::XML::Node * repr)
{
    if ( tree->repr == repr ) return;

    if (tree->store) {
        gtk_tree_view_set_model(GTK_TREE_VIEW(tree), nullptr);
        gtk_tree_model_foreach(GTK_TREE_MODEL(tree->store), remove_all_listeners, nullptr);
        g_object_unref(tree->store);
        tree->store = nullptr;
    }

    if (tree->repr) {
        Inkscape::GC::release(tree->repr);
    }
    tree->repr = repr;
    if (repr) {
        tree->store = gtk_tree_store_new(STORE_N_COLS, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_STRING);

        Inkscape::GC::anchor(repr);
        add_node(tree, nullptr, nullptr, repr);

        // Set the tree model here, after all data is inserted
        gtk_tree_view_set_model (GTK_TREE_VIEW(tree), GTK_TREE_MODEL(tree->store));
        g_signal_connect(G_OBJECT(tree->store), "row-changed", G_CALLBACK(on_row_changed), tree);

        GtkTreePath *path = gtk_tree_path_new_from_indices(0, -1);
        gtk_tree_view_expand_to_path (GTK_TREE_VIEW(tree), path);
        gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW(tree), path, nullptr, true, 0.5, 0.0);
        gtk_tree_path_free(path);
    }
}

/*
 * Return the node data at a given GtkTreeIter position
 */
NodeData *sp_xmlview_tree_node_get_data(GtkTreeModel *model, GtkTreeIter *iter)
{
    NodeData *data = nullptr;
    gtk_tree_model_get(model, iter, STORE_DATA_COL, &data, -1);
    return data;
}

/*
 * Return the repr at a given GtkTreeIter position
 */
Inkscape::XML::Node *
sp_xmlview_tree_node_get_repr (GtkTreeModel *model, GtkTreeIter * iter)
{
    NodeData *data = sp_xmlview_tree_node_get_data(model, iter);
    return data ? data->repr : nullptr;
}

struct IterByReprData {
    const Inkscape::XML::Node *repr; //< in
    GtkTreeIter *iter;               //< out
};

/*
 * Find a GtkTreeIter position in the tree by repr
 * @return True if the node was found
 */
gboolean
sp_xmlview_tree_get_repr_node (SPXMLViewTree * tree, Inkscape::XML::Node * repr, GtkTreeIter *iter)
{
    iter->stamp = 0; // invalidate iterator
    IterByReprData funcdata = { repr, iter };
    gtk_tree_model_foreach(GTK_TREE_MODEL(tree->store), foreach_func, &funcdata);
    return iter->stamp != 0;
}

gboolean foreach_func(GtkTreeModel *model, GtkTreePath * /*path*/, GtkTreeIter *iter, gpointer user_data)
{
    auto funcdata = static_cast<IterByReprData *>(user_data);
    if (sp_xmlview_tree_node_get_repr(model, iter) == funcdata->repr) {
        *funcdata->iter = *iter;
        return TRUE;
    }

    return FALSE;
}

/*
 * Callback function for string searches in the tree
 * Return a match on any substring
 */
gboolean search_equal_func(GtkTreeModel *model, gint /*column*/, const gchar *key, GtkTreeIter *iter, gpointer /*search_data*/)
{
    gchar *text = nullptr;
    gtk_tree_model_get(model, iter, STORE_TEXT_COL, &text, -1);

    gboolean match = (strstr(text, key) != nullptr);

    g_free(text);

    return !match;
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=4:softtabstop=4:fileencoding=utf-8 :
