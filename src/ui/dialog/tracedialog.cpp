// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Bitmap tracing settings dialog - second implementation.
 */
/* Authors:
 *   Bob Jamison
 *   Marc Jeanmougin <marc.jeanmougin@telecom-paristech.fr>
 *   PBS <pbs3141@gmail.com>
 *   Others - see git history.
 *
 * Copyright (C) 2019-2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "tracedialog.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <glibmm/i18n.h>
#include <gdkmm/general.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/builder.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/eventcontrollerfocus.h>
#include <gtkmm/frame.h>
#include <gtkmm/grid.h>
#include <gtkmm/picture.h>
#include <gtkmm/notebook.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/progressbar.h>
#include <gtkmm/stack.h>

#include "desktop.h"
#include "selection.h"
#include "trace/autotrace/inkscape-autotrace.h"
#include "trace/depixelize/inkscape-depixelize.h"
#include "trace/potrace/inkscape-potrace.h"
#include "ui/builder-utils.h"
#include "ui/util.h"
#include "ui/widget/bin.h"

namespace Inkscape::UI::Dialog {
namespace {

constexpr auto cbt_ss_map = std::to_array({
    Trace::Potrace::TraceType::BRIGHTNESS,
    Trace::Potrace::TraceType::CANNY,
    Trace::Potrace::TraceType::QUANT,
    Trace::Potrace::TraceType::AUTOTRACE_SINGLE,
    Trace::Potrace::TraceType::AUTOTRACE_CENTERLINE
});

constexpr auto cbt_ms_map = std::to_array({
    Trace::Potrace::TraceType::BRIGHTNESS_MULTI,
    Trace::Potrace::TraceType::QUANT_COLOR,
    Trace::Potrace::TraceType::QUANT_MONO,
    Trace::Potrace::TraceType::AUTOTRACE_MULTI
});

enum class EngineType
{
    Potrace,
    Autotrace,
    Depixelize
};

struct TraceData
{
    std::unique_ptr<Trace::TracingEngine> engine;
    bool sioxEnabled;
};

class TraceDialogImpl : public TraceDialog
{
public:
    TraceDialogImpl();
    ~TraceDialogImpl() override;

protected:
    void selectionModified(Selection *selection, unsigned flags) override;
    void selectionChanged(Selection *selection) override;

private:
    TraceData getTraceData() const;
    void setDefaults();
    void adjustParamsVisible();
    void onTraceClicked();
    void onAbortClicked();
    bool previewsEnabled() const;
    void schedulePreviewUpdate(int msecs, bool force = false);
    void updatePreview(bool force = false);
    void launchPreviewGeneration();

    // Handles to ongoing asynchronous computations.
    Trace::TraceFuture trace_future;
    Trace::TraceFuture preview_future;

    // Delayed preview generation.
    sigc::connection preview_timeout_conn;
    bool preview_pending_recompute = false;

    Glib::RefPtr<Gtk::Builder> builder;
    UI::Widget::Bin bin;
    Glib::RefPtr<Gtk::Adjustment> MS_scans, PA_curves, PA_islands, PA_sparse1, PA_sparse2;
    Glib::RefPtr<Gtk::Adjustment> SS_AT_ET_T, SS_AT_FI_T, SS_BC_T, SS_CQ_T, SS_ED_T;
    Glib::RefPtr<Gtk::Adjustment> optimize, smooth, speckles;
    Gtk::DropDown &CBT_SS, &CBT_MS;
    Gtk::CheckButton &CB_invert, &CB_MS_smooth, &CB_MS_stack, &CB_MS_rb;
    Gtk::CheckButton &CB_speckles,  &CB_smooth,  &CB_optimize,  &CB_SIOX;
    Gtk::CheckButton &CB_speckles1, &CB_smooth1, &CB_optimize1, &CB_SIOX1, &CB_PA_optimize;
    Gtk::CheckButton &RB_PA_voronoi;
    Gtk::Button &B_RESET, &B_STOP, &B_OK, &B_Update;
    Gtk::Box &mainBox;
    Gtk::Notebook &choice_tab;
    Gtk::Picture &previewArea;
    Gtk::Box &orient_box;
    Gtk::Frame &_preview_frame;
    Gtk::Grid &_param_grid;
    Gtk::CheckButton &_live_preview;
    Gtk::Stack &stack;
    Gtk::ProgressBar &progressbar;
    Gtk::Box &boxchild1, &boxchild2;
};

enum class Page
{
    SingleScan,
    MultiScan,
    PixelArt
};

TraceData TraceDialogImpl::getTraceData() const
{
    auto current_page = static_cast<Page>(choice_tab.get_current_page());

    auto &cb_siox = current_page == Page::SingleScan ? CB_SIOX : CB_SIOX1;
    bool enable_siox = cb_siox.get_active();

    auto trace_type = current_page == Page::SingleScan
                    ? cbt_ss_map.at(CBT_SS.get_selected())
                    : cbt_ms_map.at(CBT_MS.get_selected());

    EngineType engine_type;
    if (current_page == Page::PixelArt) {
        engine_type = EngineType::Depixelize;
    } else {
        switch (trace_type) {
            case Inkscape::Trace::Potrace::TraceType::AUTOTRACE_SINGLE:
            case Inkscape::Trace::Potrace::TraceType::AUTOTRACE_CENTERLINE:
            case Inkscape::Trace::Potrace::TraceType::AUTOTRACE_MULTI:
                engine_type = EngineType::Autotrace;
                break;
            default:
                engine_type = EngineType::Potrace;
                break;
        }
    }

    auto setup_potrace = [&, this] {
        auto eng = std::make_unique<Trace::Potrace::PotraceTracingEngine>(
            trace_type, CB_invert.get_active(), (int)SS_CQ_T->get_value(), SS_BC_T->get_value(),
            0, // Brightness floor
            SS_ED_T->get_value(), (int)MS_scans->get_value(), CB_MS_stack.get_active(), CB_MS_smooth.get_active(),
            CB_MS_rb.get_active());

        auto &cb_optimize = current_page == Page::SingleScan ? CB_optimize : CB_optimize1;
        eng->setOptiCurve(cb_optimize.get_active());
        eng->setOptTolerance(optimize->get_value());

        auto &cb_smooth = current_page == Page::SingleScan ? CB_smooth : CB_smooth1;
        eng->setAlphaMax(cb_smooth.get_active() ? smooth->get_value() : 0);

        auto &cb_speckles = current_page == Page::SingleScan ? CB_speckles : CB_speckles1;
        eng->setTurdSize(cb_speckles.get_active() ? (int)speckles->get_value() : 0);

        return eng;
    };

    auto setup_autotrace = [&, this] {
        auto eng = std::make_unique<Trace::Autotrace::AutotraceTracingEngine>();

        switch (trace_type) {
            case Inkscape::Trace::Potrace::TraceType::AUTOTRACE_SINGLE:
                eng->setColorCount(2);
                break;
            case Inkscape::Trace::Potrace::TraceType::AUTOTRACE_CENTERLINE:
                eng->setColorCount(2);
                eng->setCenterLine(true);
                eng->setPreserveWidth(true);
                break;
            case Inkscape::Trace::Potrace::TraceType::AUTOTRACE_MULTI:
                eng->setColorCount((int)MS_scans->get_value() + 1);
                break;
            default:
                assert(false);
                break;
        }

        eng->setFilterIterations((int)SS_AT_FI_T->get_value());
        eng->setErrorThreshold(SS_AT_ET_T->get_value());

        return eng;
    };

    auto setup_depixelize = [this] {
        return std::make_unique<Trace::Depixelize::DepixelizeTracingEngine>(
            RB_PA_voronoi.get_active() ? Inkscape::Trace::Depixelize::TraceType::VORONOI : Inkscape::Trace::Depixelize::TraceType::BSPLINES,
            PA_curves->get_value(), (int) PA_islands->get_value(),
            (int) PA_sparse1->get_value(), PA_sparse2->get_value(),
            CB_PA_optimize.get_active());
    };

    TraceData data;
    switch (engine_type) {
        case EngineType::Potrace:    data.engine = setup_potrace();    break;
        case EngineType::Autotrace:  data.engine = setup_autotrace();  break;
        case EngineType::Depixelize: data.engine = setup_depixelize(); break;
        default: assert(false); break;
    }
    data.sioxEnabled = enable_siox;

    return data;
}

void TraceDialogImpl::selectionChanged(Inkscape::Selection *selection)
{
    updatePreview();
}

void TraceDialogImpl::selectionModified(Selection *selection, unsigned flags)
{
    auto mask = SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_PARENT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG;
    if ((flags & mask) == mask) {
        // All flags set - preview instantly.
        updatePreview();
    } else if (flags & mask) {
        // At least one flag set - preview after a long delay.
        schedulePreviewUpdate(1000);
    }
}

void TraceDialogImpl::setDefaults()
{
    MS_scans->set_value(8);
    PA_curves->set_value(1);
    PA_islands->set_value(5);
    PA_sparse1->set_value(4);
    PA_sparse2->set_value(1);
    SS_AT_FI_T->set_value(4);
    SS_AT_ET_T->set_value(2);
    SS_BC_T->set_value(0.45);
    SS_CQ_T->set_value(64);
    SS_ED_T->set_value(.65);
    optimize->set_value(0.2);
    smooth->set_value(1);
    speckles->set_value(2);
    CB_invert.set_active(false);
    CB_MS_smooth.set_active(true);
    CB_MS_stack.set_active(true);
    CB_MS_rb.set_active(false);
    CB_speckles.set_active(true);
    CB_smooth.set_active(true);
    CB_optimize.set_active(true);
    CB_speckles1.set_active(true);
    CB_smooth1.set_active(true);
    CB_optimize1.set_active(true);
    CB_PA_optimize.set_active(false);
    CB_SIOX.set_active(false);
    CB_SIOX1.set_active(false);
}

void TraceDialogImpl::onAbortClicked()
{
    if (!trace_future) {
        // Not tracing; nothing to cancel.
        return;
    }

    stack.set_visible_child(boxchild1);
    if (auto desktop = getDesktop()) desktop->clearWaitingCursor();
    trace_future.cancel();
}

void TraceDialogImpl::onTraceClicked()
{
    if (trace_future) {
        // Still tracing; wait for either finished or cancelled.
        return;
    }

    // Attempt to fire off the tracer.
    auto data = getTraceData();
    trace_future = Trace::trace(std::move(data.engine), data.sioxEnabled,
        // On progress:
        [this] (double progress) {
            progressbar.set_fraction(progress);
        },
        // On completion without cancelling:
        [this] {
            progressbar.set_fraction(1.0);
            stack.set_visible_child(boxchild1);
            if (auto desktop = getDesktop()) desktop->clearWaitingCursor();
            trace_future.cancel();
        }
    );

    if (trace_future) {
        // Put the UI into the tracing state.
        if (auto desktop = getDesktop()) desktop->setWaitingCursor();
        stack.set_visible_child(boxchild2);
        progressbar.set_fraction(0.0);
    }
}

using Inkscape::UI::create_builder;
using Inkscape::UI::get_widget;

TraceDialogImpl::TraceDialogImpl()
  : builder(create_builder("dialog-trace.glade"))
    // Adjustment
  , MS_scans       (get_object<Gtk::Adjustment>(builder,              "MS_scans"))
  , PA_curves      (get_object<Gtk::Adjustment>(builder,             "PA_curves"))
  , PA_islands     (get_object<Gtk::Adjustment>(builder,            "PA_islands"))
  , PA_sparse1     (get_object<Gtk::Adjustment>(builder,            "PA_sparse1"))
  , PA_sparse2     (get_object<Gtk::Adjustment>(builder,            "PA_sparse2"))
  , SS_AT_FI_T     (get_object<Gtk::Adjustment>(builder,            "SS_AT_FI_T"))
  , SS_AT_ET_T     (get_object<Gtk::Adjustment>(builder,            "SS_AT_ET_T"))
  , SS_BC_T        (get_object<Gtk::Adjustment>(builder,               "SS_BC_T"))
  , SS_CQ_T        (get_object<Gtk::Adjustment>(builder,               "SS_CQ_T"))
  , SS_ED_T        (get_object<Gtk::Adjustment>(builder,               "SS_ED_T"))
  , optimize       (get_object<Gtk::Adjustment>(builder,              "optimize"))
  , smooth         (get_object<Gtk::Adjustment>(builder,                "smooth"))
  , speckles       (get_object<Gtk::Adjustment>(builder,              "speckles"))
    // ComboBoxText
  , CBT_SS         (get_widget<Gtk::DropDown>    (builder,              "CBT_SS"))
  , CBT_MS         (get_widget<Gtk::DropDown>    (builder,              "CBT_MS"))
    // CheckButton
  , CB_invert      (get_widget<Gtk::CheckButton> (builder,           "CB_invert"))
  , CB_MS_smooth   (get_widget<Gtk::CheckButton> (builder,        "CB_MS_smooth"))
  , CB_MS_stack    (get_widget<Gtk::CheckButton> (builder,         "CB_MS_stack"))
  , CB_MS_rb       (get_widget<Gtk::CheckButton> (builder,            "CB_MS_rb"))
  , CB_speckles    (get_widget<Gtk::CheckButton> (builder,         "CB_speckles"))
  , CB_smooth      (get_widget<Gtk::CheckButton> (builder,           "CB_smooth"))
  , CB_optimize    (get_widget<Gtk::CheckButton> (builder,         "CB_optimize"))
  , CB_SIOX        (get_widget<Gtk::CheckButton> (builder,             "CB_SIOX"))
  , CB_speckles1   (get_widget<Gtk::CheckButton> (builder,        "CB_speckles1"))
  , CB_smooth1     (get_widget<Gtk::CheckButton> (builder,          "CB_smooth1"))
  , CB_optimize1   (get_widget<Gtk::CheckButton> (builder,        "CB_optimize1"))
  , CB_SIOX1       (get_widget<Gtk::CheckButton> (builder,            "CB_SIOX1"))
  , CB_PA_optimize (get_widget<Gtk::CheckButton> (builder,      "CB_PA_optimize"))
    // RadioButton
  , RB_PA_voronoi  (get_widget<Gtk::CheckButton> (builder,       "RB_PA_voronoi"))
    // Button
  , B_RESET        (get_widget<Gtk::Button>      (builder,             "B_RESET"))
  , B_STOP         (get_widget<Gtk::Button>      (builder,              "B_STOP"))
  , B_OK           (get_widget<Gtk::Button>      (builder,                "B_OK"))
  , B_Update       (get_widget<Gtk::Button>      (builder,            "B_Update"))
    // Box
  , mainBox        (get_widget<Gtk::Box>         (builder,             "mainBox"))
  , choice_tab     (get_widget<Gtk::Notebook>    (builder,          "choice_tab"))
  , previewArea    (get_widget<Gtk::Picture>     (builder,         "previewArea"))
  , orient_box     (get_widget<Gtk::Box>         (builder,          "orient_box"))
  , _preview_frame (get_widget<Gtk::Frame>       (builder,      "_preview_frame"))
  , _param_grid    (get_widget<Gtk::Grid>        (builder,         "_param_grid"))
  , _live_preview  (get_widget<Gtk::CheckButton> (builder,       "_live_preview"))
  , stack          (get_widget<Gtk::Stack>       (builder,               "stack"))
  , progressbar    (get_widget<Gtk::ProgressBar> (builder,         "progressbar"))
  , boxchild1      (get_widget<Gtk::Box>         (builder,           "boxchild1"))
  , boxchild2      (get_widget<Gtk::Box>         (builder,           "boxchild2"))
{
    append(bin);
    bin.set_child(mainBox);
    bin.set_expand(true);

    auto prefs = Preferences::get();

    _live_preview.set_active(prefs->getBool(getPrefsPath() + "liveUpdate", true));

    B_Update.signal_clicked().connect([this] { updatePreview(true); });
    B_OK.signal_clicked().connect(sigc::mem_fun(*this, &TraceDialogImpl::onTraceClicked));
    B_STOP.signal_clicked().connect(sigc::mem_fun(*this, &TraceDialogImpl::onAbortClicked));
    B_RESET.signal_clicked().connect(sigc::mem_fun(*this, &TraceDialogImpl::setDefaults));

    // attempt at making UI responsive: relocate preview to the right or bottom of dialog depending on dialog size
    bin.connectBeforeResize([this] (int width, int height, int baseline) {
        // skip bogus sizes
        if (width >= 10 && height >= 10) {
            // ratio: is dialog wide or is it tall?
            double const ratio = width / static_cast<double>(height);
            // g_warning("size alloc: %d x %d - %f", alloc.get_width(), alloc.get_height(), ratio);
            constexpr double hysteresis = 0.01;
            if (ratio < 1 - hysteresis) {
                // narrow/tall
                choice_tab.set_valign(Gtk::Align::START);
                orient_box.set_orientation(Gtk::Orientation::VERTICAL);
            } else if (ratio > 1 + hysteresis) {
                // wide/short
                orient_box.set_orientation(Gtk::Orientation::HORIZONTAL);
                choice_tab.set_valign(Gtk::Align::FILL);
            }
        }
    });

    CBT_SS.property_selected().signal_changed().connect([this] { adjustParamsVisible(); });
    adjustParamsVisible();

    // watch for changes, but only in params that can impact preview bitmap
    for (auto adj : {SS_BC_T, SS_ED_T, SS_CQ_T, SS_AT_FI_T, SS_AT_ET_T, /* optimize, smooth, speckles,*/ MS_scans, PA_curves, PA_islands, PA_sparse1, PA_sparse2 }) {
        adj->signal_value_changed().connect([this] { updatePreview(); });
    }
    for (auto checkbtn : {&CB_invert, &CB_MS_rb, /* CB_MS_smooth, CB_MS_stack, CB_optimize1, CB_optimize, */ &CB_PA_optimize, &CB_SIOX1, &CB_SIOX, /* CB_smooth1, CB_smooth, CB_speckles1, CB_speckles, */ &_live_preview}) {
        checkbtn->signal_toggled().connect([this] { updatePreview(); });
    }
    for (auto combo : {&CBT_SS, &CBT_MS}) {
        combo->property_selected().signal_changed().connect([this] { updatePreview(); });
    }
    choice_tab.signal_switch_page().connect([this] (Gtk::Widget *, unsigned) { updatePreview(); });

    auto focus = Gtk::EventControllerFocus::create();
    focus->set_propagation_phase(Gtk::PropagationPhase::BUBBLE);
    add_controller(focus);
    focus->signal_enter().connect([this] { updatePreview(); });
}

