// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A simple dialog for creating grid type arrangements of selected objects
 *
 * Authors:
 *   Bob Jamison ( based off trace dialog)
 *   John Cliff
 *   Other dudes from The Inkscape Organization
 *   Abhishek Sharma
 *   Declara Denis
 *
 * Copyright (C) 2004 Bob Jamison
 * Copyright (C) 2004 John Cliff
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

//#define DEBUG_GRID_ARRANGE 1

#include "grid-arrange-tab.h"

#include <algorithm>
#include <iterator>
#include <numeric>
#include <vector>

#include <glibmm/i18n.h>
#include <gtkmm/grid.h>
#include <gtkmm/sizegroup.h>
#include <sigc++/functors/mem_fun.h>

#include <2geom/transforms.h>

#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "inkscape.h"
#include "preferences.h"
#include "selection.h"
#include "object/object-set.h"
#include "ui/icon-names.h"
#include "ui/pack.h"
#include "ui/dialog/tile.h" // for Inkscape::UI::Dialog::ArrangeDialog

/**
 * Sort ObjectSet by an existing grid arrangement
 *
 * This is based on this paper here: DOI:10.1049/iet-ipr.2015.0126
 *
 * @param items - The unsorted object set to sort.
 * @returns a new grid-sorted std::vector of SPItems.
 */
static std::vector<SPItem *> grid_item_sort(Inkscape::ObjectSet *items)
{
    std::vector<SPItem *> results;
    Inkscape::ObjectSet rest;

    // 1. Find middle Y position of the largest top object.
    double box_top = items->visualBounds()->min()[Geom::Y];
    double last_height = 0.0;
    double target = box_top;
    for (auto item : items->items()) {
        if (auto item_box = item->desktopVisualBounds()) {
            if (Geom::are_near(item_box->min()[Geom::Y], box_top, 2.0)) {
                if (item_box->height() > last_height) {
                    last_height = item_box->height();
                    target = item_box->midpoint()[Geom::Y];
                }
            }
        }
    }

    // 2. Loop through all remaining items
    for (auto item : items->items()) {
        // Items without visual bounds are completely ignored.
        if (auto item_box = item->desktopVisualBounds()) {
            auto radius = item_box->height() / 2;
            auto min = item_box->midpoint()[Geom::Y] - radius;
            auto max = item_box->midpoint()[Geom::Y] + radius;

            if (max > target && min < target) {
                // 2a. if the item's radius falls on the Y position above
                results.push_back(item);
            } else {
                // 2b. Save items not in this row for later
                rest.add(item);
            }
        }
    }

    // 3. Sort this single row according to the X position
    std::sort(results.begin(), results.end(), [](SPItem *a, SPItem *b) {
        // These boxes always exist because of the above filtering.
        return (a->desktopVisualBounds()->min()[Geom::X] < b->desktopVisualBounds()->min()[Geom::X]);
    });

    if (results.size() == 0) {
        g_warning("Bad grid detection when sorting items!");
    } else if (!rest.isEmpty()) {
        // 4. If there's any remaining, run this function again.
        auto sorted_rest = grid_item_sort(&rest);
        results.reserve(items->size());
        results.insert(results.end(), sorted_rest.begin(), sorted_rest.end());
    }
    return results;
}

namespace Inkscape::UI::Dialog {

//#########################################################################
//## E V E N T S
//#########################################################################

/*
 *
 * This arranges the selection in a grid pattern.
 *
 */
void GridArrangeTab::arrange()
{
    // check for correct numbers in the row- and col-spinners
    on_col_spinbutton_changed();
    on_row_spinbutton_changed();

    // set padding to manual values
    double paddingx = XPadding.getValue("px");
    double paddingy = YPadding.getValue("px");
    int NoOfCols = NoOfColsSpinner.get_value_as_int();
    int NoOfRows = NoOfRowsSpinner.get_value_as_int();

    SPDesktop *desktop = Parent->getDesktop();
    desktop->getDocument()->ensureUpToDate();
    Inkscape::Selection *selection = desktop->getSelection();
    if (!selection || selection->isEmpty()) return;

    auto sel_box = selection->documentBounds(SPItem::VISUAL_BBOX);
    if (sel_box.empty()) return;
    double grid_left = sel_box->min()[Geom::X];
    double grid_top = sel_box->min()[Geom::Y];

    // require the sorting done before we can calculate row heights etc.
    auto sorted = grid_item_sort(selection);

    // Calculate individual Row and Column sizes if necessary
    auto row_heights = std::vector<double>(NoOfRows, 0.0);
    auto col_widths = std::vector<double>(NoOfCols, 0.0);
    for(int i = 0; i < sorted.size(); i++) {
        if (Geom::OptRect box = sorted[i]->documentVisualBounds()) {
            double width = box->dimensions()[Geom::X];
            double height = box->dimensions()[Geom::Y];
            if (width > col_widths[(i % NoOfCols)]) {
                col_widths[(i % NoOfCols)] = width;
            }
            if (height > row_heights[(i / NoOfCols)]) {
                row_heights[(i / NoOfCols)] = height;
            }
        }
    }

    double col_width = *std::max_element(std::begin(col_widths), std::end(col_widths));
    double row_height = *std::max_element(std::begin(row_heights), std::end(row_heights));

    /// Make sure the top and left of the grid don't move by compensating for align values.
    if (RowHeightButton.get_active()){
        grid_top = grid_top - (((row_height - row_heights[0]) / 2)*(VertAlign));
    }
    if (ColumnWidthButton.get_active()){
        grid_left = grid_left - (((col_width - col_widths[0]) /2)*(HorizAlign));
    }

    // Calculate total widths and heights, allowing for columns and rows non uniformly sized.
    double last_col_padding = 0.0;
    double total_col_width = 0.0;
    if (ColumnWidthButton.get_active()){
        total_col_width = col_width * NoOfCols;
        // Remember the amount of padding the box will lose on the right side
        last_col_padding = (col_width - col_widths[NoOfCols-1]) / 2;
        std::fill(col_widths.begin(), col_widths.end(), col_width);
    } else {
        total_col_width = std::accumulate(col_widths.begin(), col_widths.end(), 0);
    }

    double last_row_padding = 0.0;
    double total_row_height = 0.0;
    if (RowHeightButton.get_active()){
        total_row_height = row_height * NoOfRows;
        // Remember the amount of padding the box will lose on the bottom side
        last_row_padding = (row_height - row_heights[NoOfRows-1]) / 2;
        std::fill(row_heights.begin(), row_heights.end(), row_height);
    } else {
        total_row_height = std::accumulate(row_heights.begin(), row_heights.end(), 0);
    }

    // Fit to bbox, calculate padding between rows accordingly.
    if (SpaceByBBoxRadioButton.get_active()) {
        paddingx = (sel_box->width() - total_col_width + last_col_padding) / (NoOfCols -1);
        paddingy = (sel_box->height() - total_row_height + last_row_padding) / (NoOfRows -1);
    }

    /*
        Horizontal align  - Left    = 0
                            Centre  = 1
                            Right   = 2

        Vertical align    - Top     = 0
                            Middle  = 1
                            Bottom  = 2

       X position is calculated by taking the grids left co-ord, adding the distance to the column,
       then adding 1/2 the spacing multiplied by the align variable above,
       Y position likewise, takes the top of the grid, adds the y to the current row then adds the padding in to align it.
    */

    // Calculate row and column x and y coords required to allow for columns and rows which are non uniformly sized.
    std::vector<double> col_xs(NoOfCols);
    for (int col=1; col < NoOfCols; col++) {
        col_xs[col] = col_widths[col - 1] + paddingx + col_xs[col - 1];
    }

    std::vector<double> row_ys(NoOfRows);
    for (int row=1; row < NoOfRows; row++) {
        row_ys[row] = row_heights[row - 1] + paddingy + row_ys[row - 1];
    }

    int cnt = 0;
    std::vector<SPItem*>::iterator it = sorted.begin();
    for (int row_cnt=0; ((it != sorted.end()) && (row_cnt<NoOfRows)); ++row_cnt) {
         std::vector<SPItem *> current_row;
         int col_cnt = 0;
         for(;it!=sorted.end()&&col_cnt<NoOfCols;++it) {
             current_row.push_back(*it);
             col_cnt++;
         }

         for (auto item:current_row) {
             auto min = Geom::Point(0, 0);
             double width = 0, height = 0;
             if (auto vbox = item->documentVisualBounds()) {
                 width = vbox->dimensions()[Geom::X];
                 height = vbox->dimensions()[Geom::Y];
                 min = vbox->min();
             }

             int row = cnt / NoOfCols;
             int col = cnt % NoOfCols;

             double new_x = grid_left + (((col_widths[col] - width)/2)*HorizAlign) + col_xs[col];
             double new_y = grid_top + (((row_heights[row] - height)/2)*VertAlign) + row_ys[row];

             Geom::Point move = Geom::Point(new_x, new_y) - min;
             Geom::Affine const affine = Geom::Affine(Geom::Translate(move));
             item->set_i2d_affine(item->i2doc_affine() * affine * item->document->doc2dt());
             item->doWriteTransform(item->transform);
             item->updateRepr();
             cnt +=1;
         }
    }

    DocumentUndo::done(desktop->getDocument(), _("Arrange in a grid"), INKSCAPE_ICON("dialog-align-and-distribute"));
}

//#########################################################################
//## E V E N T S
//#########################################################################

/**
 * changed value in # of columns spinbox.
 */
void GridArrangeTab::on_row_spinbutton_changed()
{
    SPDesktop *desktop = Parent->getDesktop();
    Inkscape::Selection *selection = desktop ? desktop->getSelection() : nullptr;
    if (!selection) return;

    int selcount = (int) boost::distance(selection->items());

    double NoOfRows = ceil(selcount / NoOfColsSpinner.get_value());
    NoOfRowsSpinner.set_value(NoOfRows);
}

/**
 * changed value in # of rows spinbox.
 */
void GridArrangeTab::on_col_spinbutton_changed()
{
    SPDesktop *desktop = Parent->getDesktop();
    Inkscape::Selection *selection = desktop ? desktop->getSelection() : nullptr;
    if (!selection) return;

    int selcount = (int) boost::distance(selection->items());

    double NoOfCols = ceil(selcount / NoOfRowsSpinner.get_value());
    NoOfColsSpinner.set_value(NoOfCols);
}

/**
 * changed value in x padding spinbox.
 */
void GridArrangeTab::on_xpad_spinbutton_changed()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setDouble("/dialogs/gridtiler/XPad", XPadding.getValue("px"));

}

