// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * System-wide clipboard management - implementation.
 *//*
 * Authors:
 * see git history
 *   Krzysztof Kosiński <tweenk@o2.pl>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Incorporates some code from selection-chemistry.cpp, see that file for more credits.
 *   Abhishek Sharma
 *   Tavmjong Bah
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "clipboard.h"

#include <giomm/application.h>
#include <glib/gstdio.h> // for g_file_set_contents etc., used in _onGet and paste
#include <glibmm/i18n.h>
#include <gdkmm/clipboard.h>

#include <2geom/transforms.h>
#include <2geom/path-sink.h>
#include <iostream>
#include <string>
#include <boost/bimap.hpp>

// TODO: reduce header bloat if possible

#include "context-fns.h"
#include "desktop-style.h" // for sp_desktop_set_style, used in _pasteStyle
#include "desktop.h"
#include "document.h"
#include "file.h" // for file_import, used in _pasteImage
#include "filter-chemistry.h"
#include "gradient-drag.h"
#include "id-clash.h"
#include "inkscape.h"
#include "message-stack.h"
#include "page-manager.h"
#include "path-chemistry.h"
#include "selection.h"
#include "selection-chemistry.h"
#include "style.h"

#include "display/curve.h"
#include "extension/db.h" // extension database
#include "extension/find_extension_by_mime.h"
#include "extension/input.h"
#include "extension/output.h"
#include "helper/png-write.h"
#include "inkgc/gc-core.h"
#include "live_effects/lpe-bspline.h"
#include "live_effects/lpe-spiro.h"
#include "live_effects/lpeobject-reference.h"
#include "live_effects/lpeobject.h"
#include "live_effects/parameter/path.h"
#include "object/box3d.h"
#include "object/persp3d.h"
#include "object/object-set.h"
#include "object/sp-clippath.h"
#include "object/sp-defs.h"
#include "object/sp-flowtext.h"
#include "object/sp-gradient-reference.h"
#include "object/sp-hatch.h"
#include "object/sp-item-transform.h"
#include "object/sp-linear-gradient.h"
#include "object/sp-marker.h"
#include "object/sp-mask.h"
#include "object/sp-mesh-gradient.h"
#include "object/sp-page.h"
#include "object/sp-path.h"
#include "object/sp-pattern.h"
#include "object/sp-radial-gradient.h"
#include "object/sp-rect.h"
#include "object/sp-root.h"
#include "object/sp-shape.h"
#include "object/sp-symbol.h"
#include "object/sp-textpath.h"
#include "object/sp-use.h"
#include "svg/css-ostringstream.h" // used in copy
#include "svg/svg-color.h"
#include "svg/svg.h" // for sp_svg_transform_write, used in _copySelection
#include "text-chemistry.h"
#include "text-editing.h"
#include "ui/tool/control-point-selection.h"
#include "ui/tool/multi-path-manipulator.h"
#include "ui/tools/dropper-tool.h" // used in copy()
#include "ui/tools/node-tool.h"
#include "ui/tools/text-tool.h"
#include "util/units.h"
#include "xml/repr.h"
#include "xml/sp-css-attr.h"

/// Made up mimetype to represent Gdk::Pixbuf clipboard contents.
#define CLIPBOARD_GDK_PIXBUF_TARGET "image/x-gdk-pixbuf"

#define CLIPBOARD_TEXT_TARGET "text/plain"

#ifdef _WIN32
#include <windows.h>
#endif

namespace Inkscape {
namespace UI {

/**
 * Default implementation of the clipboard manager.
 */
class ClipboardManagerImpl : public ClipboardManager {
public:
    void copy(ObjectSet *set) override;
    void copyPathParameter(Inkscape::LivePathEffect::PathParam *) override;
    void copySymbol(Inkscape::XML::Node* symbol, gchar const* style, SPDocument *source, Geom::Rect const &bbox) override;
    void insertSymbol(SPDesktop *desktop, Geom::Point const &shift_dt) override;
    bool paste(SPDesktop *desktop, bool in_place, bool on_page) override;
    bool pasteStyle(ObjectSet *set) override;
    bool pasteSize(ObjectSet *set, bool separately, bool apply_x, bool apply_y) override;
    bool pastePathEffect(ObjectSet *set) override;
    Glib::ustring getPathParameter(SPDesktop* desktop) override;
    Glib::ustring getShapeOrTextObjectId(SPDesktop *desktop) override;
    std::vector<Glib::ustring> getElementsOfType(SPDesktop *desktop, gchar const* type = "*", gint maxdepth = -1) override;
    Glib::ustring getFirstObjectID() override;

    ClipboardManagerImpl();
    ~ClipboardManagerImpl() override;

private:
    void _cleanStyle(SPCSSAttr *);
    void _copySelection(ObjectSet *);
    void _copyCompleteStyle(SPItem *item, Inkscape::XML::Node *target, bool child = false);
    void _copyUsedDefs(SPItem *);
    void _copyGradient(SPGradient *);
    void _copyPattern(SPPattern *);
    void _copyHatch(SPHatch *);
    void _copyTextPath(SPTextPath *);
    bool _copyNodes(SPDesktop *desktop, ObjectSet *set);
    Inkscape::XML::Node *_copyNode(Inkscape::XML::Node *, Inkscape::XML::Document *, Inkscape::XML::Node *);
    Inkscape::XML::Node *_copyIgnoreDup(Inkscape::XML::Node *, Inkscape::XML::Document *, Inkscape::XML::Node *);

    bool _pasteImage(SPDocument *doc);
    bool _pasteText(SPDesktop *desktop);
    bool _pasteNodes(SPDesktop *desktop, SPDocument *clipdoc, bool in_place, bool on_page);
    void _applyPathEffect(SPItem *, gchar const *);
    std::unique_ptr<SPDocument> _retrieveClipboard(Glib::ustring = "");

    // clipboard callbacks
    void _onGet(Gtk::SelectionData &, guint);
    void _onClear();

    // various helpers
    void _createInternalClipboard();
    void _discardInternalClipboard();
    Inkscape::XML::Node *_createClipNode();
    Geom::Scale _getScale(SPDesktop *desktop, Geom::Point const &min, Geom::Point const &max, Geom::Rect const &obj_rect, bool apply_x, bool apply_y);
    Glib::ustring _getBestTarget(SPDesktop *desktop = nullptr);
    void _setClipboardTargets();
    void _setClipboardColor(guint32);
    void _userWarn(SPDesktop *, char const *);

    // private properties
    std::unique_ptr<SPDocument> _clipboardSPDoc; ///< Document that stores the clipboard until someone requests it
    Inkscape::XML::Node *_defs; ///< Reference to the clipboard document's defs node
    Inkscape::XML::Node *_root; ///< Reference to the clipboard's root node
    Inkscape::XML::Node *_clipnode; ///< The node that holds extra information
    Inkscape::XML::Document *_doc; ///< Reference to the clipboard's Inkscape::XML::Document
    std::set<SPItem*> cloned_elements;
    std::vector<SPCSSAttr*> te_selected_style;
    std::vector<unsigned> te_selected_style_positions;
    int nr_blocks = 0;

    // we need a way to copy plain text AND remember its style;
    // the standard _clipnode is only available in an SVG tree, hence this special storage
    SPCSSAttr *_text_style; ///< Style copied along with plain text fragment

