// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * TODO: insert short description here
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
/**
 * @file-update
 * Operations to bump files from the pre-0.92 era into the 0.92+ era
 * (90dpi vs 96dpi, line height problems, and related bugs)
 */
/* Authors:
 * Tavmjong Bah
 * Marc Jeanmougin
 * su_v
 */

#include <clocale>
#include <gtkmm.h>
#include <string>
#include <vector>

#include "desktop.h"
#include "display/control/canvas-item-grid.h"
#include "document.h"
#include "document-undo.h"
#include "extension/db.h"
#include "extension/effect.h"
#include "extension/input.h"
#include "extension/output.h"
#include "extension/system.h"
#include "file.h"
#include "inkscape.h"
#include "io/dir-util.h"
#include "io/sys.h"
#include "live_effects/effect.h"
#include "live_effects/lpeobject.h"
#include "message.h"
#include "message-stack.h"
#include "object/filters/composite.h"
#include "object/persp3d.h"
#include "object/sp-defs.h"
#include "object/sp-flowdiv.h"
#include "object/sp-flowtext.h"
#include "object/sp-grid.h"
#include "object/sp-guide.h"
#include "object/sp-item.h"
#include "object/sp-namedview.h"
#include "object/sp-object.h"
#include "object/sp-root.h"
#include "object/sp-text.h"
#include "object/sp-tspan.h"
#include "preferences.h"
#include "print.h"
#include "proj_pt.h"
#include "selection-chemistry.h"
#include "style.h"
#include "text-editing.h"
#include "ui/dialog-run.h"
#include "ui/pack.h"
#include "ui/shape-editor.h"

using Inkscape::DocumentUndo;

int sp_file_convert_dpi_method_commandline = -1; // Unset

bool is_line(SPObject *i)
{
    if (!(i->getAttribute("sodipodi:role")))
        return false;
    return !strcmp(i->getAttribute("sodipodi:role"), "line");
}

void fix_blank_line(SPObject *o)
{
    if (is<SPText>(o))
        ((SPText *)o)->rebuildLayout();
    else if (is<SPFlowtext>(o))
        ((SPFlowtext *)o)->rebuildLayout();

    SPIFontSize fontsize = o->style->font_size;
    SPILengthOrNormal lineheight = o->style->line_height;
    std::vector<SPObject *> cl = o->childList(false);
    bool beginning = true;
    for (std::vector<SPObject *>::const_iterator ci = cl.begin(); ci != cl.end(); ++ci) {
        SPObject *i = *ci;
        if ((is<SPTSpan>(i) && is_line(i)) || is<SPFlowpara>(i) || is<SPFlowdiv>(i)) {
            if (sp_text_get_length((SPItem *)i) <= 1) { // empty line
                Inkscape::Text::Layout::iterator pos =
                    te_get_layout((SPItem *)(o))
                        ->charIndexToIterator(
                            ((is<SPFlowpara>(i) || is<SPFlowdiv>(i)) ? 0 : ((ci == cl.begin()) ? 0 : 1)) +
                            sp_text_get_length_upto(o, i));
                sp_te_insert((SPItem *)o, pos, "\u00a0"); //"\u00a0"
                gchar *l = g_strdup_printf("%f", lineheight.value);
                gchar *f = g_strdup_printf("%f", fontsize.value);
                i->style->line_height.readIfUnset(l);
                if (!beginning)
                    i->style->font_size.read(f);
                else
                    i->style->font_size.readIfUnset(f);
                g_free(l);
                g_free(f);
            } else {
                beginning = false;
                fontsize = i->style->font_size;
                lineheight = o->style->line_height;
            }
        }
    }
}

