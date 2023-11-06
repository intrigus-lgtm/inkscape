// SPDX-License-Identifier: GPL-2.0-or-later
/* Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Johan Engelen <j.b.c.engelen@ewi.utwente.nl>
 *   Anshudhar Kumar Singh <anshudhar2001@gmail.com>
 *
 * Copyright (C) 1999-2007, 2021 Authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SP_EXPORT_SINGLE_H
#define SP_EXPORT_SINGLE_H

#include <map>
#include <memory>
#include <vector>
#include <glibmm/refptr.h>
#include <gtkmm/box.h>
#include <gtkmm/entry.h>

#include "helper/auto-connection.h"

namespace Gtk {
class Builder;
class Button;
class CheckButton;
class FlowBox;
class Grid;
class Label;
class ProgressBar;
class ToggleButton;
class ScrolledWindow;
class SpinButton;
} // namespace Gtk

class InkscapeApplication;
class SPDesktop;
class SPDocument;
class SPObject;
class SPPage;

namespace Inkscape {

class Selection;
class Preferences;

namespace UI {

namespace Widget {
class UnitMenu;
class ColorPicker;
} // namespace Widget

namespace Dialog {

class PreviewDrawing;
class ExportPreview;
class ExtensionList;

class SingleExport : public Gtk::Box
{
public:
    SingleExport(BaseObjectType *cobject, const Glib::RefPtr<Gtk::Builder> &refGlade);
    ~SingleExport() override;

    void setApp(InkscapeApplication *app) { _app = app; }
    void setDocument(SPDocument *document);
    void setDesktop(SPDesktop *desktop);
    void selectionChanged(Inkscape::Selection *selection);
    void selectionModified(Inkscape::Selection *selection, guint flags);
    void refresh()
    {
        refreshArea();
        refreshPage();
        loadExportHints();
    };

private:
    enum sb_type
    {
        SPIN_X0 = 0,
        SPIN_X1,
        SPIN_Y0,
        SPIN_Y1,
        SPIN_WIDTH,
        SPIN_HEIGHT,
        SPIN_BMWIDTH,
        SPIN_BMHEIGHT,
        SPIN_DPI
    };

    enum selection_mode
    {
        SELECTION_PAGE = 0, // Default is alaways placed first
        SELECTION_SELECTION,
        SELECTION_DRAWING,
        SELECTION_CUSTOM,
    };

    InkscapeApplication *_app = nullptr;
    SPDesktop *_desktop = nullptr;
    SPDocument *_document = nullptr;
    std::shared_ptr<PreviewDrawing> _preview_drawing;

    bool setupDone = false; // To prevent setup() call add connections again.

    std::map<sb_type, Gtk::SpinButton *> spin_buttons;
    std::map<sb_type, Gtk::Label *> spin_labels;
    std::map<selection_mode, Gtk::ToggleButton *> selection_buttons;

    Gtk::CheckButton *show_export_area = nullptr;

    // In order of intialization
    Gtk::FlowBox &pages_list;
    Gtk::ScrolledWindow &pages_list_box;
    Gtk::Grid &size_box;
    Inkscape::UI::Widget::UnitMenu &units;
    Gtk::Box &si_units_row;
    Gtk::CheckButton &si_hide_all;
    Gtk::CheckButton &si_show_preview;

    ExportPreview &preview;
    Gtk::Box &preview_box;

    ExtensionList &si_extension_cb;

    Gtk::Entry &si_filename_entry;
    Gtk::Button &si_export;
    Gtk::ProgressBar &progress_bar;
    Gtk::Widget &progress_box;
    Gtk::Button &cancel_button;

    bool filename_modified = false;
    Glib::ustring original_name;
    Glib::ustring doc_export_name;

    Inkscape::Preferences *prefs = nullptr;
    std::map<selection_mode, Glib::ustring> selection_names;
    selection_mode current_key = (selection_mode)0;

    void setup();
    void setupUnits();
    void setupExtensionList();
    void setupSpinButtons();
    void toggleSpinButtonVisibility();
    void refreshPreview();

    // change range and callbacks to spinbuttons
    template <typename T>
    void setupSpinButton(Gtk::SpinButton *sb, double val, double min, double max, double step, double page, int digits,
                         bool sensitive, void (SingleExport::*cb)(T), T param);

    void setDefaultSelectionMode();
    void onAreaXChange(sb_type type);
    void onAreaYChange(sb_type type);
    void onDpiChange(sb_type type);
    void onAreaTypeToggle(selection_mode key);
    void onUnitChanged();
    void onFilenameModified();
    void onExtensionChanged();
    void onExport();
    void onCancel();
    void onBrowse(Gtk::Entry::IconPosition pos);
    void on_inkscape_selection_modified(Inkscape::Selection *selection, guint flags);
    void on_inkscape_selection_changed(Inkscape::Selection *selection);

    void refreshArea();
    void refreshPage();
    void loadExportHints();
    void saveExportHints(SPObject *target);
    void areaXChange(sb_type type);
    void areaYChange(sb_type type);
    void dpiChange(sb_type type);
    void setArea(double x0, double y0, double x1, double y1);
    void blockSpinConns(bool status);

    void setExporting(bool exporting, Glib::ustring const &text = "");
    /**
     * Callback to be used in for loop to update the progress bar.
     *
     * @param value number between 0 and 1 indicating the fraction of progress (0.17 = 17 % progress)
     */
    static unsigned int onProgressCallback(float value, void *data);

    /**
     * Page functions
     */
    void clearPagePreviews();
    void onPagesChanged();
    void onPagesModified(SPPage *page);
    void onPagesSelected(SPPage *page);
    void setPagesMode(bool multi);
    void selectPage(SPPage *page);
    std::vector<SPPage const *> getSelectedPages() const;

    bool interrupted;

    // Gtk Signals
    std::vector<auto_connection> spinButtonConns;
    auto_connection filenameConn;
    auto_connection extensionConn;
    auto_connection exportConn;
    auto_connection cancelConn;
    auto_connection browseConn;
    auto_connection _pages_list_changed;
    // Document Signals
    auto_connection _page_selected_connection;
    auto_connection _page_modified_connection;
    auto_connection _page_changed_connection;

    std::unique_ptr<Inkscape::UI::Widget::ColorPicker> _bgnd_color_picker;
};

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif // SP_EXPORT_SINGLE_H

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
