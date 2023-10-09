// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cmath>
#include <vector>
#include <epoxy/gl.h>
#include "pixelstreamer.h"
#include "helper/mathfns.h"

namespace Inkscape {
namespace UI {
namespace Widget {
namespace {

cairo_user_data_key_t constexpr key{};

class PersistentPixelStreamer : public PixelStreamer
{
    static int constexpr bufsize = 0x1000000; // 16 MiB

    struct Buffer
    {
        GLuint pbo;          // Pixel buffer object.
        unsigned char *data; // The pointer to the mapped region.
        int off;             // Offset of the unused region, in bytes. Always a multiple of 64.
        int refs;            // How many mappings are currently using this buffer.
        GLsync sync;         // Sync object for telling us when the GPU has finished reading from this buffer.
        bool ready;          // Whether this buffer is ready for re-use.

        void create()
        {
            glGenBuffers(1, &pbo);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
            glBufferStorage(GL_PIXEL_UNPACK_BUFFER, bufsize, nullptr, GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT);
            data = (unsigned char*)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, bufsize, GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_FLUSH_EXPLICIT_BIT);
            off = 0;
            refs = 0;
        }

        void destroy()
        {
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
            glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
            glDeleteBuffers(1, &pbo);
        }

        // Advance a buffer in state 3 or 4 as far as possible towards state 5.
        void advance()
        {
            if (!sync) {
                sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            } else {
                auto ret = glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
                if (ret == GL_CONDITION_SATISFIED || ret == GL_ALREADY_SIGNALED) {
                    glDeleteSync(sync);
                    ready = true;
                }
            }
        }
    };
    std::vector<Buffer> buffers;

    int current_buffer;

    struct Mapping
    {
        bool used;                 // Whether the mapping is in use, or on the freelist.
        int buf;                   // The buffer the mapping is using.
        int off;                   // Offset of the mapped region.
        int size;                  // Size of the mapped region.
        int width, height, stride; // Image properties.
    };
    std::vector<Mapping> mappings;

    /*
     * A Buffer cycles through the following five states:
     *
     *     1. Current                                -->  We are currently filling this buffer up with allocations.
     *     2. Not current, refs > 0                  -->  Finished the above, but may still be writing into it and issuing GL commands from it.
     *     3. Not current, refs == 0, !ready, !sync  -->  Finished the above, but GL may be reading from it. We have yet to create its sync object.
     *     4. Not current, refs == 0, !ready, sync   -->  We have now created its sync object, but it has not been signalled yet.
     *     5. Not current, refs == 0, ready          -->  The sync object has been signalled and deleted.
     *
     * Only one Buffer is Current at any given time, and is marked by the current_buffer variable.
     */

public:
    PersistentPixelStreamer()
    {
        // Create a single initial buffer and make it the current buffer.
        buffers.emplace_back();
        buffers.back().create();
        current_buffer = 0;
    }

    Method get_method() const override { return Method::Persistent; }

    Cairo::RefPtr<Cairo::ImageSurface> request(Geom::IntPoint const &dimensions, bool nogl) override
    {
        // Calculate image properties required by cairo.
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, dimensions.x());
        int size = stride * dimensions.y();
        int sizeup = Util::round_up(size, 64);
        assert(sizeup < bufsize);