void fix_line_spacing(SPObject *o)
{
    SPILengthOrNormal lineheight = o->style->line_height;
    bool inner = false;
    std::vector<SPObject *> cl = o->childList(false);
    for (auto i : cl) {
        if ((is<SPTSpan>(i) && is_line(i)) || is<SPFlowpara>(i) || is<SPFlowdiv>(i)) {
            // if no line-height attribute, set it
            gchar *l = g_strdup_printf("%f", lineheight.value);
            i->style->line_height.readIfUnset(l);
            g_free(l);
        }
        inner = true;
    }
    if (inner) {
        if (is<SPText>(o)) {
            o->style->line_height.read("0.00%");
        } else {
            o->style->line_height.read("0.01%");
        }
    }
}

void fix_font_name(SPObject *o)
{
    std::vector<SPObject *> cl = o->childList(false);
    for (auto ci : cl)
        fix_font_name(ci);
    std::string prev = o->style->font_family.value();
    if (prev == "Sans")
        o->style->font_family.read("sans-serif");
    else if (prev == "Serif")
        o->style->font_family.read("serif");
    else if (prev == "Monospace")
        o->style->font_family.read("monospace");
}

void fix_font_size(SPObject *o)
{
    SPIFontSize fontsize = o->style->font_size;
    if (!fontsize.set)
        return;
    bool inner = false;
    std::vector<SPObject *> cl = o->childList(false);
    for (auto i : cl) {
        fix_font_size(i);
        if ((is<SPTSpan>(i) && is_line(i)) || is<SPFlowpara>(i) || is<SPFlowdiv>(i)) {
            inner = true;
            gchar *s = g_strdup_printf("%f", fontsize.value);
            if (fontsize.set)
                i->style->font_size.readIfUnset(s);
            g_free(s);
        }
    }
    if (inner && (is<SPText>(o) || is<SPFlowtext>(o)))
        o->style->font_size.clear();
}

void fix_osb(SPObject *i)
{
    if (auto a = i->getAttribute("osb:paint")) {
        i->setAttribute("inkscape:swatch", a);
        i->setAttribute("osb:paint", nullptr);
        i->updateRepr();
    }
}

// helper function
void sp_file_text_run_recursive(void (*f)(SPObject *), SPObject *o)
{
    if (is<SPText>(o) || is<SPFlowtext>(o))
        f(o);
    else {
        std::vector<SPObject *> cl = o->childList(false);
        for (auto ci : cl)
            sp_file_text_run_recursive(f, ci);
    }
}

void fix_update(SPObject *o)
{
    o->style->write();
    o->updateRepr();
}

void sp_file_convert_text_baseline_spacing(SPDocument *doc)
{
    char *oldlocale = g_strdup(setlocale(LC_NUMERIC, nullptr));
    setlocale(LC_NUMERIC, "C");
    sp_file_text_run_recursive(fix_blank_line, doc->getRoot());
    sp_file_text_run_recursive(fix_line_spacing, doc->getRoot());
    sp_file_text_run_recursive(fix_font_size, doc->getRoot());
    setlocale(LC_NUMERIC, oldlocale);
    g_free(oldlocale);

    sp_file_text_run_recursive(fix_update, doc->getRoot());
}

void sp_file_fix_osb(SPObject *o)
{
    fix_osb(o);
    std::vector<SPObject *> cl = o->childList(false);
    for (auto ci : cl)
        sp_file_fix_osb(ci);
}

/**
 * Implements a fix for https://gitlab.com/inkscape/inkscape/-/issues/45
 * Line spacing for empty lines was handled differently before 1.0
 * and in particular with the first empty lines or with how style attributes
 * are processed in empty lines (line  =  tspan with sodipodi:role="line")
 *
 * This function "fixes" a text element in a old document by removing the
 * first empty lines and style attrs on other empty lines.
 *
 * */
void _fix_pre_v1_empty_lines(SPObject *o)
{
    std::vector<SPObject *> cl = o->childList(false);
    bool begin = true;
    std::string cur_y = "";
    for (auto ci : cl) {
        if (!is<SPTSpan>(ci))
            continue;
        if (!is_line(ci))
            continue;
        if (!ci->childList(false).empty()) {
            if (begin)
                cur_y = ci->getAttribute("y") ? ci->getAttribute("y") : cur_y;
            begin = false;
        } else {
            ci->removeAttribute("style");
            ci->updateRepr();
            if (begin) {
                ci->deleteObject();
            }
        }
        if (cur_y != "")
            o->setAttribute("y", cur_y);
    }
}

