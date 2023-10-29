// SPDX-License-Identifier: GPL-2.0-or-later

#include "optglarea.h"

#include <cassert>
#include <utility>
#include <gdkmm/glcontext.h>
#include <gdkmm/gltexture.h>
#include <gdkmm/gltexturebuilder.h>
#include <gtkmm/snapshot.h>
#include <2geom/int-point.h>
#include "ui/widget/canvas/texture.h"

namespace Inkscape::UI::Widget {
namespace {

template <auto &f>
GLuint create_buffer()
{
    GLuint result;
    f(1, &result);
    return result;
}

template <typename T>
std::weak_ptr<T> weakify(std::shared_ptr<T> const &p)
{
    return p;
}

// Workaround for sigc not supporting move-only lambdas.
template <typename F>
auto share_lambda(F &&f)
{
    using Fd = std::decay_t<F>;

    struct Result
    {
        auto operator()() { (*f)(); }
        std::shared_ptr<Fd> f;
    };

    return Result{std::make_shared<Fd>(std::move(f))};
}

} // namespace

struct OptGLArea::GLState
{
    std::shared_ptr<Gdk::GLContext> const context;

    GLuint const framebuffer = create_buffer<glGenFramebuffers>();
    GLuint const stencilbuffer = create_buffer<glGenRenderbuffers>();

    Glib::RefPtr<Gdk::GLTextureBuilder> const builder = Gdk::GLTextureBuilder::create();

    std::optional<Geom::IntPoint> size;

    Texture current_texture;
    std::vector<Texture> spare_textures;

    GLState(Glib::RefPtr<Gdk::GLContext> &&context_)
        : context{std::move(context_)}
    {
        builder->set_context(context);
        builder->set_format(Gdk::MemoryFormat::B8G8R8A8_PREMULTIPLIED);
    }

    ~GLState()
    {
        glDeleteRenderbuffers(1, &stencilbuffer);
        glDeleteFramebuffers (1, &framebuffer);
    }
};

OptGLArea::OptGLArea() = default;
OptGLArea::~OptGLArea() = default;

void OptGLArea::on_realize()
{
    Gtk::Widget::on_realize();
    if (opengl_enabled) init_opengl();
}

void OptGLArea::on_unrealize()
{
    if (opengl_enabled) uninit_opengl();
    Gtk::Widget::on_unrealize();
}

void OptGLArea::set_opengl_enabled(bool enabled)
{
    if (opengl_enabled == enabled) return;
    if (opengl_enabled && get_realized()) uninit_opengl();
    opengl_enabled = enabled;
    if (opengl_enabled && get_realized()) init_opengl();
}

void OptGLArea::init_opengl()
{
    auto context = create_context();
    if (!context) {
        opengl_enabled = false;
        return;
    }
    context->make_current();
    gl = std::make_shared<GLState>(std::move(context));
    Gdk::GLContext::clear_current();
}

void OptGLArea::uninit_opengl()
{
    gl->context->make_current();
    gl.reset();
    Gdk::GLContext::clear_current();
}

void OptGLArea::make_current()
{
    assert(gl);
    gl->context->make_current();
}

void OptGLArea::bind_framebuffer() const
{
    assert(gl);
    assert(gl->current_texture);

    glBindFramebuffer(GL_FRAMEBUFFER, gl->framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->current_texture.id(), 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, gl->stencilbuffer);
}

void OptGLArea::snapshot_vfunc(Glib::RefPtr<Gtk::Snapshot> const &snapshot)
{
    if (opengl_enabled) {
        auto const size = Geom::IntPoint(get_width(), get_height()) * get_scale_factor();

        if (size.x() == 0 || size.y() == 0) {
            return;
        }

        gl->context->make_current();

        // Check if the size has changed.
        if (size != gl->size) {
            gl->size = size;

            // Resize the framebuffer.
            glBindRenderbuffer(GL_RENDERBUFFER, gl->stencilbuffer);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, size.x(), size.y());

            // Resize the texture builder.
            gl->builder->set_width(size.x());
            gl->builder->set_height(size.y());
        }

        // Discard wrongly-sized spare textures.
        std::erase_if(gl->spare_textures, [&] (auto &tex) { return tex.size() != size; });
        // Todo: Consider clearing out excess spare textures every once in a while.

        // Set the current texture.
        assert(!gl->current_texture);
        if (!gl->spare_textures.empty()) {
            // Grab a spare texture.
            gl->current_texture = std::move(gl->spare_textures.back());
            gl->spare_textures.pop_back();
        } else {
            // Create a new one.
            gl->current_texture = Texture(size);
        }

        // This typically calls bind_framebuffer().
        paint_widget({});

        // Wrap the OpenGL texture we've just drawn to in a Gdk::GLTexture.
        gl->builder->set_id(gl->current_texture.id());
        auto gdktexture = std::static_pointer_cast<Gdk::GLTexture>(gl->builder->build(
            share_lambda([texture = std::move(gl->current_texture),
                          context = gl->context,
                          gl_weak = weakify(gl)] () mutable
            {
                if (auto gl = gl_weak.lock()) {
                    // Return the texture to the texture pool.
                    gl->spare_textures.emplace_back(std::move(texture));
                } else {
                    // Destroy the texture in its GL context.
                    context->make_current();
                    texture.clear();
                    Gdk::GLContext::clear_current();
                }
            })
        ));

        // Render the texture upside-down.
        // Todo: The canvas does the same, so both transformations can be removed.
        snapshot->save();
        snapshot->translate({ 0.0f, (float)get_height() });
        snapshot->scale(1, -1);
        snapshot->append_texture(std::move(gdktexture), Gdk::Graphene::Rect(0, 0, get_width(), get_height()).gobj());
        snapshot->restore();
    } else {
        auto const cr = snapshot->append_cairo(Gdk::Graphene::Rect(0, 0, get_width(), get_height()).gobj());
        paint_widget(cr);
    }
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
