// SPDX-License-Identifier: GPL-2.0-or-later

#include "glgraphics.h"

#include <2geom/transforms.h>
#include <2geom/rect.h>

#include "ui/util.h"
#include "helper/geom.h"
#include "stores.h"
#include "prefs.h"
#include "pixelstreamer.h"
#include "util.h"

namespace Inkscape::UI::Widget {

namespace {

// 2Geom <-> OpenGL

void geom_to_uniform_mat(Geom::Affine const &affine, GLuint location)
{
    glUniformMatrix2fv(location, 1, GL_FALSE, std::begin({(GLfloat)affine[0], (GLfloat)affine[1], (GLfloat)affine[2], (GLfloat)affine[3]}));
}

void geom_to_uniform_trans(Geom::Affine const &affine, GLuint location)
{
    glUniform2fv(location, 1, std::begin({(GLfloat)affine[4], (GLfloat)affine[5]}));
}

void geom_to_uniform(Geom::Affine const &affine, GLuint mat_location, GLuint trans_location)
{
    geom_to_uniform_mat(affine, mat_location);
    geom_to_uniform_trans(affine, trans_location);
}

void geom_to_uniform(Geom::Point const &vec, GLuint location)
{
    glUniform2fv(location, 1, std::begin({(GLfloat)vec.x(), (GLfloat)vec.y()}));
}

// Get the affine transformation required to paste fragment A onto fragment B, assuming
// coordinates such that A is a texture (0 to 1) and B is a framebuffer (-1 to 1).
static auto calc_paste_transform(Fragment const &a, Fragment const &b)
{
    Geom::Affine result = Geom::Scale(a.rect.dimensions());

    if (a.affine == b.affine) {
        result *= Geom::Translate(a.rect.min() - b.rect.min());
    } else {
        result *= Geom::Translate(a.rect.min()) * a.affine.inverse() * b.affine * Geom::Translate(-b.rect.min());
    }

    return result * Geom::Scale(2.0 / b.rect.dimensions()) * Geom::Translate(-1.0, -1.0);
}

// Given a region, shrink it by 0.5px, and convert the result to a VAO of triangles.
static auto region_shrink_vao(Cairo::RefPtr<Cairo::Region> const &reg, Geom::IntRect const &rel)
{
    // Shrink the region by 0.5 (translating it by (0.5, 0.5) in the process).
    auto reg2 = shrink_region(reg, 1);

    // Preallocate the vertex buffer.
    int nrects = reg2->get_num_rectangles();
    std::vector<GLfloat> verts;
    verts.reserve(nrects * 12);

    // Add a vertex to the buffer, transformed to a coordinate system in which the enclosing rectangle 'rel' goes from 0 to 1.
    // Also shift them up/left by 0.5px; combined with the width/height increase from earlier, this shrinks the region by 0.5px.
    auto emit_vertex = [&] (Geom::IntPoint const &pt) {
        verts.emplace_back((pt.x() - 0.5f - rel.left()) / rel.width());
        verts.emplace_back((pt.y() - 0.5f - rel.top() ) / rel.height());
    };

    // Todo: Use a better triangulation algorithm here that results in 1) less triangles, and 2) no seaming.
    for (int i = 0; i < nrects; i++) {
        auto rect = cairo_to_geom(reg2->get_rectangle(i));
        for (int j = 0; j < 6; j++) {
            int constexpr indices[] = {0, 1, 2, 0, 2, 3};
            emit_vertex(rect.corner(indices[j]));
        }
    }

    // Package the data in a VAO.
    VAO result;
    glGenBuffers(1, &result.vbuf);
    glBindBuffer(GL_ARRAY_BUFFER, result.vbuf);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(GLfloat), verts.data(), GL_STREAM_DRAW);
    glGenVertexArrays(1, &result.vao);
    glBindVertexArray(result.vao);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 2, 0);

    // Return the VAO and the number of rectangles.
    return std::make_pair(std::move(result), nrects);
}

