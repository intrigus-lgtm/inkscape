// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * File/Print operations.
 */
/* Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Chema Celorio <chema@celorio.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Bruno Dilly <bruno.dilly@gmail.com>
 *   Stephen Silver <sasilver@users.sourceforge.net>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *   Tavmjong Bah
 *
 * Copyright (C) 2006 Johan Engelen <johan@shouraizou.nl>
 * Copyright (C) 1999-2016 Authors
 * Copyright (C) 2004 David Turner
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

/** @file
 * @note This file needs to be cleaned up extensively.
 * What it probably needs is to have one .h file for
 * the API, and two or more .cpp files for the implementations.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"  // only include where actually required!
#endif

#include <gtkmm.h>

#include "file.h"
#include "inkscape-application.h"
#include "inkscape-window.h"

#include "desktop.h"
#include "document-undo.h"
#include "event-log.h"
#include "id-clash.h"
#include "inkscape-version.h"
#include "inkscape.h"
#include "layer-manager.h"
#include "message-stack.h"
#include "page-manager.h"
#include "path-prefix.h"
#include "print.h"
#include "selection.h"
#include "rdf.h"

#include "extension/db.h"
#include "extension/effect.h"
#include "extension/input.h"
#include "extension/output.h"

#include "io/file.h"
#include "io/resource.h"
#include "io/fix-broken-links.h"
#include "io/sys.h"

#include "object/sp-defs.h"
#include "object/sp-namedview.h"
#include "object/sp-page.h"
#include "object/sp-root.h"
#include "object/sp-use.h"
#include "page-manager.h"
#include "style.h"

#include "ui/dialog/filedialog.h"
#include "ui/icon-names.h"
#include "ui/interface.h"
#include "ui/tools/tool-base.h"

#include "svg/svg.h" // for sp_svg_transform_write, used in sp_import_document
#include "xml/rebase-hrefs.h"
#include "xml/sp-css-attr.h"

using Inkscape::DocumentUndo;
using Inkscape::IO::Resource::TEMPLATES;
using Inkscape::IO::Resource::USER;

/*######################
## N E W
######################*/

/**
 * Create a blank document and add it to the desktop
 * Input: empty string or template file name.
 */
SPDesktop *sp_file_new(const std::string &templ)
{
    auto *app = InkscapeApplication::instance();

    SPDocument* doc = app->document_new (templ);
    if (!doc) {
        std::cerr << "sp_file_new: failed to open document: " << templ << std::endl;
    }
    InkscapeWindow* win = app->window_open (doc);

    SPDesktop* desktop = win->get_desktop();

    return desktop;
}

std::string sp_file_default_template_uri()
{
    return Inkscape::IO::Resource::get_filename(TEMPLATES, "default.svg", true);
}

SPDesktop* sp_file_new_default()
{
    SPDesktop* desk = sp_file_new(sp_file_default_template_uri());
    //rdf_add_from_preferences( SP_ACTIVE_DOCUMENT );

    return desk;
}

/**
 *  Handle prompting user for "do you want to revert"?  Revert on "OK"
 */
void sp_file_revert_dialog()
{
    SPDesktop  *desktop = SP_ACTIVE_DESKTOP;
    g_assert(desktop != nullptr);

    SPDocument *doc = desktop->getDocument();
    g_assert(doc != nullptr);

    Inkscape::XML::Node *repr = doc->getReprRoot();
    g_assert(repr != nullptr);

    gchar const *filename = doc->getDocumentFilename();
    if (!filename) {
        desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("Document not saved yet.  Cannot revert."));
        return;
    }

    bool do_revert = true;
    if (doc->isModifiedSinceSave()) {
        Glib::ustring tmpString = Glib::ustring::compose(_("Changes will be lost! Are you sure you want to reload document %1?"), filename);
        bool response = desktop->warnDialog (tmpString);
        if (!response) {
            do_revert = false;
        }
    }

    bool reverted = false;
    if (do_revert) {
        auto *app = InkscapeApplication::instance();
        reverted = app->document_revert (doc);
    }

    if (reverted) {
        desktop->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("Document reverted."));
    } else {
        desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("Document not reverted."));
    }
}

/**
 *  Display an file Open selector.  Open a document if OK is pressed.
 *  Can select single or multiple files for opening.
 */
