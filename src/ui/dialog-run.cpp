// SPDX-License-Identifier: GPL-2.0-or-later

#include <optional>

#include <glibmm/main.h>
#include <gtkmm/dialog.h>
#include <gtkmm/window.h>

#include "dialog-run.h"

namespace Inkscape::UI {

int dialog_run(Gtk::Dialog &dialog)
{
    std::optional<int> result;

    auto response_conn = dialog.signal_response().connect([&] (int response) {
        result = response;
    });

    auto hide_conn = dialog.signal_hide().connect([&] {
        result = Gtk::ResponseType::NONE;
    });

    dialog.set_modal();
    dialog.set_visible(true);

    auto main_context = Glib::MainContext::get_default();
    while (!result) {
        main_context->iteration(true);
    }

    response_conn.disconnect();
    hide_conn.disconnect();

    dialog.set_visible(false);

    return *result;
}

void dialog_show_modal_and_selfdestruct(std::unique_ptr<Gtk::Dialog> dialog, Gtk::Root * const root)
{
    if (auto const window = dynamic_cast<Gtk::Window *>(root)) {
        dialog->set_transient_for(*window);
    }
    dialog->set_modal();
    dialog->signal_response().connect([d = dialog.get()] (auto) { delete d; });
    dialog->set_visible(true);
    dialog.release(); // deleted by signal_response handler
}

} // namespace Inkscape::UI