    Glib::RefPtr<Gdk::Clipboard> _clipboard; ///< Handle to the system wide clipboard - for convenience
    std::list<Glib::ustring> _preferred_targets; ///< List of supported clipboard targets
};

ClipboardManagerImpl::ClipboardManagerImpl()
    : _clipboardSPDoc(nullptr),
      _defs(nullptr),
      _root(nullptr),
      _clipnode(nullptr),
      _doc(nullptr),
      _text_style(nullptr),
      _clipboard( Gdk::Clipboard::get() )
{
    // Clipboard Formats: http://msdn.microsoft.com/en-us/library/ms649013(VS.85).aspx
    // On Windows, most graphical applications can handle CF_DIB/CF_BITMAP and/or CF_ENHMETAFILE
    // GTK automatically presents an "image/bmp" target as CF_DIB/CF_BITMAP
    // Presenting "image/x-emf" as CF_ENHMETAFILE must be done by Inkscape ?

    // push supported clipboard targets, in order of preference
    _preferred_targets.emplace_back("image/x-inkscape-svg");
    _preferred_targets.emplace_back("image/svg+xml");
    _preferred_targets.emplace_back("image/svg+xml-compressed");
    _preferred_targets.emplace_back("image/x-emf");
    _preferred_targets.emplace_back("CF_ENHMETAFILE");
    _preferred_targets.emplace_back("WCF_ENHMETAFILE"); // seen on Wine
    _preferred_targets.emplace_back("application/pdf");
    _preferred_targets.emplace_back("image/x-adobe-illustrator");

    // Clipboard requests on app termination can cause undesired extension
    // popup windows. Clearing the clipboard can prevent this.
    auto application = Gio::Application::get_default();
    if (application) {
        application->signal_shutdown().connect_notify([this]() { this->_discardInternalClipboard(); });
    }
}

ClipboardManagerImpl::~ClipboardManagerImpl() = default;

/**
 * Copy selection contents to the clipboard.
 */
void ClipboardManagerImpl::copy(ObjectSet *set)
{
    if (auto const desktop = set->desktop()) {
        // Special case for when the gradient dragger is active - copies gradient color
        if (auto const drag = desktop->getTool()->get_drag();
            drag && drag->hasSelection())
        {
            guint32 col = drag->getColor();

            // set the color as clipboard content (text in RRGGBBAA format)
            _setClipboardColor(col);

            // create a style with this color on fill and opacity in master opacity, so it can be
            // pasted on other stops or objects
            if (_text_style) {
                sp_repr_css_attr_unref(_text_style);
                _text_style = nullptr;
            }
            _text_style = sp_repr_css_attr_new();
            // print and set properties
            gchar color_str[16];
            g_snprintf(color_str, 16, "#%06x", col >> 8);
            sp_repr_css_set_property(_text_style, "fill", color_str);
            float opacity = SP_RGBA32_A_F(col);
            if (opacity > 1.0) {
                opacity = 1.0; // safeguard
            }
            Inkscape::CSSOStringStream opcss;
            opcss << opacity;
            sp_repr_css_set_property(_text_style, "opacity", opcss.str().data());

            _discardInternalClipboard();
            return;
        }

        // Special case for when the color picker ("dropper") is active - copies color under cursor
        if (auto const dt = dynamic_cast<Tools::DropperTool const *>(desktop->getTool())) {
            _setClipboardColor(dt->get_color(false, true));
            _discardInternalClipboard();
            return;
        }

        // Special case for when the text tool is active - if some text is selected, copy plain text,
        // not the object that holds it; also copy the style at cursor into
        if (auto const text_tool = dynamic_cast<Tools::TextTool*>(desktop->getTool())) {
            _discardInternalClipboard();
            _clipboard->set_text(get_selected_text(*text_tool));
            if (_text_style) {
                sp_repr_css_attr_unref(_text_style);
                _text_style = nullptr;
            }
            _text_style = get_style_at_cursor(*text_tool);
            return;
        }

        // Special case for copying part of a path instead of the whole selected object.
        if (_copyNodes(desktop, set)) {
            return;
        }

        if (set->isEmpty()) {  // check whether something is selected
            _userWarn(desktop, "Nothing was copied.");
            return;
        }
    }

    _discardInternalClipboard();

    _createInternalClipboard();   // construct a new clipboard document
    _copySelection(set);   // copy all items in the selection to the internal clipboard
    fit_canvas_to_drawing(_clipboardSPDoc.get());

    _setClipboardTargets();
}

/**
 * Copy a Live Path Effect path parameter to the clipboard.
 * @param pp The path parameter to store in the clipboard.
 */
void ClipboardManagerImpl::copyPathParameter(Inkscape::LivePathEffect::PathParam *pp)
{
    if ( pp == nullptr ) {
        return;
    }
    SPItem * item = SP_ACTIVE_DESKTOP->getSelection()->singleItem();
    Geom::PathVector pv = pp->get_pathvector();
    if (item != nullptr) {
        pv *= item->i2doc_affine();
    }
    auto svgd = sp_svg_write_path(pv);

    if (svgd.empty()) {
        return;
    }

    _discardInternalClipboard();
    _createInternalClipboard();

    Inkscape::XML::Node *pathnode = _doc->createElement("svg:path");
    pathnode->setAttribute("d", svgd);
    _root->appendChild(pathnode);
    Inkscape::GC::release(pathnode);

    fit_canvas_to_drawing(_clipboardSPDoc.get());
    _setClipboardTargets();
}

/**
 * Copy a symbol from the symbol dialog.
 *
 * @param symbol The Inkscape::XML::Node for the symbol.
 * @param style The style to be applied to the symbol.
 * @param source The source document of the symbol.
 * @param bbox The bounding box of the symbol, in desktop coordinates.
 */
void ClipboardManagerImpl::copySymbol(Inkscape::XML::Node* symbol, gchar const* style, SPDocument *source,
                                      Geom::Rect const &bbox)
{
    if (!symbol)
        return;

    _discardInternalClipboard();
    _createInternalClipboard();

    // We add "_duplicate" to have a well defined symbol name that
    // bypasses the "prevent_id_classes" routine. We'll get rid of it
    // when we paste.
    auto original = cast<SPItem>(source->getObjectByRepr(symbol));
    _copyUsedDefs(original);
    Inkscape::XML::Node *repr = symbol->duplicate(_doc);
    Glib::ustring symbol_name = repr->attribute("id");

    symbol_name += "_inkscape_duplicate";
    repr->setAttribute("id", symbol_name);
    _defs->appendChild(repr);
    auto nsymbol = cast<SPSymbol>(_clipboardSPDoc->getObjectById(symbol_name));
    if (nsymbol) {
        _copyCompleteStyle(original, repr, true);
        auto scale = _clipboardSPDoc->getDocumentScale();
        // Convert scale from source to clipboard user units
        nsymbol->scaleChildItemsRec(scale, Geom::Point(0, 0), false);
        if (!nsymbol->title()) {
            nsymbol->setTitle(nsymbol->label() ? nsymbol->label() : nsymbol->getId());
        }
        auto href = Glib::ustring("#") + symbol_name;
        size_t pos = href.find( "_inkscape_duplicate" );
        // while ffix rename id we do this hack
        href.erase( pos );
        Inkscape::XML::Node *use_repr = _doc->createElement("svg:use");
        use_repr->setAttribute("xlink:href", href);
   
        /**
        * If the symbol has a viewBox but no width or height, then take width and
        * height from the viewBox and set them on the use element. Otherwise, the
        * use element will have 100% document width and height!
        */
        {
            auto widthAttr = symbol->attribute("width");
            auto heightAttr = symbol->attribute("height");
            auto viewBoxAttr = symbol->attribute("viewBox");
            if (viewBoxAttr && !(heightAttr || widthAttr)) {
                SPViewBox vb;
                vb.set_viewBox(viewBoxAttr);
                if (vb.viewBox_set) {
                    use_repr->setAttributeSvgDouble("width", vb.viewBox.width());
                    use_repr->setAttributeSvgDouble("height", vb.viewBox.height());
                }
            }
        }
        // Set a default style in <use> rather than <symbol> so it can be changed.
        use_repr->setAttribute("style", style);
        _root->appendChild(use_repr);
        // because a extrange reason on append use getObjectsByElement("symbol") return 2 elements, 
        // it not give errrost by the moment;
        if (auto use = cast<SPUse>(_clipboardSPDoc->getObjectByRepr(use_repr))) {
            Geom::Affine affine = source->getDocumentScale();
            use->doWriteTransform(affine, &affine, false);
        }
        // Set min and max offsets based on the bounding rectangle.
        _clipnode->setAttributePoint("min", bbox.min());
        _clipnode->setAttributePoint("max", bbox.max());
        fit_canvas_to_drawing(_clipboardSPDoc.get());
    }
    _setClipboardTargets();
}

/**
 * Insert a symbol into the document at the prescribed position (at the end of a drag).
 *
 * @param desktop The desktop onto which the symbol has been dropped.
 * @param shift_dt The vector by which the symbol position should be shifted, in desktop coordinates.
 */
void ClipboardManagerImpl::insertSymbol(SPDesktop *desktop, Geom::Point const &shift_dt)
{
    if (!desktop || !Inkscape::have_viable_layer(desktop, desktop->getMessageStack())) {
        return;
    }
    auto symbol = _retrieveClipboard("image/x-inkscape-svg");
    if (!symbol) {
        return;
    }

    prevent_id_clashes(symbol.get(), desktop->getDocument(), true);
    auto *root = symbol->getRoot();

    // Synthesize a clipboard position in order to paste the symbol where it got dropped.
    if (auto *clipnode = sp_repr_lookup_name(root->getRepr(), "inkscape:clipboard", 1)) {
        clipnode->setAttributePoint("min", clipnode->getAttributePoint("min") + shift_dt);
        clipnode->setAttributePoint("max", clipnode->getAttributePoint("max") + shift_dt);
    }

    sp_import_document(desktop, symbol.get(), true);
}

/**
 * Paste from the system clipboard into the active desktop.
 * @param in_place Whether to put the contents where they were when copied.
 */
bool ClipboardManagerImpl::paste(SPDesktop *desktop, bool in_place, bool on_page)
{
    // do any checking whether we really are able to paste before requesting the contents
    if ( desktop == nullptr ) {
        return false;
    }
    if ( Inkscape::have_viable_layer(desktop, desktop->getMessageStack()) == false ) {
        return false;
    }

    Glib::ustring target = _getBestTarget(desktop);

    // Special cases of clipboard content handling go here
    // Note that target priority is determined in _getBestTarget.
    // TODO: Handle x-special/gnome-copied-files and text/uri-list to support pasting files

    // if there is an image on the clipboard, paste it
    if ( !on_page && target == CLIPBOARD_GDK_PIXBUF_TARGET ) {
        return _pasteImage(desktop->doc());
    }
    if ( !on_page && target == CLIPBOARD_TEXT_TARGET ) {
        // It was text, and we did paste it. If not, continue on.
        if (_pasteText(desktop)) {
            return true;
        }
        // If the clipboard contains text/plain, but is an svg document
        // then we'll try and detect it and then paste it if possible.
    }

    auto tempdoc = _retrieveClipboard(target);

    if ( tempdoc == nullptr ) {
        if (target == CLIPBOARD_TEXT_TARGET ) {
            _userWarn(desktop, _("Can't paste text outside of the text tool."));
            return false;
        } else {
            _userWarn(desktop, _("Nothing on the clipboard."));
            return false;
        }
    }

    if (_pasteNodes(desktop, tempdoc.get(), in_place, on_page)) {
        return true;
    }

    // copy definitions
    prevent_id_clashes(tempdoc.get(), desktop->getDocument(), true);
    sp_import_document(desktop, tempdoc.get(), in_place, on_page);

    // _copySelection() has put all items in groups, now ungroup them (preserves transform
    // relationships of clones, text-on-path, etc.)
    if (target == "image/x-inkscape-svg") {
        SPDocument *doc = nullptr;
        desktop->getSelection()->ungroup(true);
        std::vector<SPItem *> vec2(desktop->getSelection()->items().begin(), desktop->getSelection()->items().end());
        for (auto item : vec2) {
            // just a bit beauty on paste hidden items unselect
            doc = item->document;
            if (vec2.size() > 1 && item->isHidden()) {
                desktop->getSelection()->remove(item);
            }
            auto pasted_lpe_item = cast<SPLPEItem>(item);
            if (pasted_lpe_item) {
                remove_hidder_filter(pasted_lpe_item);
            }
        }
        if (doc) {
            doc->update_lpobjs();
        }
    }

    return true;
}

/**
 * Copy any selected nodes and return true if there were nodes.
 */
bool ClipboardManagerImpl::_copyNodes(SPDesktop *desktop, ObjectSet *set)
{
    auto const node_tool = dynamic_cast<Inkscape::UI::Tools::NodeTool *>(desktop->getTool());
    if (!node_tool || !node_tool->_selected_nodes)
        return false;

    SPPath *first_path = nullptr;
    for (auto obj : set->items()) {
        if ((first_path = cast<SPPath>(obj))) {
            break;
        }
    }

    auto builder = new Geom::PathBuilder();
    node_tool->_multipath->copySelectedPath(builder);
    Geom::PathVector pathv = builder->peek();

    // discardInternalClipboard done after copy, as deleting clipboard
    // document may trigger tool switch (as in PathParam::~PathParam)
    _discardInternalClipboard();
    _createInternalClipboard();

    // Copy document height so that desktopVisualBounds() is equivalent in the
    // source document and the clipboard.
    _clipboardSPDoc->setWidthAndHeight(desktop->doc()->getWidth(), desktop->doc()->getHeight());

    // Were any nodes actually copied?
    if (pathv.empty() || !first_path)
        return false;

    Inkscape::XML::Node *pathRepr = _doc->createElement("svg:path");

    // pathv is in desktop coordinates
    auto source_scale = first_path->i2dt_affine();
    pathRepr->setAttribute("d", sp_svg_write_path(pathv * source_scale.inverse()));
    pathRepr->setAttributeOrRemoveIfEmpty("transform", first_path->getAttribute("transform"));

    // Group the path to make it consistant with other copy processes
    auto group = _doc->createElement("svg:g");
    _root->appendChild(group);
    Inkscape::GC::release(group);

    // Store the style for paste-as-object operations. Ignored if pasting into an other path.
    pathRepr->setAttribute("style", first_path->style->write(SP_STYLE_FLAG_IFSET) );
    group->appendChild(pathRepr);
    Inkscape::GC::release(pathRepr);

    // Store the parent transformation, and scaling factor of the copied object
    if (auto parent = cast<SPItem>(first_path->parent)) {
          auto transform_str = sp_svg_transform_write(parent->i2doc_affine());
          group->setAttributeOrRemoveIfEmpty("transform", transform_str);
    }

    // Set the translation for paste-in-place operation, must be done after repr appends
    if (auto path_obj = cast<SPPath>(_clipboardSPDoc->getObjectByRepr(pathRepr))) {
        // we could use pathv.boundsFast here, but that box doesn't include stroke width
        // so we must take the value from the visualBox of the new shape instead.
        assert(Geom::are_near(path_obj->document->getDimensions(), first_path->document->getDimensions()));
        auto bbox = *(path_obj->desktopVisualBounds());
        _clipnode->setAttributePoint("min", bbox.min());
        _clipnode->setAttributePoint("max", bbox.max());
    }
    _setClipboardTargets();
    return true;
}

/**
 * Paste nodes into a selected path and return true if it's possible.
 *   if the node tool selected
 *   and one path selected in target
 *   and one path in source
 */
bool ClipboardManagerImpl::_pasteNodes(SPDesktop *desktop, SPDocument *clipdoc, bool in_place, bool on_page)
{
    auto const node_tool = dynamic_cast<Inkscape::UI::Tools::NodeTool *>(desktop->getTool());
    if (!node_tool || desktop->getSelection()->objects().size() != 1)
        return false;

    SPObject *obj = desktop->getSelection()->objects().back();
    auto target_path = cast<SPPath>(obj);
    if (!target_path)
        return false;

    auto const dt_to_target = target_path->dt2i_affine();
    // Select all nodes prior to pasting in, for later inversion.
    node_tool->_selected_nodes->selectAll();

    for (auto node = clipdoc->getReprRoot()->firstChild(); node; node = node->next()) {
        auto source_obj = clipdoc->getObjectByRepr(node);

        // Unpack group that may have a transformation inside it.
        if (auto source_group = cast<SPGroup>(source_obj)) {
            if (source_group->children.size() == 1) {
                source_obj = source_group->firstChild();
            }
        }

        if (auto source_path = cast<SPPath>(source_obj)) {
            auto source_to_target = source_path->i2dt_affine();
            auto source_curve = *source_path->curveForEdit();
            auto target_curve = *target_path->curveForEdit();

            auto bbox = *(source_path->desktopVisualBounds());
            if (!in_place) {
                // Move the source curve to the mouse pointer (desktop coordinates)
                source_to_target *= Geom::Translate((desktop->point() - bbox.midpoint()).round());
            } else if (auto clipnode = sp_repr_lookup_name(clipdoc->getReprRoot(), "inkscape:clipboard", 1)) {
                // Force translation so a foreign path will end up in the right place.
                source_to_target *= Geom::Translate(clipnode->getAttributePoint("min") - bbox.min());
            }

            source_to_target *= dt_to_target;

            // Finally convert the curve into path item's coordinate system
            source_curve.transform(source_to_target);

            // Add the source curve to the target copy
            target_curve.append(std::move(source_curve));

            // Set the attribute to keep the document up to date (fixes undo)
            auto str = sp_svg_write_path(target_curve.get_pathvector());
            target_path->setAttribute("d", str);

            if (on_page) {
                g_warning("Node paste on page not Implemented");
            }
        }
    }

    // Finally we invert the selection, this selects all newly added nodes.
    node_tool->_selected_nodes->invertSelection();

    return true;
}

/**
 * Returns the id of the first visible copied object.
 */
Glib::ustring ClipboardManagerImpl::getFirstObjectID()
{
    auto tempdoc = _retrieveClipboard("image/x-inkscape-svg");
    if ( tempdoc == nullptr ) {
        return {};
    }

    Inkscape::XML::Node *root = tempdoc->getReprRoot();

    if (!root) {
        return {};
    }

    Inkscape::XML::Node *ch = root->firstChild();
    Inkscape::XML::Node *child = nullptr;
    // now clipboard is wrapped on copy since 202d57ea fix
    while (ch != nullptr &&
           g_strcmp0(ch->name(), "svg:g") &&
           g_strcmp0(child?child->name():nullptr, "svg:g") &&
           g_strcmp0(child?child->name():nullptr, "svg:path") &&
           g_strcmp0(child?child->name():nullptr, "svg:use") &&
           g_strcmp0(child?child->name():nullptr, "svg:text") &&
           g_strcmp0(child?child->name():nullptr, "svg:image") &&
           g_strcmp0(child?child->name():nullptr, "svg:rect") &&
           g_strcmp0(child?child->name():nullptr, "svg:ellipse") &&
           g_strcmp0(child?child->name():nullptr, "svg:circle")
        ) {
        ch = ch->next();
        child = ch ? ch->firstChild(): nullptr;
    }

    if (child) {
        char const *id = child->attribute("id");
        if (id) {
            return id;
        }
    }

    return {};
}

/**
 * Remove certain css elements which are not useful for pasteStyle
 */
void ClipboardManagerImpl::_cleanStyle(SPCSSAttr *style)
{
    if (style) {
        /* Clean text 'position' properties */
        sp_repr_css_unset_property(style, "text-anchor");
        sp_repr_css_unset_property(style, "shape-inside");
        sp_repr_css_unset_property(style, "shape-subtract");
        sp_repr_css_unset_property(style, "shape-padding");
        sp_repr_css_unset_property(style, "shape-margin");
        sp_repr_css_unset_property(style, "inline-size");
    }
}

/**
 * Implements the Paste Style action.
 */
bool ClipboardManagerImpl::pasteStyle(ObjectSet *set)
{
    auto dt = set->desktop();
    if (dt == nullptr) {
        return false;
    }

    // check whether something is selected
    if (set->isEmpty()) {
        _userWarn(set->desktop(), _("Select <b>object(s)</b> to paste style to."));
        return false;
    }

    auto tempdoc = _retrieveClipboard("image/x-inkscape-svg");
    if ( tempdoc == nullptr ) {
        // no document, but we can try _text_style
        if (_text_style) {
            _cleanStyle(_text_style);
            sp_desktop_set_style(set, set->desktop(), _text_style);
            return true;
        } else {
            _userWarn(set->desktop(), _("No style on the clipboard."));
            return false;
        }
    }

    static auto *const prefs = Inkscape::Preferences::get();
    auto const copy_computed = prefs->getBool("/options/copycomputedstyle/value", true);

    Inkscape::XML::Node *root = tempdoc->getReprRoot();
    Inkscape::XML::Node *clipnode = sp_repr_lookup_name(root, "inkscape:clipboard", 1);

    if (!clipnode) {
        _userWarn(set->desktop(), _("No style on the clipboard."));
        return false;
    }

    bool pasted = false;

    if (copy_computed) {
        SPCSSAttr *style = sp_repr_css_attr(clipnode, "style");
        sp_desktop_set_style(set, set->desktop(), style);
        pasted = true;
    } else {
        for (auto node : set->xmlNodes()) {
            pasted = node->copyAttribute("class", clipnode, true) || pasted;
            pasted = node->copyAttribute("style", clipnode, true) || pasted;
        }
    }

    if (pasted) {
        // pasted style might depend on defs from the source
        set->document()->importDefs(tempdoc.get());
    }

    return pasted;
}

/**
 * Resize the selection or each object in the selection to match the clipboard's size.
 * @param separately Whether to scale each object in the selection separately
 * @param apply_x Whether to scale the width of objects / selection
 * @param apply_y Whether to scale the height of objects / selection
 */
bool ClipboardManagerImpl::pasteSize(ObjectSet *set, bool separately, bool apply_x, bool apply_y)
{
    if (!apply_x && !apply_y) {
        return false; // pointless parameters
    }

    if (set->isEmpty()) {
        if(set->desktop())
            _userWarn(set->desktop(), _("Select <b>object(s)</b> to paste size to."));
        return false;
    }

    // FIXME: actually, this should accept arbitrary documents
    auto tempdoc = _retrieveClipboard("image/x-inkscape-svg");
    if ( tempdoc == nullptr ) {
        if(set->desktop())
            _userWarn(set->desktop(), _("No size on the clipboard."));
        return false;
    }

    // retrieve size information from the clipboard
    Inkscape::XML::Node *root = tempdoc->getReprRoot();
    Inkscape::XML::Node *clipnode = sp_repr_lookup_name(root, "inkscape:clipboard", 1);
    if (!clipnode) return false;

    Geom::Point min, max;
    bool visual_bbox = !Inkscape::Preferences::get()->getInt("/tools/bounding_box");
    min = clipnode->getAttributePoint((visual_bbox ? "min" : "geom-min"), min);
    max = clipnode->getAttributePoint((visual_bbox ? "max" : "geom-max"), max);

    if (separately) {
        // resize each object in the selection
        auto itemlist = set->items();
        for (auto i = itemlist.begin(); i != itemlist.end(); ++i) {
            SPItem *item = *i;
            if (item) {
                Geom::OptRect obj_size = item->desktopPreferredBounds();
                if ( obj_size ) {
                    item->scale_rel(_getScale(set->desktop(), min, max, *obj_size, apply_x, apply_y));
                }
            } else {
                g_assert_not_reached();
            }
        }
    } else {
        // resize the selection as a whole
        Geom::OptRect sel_size = set->preferredBounds();
        if (sel_size) {
            set->setScaleRelative(sel_size->midpoint(),
                                         _getScale(set->desktop(), min, max, *sel_size, apply_x, apply_y));
        }
    }

    return true;
}

/**
 * Applies a path effect from the clipboard to the selected path.
 */
bool ClipboardManagerImpl::pastePathEffect(ObjectSet *set)
{
    /** @todo FIXME: pastePathEffect crashes when moving the path with the applied effect,
        segfaulting in fork_private_if_necessary(). */

    if ( set->desktop() == nullptr ) {
        return false;
    }

    //Inkscape::Selection *selection = desktop->getSelection();
    if (!set || set->isEmpty()) {
        _userWarn(set->desktop(), _("Select <b>object(s)</b> to paste live path effect to."));
        return false;
    }

    auto tempdoc = _retrieveClipboard("image/x-inkscape-svg");
    if ( tempdoc ) {
        Inkscape::XML::Node *root = tempdoc->getReprRoot();
        Inkscape::XML::Node *clipnode = sp_repr_lookup_name(root, "inkscape:clipboard", 1);
        if ( clipnode ) {
            gchar const *effectstack = clipnode->attribute("inkscape:path-effect");
            if ( effectstack ) {
                set->document()->importDefs(tempdoc.get());
                // make sure all selected items are converted to paths first (i.e. rectangles)
                set->toLPEItems();
                auto itemlist= set->items();
                for(auto i=itemlist.begin();i!=itemlist.end();++i){
                    SPItem *item = *i;
                    _applyPathEffect(item, effectstack);
                    item->doWriteTransform(item->transform);
                }

                return true;
            }
        }
    }

    // no_effect:
    _userWarn(set->desktop(), _("No effect on the clipboard."));
    return false;
}

/**
 * Get LPE path data from the clipboard.
 * @return The retrieved path data (contents of the d attribute), or "" if no path was found
 */
Glib::ustring ClipboardManagerImpl::getPathParameter(SPDesktop* desktop)
{
    auto doc = _retrieveClipboard(); // any target will do here
    if (!doc) {
        _userWarn(desktop, _("Nothing on the clipboard."));
        return "";
    }

    // unlimited search depth
    auto repr = sp_repr_lookup_name(doc->getReprRoot(), "svg:path", -1);
    auto item = cast<SPItem>(doc->getObjectByRepr(repr));

    if (!item) {
        _userWarn(desktop, _("Clipboard does not contain a path."));
        return "";
    }

    // Adjust any copied path into the target document transform.
    auto tr_p = item->i2doc_affine();
    auto tr_s = doc->getDocumentScale().inverse();
    auto pathv = sp_svg_read_pathv(repr->attribute("d"));
    return sp_svg_write_path(pathv * tr_s * tr_p);
}

/**
 * Get object id of a shape or text item from the clipboard.
 * @return The retrieved id string (contents of the id attribute), or "" if no shape or text item was found.
 */
Glib::ustring ClipboardManagerImpl::getShapeOrTextObjectId(SPDesktop *desktop)
{
    // https://bugs.launchpad.net/inkscape/+bug/1293979
    // basically, when we do a depth-first search, we're stopping
    // at the first object to be <svg:path> or <svg:text>.
    // but that could then return the id of the object's
    // clip path or mask, not the original path!

    auto tempdoc = _retrieveClipboard(); // any target will do here
    if ( tempdoc == nullptr ) {
        _userWarn(desktop, _("Nothing on the clipboard."));
        return "";
    }
    Inkscape::XML::Node *root = tempdoc->getReprRoot();

    // 1293979: strip out the defs of the document
    root->removeChild(tempdoc->getDefs()->getRepr());

    Inkscape::XML::Node *repr = sp_repr_lookup_name(root, "svg:path", -1); // unlimited search depth
    if ( repr == nullptr ) {
        repr = sp_repr_lookup_name(root, "svg:text", -1);
    }
    if (repr == nullptr) {
        repr = sp_repr_lookup_name(root, "svg:ellipse", -1);
    }
    if (repr == nullptr) {
        repr = sp_repr_lookup_name(root, "svg:rect", -1);
    }
    if (repr == nullptr) {
        repr = sp_repr_lookup_name(root, "svg:circle", -1);
    }

    if ( repr == nullptr ) {
        _userWarn(desktop, _("Clipboard does not contain a path."));
        return "";
    }
    gchar const *svgd = repr->attribute("id");
    return svgd ? svgd : "";
}

/**
 * Get all objects id  from the clipboard.
 * @return A vector containing all IDs or empty if no shape or text item was found.
 * type. Set to "*" to retrieve all elements of the types vector inside, feel free to populate more
 */
std::vector<Glib::ustring> ClipboardManagerImpl::getElementsOfType(SPDesktop *desktop, gchar const* type, gint maxdepth)
{
    std::vector<Glib::ustring> result;
    auto tempdoc = _retrieveClipboard(); // any target will do here
    if ( tempdoc == nullptr ) {
        _userWarn(desktop, _("Nothing on the clipboard."));
        return result;
    }
    Inkscape::XML::Node *root = tempdoc->getReprRoot();

    // 1293979: strip out the defs of the document
    root->removeChild(tempdoc->getDefs()->getRepr());
    std::vector<Inkscape::XML::Node const *> reprs;
    if (strcmp(type, "*") == 0){
        //TODO:Fill vector with all possible elements
        std::vector<Glib::ustring> types;
        types.push_back((Glib::ustring)"svg:path");
        types.push_back((Glib::ustring)"svg:circle");
        types.push_back((Glib::ustring)"svg:rect");
        types.push_back((Glib::ustring)"svg:ellipse");
        types.push_back((Glib::ustring)"svg:text");
        types.push_back((Glib::ustring)"svg:use");
        types.push_back((Glib::ustring)"svg:g");
        types.push_back((Glib::ustring)"svg:image");
        for (auto type_elem : types) {
            std::vector<Inkscape::XML::Node const *> reprs_found = sp_repr_lookup_name_many(root, type_elem.c_str(), maxdepth); // unlimited search depth
            reprs.insert(reprs.end(), reprs_found.begin(), reprs_found.end());
        }
    } else {
        reprs = sp_repr_lookup_name_many(root, type, maxdepth);
    }
    for (auto node : reprs) {
        result.emplace_back(node->attribute("id"));
    }
    if ( result.empty() ) {
        _userWarn(desktop, (Glib::ustring::compose(_("Clipboard does not contain any objects of type \"%1\"."), type)).c_str());
        return result;
    }
    return result;
}

/**
 * Iterate over a list of items and copy them to the clipboard.
 */
void ClipboardManagerImpl::_copySelection(ObjectSet *selection)
{
    static auto *const prefs = Inkscape::Preferences::get();
    auto const copy_computed = prefs->getBool("/options/copycomputedstyle/value", true);
    SPPage *page = nullptr;

    // copy the defs used by all items
    auto itemlist = selection->items();
    cloned_elements.clear();
    std::vector<SPItem *> items(itemlist.begin(), itemlist.end());
    for (auto item : itemlist) {
        if (!page) {
            page = item->document->getPageManager().getPageFor(item, false);
        }
        auto lpeitem = cast<SPLPEItem>(item);
        if (lpeitem) {
            for (auto satellite : lpeitem->get_satellites(false, true)) {
                if (satellite) {
                    auto item2 = cast<SPItem>(satellite);
                    if (item2 && std::find(items.begin(), items.end(), item2) == items.end()) {
                        items.push_back(item2);
                    }
                }
            }
        }
    }
    cloned_elements.clear();
    for (auto item : items) {
        if (item) {
            _copyUsedDefs(item);
        } else {
            g_assert_not_reached();
        }
    }

    // copy the representation of the items
    std::vector<SPObject *> sorted_items(items.begin(), items.end());
    {
        // Get external text references and add them to sorted_items
        auto ext_refs = text_categorize_refs(selection->document(),
                sorted_items.begin(), sorted_items.end(),
                TEXT_REF_EXTERNAL);
        for (auto const &ext_ref : ext_refs) {
            sorted_items.push_back(selection->document()->getObjectById(ext_ref.first));
        }
    }
    sort(sorted_items.begin(), sorted_items.end(), sp_object_compare_position_bool);

    //remove already copied elements from cloned_elements
    std::vector<SPItem*>tr;
    for(auto cloned_element : cloned_elements){
        if(std::find(sorted_items.begin(),sorted_items.end(),cloned_element)!=sorted_items.end())
            tr.push_back(cloned_element);
    }
    for(auto & it : tr){
        cloned_elements.erase(it);
    }

    // One group per shared parent
    std::map<SPObject const *, Inkscape::XML::Node *> groups;

    sorted_items.insert(sorted_items.end(),cloned_elements.begin(),cloned_elements.end());
    for(auto sorted_item : sorted_items){
        auto item = cast<SPItem>(sorted_item);
        if (item) {
            // Create a group with the parent transform. This group will be ungrouped when pasting
            // und takes care of transform relationships of clones, text-on-path, etc.
            auto &group = groups[item->parent];
            if (!group) {
                group = _doc->createElement("svg:g");
                _root->appendChild(group);
                Inkscape::GC::release(group);

                if (auto parent = cast<SPItem>(item->parent)) {
                    auto transform_str = sp_svg_transform_write(parent->i2doc_affine());
                    group->setAttributeOrRemoveIfEmpty("transform", transform_str);
                }
            }

            Inkscape::XML::Node *obj = item->getRepr();
            Inkscape::XML::Node *obj_copy;
            if(cloned_elements.find(item)==cloned_elements.end())
                obj_copy = _copyNode(obj, _doc, group);
            else
                obj_copy = _copyNode(obj, _doc, _clipnode);

            if (copy_computed) {
                // copy complete inherited style
                _copyCompleteStyle(item, obj_copy);
            }
        }
    }
    // copy style for Paste Style action
    if (auto item = selection->singleItem()) {
        if (copy_computed) {
            SPCSSAttr *style = take_style_from_item(item);
            _cleanStyle(style);
            sp_repr_css_set(_clipnode, style, "style");
            sp_repr_css_attr_unref(style);
        } else {
            _clipnode->copyAttribute("class", item->getRepr(), true);
            _clipnode->copyAttribute("style", item->getRepr(), true);
        }

        // copy path effect from the first path
        if (gchar const *effect = item->getRepr()->attribute("inkscape:path-effect")) {
            _clipnode->setAttribute("inkscape:path-effect", effect);
        }
    }

    if (Geom::OptRect size = selection->visualBounds()) {
        _clipnode->setAttributePoint("min", size->min());
        _clipnode->setAttributePoint("max", size->max());
    }
    if (Geom::OptRect geom_size = selection->geometricBounds()) {
        _clipnode->setAttributePoint("geom-min", geom_size->min());
        _clipnode->setAttributePoint("geom-max", geom_size->max());
    }
    if (page) {
        auto page_rect = page->getDesktopRect();
        _clipnode->setAttributePoint("page-min", page_rect.min());
        _clipnode->setAttributePoint("page-max", page_rect.max());
    }
}

/**
 * Copies the style from the stylesheet to preserve it.
 *
 * @param item - The source item (connected to it's document)
 * @param target - The target xml node to store the style in.
 * @param child - Flag to indicate a recursive call, do not use.
 */
void ClipboardManagerImpl::_copyCompleteStyle(SPItem *item, Inkscape::XML::Node *target, bool child)
{
    auto source = item->getRepr();
    SPCSSAttr *css;
    if (child) {
        // Child styles shouldn't copy their parent's existing cascaded style.
        css = sp_repr_css_attr(source, "style");
    } else {
        css = sp_repr_css_attr_inherited(source, "style");
    }
    for (auto iter : item->style->properties()) {
        if (iter->style_src == SPStyleSrc::STYLE_SHEET) {
            css->setAttributeOrRemoveIfEmpty(iter->name(), iter->get_value());
        }
    }
    sp_repr_css_set(target, css, "style");
    sp_repr_css_attr_unref(css);

    if (is<SPGroup>(item)) {
        // Recursively go through chldren too
        auto source_child = source->firstChild();
        auto target_child = target->firstChild();
        while (source_child && target_child) {
            if (auto child_item = cast<SPItem>(item->document->getObjectByRepr(source_child))) {
                _copyCompleteStyle(child_item, target_child, true);
            }
            source_child = source_child->next();
            target_child = target_child->next();
        }
    }
}

/**
 * Recursively copy all the definitions used by a given item to the clipboard defs.
 */
void ClipboardManagerImpl::_copyUsedDefs(SPItem *item)
{
    auto use = cast<SPUse>(item);
    if (use && use->get_original()) {
        if(cloned_elements.insert(use->get_original()).second)
            _copyUsedDefs(use->get_original());
    }

    // copy fill and stroke styles (patterns and gradients)
    SPStyle *style = item->style;

    if (style && (style->fill.isPaintserver())) {
        SPPaintServer *server = item->style->getFillPaintServer();
        if (is<SPLinearGradient>(server) || is<SPRadialGradient>(server) || is<SPMeshGradient>(server) ) {
            _copyGradient(cast<SPGradient>(server));
        }
        auto pattern = cast<SPPattern>(server);
        if (pattern) {
            _copyPattern(pattern);
        }
        auto hatch = cast<SPHatch>(server);
        if (hatch) {
            _copyHatch(hatch);
        }
    }
    if (style && (style->stroke.isPaintserver())) {
        SPPaintServer *server = item->style->getStrokePaintServer();
        if (is<SPLinearGradient>(server) || is<SPRadialGradient>(server) || is<SPMeshGradient>(server) ) {
            _copyGradient(cast<SPGradient>(server));
        }
        auto pattern = cast<SPPattern>(server);
        if (pattern) {
            _copyPattern(pattern);
        }
        auto hatch = cast<SPHatch>(server);
        if (hatch) {
            _copyHatch(hatch);
        }
    }

    // For shapes, copy all of the shape's markers
    auto shape = cast<SPShape>(item);
    if (shape) {
        for (auto & i : shape->_marker) {
            if (i) {
                _copyNode(i->getRepr(), _doc, _defs);
            }
        }
    }

    // For 3D boxes, copy perspectives
    if (auto box = cast<SPBox3D>(item)) {
        if (auto perspective = box->get_perspective()) {
            _copyNode(perspective->getRepr(), _doc, _defs);
        }
    }

    // Copy text paths
    {
        auto text = cast<SPText>(item);
        SPTextPath *textpath = text ? cast<SPTextPath>(text->firstChild()) : nullptr;
        if (textpath) {
            _copyTextPath(textpath);
        }
        if (text) {
            for (auto &&shape_prop_ptr : {
                    reinterpret_cast<SPIShapes SPStyle::*>(&SPStyle::shape_inside),
                    reinterpret_cast<SPIShapes SPStyle::*>(&SPStyle::shape_subtract) }) {
                for (auto *href : (text->style->*shape_prop_ptr).hrefs) {
                    auto shape_obj = href->getObject();
                    if (!shape_obj)
                        continue;
                    auto shape_repr = shape_obj->getRepr();
                    if (sp_repr_is_def(shape_repr)) {
                        _copyIgnoreDup(shape_repr, _doc, _defs);
                    }
                }
            }
        }
    }

    // Copy clipping objects
    if (SPObject *clip = item->getClipObject()) {
        _copyNode(clip->getRepr(), _doc, _defs);
        // recurse
        for (auto &o : clip->children) {
            if (auto childItem = cast<SPItem>(&o)) {
                _copyUsedDefs(childItem);
            }
        }
    }
    // Copy mask objects
    if (SPObject *mask = item->getMaskObject()) {
            _copyNode(mask->getRepr(), _doc, _defs);
            // recurse into the mask for its gradients etc.
            for(auto& o: mask->children) {
                auto childItem = cast<SPItem>(&o);
                if (childItem) {
                    _copyUsedDefs(childItem);
                }
            }
    }

    // Copy filters
    if (style->getFilter()) {
        SPObject *filter = style->getFilter();
        if (is<SPFilter>(filter)) {
            _copyNode(filter->getRepr(), _doc, _defs);
        }
    }

    // For lpe items, copy lpe stack if applicable
    auto lpeitem = cast<SPLPEItem>(item);
    if (lpeitem) {
        if (lpeitem->hasPathEffect()) {
            PathEffectList path_effect_list( *lpeitem->path_effect_list);
            for (auto &lperef : path_effect_list) {
                LivePathEffectObject *lpeobj = lperef->lpeobject;
                if (lpeobj) {
                  _copyNode(lpeobj->getRepr(), _doc, _defs);
                }
            }
        }
    }

    // recurse
    for(auto& o: item->children) {
        auto childItem = cast<SPItem>(&o);
        if (childItem) {
            _copyUsedDefs(childItem);
        }
    }
}

/**
 * Copy a single gradient to the clipboard's defs element.
 */
void ClipboardManagerImpl::_copyGradient(SPGradient *gradient)
{
    while (gradient) {
        // climb up the refs, copying each one in the chain
        _copyNode(gradient->getRepr(), _doc, _defs);
        if (gradient->ref){
            gradient = gradient->ref->getObject();
        }
        else {
            gradient = nullptr;
        }
    }
}

/**
 * Copy a single pattern to the clipboard document's defs element.
 */
void ClipboardManagerImpl::_copyPattern(SPPattern *pattern)
{
    // climb up the references, copying each one in the chain
    while (pattern) {
        _copyNode(pattern->getRepr(), _doc, _defs);

        // items in the pattern may also use gradients and other patterns, so recurse
        for (auto& child: pattern->children) {
            auto childItem = cast<SPItem>(&child);
            if (childItem) {
                _copyUsedDefs(childItem);
            }
        }
        pattern = pattern->ref.getObject();
    }
}

/**
 * Copy a single hatch to the clipboard document's defs element.
 */
void ClipboardManagerImpl::_copyHatch(SPHatch *hatch)
{
    // climb up the references, copying each one in the chain
    while (hatch) {
        _copyNode(hatch->getRepr(), _doc, _defs);

        for (auto &child : hatch->children) {
            auto childItem = cast<SPItem>(&child);
            if (childItem) {
                _copyUsedDefs(childItem);
            }
        }
        if (hatch->ref) {
            hatch = hatch->ref->getObject();
        } else {
            hatch = nullptr;
        }
    }
}

/**
 * Copy a text path to the clipboard's defs element.
 */
void ClipboardManagerImpl::_copyTextPath(SPTextPath *tp)
{
    SPItem *path = sp_textpath_get_path_item(tp);
    if (!path) {
        return;
    }
    // textpaths that aren't in defs (on the canvas) shouldn't be copied because if
    // both objects are being copied already, this ends up stealing the refs id.
    if(path->parent && is<SPDefs>(path->parent)) {
        _copyIgnoreDup(path->getRepr(), _doc, _defs);
    }
}

/**
 * Copy a single XML node from one document to another.
 * @param node The node to be copied
 * @param target_doc The document to which the node is to be copied
 * @param parent The node in the target document which will become the parent of the copied node
 * @return Pointer to the copied node
 */
Inkscape::XML::Node *ClipboardManagerImpl::_copyNode(Inkscape::XML::Node *node, Inkscape::XML::Document *target_doc, Inkscape::XML::Node *parent)
{
    Inkscape::XML::Node *dup = node->duplicate(target_doc);
    parent->appendChild(dup);
    Inkscape::GC::release(dup);
    return dup;
}

Inkscape::XML::Node *ClipboardManagerImpl::_copyIgnoreDup(Inkscape::XML::Node *node, Inkscape::XML::Document *target_doc, Inkscape::XML::Node *parent)
{
    if (sp_repr_lookup_child(_root, "id", node->attribute("id"))) {
        // node already copied
        return nullptr;
    }
    Inkscape::XML::Node *dup = node->duplicate(target_doc);
    parent->appendChild(dup);
    Inkscape::GC::release(dup);
    return dup;
}

/**
 * Retrieve a bitmap image from the clipboard and paste it into the active document.
 */
bool ClipboardManagerImpl::_pasteImage(SPDocument *doc)
{
    if ( doc == nullptr ) {
        return false;
    }

    // retrieve image data
    Glib::RefPtr<Gdk::Pixbuf> img = _clipboard->wait_for_image();
    if (!img) {
        return false;
    }

    Inkscape::Extension::Extension *png = Inkscape::Extension::find_by_mime("image/png");
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring attr_saved = prefs->getString("/dialogs/import/link");
    bool ask_saved = prefs->getBool("/dialogs/import/ask");
    prefs->setString("/dialogs/import/link", "embed");
    prefs->setBool("/dialogs/import/ask", false);
    png->set_gui(false);

    gchar *filename = g_build_filename( g_get_user_cache_dir(), "inkscape-clipboard-import", nullptr );
    img->save(filename, "png");
    file_import(doc, filename, png);
    g_free(filename);
    prefs->setString("/dialogs/import/link", attr_saved);
    prefs->setBool("/dialogs/import/ask", ask_saved);
    png->set_gui(true);

    return true;
}

/**
 * Paste text into the selected text object or create a new one to hold it.
 */
bool ClipboardManagerImpl::_pasteText(SPDesktop *desktop)
{
    if (!desktop) {
        return false;
    }

    // if the text editing tool is active, paste the text into the active text object
    if (auto text_tool = dynamic_cast<Tools::TextTool*>(desktop->getTool())) {
        return text_tool->pasteInline();
    }

    // Parse the clipboard text as if it was a color string.
    Glib::RefPtr<Gdk::Clipboard> clipboard = Gdk::Clipboard::get();
    Glib::ustring const clip_text = clipboard->wait_for_text();
    if (clip_text.length() < 30) {
        // Zero makes it impossible to paste a 100% transparent black, but it's useful.
        guint32 const rgb0 = sp_svg_read_color(clip_text.c_str(), 0x0);
        if (rgb0) {
            SPCSSAttr *color_css = sp_repr_css_attr_new();
            sp_repr_css_set_property(color_css, "fill", SPColor(rgb0).toString().c_str());
            // In the future this could parse opacity, but sp_svg_read_color lacks this.
            sp_repr_css_set_property(color_css, "fill-opacity", "1.0");
            sp_desktop_set_style(desktop, color_css);
            return true;
        }
    }
    return false;
}

/**
 * Applies a pasted path effect to a given item.
 */
void ClipboardManagerImpl::_applyPathEffect(SPItem *item, gchar const *effectstack)
{
    if ( item == nullptr ) {
        return;
    }

    auto lpeitem = cast<SPLPEItem>(item);
    if (lpeitem && effectstack) {
        std::istringstream iss(effectstack);
        std::string href;
        while (std::getline(iss, href, ';'))
        {
            SPObject *obj = sp_uri_reference_resolve(_clipboardSPDoc.get(), href.c_str());
            if (!obj) {
                return;
            }
            auto lpeobj = cast<LivePathEffectObject>(obj);
            if (lpeobj) {
                Inkscape::LivePathEffect::LPESpiro *spiroto = dynamic_cast<Inkscape::LivePathEffect::LPESpiro *>(lpeobj->get_lpe());
                bool has_spiro = lpeitem->hasPathEffectOfType(Inkscape::LivePathEffect::SPIRO);
                Inkscape::LivePathEffect::LPEBSpline *bsplineto = dynamic_cast<Inkscape::LivePathEffect::LPEBSpline *>(lpeobj->get_lpe());
                bool has_bspline = lpeitem->hasPathEffectOfType(Inkscape::LivePathEffect::BSPLINE);
                if ((!spiroto || !has_spiro) && (!bsplineto || !has_bspline)) {
                    lpeitem->addPathEffect(lpeobj);
                }
            }
        }
        // for each effect in the stack, check if we need to fork it before adding it to the item
        lpeitem->forkPathEffectsIfNecessary(1);
    }
}

/**
 * Retrieve the clipboard contents as a document.
 * @return Clipboard contents converted to SPDocument, or NULL if no suitable content was present
 */
std::unique_ptr<SPDocument> ClipboardManagerImpl::_retrieveClipboard(Glib::ustring required_target)
{
    Glib::ustring best_target;
    if ( required_target == "" ) {
        best_target = _getBestTarget();
    } else {
        best_target = required_target;
    }

    if ( best_target == "" ) {
        return nullptr;
    }

    // FIXME: Temporary hack until we add memory input.
    // Save the clipboard contents to some file, then read it
    gchar *filename = g_build_filename( g_get_user_cache_dir(), "inkscape-clipboard-import", nullptr );

    bool file_saved = false;
    Glib::ustring target = best_target;

#ifdef _WIN32
    if (best_target == "CF_ENHMETAFILE" || best_target == "WCF_ENHMETAFILE")
    {   // Try to save clipboard data as en emf file (using win32 api)
        if (OpenClipboard(NULL)) {
            HGLOBAL hglb = GetClipboardData(CF_ENHMETAFILE);
            if (hglb) {
                HENHMETAFILE hemf = CopyEnhMetaFile((HENHMETAFILE) hglb, filename);
                if (hemf) {
                    file_saved = true;
                    target = "image/x-emf";
                    DeleteEnhMetaFile(hemf);
                }
            }
            CloseClipboard();
        }
    }
#endif

    if (!file_saved) {
        if ( !_clipboard->wait_is_target_available(best_target) ) {
            return nullptr;
        }

        // doing this synchronously makes better sense
        // TODO: use another method because this one is badly broken imo.
        // from documentation: "Returns: A SelectionData object, which will be invalid if retrieving the given target failed."
        // I don't know how to check whether an object is 'valid' or not, unusable if that's not possible...
        Gtk::SelectionData sel = _clipboard->wait_for_contents(best_target);
        target = sel.get_target();  // this can crash if the result was invalid of last function. No way to check for this :(

        // FIXME: Temporary hack until we add memory input.
        // Save the clipboard contents to some file, then read it
        g_file_set_contents(filename, (const gchar *) sel.get_data(), sel.get_length(), nullptr);
    }

    // there is no specific plain SVG input extension, so if we can paste the Inkscape SVG format,
    // we use the image/svg+xml mimetype to look up the input extension
    if (target == "image/x-inkscape-svg" || target == "text/plain") {
        target = "image/svg+xml";
    }
    // Use the EMF extension to import metafiles
    if (target == "CF_ENHMETAFILE" || target == "WCF_ENHMETAFILE") {
        target = "image/x-emf";
    }

    Inkscape::Extension::DB::InputList inlist;
    Inkscape::Extension::db.get_input_list(inlist);
    Inkscape::Extension::DB::InputList::const_iterator in = inlist.begin();
    for (; in != inlist.end() && target != (*in)->get_mimetype() ; ++in) {
    };
    if ( in == inlist.end() ) {
        return nullptr; // this shouldn't happen unless _getBestTarget returns something bogus
    }

    SPDocument *tempdoc = nullptr;
    try {
        tempdoc = (*in)->open(filename);
    } catch (...) {
    }
    g_unlink(filename);
    g_free(filename);

    return std::unique_ptr<SPDocument>(tempdoc);
}

#ifdef __APPLE__

// MIME types: image/x-inkscape-svg, image/png, image/svg+xml, image/x-inkscape-svg-compressed, image/svg+xml-compressed, application/pdf, image/svg+xml, application/x-zip, application/tar, image/dxf, image/dxf, image/x-e-postscript, image/x-emf, text/xml+fxg, text/plain, image/hpgl, text/html, image/jpeg, application/x-zip, text/x-tex, text/xml+xaml, text/x-povray-script, image/x-postscript, text/x-povray-script, image/sif, image/tiff, image/webp, image/x-wmf,

template <typename L, typename R>
boost::bimap<L, R> make_bimap(std::initializer_list<typename boost::bimap<L, R>::value_type> list) {
    return boost::bimap<L, R>(list.begin(), list.end());
}

// MIME type to Universal Type Identifiers
static auto mime_uti = make_bimap<std::string, std::string>({
    {"image/x-inkscape-svg", "org.inkscape.svg"},
    {"image/svg+xml",        "public.svg-image"},
    {"image/png",            "public.png"},
    {"image/webp",           "public.webp"},
    {"image/tiff",           "public.tiff"},
    {"image/jpeg",           "public.jpeg"},
    {"image/x-e-postscript", "com.adobe.encapsulated-postscript"},
    {"image/x-postscript",   "com.adobe.postscript"},
    // {"text/plain",           "public.plain-text"}, - GIMP color palette
    {"text/html",            "public.html"},
    {"application/pdf",      "com.adobe.pdf"},
    {"application/tar",      "public.tar-archive"},
    {"application/x-zip",    "public.zip-archive"},
});

#endif

/**
 * Callback called when some other application requests data from Inkscape.
 *
 * Finds a suitable output extension to save the internal clipboard document,
 * then saves it to memory and sets the clipboard contents.
 */
void ClipboardManagerImpl::_onGet(Gtk::SelectionData &sel, guint /*info*/)
{
    if (_clipboardSPDoc == nullptr)
        return;

    Glib::ustring target = sel.get_target();
    g_info("Clipboard _onGet target: %s", target.c_str());

    if (target == "") {
        return; // this shouldn't happen
    }

    if (target == CLIPBOARD_TEXT_TARGET) {
        target = "image/x-inkscape-svg";
    }

#ifdef __APPLE__
    // translate UTI back to MIME
    auto mime = mime_uti.right.find(target);
    if (mime != mime_uti.right.end()) {
        target = mime->get_left();
    }
#endif

    // FIXME: Temporary hack until we add support for memory output.
    // Save to a temporary file, read it back and then set the clipboard contents
    gchar *filename = g_build_filename( g_get_user_cache_dir(), "inkscape-clipboard-export", nullptr );
    gchar *data = nullptr;
    gsize len;

    // XXX This is a crude fix for clipboards accessing extensions
    // Remove when gui is extracted from extension execute and uses exceptions.
    bool previous_gui = INKSCAPE.use_gui();
    INKSCAPE.use_gui(false);

    try {
        Inkscape::Extension::DB::OutputList outlist;
        Inkscape::Extension::db.get_output_list(outlist);
        Inkscape::Extension::DB::OutputList::const_iterator out = outlist.begin();
        for ( ; out != outlist.end() && target != (*out)->get_mimetype() ; ++out) {
        }
        if (!(*out)->loaded()) {
            // Need to load the extension.
            (*out)->set_state(Inkscape::Extension::Extension::STATE_LOADED);
        }

        if ((*out)->is_raster())
        {
            gdouble dpi = Inkscape::Util::Quantity::convert(1, "in", "px");
            guint32 bgcolor = 0x00000000;

            Geom::Point origin (_clipboardSPDoc->getRoot()->x.computed, _clipboardSPDoc->getRoot()->y.computed);
            Geom::Rect area = Geom::Rect(origin, origin + _clipboardSPDoc->getDimensions());

            unsigned long int width = (unsigned long int) (area.width() + 0.5);
            unsigned long int height = (unsigned long int) (area.height() + 0.5);

            // read from namedview
            Inkscape::XML::Node *nv = _clipboardSPDoc->getReprNamedView();
            if (nv && nv->attribute("pagecolor")) {
                bgcolor = sp_svg_read_color(nv->attribute("pagecolor"), 0xffffff00);
            }
            if (nv && nv->attribute("inkscape:pageopacity")) {
                double opacity = nv->getAttributeDouble("inkscape:pageopacity", 1.0);
                bgcolor |= SP_COLOR_F_TO_U(opacity);
            }
            gchar *raster_file = g_build_filename( g_get_user_cache_dir(), "inkscape-clipboard-export-raster", nullptr );
            sp_export_png_file(_clipboardSPDoc.get(), raster_file, area, width, height, dpi, dpi, bgcolor, nullptr,
                               nullptr, true, {});
            (*out)->export_raster(_clipboardSPDoc.get(), raster_file, filename, true);
            unlink(raster_file);
            g_free(raster_file);
        }
        else
        {
            (*out)->save(_clipboardSPDoc.get(), filename, true);
        }
        g_file_get_contents(filename, &data, &len, nullptr);

        sel.set(8, (guint8 const *) data, len);
    } catch (...) {
    }

    INKSCAPE.use_gui(previous_gui);
    g_unlink(filename); // delete the temporary file
    g_free(filename);
    g_free(data);
}

/**
 * Callback when someone else takes the clipboard.
 *
 * When the clipboard owner changes, this callback clears the internal clipboard document
 * to reduce memory usage.
 */
void ClipboardManagerImpl::_onClear()
{
    // why is this called before _onGet???
    //_discardInternalClipboard();
}

/**
 * Creates an internal clipboard document from scratch.
 */
void ClipboardManagerImpl::_createInternalClipboard()
{
    if ( _clipboardSPDoc == nullptr ) {
        _clipboardSPDoc.reset(SPDocument::createNewDoc(nullptr, false, true));
        //g_assert( _clipboardSPDoc != NULL );
        _defs = _clipboardSPDoc->getDefs()->getRepr();
        _doc = _clipboardSPDoc->getReprDoc();
        _root = _clipboardSPDoc->getReprRoot();

        // Preserve ANY copied text kerning
        _root->setAttribute("xml:space", "preserve");

        if (SP_ACTIVE_DOCUMENT) {
            _clipboardSPDoc->setDocumentBase(SP_ACTIVE_DOCUMENT->getDocumentBase());
        }

        _clipnode = _doc->createElement("inkscape:clipboard");
        _root->appendChild(_clipnode);
        Inkscape::GC::release(_clipnode);

        // once we create a SVG document, style will be stored in it, so flush _text_style
        if (_text_style) {
            sp_repr_css_attr_unref(_text_style);
            _text_style = nullptr;
        }
    }
}

/**
 * Deletes the internal clipboard document.
 */
void ClipboardManagerImpl::_discardInternalClipboard()
{
    if (_clipboardSPDoc) {
        _clipboardSPDoc.reset();
        _defs = nullptr;
        _doc = nullptr;
        _root = nullptr;
        _clipnode = nullptr;
    }
}

/**
 * Get the scale to resize an item, based on the command and desktop state.
 */
Geom::Scale ClipboardManagerImpl::_getScale(SPDesktop *desktop, Geom::Point const &min, Geom::Point const &max, Geom::Rect const &obj_rect, bool apply_x, bool apply_y)
{
    double scale_x = 1.0;
    double scale_y = 1.0;

    if (apply_x) {
        scale_x = (max[Geom::X] - min[Geom::X]) / obj_rect[Geom::X].extent();
    }
    if (apply_y) {
        scale_y = (max[Geom::Y] - min[Geom::Y]) / obj_rect[Geom::Y].extent();
    }
    // If the "lock aspect ratio" button is pressed and we paste only a single coordinate,
    // resize the second one by the same ratio too
    if (desktop && desktop->isToolboxButtonActive("lock")) {
        if (apply_x && !apply_y) {
            scale_y = scale_x;
        }
        if (apply_y && !apply_x) {
            scale_x = scale_y;
        }
    }

    return Geom::Scale(scale_x, scale_y);
}

/**
 * Find the most suitable clipboard target.
 */
Glib::ustring ClipboardManagerImpl::_getBestTarget(SPDesktop *desktop)
{
    auto targets = _clipboard->wait_for_targets();

    // clipboard target debugging snippet
    /*
    g_message("Begin clipboard targets");
    for ( std::list<Glib::ustring>::iterator x = targets.begin() ; x != targets.end(); ++x )
        g_message("Clipboard target: %s", (*x).data());
    g_message("End clipboard targets\n");
    //*/

    // Prioritise text when the text tool is active
    if (desktop && dynamic_cast<Inkscape::UI::Tools::TextTool *>(desktop->getTool())) {
        if (_clipboard->wait_is_text_available()) {
            return CLIPBOARD_TEXT_TARGET;
        }
    }

    for (auto & _preferred_target : _preferred_targets)
    {
        if ( std::find(targets.begin(), targets.end(), _preferred_target) != targets.end() ) {
            return _preferred_target;
        }
    }
#ifdef _WIN32
    if (OpenClipboard(NULL))
    {   // If both bitmap and metafile are present, pick the one that was exported first.
        UINT format = EnumClipboardFormats(0);
        while (format) {
            if (format == CF_ENHMETAFILE || format == CF_DIB || format == CF_BITMAP) {
                break;
            }
            format = EnumClipboardFormats(format);
        }
        CloseClipboard();

        if (format == CF_ENHMETAFILE) {
            return "CF_ENHMETAFILE";
        }
        if (format == CF_DIB || format == CF_BITMAP) {
            return CLIPBOARD_GDK_PIXBUF_TARGET;
        }
    }

    if (IsClipboardFormatAvailable(CF_ENHMETAFILE)) {
        return "CF_ENHMETAFILE";
    }
#endif
    if (_clipboard->wait_is_image_available()) {
        return CLIPBOARD_GDK_PIXBUF_TARGET;
    }
    if (_clipboard->wait_is_text_available()) {
        return CLIPBOARD_TEXT_TARGET;
    }

    return "";
}

/**
 * Set the clipboard targets to reflect the mimetypes Inkscape can output.
 */
void ClipboardManagerImpl::_setClipboardTargets()
{
    Inkscape::Extension::DB::OutputList outlist;
    Inkscape::Extension::db.get_output_list(outlist);
    std::vector<Gtk::TargetEntry> target_list;

    bool plaintextSet = false;
    for (Inkscape::Extension::DB::OutputList::const_iterator out = outlist.begin() ; out != outlist.end() ; ++out) {
        if ( !(*out)->deactivated() ) {
            Glib::ustring mime = (*out)->get_mimetype();
#ifdef __APPLE__
            auto uti = mime_uti.left.find(mime);
            if (uti != mime_uti.left.end()) {
                target_list.emplace_back(uti->get_right());
            }
#endif
            if (mime != CLIPBOARD_TEXT_TARGET) {
                if ( !plaintextSet && (mime.find("svg") == Glib::ustring::npos) ) {
                    target_list.emplace_back(CLIPBOARD_TEXT_TARGET);
                    plaintextSet = true;
                }
                target_list.emplace_back(mime);
            }
        }
    }

    // Add PNG export explicitly since there is no extension for this...
    // On Windows, GTK will also present this as a CF_DIB/CF_BITMAP
    target_list.emplace_back( "image/png" );

    _clipboard->set(target_list,
        sigc::mem_fun(*this, &ClipboardManagerImpl::_onGet),
        sigc::mem_fun(*this, &ClipboardManagerImpl::_onClear));

#ifdef _WIN32
    // If the "image/x-emf" target handled by the emf extension would be
    // presented as a CF_ENHMETAFILE automatically (just like an "image/bmp"
    // is presented as a CF_BITMAP) this code would not be needed.. ???
    // Or maybe there is some other way to achieve the same?

    // Note: Metafile is the only format that is rendered and stored in clipboard
    // on Copy, all other formats are rendered only when needed by a Paste command.

    // FIXME: This should at least be rewritten to use "delayed rendering".
    //        If possible make it delayed rendering by using GTK API only.

    if (OpenClipboard(NULL)) {
        if ( _clipboardSPDoc != NULL ) {
            const Glib::ustring target = "image/x-emf";

            Inkscape::Extension::DB::OutputList outlist;
            Inkscape::Extension::db.get_output_list(outlist);
            Inkscape::Extension::DB::OutputList::const_iterator out = outlist.begin();
            for ( ; out != outlist.end() && target != (*out)->get_mimetype() ; ++out) {
            }
            if ( out != outlist.end() ) {
                // FIXME: Temporary hack until we add support for memory output.
                // Save to a temporary file, read it back and then set the clipboard contents
                gchar *filename = g_build_filename( g_get_user_cache_dir(), "inkscape-clipboard-export.emf", nullptr );

                try {
                    (*out)->save(_clipboardSPDoc.get(), filename);
                    HENHMETAFILE hemf = GetEnhMetaFileA(filename);
                    if (hemf) {
                        SetClipboardData(CF_ENHMETAFILE, hemf);
                        DeleteEnhMetaFile(hemf);
                    }
                } catch (...) {
                }
                g_unlink(filename); // delete the temporary file
                g_free(filename);
            }
        }
        CloseClipboard();
    }
#endif
}

/**
 * Set the string representation of a 32-bit RGBA color as the clipboard contents.
 */
void ClipboardManagerImpl::_setClipboardColor(guint32 color)
{
    gchar colorstr[16];
    g_snprintf(colorstr, 16, "%08x", color);
    _clipboard->set_text(colorstr);
}

/**
 * Put a notification on the message stack.
 */
void ClipboardManagerImpl::_userWarn(SPDesktop *desktop, char const *msg)
{
    if(desktop)
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, msg);
}

/* #######################################
          ClipboardManager class
   ####################################### */

ClipboardManager *ClipboardManager::_instance = nullptr;

ClipboardManager::ClipboardManager() = default;
ClipboardManager::~ClipboardManager() = default;
ClipboardManager *ClipboardManager::get()
{
    if ( _instance == nullptr ) {
        _instance = new ClipboardManagerImpl;
    }

    return _instance;
}

} // namespace Inkscape
} // namespace IO

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
