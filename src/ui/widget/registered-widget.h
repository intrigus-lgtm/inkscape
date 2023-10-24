// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Ralf Stephan <ralf@ark.in-berlin.de>
 *   Johan Engelen <j.b.c.engelen@utwente.nl>
 *   Abhishek Sharma
 *
 * Copyright (C) 2005-2008 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 *
 *
 * Used by Live Path Effects (see src/live_effects/parameter/) and Document Properties dialog.
 *
 */

#ifndef SEEN_INKSCAPE_UI_WIDGET_REGISTERED_WIDGET_H
#define SEEN_INKSCAPE_UI_WIDGET_REGISTERED_WIDGET_H

#include <utility>
#include <vector>
#include <glibmm/ustring.h>
#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/togglebutton.h>
#include <2geom/affine.h>

#include "desktop.h"
#include "document.h"
#include "document-undo.h"
#include "registry.h"

#include "helper/auto-connection.h"
#include "object/sp-namedview.h"
#include "ui/widget/color-picker.h"
#include "ui/widget/font-button.h"
#include "ui/widget/point.h"
#include "ui/widget/random.h"
#include "ui/widget/scalar-unit.h"
#include "ui/widget/scalar.h"
#include "ui/widget/text.h"
#include "ui/widget/unit-menu.h"
#include "xml/node.h"

class SPDocument;

namespace Inkscape::UI::Widget {

class Registry;

template <class W>
class RegisteredWidget : public W {
public:
    void set_undo_parameters(Glib::ustring _event_description, Glib::ustring _icon_name)
    {
        icon_name = std::move(_icon_name);
        event_description = std::move(_event_description);
        write_undo = true;
    }

    void set_xml_target(Inkscape::XML::Node *xml_node, SPDocument *document)
    {
        repr = xml_node;
        doc = document;
    }

    bool is_updating() const { return _wr && _wr->isUpdating(); }

protected:
    template <typename ...Args>
    RegisteredWidget(Args &&...args)
    : W{std::forward<Args>(args)...}
    {
    }

    void init_parent(Glib::ustring const &key, Registry &wr,
                     Inkscape::XML::Node * const repr_in, SPDocument * const doc_in)
    {
        _wr = &wr;
        _key = key;
        repr = repr_in;
        doc = doc_in;
        if (repr && !doc)  // doc cannot be NULL when repr is not NULL
            g_warning("Initialization of registered widget using defined repr but with doc==NULL");
    }

    void write_to_xml(const char * const svgstr)
    {
        // Use local repr here. When repr is specified, use that one, but
        // if repr==NULL, get the repr of namedview of active desktop.
        Inkscape::XML::Node *local_repr = repr;
        SPDocument *local_doc = doc;
        if (!local_repr) {
            SPDesktop* dt = _wr->desktop();
            if (!dt) {
                return;
            }
            local_repr = reinterpret_cast<SPObject *>(dt->getNamedView())->getRepr();
            local_doc = dt->getDocument();
        }
        const char * svgstr_old = local_repr->attribute(_key.c_str());
        {
            DocumentUndo::ScopedInsensitive _no_undo(local_doc);
            if (!write_undo) {
                local_repr->setAttribute(_key, svgstr);
            }
        }
        if (svgstr_old && svgstr && strcmp(svgstr_old,svgstr)) {
            local_doc->setModifiedSinceSave();
        }

        if (write_undo) {
            local_repr->setAttribute(_key, svgstr);
            DocumentUndo::done(local_doc, event_description, icon_name);
        }
    }

    Registry * _wr = nullptr;
    Glib::ustring _key;
    Inkscape::XML::Node * repr = nullptr;
    SPDocument * doc = nullptr;
    Glib::ustring event_description;
    Glib::ustring icon_name; // Used by History dialog.
    bool write_undo = false;
};

//#######################################################

class RegisteredCheckButton : public RegisteredWidget<Gtk::CheckButton> {
public:
    RegisteredCheckButton(Glib::ustring const &label, Glib::ustring const &tip,
                          Glib::ustring const &key, Registry &wr, bool right = false,
                          Inkscape::XML::Node *repr_in = nullptr, SPDocument *doc_in = nullptr,
                          char const *active_str = "true", char const *inactive_str = "false");