auto pref_to_pixelstreamer(int index)
{
    auto constexpr arr = std::array{PixelStreamer::Method::Auto,
                                    PixelStreamer::Method::Persistent,
                                    PixelStreamer::Method::Asynchronous,
                                    PixelStreamer::Method::Synchronous};
    assert(1 <= index && index <= arr.size());
    return arr[index - 1];
}

} // namespace

GLGraphics::GLGraphics(Prefs const &prefs, Stores const &stores, PageInfo const &pi)
    : prefs(prefs)
    , stores(stores)
    , pi(pi)
{
    // Create rectangle geometry.
    GLfloat constexpr verts[] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    glGenBuffers(1, &rect.vbuf);
    glBindBuffer(GL_ARRAY_BUFFER, rect.vbuf);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glGenVertexArrays(1, &rect.vao);
    glBindVertexArray(rect.vao);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 2, 0);

    // Create shader programs.
    auto vs = VShader(R"(
        #version 330 core

        uniform mat2 mat;
        uniform vec2 trans;
        uniform vec2 subrect;
        layout(location = 0) in vec2 pos;
        smooth out vec2 uv;

        void main()
        {
            uv = pos * subrect;
            vec2 pos2 = mat * pos + trans;
            gl_Position = vec4(pos2.x, pos2.y, 0.0, 1.0);
        }
    )");

    auto texcopy_fs = FShader(R"(
        #version 330 core

        uniform sampler2D tex;
        smooth in vec2 uv;
        out vec4 outColour;

        void main()
        {
            outColour = texture(tex, uv);
        }
    )");

    auto texcopydouble_fs = FShader(R"(
        #version 330 core

        uniform sampler2D tex;
        uniform sampler2D tex_outline;
        smooth in vec2 uv;
        layout(location = 0) out vec4 outColour;
        layout(location = 1) out vec4 outColour_outline;

        void main()
        {
            outColour = texture(tex, uv);
            outColour_outline = texture(tex_outline, uv);
        }
    )");

    auto outlineoverlay_fs = FShader(R"(
        #version 330 core

        uniform sampler2D tex;
        uniform sampler2D tex_outline;
        uniform float opacity;
        smooth in vec2 uv;
        out vec4 outColour;

        void main()
        {
            vec4 c1 = texture(tex, uv);
            vec4 c2 = texture(tex_outline, uv);
            vec4 c1w = vec4(mix(c1.rgb, vec3(1.0, 1.0, 1.0) * c1.a, opacity), c1.a);
            outColour = c1w * (1.0 - c2.a) + c2;
        }
    )");

    auto xray_fs = FShader(R"(
        #version 330 core

        uniform sampler2D tex;
        uniform sampler2D tex_outline;
        uniform vec2 pos;
        uniform float radius;
        smooth in vec2 uv;
        out vec4 outColour;

        void main()
        {
            vec4 c1 = texture(tex, uv);
            vec4 c2 = texture(tex_outline, uv);

            float r = length(gl_FragCoord.xy - pos);
            r = clamp((radius - r) / 2.0, 0.0, 1.0);

            outColour = mix(c1, c2, r);
        }
    )");

    auto outlineoverlayxray_fs = FShader(R"(
        #version 330 core

        uniform sampler2D tex;
        uniform sampler2D tex_outline;
        uniform float opacity;
        uniform vec2 pos;
        uniform float radius;
        smooth in vec2 uv;
        out vec4 outColour;

        void main()
        {
            vec4 c1 = texture(tex, uv);
            vec4 c2 = texture(tex_outline, uv);
            vec4 c1w = vec4(mix(c1.rgb, vec3(1.0, 1.0, 1.0) * c1.a, opacity), c1.a);
            outColour = c1w * (1.0 - c2.a) + c2;

            float r = length(gl_FragCoord.xy - pos);
            r = clamp((radius - r) / 2.0, 0.0, 1.0);

            outColour = mix(outColour, c2, r);
        }
    )");

    auto checker_fs = FShader(R"(
        #version 330 core

        uniform float size;
        uniform vec3 col1, col2;
        out vec4 outColour;

        void main()
        {
            vec2 a = floor(fract(gl_FragCoord.xy / size) * 2.0);
            float b = abs(a.x - a.y);
            outColour = vec4((1.0 - b) * col1 + b * col2, 1.0);
        }
    )");

    auto shadow_gs = GShader(R"(
        #version 330 core

        layout(triangles) in;
        layout(triangle_strip, max_vertices = 10) out;

        uniform vec2 wh;
        uniform float size;
        uniform vec2 dir;

        smooth out vec2 uv;
        flat out vec2 maxuv;

        void f(vec4 p, vec4 v0, mat2 m)
        {
            gl_Position = p;
            uv = m * (p.xy - v0.xy);
            EmitVertex();
        }

        float push(float x)
        {
            return 0.15 * (1.0 + clamp(x / 0.707, -1.0, 1.0));
        }

        void main()
        {
            vec4 v0 = gl_in[0].gl_Position;
            vec4 v1 = gl_in[1].gl_Position;
            vec4 v2 = gl_in[2].gl_Position;
            vec4 v3 = gl_in[2].gl_Position - gl_in[1].gl_Position + gl_in[0].gl_Position;

            vec2 a = normalize((v1 - v0).xy * wh);
            vec2 b = normalize((v3 - v0).xy * wh);
            float det = a.x * b.y - a.y * b.x;
            float s = -sign(det);
            vec2 c = size / abs(det) / wh;
            vec4 d = vec4(a * c, 0.0, 0.0);
            vec4 e = vec4(b * c, 0.0, 0.0);
            mat2 m = s * mat2(a.y, -b.y, -a.x, b.x) * mat2(wh.x, 0.0, 0.0, wh.y) / size;

            float ap = s * dot(vec2(a.y, -a.x), dir);
            float bp = s * dot(vec2(-b.y, b.x), dir);
            v0.xy += (b *  push( ap) + a *  push( bp)) * size / wh;
            v1.xy += (b *  push( ap) + a * -push(-bp)) * size / wh;
            v2.xy += (b * -push(-ap) + a * -push(-bp)) * size / wh;
            v3.xy += (b * -push(-ap) + a *  push( bp)) * size / wh;

            maxuv = m * (v2.xy - v0.xy);
            f(v0, v0, m);
            f(v0 - d - e, v0, m);
            f(v1, v0, m);
            f(v1 + d - e, v0, m);
            f(v2, v0, m);
            f(v2 + d + e, v0, m);
            f(v3, v0, m);
            f(v3 - d + e, v0, m);
            f(v0, v0, m);
            f(v0 - d - e, v0, m);
            EndPrimitive();
        }
    )");

    auto shadow_fs = FShader(R"(
        #version 330 core

        uniform vec4 shadow_col;

        smooth in vec2 uv;
        flat in vec2 maxuv;

        out vec4 outColour;

        void main()
        {
            float x = max(uv.x - maxuv.x, 0.0) - max(-uv.x, 0.0);
            float y = max(uv.y - maxuv.y, 0.0) - max(-uv.y, 0.0);
            float s = min(length(vec2(x, y)), 1.0);

            float A = 4.0; // This coefficient changes how steep the curve is and controls shadow drop-off.
            s = (exp(A * (1.0 - s)) - 1.0) / (exp(A) - 1.0); // Exponential decay for drop shadow - long tail.

            outColour = shadow_col * s;
        }
    )");

    texcopy.create(vs, texcopy_fs);
    texcopydouble.create(vs, texcopydouble_fs);
    outlineoverlay.create(vs, outlineoverlay_fs);
    xray.create(vs, xray_fs);
    outlineoverlayxray.create(vs, outlineoverlayxray_fs);
    checker.create(vs, checker_fs);
    shadow.create(vs, shadow_gs, shadow_fs);

    // Create the framebuffer object for rendering to off-view fragments.
    glGenFramebuffers(1, &fbo);

    // Create the texture cache.
    texturecache = TextureCache::create();

    // Create the PixelStreamer.
    pixelstreamer = PixelStreamer::create_supported(pref_to_pixelstreamer(prefs.pixelstreamer_method));

    // Set the last known state as unspecified, forcing a pipeline recreation whatever the next operation is.
    state = State::None;
}