void sp_file_fix_empty_lines(SPDocument *doc)
{
    sp_file_text_run_recursive(_fix_pre_v1_empty_lines, doc->getRoot());
    sp_file_text_run_recursive(fix_update, doc->getRoot());
}

void sp_file_convert_font_name(SPDocument *doc)
{
    sp_file_text_run_recursive(fix_font_name, doc->getRoot());
    sp_file_text_run_recursive(fix_update, doc->getRoot());
}

// Quick and dirty internal backup function
bool sp_file_save_backup(Glib::ustring uri)
{
    Glib::ustring out = uri;
    out.insert(out.find(".svg"), "_backup");

    FILE *filein = Inkscape::IO::fopen_utf8name(uri.c_str(), "rb");
    if (!filein) {
        std::cerr << "sp_file_save_backup: failed to open: " << uri.raw() << std::endl;
        return false;
    }

    FILE *fileout = Inkscape::IO::fopen_utf8name(out.c_str(), "wb");
    if (!fileout) {
        std::cerr << "sp_file_save_backup: failed to open: " << out.raw() << std::endl;
        fclose(filein);
        return false;
    }

    int ch;
    while ((ch = fgetc(filein)) != EOF) {
        fputc(ch, fileout);
    }
    fflush(fileout);

    bool return_value = true;
    if (ferror(fileout)) {
        std::cerr << "sp_file_save_backup: error when writing to: " << out.raw() << std::endl;
        return_value = false;
    }

    fclose(filein);
    fclose(fileout);

    return return_value;
}

