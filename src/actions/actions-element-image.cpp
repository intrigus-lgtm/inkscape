// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Gio::Actions for use with <image>.
 *
 * Copyright (C) 2022 Tavmjong Bah
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 *
 */

#include "actions-element-image.h"
#include "actions-helper.h"

#include <iostream>

#include <giomm.h>  // Not <gtkmm.h>! To eventually allow a headless version!
#include <gtkmm.h>  // OK, we lied. We pop-up an message dialog if external editor not found and if we have a GUI.
#include <glibmm/i18n.h>

#include "desktop.h"
#include "document.h"
#include "document-undo.h"
#include "inkscape-application.h"
#include "inkscape-window.h"
#include "message-stack.h"
#include "preferences.h"
#include "selection.h"            // Selection

#include "object/sp-image.h"
#include "object/sp-clippath.h"
#include "object/sp-rect.h"
#include "ui/dialog-run.h"
#include "ui/tools/select-tool.h"
#include "util/format_size.h"
#include "xml/href-attribute-helper.h"

Glib::ustring image_get_editor_name(bool is_svg)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    Glib::ustring editor;
    if (is_svg) {
        editor = prefs->getString("/options/svgeditor/value", "inkscape");
    } else {
        editor = prefs->getString("/options/bitmapeditor/value", "gimp");
    }
    return editor;
}

// Note that edits are external to Inkscape and thus we cannot undo them!
void image_edit(InkscapeApplication *app)
{
    auto selection = app->get_active_selection();
    if (selection->isEmpty()) {
        // Nothing to do.
        return;
    }

    auto document = selection->document();

    for (auto item : selection->items()) {
        auto image = cast<SPImage>(item);
        if (image) {

            Inkscape::XML::Node *node = item->getRepr();
            const gchar *href = Inkscape::getHrefAttribute(*node).second;
            if (!href) {
                show_output("image_edit: no xlink:href");
                continue;
            }

            if (strncmp (href, "data", 4) == 0) {
                show_output("image_edit: cannot edit embedded image");
                continue;
            }

            // Find filename.
            std::string filename = href;
            if (strncmp (href, "file", 4) == 0) {
                filename = Glib::filename_from_uri(href);
            }

            if (Glib::path_is_absolute(filename)) {
                // Do nothing
            } else if (document->getDocumentBase()) {
                filename = Glib::build_filename(document->getDocumentBase(), filename);
            } else {
                filename = Glib::build_filename(Glib::get_current_dir(), filename);
            }

            // Bitmap or SVG?
            bool is_svg = false;
            if (filename.substr(filename.find_last_of(".") + 1) == "SVG" ||
                filename.substr(filename.find_last_of(".") + 1) == "svg") {
                is_svg = true;
            }

            // Get editor.
            auto editor = image_get_editor_name(is_svg);

#ifdef _WIN32
            // Parsing is done according to Unix shell rules, need to enclose editor path by single
            // quotes (everything before file extension).
            int            index = editor.find(".exe");
            if (index < 0) index = editor.find(".bat");
            if (index < 0) index = editor.find(".com");
            if (index < 0) index = editor.length();

            editor.insert(index, "'");
            editor.insert(0, "'");
#endif
            Glib::ustring command = editor + " '" + filename + "'";

            GError* error = nullptr;
            g_spawn_command_line_async(command.c_str(), &error);
            if (error) {
                Glib::ustring message = _("Failed to edit external image.\n<small>Note: Path to editor can be set in Preferences dialog.</small>");
                Glib::ustring message2 = Glib::ustring::compose(_("System error message: %1"), error->message);
                if (auto window = app->get_active_window()) {
                    auto dialog = std::make_unique<Gtk::MessageDialog>(*window, message, true, Gtk::MessageType::WARNING, Gtk::ButtonsType::OK);
                    dialog->property_destroy_with_parent() = true;
                    dialog->set_name("SetEditorDialog");
                    dialog->set_title(_("External Edit Image:"));
                    dialog->set_secondary_text(message2);
                    Inkscape::UI::dialog_show_modal_and_selfdestruct(std::move(dialog));
                } else {
                    show_output(Glib::ustring("image_edit: ") + message.raw());
                }
                g_error_free(error);
            }
        }
    }
}