/**
 * changed value in y padding spinbox.
 */
void GridArrangeTab::on_ypad_spinbutton_changed()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setDouble("/dialogs/gridtiler/YPad", YPadding.getValue("px"));
}

/**
 * checked/unchecked autosize Rows button.
 */
void GridArrangeTab::on_RowSize_checkbutton_changed()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (RowHeightButton.get_active()) {
        prefs->setDouble("/dialogs/gridtiler/AutoRowSize", 20);
    } else {
        prefs->setDouble("/dialogs/gridtiler/AutoRowSize", -20);
    }
    RowHeightBox.set_sensitive ( !RowHeightButton.get_active());
}

/**
 * checked/unchecked autosize Rows button.
 */
void GridArrangeTab::on_ColSize_checkbutton_changed()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (ColumnWidthButton.get_active()) {
        prefs->setDouble("/dialogs/gridtiler/AutoColSize", 20);
    } else {
        prefs->setDouble("/dialogs/gridtiler/AutoColSize", -20);
    }
    ColumnWidthBox.set_sensitive ( !ColumnWidthButton.get_active());
}

/**
 * changed value in columns spinbox.
 */
void GridArrangeTab::on_rowSize_spinbutton_changed()
{
    // quit if run by the attr_changed listener
    if (updating) {
            return;
        }

    // in turn, prevent listener from responding
    updating = true;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setDouble("/dialogs/gridtiler/RowHeight", RowHeightSpinner.get_value());
    updating=false;

}

/**
 * changed value in rows spinbox.
 */