        // Attempt to advance buffers in states 3 or 4 towards 5, if allowed.
        if (!nogl) {
            for (int i = 0; i < buffers.size(); i++) {
                if (i != current_buffer && buffers[i].refs == 0 && !buffers[i].ready) {
                    buffers[i].advance();
                }
            }
        }
        // Continue using the current buffer if possible.
        if (buffers[current_buffer].off + sizeup <= bufsize) {
            goto chosen_buffer;
        }
        // Otherwise, the current buffer has filled up. After this point, the current buffer will change.
        // Therefore, handle the state change of the current buffer out of the Current state. Usually that
        // means doing nothing because the transition to state 2 is automatic. But if refs == 0 already,
        // then we need to transition into state 3 by setting ready = false. If we're allowed to use GL,
        // then we can additionally transition into state 4 by creating the sync object.
        if (buffers[current_buffer].refs == 0) {
            buffers[current_buffer].ready = false;
            buffers[current_buffer].sync = nogl ? nullptr : glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        }
        // Attempt to re-use a old buffer that has reached state 5.
        for (int i = 0; i < buffers.size(); i++) {
            if (i != current_buffer && buffers[i].refs == 0 && buffers[i].ready) {
                // Found an unused buffer. Re-use it. (Move to state 1.)
                buffers[i].off = 0;
                current_buffer = i;
                goto chosen_buffer;
            }
        }
        // Otherwise, there are no available buffers. Create and use a new one. That requires GL, so fail if not allowed.
        if (nogl) {
            return {};
        }
        buffers.emplace_back();
        buffers.back().create();
        current_buffer = buffers.size() - 1;
    chosen_buffer:
        // Finished changing the current buffer.
        auto &b = buffers[current_buffer];

        // Choose/create the mapping to use.
        auto choose_mapping = [&, this] {
            for (int i = 0; i < mappings.size(); i++) {
                if (!mappings[i].used) {
                    // Found unused mapping.
                    return i;
                }
            }
            // No free mapping; create one.
            mappings.emplace_back();
            return (int)mappings.size() - 1;
        };

        auto mapping = choose_mapping();
        auto &m = mappings[mapping];

        // Set up the mapping bookkeeping.
        m = {true, current_buffer, b.off, size, dimensions.x(), dimensions.y(), stride};
        b.off += sizeup;
        b.refs++;

        // Create the image surface.
        auto surface = Cairo::ImageSurface::create(b.data + m.off, Cairo::Surface::Format::ARGB32, dimensions.x(), dimensions.y(), stride);

        // Attach the mapping handle as user data.
        cairo_surface_set_user_data(surface->cobj(), &key, (void*)(uintptr_t)mapping, nullptr);

        return surface;
    }

    void finish(Cairo::RefPtr<Cairo::ImageSurface> surface, bool junk) override
    {
        // Extract the mapping handle from the surface's user data.
        auto mapping = (int)(uintptr_t)cairo_surface_get_user_data(surface->cobj(), &key);

        // Flush all changes from the image surface to the buffer, and delete it.
        surface.reset();

        auto &m = mappings[mapping];
        auto &b = buffers[m.buf];

        // Flush the mapped subregion.
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, b.pbo);
        glFlushMappedBufferRange(GL_PIXEL_UNPACK_BUFFER, m.off, m.size);

        // Tear down the mapping bookkeeping. (if this causes transition 2 --> 3, it is handled below.)
        m.used = false;
        b.refs--;

        // Upload to the texture from the mapped subregion.
        if (!junk) {
            glPixelStorei(GL_UNPACK_ROW_LENGTH, m.stride / 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m.width, m.height, GL_BGRA, GL_UNSIGNED_BYTE, (void*)(uintptr_t)m.off);
        }

        // If the buffer is due for recycling, issue a sync command so that we can recycle it when it's ready. (Handle transition 2 --> 4.)
        if (m.buf != current_buffer && b.refs == 0) {
            b.ready = false;
            b.sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        }

        // Check other buffers to see if they're ready for recycling. (Advance from 3/4 towards 5.)
        for (int i = 0; i < buffers.size(); i++) {
            if (i != current_buffer && i != m.buf && buffers[i].refs == 0 && !buffers[i].ready) {
                buffers[i].advance();
            }
        }
    }

    ~PersistentPixelStreamer() override
    {
        // Delete any sync objects. (For buffers in state 4.)
        for (int i = 0; i < buffers.size(); i++) {
            if (i != current_buffer && buffers[i].refs == 0 && !buffers[i].ready && buffers[i].sync) {
                glDeleteSync(buffers[i].sync);
            }
        }

        // Wait for GL to finish reading out of all the buffers.
        glFinish();

        // Deallocate the buffers on the GL side.
        for (auto &b : buffers) {
            b.destroy();
        }
    }
};

class AsynchronousPixelStreamer : public PixelStreamer
{
    static int constexpr minbufsize = 0x4000; // 16 KiB
    static int constexpr expire_timeout = 10000;