void
sp_file_open_dialog(Gtk::Window &parentWindow, gpointer /*object*/, gpointer /*data*/)
{
    // Get the current directory for finding files.
    static std::string open_path;
    Inkscape::UI::Dialog::get_start_directory(open_path, "/dialogs/open/path", true);

    // Create a dialog.
    auto openDialogInstance = Inkscape::UI::Dialog::FileOpenDialog::create(
        parentWindow,
        open_path,
        Inkscape::UI::Dialog::SVG_TYPES,
        _("Select file to open"));

    // Show the dialog.
    bool const success = openDialogInstance->show();

    // Save the folder the user selected for later.
    open_path = openDialogInstance->getCurrentDirectory()->get_path();

    if (!success) {
        return;
    }

    // Open selected files.
    auto *app = InkscapeApplication::instance();
    auto files = openDialogInstance->getFiles();
    for (int i = 0; i < files->get_n_items(); i++) {
        auto file = files->get_typed_object<Gio::File>(i);
        app->create_window(file);
    }

    // Save directory to preferences (if only one file selected as we could have files from
    // multiple directories).
    if (files->get_n_items() == 1) {
        open_path = Glib::path_get_dirname(files->get_typed_object<Gio::File>(0)->get_path());
        open_path.append(G_DIR_SEPARATOR_S);
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        prefs->setString("/dialogs/open/path", open_path);
    }

    return;
}

/*######################
## V A C U U M
######################*/

/**
 * Remove unreferenced defs from the defs section of the document.
 */
void sp_file_vacuum(SPDocument *doc)
{
    unsigned int diff = doc->vacuumDocument();

    DocumentUndo::done(doc, _("Clean up document"), INKSCAPE_ICON("document-cleanup"));

    SPDesktop *dt = SP_ACTIVE_DESKTOP;
    if (dt != nullptr) {
        // Show status messages when in GUI mode
        if (diff > 0) {
            dt->messageStack()->flashF(Inkscape::NORMAL_MESSAGE,
                    ngettext("Removed <b>%i</b> unused definition in &lt;defs&gt;.",
                            "Removed <b>%i</b> unused definitions in &lt;defs&gt;.",
                            diff),
                    diff);
        } else {
            dt->messageStack()->flash(Inkscape::NORMAL_MESSAGE,  _("No unused definitions in &lt;defs&gt;."));
        }
    }
}

/*######################
## S A V E
######################*/

/**
 * This 'save' function called by the others below
 *
 * \param    official  whether to set :output_module and :modified in the
 *                     document; is true for normal save, false for temporary saves
 */
static bool
file_save(Gtk::Window &parentWindow,
          SPDocument *doc,
          const Glib::RefPtr<Gio::File> file,
          Inkscape::Extension::Extension *key,
          bool checkoverwrite,
          bool official,
          Inkscape::Extension::FileSaveMethod save_method)
{
    if (!doc) { //Safety check
        return false;
    }

    auto path = file->get_path();
    auto display_name = file->get_parse_name();

    Inkscape::Version save = doc->getRoot()->version.inkscape;
    doc->getReprRoot()->setAttribute("inkscape:version", Inkscape::version_string);
    try {
        Inkscape::Extension::save(key, doc, file->get_path().c_str(),
                                  checkoverwrite, official,
                                  save_method);
    } catch (Inkscape::Extension::Output::no_extension_found &e) {
        gchar *text = g_strdup_printf(_("No Inkscape extension found to save document (%s).  This may have been caused by an unknown filename extension."), display_name.c_str());
        SP_ACTIVE_DESKTOP->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("Document not saved."));
        sp_ui_error_dialog(text);
        g_free(text);
        // Restore Inkscape version
        doc->getReprRoot()->setAttribute("inkscape:version", sp_version_to_string( save ));
        return false;
    } catch (Inkscape::Extension::Output::file_read_only &e) {
        gchar *text = g_strdup_printf(_("File %s is write protected. Please remove write protection and try again."), display_name.c_str());
        SP_ACTIVE_DESKTOP->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("Document not saved."));
        sp_ui_error_dialog(text);
        g_free(text);
        doc->getReprRoot()->setAttribute("inkscape:version", sp_version_to_string( save ));
        return false;
    } catch (Inkscape::Extension::Output::save_failed &e) {
        gchar *text = g_strdup_printf(_("File %s could not be saved."), display_name.c_str());
        SP_ACTIVE_DESKTOP->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("Document not saved."));
        sp_ui_error_dialog(text);
        g_free(text);
        doc->getReprRoot()->setAttribute("inkscape:version", sp_version_to_string( save ));
        return false;
    } catch (Inkscape::Extension::Output::save_cancelled &e) {
        SP_ACTIVE_DESKTOP->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("Document not saved."));
        doc->getReprRoot()->setAttribute("inkscape:version", sp_version_to_string( save ));
        return false;
    } catch (Inkscape::Extension::Output::export_id_not_found &e) {
        gchar *text = g_strdup_printf(_("File could not be saved:\nNo object with ID '%s' found."), e.id);
        SP_ACTIVE_DESKTOP->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("Document not saved."));
        sp_ui_error_dialog(text);
        g_free(text);
        doc->getReprRoot()->setAttribute("inkscape:version", sp_version_to_string( save ));
        return false;
    } catch (Inkscape::Extension::Output::no_overwrite &e) {
        return sp_file_save_dialog(parentWindow, doc, save_method);
    } catch (std::exception &e) {
        gchar *text = g_strdup_printf(_("File %s could not be saved.\n\n"
                                        "The following additional information was returned by the output extension:\n"
                                        "'%s'"), display_name.c_str(), e.what());
        SP_ACTIVE_DESKTOP->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("Document not saved."));
        sp_ui_error_dialog(text);
        g_free(text);
        doc->getReprRoot()->setAttribute("inkscape:version", sp_version_to_string( save ));
        return false;
    } catch (...) {
        g_critical("Extension '%s' threw an unspecified exception.", key->get_id());
        gchar *text = g_strdup_printf(_("File %s could not be saved."), display_name.c_str());
        SP_ACTIVE_DESKTOP->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("Document not saved."));
        sp_ui_error_dialog(text);
        g_free(text);
        doc->getReprRoot()->setAttribute("inkscape:version", sp_version_to_string( save ));
        return false;
    }

    if (SP_ACTIVE_DESKTOP) {
        if (! SP_ACTIVE_DESKTOP->messageStack()) {
            g_message("file_save: ->messageStack() == NULL. please report to bug #967416");
        }
    } else {
        g_message("file_save: SP_ACTIVE_DESKTOP == NULL. please report to bug #967416");
    }

    doc->get_event_log()->rememberFileSave();
    Glib::ustring msg;
    if (doc->getDocumentFilename() == nullptr) {
        msg = Glib::ustring::format(_("Document saved."));
    } else {
        msg = Glib::ustring::format(_("Document saved."), " ", doc->getDocumentFilename());
    }
    SP_ACTIVE_DESKTOP->messageStack()->flash(Inkscape::NORMAL_MESSAGE, msg.c_str());
    return true;
}