int gui_request_dpi_fix_method(SPDocument *doc)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Gtk::Dialog scale_dialog(_("Convert legacy Inkscape file"));
    scale_dialog.set_transient_for(*(INKSCAPE.active_desktop()->getToplevel()));
    scale_dialog.set_margin(10);
    scale_dialog.set_resizable(false);
    Gtk::Label explanation;
    explanation.set_markup(Glib::ustring("<b>") + doc->getDocumentName() + "</b>\n" +
                           _("was created in an older version of Inkscape (90 DPI) and we need "
                             "to make it compatible with newer versions (96 DPI). Tell us about this file:\n"));
    explanation.set_wrap(true);
    explanation.set_size_request(600, -1);

    Gtk::Label choice1_label;
    choice1_label.set_markup(_("This file contains digital artwork for screen display. <b>(Choose if unsure.)</b>"));
    Gtk::CheckButton choice1;
    choice1.set_child(choice1_label);
    Gtk::CheckButton choice2(_("This file is intended for physical output, such as paper or 3D prints."));
    choice2.set_group(choice1);
    Gtk::Label choice2_1_label;
    choice2_1_label.set_markup(_("The appearance of elements such as clips, masks, filters, and clones\n"
                                 "is most important. <b>(Choose if unsure.)</b>"));
    Gtk::CheckButton choice2_1;
    choice2_1.set_child(choice2_1_label);
    Gtk::CheckButton choice2_2(_("The accuracy of the physical unit size and position values of objects\n"
                                 "in the file is most important. (Experimental.)"));
    choice2_2.set_group(choice2_1);
    Gtk::CheckButton backup_button(_("Create a backup file in same directory."));
    Gtk::Expander moreinfo(_("More details..."));
    Gtk::Label moreinfo_text("", Gtk::Align::START);
    moreinfo_text.set_markup(
        // TRANSLATORS: Please don't translate link unless the page exists in your language. Add your language
        // code to the link this way: https://inkscape.org/[lang]/learn/faq#dpi_change
        _("<small>We've updated Inkscape to follow the CSS standard of 96 DPI for "
          "better browser compatibility; we used to use 90 DPI. Digital artwork for screen\n"
          "display will be converted to 96 DPI without scaling and should be unaffected.\n"
          "Artwork drawn at 90 DPI for a specific physical size will be too small if "
          "converted to 96 DPI without any scaling. There are two scaling methods:\n\n"
          "<b>Scaling the whole document:</b> The least error-prone method, this preserves "
          "the appearance of the artwork, including filters and the position of masks, etc. \n"
          "The scale of the artwork relative to the document size may not be accurate.\n\n"
          "<b>Scaling individual elements in the artwork:</b> This method is less reliable "
          "and can result in a changed appearance, \nbut is better for physical output that "
          "relies on accurate sizes and positions (for example, for 3D printing.)\n\n"
          "More information about this change are available in the <a "
          "href='https://inkscape.org/en/learn/faq#dpi_change'>Inkscape FAQ</a>"
          "</small>"));
    moreinfo_text.set_wrap(true);
    moreinfo_text.set_margin_bottom(20);
    moreinfo_text.set_margin_top(20);
    moreinfo_text.set_margin_start(30);
    moreinfo_text.set_margin_end(15);

    Gtk::Box b;
    Inkscape::UI::pack_start(b, choice2_1, false, false, 4);
    Inkscape::UI::pack_start(b, choice2_2, false, false, 4);
    choice2_1.set_visible(true);
    choice2_2.set_visible(true);

    b.set_halign(Gtk::Align::START);
    b.set_valign(Gtk::Align::START);
    b.set_hexpand(false);
    b.set_vexpand(false);
    b.set_margin_start(30);

    Gtk::Box *content = scale_dialog.get_content_area();
    Gtk::Button *ok_button = scale_dialog.add_button(_("OK"), GTK_RESPONSE_ACCEPT);
    bool backup = prefs->getBool("/options/dpifixbackup", true);
    backup_button.set_active(backup);
    // clang-format off
    Inkscape::UI::pack_start(*content, explanation,   false, false, 5);
    Inkscape::UI::pack_start(*content, choice1,       false, false, 5);
    Inkscape::UI::pack_start(*content, choice2,       false, false, 5);
    Inkscape::UI::pack_start(*content, b,             false, false, 5);
    Inkscape::UI::pack_start(*content, backup_button, false, false, 5);
    Inkscape::UI::pack_start(*content, moreinfo,      false, false, 5);
    // clang-format on
    moreinfo.set_child(moreinfo_text);
    b.set_visible(false);
    choice1.signal_toggled().connect(sigc::mem_fun(b, &Gtk::Box::hide));
    choice2.signal_toggled().connect(sigc::mem_fun(b, &Gtk::Box::show));

    int response = prefs->getInt("/options/dpiupdatemethod", FILE_DPI_UNCHANGED);
    if (response != FILE_DPI_UNCHANGED) {
        choice2.set_active();
        b.set_visible(true);
        if (response == FILE_DPI_DOCUMENT_SCALED) {
            choice2_2.set_active();
        }
    }
    ok_button->grab_focus();

    int status = Inkscape::UI::dialog_run(scale_dialog);
    if (status == GTK_RESPONSE_ACCEPT) {
        backup = backup_button.get_active();
        prefs->setBool("/options/dpifixbackup", backup);
        response = choice1.get_active()     ? FILE_DPI_UNCHANGED
                   : choice2_1.get_active() ? FILE_DPI_VIEWBOX_SCALED
                                            : FILE_DPI_DOCUMENT_SCALED;
        prefs->setInt("/options/dpiupdatemethod", response);
    }
    return response;
}