GLGraphics::~GLGraphics()
{
    glDeleteFramebuffers(1, &fbo);
}

std::unique_ptr<Graphics> Graphics::create_gl(Prefs const &prefs, Stores const &stores, PageInfo const &pi)
{
    return std::make_unique<GLGraphics>(prefs, stores, pi);
}

void GLGraphics::set_outlines_enabled(bool enabled)
{
    outlines_enabled = enabled;
    if (!enabled) {
        store.outline_texture.clear();
        snapshot.outline_texture.clear();
    }
}

void GLGraphics::setup_stores_pipeline()
{
    if (state == State::Stores) return;
    state = State::Stores;

    glDisable(GL_BLEND);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    GLuint constexpr attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(outlines_enabled ? 2 : 1, attachments);

    auto const &shader = outlines_enabled ? texcopydouble : texcopy;
    glUseProgram(shader.id);
    mat_loc = shader.loc("mat");
    trans_loc = shader.loc("trans");
    geom_to_uniform({1.0, 1.0}, shader.loc("subrect"));
    tex_loc = shader.loc("tex");
    if (outlines_enabled) texoutline_loc = shader.loc("tex_outline");
}

void GLGraphics::recreate_store(Geom::IntPoint const &dims)
{
    auto tex_size = dims * scale_factor;

    // Setup the base pipeline.
    setup_stores_pipeline();

    // Recreate the store textures.
    auto recreate = [&] (Texture &tex) {
        if (tex && tex.size() == tex_size) {
            tex.invalidate();
        } else {
            tex = Texture(tex_size);
        }
    };

    recreate(store.texture);
    if (outlines_enabled) {
        recreate(store.outline_texture);
    }

    // Bind the store to the framebuffer for writing to.
                          glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, store.texture.id(),         0);
    if (outlines_enabled) glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, store.outline_texture.id(), 0);
    glViewport(0, 0, store.texture.size().x(), store.texture.size().y());

    // Clear the store to transparent.
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);
}