/**
 *  Display a SaveAs dialog.  Save the document if OK pressed.
 */
bool
sp_file_save_dialog(Gtk::Window &parentWindow, SPDocument *doc, Inkscape::Extension::FileSaveMethod save_method)
{
    bool is_copy = (save_method == Inkscape::Extension::FILE_SAVE_METHOD_SAVE_COPY);

    // Note: default_extension has the format "org.inkscape.output.svg.inkscape",
    //       whereas filename_extension only uses ".svg"
    auto default_extension = Inkscape::Extension::get_file_save_extension(save_method);
    auto extension = dynamic_cast<Inkscape::Extension::Output *>(Inkscape::Extension::db.get(default_extension.c_str()));

    std::string filename_extension = ".svg";
    if (extension) {
        filename_extension = extension->get_extension(); // Glib::ustring -> std::string FIXME
    }

    std::string save_path = Inkscape::Extension::get_file_save_path(doc, save_method); // Glib::ustring -> std::string FIXME

    if (!Inkscape::IO::file_test(save_path.c_str(), (GFileTest)(G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))) {
        save_path.clear();
    }

    if (save_path.empty()) {
        save_path = Glib::get_home_dir();
    }

    std::string save_loc = save_path;
    save_loc.append(G_DIR_SEPARATOR_S);

    int i = 1;
    if ( !doc->getDocumentFilename() ) {
        // We are saving for the first time; create a unique default filename
        save_loc = save_loc + _("drawing") + filename_extension;

        while (Inkscape::IO::file_test(save_loc.c_str(), G_FILE_TEST_EXISTS)) {
            save_loc = save_path;
            save_loc.append(G_DIR_SEPARATOR_S);
            save_loc = save_loc + Glib::ustring::compose(_("drawing-%1"), i++) + filename_extension;
        }
    } else {
        save_loc.append(Glib::path_get_basename(doc->getDocumentFilename()));
    }

    // Show the SaveAs dialog.
    char const *dialog_title = is_copy ?
        (char const *) _("Select file to save a copy to") :
        (char const *) _("Select file to save to");

    gchar* doc_title = doc->getRoot()->title();
    auto saveDialog =
        Inkscape::UI::Dialog::FileSaveDialog::create(
            parentWindow,
            save_loc,
            Inkscape::UI::Dialog::SVG_TYPES,
            dialog_title,
            default_extension,
            doc_title ? doc_title : "",
            save_method);

    saveDialog->setExtension(extension); // Use default extension from preferences!

    bool success = saveDialog->show();
    if (!success) {
        if (doc_title) {
            g_free(doc_title);
        }
        return success;
    }

    // set new title here (call RDF to ensure metadata and title element are updated)
    rdf_set_work_entity(doc, rdf_find_entity("title"), saveDialog->getDocTitle().c_str());

    auto file = saveDialog->getFile();
    Inkscape::Extension::Extension *selectionType = saveDialog->getExtension();

    saveDialog.reset();
    if (doc_title) {
        g_free(doc_title);
    }

    if (file_save(parentWindow, doc, file, selectionType, true, !is_copy, save_method)) {

        if (doc->getDocumentFilename()) {
            Glib::RefPtr<Gtk::RecentManager> recent = Gtk::RecentManager::get_default();
            recent->add_item(file->get_uri()); // Gtk4 add_item(file)
        }

        save_path = Glib::path_get_dirname(file->get_path());
        Inkscape::Extension::store_save_path_in_prefs(save_path, save_method);

        return success;
    }

    return false;
}

