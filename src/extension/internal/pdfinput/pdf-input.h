// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   miklos erdelyi
 *
 * Copyright (C) 2007 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_EXTENSION_INTERNAL_PDFINPUT_H
#define SEEN_EXTENSION_INTERNAL_PDFINPUT_H

#ifdef HAVE_CONFIG_H
# include "config.h"  // only include where actually required!
#endif

#ifdef HAVE_POPPLER

#include <glibmm/refptr.h>
#include <gtkmm/dialog.h>
#include <unordered_map>

#include "../../implementation/implementation.h"
#include "async/channel.h"
#include "poppler-transition-api.h"
#include "poppler-utils.h"
#include "svg-builder.h"

namespace Gtk {
class Builder;
class Button;
class CheckButton;
class DrawingArea;
class Entry;
class Label;
class ListStore;
class Scale;
} // namespace Gtk

#ifdef HAVE_POPPLER_CAIRO
struct _PopplerDocument;
typedef struct _PopplerDocument            PopplerDocument;
#endif

class Page;
class PDFDoc;

namespace Gtk {
class Button;
class CheckButton;
class ComboBoxText;
class DrawingArea;
class Frame;
class Scale;
class Box;
class Label;
class Entry;
} // namespace Gtk

namespace Inkscape {

namespace UI::Widget {
class SpinButton;
class Frame;
} // namespace UI::Widget

enum class PdfImportType : unsigned char
{
    PDF_IMPORT_INTERNAL,
    PDF_IMPORT_CAIRO,
};

namespace Extension::Internal {

class FontModelColumns;

/**
 * PDF import using libpoppler.
 */
class PdfImportDialog final : public Gtk::Dialog
{
public:
    PdfImportDialog(std::shared_ptr<PDFDoc> doc, const gchar *uri);
    ~PdfImportDialog() final;

    bool showDialog();
    std::string getSelectedPages();
    PdfImportType getImportMethod();
    void getImportSettings(Inkscape::XML::Node *prefs);
    FontStrategies getFontStrategies();
    void setFontStrategies(const FontStrategies &fs);

private:
    void _fontRenderChanged();
    void _setPreviewPage(int page);
    void _setFonts(const FontList &fonts);

    // Signal handlers
    void _drawFunc(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
    void _onPageNumberChanged();
    void _onPrecisionChanged();

    Glib::RefPtr<Gtk::Builder> _builder;

    Gtk::Entry &_page_numbers;
    Gtk::DrawingArea &_preview_area;
    Gtk::CheckButton &_embed_images;
    Gtk::Scale &_mesh_slider;
    Gtk::Label &_mesh_label;
    Gtk::Button &_next_page;
    Gtk::Button &_prev_page;
    Gtk::Label &_current_page;
    Glib::RefPtr<Gtk::ListStore> _font_model;
    FontModelColumns *_font_col;

    std::shared_ptr<PDFDoc> _pdf_doc;   // Document to be imported
    std::string _current_pages;  // Current selected pages
    FontList _font_list;         // List of fonts and the pages they appear on
    int _total_pages = 0;
    int _preview_page = 1;
    Page *_previewed_page;    // Currently previewed page
    unsigned char *_thumb_data; // Thumbnail image data
    int _thumb_width, _thumb_height;    // Thumbnail size
    int _thumb_rowstride;
    int _preview_width, _preview_height;    // Size of the preview area
    bool _render_thumb;     // Whether we can/shall render thumbnails
#ifdef HAVE_POPPLER_CAIRO
    bool _preview_rendering_in_progress = false;
    std::unordered_map<int, std::shared_ptr<cairo_surface_t>> _cairo_surfaces;
    std::vector<Async::Channel::Dest> _channels;
    PopplerDocument *_poppler_doc = nullptr;
#endif
};

    
class PdfInput final: public Inkscape::Extension::Implementation::Implementation {
public:
    PdfInput() = default;
    SPDocument *open(Inkscape::Extension::Input *mod,
                     const gchar *uri) final;
    static void         init( );
private:
    void add_builder_page(
        std::shared_ptr<PDFDoc> pdf_doc,
        SvgBuilder *builder, SPDocument *doc,
        int page_num);
};

} // namespace Inkscape::Extension::Internal

} // namespace Inkscape

#endif // HAVE_POPPLER

#endif // SEEN_EXTENSION_INTERNAL_PDFINPUT_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
