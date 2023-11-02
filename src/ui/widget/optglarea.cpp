// SPDX-License-Identifier: GPL-2.0-or-later

#include "optglarea.h"

#include <cassert>
#include <cairomm/context.h>
#include <gdkmm/glcontext.h>
#include <sigc++/functors/mem_fun.h>

namespace Inkscape::UI::Widget {

OptGLArea::OptGLArea()
{
    set_app_paintable(true); // No problem for GTK4 port since this whole widget will be deleted.
    opengl_enabled = false;

    set_draw_func(sigc::mem_fun(*this, &OptGLArea::draw_func));
}

OptGLArea::~OptGLArea() = default;

void OptGLArea::on_realize()
{
    Gtk::DrawingArea::on_realize();
    if (opengl_enabled) init_opengl();
}

void OptGLArea::on_unrealize()
{
    if (context) {
        if (framebuffer) {
            context->make_current();
            delete_framebuffer();
        }
        if (context == Gdk::GLContext::get_current()) {
            Gdk::GLContext::clear_current(); // ?
        }
        context.reset();
    }
    Gtk::DrawingArea::on_unrealize();
}

void OptGLArea::on_size_allocate(int const width, int const height, int const baseline)
{
    Gtk::DrawingArea::on_size_allocate(width, height, baseline);

    if (get_realized()) need_resize = true;
}

void OptGLArea::set_opengl_enabled(bool enabled)
{
    if (opengl_enabled == enabled) return;
    opengl_enabled = enabled;
    if (opengl_enabled && get_realized()) init_opengl();
}

void OptGLArea::init_opengl()
{
    context = create_context();
    if (!context) opengl_enabled = false;
    framebuffer = 0;
    need_resize = true;
}

void OptGLArea::make_current()
{
    assert(context);
    context->make_current();
}

void OptGLArea::draw_func(Cairo::RefPtr<Cairo::Context> const &cr, int const width, int const height)
{
    if (opengl_enabled) {
        context->make_current();

        if (!framebuffer) {
            create_framebuffer();
        }

        if (need_resize) {
            resize_framebuffer();
            need_resize = false;
        }

        paint_widget(cr);

        int s = get_scale_factor();
        int const w = width  * s;
        int const h = height * s;
        gdk_cairo_draw_from_gl(cr->cobj(), get_window()->gobj(), renderbuffer, GL_RENDERBUFFER, s, 0, 0, w, h);

        context->make_current(); // ?
    } else {
        paint_widget(cr);
    }

    return true;
}

void OptGLArea::bind_framebuffer() const
{
    assert(context);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, stencilbuffer);
}

void OptGLArea::create_framebuffer()
{
    glGenFramebuffers (1, &framebuffer);
    glGenRenderbuffers(1, &renderbuffer);
    glGenRenderbuffers(1, &stencilbuffer);
}

void OptGLArea::delete_framebuffer()
{
    glDeleteRenderbuffers(1, &renderbuffer);
    glDeleteRenderbuffers(1, &stencilbuffer);
    glDeleteFramebuffers (1, &framebuffer);
}

void OptGLArea::resize_framebuffer() const
{
    int s = get_scale_factor();
    int w = get_allocated_width() * s;
    int h = get_allocated_height() * s;
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB8, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, stencilbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
}

} // namespace Inkscape::UI::Widget

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:
