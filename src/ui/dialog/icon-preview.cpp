// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * A simple dialog for previewing icon representation.
 */
/* Authors:
 *   Jon A. Cruz
 *   Bob Jamison
 *   Other dudes from The Inkscape Organization
 *   Abhishek Sharma
 *
 * Copyright (C) 2004 Bob Jamison
 * Copyright (C) 2005,2010 Jon A. Cruz
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "icon-preview.h"

#include <glibmm/i18n.h>
#include <glibmm/timer.h>
#include <glibmm/main.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/frame.h>
#include <gtkmm/togglebutton.h>
#include <sigc++/adaptors/bind.h>
#include <sigc++/functors/mem_fun.h>

#include "desktop.h"
#include "document.h"
#include "page-manager.h"
#include "selection.h"
#include "display/cairo-utils.h"
#include "display/drawing-context.h"
#include "display/drawing.h"
#include "object/sp-root.h"
#include "ui/pack.h"
#include "ui/widget/frame.h"

extern "C" {
// takes doc, drawing, icon, and icon name to produce pixels
guchar *
sp_icon_doc_icon( SPDocument *doc, Inkscape::Drawing &drawing,
                  const gchar *name, unsigned int psize, unsigned &stride);
} // extern "C"

#define noICON_VERBOSE 1

namespace Inkscape::UI::Dialog {

//#########################################################################
//## E V E N T S
//#########################################################################

void IconPreviewPanel::on_button_clicked(int which)
{
    if ( hot != which ) {
        buttons[hot]->set_active( false );

        hot = which;
        updateMagnify();
        queue_draw();
    }
}

//#########################################################################
//## C O N S T R U C T O R    /    D E S T R U C T O R
//#########################################################################

IconPreviewPanel::IconPreviewPanel()
    : DialogBase("/dialogs/iconpreview", "IconPreview")
    , drawing(nullptr)
    , drawing_doc(nullptr)
    , visionkey(0)
    , timer(nullptr)
    , renderTimer(nullptr)
    , pending(false)
    , minDelay(0.1)
    , targetId()
    , hot(1)
    , selectionButton(nullptr)
    , docModConn()
    , iconBox(Gtk::Orientation::VERTICAL)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    bool pack = prefs->getBool("/iconpreview/pack", true);

    std::vector<Glib::ustring> pref_sizes = prefs->getAllDirs("/iconpreview/sizes/default");

    for (auto & pref_size : pref_sizes) {
        if (prefs->getBool(pref_size + "/show", true)) {
            int sizeVal = prefs->getInt(pref_size + "/value", -1);
            if (sizeVal > 0) {
                sizes.push_back(sizeVal);
            }
        }
    }

    if (sizes.empty()) {
        sizes.resize(5);
        sizes[0] = 16;
        sizes[1] = 24;
        sizes[2] = 32;
        sizes[3] = 48;
        sizes[4] = 128;
    }

    pixMem .resize(sizes.size());
    images .resize(sizes.size());
    labels .resize(sizes.size());
    buttons.resize(sizes.size());

    for (std::size_t i = 0; i < sizes.size(); ++i) {
        labels[i] = Glib::ustring::compose("%1 x %1", sizes[i]);
    }

    magLabel.set_label(labels[hot]);

    auto const magBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);

    auto const magFrame = Gtk::make_managed<UI::Widget::Frame>(_("Magnified:"));
    magFrame->add( magnified );

    UI::pack_start(*magBox, *magFrame, UI::PackOptions::expand_widget);
    UI::pack_start(*magBox,  magLabel, UI::PackOptions::shrink       );

    auto const verts = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);

    Gtk::Box *horiz = nullptr;
    int previous = 0;
    int avail = 0;
    for (auto i = sizes.size(); i-- > 0;) {
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, sizes[i]);
        pixMem[i].resize(sizes[i] * stride);
        auto const pb = Gdk::Pixbuf::create_from_data(pixMem[i].data(),
            Gdk::Colorspace::RGB, true, 8, sizes[i], sizes[i], stride);
        images[i] = Gtk::make_managed<Gtk::Image>(pb);

        auto const &label = labels[i];

        buttons[i] = Gtk::make_managed<Gtk::ToggleButton>();
        buttons[i]->add_css_class("icon-preview");
        buttons[i]->set_has_frame(false);
        buttons[i]->set_active( i == hot );

        if ( prefs->getBool("/iconpreview/showFrames", true) ) {
            auto const frame = Gtk::make_managed<Gtk::Frame>();
            frame->add(*images[i]);
            buttons[i]->add(*frame);
        } else {
            buttons[i]->add(*images[i]);
        }

        buttons[i]->set_tooltip_text(label);
        buttons[i]->signal_clicked().connect(
            sigc::bind(sigc::mem_fun(*this, &IconPreviewPanel::on_button_clicked), i));
        buttons[i]->set_halign(Gtk::Align::CENTER);
        buttons[i]->set_valign(Gtk::Align::CENTER);

        if ( !pack || ( (avail == 0) && (previous == 0) ) ) {
            UI::pack_end(*verts, *(buttons[i]), UI::PackOptions::shrink);
            previous = sizes[i];
            avail = sizes[i];
        } else {
            static constexpr int pad = 12;

            if ((avail < pad) || ((sizes[i] > avail) && (sizes[i] < previous))) {
                horiz = nullptr;
            }

            if ((horiz == nullptr) && (sizes[i] <= previous)) {
                avail = previous;
            }

            if (sizes[i] <= avail) {
                if (!horiz) {
                    horiz = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
                    horiz->set_halign(Gtk::Align::CENTER);
                    avail = previous;
                    UI::pack_end(*verts, *horiz, UI::PackOptions::shrink);
                }

                UI::pack_end(*horiz, *buttons[i], UI::PackOptions::expand_widget);

                avail -= sizes[i];
                avail -= pad; // a little extra for padding
            } else {
                horiz = nullptr;
                UI::pack_end(*verts, *(buttons[i]), UI::PackOptions::shrink);
            }
        }
    }

    UI::pack_start(iconBox, splitter);
    splitter.pack1( *magBox, true, false );
    auto const actuals = Gtk::make_managed<UI::Widget::Frame>(_("Actual Size:"));
    actuals->set_margin(4);
    actuals->add(*verts);
    splitter.pack2( *actuals, false, false );

    selectionButton = Gtk::make_managed<Gtk::CheckButton>(C_("Icon preview window", "Sele_ction"), true);
    UI::pack_start(*magBox,  *selectionButton, UI::PackOptions::shrink );
    selectionButton->set_tooltip_text(_("Selection only or whole document"));
    selectionButton->signal_clicked().connect( sigc::mem_fun(*this, &IconPreviewPanel::modeToggled) );

    int const val = prefs->getBool("/iconpreview/selectionOnly");
    selectionButton->set_active( val != 0 );

    UI::pack_start(*this, iconBox, UI::PackOptions::shrink);

    show_all_children();

    refreshPreview();
}

IconPreviewPanel::~IconPreviewPanel()
{
    removeDrawing();

    if (timer) {
        timer->stop();
        timer.reset(); // Reset the unique_ptr, not the Timer!
    }

    if ( renderTimer ) {
        renderTimer->stop();
        renderTimer.reset(); // Reset the unique_ptr, not the Timer!
    }
}

//#########################################################################
//## M E T H O D S
//#########################################################################

#if ICON_VERBOSE
static Glib::ustring getTimestr()
{
    Glib::ustring str;
    gint64 micr = g_get_monotonic_time();
    gint64 mins = ((int)round(micr / 60000000)) % 60;
    gdouble dsecs = micr / 1000000;
    gchar *ptr = g_strdup_printf(":%02u:%f", mins, dsecs);
    str = ptr;
    g_free(ptr);
    ptr = 0;
    return str;
}
#endif // ICON_VERBOSE

void IconPreviewPanel::selectionModified(Selection *selection, guint flags)
{
    if (getDesktop() && Inkscape::Preferences::get()->getBool("/iconpreview/autoRefresh", true)) {
        queueRefresh();
    }
}

void IconPreviewPanel::documentReplaced()
{
    removeDrawing();

    drawing_doc = getDocument();

    if (drawing_doc) {
        drawing = std::make_unique<Inkscape::Drawing>();
        visionkey = SPItem::display_key_new(1);
        drawing->setRoot(drawing_doc->getRoot()->invoke_show(*drawing, visionkey, SP_ITEM_SHOW_DISPLAY));
        docDesConn = drawing_doc->connectDestroy([this]{ removeDrawing(); });
        queueRefresh();
    }
}

/// Safely delete the Inkscape::Drawing and references to it.
void IconPreviewPanel::removeDrawing()
{
    docDesConn.disconnect();

    if (!drawing) {
        return;
    }

    drawing_doc->getRoot()->invoke_hide(visionkey);
    drawing.reset();

    drawing_doc = nullptr;
}

void IconPreviewPanel::refreshPreview()
{
    auto document = getDocument();
    if (!timer) {
        timer = std::make_unique<Glib::Timer>();
    }
    if (timer->elapsed() < minDelay) {
#if ICON_VERBOSE
        g_message( "%s Deferring refresh as too soon. calling queueRefresh()", getTimestr().c_str() );
#endif //ICON_VERBOSE
        // Do not refresh too quickly
        queueRefresh();
    } else if (document) {
#if ICON_VERBOSE
        g_message( "%s Refreshing preview.", getTimestr().c_str() );
#endif // ICON_VERBOSE
        bool hold = Inkscape::Preferences::get()->getBool("/iconpreview/selectionHold", true);
        SPObject *target = nullptr;
        if ( selectionButton && selectionButton->get_active() )
        {
            target = (hold && !targetId.empty()) ? document->getObjectById( targetId.c_str() ) : nullptr;
            if ( !target ) {
                targetId.clear();
                if (auto selection = getSelection()) {
                    for (auto item : selection->items()) {
                        if (gchar const *id = item->getId()) {
                            targetId = id;
                            target = item;
                        }
                    }
                }
            }
        } else {
            target = getDesktop()->getDocument()->getRoot();
        }
        if (target) {
            renderPreview(target);
        }
#if ICON_VERBOSE
        g_message( "%s  resetting timer", getTimestr().c_str() );
#endif // ICON_VERBOSE
        timer->reset();
    }
}

bool IconPreviewPanel::refreshCB()
{
    bool callAgain = true;
    if (!timer) {
        timer = std::make_unique<Glib::Timer>();
    }
    if ( timer->elapsed() > minDelay ) {
#if ICON_VERBOSE
        g_message( "%s refreshCB() timer has progressed", getTimestr().c_str() );
#endif // ICON_VERBOSE
        callAgain = false;
        refreshPreview();
#if ICON_VERBOSE
        g_message( "%s refreshCB() setting pending false", getTimestr().c_str() );
#endif // ICON_VERBOSE
        pending = false;
    }
    return callAgain;
}

void IconPreviewPanel::queueRefresh()
{
    if (!pending) {
        pending = true;
#if ICON_VERBOSE
        g_message( "%s queueRefresh() Setting pending true", getTimestr().c_str() );
#endif // ICON_VERBOSE
        if (!timer) {
            timer = std::make_unique<Glib::Timer>();
        }
        Glib::signal_idle().connect( sigc::mem_fun(*this, &IconPreviewPanel::refreshCB), Glib::PRIORITY_DEFAULT_IDLE );
    }
}

void IconPreviewPanel::modeToggled()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool selectionOnly = (selectionButton && selectionButton->get_active());
    prefs->setBool("/iconpreview/selectionOnly", selectionOnly);
    if ( !selectionOnly ) {
        targetId.clear();
    }

    refreshPreview();
}

void overlayPixels(guchar *px, int width, int height, int stride,
                            unsigned r, unsigned g, unsigned b)
{
    int bytesPerPixel = 4;
    int spacing = 4;
    for ( int y = 0; y < height; y += spacing ) {
        guchar *ptr = px + y * stride;
        for ( int x = 0; x < width; x += spacing ) {
            *(ptr++) = r;
            *(ptr++) = g;
            *(ptr++) = b;
            *(ptr++) = 0xff;

            ptr += bytesPerPixel * (spacing - 1);
        }
    }

    if ( width > 1 && height > 1 ) {
        // point at the last pixel
        guchar *ptr = px + ((height-1) * stride) + ((width - 1) * bytesPerPixel);

        if ( width > 2 ) {
            px[4] = r;
            px[5] = g;
            px[6] = b;
            px[7] = 0xff;

            ptr[-12] = r;
            ptr[-11] = g;
            ptr[-10] = b;
            ptr[-9] = 0xff;
        }

        ptr[-4] = r;
        ptr[-3] = g;
        ptr[-2] = b;
        ptr[-1] = 0xff;

        px[0 + stride] = r;
        px[1 + stride] = g;
        px[2 + stride] = b;
        px[3 + stride] = 0xff;

        ptr[0 - stride] = r;
        ptr[1 - stride] = g;
        ptr[2 - stride] = b;
        ptr[3 - stride] = 0xff;

        if ( height > 2 ) {
            ptr[0 - stride * 3] = r;
            ptr[1 - stride * 3] = g;
            ptr[2 - stride * 3] = b;
            ptr[3 - stride * 3] = 0xff;
        }
    }
}

// takes doc, drawing, icon, and icon name to produce pixels
extern "C" guchar *
sp_icon_doc_icon( SPDocument *doc, Inkscape::Drawing &drawing,
                  gchar const *name, unsigned psize,
                  unsigned &stride)
{
    bool const dump = Inkscape::Preferences::get()->getBool("/debug/icons/dumpSvg");
    guchar *px = nullptr;

    if (doc) {
        SPObject *object = doc->getObjectById(name);
        if (object && is<SPItem>(object)) {
            auto item = cast<SPItem>(object);
            // Find bbox in document
            Geom::OptRect dbox = item->documentVisualBounds();

            if ( object->parent == nullptr )
            {
                dbox = *(doc->preferredBounds());
            }

            /* This is in document coordinates, i.e. pixels */
            if ( dbox ) {
                /* Update to renderable state */
                double sf = 1.0;
                drawing.root()->setTransform(Geom::Scale(sf));
                drawing.update();
                /* Item integer bbox in points */
                // NOTE: previously, each rect coordinate was rounded using floor(c + 0.5)
                Geom::IntRect ibox = dbox->roundOutwards();

                if ( dump ) {
                    g_message( "   box    --'%s'  (%f,%f)-(%f,%f)", name, (double)ibox.left(), (double)ibox.top(), (double)ibox.right(), (double)ibox.bottom() );
                }

                /* Find button visible area */
                int width = ibox.width();
                int height = ibox.height();

                if ( dump ) {
                    g_message( "   vis    --'%s'  (%d,%d)", name, width, height );
                }

                {
                    int block = std::max(width, height);
                    if (block != static_cast<int>(psize) ) {
                        if ( dump ) {
                            g_message("      resizing" );
                        }
                        sf = (double)psize / (double)block;

                        drawing.root()->setTransform(Geom::Scale(sf));
                        drawing.update();

                        auto scaled_box = *dbox * Geom::Scale(sf);
                        ibox = scaled_box.roundOutwards();
                        if ( dump ) {
                            g_message( "   box2   --'%s'  (%f,%f)-(%f,%f)", name, (double)ibox.left(), (double)ibox.top(), (double)ibox.right(), (double)ibox.bottom() );
                        }

                        /* Find button visible area */
                        width = ibox.width();
                        height = ibox.height();
                        if ( dump ) {
                            g_message( "   vis2   --'%s'  (%d,%d)", name, width, height );
                        }
                    }
                }

                Geom::IntPoint pdim(psize, psize);
                int dx, dy;
                //dx = (psize - width) / 2;
                //dy = (psize - height) / 2;
                dx=dy=psize;
                dx=(dx-width)/2; // watch out for psize, since 'unsigned'-'signed' can cause problems if the result is negative
                dy=(dy-height)/2;
                Geom::IntRect area = Geom::IntRect::from_xywh(ibox.min() - Geom::IntPoint(dx,dy), pdim);
                /* Actual renderable area */
                Geom::IntRect ua = *Geom::intersect(ibox, area);

                if ( dump ) {
                    g_message( "   area   --'%s'  (%f,%f)-(%f,%f)", name, (double)area.left(), (double)area.top(), (double)area.right(), (double)area.bottom() );
                    g_message( "   ua     --'%s'  (%f,%f)-(%f,%f)", name, (double)ua.left(), (double)ua.top(), (double)ua.right(), (double)ua.bottom() );
                }

                stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, psize);

                /* Set up pixblock */
                px = g_new(guchar, stride * psize);
                memset(px, 0x00, stride * psize);

                /* Render */
                cairo_surface_t *s = cairo_image_surface_create_for_data(px,
                    CAIRO_FORMAT_ARGB32, psize, psize, stride);
                Inkscape::DrawingContext dc(s, ua.min());

                auto bg = doc->getPageManager().getDefaultBackgroundColor();

                cairo_t *cr = cairo_create(s);
                cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
                cairo_rectangle(cr, 0, 0, psize, psize);
                cairo_fill(cr);
                cairo_save(cr);
                cairo_destroy(cr);

                drawing.render(dc, ua);
                cairo_surface_destroy(s);

                // convert to GdkPixbuf format
                convert_pixels_argb32_to_pixbuf(px, psize, psize, stride);

                if ( Inkscape::Preferences::get()->getBool("/debug/icons/overlaySvg") ) {
                    overlayPixels( px, psize, psize, stride, 0x00, 0x00, 0xff );
                }
            }
        }
    }

    return px;
} // end of sp_icon_doc_icon()

