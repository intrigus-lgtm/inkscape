// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Print dialog.
 */
/* Authors:
 *   Kees Cook <kees@outflux.net>
 *   Abhishek Sharma
 *   Patrick McDermott
 *
 * Copyright (C) 2007 Kees Cook
 * Copyright (C) 2017 Patrick McDermott
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cmath>

#include <gtkmm.h>

#include "inkscape.h"
#include "preferences.h"
#include "print.h"

#include "extension/internal/cairo-render-context.h"
#include "extension/internal/cairo-renderer.h"
#include "document.h"
#include "object/sp-page.h"

#include "util/units.h"
#include "helper/png-write.h"
#include "page-manager.h"
#include "svg/svg-color.h"

#include <glibmm/i18n.h>


namespace Inkscape {
namespace UI {
namespace Dialog {

Glib::RefPtr<Gtk::PrintSettings> &get_printer_settings()
{
    static Glib::RefPtr<Gtk::PrintSettings> printer_settings;
    return printer_settings;
}

Print::Print(SPDocument *doc, SPItem *base) :
    _doc (doc),
    _base (base)
{
    g_assert (_doc);
    g_assert (_base);

    _printop = Gtk::PrintOperation::create();

    // set up dialog title, based on document name
    const Glib::ustring jobname = _doc->getDocumentName() ? _doc->getDocumentName() : _("SVG Document");
    Glib::ustring title = _("Print");
    title += " ";
    title += jobname;
    _printop->set_job_name(title);

    _printop->set_unit(Gtk::Unit::POINTS);
    Glib::RefPtr<Gtk::PageSetup> page_setup = Gtk::PageSetup::create();

    // Default to a custom paper size, in case we can't find a more specific size
    set_paper_size(page_setup, _doc->getWidth().value("pt"), _doc->getHeight().value("pt"));
    _printop->set_default_page_setup(page_setup);
    _printop->set_use_full_page(true);
    _printop->set_n_pages(1);

    // Now process actual multi-page setup.
    auto &pm = _doc->getPageManager();
    if (pm.hasPages()) {
        // This appears to be limiting which pages get rendered
        _printop->set_n_pages(pm.getPageCount());
        _printop->set_current_page(pm.getSelectedPageIndex());
        _printop->signal_request_page_setup().connect(sigc::mem_fun(*this, &Print::setup_page));
    }

    // set up signals
    _workaround._doc = _doc;
    _workaround._base = _base;
    _workaround._tab = &_tab;
    _printop->signal_create_custom_widget().connect(sigc::mem_fun(*this, &Print::create_custom_widget), true);
    _printop->signal_begin_print().connect(sigc::mem_fun(*this, &Print::begin_print));
    _printop->signal_draw_page().connect(sigc::mem_fun(*this, &Print::draw_page));

    // build custom preferences tab
    _printop->set_custom_tab_label(_("Rendering"));
}

/**
 * Return the required page setup, only connected for multi-page documents
 * and only required where there are pages of different sizes.
 */
void Print::setup_page(const Glib::RefPtr<Gtk::PrintContext>& context, int page_nr,
                       const Glib::RefPtr<Gtk::PageSetup> &setup)
{
    auto &pm = _workaround._doc->getPageManager();
    if (auto page = pm.getPage(page_nr)) {
        auto rect = page->getDesktopRect();
        auto width = Inkscape::Util::Quantity::convert(rect.width(), "px", "pt");
        auto height = Inkscape::Util::Quantity::convert(rect.height(), "px", "pt");
        set_paper_size(setup, width, height);
    }
}

/**
 * Set the paper size with correct orientation.
 */
void Print::set_paper_size(const Glib::RefPtr<Gtk::PageSetup> &page_setup, double page_width, double page_height)
{
    auto p_size = Gtk::PaperSize("custom", "custom", page_width, page_height, Gtk::Unit::POINTS);

    // Some print drivers, like the EPSON's ESC/P-R CUPS driver, don't accept custom
    // page sizes, so we'll try to find a known page size.
    // GTK+'s known paper sizes always have a longer height than width, so we'll rotate
    // the page and set its orientation to landscape as necessary in order to match a paper size.
    // Unfortunately, some printers, like Epilog laser cutters, don't understand landscape
    // mode.
    // As a compromise, we'll only rotate the page if we actually find a matching paper size,
    // since laser cutter beds tend to be custom sizes.
    Gtk::PageOrientation orientation = Gtk::PageOrientation::PORTRAIT;
    if (page_width > page_height) {
        orientation = Gtk::PageOrientation::REVERSE_LANDSCAPE;
        std::swap(page_width, page_height);
    }

    // attempt to match document size against known paper sizes
    std::vector<Gtk::PaperSize> known_sizes = Gtk::PaperSize::get_paper_sizes(false);
    for (auto& size : known_sizes) {
        if (fabs(size.get_width(Gtk::Unit::POINTS) - page_width) >= 1.0) {
            // width (short edge) doesn't match
            continue;
        }
        if (fabs(size.get_height(Gtk::Unit::POINTS) - page_height) >= 1.0) {
            // height (short edge) doesn't match
            continue;
        }
        // size matches
        p_size = size;
        break;
    }
    page_setup->set_paper_size(p_size);
    page_setup->set_orientation(orientation);
}