/**
 * Attempt to crop an image's physical pixels by the rectangle give
 * OR if not specified, by any applied clipping object.
 */
void image_crop(InkscapeApplication *app)
{
    auto win = app->get_active_window();
    auto doc = app->get_active_document();
    auto msg = win->get_desktop()->messageStack();
    auto const tool = win->get_desktop()->getTool();
    int done = 0;
    int bytes = 0;

    auto selection = app->get_active_selection();
    if (selection->isEmpty()) {
        msg->flash(Inkscape::ERROR_MESSAGE, _("Nothing selected."));
        return;
    }

    // Find a target rectangle, if provided.
    Geom::OptRect target;
    SPRect *rect = nullptr;
    for (auto item : selection->items()) {
        rect = cast<SPRect>(item);
        if (rect) {
            target = rect->geometricBounds(rect->i2doc_affine());
            break;
        }
    }

    // For each selected item, we loop through and attempt to crop the
    // raster image to the geometric bounds of the clipping object.
    for (auto item : selection->items()) {
        if (auto image = cast<SPImage>(item)) {
            bytes -= std::strlen(image->href);
            Geom::OptRect area;
            if (target) {
                // MODE A. Crop to selected rectangle.
                area = target;
            } else if (auto clip = image->getClipObject()) {
                // MODE B. Crop to image's xisting clip region
                area = clip->geometricBounds(image->i2doc_affine());
            }
            done += (int)(area && image->cropToArea(*area));
            bytes += std::strlen(image->href);
        }
    }
    if (rect) {
        rect->deleteObject();
    }

    // Tell the user what happened, since so many things could have changed.
    if (done) {
        // The select tool has no idea the image description needs updating. Force it.
        if (auto selector = dynamic_cast<Inkscape::UI::Tools::SelectTool*>(tool)) {
            selector->updateDescriber(selection);
        }
        std::stringstream ss;
        ss << ngettext("<b>%d</b> image cropped", "<b>%d</b> images cropped", done);
        if (bytes < 0) {
            ss << ", " << ngettext("%s byte removed", "%s bytes removed", abs(bytes));
        } else if (bytes > 0) {
            ss << ", <b>" << ngettext("%s byte added!", "%s bytes added!", bytes) << "</b>";
        }
        // Do flashing after select tool update.
        msg->flashF(Inkscape::INFORMATION_MESSAGE, ss.str().c_str(), done, Inkscape::Util::format_size(abs(bytes)).c_str());
        Inkscape::DocumentUndo::done(doc, "ActionImageCrop", "Crop Images");
    } else {
        msg->flash(Inkscape::WARNING_MESSAGE, _("No images cropped!"));
    }
}

std::vector<std::vector<Glib::ustring>> raw_data_element_image =
{
    // clang-format off
    {"app.element-image-crop",          N_("Crop image to clip"),  "Image",    N_("Remove parts of the image outside the applied clipping area.") },
    {"app.element-image-edit",          N_("Edit externally"),   "Image",    N_("Edit image externally (image must be selected and not embedded).")    },
    // clang-format on
};

void
add_actions_element_image(InkscapeApplication* app)
{
    auto *gapp = app->gio_app();

    // clang-format off
    gapp->add_action(                "element-image-crop",          sigc::bind(sigc::ptr_fun(&image_crop),      app));
    gapp->add_action(                "element-image-edit",          sigc::bind(sigc::ptr_fun(&image_edit),      app));
    // clang-format on

    app->get_action_extra_data().add_data(raw_data_element_image);
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
