// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef INKSCAPE_UI_WIDGET_OPTGLAREA_H
#define INKSCAPE_UI_WIDGET_OPTGLAREA_H

#include <epoxy/gl.h>
#include <glibmm/refptr.h>
#include <gtkmm/drawingarea.h>

namespace Cairo { class Context; }
namespace Gdk { class GLContext; }

namespace Inkscape::UI::Widget {

/**
 * A widget that can dynamically switch between a Gtk::DrawingArea and a Gtk::GLArea.
 * Based on the source code for both widgets.
 */
class OptGLArea : public Gtk::Widget
{
public:
    OptGLArea();
    ~OptGLArea() override;

    /**
     * Set whether OpenGL is enabled. Initially it is disabled. Upon enabling it,
     * create_context will be called as soon as the widget is realized. If
     * context creation fails, OpenGL will be disabled again.
     */
    void set_opengl_enabled(bool);
    bool get_opengl_enabled() const { return opengl_enabled; }

    /**
     * Call before doing any OpenGL operations to make the context current.
     * Automatically done before calling paint_widget().
     */
    void make_current();

    /**
     * Call before rendering to the widget to bind the widget's framebuffer.
     */
    void bind_framebuffer() const;

protected:
    void on_realize() override;
    void on_unrealize() override;
    void snapshot_vfunc(Glib::RefPtr<Gtk::Snapshot> const &snapshot) override;

    /**
     * Reimplement to create the desired OpenGL context. Return nullptr on error.
     */
    virtual Glib::RefPtr<Gdk::GLContext> create_context() = 0;

    /**
     * Reimplement to render the widget. The Cairo context is only for when OpenGL is disabled.
     */
    virtual void paint_widget(Cairo::RefPtr<Cairo::Context> const &) {}

private:
    bool opengl_enabled = false;

    struct GLState;
    std::shared_ptr<GLState> gl;

    void init_opengl();
    void uninit_opengl();
};

} // namespace Inkscape::UI::Widget

#endif // INKSCAPE_UI_WIDGET_OPTGLAREA_H

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
