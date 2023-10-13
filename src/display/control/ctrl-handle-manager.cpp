// SPDX-License-Identifier: GPL-2.0-or-later
#include "ctrl-handle-manager.h"

#include <sigc++/signal.h>
#include <glibmm/main.h>
#include <giomm/file.h>
#include <giomm/filemonitor.h>

#include "ctrl-handle-styling.h"

#include "helper/auto-connection.h"
#include "io/resource.h"

namespace Inkscape::Handles {
namespace {

class ManagerImpl final : public Manager
{
public:
    ManagerImpl();

private:
    // Most recent css (shared between all CanvasItemContexts)
    std::shared_ptr<Css const> css;

    // For file system monitoring.
    Glib::RefPtr<Gio::FileMonitor> monitor;
    auto_connection timeout;

    // Emitted when the css changes.
    sigc::signal<void()> signal_css_updated;

    void updateCss();

    friend Manager;
};

ManagerImpl::ManagerImpl()
{
    // Set the initial css.
    updateCss();

    // Monitor the user css path for changes. We use a timeout to compress multiple events into one.
    auto const path = IO::Resource::get_path_string(IO::Resource::USER, IO::Resource::UIS, "node-handles.css");
    auto file = Gio::File::create_for_path(path);

    monitor = file->monitor_file();
    monitor->signal_changed().connect([this] (Glib::RefPtr<Gio::File> const &, Glib::RefPtr<Gio::File> const &, Gio::FileMonitor::Event) {
        if (timeout) return;
        timeout = Glib::signal_timeout().connect([this] {
            updateCss();
            signal_css_updated.emit();
            return false;
        }, 200);
    });
}

void ManagerImpl::updateCss()
{
    css = std::make_shared<Css const>(parse_css());
}

} // namespace

Manager &Manager::get()
{
    static ManagerImpl instance;
    return instance;
}

std::shared_ptr<Css const> Manager::getCss() const
{
    auto &d = static_cast<ManagerImpl const &>(*this);
    return d.css;
}

sigc::connection Manager::connectCssUpdated(sigc::slot<void()> &&slot)
{
    auto &d = static_cast<ManagerImpl &>(*this);
    return d.signal_css_updated.connect(std::move(slot));
}

} // namespace Inkscape::Handles

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