    static int constexpr size_to_bucket(int size) { return Util::floorlog2((size - 1) / minbufsize) + 1; }
    static int constexpr bucket_maxsize(int b) { return minbufsize * (1 << b); }

    struct Buffer
    {
        GLuint pbo;
        unsigned char *data;

        void create(int size)
        {
            glGenBuffers(1, &pbo);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
            glBufferData(GL_PIXEL_UNPACK_BUFFER, size, nullptr, GL_STREAM_DRAW);
            data = (unsigned char*)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, size, GL_MAP_READ_BIT | GL_MAP_WRITE_BIT);
        }

        void destroy()
        {
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
            glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
            glDeleteBuffers(1, &pbo);
        }
    };

    struct Bucket
    {
        std::vector<Buffer> spares;
        int used = 0;
        int high_use_count = 0;
    };
    std::vector<Bucket> buckets;

    struct Mapping
    {
        bool used;
        Buffer buf;
        int bucket;
        int width, height, stride;
    };
    std::vector<Mapping> mappings;

    int expire_timer = 0;

public:
    Method get_method() const override { return Method::Asynchronous; }

    Cairo::RefPtr<Cairo::ImageSurface> request(Geom::IntPoint const &dimensions, bool nogl) override
    {
        // Calculate image properties required by cairo.
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, dimensions.x());
        int size = stride * dimensions.y();

        // Find the bucket that size falls into.
        int bucket = size_to_bucket(size);
        if (bucket >= buckets.size()) {
            buckets.resize(bucket + 1);
        }
        auto &b = buckets[bucket];

        // Find/create a buffer of the appropriate size.
        Buffer buf;
        if (!b.spares.empty()) {
            // If the bucket has any spare mapped buffers, then use one of them.
            buf = std::move(b.spares.back());
            b.spares.pop_back();
        } else if (!nogl) {
            // Otherwise, we have to use OpenGL to create and map a new buffer.
            buf.create(bucket_maxsize(bucket));
        } else {
            // If we're not allowed to issue GL commands, then that is a failure.
            return {};
        }

        // Record the new use count of the bucket.
        b.used++;
        if (b.used > b.high_use_count) {
            // If the use count has gone above the high-water mark, record it and reset the timer for when to clean up excess spares.
            b.high_use_count = b.used;
            expire_timer = 0;
        }

        auto choose_mapping = [&, this] {
            for (int i = 0; i < mappings.size(); i++) {
                if (!mappings[i].used) {
                    return i;
                }
            }
            mappings.emplace_back();
            return (int)mappings.size() - 1;
        };

        auto mapping = choose_mapping();
        auto &m = mappings[mapping];

        m.used = true;
        m.buf = std::move(buf);
        m.bucket = bucket;
        m.width = dimensions.x();
        m.height = dimensions.y();
        m.stride = stride;