/**
 * Save a document, displaying a SaveAs dialog if necessary.
 */
bool
sp_file_save_document(Gtk::Window &parentWindow, SPDocument *doc)
{
    bool success = true;

    if (doc->isModifiedSinceSave()) {
        if ( doc->getDocumentFilename() == nullptr )
        {
            // In this case, an argument should be given that indicates that the document is the first
            // time saved, so that .svg is selected as the default and not the last one "Save as ..." extension used
            return sp_file_save_dialog(parentWindow, doc, Inkscape::Extension::FILE_SAVE_METHOD_INKSCAPE_SVG);
        } else {
            std::string path = doc->getDocumentFilename();

            // Try to determine the extension from the filename; this may not lead to a valid extension,
            // but this case is caught in the file_save method below (or rather in Extension::save()
            // further down the line).
            std::string ext;
            std::string::size_type pos = path.rfind('.');
            if (pos != std::string::npos) {
                // FIXME: this could/should be more sophisticated (see FileSaveDialog::appendExtension()),
                // but hopefully it's a reasonable workaround for now
                ext = path.substr( pos );
            }
            auto file = Gio::File::create_for_path(path);
            success = file_save(parentWindow, doc, file, Inkscape::Extension::db.get(ext.c_str()), false, true, Inkscape::Extension::FILE_SAVE_METHOD_SAVE_AS);
            if (success == false) {
                // give the user the chance to change filename or extension
                return sp_file_save_dialog(parentWindow, doc, Inkscape::Extension::FILE_SAVE_METHOD_INKSCAPE_SVG);
            }
        }
    } else {
        Glib::ustring msg;
        if (doc->getDocumentFilename() == nullptr) {
            msg = Glib::ustring::format(_("No changes need to be saved."));
        } else {
            msg = Glib::ustring::format(_("No changes need to be saved."), " ", doc->getDocumentFilename());
        }
        SP_ACTIVE_DESKTOP->messageStack()->flash(Inkscape::WARNING_MESSAGE, msg.c_str());
        success = true;
    }

    return success;
}

/**
 * Save a document.
 */
bool
sp_file_save(Gtk::Window &parentWindow, gpointer /*object*/, gpointer /*data*/)
{
    if (!SP_ACTIVE_DOCUMENT) {
        return false;
    }

    SP_ACTIVE_DESKTOP->messageStack()->flash(Inkscape::IMMEDIATE_MESSAGE, _("Saving document..."));

    sp_namedview_document_from_window(SP_ACTIVE_DESKTOP);
    return sp_file_save_document(parentWindow, SP_ACTIVE_DOCUMENT);
}

/**
 *  Save a document, always displaying the SaveAs dialog.
 */
bool
sp_file_save_as(Gtk::Window &parentWindow, gpointer /*object*/, gpointer /*data*/)
{
    if (!SP_ACTIVE_DOCUMENT) {
        return false;
    }

    sp_namedview_document_from_window(SP_ACTIVE_DESKTOP);
    return sp_file_save_dialog(parentWindow, SP_ACTIVE_DOCUMENT, Inkscape::Extension::FILE_SAVE_METHOD_SAVE_AS);
}

/**
 *  Save a copy of a document, always displaying a sort of SaveAs dialog.
 */
bool
sp_file_save_a_copy(Gtk::Window &parentWindow, gpointer /*object*/, gpointer /*data*/)
{
    if (!SP_ACTIVE_DOCUMENT) {
        return false;
    }

    sp_namedview_document_from_window(SP_ACTIVE_DESKTOP);
    return sp_file_save_dialog(parentWindow, SP_ACTIVE_DOCUMENT, Inkscape::Extension::FILE_SAVE_METHOD_SAVE_COPY);
}

/**
 *  Save a copy of a document as template.
 */
