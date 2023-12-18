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

#ifndef SP_EXPORT_BATCH_H
#define SP_EXPORT_BATCH_H

#include <map>
#include <memory>
#include <string>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/entry.h>
#include <gtkmm/enums.h>
#include <gtkmm/flowboxchild.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/checkbutton.h>

#include "helper/auto-connection.h"
#include "ui/widget/export-preview.h"

namespace Gtk {
class Builder;
class Button;
class FlowBox;
class ProgressBar;
class Widget;
} // namespace Gtk

class InkscapeApplication;
class SPDesktop;
class SPDocument;
class SPItem;
class SPPage;

namespace Inkscape {

class Preferences;
class Selection;

namespace UI {

namespace Widget {
class ColorPicker;
} // namespace Widget

namespace Dialog {

class ExportList;

class BatchItem final : public Gtk::FlowBoxChild
{
public:
    BatchItem(SPItem *item, std::shared_ptr<PreviewDrawing> drawing);
    BatchItem(SPPage *page, std::shared_ptr<PreviewDrawing> drawing);
    ~BatchItem() final;

    Glib::ustring getLabel() const { return _label_str; }
    SPItem *getItem() const { return _item; }
    SPPage *getPage() const { return _page; }
    void refresh(bool hide, guint32 bg_color);
    void setDrawing(std::shared_ptr<PreviewDrawing> drawing);

    auto get_radio_group() { return &_option; }
    void on_parent_changed();
    void on_mode_changed(Gtk::SelectionMode mode);
    void set_selected(bool selected);
    void update_selected();

private:
    void init(std::shared_ptr<PreviewDrawing> drawing);
    void update_label();

    Glib::ustring _label_str;
    Gtk::Grid _grid;
    Gtk::Label _label;
    Gtk::CheckButton _selector;
    Gtk::CheckButton _option;
    ExportPreview _preview;
    SPItem *_item = nullptr;
    SPPage *_page = nullptr;
    bool is_hide = false;

    auto_connection _selection_widget_changed_conn;
    auto_connection _object_modified_conn;
};

class BatchExport final : public Gtk::Box
{
public:
    BatchExport(BaseObjectType *cobject, const Glib::RefPtr<Gtk::Builder>& builder);
    ~BatchExport() final;

    void setApp(InkscapeApplication *app) { _app = app; }
    void setDocument(SPDocument *document);
    void setDesktop(SPDesktop *desktop);
    void selectionChanged(Inkscape::Selection *selection);
    void selectionModified(Inkscape::Selection *selection, guint flags);
    void pagesChanged();
    void queueRefreshItems();
    void queueRefresh(bool rename_file = false);

private:
    enum selection_mode
    {
        SELECTION_LAYER = 0, // Default is alaways placed first
        SELECTION_SELECTION,
        SELECTION_PAGE,
    };

    InkscapeApplication *_app;
    SPDesktop *_desktop = nullptr;
    SPDocument *_document = nullptr;
    std::shared_ptr<PreviewDrawing> _preview_drawing;
    bool setupDone = false; // To prevent setup() call add connections again.

    std::map<selection_mode, Gtk::ToggleButton *> selection_buttons;
    Gtk::FlowBox &preview_container;
    Gtk::CheckButton &show_preview;
    Gtk::CheckButton &overwrite;
    Gtk::Label &num_elements;
    Gtk::CheckButton &hide_all;
    Gtk::Button &path_chooser;
    Gtk::Entry &name_text;
    Gtk::Button &export_btn;
    Gtk::Button &cancel_btn;
    Gtk::ProgressBar &_prog;
    Gtk::ProgressBar &_prog_batch;
    ExportList &export_list;
    Gtk::Box &progress_box;

    // Store all items to be displayed in flowbox
    std::map<std::string, std::unique_ptr<BatchItem>> current_items;

    std::string original_name;

    Inkscape::Preferences *prefs = nullptr;
    std::map<selection_mode, Glib::ustring> selection_names;
    selection_mode current_key;

    // initialise variables from builder
    void initialise(const Glib::RefPtr<Gtk::Builder> &builder);
    void setup();
    void setDefaultSelectionMode();
    void onAreaTypeToggle(selection_mode key);
    void onExport();
    void onCancel();

    void refreshPreview();
    void refreshItems();
    void loadExportHints(bool rename_file);
    void pickBatchPath();

    Glib::ustring getBatchPath() const;
    void setBatchPath(Glib::ustring const &path);
    Glib::ustring getBatchName(bool fallback) const;
    void setBatchName(Glib::ustring const &name);
    void setExporting(bool exporting, Glib::ustring const &text = "", Glib::ustring const &test_batch = "");

    static unsigned int onProgressCallback(float value, void *);

    bool interrupted;

    // Gtk Signals
    auto_connection filename_conn;
    auto_connection export_conn;
    auto_connection cancel_conn;
    auto_connection browse_conn;
    auto_connection refresh_conn;
    auto_connection refresh_items_conn;
    // SVG Signals
    auto_connection _pages_changed_connection;

    std::unique_ptr<Inkscape::UI::Widget::ColorPicker> _bgnd_color_picker;
};

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif // SP_EXPORT_BATCH_H

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
