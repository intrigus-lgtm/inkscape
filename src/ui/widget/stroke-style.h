// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Widgets used in the stroke style dialog.
 */
/* Author:
 *   Lauris Kaplinski <lauris@ximian.com>
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright (C) 2010 Jon A. Cruz
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_DIALOGS_STROKE_STYLE_H
#define SEEN_DIALOGS_STROKE_STYLE_H

#include <vector>
#include <glibmm/refptr.h>
#include <gtkmm/box.h>
#include <gtkmm/togglebutton.h>

#include "style.h"
#include "ui/widget/spinbutton.h"

namespace Gtk {
class Adjustment;
class Entry;
class Grid;
class Label;
} // namespace Gtk

class SPDocument;
class SPObject;
SPObject *getMarkerObj(gchar const *n, SPDocument *doc);

namespace Inkscape {

namespace Util {
class Unit;
} // namespace Util

namespace UI::Widget {

class DashSelector;
class MarkerComboBox;
class UnitMenu;

class StrokeStyleButton;

class StrokeStyle : public Gtk::Box
{
public:
    StrokeStyle();
    ~StrokeStyle() override;
    void setDesktop(SPDesktop *desktop);
    void updateLine();
    void selectionModifiedCB(guint flags);
    void selectionChangedCB();
private:
    /** List of valid types for the stroke-style radio check-button widget */
    enum StrokeStyleButtonType {
        STROKE_STYLE_BUTTON_JOIN, ///< A button to set the line-join style
        STROKE_STYLE_BUTTON_CAP,  ///< A button to set the line-cap style
        STROKE_STYLE_BUTTON_ORDER ///< A button to set the paint-order style
    };

    /**
     * A custom radio check-button for setting the stroke style.  It can be configured
     * to set either the join or cap style by setting the button_type field.
     */
    class StrokeStyleButton : public Gtk::ToggleButton {
        public:
            StrokeStyleButton(Gtk::ToggleButton    *&grp,
                              char const            *icon,
                              StrokeStyleButtonType  button_type,
                              gchar const           *stroke_style);

            /** Get the type (line/cap) of the stroke-style button */
            inline StrokeStyleButtonType get_button_type() {return button_type;}

            /** Get the stroke style attribute associated with the button */
            inline gchar const * get_stroke_style() {return stroke_style;}

        private:
            StrokeStyleButtonType button_type; ///< The type (line/cap) of the button
            gchar const *stroke_style;         ///< The stroke style associated with the button
    };

    std::vector<double> getDashFromStyle(SPStyle *style, double &offset);

    void updateAllMarkers(std::vector<SPItem*> const &objects, bool skip_undo = false);
    void setDashSelectorFromStyle(DashSelector *dsel, SPStyle *style);
    void setJoinType (unsigned const jointype);
    void setCapType (unsigned const captype);
    void setPaintOrder (gchar const *paint_order);
    void setJoinButtons(Gtk::ToggleButton *active);
    void setCapButtons(Gtk::ToggleButton *active);
    void setPaintOrderButtons(Gtk::ToggleButton *active);
    void setStrokeWidth();
    void setStrokeDash();
    void setStrokeMiter();
    void setScaledDash(SPCSSAttr *css, int ndash, const double *dash, double offset, double scale);
    bool isHairlineSelected() const;

    StrokeStyleButton * makeRadioButton(Gtk::ToggleButton    *&grp,
                                        char const            *icon,
                                        Gtk::Box              *hb,
                                        StrokeStyleButtonType  button_type,
                                        gchar const           *stroke_style);

    // Callback functions
    void unitChangedCB();
    bool areMarkersBeingUpdated();
    void markerSelectCB(MarkerComboBox *marker_combo, SPMarkerLoc const which);
    static void buttonToggledCB(StrokeStyleButton *tb, StrokeStyle *spw);


    MarkerComboBox *startMarkerCombo;
    MarkerComboBox *midMarkerCombo;
    MarkerComboBox *endMarkerCombo;
    Gtk::Grid *table;
    Glib::RefPtr<Gtk::Adjustment> widthAdj;
    Glib::RefPtr<Gtk::Adjustment> miterLimitAdj;
    SpinButton *miterLimitSpin;
    SpinButton *widthSpin;
    UnitMenu *unitSelector;
    //Gtk::ToggleButton *hairline;
    StrokeStyleButton *joinMiter;
    StrokeStyleButton *joinRound;
    StrokeStyleButton *joinBevel;
    StrokeStyleButton *capButt;
    StrokeStyleButton *capRound;
    StrokeStyleButton *capSquare;
    StrokeStyleButton *paintOrderFSM;
    StrokeStyleButton *paintOrderSFM;
    StrokeStyleButton *paintOrderFMS;
    StrokeStyleButton *paintOrderMFS;
    StrokeStyleButton *paintOrderSMF;
    StrokeStyleButton *paintOrderMSF;
    DashSelector *dashSelector;
    Gtk::Entry* _pattern_entry = nullptr;
    Gtk::Label* _pattern_label = nullptr;
    void update_dash_entry(const std::vector<double> &dash_pattern);
    bool _editing_dash_pattern = false;

    gboolean update;
    double _last_width = 0.0;
    SPDesktop *desktop;
    sigc::connection startMarkerConn;
    sigc::connection midMarkerConn;
    sigc::connection endMarkerConn;
    
    Inkscape::Util::Unit const *_old_unit;

    void _handleDocumentReplaced(SPDesktop *, SPDocument *);
    void enterEditMarkerMode(SPMarkerLoc editMarkerMode);
    sigc::connection _document_replaced_connection;
};

} // namespace UI::Widget

} // namespace Inkscape

#endif // SEEN_DIALOGS_STROKE_STYLE_H

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