void GLGraphics::shift_store(Fragment const &dest)
{
    auto tex_size = dest.rect.dimensions() * scale_factor;

    // Setup the base pipeline.
    setup_stores_pipeline();

    // Create the new fragment.
    auto create_or_reuse = [&] (Texture &tex, Texture &from) {
        if (from && from.size() == tex_size) {
            from.invalidate();
            tex = std::move(from);
        } else {
            tex = Texture(tex_size);
        }
    };

    GLFragment fragment;
    create_or_reuse(fragment.texture, snapshot.texture);
    if (outlines_enabled) {
        create_or_reuse(fragment.outline_texture, snapshot.outline_texture);
    }

    // Bind new store to the framebuffer to writing to.
                          glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fragment.texture        .id(), 0);
    if (outlines_enabled) glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, fragment.outline_texture.id(), 0);
    glViewport(0, 0, fragment.texture.size().x(), fragment.texture.size().y());

    // Clear new store to transparent.
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    // Bind the old store to texture units 0 and 1 for reading from.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, store.texture.id());
    glUniform1i(tex_loc, 0);
    if (outlines_enabled) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, store.outline_texture.id());
        glUniform1i(texoutline_loc, 1);
    }
    glBindVertexArray(rect.vao);

    // Copy re-usuable contents of the old store into the new store.
    geom_to_uniform(calc_paste_transform(stores.store(), dest), mat_loc, trans_loc);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Set the result as the new store.
    snapshot = std::move(store);
    store = std::move(fragment);
}

void GLGraphics::swap_stores()
{
    std::swap(store, snapshot);
}