TraceDialogImpl::~TraceDialogImpl()
{
    Inkscape::Preferences* prefs = Inkscape::Preferences::get();
    prefs->setBool(getPrefsPath() + "liveUpdate", _live_preview.get_active());
    preview_timeout_conn.disconnect();
}

bool TraceDialogImpl::previewsEnabled() const
{
    return _live_preview.get_active() && is_widget_effectively_visible(this);
}

void TraceDialogImpl::schedulePreviewUpdate(int msecs, bool force)
{
    if (!previewsEnabled() && !force) {
        return;
    }

    // Restart timeout.
    preview_timeout_conn.disconnect();
    preview_timeout_conn = Glib::signal_timeout().connect([this] {
        updatePreview(true);
        return false;
    }, msecs);
}

void TraceDialogImpl::updatePreview(bool force)
{
    if (!previewsEnabled() && !force) {
        return;
    }

    preview_timeout_conn.disconnect();

    if (preview_future) {
        // Preview generation already running - flag for recomputation when finished.
        preview_pending_recompute = true;
        return;
    }

    preview_pending_recompute = false;

    auto data = getTraceData();
    preview_future = Trace::preview(std::move(data.engine), data.sioxEnabled,
        // On completion:
        [this] (Glib::RefPtr<Gdk::Pixbuf> result) {
            previewArea.set_paintable(Gdk::Texture::create_for_pixbuf(result));
            preview_future.cancel();

            // Recompute if invalidated during computation.
            if (preview_pending_recompute) {
                updatePreview();
            }
        }
    );

    if (!preview_future) {
        // On instant failure:
        previewArea.set_paintable({});
    }
}

void TraceDialogImpl::adjustParamsVisible()
{
    int constexpr start_row = 2;
    int option = CBT_SS.get_selected();
    if (option >= 3) option = 3;
    int show1 = start_row + option;
    int show2 = show1;
    if (option == 3) ++show2;

    for (int row = start_row; row < start_row + 5; ++row) {
        for (int col = 0; col < 4; ++col) {
            if (auto widget = _param_grid.get_child_at(col, row)) {
                if (row == show1 || row == show2) {
                    widget->set_visible(true);
                } else {
                    widget->set_visible(false);
                }
            }
        }
    }
}

} // namespace

std::unique_ptr<TraceDialog> TraceDialog::create()
{
    return std::make_unique<TraceDialogImpl>();
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