bool
sp_file_save_template(Gtk::Window &parentWindow, Glib::ustring name,
    Glib::ustring author, Glib::ustring description, Glib::ustring keywords,
    bool isDefault)
{
    if (!SP_ACTIVE_DOCUMENT || name.length() == 0)
        return true;

    auto document = SP_ACTIVE_DOCUMENT;

    DocumentUndo::ScopedInsensitive _no_undo(document);

    auto root = document->getReprRoot();
    auto xml_doc = document->getReprDoc();

    auto templateinfo_node = xml_doc->createElement("inkscape:templateinfo");
    Inkscape::GC::release(templateinfo_node);

    auto element_node = xml_doc->createElement("inkscape:name");
    Inkscape::GC::release(element_node);

    element_node->appendChild(xml_doc->createTextNode(name.c_str()));
    templateinfo_node->appendChild(element_node);

    if (author.length() != 0) {

        element_node = xml_doc->createElement("inkscape:author");
        Inkscape::GC::release(element_node);

        element_node->appendChild(xml_doc->createTextNode(author.c_str()));
        templateinfo_node->appendChild(element_node);
    }

    if (description.length() != 0) {

        element_node = xml_doc->createElement("inkscape:shortdesc");
        Inkscape::GC::release(element_node);

        element_node->appendChild(xml_doc->createTextNode(description.c_str()));
        templateinfo_node->appendChild(element_node);

    }

    element_node = xml_doc->createElement("inkscape:date");
    Inkscape::GC::release(element_node);

    element_node->appendChild(xml_doc->createTextNode(
        Glib::DateTime::create_now_local().format("%F").c_str()));
    templateinfo_node->appendChild(element_node);

    if (keywords.length() != 0) {

        element_node = xml_doc->createElement("inkscape:keywords");
        Inkscape::GC::release(element_node);

        element_node->appendChild(xml_doc->createTextNode(keywords.c_str()));
        templateinfo_node->appendChild(element_node);

    }

    root->appendChild(templateinfo_node);

    // Escape filenames for windows users, but filenames are not URIs so
    // Allow UTF-8 and don't escape spaces which are popular chars.
    auto encodedName = Glib::uri_escape_string(name, " ", true);
    encodedName.append(".svg");

    auto path = Inkscape::IO::Resource::get_path_string(USER, TEMPLATES, encodedName.c_str());

    auto operation_confirmed = sp_ui_overwrite_file(path);

    auto file = Gio::File::create_for_path(path);

    if (operation_confirmed) {
        file_save(parentWindow, document, file,
            Inkscape::Extension::db.get(".svg"), false, false,
            Inkscape::Extension::FILE_SAVE_METHOD_INKSCAPE_SVG);

        if (isDefault) {
            // save as "default.svg" by default (so it works independently of UI language), unless
            // a localized template like "default.de.svg" is already present (which overrides "default.svg")
            std::string default_svg_localized = std::string("default.") + _("en") + ".svg";
            path = Inkscape::IO::Resource::get_path_string(USER, TEMPLATES, default_svg_localized.c_str());

            if (!Inkscape::IO::file_test(path.c_str(), G_FILE_TEST_EXISTS)) {
                path = Inkscape::IO::Resource::get_path_string(USER, TEMPLATES, "default.svg");
            }

            file = Gio::File::create_for_path(path);
            file_save(parentWindow, document, file,
                Inkscape::Extension::db.get(".svg"), false, false,
                Inkscape::Extension::FILE_SAVE_METHOD_INKSCAPE_SVG);
        }
    }
    
    // remove this node from current document after saving it as template
    root->removeChild(templateinfo_node);

    return operation_confirmed;
}

/*######################
## I M P O R T
######################*/

/**
 * Paste the contents of a document into the active desktop.
 * @param clipdoc The document to paste
 * @param in_place Whether to paste the selection where it was when copied
 * @pre @c clipdoc is not empty and items can be added to the current layer
 */