void GLGraphics::fast_snapshot_combine()
{
    // Ensure the base pipeline is correctly set up.
    setup_stores_pipeline();

    // Compute the vertex data for the drawn region.
    auto [clean_vao, clean_numrects] = region_shrink_vao(stores.store().drawn, stores.store().rect);

    // Bind the snapshot to the framebuffer for writing to.
                          glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, snapshot.texture.id(),         0);
    if (outlines_enabled) glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, snapshot.outline_texture.id(), 0);
    glViewport(0, 0, snapshot.texture.size().x(), snapshot.texture.size().y());

    // Bind the store to texture unit 0 (and its outline to 1, if necessary).
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, store.texture.id());
    glUniform1i(tex_loc, 0);
    if (outlines_enabled) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, store.outline_texture.id());
        glUniform1i(texoutline_loc, 1);
    }

    // Copy the clean region of the store to the snapshot.
    geom_to_uniform(calc_paste_transform(stores.store(), stores.snapshot()), mat_loc, trans_loc);
    glBindVertexArray(clean_vao.vao);
    glDrawArrays(GL_TRIANGLES, 0, 6 * clean_numrects);
}

void GLGraphics::snapshot_combine(Fragment const &dest)
{
    // Create the new fragment.
    auto content_size = dest.rect.dimensions() * scale_factor;

    // Ensure the base pipeline is correctly set up.
    setup_stores_pipeline();

    // Compute the vertex data for the clean region.
    auto [clean_vao, clean_numrects] = region_shrink_vao(stores.store().drawn, stores.store().rect);

    GLFragment fragment;
                          fragment.texture         = Texture(content_size);
    if (outlines_enabled) fragment.outline_texture = Texture(content_size);

    // Bind the new fragment to the framebuffer for writing to.
                          glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fragment.texture.id(),         0);
    if (outlines_enabled) glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, fragment.outline_texture.id(), 0);

    // Clear the new fragment to transparent.
    glViewport(0, 0, fragment.texture.size().x(), fragment.texture.size().y());
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    // Bind the store and snapshot to texture units 0 and 1 (and their outlines to 2 and 3, if necessary).
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, snapshot.texture.id());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, store.texture.id());
    if (outlines_enabled) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, snapshot.outline_texture.id());
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, store.outline_texture.id());
    }

    // Paste the snapshot store onto the new fragment.
    glUniform1i(tex_loc, 0);
    if (outlines_enabled) glUniform1i(texoutline_loc, 2);
    geom_to_uniform(calc_paste_transform(stores.snapshot(), dest), mat_loc, trans_loc);
    glBindVertexArray(rect.vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Paste the backing store onto the new fragment.
    glUniform1i(tex_loc, 1);
    if (outlines_enabled) glUniform1i(texoutline_loc, 3);
    geom_to_uniform(calc_paste_transform(stores.store(), dest), mat_loc, trans_loc);
    glBindVertexArray(clean_vao.vao);
    glDrawArrays(GL_TRIANGLES, 0, 6 * clean_numrects);

    // Set the result as the new snapshot.
    snapshot = std::move(fragment);
}

void GLGraphics::invalidate_snapshot()
{
    if (snapshot.texture) snapshot.texture.invalidate();
    if (snapshot.outline_texture) snapshot.outline_texture.invalidate();
}

void GLGraphics::setup_tiles_pipeline()
{
    if (state == State::Tiles) return;
    state = State::Tiles;

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    GLuint constexpr attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(outlines_enabled ? 2 : 1, attachments);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, store.texture.id(), 0);
    if (outlines_enabled) glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, store.outline_texture.id(), 0);
    glViewport(0, 0, store.texture.size().x(), store.texture.size().y());

    auto const &shader = outlines_enabled ? texcopydouble : texcopy;
    glUseProgram(shader.id);
    mat_loc = shader.loc("mat");
    trans_loc = shader.loc("trans");
    subrect_loc = shader.loc("subrect");
    glUniform1i(shader.loc("tex"), 0);
    if (outlines_enabled) glUniform1i(shader.loc("tex_outline"), 1);

    glBindVertexArray(rect.vao);
    glDisable(GL_BLEND);
};