        auto surface = Cairo::ImageSurface::create(m.buf.data, Cairo::Surface::Format::ARGB32, m.width, m.height, m.stride);
        cairo_surface_set_user_data(surface->cobj(), &key, (void*)(uintptr_t)mapping, nullptr);
        return surface;
    }

    void finish(Cairo::RefPtr<Cairo::ImageSurface> surface, bool junk) override
    {
        auto mapping = (int)(uintptr_t)cairo_surface_get_user_data(surface->cobj(), &key);
        surface.reset();

        auto &m = mappings[mapping];
        auto &b = buckets[m.bucket];

        // Unmap the buffer.
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m.buf.pbo);
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);

        // Upload the buffer to the texture.
        if (!junk) {
            glPixelStorei(GL_UNPACK_ROW_LENGTH, m.stride / 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m.width, m.height, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        }

        // Mark the mapping slot as unused.
        m.used = false;

        // Orphan and re-map the buffer.
        auto size = bucket_maxsize(m.bucket);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, size, nullptr, GL_STREAM_DRAW);
        m.buf.data = (unsigned char*)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, size, GL_MAP_READ_BIT | GL_MAP_WRITE_BIT);

        // Put the buffer back in its corresponding bucket's pile of spares.
        b.spares.emplace_back(std::move(m.buf));
        b.used--;

        // If the expiration timeout has been reached, get rid of excess spares from all buckets, and reset the high use counts.
        expire_timer++;
        if (expire_timer >= expire_timeout) {
            expire_timer = 0;

            for (auto &b : buckets) {
                int max_spares = b.high_use_count - b.used;
                assert(max_spares >= 0);
                if (b.spares.size() > max_spares) {
                    for (int i = max_spares; i < b.spares.size(); i++) {
                        b.spares[i].destroy();
                    }
                    b.spares.resize(max_spares);
                }
                b.high_use_count = b.used;
            }
        }
    }

    ~AsynchronousPixelStreamer() override
    {
        // Unmap and delete all spare buffers. (They are not being used.)
        for (auto &b : buckets) {
            for (auto &buf : b.spares) {
                buf.destroy();
            }
        }
    }
};

class SynchronousPixelStreamer : public PixelStreamer
{
    struct Mapping
    {
        bool used;
        std::vector<unsigned char> data;
        int size, width, height, stride;
    };
    std::vector<Mapping> mappings;

public:
    Method get_method() const override { return Method::Synchronous; }

    Cairo::RefPtr<Cairo::ImageSurface> request(Geom::IntPoint const &dimensions, bool) override
    {
        auto choose_mapping = [&, this] {
            for (int i = 0; i < mappings.size(); i++) {
                if (!mappings[i].used) {
                    return i;
                }
            }
            mappings.emplace_back();
            return (int)mappings.size() - 1;
        };

        auto mapping = choose_mapping();
        auto &m = mappings[mapping];

        m.used = true;
        m.width = dimensions.x();
        m.height = dimensions.y();
        m.stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, m.width);
        m.size = m.stride * m.height;
        m.data.resize(m.size);

        auto surface = Cairo::ImageSurface::create(&m.data[0], Cairo::Surface::Format::ARGB32, m.width, m.height, m.stride);
        cairo_surface_set_user_data(surface->cobj(), &key, (void*)(uintptr_t)mapping, nullptr);
        return surface;
    }

    void finish(Cairo::RefPtr<Cairo::ImageSurface> surface, bool junk) override
    {
        auto mapping = (int)(uintptr_t)cairo_surface_get_user_data(surface->cobj(), &key);
        surface.reset();

        auto &m = mappings[mapping];

        if (!junk) {
            glPixelStorei(GL_UNPACK_ROW_LENGTH, m.stride / 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m.width, m.height, GL_BGRA, GL_UNSIGNED_BYTE, &m.data[0]);
        }

        m.used = false;
        m.data.clear();
    }
};

} // namespace

std::unique_ptr<PixelStreamer> PixelStreamer::create_supported(Method method)
{
    int ver = epoxy_gl_version();

    if (method <= Method::Asynchronous) {
        if (ver >= 30 || epoxy_has_gl_extension("GL_ARB_map_buffer_range")) {
            if (method <= Method::Persistent) {
                if (ver >= 44 || (epoxy_has_gl_extension("GL_ARB_buffer_storage") &&
                                  epoxy_has_gl_extension("GL_ARB_texture_storage") &&
                                  epoxy_has_gl_extension("GL_ARB_SYNC")))
                {
                    return std::make_unique<PersistentPixelStreamer>();
                } else if (method != Method::Auto) {
                    std::cerr << "Persistent PixelStreamer not available" << std::endl;
                }
            }
            return std::make_unique<AsynchronousPixelStreamer>();
        } else if (method != Method::Auto) {
            std::cerr << "Asynchronous PixelStreamer not available" << std::endl;
        }
    }
    return std::make_unique<SynchronousPixelStreamer>();
}

} // namespace Widget
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