void sp_import_document(SPDesktop *desktop, SPDocument *clipdoc, bool in_place, bool on_page)
{
    //TODO: merge with file_import()

    SPDocument *target_document = desktop->getDocument();
    Inkscape::XML::Node *root = clipdoc->getReprRoot();
    auto layer = desktop->layerManager().currentLayer();
    Inkscape::XML::Node *target_parent = layer->getRepr();

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    // Get page manager for on_page pasting, this must be done before selection changes
    Inkscape::PageManager &pm = target_document->getPageManager();
    SPPage *to_page = pm.getSelected();

    auto *node_after = desktop->getSelection()->topRepr();
    if (node_after && prefs->getBool("/options/paste/aboveselected", true) && node_after != target_parent) {
        target_parent = node_after->parent();

        // find parent group
        for (auto p = target_document->getObjectByRepr(node_after->parent()); p; p = p->parent) {
            if (auto parent_group = cast<SPGroup>(p)) {
                layer = parent_group;
                break;
            }
        }
    } else {
        node_after = target_parent->lastChild();
    }

    // copy definitions
    desktop->doc()->importDefs(clipdoc);

    Inkscape::XML::Node* clipboard = nullptr;
    // copy objects
    std::vector<Inkscape::XML::Node*> pasted_objects;
    for (Inkscape::XML::Node *obj = root->firstChild() ; obj ; obj = obj->next()) {
        // Don't copy metadata, defs, named views and internal clipboard contents to the document
        if (!strcmp(obj->name(), "svg:defs")) {
            continue;
        }
        if (!strcmp(obj->name(), "svg:metadata")) {
            continue;
        }
        if (!strcmp(obj->name(), "sodipodi:namedview")) {
            continue;
        }
        if (!strcmp(obj->name(), "inkscape:clipboard")) {
            clipboard = obj;
            continue;
        }

        Inkscape::XML::Node *obj_copy = obj->duplicate(target_document->getReprDoc());
        target_parent->addChild(obj_copy, node_after);
        node_after = obj_copy;
        Inkscape::GC::release(obj_copy);

        pasted_objects.push_back(obj_copy);

        // if we are pasting a clone to an already existing object, its
        // transform is relative to the document, not to its original (see ui/clipboard.cpp)
        auto spobject = target_document->getObjectByRepr(obj_copy);
        auto use = cast<SPUse>(spobject);
        if (use) {
            SPItem *original = use->get_original();
            if (original) {
                Geom::Affine relative_use_transform = original->transform.inverse() * use->transform;
                obj_copy->setAttributeOrRemoveIfEmpty("transform", sp_svg_transform_write(relative_use_transform));
            }
        }
    }

    std::vector<Inkscape::XML::Node*> pasted_objects_not;
    Geom::Affine doc2parent = layer->i2doc_affine().inverse();

    Geom::OptRect from_page;
    if (clipboard) {
        if (clipboard->attribute("page-min")) {
            from_page = Geom::OptRect(clipboard->getAttributePoint("page-min"), clipboard->getAttributePoint("page-max"));
        }

        for (Inkscape::XML::Node *obj = clipboard->firstChild(); obj; obj = obj->next()) {
            if (target_document->getObjectById(obj->attribute("id")))
                continue;
            Inkscape::XML::Node *obj_copy = obj->duplicate(target_document->getReprDoc());
            layer->appendChildRepr(obj_copy);
            Inkscape::GC::release(obj_copy);
            pasted_objects_not.push_back(obj_copy);
        }
    }
    target_document->ensureUpToDate();
    Inkscape::Selection *selection = desktop->getSelection();
    selection->setReprList(pasted_objects_not);

    selection->deleteItems(true);

    // Change the selection to the freshly pasted objects
    selection->setReprList(pasted_objects);
    for (auto item : selection->items()) {
        auto pasted_lpe_item = cast<SPLPEItem>(item);
        if (pasted_lpe_item) {
            sp_lpe_item_enable_path_effects(pasted_lpe_item, false);
        }
    }
    // Apply inverse of parent transform
    selection->applyAffine(desktop->dt2doc() * doc2parent * desktop->doc2dt(), true, false, false);

    // Update (among other things) all curves in paths, for bounds() to work
    target_document->ensureUpToDate();

    // move selection either to original position (in_place) or to mouse pointer
    Geom::OptRect sel_bbox = selection->visualBounds();
    if (sel_bbox) {
        // get offset of selection to original position of copied elements
        Geom::Point pos_original;
        Inkscape::XML::Node *clipnode = sp_repr_lookup_name(root, "inkscape:clipboard", 1);
        if (clipnode) {
            Geom::Point min, max;
            min = clipnode->getAttributePoint("min", min);
            max = clipnode->getAttributePoint("max", max);
            pos_original = Geom::Point(min[Geom::X], max[Geom::Y]);
        }
        Geom::Point offset = pos_original - sel_bbox->corner(3);

        if (!in_place) {
            auto &m = desktop->getNamedView()->snap_manager;
            m.setup(desktop);
            desktop->getTool()->discard_delayed_snap_event();

            // get offset from mouse pointer to bbox center, snap to grid if enabled
            Geom::Point mouse_offset = desktop->point() - sel_bbox->midpoint();
            offset = m.multipleOfGridPitch(mouse_offset - offset, sel_bbox->midpoint() + offset) + offset;
            // Integer align for mouse pasting
            offset = offset.round();
            m.unSetup();
        } else if (on_page && from_page && to_page) {
            // Moving to the same location on a different page requires us to remove the original page translation
            offset *= Geom::Translate(from_page->min()).inverse();
            // Then add the new page's transform on top.
            offset *= Geom::Translate(to_page->getDesktopRect().min());
        }

        selection->moveRelative(offset);
        for (auto po : pasted_objects) {
            auto lpeitem = cast<SPLPEItem>(target_document->getObjectByRepr(po));
            if (lpeitem) {
                sp_lpe_item_enable_path_effects(lpeitem, true);
            }
        }
    }
    target_document->emitReconstructionFinish();
}

/**
 *  Import a resource.  Called by sp_file_import() (Drag and Drop)
 */