Cairo::RefPtr<Cairo::ImageSurface> GLGraphics::request_tile_surface(Geom::IntRect const &rect, bool nogl)
{
    Cairo::RefPtr<Cairo::ImageSurface> surface;

    {
        auto g = std::lock_guard(ps_mutex);
        surface = pixelstreamer->request(rect.dimensions() * scale_factor, nogl);
    }

    if (surface) {
        cairo_surface_set_device_scale(surface->cobj(), scale_factor, scale_factor);
    }

    return surface;
}

void GLGraphics::draw_tile(Fragment const &fragment, Cairo::RefPtr<Cairo::ImageSurface> surface, Cairo::RefPtr<Cairo::ImageSurface> outline_surface)
{
    auto g = std::lock_guard(ps_mutex);
    auto surface_size = dimensions(surface);

    Texture texture, outline_texture;

    glActiveTexture(GL_TEXTURE0);
    texture = texturecache->request(surface_size); // binds
    pixelstreamer->finish(std::move(surface)); // uploads content

    if (outlines_enabled) {
        glActiveTexture(GL_TEXTURE1);
        outline_texture = texturecache->request(surface_size);
        pixelstreamer->finish(std::move(outline_surface));
    }

    setup_tiles_pipeline();

    geom_to_uniform(calc_paste_transform(fragment, stores.store()), mat_loc, trans_loc);
    geom_to_uniform(Geom::Point(surface_size) / texture.size(), subrect_loc);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    texturecache->finish(std::move(texture));
    if (outlines_enabled) {
        texturecache->finish(std::move(outline_texture));
    }
}

void GLGraphics::junk_tile_surface(Cairo::RefPtr<Cairo::ImageSurface> surface)
{
    auto g = std::lock_guard(ps_mutex);
    pixelstreamer->finish(std::move(surface), true);
}

void GLGraphics::setup_widget_pipeline(Fragment const &view)
{
    state = State::Widget;

    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0, 0, view.rect.width() * scale_factor, view.rect.height() * scale_factor);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_NOTEQUAL, 1, 1);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, store.texture.id());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, snapshot.texture.id());
    if (outlines_enabled) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, store.outline_texture.id());
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, snapshot.outline_texture.id());
    }
    glBindVertexArray(rect.vao);
};