    void setActive(bool);

    // a subordinate button is only sensitive when the main button is active
    // i.e. a subordinate button is greyed-out when the main button is not checked

    void setSubordinateWidgets(std::vector<Gtk::Widget *> btns) {
        _subordinate_widgets = std::move(btns);
    }

    bool setProgrammatically; // true if the value was set by setActive, not changed by the user;
                              // if a callback checks it, it must reset it back to false

private:
    char const *_active_str = nullptr, *_inactive_str = nullptr;
    std::vector<Gtk::Widget *> _subordinate_widgets;

    void on_toggled() final;
};

class RegisteredToggleButton : public RegisteredWidget<Gtk::ToggleButton> {
public:
    RegisteredToggleButton(Glib::ustring const &label, Glib::ustring const &tip,
                           Glib::ustring const &key, Registry &wr, bool right = true,
                           Inkscape::XML::Node *repr_in = nullptr, SPDocument *doc_in = nullptr,
                           char const *icon_active = "true", char const *icon_inactive = "false");

    void setActive(bool);

    // a subordinate button is only sensitive when the main button is active
    // i.e. a subordinate button is greyed-out when the main button is not checked
    void setSubordinateWidgets(std::vector<Gtk::Widget *> btns) {
        _subordinate_widgets = std::move(btns);
    }

    bool setProgrammatically; // true if the value was set by setActive, not changed by the user;
                              // if a callback checks it, it must reset it back to false

private:
    std::vector<Gtk::Widget *> _subordinate_widgets;

    void on_toggled() final;
};

class RegisteredUnitMenu : public RegisteredWidget<Labelled> {
public:
    RegisteredUnitMenu(Glib::ustring const &label,
                       Glib::ustring const &key,
                       Registry &wr,
                       Inkscape::XML::Node *repr_in = nullptr,
                       SPDocument *doc_in = nullptr);

    void setUnit(const Glib::ustring);
    Unit     const * getUnit    () const { return getUnitMenu()->getUnit(); };
    UnitMenu const * getUnitMenu() const { return static_cast<UnitMenu const *>(getWidget()); };
    UnitMenu       * getUnitMenu()       { return static_cast<UnitMenu       *>(getWidget()); };

private:
    auto_connection _changed_connection;

    void on_changed();
};

// Allow RegisteredScalarUnit to output lengths in 'user units' (which may have direction dependent
// scale factors).
enum RSU_UserUnits {
    RSU_none,
    RSU_x,
    RSU_y
};

class RegisteredScalarUnit : public RegisteredWidget<ScalarUnit> {
public:
    RegisteredScalarUnit(Glib::ustring const &label,
                         Glib::ustring const &tip,
                         Glib::ustring const &key,
                         RegisteredUnitMenu &rum,
                         Registry &wr,
                         Inkscape::XML::Node *repr_in = nullptr,
                         SPDocument *doc_in = nullptr,
                         RSU_UserUnits user_units = RSU_none);

protected:
    void on_value_changed();

private:
    auto_connection  _value_changed_connection;
    UnitMenu const  *_um;
    RSU_UserUnits    _user_units;
};

class RegisteredScalar : public RegisteredWidget<Scalar> {
public:
    RegisteredScalar(Glib::ustring const &label,
                     Glib::ustring const &tip,
                     Glib::ustring const &key,
                     Registry &wr,
                     Inkscape::XML::Node *repr_in = nullptr,
                     SPDocument *doc_in = nullptr);

protected:
    void on_value_changed();

private:
    auto_connection _value_changed_connection;
};

class RegisteredText : public RegisteredWidget<Text> {
public:
    RegisteredText(Glib::ustring const &label,
                   Glib::ustring const &tip,
                   Glib::ustring const &key,
                   Registry &wr,
                   Inkscape::XML::Node *repr_in = nullptr,
                   SPDocument *doc_in = nullptr);

protected:
    void on_activate();

private:
    auto_connection _activate_connection;
};

class RegisteredColorPicker : public RegisteredWidget<LabelledColorPicker> {
public:
    RegisteredColorPicker(Glib::ustring const &label,
                          Glib::ustring const &title,
                          Glib::ustring const &tip,
                          Glib::ustring const &ckey,
                          Glib::ustring const &akey,
                          Registry &wr,
                          Inkscape::XML::Node *repr_in = nullptr,
                          SPDocument *doc_in = nullptr);