void sp_file_convert_dpi(SPDocument *doc)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    SPRoot *root = doc->getRoot();

    // See if we need to offer the user a fix for the 90->96 px per inch change.
    // std::cout << "SPFileOpen:" << std::endl;
    // std::cout << "  Version: " << sp_version_to_string(root->version.inkscape) << std::endl;
    // std::cout << "  SVG file from old Inkscape version detected: "
    //           << sp_version_to_string(root->version.inkscape) << std::endl;
    static const double ratio = 90.0 / 96.0;

    bool need_fix_viewbox = false;
    bool need_fix_units = false;
    bool need_fix_guides = false;
    bool need_fix_grid_mm = false;
    bool need_fix_box3d = false;
    bool did_scaling = false;

    // Check if potentially need viewbox or unit fix
    switch (root->width.unit) {
        case SVGLength::PC:
        case SVGLength::PT:
        case SVGLength::MM:
        case SVGLength::CM:
        case SVGLength::INCH:
            need_fix_viewbox = true;
            break;
        case SVGLength::NONE:
        case SVGLength::PX:
            need_fix_units = true;
            break;
        case SVGLength::EM:
        case SVGLength::EX:
        case SVGLength::PERCENT:
            // OK
            break;
        default:
            std::cerr << "sp_file_convert_dpi: Unhandled width unit!" << std::endl;
    }

    switch (root->height.unit) {
        case SVGLength::PC:
        case SVGLength::PT:
        case SVGLength::MM:
        case SVGLength::CM:
        case SVGLength::INCH:
            need_fix_viewbox = true;
            break;
        case SVGLength::NONE:
        case SVGLength::PX:
            need_fix_units = true;
            break;
        case SVGLength::EM:
        case SVGLength::EX:
        case SVGLength::PERCENT:
            // OK
            break;
        default:
            std::cerr << "sp_file_convert_dpi: Unhandled height unit!" << std::endl;
    }

    if (need_fix_units && need_fix_viewbox) {
        std::cerr << "Different units in document size !" << std::endl;
        if (root->viewBox_set) {
            need_fix_viewbox = false;
        } else {
            need_fix_units = false;
        }
    }

    // std::cout << "Absolute SVG units in root? " << (need_fix_viewbox?"true":"false") << std::endl;
    // std::cout << "User units in root? "         << (need_fix_units  ?"true":"false") << std::endl;

    if ((!root->viewBox_set && need_fix_viewbox) || need_fix_units) {
        int response = FILE_DPI_UNCHANGED; // default
        if (sp_file_convert_dpi_method_commandline != -1) {
            // Method was set by CLI
            response = sp_file_convert_dpi_method_commandline;
        } else if (INKSCAPE.use_gui()) {
            response = gui_request_dpi_fix_method(doc);
        }

        bool backup = prefs->getBool("/options/dpifixbackup", true);
        if (backup && (response != FILE_DPI_UNCHANGED)) {
            const char *filename = doc->getDocumentFilename();
            if (filename) {
                sp_file_save_backup(Glib::ustring(filename));
            }
        }

        if (!(response == FILE_DPI_UNCHANGED && need_fix_units)) {
            need_fix_guides = true; // Only fix guides if drawing scaled
            need_fix_box3d = true;
        }

        if (response == FILE_DPI_VIEWBOX_SCALED) {
            double ratio_viewbox = need_fix_units ? 1.0 : ratio;

            doc->setViewBox(Geom::Rect::from_xywh(0, 0, doc->getWidth().value("px") * ratio_viewbox,
                                                  doc->getHeight().value("px") * ratio_viewbox));
            Inkscape::Util::Quantity width = // maybe set it to mm ?
                Inkscape::Util::Quantity(doc->getWidth().value("px") / ratio, "px");
            Inkscape::Util::Quantity height = Inkscape::Util::Quantity(doc->getHeight().value("px") / ratio, "px");
            if (need_fix_units) {
                doc->setWidthAndHeight(width, height, false);
            }

        } else if (response == FILE_DPI_DOCUMENT_SCALED) {
            Inkscape::Util::Quantity width = // maybe set it to mm ?
                Inkscape::Util::Quantity(doc->getWidth().value("px") / ratio, "px");
            Inkscape::Util::Quantity height = Inkscape::Util::Quantity(doc->getHeight().value("px") / ratio, "px");
            if (need_fix_units) {
                doc->setWidthAndHeight(width, height, false);
            }

            if (!root->viewBox_set) {
                // Save preferences
                bool transform_stroke = prefs->getBool("/options/transform/stroke", true);
                bool transform_rectcorners = prefs->getBool("/options/transform/rectcorners", true);
                bool transform_pattern = prefs->getBool("/options/transform/pattern", true);
                bool transform_gradient = prefs->getBool("/options/transform/gradient", true);

                prefs->setBool("/options/transform/stroke", true);
                prefs->setBool("/options/transform/rectcorners", true);
                prefs->setBool("/options/transform/pattern", true);
                prefs->setBool("/options/transform/gradient", true);

                Inkscape::UI::ShapeEditor::blockSetItem(true);
                doc->getRoot()->scaleChildItemsRec(Geom::Scale(1 / ratio), Geom::Point(0, 0), false);
                Inkscape::UI::ShapeEditor::blockSetItem(false);

                // Restore preferences
                prefs->setBool("/options/transform/stroke", transform_stroke);
                prefs->setBool("/options/transform/rectcorners", transform_rectcorners);
                prefs->setBool("/options/transform/pattern", transform_pattern);
                prefs->setBool("/options/transform/gradient", transform_gradient);

                did_scaling = true;
            }

        } else { // FILE_DPI_UNCHANGED
            if (need_fix_units) {
                need_fix_grid_mm = true;
            }
        }
    }

    // Fix guides and grids and perspective
    for (SPObject *child = root->firstChild(); child; child = child->getNext()) {
        auto nv = cast<SPNamedView>(child);
        if (nv) {
            if (need_fix_guides) {
                // std::cout << "Fixing guides" << std::endl;
                for (SPObject *child2 = nv->firstChild(); child2; child2 = child2->getNext()) {
                    auto gd = cast<SPGuide>(child2);
                    if (gd) {
                        gd->moveto(gd->getPoint() / ratio, true);
                    }
                }
            }

            for (auto grid : nv->grids) {
                if (grid->getType() == GridType::RECTANGULAR) {
                    Geom::Scale scale = doc->getDocumentScale();
                    if (grid->isLegacy()) {
                        if (grid->isPixel()) {
                            if (need_fix_grid_mm) {
                                grid->scale(Geom::Scale(1, 1)); // See note below
                            } else {
                                scale *= Geom::Scale(ratio, ratio);
                                grid->scale(scale.inverse()); /* *** */
                            }
                        } else {
                            if (need_fix_grid_mm) {
                                grid->scale(Geom::Scale(ratio, ratio));
                            } else {
                                grid->scale(scale.inverse()); /* *** */
                            }
                        }
                    } else {
                        if (need_fix_guides) {
                            if (did_scaling) {
                                grid->scale(Geom::Scale(ratio, ratio).inverse());
                            } else {
                                // HACK: Scaling the document does not seem to cause
                                // grids defined in document units to be updated.
                                // This forces an update.
                                grid->scale(Geom::Scale(1, 1));
                            }
                        }
                    }
                }
            }
        } // If SPNamedView

        auto defs = cast<SPDefs>(child);
        if (defs && need_fix_box3d) {
            for (SPObject *child = defs->firstChild(); child; child = child->getNext()) {
                auto persp3d = cast<Persp3D>(child);
                if (persp3d) {
                    std::vector<Glib::ustring> tokens;

                    const gchar *vp_x = persp3d->getAttribute("inkscape:vp_x");
                    const gchar *vp_y = persp3d->getAttribute("inkscape:vp_y");
                    const gchar *vp_z = persp3d->getAttribute("inkscape:vp_z");
                    const gchar *vp_o = persp3d->getAttribute("inkscape:persp3d-origin");
                    // std::cout << "Found Persp3d: "
                    //           << " vp_x: " << vp_x
                    //           << " vp_y: " << vp_y
                    //           << " vp_z: " << vp_z << std::endl;
                    Proj::Pt2 pt_x(vp_x);
                    Proj::Pt2 pt_y(vp_y);
                    Proj::Pt2 pt_z(vp_z);
                    Proj::Pt2 pt_o(vp_o);
                    pt_x = pt_x * (1.0 / ratio);
                    pt_y = pt_y * (1.0 / ratio);
                    pt_z = pt_z * (1.0 / ratio);
                    pt_o = pt_o * (1.0 / ratio);
                    persp3d->setAttribute("inkscape:vp_x", pt_x.coord_string());
                    persp3d->setAttribute("inkscape:vp_y", pt_y.coord_string());
                    persp3d->setAttribute("inkscape:vp_z", pt_z.coord_string());
                    persp3d->setAttribute("inkscape:persp3d-origin", pt_o.coord_string());
                }
            }
        }
    } // Look for SPNamedView and SPDefs loop

    // desktop->getDocument()->ensureUpToDate();  // Does not update box3d!
    DocumentUndo::done(doc, _("Update Document"), "");
}