void IconPreviewPanel::renderPreview( SPObject* obj )
{
    SPDocument * doc = obj->document;
    gchar const * id = obj->getId();
    if ( !renderTimer ) {
        renderTimer = std::make_unique<Glib::Timer>();
    }
    renderTimer->reset(); // Reset the Timer, not the unique_ptr!

#if ICON_VERBOSE
    g_message("%s setting up to render '%s' as the icon", getTimestr().c_str(), id );
#endif // ICON_VERBOSE

    for (std::size_t i = 0; i < sizes.size(); ++i) {
        unsigned unused;
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, sizes[i]);
        guchar *px = sp_icon_doc_icon(doc, *drawing, id, sizes[i], unused);
        if ( px ) {
            memcpy( pixMem[i].data(), px, sizes[i] * stride );
            g_free( px );
        } else {
            memset( pixMem[i].data(), 0, sizes[i] * stride );
        }
        images[i]->set(images[i]->get_pixbuf());
    }
    updateMagnify();

    renderTimer->stop();
    minDelay = std::max( 0.1, renderTimer->elapsed() * 3.0 );
#if ICON_VERBOSE
    g_message("  render took %f seconds.", renderTimer->elapsed());
#endif // ICON_VERBOSE
}

void IconPreviewPanel::updateMagnify()
{
    Glib::RefPtr<Gdk::Pixbuf> buf = images[hot]->get_pixbuf()->scale_simple( 128, 128, Gdk::InterpType::NEAREST );
    magLabel.set_label(labels[hot]);
    magnified.set( buf );
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