void GridArrangeTab::on_colSize_spinbutton_changed()
{
    // quit if run by the attr_changed listener
    if (updating) {
            return;
        }

    // in turn, prevent listener from responding
    updating = true;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setDouble("/dialogs/gridtiler/ColWidth", ColumnWidthSpinner.get_value());
    updating=false;

}

/**
 * changed Radio button in Spacing group.
 */
void GridArrangeTab::Spacing_button_changed()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (SpaceManualRadioButton.get_active()) {
        prefs->setDouble("/dialogs/gridtiler/SpacingType", 20);
    } else {
        prefs->setDouble("/dialogs/gridtiler/SpacingType", -20);
    }

    XPadding.set_sensitive ( SpaceManualRadioButton.get_active());
    YPadding.set_sensitive ( SpaceManualRadioButton.get_active());
}

/**
 * changed Anchor selection widget.
 */
void GridArrangeTab::Align_changed()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    VertAlign = AlignmentSelector.getVerticalAlignment();
    prefs->setInt("/dialogs/gridtiler/VertAlign", VertAlign);
    HorizAlign = AlignmentSelector.getHorizontalAlignment();
    prefs->setInt("/dialogs/gridtiler/HorizAlign", HorizAlign);
}

/**
 * Desktop selection changed
 */
void GridArrangeTab::updateSelection()
{
    // quit if run by the attr_changed listener
    if (updating) {
        return;
    }

    // in turn, prevent listener from responding
    updating = true;
    SPDesktop *desktop = Parent->getDesktop();
    Inkscape::Selection *selection = desktop ? desktop->getSelection() : nullptr;
    std::vector<SPItem*> items;
    if (selection) {
        items.insert(items.end(), selection->items().begin(), selection->items().end());
    }

    if (!items.empty()) {
        int selcount = items.size();

        if (NoOfColsSpinner.get_value() > 1 && NoOfRowsSpinner.get_value() > 1){
            // Update the number of rows assuming number of columns wanted remains same.
            double NoOfRows = ceil(selcount / NoOfColsSpinner.get_value());
            NoOfRowsSpinner.set_value(NoOfRows);

            // if the selection has less than the number set for one row, reduce it appropriately
            if (selcount < NoOfColsSpinner.get_value()) {
                double NoOfCols = ceil(selcount / NoOfRowsSpinner.get_value());
                NoOfColsSpinner.set_value(NoOfCols);
            }
        } else {
            double PerRow = ceil(sqrt(selcount));
            double PerCol = ceil(sqrt(selcount));
            NoOfRowsSpinner.set_value(PerRow);
            NoOfColsSpinner.set_value(PerCol);
        }
    }

    updating = false;
}

void GridArrangeTab::setDesktop(SPDesktop *desktop)
{
    _selection_changed_connection.disconnect();

    if (desktop) {
        updateSelection();

        _selection_changed_connection = INKSCAPE.signal_selection_changed.connect(
            sigc::hide<0>(sigc::mem_fun(*this, &GridArrangeTab::updateSelection)));
    }
}

//#########################################################################
//## C O N S T R U C T O R    /    D E S T R U C T O R
//#########################################################################
/**
 * Constructor
 */