SPObject *
file_import(SPDocument *in_doc, const std::string &path, Inkscape::Extension::Extension *key)
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    bool cancelled = false;
    auto prefs = Inkscape::Preferences::get();
    bool onimport = prefs->getBool("/options/onimport", true);

    // Store mouse pointer location before opening any dialogs, so we can drop the item where initially intended.
    auto pointer_location = desktop->point();

    //DEBUG_MESSAGE( fileImport, "file_import( in_doc:%p uri:[%s], key:%p", in_doc, uri, key );
    SPDocument *doc;
    try {
        doc = Inkscape::Extension::open(key, path.c_str());
    } catch (Inkscape::Extension::Input::no_extension_found &e) {
        doc = nullptr;
    } catch (Inkscape::Extension::Input::open_failed &e) {
        doc = nullptr;
    } catch (Inkscape::Extension::Input::open_cancelled &e) {
        doc = nullptr;
        cancelled = true;
    }

    if (onimport && !prefs->getBool("/options/onimport", true)) {
        // Opened instead of imported (onimport set to false in Svg::open)
        prefs->setBool("/options/onimport", true);
        return nullptr;
    } else if (doc != nullptr) {
        // Always preserve any imported text kerning / formatting
        auto root_repr = in_doc->getReprRoot();
        root_repr->setAttribute("xml:space", "preserve");

        Inkscape::XML::rebase_hrefs(doc, in_doc->getDocumentBase(), false);
        Inkscape::XML::Document *xml_in_doc = in_doc->getReprDoc();
        prevent_id_clashes(doc, in_doc, true);
        sp_file_fix_lpe(doc);

        in_doc->importDefs(doc);

        // The extension should set it's pages enabled or disabled when opening
        // in order to indicate if pages are being imported or if objects are.
        if (doc->getPageManager().hasPages()) {
            file_import_pages(in_doc, doc);
            DocumentUndo::done(in_doc, _("Import Pages"), INKSCAPE_ICON("document-import"));
            // This return is only used by dbus in document-interface.cpp (now removed).
            return nullptr;
        }

        SPCSSAttr *style = sp_css_attr_from_object(doc->getRoot());

        // Count the number of top-level items in the imported document.
        guint items_count = 0;
        SPObject *o = nullptr;
        for (auto& child: doc->getRoot()->children) {
            if (is<SPItem>(&child)) {
                items_count++;
                o = &child;
            }
        }

        //ungroup if necessary
        bool did_ungroup = false;
        while(items_count==1 && o && is<SPGroup>(o) && o->children.size()==1){
            std::vector<SPItem *>v;
            sp_item_group_ungroup(cast<SPGroup>(o), v);
            o = v.empty() ? nullptr : v[0];
            did_ungroup=true;
        }

        // Create a new group if necessary.
        Inkscape::XML::Node *newgroup = nullptr;
        const auto & al = style->attributeList();
        if ((style && !al.empty()) || items_count > 1) {
            newgroup = xml_in_doc->createElement("svg:g");
            sp_repr_css_set(newgroup, style, "style");
        }

        // Determine the place to insert the new object.
        // This will be the current layer, if possible.
        // FIXME: If there's no desktop (command line run?) we need
        //        a document:: method to return the current layer.
        //        For now, we just use the root in this case.
        SPObject *place_to_insert;
        if (desktop) {
            place_to_insert = desktop->layerManager().currentLayer();
        } else {
            place_to_insert = in_doc->getRoot();
        }

        // Construct a new object representing the imported image,
        // and insert it into the current document.
        SPObject *new_obj = nullptr;
        for (auto& child: doc->getRoot()->children) {
            if (is<SPItem>(&child)) {
                Inkscape::XML::Node *newitem = did_ungroup ? o->getRepr()->duplicate(xml_in_doc) : child.getRepr()->duplicate(xml_in_doc);

                // convert layers to groups, and make sure they are unlocked
                // FIXME: add "preserve layers" mode where each layer from
                //        import is copied to the same-named layer in host
                newitem->removeAttribute("inkscape:groupmode");
                newitem->removeAttribute("sodipodi:insensitive");

                if (newgroup) newgroup->appendChild(newitem);
                else new_obj = place_to_insert->appendChildRepr(newitem);
            }

            // don't lose top-level defs or style elements
            else if (child.getRepr()->type() == Inkscape::XML::NodeType::ELEMENT_NODE) {
                const gchar *tag = child.getRepr()->name();
                if (!strcmp(tag, "svg:style")) {
                    in_doc->getRoot()->appendChildRepr(child.getRepr()->duplicate(xml_in_doc));
                }
            }
        }
        in_doc->emitReconstructionFinish();
        if (newgroup) new_obj = place_to_insert->appendChildRepr(newgroup);

        // release some stuff
        if (newgroup) Inkscape::GC::release(newgroup);
        if (style) sp_repr_css_attr_unref(style);

        // select and move the imported item
        if (new_obj && is<SPItem>(new_obj)) {
            Inkscape::Selection *selection = desktop->getSelection();
            selection->set(cast<SPItem>(new_obj));

            // preserve parent and viewBox transformations
            // c2p is identity matrix at this point unless ensureUpToDate is called
            doc->ensureUpToDate();
            Geom::Affine affine = doc->getRoot()->c2p * cast<SPItem>(place_to_insert)->i2doc_affine().inverse();
            selection->applyAffine(desktop->dt2doc() * affine * desktop->doc2dt(), true, false, false);

            // move to mouse pointer
            {
                desktop->getDocument()->ensureUpToDate();
                Geom::OptRect sel_bbox = selection->visualBounds();
                if (sel_bbox) {
                    Geom::Point m(pointer_location.round() - sel_bbox->midpoint());
                    selection->moveRelative(m, false);
                }
            }
        }
        
        DocumentUndo::done(in_doc, _("Import"), INKSCAPE_ICON("document-import"));
        return new_obj;
    } else if (!cancelled) {
        gchar *text = g_strdup_printf(_("Failed to load the requested file %s"), path.c_str());
        sp_ui_error_dialog(text);
        g_free(text);
    }

    return nullptr;
}

