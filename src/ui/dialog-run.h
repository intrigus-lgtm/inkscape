// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef INKSCAPE_UI_DIALOG_RUN_H
#define INKSCAPE_UI_DIALOG_RUN_H

#include <memory>

namespace Gtk {
class Dialog;
class Root;
} // namespace Gtk

namespace Inkscape::UI {

/**
 * This is a GTK4 porting aid meant to replace the removal of the Gtk::Dialog synchronous API.
 *
 * It is intended as a temporary measure, although experience suggests it will be anything but.
 *
 * Todo: Attempt to port code that uses this function to the asynchronous API.
 */
int dialog_run(Gtk::Dialog &dialog);

/**
 * Show a dialog modally, destroying it when the user dismisses it.
 * If toplevel is not null, the dialog is shown as a transient for toplevel.
 */
void dialog_show_modal_and_selfdestruct(std::unique_ptr<Gtk::Dialog> dialog, Gtk::Root *root = nullptr);

} // namespace Inkscape::UI

#endif // INKSCAPE_UI_DIALOG_RUN_H