// pre-1.1:
// Do not use canvas API-specific feComposite operators, they do *not* apply to SVG.
// https://github.com/w3c/csswg-drafts/issues/5267
void fix_feComposite(SPObject *i)
{
    if (!is<SPFeComposite>(i))
        return;
    auto oper = i->getAttribute("operator");
    if (!g_strcmp0(oper, "clear")) {
        i->setAttribute("operator", "arithmetic");
        i->setAttribute("k1", "0");
        i->setAttribute("k2", "0");
        i->setAttribute("k3", "0");
        i->setAttribute("k4", "0");
    } else if (!g_strcmp0(oper, "copy")) {
        i->setAttribute("operator", "arithmetic");
        i->setAttribute("k1", "0");
        i->setAttribute("k2", "1");
        i->setAttribute("k3", "0");
        i->setAttribute("k4", "0");
    } else if (!g_strcmp0(oper, "destination")) {
        i->setAttribute("operator", "arithmetic");
        i->setAttribute("k1", "0");
        i->setAttribute("k2", "0");
        i->setAttribute("k3", "1");
        i->setAttribute("k4", "0");
    } else if (!g_strcmp0(oper, "destination-over")) {
        auto in1 = i->getAttribute("in");
        auto in2 = i->getAttribute("in2");
        i->setAttribute("in", in2);
        i->setAttribute("in2", in1);
        i->setAttribute("operator", "over");
    } else if (!g_strcmp0(oper, "destination-in")) {
        auto in1 = i->getAttribute("in");
        auto in2 = i->getAttribute("in2");
        i->setAttribute("in", in2);
        i->setAttribute("in2", in1);
        i->setAttribute("operator", "in");
    } else if (!g_strcmp0(oper, "destination-out")) {
        auto in1 = i->getAttribute("in");
        auto in2 = i->getAttribute("in2");
        i->setAttribute("in", in2);
        i->setAttribute("in2", in1);
        i->setAttribute("operator", "out");
    } else if (!g_strcmp0(oper, "destination-atop")) {
        auto in1 = i->getAttribute("in");
        auto in2 = i->getAttribute("in2");
        i->setAttribute("in", in2);
        i->setAttribute("in2", in1);
        i->setAttribute("operator", "atop");
    } // else do nothing, we're good
    i->updateRepr();
}

void sp_file_fix_feComposite(SPObject *o)
{
    fix_feComposite(o);
    std::vector<SPObject *> cl = o->childList(false);
    for (auto ci : cl)
        sp_file_fix_feComposite(ci);
}

void sp_file_fix_lpe(SPDocument *doc)
{
    // need document insensitive to avoid problems on last undo
    DocumentUndo::ScopedInsensitive _no_undo(doc);
    for (auto &obj : doc->getObjectsByElement("path-effect", true)) {
        auto lpeobj = cast<LivePathEffectObject>(obj);
        if (lpeobj) {
            auto *lpe = lpeobj->get_lpe();
            if (lpe) {
                lpe->doOnOpen_impl();
            }
        }
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