    void setRgba32(std::uint32_t);
    void closeWindow();

private:
    Glib::ustring _ckey, _akey;
    auto_connection _changed_connection;

    void on_changed(std::uint32_t);
};

class RegisteredInteger : public RegisteredWidget<Scalar> {
public:
    RegisteredInteger(Glib::ustring const &label,
                      Glib::ustring const &tip,
                      Glib::ustring const &key,
                      Registry &wr,
                      Inkscape::XML::Node *repr_in = nullptr,
                      SPDocument *doc_in = nullptr);

    bool setProgrammatically; // true if the value was set by setValue, not changed by the user;
                              // if a callback checks it, it must reset it back to false

protected:
    void on_value_changed();

private:
    auto_connection _changed_connection;
};


class RegisteredTransformedPoint : public RegisteredWidget<Point> {
public:
    RegisteredTransformedPoint(Glib::ustring const &label,
                               Glib::ustring const &tip,
                               Glib::ustring const &key,
                               Registry &wr,
                               Inkscape::XML::Node *repr_in = nullptr,
                               SPDocument *doc_in = nullptr);

           // redefine setValue, because transform must be applied
    void setValue(Geom::Point const & p);

    void setTransform(Geom::Affine const & canvas_to_svg);

protected:
    void on_value_changed();

private:
    auto_connection _value_x_changed_connection;
    auto_connection _value_y_changed_connection;
    Geom::Affine to_svg;
};


class RegisteredVector : public RegisteredWidget<Point> {
public:
    RegisteredVector(Glib::ustring const &label,
                     Glib::ustring const &tip,
                     Glib::ustring const &key,
                     Registry &wr,
                     Inkscape::XML::Node *repr_in = nullptr,
                     SPDocument *doc_in = nullptr);

    // redefine setValue, because transform must be applied
    void setValue(Geom::Point const & p);
    void setValue(Geom::Point const & p, Geom::Point const & origin);

    /**
     * Changes the widgets text to polar coordinates. The SVG output will still be a normal cartesian vector.
     * Careful: when calling getValue(), the return value's X-coord will be the angle, Y-value will be the distance/length. 
     * After changing the coords type (polar/non-polar), the value has to be reset (setValue).
     */
    void setPolarCoords(bool polar_coords = true);

protected:
    void on_value_changed();

private:
    auto_connection _value_x_changed_connection;
    auto_connection _value_y_changed_connection;
    Geom::Point _origin;
    bool _polar_coords;
};


class RegisteredRandom : public RegisteredWidget<Random> {
public:
    RegisteredRandom(Glib::ustring const &label,
                     Glib::ustring const &tip,
                     Glib::ustring const &key,
                     Registry &wr,
                     Inkscape::XML::Node *repr_in = nullptr,
                     SPDocument *doc_in = nullptr);

    void setValue(double val, long startseed);

protected:
    void on_value_changed();

private:
    auto_connection _value_changed_connection;
    auto_connection _reseeded_connection;
};

class RegisteredFontButton : public RegisteredWidget<FontButton> {
public:
    RegisteredFontButton(Glib::ustring const &label,
                         Glib::ustring const &tip,
                         Glib::ustring const &key,
                         Registry &wr,
                         Inkscape::XML::Node *repr_in = nullptr,
                         SPDocument *doc_in = nullptr);

    void setValue(Glib::ustring fontspec);

protected:
    void on_value_changed();

private:
    auto_connection _signal_font_set;
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_INKSCAPE_UI_WIDGET_REGISTERED_WIDGET_H

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