/**
 * Import the given document as a set of multiple pages and append to this one.
 *
 * @param this_doc - Our current document, to be changed
 * @param that_doc - The documennt that contains our importable pages
 */
void file_import_pages(SPDocument *this_doc, SPDocument *that_doc)
{
    auto &this_pm = this_doc->getPageManager();
    auto &that_pm = that_doc->getPageManager();
    auto this_root = this_doc->getReprRoot();
    auto that_root = that_doc->getReprRoot();

    // Make sure objects have visualBounds created for import
    that_doc->ensureUpToDate();
    this_pm.enablePages();

    Geom::Affine tr = Geom::Translate(this_pm.nextPageLocation() * this_doc->getDocumentScale());
    for (auto &that_page : that_pm.getPages()) {
        auto this_page = this_pm.newDocumentPage(that_page->getDocumentRect() * tr);
        // Set the margin, bleed, etc
        this_page->copyFrom(that_page);
    }

    // Unwind the document scales for the imported objects
    tr = this_doc->getDocumentScale().inverse() * that_doc->getDocumentScale() * tr;
    Inkscape::ObjectSet set(this_doc);
    for (Inkscape::XML::Node *that_repr = that_root->firstChild(); that_repr; that_repr = that_repr->next()) {
        // Don't copy metadata, defs, named views and internal clipboard contents to the document
        if (!strcmp(that_repr->name(), "svg:defs") ||
            !strcmp(that_repr->name(), "svg:metadata") ||
            !strcmp(that_repr->name(), "sodipodi:namedview")) {
            continue;
        }

        auto this_repr = that_repr->duplicate(this_doc->getReprDoc());
        this_root->addChild(this_repr, this_root->lastChild());
        Inkscape::GC::release(this_repr);
        if (auto this_item = this_doc->getObjectByRepr(this_repr)) {
            set.add(this_item);
        }
    }
    set.applyAffine(tr, true, false, true);
}

/**
 *  Display an Open dialog, import a resource if OK pressed.
 */
void
sp_file_import(Gtk::Window &parentWindow)
{
    SPDocument *doc = SP_ACTIVE_DOCUMENT;
    if (!doc) {
        return;
    }

    static std::string import_path;
    Inkscape::UI::Dialog::get_start_directory(import_path, "/dialogs/import/path");

    // Create new dialog (don't use an old one, because parentWindow has probably changed)
    auto importDialogInstance =
        Inkscape::UI::Dialog::FileOpenDialog::create(
            parentWindow,
            import_path,
            Inkscape::UI::Dialog::IMPORT_TYPES,
            (char const *)_("Select file to import"));

    bool success = importDialogInstance->show();
    if (!success) {
        return;
    }

    auto files = importDialogInstance->getFiles();
    auto extension = importDialogInstance->getExtension();
    for (int i = 0; i < files->get_n_items(); i++) {
        auto file = files->get_typed_object<Gio::File>(i);
        file_import(doc, file->get_path(), extension);
    }

    // Save directory to preferences (if only one file selected as we could have files from
    // multiple directories).
    if (files->get_n_items() == 1) {
        import_path = Glib::path_get_dirname(files->get_typed_object<Gio::File>(0)->get_path());
        import_path.append(G_DIR_SEPARATOR_S);
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        prefs->setString("/dialogs/import/path", import_path);
    }

    return;
}

/*######################
## P R I N T
######################*/

/**
 *  Print the current document, if any.
 */
void
sp_file_print(Gtk::Window& parentWindow)
{
    SPDocument *doc = SP_ACTIVE_DOCUMENT;
    if (doc) {
        sp_print_document(parentWindow, doc);
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