void GLGraphics::paint_widget(Fragment const &view, PaintArgs const &a, Cairo::RefPtr<Cairo::Context> const&)
{
    // If in decoupled mode, create the vertex data describing the drawn region of the store.
    VAO clean_vao;
    int clean_numrects;
    if (stores.mode() == Stores::Mode::Decoupled) {
        std::tie(clean_vao, clean_numrects) = region_shrink_vao(stores.store().drawn, stores.store().rect);
    }

    setup_widget_pipeline(view);

    // Clear the buffers. Since we have to pick a clear colour, we choose the page colour, enabling the single-page optimisation later.
    glClearColor(SP_RGBA32_R_U(page) / 255.0f, SP_RGBA32_G_U(page) / 255.0f, SP_RGBA32_B_U(page) / 255.0f, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    if (check_single_page(view, pi)) {
        // A single page occupies the whole view.
        if (SP_RGBA32_A_U(page) == 255) {
            // Page is solid - nothing to do, since already cleared to this colour.
        } else {
            // Page is checkerboard - fill view with page pattern.
            glDisable(GL_BLEND);
            glUseProgram(checker.id);
            glUniform1f(checker.loc("size"), 12.0 * scale_factor);
            glUniform3fv(checker.loc("col1"), 1, std::begin(rgb_to_array(page)));
            glUniform3fv(checker.loc("col2"), 1, std::begin(checkerboard_darken(page)));
            geom_to_uniform(Geom::Scale(2.0, -2.0) * Geom::Translate(-1.0, 1.0), checker.loc("mat"), checker.loc("trans"));
            geom_to_uniform({1.0, 1.0}, checker.loc("subrect"));
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);

        auto set_page_transform = [&] (Geom::Rect const &rect, Program const &prog) {
            geom_to_uniform(Geom::Scale(rect.dimensions()) * Geom::Translate(rect.min()) * calc_paste_transform({{}, Geom::IntRect::from_xywh(0, 0, 1, 1)}, view) * Geom::Scale(1.0, -1.0), prog.loc("mat"), prog.loc("trans"));
        };

        // Pages
        glUseProgram(checker.id);
        glUniform1f(checker.loc("size"), 12.0 * scale_factor);
        glUniform3fv(checker.loc("col1"), 1, std::begin(rgb_to_array(page)));
        glUniform3fv(checker.loc("col2"), 1, std::begin(checkerboard_darken(page)));
        geom_to_uniform({1.0, 1.0}, checker.loc("subrect"));
        for (auto &rect : pi.pages) {
            set_page_transform(rect, checker);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        }

        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

        // Desk
        glUniform3fv(checker.loc("col1"), 1, std::begin(rgb_to_array(desk)));
        glUniform3fv(checker.loc("col2"), 1, std::begin(checkerboard_darken(desk)));
        geom_to_uniform(Geom::Scale(2.0, -2.0) * Geom::Translate(-1.0, 1.0), checker.loc("mat"), checker.loc("trans"));
        geom_to_uniform({1.0, 1.0}, checker.loc("subrect"));
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        // Shadows
        if (SP_RGBA32_A_U(border) != 0) {
            auto dir = (Geom::Point(1.0, a.yaxisdir) * view.affine * Geom::Scale(1.0, -1.0)).normalized(); // Shadow direction rotates with view.
            glUseProgram(shadow.id);
            geom_to_uniform({1.0, 1.0}, shadow.loc("subrect"));
            glUniform2fv(shadow.loc("wh"), 1, std::begin({(GLfloat)view.rect.width(), (GLfloat)view.rect.height()}));
            glUniform1f(shadow.loc("size"), 40.0 * std::pow(std::abs(view.affine.det()), 0.25));
            glUniform2fv(shadow.loc("dir"), 1, std::begin({(GLfloat)dir.x(), (GLfloat)dir.y()}));
            glUniform4fv(shadow.loc("shadow_col"), 1, std::begin(premultiplied(rgba_to_array(border))));
            for (auto &rect : pi.pages) {
                set_page_transform(rect, shadow);
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
        }

        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    }

    glStencilFunc(GL_NOTEQUAL, 2, 2);

    enum class DrawMode
    {
        Store,
        Outline,
        Combine
    };

    auto draw_store = [&, this] (Program const &prog, DrawMode drawmode) {
        glUseProgram(prog.id);
        geom_to_uniform({1.0, 1.0}, prog.loc("subrect"));
        glUniform1i(prog.loc("tex"), drawmode == DrawMode::Outline ? 2 : 0);
        if (drawmode == DrawMode::Combine) {
            glUniform1i(prog.loc("tex_outline"), 2);
            glUniform1f(prog.loc("opacity"), prefs.outline_overlay_opacity / 100.0);
        }

        if (stores.mode() == Stores::Mode::Normal) {
            // Backing store fragment.
            geom_to_uniform(calc_paste_transform(stores.store(), view) * Geom::Scale(1.0, -1.0), prog.loc("mat"), prog.loc("trans"));
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        } else {
            // Backing store fragment, clipped to its clean region.
            geom_to_uniform(calc_paste_transform(stores.store(), view) * Geom::Scale(1.0, -1.0), prog.loc("mat"), prog.loc("trans"));
            glBindVertexArray(clean_vao.vao);
            glDrawArrays(GL_TRIANGLES, 0, 6 * clean_numrects);

            // Snapshot fragment.
            glUniform1i(prog.loc("tex"), drawmode == DrawMode::Outline ? 3 : 1);
            if (drawmode == DrawMode::Combine) glUniform1i(prog.loc("tex_outline"), 3);
            geom_to_uniform(calc_paste_transform(stores.snapshot(), view) * Geom::Scale(1.0, -1.0), prog.loc("mat"), prog.loc("trans"));
            glBindVertexArray(rect.vao);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        }
    };

    if (a.splitmode == Inkscape::SplitMode::NORMAL || (a.splitmode == Inkscape::SplitMode::XRAY && !a.mouse)) {

        // Drawing the backing store over the whole view.
        a.render_mode == Inkscape::RenderMode::OUTLINE_OVERLAY
                        ? draw_store(outlineoverlay, DrawMode::Combine)
                        : draw_store(texcopy, DrawMode::Store);

    } else if (a.splitmode == Inkscape::SplitMode::SPLIT) {

        // Calculate the clipping rectangles for split view.
        auto [store_clip, outline_clip] = calc_splitview_cliprects(view.rect.dimensions(), a.splitfrac, a.splitdir);

        glEnable(GL_SCISSOR_TEST);

        // Draw the backing store.
        glScissor(store_clip.left() * scale_factor, (view.rect.height() - store_clip.bottom()) * scale_factor, store_clip.width() * scale_factor, store_clip.height() * scale_factor);
        a.render_mode == Inkscape::RenderMode::OUTLINE_OVERLAY
                        ? draw_store(outlineoverlay, DrawMode::Combine)
                        : draw_store(texcopy, DrawMode::Store);

        // Draw the outline store.
        glScissor(outline_clip.left() * scale_factor, (view.rect.height() - outline_clip.bottom()) * scale_factor, outline_clip.width() * scale_factor, outline_clip.height() * scale_factor);
        draw_store(texcopy, DrawMode::Outline);

        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);

        // Calculate the bounding rectangle of the split view controller.
        auto rect = Geom::IntRect({0, 0}, view.rect.dimensions());
        auto dim = a.splitdir == Inkscape::SplitDirection::EAST || a.splitdir == Inkscape::SplitDirection::WEST ? Geom::X : Geom::Y;
        rect[dim] = Geom::IntInterval(-21, 21) + std::round(a.splitfrac[dim] * view.rect.dimensions()[dim]);

        // Lease out a PixelStreamer mapping to draw on.
        auto surface_size = rect.dimensions() * scale_factor;
        auto surface = pixelstreamer->request(surface_size);
        cairo_surface_set_device_scale(surface->cobj(), scale_factor, scale_factor);

        // Actually draw the content with Cairo.
        auto cr = Cairo::Context::create(surface);
        cr->set_operator(Cairo::Context::Operator::SOURCE);
        cr->set_source_rgba(0.0, 0.0, 0.0, 0.0);
        cr->paint();
        cr->translate(-rect.left(), -rect.top());
        paint_splitview_controller(view.rect.dimensions(), a.splitfrac, a.splitdir, a.hoverdir, cr);

        // Convert the surface to a texture.
        glActiveTexture(GL_TEXTURE0);
        auto texture = texturecache->request(surface_size);
        pixelstreamer->finish(std::move(surface));

        // Paint the texture onto the view.
        glUseProgram(texcopy.id);
        glUniform1i(texcopy.loc("tex"), 0);
        geom_to_uniform(Geom::Scale(rect.dimensions()) * Geom::Translate(rect.min()) * Geom::Scale(2.0 / view.rect.width(), -2.0 / view.rect.height()) * Geom::Translate(-1.0, 1.0), texcopy.loc("mat"), texcopy.loc("trans"));
        geom_to_uniform(Geom::Point(surface_size) / texture.size(), texcopy.loc("subrect"));
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        // Return the texture back to the texture cache.
        texturecache->finish(std::move(texture));
    } else { // if (_split_mode == Inkscape::SplitMode::XRAY && a.mouse)
        // Draw the backing store over the whole view.
        auto const &shader = a.render_mode == Inkscape::RenderMode::OUTLINE_OVERLAY ? outlineoverlayxray : xray;
        glUseProgram(shader.id);
        glUniform1f(shader.loc("radius"), prefs.xray_radius * scale_factor);
        glUniform2fv(shader.loc("pos"), 1, std::begin({(GLfloat)(a.mouse->x() * scale_factor), (GLfloat)((view.rect.height() - a.mouse->y()) * scale_factor)}));
        draw_store(shader, DrawMode::Combine);
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