GridArrangeTab::GridArrangeTab(ArrangeDialog *parent)
    : Parent(parent),
      XPadding{_("X:"), _("Horizontal spacing between columns"), UNIT_TYPE_LINEAR, "object-columns", &PaddingUnitMenu},
      YPadding(_("Y:"), _("Vertical spacing between rows"), XPadding, "object-rows"),
      PaddingTable(Gtk::make_managed<Gtk::Grid>())
{
     // bool used by spin button callbacks to stop loops where they change each other.
    updating = false;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    auto _col1 = Gtk::SizeGroup::create(Gtk::SizeGroup::Mode::HORIZONTAL);
    auto _col2 = Gtk::SizeGroup::create(Gtk::SizeGroup::Mode::HORIZONTAL);
    auto _col3 = Gtk::SizeGroup::create(Gtk::SizeGroup::Mode::HORIZONTAL);

    Gtk::Box *contents = this;
    set_valign(Gtk::Align::START);

#define MARGIN 2

    //##Set up the panel

    NoOfRowsLabel.set_text_with_mnemonic(_("_Rows:"));
    NoOfRowsLabel.set_mnemonic_widget(NoOfRowsSpinner);
    NoOfRowsBox.set_orientation(Gtk::Orientation::VERTICAL);
    UI::pack_start(NoOfRowsBox, NoOfRowsLabel, false, false, MARGIN);

    NoOfRowsSpinner.set_digits(0);
    NoOfRowsSpinner.set_increments(1, 0);
    NoOfRowsSpinner.set_range(1.0, 10000.0);
    NoOfRowsSpinner.signal_changed().connect(sigc::mem_fun(*this, &GridArrangeTab::on_col_spinbutton_changed));
    NoOfRowsSpinner.set_tooltip_text(_("Number of rows"));
    UI::pack_start(NoOfRowsBox, NoOfRowsSpinner, false, false, MARGIN);
    _col1->add_widget(NoOfRowsBox);

    RowHeightButton.set_label(_("Equal _height"));
    RowHeightButton.set_use_underline(true);
    double AutoRow = prefs->getDouble("/dialogs/gridtiler/AutoRowSize", 15);
    if (AutoRow>0)
         AutoRowSize=true;
    else
         AutoRowSize=false;
    RowHeightButton.set_active(AutoRowSize);

    UI::pack_start(NoOfRowsBox, RowHeightButton, false, false, MARGIN);

    RowHeightButton.set_tooltip_text(_("If not set, each row has the height of the tallest object in it"));
    RowHeightButton.signal_toggled().connect(sigc::mem_fun(*this, &GridArrangeTab::on_RowSize_checkbutton_changed));

    UI::pack_start(SpinsHBox, NoOfRowsBox, false, false, MARGIN);

    /*#### Label for X ####*/
    XByYLabel.set_markup("<span size='larger'> &#215; </span>");
    XByYLabel.set_valign(Gtk::Align::CENTER);
    UI::pack_start(SpinsHBox, XByYLabel, false, false, MARGIN);
    _col2->add_widget(XByYLabel);

    /*#### Number of columns ####*/

    NoOfColsLabel.set_text_with_mnemonic(_("_Columns:"));
    NoOfColsLabel.set_mnemonic_widget(NoOfColsSpinner);
    NoOfColsBox.set_orientation(Gtk::Orientation::VERTICAL);
    UI::pack_start(NoOfColsBox, NoOfColsLabel, false, false, MARGIN);

    NoOfColsSpinner.set_digits(0);
    NoOfColsSpinner.set_increments(1, 0);
    NoOfColsSpinner.set_range(1.0, 10000.0);
    NoOfColsSpinner.signal_changed().connect(sigc::mem_fun(*this, &GridArrangeTab::on_row_spinbutton_changed));
    NoOfColsSpinner.set_tooltip_text(_("Number of columns"));
    UI::pack_start(NoOfColsBox, NoOfColsSpinner, false, false, MARGIN);
    _col3->add_widget(NoOfColsBox);

    ColumnWidthButton.set_label(_("Equal _width"));
    ColumnWidthButton.set_use_underline(true);
    double AutoCol = prefs->getDouble("/dialogs/gridtiler/AutoColSize", 15);
    if (AutoCol>0)
         AutoColSize=true;
    else
         AutoColSize=false;
    ColumnWidthButton.set_active(AutoColSize);
    UI::pack_start(NoOfColsBox, ColumnWidthButton, false, false, MARGIN);

    ColumnWidthButton.set_tooltip_text(_("If not set, each column has the width of the widest object in it"));
    ColumnWidthButton.signal_toggled().connect(sigc::mem_fun(*this, &GridArrangeTab::on_ColSize_checkbutton_changed));

    UI::pack_start(SpinsHBox, NoOfColsBox, false, false, MARGIN);

    TileBox.set_orientation(Gtk::Orientation::VERTICAL);
    UI::pack_start(TileBox, SpinsHBox, false, false, MARGIN);

    VertAlign = prefs->getInt("/dialogs/gridtiler/VertAlign", 1);
    HorizAlign = prefs->getInt("/dialogs/gridtiler/HorizAlign", 1);

    // Anchor selection widget
    AlignLabel.set_markup(_("<b>Alignment:</b>"));
    AlignLabel.set_margin_top(8);
    AlignLabel.set_halign(Gtk::Align::START);
    AlignLabel.set_valign(Gtk::Align::CENTER);
    AlignmentSelector.set_margin_start(16);
    AlignmentSelector.set_halign(Gtk::Align::START);
    AlignmentSelector.setAlignment(HorizAlign, VertAlign);
    AlignmentSelector.connectSelectionChanged(sigc::mem_fun(*this, &GridArrangeTab::Align_changed));
    UI::pack_start(TileBox, AlignLabel, false, false, MARGIN);
    UI::pack_start(TileBox, AlignmentSelector, false, false);

    {
        /*#### Radio buttons to control spacing manually or to fit selection bbox ####*/
        SpaceByBBoxRadioButton.set_label(_("_Fit into selection box"));
        SpaceByBBoxRadioButton.set_use_underline (true);
        SpaceByBBoxRadioButton.signal_toggled().connect(sigc::mem_fun(*this, &GridArrangeTab::Spacing_button_changed));

        UI::pack_start(SpacingVBox, SpaceByBBoxRadioButton, false, false, MARGIN);

        SpaceManualRadioButton.set_label(_("_Set spacing:"));
        SpaceManualRadioButton.set_use_underline (true);
        SpaceManualRadioButton.set_group(SpaceByBBoxRadioButton);
        SpaceManualRadioButton.signal_toggled().connect(sigc::mem_fun(*this, &GridArrangeTab::Spacing_button_changed));
        UI::pack_start(SpacingVBox, SpaceManualRadioButton, false, false, MARGIN);

        UI::pack_start(TileBox, SpacingVBox, false, false, MARGIN);
    }

    {
        /*#### Padding ####*/
        PaddingUnitMenu.setUnitType(UNIT_TYPE_LINEAR);
        PaddingUnitMenu.setUnit("px");

        YPadding.setDigits(5);
        YPadding.setIncrements(0.2, 0);
        YPadding.setRange(-10000, 10000);
        double yPad = prefs->getDouble("/dialogs/gridtiler/YPad", 15);
        YPadding.setValue(yPad, "px");
        YPadding.signal_value_changed().connect(sigc::mem_fun(*this, &GridArrangeTab::on_ypad_spinbutton_changed));

        XPadding.setDigits(5);
        XPadding.setIncrements(0.2, 0);
        XPadding.setRange(-10000, 10000);
        double xPad = prefs->getDouble("/dialogs/gridtiler/XPad", 15);
        XPadding.setValue(xPad, "px");

        XPadding.signal_value_changed().connect(sigc::mem_fun(*this, &GridArrangeTab::on_xpad_spinbutton_changed));
    }

    PaddingTable->set_margin(MARGIN);
    PaddingTable->set_row_spacing(MARGIN);
    PaddingTable->set_column_spacing(MARGIN);
    PaddingTable->attach(XPadding,        0, 0, 1, 1);
    PaddingTable->attach(PaddingUnitMenu, 1, 0, 1, 1);
    PaddingTable->attach(YPadding,        0, 1, 1, 1);

    UI::pack_start(TileBox, *PaddingTable, false, false, MARGIN);

    contents->set_margin(8);
    UI::pack_start(*contents, TileBox);

    double SpacingType = prefs->getDouble("/dialogs/gridtiler/SpacingType", 15);
    if (SpacingType>0) {
        ManualSpacing=true;
    } else {
        ManualSpacing=false;
    }
    SpaceManualRadioButton.set_active(ManualSpacing);
    SpaceByBBoxRadioButton.set_active(!ManualSpacing);
    XPadding.set_sensitive (ManualSpacing);
    YPadding.set_sensitive (ManualSpacing);
}

GridArrangeTab::~GridArrangeTab() {
    setDesktop(nullptr);
}

} // namespace Inkscape::UI::Dialog

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
