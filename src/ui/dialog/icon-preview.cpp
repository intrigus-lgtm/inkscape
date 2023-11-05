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
#include <gtkmm/picture.h>
#include <gtkmm/snapshot.h>
#include <gtkmm/togglebutton.h>
#include <sigc++/adaptors/bind.h>
#include <sigc++/functors/mem_fun.h>

#include "desktop.h"
#include "document.h"
#include "page-manager.h"
#include "selection.h"
#include "display/drawing-context.h"
#include "display/drawing.h"
#include "object/sp-root.h"
#include "ui/pack.h"
#include "ui/util.h"
#include "ui/widget/frame.h"

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
    images .resize(sizes.size());
    labels .resize(sizes.size());
    buttons.resize(sizes.size());
    textures.resize(sizes.size());

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
        images[i] = Gtk::make_managed<Gtk::Picture>();
        images[i]->set_size_request(sizes[i], sizes[i]);

        auto const &label = labels[i];

        buttons[i] = Gtk::make_managed<Gtk::ToggleButton>();
        buttons[i]->add_css_class("icon-preview");
        buttons[i]->set_has_frame(false);
        buttons[i]->set_active( i == hot );

        if ( prefs->getBool("/iconpreview/showFrames", true) ) {
            auto const frame = Gtk::make_managed<Gtk::Frame>();
            frame->set_child(*images[i]);
            frame->add_css_class("icon-preview");
            buttons[i]->set_child(*frame);
        } else {
            buttons[i]->set_child(*images[i]);
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
    splitter.set_start_child(*magBox);
    splitter.set_resize_start_child();
    splitter.set_shrink_start_child(false);
    auto const actuals = Gtk::make_managed<UI::Widget::Frame>(_("Actual Size:"));
    actuals->set_margin(4);
    actuals->add(*verts);
    splitter.set_end_child(*actuals);
    splitter.set_resize_start_child(false);
    splitter.set_shrink_start_child(false);

    selectionButton = Gtk::make_managed<Gtk::CheckButton>(C_("Icon preview window", "Sele_ction"), true);
    UI::pack_start(*magBox,  *selectionButton, UI::PackOptions::shrink );
    selectionButton->set_tooltip_text(_("Selection only or whole document"));
    selectionButton->signal_toggled().connect( sigc::mem_fun(*this, &IconPreviewPanel::modeToggled) );

    int const val = prefs->getBool("/iconpreview/selectionOnly");
    selectionButton->set_active( val != 0 );

    UI::pack_start(*this, iconBox, UI::PackOptions::shrink);

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

static void overlayPixels(unsigned char *px, int width, int height, int stride, unsigned r, unsigned g, unsigned b)
{
    int bytesPerPixel = 4;
    int spacing = 4;
    for ( int y = 0; y < height; y += spacing ) {
        auto ptr = px + y * stride;
        for ( int x = 0; x < width; x += spacing ) {
            *(ptr++) = 0xff;
            *(ptr++) = r;
            *(ptr++) = g;
            *(ptr++) = b;

            ptr += bytesPerPixel * (spacing - 1);
        }
    }

    if ( width > 1 && height > 1 ) {
        // point at the last pixel
        auto ptr = px + ((height-1) * stride) + ((width - 1) * bytesPerPixel);

        if ( width > 2 ) {
            px[4] = 0xff;
            px[5] = r;
            px[6] = g;
            px[7] = b;

            ptr[-12] = 0xff;
            ptr[-11] = r;
            ptr[-10] = g;
            ptr[-9]  = b;
        }

        ptr[-4] = 0xff;
        ptr[-3] = r;
        ptr[-2] = g;
        ptr[-1] = b;

        px[0 + stride] = 0xff;
        px[1 + stride] = r;
        px[2 + stride] = g;
        px[3 + stride] = b;

        ptr[0 - stride] = 0xff;
        ptr[1 - stride] = r;
        ptr[2 - stride] = g;
        ptr[3 - stride] = b;

        if ( height > 2 ) {
            ptr[0 - stride * 3] = 0xff;
            ptr[1 - stride * 3] = r;
            ptr[2 - stride * 3] = g;
            ptr[3 - stride * 3] = b;
        }
    }
}

// takes doc, drawing, icon, and icon name to produce pixels
static Cairo::RefPtr<Cairo::ImageSurface> sp_icon_doc_icon(SPDocument *doc, Drawing &drawing, char const *name, unsigned psize)
{
    if (!doc) {
        return nullptr;
    }

    auto const item = cast<SPItem>(doc->getObjectById(name));
    if (!item) {
        return nullptr;
    }

    // Find bbox in document. This is in document coordinates, i.e. pixels.
    auto const dbox = item->parent
                    ? item->documentVisualBounds()
                    : *doc->preferredBounds();
    if (!dbox) {
        return nullptr;
    }

    bool const dump = Inkscape::Preferences::get()->getBool("/debug/icons/dumpSvg");

    // Update to renderable state.
    double sf = 1.0;
    drawing.root()->setTransform(Geom::Scale(sf));
    drawing.update();
    // Item integer bbox in points.
    // NOTE: previously, each rect coordinate was rounded using floor(c + 0.5)
    Geom::IntRect ibox = dbox->roundOutwards();

    if (dump) {
        g_message("   box    --'%s'  (%f,%f)-(%f,%f)", name, (double)ibox.left(), (double)ibox.top(), (double)ibox.right(), (double)ibox.bottom());
    }

    // Find button visible area.
    int width = ibox.width();
    int height = ibox.height();

    if (dump) {
        g_message("   vis    --'%s'  (%d,%d)", name, width, height);
    }

    if (int block = std::max(width, height); block != static_cast<int>(psize)) {
        if (dump) {
            g_message("      resizing");
        }
        sf = (double)psize / (double)block;

        drawing.root()->setTransform(Geom::Scale(sf));
        drawing.update();

        auto scaled_box = *dbox * Geom::Scale(sf);
        ibox = scaled_box.roundOutwards();
        if (dump) {
            g_message("   box2   --'%s'  (%f,%f)-(%f,%f)", name, (double)ibox.left(), (double)ibox.top(), (double)ibox.right(), (double)ibox.bottom());
        }

        // Find button visible area.
        width = ibox.width();
        height = ibox.height();
        if (dump) {
            g_message("   vis2   --'%s'  (%d,%d)", name, width, height);
        }
    }

    auto pdim = Geom::IntPoint(psize, psize);
    int dx, dy;
    //dx = (psize - width) / 2;
    //dy = (psize - height) / 2;
    dx=dy=psize;
    dx=(dx-width)/2; // watch out for psize, since 'unsigned'-'signed' can cause problems if the result is negative
    dy=(dy-height)/2;
    Geom::IntRect area = Geom::IntRect::from_xywh(ibox.min() - Geom::IntPoint(dx,dy), pdim);
    // Actual renderable area.
    Geom::IntRect ua = *Geom::intersect(ibox, area);

    if (dump) {
        g_message("   area   --'%s'  (%f,%f)-(%f,%f)", name, (double)area.left(), (double)area.top(), (double)area.right(), (double)area.bottom());
        g_message("   ua     --'%s'  (%f,%f)-(%f,%f)", name, (double)ua.left(), (double)ua.top(), (double)ua.right(), (double)ua.bottom());
    }

    // Render.
    auto s = Cairo::ImageSurface::create(Cairo::ImageSurface::Format::ARGB32, psize, psize);
    auto dc = DrawingContext(s->cobj(), ua.min());

    auto bg = doc->getPageManager().getDefaultBackgroundColor();

    auto cr = Cairo::Context::create(s);
    cr->set_source_rgba(bg[0], bg[1], bg[2], bg[3]);
    cr->rectangle(0, 0, psize, psize);
    cr->fill();
    cr->save();
    cr.reset();

    drawing.render(dc, ua);

    if (Preferences::get()->getBool("/debug/icons/overlaySvg")) {
        s->flush();
        overlayPixels(s->get_data(), psize, psize, s->get_stride(), 0x00, 0x00, 0xff);
        s->mark_dirty();
    }

    return s;
}

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
        textures[i] = to_texture(sp_icon_doc_icon(doc, *drawing, id, sizes[i]));
        images[i]->set_paintable(textures[i]);
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
    magnified.set(textures[hot]);
    magLabel.set_label(labels[hot]);
}

void Magnifier::snapshot_vfunc(Glib::RefPtr<Gtk::Snapshot> const &snapshot)
{
    if (!_texture) {
        snapshot->append_color(Gdk::RGBA{0, 0, 0}, Gdk::Rectangle{0, 0, get_width(), get_height()});
        return;
    }
    auto node = gsk_texture_scale_node_new(_texture->gobj(), Gdk::Graphene::Rect{0, 0, 128, 128}.gobj(), GSK_SCALING_FILTER_NEAREST);
    gtk_snapshot_append_node(snapshot->gobj(), node);
    gsk_render_node_unref(node);
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