void Print::draw_page(const Glib::RefPtr<Gtk::PrintContext>& context, int page_nr)
{
    // TODO: If the user prints multiple copies we render the whole page for each copy
    //       It would be more efficient to render the page once (e.g. in "begin_print")
    //       and simply print this result as often as necessary

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    //printf("%s %d\n",__FUNCTION__, page_nr);

    auto &pm = _workaround._doc->getPageManager();
    auto page = pm.getPage(page_nr); // nullptr when no pages.

    if (_workaround._tab->as_bitmap()) {
        // Render as exported PNG
        prefs->setBool("/dialogs/printing/asbitmap", true);
        gdouble dpi = _workaround._tab->bitmap_dpi();
        prefs->setDouble("/dialogs/printing/dpi", dpi);
        
        auto rect = *(_workaround._doc->preferredBounds());
        if (page) {
            rect = page->getDesktopRect();
        }

        std::string tmp_png;
        std::string tmp_base = "inkscape-print-png-XXXXXX";

        int tmp_fd;
        if ( (tmp_fd = Glib::file_open_tmp(tmp_png, tmp_base)) >= 0) {
            close(tmp_fd);

            guint32 bgcolor = 0x00000000;
            Inkscape::XML::Node *nv = _workaround._doc->getReprNamedView();
            if (nv && nv->attribute("pagecolor")){
                bgcolor = sp_svg_read_color(nv->attribute("pagecolor"), 0xffffff00);
            }
            if (nv && nv->attribute("inkscape:pageopacity")){
                double opacity = nv->getAttributeDouble("inkscape:pageopacity", 1.0);
                bgcolor |= SP_COLOR_F_TO_U(opacity);
            }

            sp_export_png_file(_workaround._doc, tmp_png.c_str(), rect,
                (unsigned long)(Inkscape::Util::Quantity::convert(rect.width(), "px", "in") * dpi),
                (unsigned long)(Inkscape::Util::Quantity::convert(rect.height(), "px", "in") * dpi),
                               dpi, dpi, bgcolor, nullptr, nullptr, true, {});

            // This doesn't seem to work:
            //context->set_cairo_context ( Cairo::Context::create (Cairo::ImageSurface::create_from_png (tmp_png) ), dpi, dpi );
            //
            // so we'll use a surface pattern blat instead...
            //
            // but the C++ interface isn't implemented in cairomm:
            //context->get_cairo_context ()->set_source_surface(Cairo::ImageSurface::create_from_png (tmp_png) );
            //
            // so do it in C:
            {
                auto png = Cairo::ImageSurface::create_from_png(tmp_png);
                auto pattern = Cairo::SurfacePattern::create(png);
                auto cr = context->get_cairo_context();
                auto m = cr->get_matrix();
                cr->scale(Inkscape::Util::Quantity::convert(1, "in", "pt") / dpi,
                          Inkscape::Util::Quantity::convert(1, "in", "pt") / dpi);
                // FIXME: why is the origin offset??
                cr->set_source(pattern);
                cr->paint();
                cr->set_matrix(m);
            }

            // Clean up
            unlink (tmp_png.c_str());
        }
        else {
            g_warning("%s", _("Could not open temporary PNG for bitmap printing"));
        }
    }
    else {
        // Render as vectors
        prefs->setBool("/dialogs/printing/asbitmap", false);
        Inkscape::Extension::Internal::CairoRenderer renderer;
        Inkscape::Extension::Internal::CairoRenderContext *ctx = renderer.createContext();

        // ctx->setPSLevel(CAIRO_PS_LEVEL_3);
        ctx->setTextToPath(false);
        ctx->setFilterToBitmap(true);
        ctx->setBitmapResolution(72);

        auto cr = context->get_cairo_context();
        auto surface = cr->get_target();
        auto ctm = cr->get_matrix();

        bool ret = ctx->setSurfaceTarget(surface->cobj(), true, &ctm);
        if (ret) {
            ret = renderer.setupDocument (ctx, _workaround._doc);
            if (ret) {
                if (auto page = pm.getPage(page_nr)) {
                    renderer.renderPage(ctx, _workaround._doc, page, false);
                } else {
                    renderer.renderItem(ctx, _workaround._base);
                }
                ctx->finish(false);  // do not finish the cairo_surface_t - it's owned by our GtkPrintContext!
            }
            else {
                g_warning("%s", _("Could not set up Document"));
            }
        }
        else {
            g_warning("%s", _("Failed to set CairoRenderContext"));
        }

        // Clean up
        renderer.destroyContext(ctx);
    }

}

Gtk::Widget *Print::create_custom_widget()
{
    return &_tab;
}

void Print::begin_print(const Glib::RefPtr<Gtk::PrintContext>&)
{
    // Could change which pages get printed here, but nothing to do.
}

Gtk::PrintOperation::Result Print::run(Gtk::PrintOperation::Action, Gtk::Window &parent_window)
{
    // Remember to restore the previous print settings
    _printop->set_print_settings(get_printer_settings());

    try {
        Gtk::PrintOperation::Result res = _printop->run(Gtk::PrintOperation::Action::PRINT_DIALOG, parent_window);

        // Save printer settings (but only on success)
        if (res == Gtk::PrintOperation::Result::APPLY) {
            get_printer_settings() = _printop->get_print_settings();
        }

        return res;
    } catch (const Glib::Error &e) {
        g_warning("Failed to print '%s': %s", _doc->getDocumentName(), e.what());
    }

    return Gtk::PrintOperation::Result::ERROR;
}


} // namespace Dialog
} // namespace UI
} // namespace Inkscape

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
