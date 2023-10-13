// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * PaintSelector: Generic paint selector widget.
 *//*
 * Authors:
 * see git history
 *   Lauris Kaplinski
 *   bulia byak <buliabyak@users.sf.net>
 *   John Cliff <simarilius@yahoo.com>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#define noSP_PS_VERBOSE

#include "paint-selector.h"

#include <cstring>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include <2geom/affine.h>
#include <2geom/point.h>
#include <2geom/transforms.h>
#include <glibmm/fileutils.h>
#include <glibmm/i18n.h>
#include <gtkmm/combobox.h>
#include <gtkmm/label.h>
#include <gtkmm/radiobutton.h>
#include <gtkmm/togglebutton.h>

#include "desktop-style.h"
#include "document.h"
#include "style.h"
#include "inkscape.h"

#include "helper/stock-items.h"
#include "io/resource.h"
#include "io/sys.h"
#include "object/sp-hatch.h"
#include "object/sp-linear-gradient.h"
#include "object/sp-mesh-gradient.h"
#include "object/sp-pattern.h"
#include "object/sp-radial-gradient.h"
#include "object/sp-stop.h"
#include "path-prefix.h"
#include "pattern-manipulation.h"
#include "svg/css-ostringstream.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/pack.h"
#include "ui/widget/color-notebook.h"
#include "ui/widget/gradient-editor.h"
#include "ui/widget/gradient-selector.h"
#include "ui/widget/pattern-editor.h"
#include "ui/widget/scrollprotected.h"
#include "ui/widget/swatch-selector.h"
#include "widgets/widget-sizes.h"
#include "xml/repr.h"

#ifdef SP_PS_VERBOSE
#include "svg/svg-icc-color.h"
#endif // SP_PS_VERBOSE

using Inkscape::UI::SelectedColor;

#ifdef SP_PS_VERBOSE
static gchar const *modeStrings[] = {
    "MODE_EMPTY",
    "MODE_MULTIPLE",
    "MODE_NONE",
    "MODE_SOLID_COLOR",
    "MODE_GRADIENT_LINEAR",
    "MODE_GRADIENT_RADIAL",
#ifdef WITH_MESH
    "MODE_GRADIENT_MESH",
#endif
    "MODE_PATTERN",
    "MODE_SWATCH",
    "MODE_UNSET",
    ".",
    ".",
};
#endif

namespace {
GtkWidget *_scrollprotected_combo_box_new_with_model(GtkTreeModel *model)
{
    auto const combobox = Gtk::make_managed<Inkscape::UI::Widget::ScrollProtected<Gtk::ComboBox>>();
    gtk_combo_box_set_model(combobox->gobj(), model);
    return combobox->Gtk::Widget::gobj();
}
} // namespace

namespace Inkscape {
namespace UI {
namespace Widget {

class FillRuleRadioButton : public Gtk::RadioButton {
  private:
    PaintSelector::FillRule _fillrule;

  public:
    FillRuleRadioButton()
        : Gtk::RadioButton()
    {}

    FillRuleRadioButton(Gtk::RadioButton::Group &group)
        : Gtk::RadioButton(group)
    {}

    inline void set_fillrule(PaintSelector::FillRule fillrule) { _fillrule = fillrule; }
    inline PaintSelector::FillRule get_fillrule() const { return _fillrule; }
};

class StyleToggleButton : public Gtk::ToggleButton {
  private:
    PaintSelector::Mode _style;

  public:
    inline void set_style(PaintSelector::Mode style) { _style = style; }
    inline PaintSelector::Mode get_style() const { return _style; }
};

static bool isPaintModeGradient(PaintSelector::Mode mode)
{
    bool isGrad = (mode == PaintSelector::MODE_GRADIENT_LINEAR) || (mode == PaintSelector::MODE_GRADIENT_RADIAL) ||
                  (mode == PaintSelector::MODE_SWATCH);

    return isGrad;
}

GradientSelectorInterface *PaintSelector::getGradientFromData() const
{
    if (_mode == PaintSelector::MODE_SWATCH && _selector_swatch) {
        return _selector_swatch->getGradientSelector();
    }
    return _selector_gradient;
}

#define XPAD 4
#define YPAD 1

PaintSelector::PaintSelector(FillOrStroke kind)
{
    set_orientation(Gtk::Orientation::VERTICAL);

    _mode = static_cast<PaintSelector::Mode>(-1); // huh?  do you mean 0xff?  --  I think this means "not in the enum"

    /* Paint style button box */
    _style = Gtk::make_managed<Gtk::Box>();
    _style->set_name("PaintSelector");
    _style->set_visible(true);
    UI::pack_start(*this, *_style, false, false);

    /* Buttons */
    _none = style_button_add(INKSCAPE_ICON("paint-none"), PaintSelector::MODE_NONE, _("No paint"));
    _solid = style_button_add(INKSCAPE_ICON("paint-solid"), PaintSelector::MODE_SOLID_COLOR, _("Flat color"));
    _gradient = style_button_add(INKSCAPE_ICON("paint-gradient-linear"), PaintSelector::MODE_GRADIENT_LINEAR,
                                 _("Linear gradient"));
    _radial = style_button_add(INKSCAPE_ICON("paint-gradient-radial"), PaintSelector::MODE_GRADIENT_RADIAL,
                               _("Radial gradient"));
#ifdef WITH_MESH
    _mesh =
        style_button_add(INKSCAPE_ICON("paint-gradient-mesh"), PaintSelector::MODE_GRADIENT_MESH, _("Mesh gradient"));
#endif
    _pattern = style_button_add(INKSCAPE_ICON("paint-pattern"), PaintSelector::MODE_PATTERN, _("Pattern"));
    _swatch = style_button_add(INKSCAPE_ICON("paint-swatch"), PaintSelector::MODE_SWATCH, _("Swatch"));
    _unset = style_button_add(INKSCAPE_ICON("paint-unknown"), PaintSelector::MODE_UNSET,
                              _("Unset paint (make it undefined so it can be inherited)"));

    /* Fillrule */
    {
        _fillrulebox = Gtk::make_managed<Gtk::Box>();
        UI::pack_end(*_style, *_fillrulebox, false, false);

        _evenodd = Gtk::make_managed<FillRuleRadioButton>();
        _evenodd->set_relief(Gtk::RELIEF_NONE);
        _evenodd->set_mode(false);
        // TRANSLATORS: for info, see http://www.w3.org/TR/2000/CR-SVG-20000802/painting.html#FillRuleProperty
        _evenodd->set_tooltip_text(
            _("Any path self-intersections or subpaths create holes in the fill (fill-rule: evenodd)"));
        _evenodd->set_fillrule(PaintSelector::FILLRULE_EVENODD);
        auto w = sp_get_icon_image("fill-rule-even-odd", GTK_ICON_SIZE_MENU);
        gtk_container_add(GTK_CONTAINER(_evenodd->gobj()), w);
        UI::pack_start(*_fillrulebox, *_evenodd, false, false);
        _evenodd->signal_toggled().connect(
            sigc::bind(sigc::mem_fun(*this, &PaintSelector::fillrule_toggled), _evenodd));

        auto grp = _evenodd->get_group();
        _nonzero = Gtk::make_managed<FillRuleRadioButton>(grp);
        _nonzero->set_relief(Gtk::RELIEF_NONE);
        _nonzero->set_mode(false);
        // TRANSLATORS: for info, see http://www.w3.org/TR/2000/CR-SVG-20000802/painting.html#FillRuleProperty
        _nonzero->set_tooltip_text(_("Fill is solid unless a subpath is counterdirectional (fill-rule: nonzero)"));
        _nonzero->set_fillrule(PaintSelector::FILLRULE_NONZERO);
        w = sp_get_icon_image("fill-rule-nonzero", GTK_ICON_SIZE_MENU);
        gtk_container_add(GTK_CONTAINER(_nonzero->gobj()), w);
        UI::pack_start(*_fillrulebox, *_nonzero, false, false);
        _nonzero->signal_toggled().connect(
            sigc::bind(sigc::mem_fun(*this, &PaintSelector::fillrule_toggled), _nonzero));
    }

    /* Frame */
    _label = Gtk::make_managed<Gtk::Label>("");
    auto const lbbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    _label->set_visible(true);
    UI::pack_start(*lbbox, *_label, false, false, 4);
    UI::pack_start(*this, *lbbox, false, false, 4);

    _frame = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    _frame->set_visible(true);
    UI::pack_start(*this, *_frame, true, true);

    /* Last used color */
    _selected_color = std::make_unique<SelectedColor>();
    _updating_color = false;

    _selected_color->signal_grabbed.connect(sigc::mem_fun(*this, &PaintSelector::onSelectedColorGrabbed));
    _selected_color->signal_dragged.connect(sigc::mem_fun(*this, &PaintSelector::onSelectedColorDragged));
    _selected_color->signal_released.connect(sigc::mem_fun(*this, &PaintSelector::onSelectedColorReleased));
    _selected_color->signal_changed.connect(sigc::mem_fun(*this, &PaintSelector::onSelectedColorChanged));

    // from _new function
    setMode(PaintSelector::MODE_MULTIPLE);

    if (kind == FILL)
        _fillrulebox->show_all();
    else
        _fillrulebox->set_visible(false);

    show_all();

    // don't let docking manager uncover hidden widgets
    set_no_show_all();
}

StyleToggleButton *PaintSelector::style_button_add(gchar const *pixmap, PaintSelector::Mode mode, gchar const *tip)
{
    GtkWidget *w;

    auto const b = Gtk::make_managed<StyleToggleButton>();
    b->set_tooltip_text(tip);
    b->set_visible(true);
    b->set_relief(Gtk::RELIEF_NONE);
    b->set_mode(false);
    b->set_style(mode);

    w = sp_get_icon_image(pixmap, GTK_ICON_SIZE_BUTTON);
    gtk_container_add(GTK_CONTAINER(b->gobj()), w);

    UI::pack_start(*_style, *b, false, false);
    b->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &PaintSelector::style_button_toggled), b));

    return b;
}

void PaintSelector::style_button_toggled(StyleToggleButton *tb)
{
    if (!_update && tb->get_active()) {
        // button toggled: explicit user action where fill/stroke style change is initiated/requested
        set_mode_ex(tb->get_style(), true);
    }
}

void PaintSelector::fillrule_toggled(FillRuleRadioButton *tb)
{
    if (!_update && tb->get_active()) {
        auto fr = tb->get_fillrule();
        _signal_fillrule_changed.emit(fr);
    }
}

void PaintSelector::setMode(Mode mode) {
    set_mode_ex(mode, false);
}

void PaintSelector::set_mode_ex(Mode mode, bool switch_style) {
    if (_mode != mode) {
        _update = true;
        _label->set_visible(true);
#ifdef SP_PS_VERBOSE
        g_print("Mode change %d -> %d   %s -> %s\n", _mode, mode, modeStrings[_mode], modeStrings[mode]);
#endif
        switch (mode) {
            case MODE_EMPTY:
                set_mode_empty();
                break;
            case MODE_MULTIPLE:
                set_mode_multiple();
                break;
            case MODE_NONE:
                set_mode_none();
                break;
            case MODE_SOLID_COLOR:
                set_mode_color(mode);
                break;
            case MODE_GRADIENT_LINEAR:
            case MODE_GRADIENT_RADIAL:
                set_mode_gradient(mode);
                break;
#ifdef WITH_MESH
            case MODE_GRADIENT_MESH:
                set_mode_mesh(mode);
                break;
#endif
            case MODE_PATTERN:
                set_mode_pattern(mode);
                break;
            case MODE_HATCH:
                set_mode_hatch(mode);
                break;
            case MODE_SWATCH:
                set_mode_swatch(mode);
                break;
            case MODE_UNSET:
                set_mode_unset();
                break;
            default:
                g_warning("file %s: line %d: Unknown paint mode %d", __FILE__, __LINE__, mode);
                break;
        }
        _mode = mode;
        _signal_mode_changed.emit(_mode, switch_style);
        _update = false;
    }
}

void PaintSelector::setFillrule(FillRule fillrule)
{
    if (_fillrulebox) {
        // TODO this flips widgets but does not use a member to store state. Revisit
        _evenodd->set_active(fillrule == FILLRULE_EVENODD);
        _nonzero->set_active(fillrule == FILLRULE_NONZERO);
    }
}

void PaintSelector::setColorAlpha(SPColor const &color, float alpha)
{
    g_return_if_fail((0.0 <= alpha) && (alpha <= 1.0));
    {
#ifdef SP_PS_VERBOSE
        g_print("PaintSelector set RGBA\n");
#endif
        setMode(MODE_SOLID_COLOR);
    }
    _updating_color = true;
    _selected_color->setColorAlpha(color, alpha);
    _updating_color = false;
}

void PaintSelector::setSwatch(SPGradient *vector)
{
#ifdef SP_PS_VERBOSE
    g_print("PaintSelector set SWATCH\n");
#endif
    setMode(MODE_SWATCH);

    if (_selector_swatch) {
        _selector_swatch->setVector((vector) ? vector->document : nullptr, vector);
    }
}

void PaintSelector::setGradientLinear(SPGradient *vector, SPLinearGradient* gradient, SPStop* selected)
{
#ifdef SP_PS_VERBOSE
    g_print("PaintSelector set GRADIENT LINEAR\n");
#endif
    setMode(MODE_GRADIENT_LINEAR);

    auto gsel = getGradientFromData();

    gsel->setMode(GradientSelector::MODE_LINEAR);
    gsel->setGradient(gradient);
    gsel->setVector((vector) ? vector->document : nullptr, vector);
    gsel->selectStop(selected);
}

void PaintSelector::setGradientRadial(SPGradient *vector, SPRadialGradient* gradient, SPStop* selected)
{
#ifdef SP_PS_VERBOSE
    g_print("PaintSelector set GRADIENT RADIAL\n");
#endif
    setMode(MODE_GRADIENT_RADIAL);

    auto gsel = getGradientFromData();

    gsel->setMode(GradientSelector::MODE_RADIAL);
    gsel->setGradient(gradient);
    gsel->setVector((vector) ? vector->document : nullptr, vector);
    gsel->selectStop(selected);
}

#ifdef WITH_MESH
void PaintSelector::setGradientMesh(SPMeshGradient *array)
{
#ifdef SP_PS_VERBOSE
    g_print("PaintSelector set GRADIENT MESH\n");
#endif
    setMode(MODE_GRADIENT_MESH);

    // GradientSelector *gsel = getGradientFromData(this);

    // gsel->setMode(GradientSelector::MODE_GRADIENT_MESH);
    // gsel->setVector((mesh) ? mesh->document : 0, mesh);
}
#endif

void PaintSelector::setGradientProperties(SPGradientUnits units, SPGradientSpread spread)
{
    g_return_if_fail(isPaintModeGradient(_mode));

    auto gsel = getGradientFromData();
    gsel->setUnits(units);
    gsel->setSpread(spread);
}

void PaintSelector::getGradientProperties(SPGradientUnits &units, SPGradientSpread &spread) const
{
    g_return_if_fail(isPaintModeGradient(_mode));

    auto gsel = getGradientFromData();
    units = gsel->getUnits();
    spread = gsel->getSpread();
}


/**
 * \post (alpha == NULL) || (*alpha in [0.0, 1.0]).
 */
void PaintSelector::getColorAlpha(SPColor &color, gfloat &alpha) const
{
    _selected_color->colorAlpha(color, alpha);

    g_assert((0.0 <= alpha) && (alpha <= 1.0));
}

SPGradient *PaintSelector::getGradientVector()
{
    SPGradient *vect = nullptr;

    if (isPaintModeGradient(_mode)) {
        auto gsel = getGradientFromData();
        vect = gsel->getVector();
    }

    return vect;
}


void PaintSelector::pushAttrsToGradient(SPGradient *gr) const
{
    SPGradientUnits units = SP_GRADIENT_UNITS_OBJECTBOUNDINGBOX;
    SPGradientSpread spread = SP_GRADIENT_SPREAD_PAD;
    getGradientProperties(units, spread);
    gr->setUnits(units);
    gr->setSpread(spread);
    gr->updateRepr();
}

void PaintSelector::clear_frame()
{
    if (_selector_solid_color) {
        _selector_solid_color->set_visible(false);
    }
    if (_selector_gradient) {
        _selector_gradient->set_visible(false);
    }
    if (_selector_mesh) {
        _selector_mesh->set_visible(false);
    }
    if (_selector_pattern) {
        _selector_pattern->set_visible(false);
    }
    if (_selector_swatch) {
        _selector_swatch->set_visible(false);
    }
}

void PaintSelector::set_mode_empty()
{
    set_style_buttons(nullptr);
    _style->set_sensitive(false);
    clear_frame();
    _label->set_markup(_("<b>No objects</b>"));
}

void PaintSelector::set_mode_multiple()
{
    set_style_buttons(nullptr);
    _style->set_sensitive(true);
    clear_frame();
    _label->set_markup(_("<b>Multiple styles</b>"));
}

void PaintSelector::set_mode_unset()
{
    set_style_buttons(_unset);
    _style->set_sensitive(true);
    clear_frame();
    _label->set_markup(_("<b>Paint is undefined</b>"));
}

void PaintSelector::set_mode_none()
{
    set_style_buttons(_none);
    _style->set_sensitive(true);
    clear_frame();
    _label->set_markup(_("<b>No paint</b>"));
}

/* Color paint */

void PaintSelector::onSelectedColorGrabbed() { _signal_grabbed.emit(); }

void PaintSelector::onSelectedColorDragged()
{
    if (_updating_color) {
        return;
    }

    _signal_dragged.emit();
}

void PaintSelector::onSelectedColorReleased() { _signal_released.emit(); }

void PaintSelector::onSelectedColorChanged()
{
    if (_updating_color) {
        return;
    }

    if (_mode == MODE_SOLID_COLOR) {
        _signal_changed.emit();
    } else {
        g_warning("PaintSelector::onSelectedColorChanged(): selected color changed while not in color selection mode");
    }
}

void PaintSelector::set_mode_color(PaintSelector::Mode /*mode*/)
{
    using Inkscape::UI::Widget::ColorNotebook;

    if (_mode == PaintSelector::MODE_SWATCH) {
        auto gsel = getGradientFromData();
        if (gsel) {
            SPGradient *gradient = gsel->getVector();

            // Gradient can be null if object paint is changed externally (ie. with a color picker tool)
            if (gradient) {
                SPColor color = gradient->getFirstStop()->getColor();
                float alpha = gradient->getFirstStop()->getOpacity();
                _selected_color->setColorAlpha(color, alpha, false);
            }
        }
    }

    set_style_buttons(_solid);
    _style->set_sensitive(true);

    if (_mode == PaintSelector::MODE_SOLID_COLOR) {
        /* Already have color selector */
        // Do nothing
    } else {
        clear_frame();

        /* Create new color selector */
        /* Create vbox */
        if (!_selector_solid_color) {
            _selector_solid_color = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);

            /* Color selector */
            auto const color_selector = Gtk::make_managed<ColorNotebook>(*_selected_color);
            color_selector->set_visible(true);
            UI::pack_start(*_selector_solid_color, *color_selector, true, true);
            /* Pack everything to frame */
            _frame->add(*_selector_solid_color);
            color_selector->set_label(_("<b>Flat color</b>"));
        }

        _selector_solid_color->set_visible(true);
    }

    _label->set_markup(""); //_("<b>Flat color</b>"));
    _label->set_visible(false);

#ifdef SP_PS_VERBOSE
    g_print("Color req\n");
#endif
}

/* Gradient */

void PaintSelector::gradient_grabbed() { _signal_grabbed.emit(); }

void PaintSelector::gradient_dragged() { _signal_dragged.emit(); }

void PaintSelector::gradient_released() { _signal_released.emit(); }

void PaintSelector::gradient_changed(SPGradient * /* gr */) { _signal_changed.emit(); }

void PaintSelector::set_mode_gradient(PaintSelector::Mode mode)
{
    if (mode == PaintSelector::MODE_GRADIENT_LINEAR) {
        set_style_buttons(_gradient);
    } else if (mode == PaintSelector::MODE_GRADIENT_RADIAL) {
        set_style_buttons(_radial);
    }
    _style->set_sensitive(true);

    if ((_mode == PaintSelector::MODE_GRADIENT_LINEAR) || (_mode == PaintSelector::MODE_GRADIENT_RADIAL)) {
        // do nothing - the selector should already be a GradientSelector
    } else {
        clear_frame();
        if (!_selector_gradient) {
            /* Create new gradient selector */
            try {
                _selector_gradient = Gtk::make_managed<GradientEditor>("/gradient-edit");
                _selector_gradient->set_visible(true);
                _selector_gradient->signal_grabbed().connect(sigc::mem_fun(*this, &PaintSelector::gradient_grabbed));
                _selector_gradient->signal_dragged().connect(sigc::mem_fun(*this, &PaintSelector::gradient_dragged));
                _selector_gradient->signal_released().connect(sigc::mem_fun(*this, &PaintSelector::gradient_released));
                _selector_gradient->signal_changed().connect(sigc::mem_fun(*this, &PaintSelector::gradient_changed));
                _selector_gradient->signal_stop_selected().connect([=](SPStop* stop) { _signal_stop_selected.emit(stop); });
                /* Pack everything to frame */
                _frame->add(*_selector_gradient);
            }
            catch (std::exception& ex) {
                g_error("Creation of GradientEditor widget failed: %s.", ex.what());
                throw;
            }
        } else {
            // Necessary when creating new gradients via the Fill and Stroke dialog
            _selector_gradient->setVector(nullptr, nullptr);
        }
        _selector_gradient->set_visible(true);
    }

    /* Actually we have to set option menu history here */
    if (mode == PaintSelector::MODE_GRADIENT_LINEAR) {
        _selector_gradient->setMode(GradientSelector::MODE_LINEAR);
        // sp_gradient_selector_set_mode(SP_GRADIENT_SELECTOR(gsel), SP_GRADIENT_SELECTOR_MODE_LINEAR);
      //   _label->set_markup(_("<b>Linear gradient</b>"));
        _label->set_visible(false);
    } else if (mode == PaintSelector::MODE_GRADIENT_RADIAL) {
        _selector_gradient->setMode(GradientSelector::MODE_RADIAL);
        // _label->set_markup(_("<b>Radial gradient</b>"));
        _label->set_visible(false);
    }

#ifdef SP_PS_VERBOSE
    g_print("Gradient req\n");
#endif
}

// ************************* MESH ************************
#ifdef WITH_MESH
void PaintSelector::mesh_destroy(GtkWidget *widget, PaintSelector * /*psel*/)
{
    // drop our reference to the mesh menu widget
    g_object_unref(G_OBJECT(widget));
}

void PaintSelector::mesh_change(GtkWidget * /*widget*/, PaintSelector *psel) { psel->_signal_changed.emit(); }


/**
 *  Returns a list of meshes in the defs of the given source document as a vector
 */
static std::vector<SPMeshGradient *> ink_mesh_list_get(SPDocument *source)
{
    std::vector<SPMeshGradient *> pl;
    if (source == nullptr)
        return pl;


    std::vector<SPObject *> meshes = source->getResourceList("gradient");
    for (auto meshe : meshes) {
        if (is<SPMeshGradient>(meshe) && cast<SPGradient>(meshe) == cast<SPGradient>(meshe)->getArray()) { // only if this is a
                                                                                                 // root mesh
            pl.push_back(cast<SPMeshGradient>(meshe));
        }
    }
    return pl;
}

/**
 * Adds menu items for mesh list.
 */
static void sp_mesh_menu_build(GtkWidget *combo, std::vector<SPMeshGradient *> &mesh_list, SPDocument * /*source*/)
{
    GtkListStore *store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(combo)));
    GtkTreeIter iter;

    for (auto i : mesh_list) {

        Inkscape::XML::Node *repr = i->getRepr();

        gchar const *meshid = repr->attribute("id");
        gchar const *label = meshid;

        // Only relevant if we supply a set of canned meshes.
        gboolean stockid = false;
        if (repr->attribute("inkscape:stockid")) {
            label = _(repr->attribute("inkscape:stockid"));
            stockid = true;
        }

        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, COMBO_COL_LABEL, label, COMBO_COL_STOCK, stockid, COMBO_COL_MESH, meshid,
                           COMBO_COL_SEP, FALSE, -1);
    }
}

/**
 * Pick up all meshes from source, except those that are in
 * current_doc (if non-NULL), and add items to the mesh menu.
 */
static void sp_mesh_list_from_doc(GtkWidget *combo, SPDocument * /*current_doc*/, SPDocument *source,
                                  SPDocument * /*mesh_doc*/)
{
    std::vector<SPMeshGradient *> pl = ink_mesh_list_get(source);
    sp_mesh_menu_build(combo, pl, source);
}


static void ink_mesh_menu_populate_menu(GtkWidget *combo, SPDocument *doc)
{
    static SPDocument *meshes_doc = nullptr;

    // If we ever add a list of canned mesh gradients, uncomment following:

    // find and load meshes.svg
    // if (meshes_doc == NULL) {
    //     char *meshes_source = g_build_filename(INKSCAPE_MESHESDIR, "meshes.svg", NULL);
    //     if (Inkscape::IO::file_test(meshes_source, G_FILE_TEST_IS_REGULAR)) {
    //         meshes_doc = SPDocument::createNewDoc(meshes_source, FALSE);
    //     }
    //     g_free(meshes_source);
    // }

    // suck in from current doc
    sp_mesh_list_from_doc(combo, nullptr, doc, meshes_doc);

    // add separator
    // {
    //     GtkListStore *store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(combo)));
    //     GtkTreeIter iter;
    //     gtk_list_store_append (store, &iter);
    //     gtk_list_store_set(store, &iter,
    //             COMBO_COL_LABEL, "", COMBO_COL_STOCK, false, COMBO_COL_MESH, "", COMBO_COL_SEP, true, -1);
    // }

    // suck in from meshes.svg
    // if (meshes_doc) {
    //     doc->ensureUpToDate();
    //     sp_mesh_list_from_doc ( combo, doc, meshes_doc, NULL );
    // }
}


static GtkWidget *ink_mesh_menu(GtkWidget *combo)
{
    SPDocument *doc = SP_ACTIVE_DOCUMENT;

    GtkListStore *store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(combo)));
    GtkTreeIter iter;

    if (!doc) {

        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, COMBO_COL_LABEL, _("No document selected"), COMBO_COL_STOCK, false,
                           COMBO_COL_MESH, "", COMBO_COL_SEP, false, -1);
        gtk_widget_set_sensitive(combo, FALSE);

    } else {

        ink_mesh_menu_populate_menu(combo, doc);
        gtk_widget_set_sensitive(combo, TRUE);
    }

    // Select the first item that is not a separator
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter)) {
        gboolean sep = false;
        gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, COMBO_COL_SEP, &sep, -1);
        if (sep) {
            gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
        }
        gtk_combo_box_set_active_iter(GTK_COMBO_BOX(combo), &iter);
    }

    return combo;
}


/*update mesh list*/
void PaintSelector::updateMeshList(SPMeshGradient *mesh)
{
    if (_update) {
        return;
    }

    g_assert(_meshmenu != nullptr);

    /* Clear existing menu if any */
    GtkTreeModel *store = gtk_combo_box_get_model(GTK_COMBO_BOX(_meshmenu));
    gtk_list_store_clear(GTK_LIST_STORE(store));

    ink_mesh_menu(_meshmenu);

    /* Set history */

    if (mesh && !_meshmenu_update) {
        _meshmenu_update = true;
        gchar const *meshname = mesh->getRepr()->attribute("id");

        // Find this mesh and set it active in the combo_box
        GtkTreeIter iter;
        gchar *meshid = nullptr;
        bool valid = gtk_tree_model_get_iter_first(store, &iter);
        if (!valid) {
            return;
        }
        gtk_tree_model_get(store, &iter, COMBO_COL_MESH, &meshid, -1);
        while (valid && strcmp(meshid, meshname) != 0) {
            valid = gtk_tree_model_iter_next(store, &iter);
            g_free(meshid);
            meshid = nullptr;
            gtk_tree_model_get(store, &iter, COMBO_COL_MESH, &meshid, -1);
        }

        if (valid) {
            gtk_combo_box_set_active_iter(GTK_COMBO_BOX(_meshmenu), &iter);
        }

        _meshmenu_update = false;
        g_free(meshid);
    }
}

#ifdef WITH_MESH
void PaintSelector::set_mode_mesh(PaintSelector::Mode mode)
{
    if (mode == PaintSelector::MODE_GRADIENT_MESH) {
        set_style_buttons(_mesh);
    }
    _style->set_sensitive(true);

    if (_mode == PaintSelector::MODE_GRADIENT_MESH) {
        /* Already have mesh menu */
        // Do nothing - the Selector is already a Gtk::Box with the required contents
    } else {
        clear_frame();

        if (!_selector_mesh) {
            /* Create vbox */
            _selector_mesh = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);

            auto const hb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 1);

            /**
             * Create a combo_box and store with 4 columns,
             * The label, a pointer to the mesh, is stockid or not, is a separator or not.
             */
            GtkListStore *store =
                gtk_list_store_new(COMBO_N_COLS, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_BOOLEAN);
            GtkWidget *combo = _scrollprotected_combo_box_new_with_model(GTK_TREE_MODEL(store));
            gtk_combo_box_set_row_separator_func(GTK_COMBO_BOX(combo), PaintSelector::isSeparator, nullptr, nullptr);

            GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
            gtk_cell_renderer_set_padding(renderer, 2, 0);
            gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), renderer, TRUE);
            gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), renderer, "text", COMBO_COL_LABEL, nullptr);

            ink_mesh_menu(combo);
            g_signal_connect(G_OBJECT(combo), "changed", G_CALLBACK(PaintSelector::mesh_change), this);
            g_signal_connect(G_OBJECT(combo), "destroy", G_CALLBACK(PaintSelector::mesh_destroy), this);
            _meshmenu = combo;
            g_object_ref(G_OBJECT(combo));

            gtk_container_add(GTK_CONTAINER(hb->gobj()), combo);
            UI::pack_start(*_selector_mesh, *hb, false, false, AUX_BETWEEN_BUTTON_GROUPS);

            g_object_unref(G_OBJECT(store));

            auto const hb2 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);

            auto const l = Gtk::make_managed<Gtk::Label>();
            l->set_markup(_("Use the <b>Mesh tool</b> to modify the mesh."));
            l->set_line_wrap(true);
            l->set_size_request(180, -1);
            UI::pack_start(*hb2, *l, true, true, AUX_BETWEEN_BUTTON_GROUPS);
            UI::pack_start(*_selector_mesh, *hb2, false, false, AUX_BETWEEN_BUTTON_GROUPS);
            _selector_mesh->show_all();

            _frame->add(*_selector_mesh);
        }

        _selector_mesh->set_visible(true);
        _label->set_markup(_("<b>Mesh fill</b>"));
    }
#ifdef SP_PS_VERBOSE
    g_print("Mesh req\n");
#endif
}
#endif // WITH_MESH

SPMeshGradient *PaintSelector::getMeshGradient()
{
    g_return_val_if_fail((_mode == MODE_GRADIENT_MESH), NULL);

    /* no mesh menu if we were just selected */
    if (_meshmenu == nullptr) {
        return nullptr;
    }
    GtkTreeModel *store = gtk_combo_box_get_model(GTK_COMBO_BOX(_meshmenu));

    /* Get the selected mesh */
    GtkTreeIter iter;
    if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(_meshmenu), &iter) ||
        !gtk_list_store_iter_is_valid(GTK_LIST_STORE(store), &iter)) {
        return nullptr;
    }

    gchar *meshid = nullptr;
    gboolean stockid = FALSE;
    // gchar *label = nullptr;
    gtk_tree_model_get(store, &iter, COMBO_COL_STOCK, &stockid, COMBO_COL_MESH, &meshid, -1);
    // gtk_tree_model_get (store, &iter, COMBO_COL_LABEL, &label, COMBO_COL_STOCK, &stockid, COMBO_COL_MESH, &meshid,
    // -1); std::cout << "  .. meshid: " << (meshid?meshid:"null") << "   label: " << (label?label:"null") << std::endl;
    // g_free(label);
    if (meshid == nullptr) {
        return nullptr;
    }

    SPMeshGradient *mesh = nullptr;
    if (strcmp(meshid, "none")) {

        gchar *mesh_name;
        if (stockid) {
            mesh_name = g_strconcat("urn:inkscape:mesh:", meshid, nullptr);
        } else {
            mesh_name = g_strdup(meshid);
        }

        SPObject *mesh_obj = get_stock_item(mesh_name);
        if (mesh_obj && is<SPMeshGradient>(mesh_obj)) {
            mesh = cast<SPMeshGradient>(mesh_obj);
        }
        g_free(mesh_name);
    } else {
        std::cerr << "PaintSelector::getMeshGradient: Unexpected meshid value." << std::endl;
    }

    g_free(meshid);

    return mesh;
}

#endif
// ************************ End Mesh ************************

void PaintSelector::set_style_buttons(Gtk::ToggleButton *active)
{
    _none->set_active(active == _none);
    _solid->set_active(active == _solid);
    _gradient->set_active(active == _gradient);
    _radial->set_active(active == _radial);
#ifdef WITH_MESH
    _mesh->set_active(active == _mesh);
#endif
    _pattern->set_active(active == _pattern);
    _swatch->set_active(active == _swatch);
    _unset->set_active(active == _unset);
}

void PaintSelector::pattern_destroy(GtkWidget *widget, PaintSelector * /*psel*/)
{
    // drop our reference to the pattern menu widget
    g_object_unref(G_OBJECT(widget));
}

void PaintSelector::pattern_change(GtkWidget * /*widget*/, PaintSelector *psel) { psel->_signal_changed.emit(); }


/*update pattern list*/
void PaintSelector::updatePatternList(SPPattern *pattern)
{
    if (_update) return;
    if (!_selector_pattern) return;

    _selector_pattern->set_selected(pattern);
}

void PaintSelector::set_mode_pattern(PaintSelector::Mode mode)
{
    if (mode == PaintSelector::MODE_PATTERN) {
        set_style_buttons(_pattern);
    }

    _style->set_sensitive(true);

    if (_mode == PaintSelector::MODE_PATTERN) {
        /* Already have pattern menu */
    } else {
        clear_frame();

        if (!_selector_pattern) {
            _selector_pattern = Gtk::make_managed<PatternEditor>("/pattern-edit", PatternManager::get());
            _selector_pattern->signal_changed().connect([=](){ _signal_changed.emit(); });
            _selector_pattern->signal_color_changed().connect([=](unsigned){ _signal_changed.emit(); });
            _selector_pattern->signal_edit().connect([=](){ _signal_edit_pattern.emit(); });
            _selector_pattern->show_all();
            _frame->add(*_selector_pattern);
        }

        SPDocument* document = SP_ACTIVE_DOCUMENT;
        _selector_pattern->set_document(document);
        _selector_pattern->set_visible(true);
        _label->set_visible(false);
    }
#ifdef SP_PS_VERBOSE
    g_print("Pattern req\n");
#endif
}

void PaintSelector::set_mode_hatch(PaintSelector::Mode mode)
{
    if (mode == PaintSelector::MODE_HATCH) {
        set_style_buttons(_unset);
    }

    _style->set_sensitive(true);

    if (_mode == PaintSelector::MODE_HATCH) {
        /* Already have hatch menu, for the moment unset */
    } else {
        clear_frame();

        _label->set_markup(_("<b>Hatch fill</b>"));
    }
#ifdef SP_PS_VERBOSE
    g_print("Hatch req\n");
#endif
}

gboolean PaintSelector::isSeparator(GtkTreeModel *model, GtkTreeIter *iter, gpointer /*data*/)
{
    gboolean sep = FALSE;
    gtk_tree_model_get(model, iter, COMBO_COL_SEP, &sep, -1);
    return sep;
}

std::optional<unsigned int> PaintSelector::get_pattern_color() {
    if (!_selector_pattern) return 0;

    return _selector_pattern->get_selected_color();
}

Geom::Affine PaintSelector::get_pattern_transform() {
    Geom::Affine matrix;
    if (!_selector_pattern) return matrix;

    return _selector_pattern->get_selected_transform();
}

Geom::Point PaintSelector::get_pattern_offset() {
    Geom::Point offset;
    if (!_selector_pattern) return offset;

    return _selector_pattern->get_selected_offset();
}

Geom::Scale PaintSelector::get_pattern_gap() {
    Geom::Scale gap(0, 0);
    if (!_selector_pattern) return gap;

    return _selector_pattern->get_selected_gap();
}

Glib::ustring PaintSelector::get_pattern_label() {
    if (!_selector_pattern) return Glib::ustring();

    return _selector_pattern->get_label();
}

bool PaintSelector::is_pattern_scale_uniform() {
    if (!_selector_pattern) return false;

    return _selector_pattern->is_selected_scale_uniform();
}

SPPattern* PaintSelector::getPattern() {
    g_return_val_if_fail(_mode == MODE_PATTERN, nullptr);

    if (!_selector_pattern) return nullptr;

    auto sel = _selector_pattern->get_selected();
    auto stock_doc = sel.second;

    if (sel.first.empty()) return nullptr;

    auto patid = sel.first;
    SPObject* pat_obj = nullptr;
    if (patid != "none") {
        if (stock_doc) {
            patid = "urn:inkscape:pattern:" + patid;
        }
        pat_obj = get_stock_item(patid.c_str(), stock_doc != nullptr, stock_doc);
    } else {
        SPDocument *doc = SP_ACTIVE_DOCUMENT;
        pat_obj = doc->getObjectById(patid);
    }

    return cast<SPPattern>(pat_obj);
}

void PaintSelector::set_mode_swatch(PaintSelector::Mode mode)
{
    if (mode == PaintSelector::MODE_SWATCH) {
        set_style_buttons(_swatch);
    }

    _style->set_sensitive(true);

    if (_mode == PaintSelector::MODE_SWATCH) {
        // Do nothing.  The selector is already a SwatchSelector
    } else {
        clear_frame();

        if (!_selector_swatch) {
            // Create new gradient selector
            _selector_swatch = Gtk::make_managed<SwatchSelector>();

            auto gsel = _selector_swatch->getGradientSelector();
            gsel->signal_grabbed().connect(sigc::mem_fun(*this, &PaintSelector::gradient_grabbed));
            gsel->signal_dragged().connect(sigc::mem_fun(*this, &PaintSelector::gradient_dragged));
            gsel->signal_released().connect(sigc::mem_fun(*this, &PaintSelector::gradient_released));
            gsel->signal_changed().connect(sigc::mem_fun(*this, &PaintSelector::gradient_changed));

            // Pack everything to frame
            _frame->add(*_selector_swatch);
        } else {
            // Necessary when creating new swatches via the Fill and Stroke dialog
            _selector_swatch->setVector(nullptr, nullptr);
        }
        _selector_swatch->set_visible(true);
        _label->set_markup(_("<b>Swatch fill</b>"));
    }

#ifdef SP_PS_VERBOSE
    g_print("Swatch req\n");
#endif
}

// TODO this seems very bad to be taking in a desktop pointer to muck with. Logic probably belongs elsewhere
void PaintSelector::setFlatColor(SPDesktop *desktop, gchar const *color_property, gchar const *opacity_property)
{
    SPCSSAttr *css = sp_repr_css_attr_new();

    SPColor color;
    gfloat alpha = 0;
    getColorAlpha(color, alpha);

    std::string colorStr = color.toString();

#ifdef SP_PS_VERBOSE
    guint32 rgba = color.toRGBA32(alpha);
    g_message("sp_paint_selector_set_flat_color() to '%s' from 0x%08x::%s", colorStr.c_str(), rgba,
              (color.icc ? color.icc->colorProfile.c_str() : "<null>"));
#endif // SP_PS_VERBOSE

    sp_repr_css_set_property(css, color_property, colorStr.c_str());
    Inkscape::CSSOStringStream osalpha;
    osalpha << alpha;
    sp_repr_css_set_property(css, opacity_property, osalpha.str().c_str());

    sp_desktop_set_style(desktop, css);

    sp_repr_css_attr_unref(css);
}

PaintSelector::Mode PaintSelector::getModeForStyle(SPStyle const &style, FillOrStroke kind)
{
    Mode mode = MODE_UNSET;
    SPIPaint const &target = *style.getFillOrStroke(kind == FILL);

    if (!target.set) {
        mode = MODE_UNSET;
    } else if (target.isPaintserver()) {
        SPPaintServer const *server = kind == FILL ? style.getFillPaintServer() : style.getStrokePaintServer();

#ifdef SP_PS_VERBOSE
        g_message("PaintSelector::getModeForStyle(%p, %d)", &style, kind);
        g_message("==== server:%p %s  grad:%s   swatch:%s", server, server->getId(),
                  (is<SPGradient>(server) ? "Y" : "n"),
                  (is<SPGradient>(server) && cast<SPGradient>(server)->getVector()->isSwatch() ? "Y" : "n"));
#endif // SP_PS_VERBOSE


        if (server && is<SPGradient>(server) && cast<SPGradient>(server)->getVector()->isSwatch()) {
            mode = MODE_SWATCH;
        } else if (is<SPLinearGradient>(server)) {
            mode = MODE_GRADIENT_LINEAR;
        } else if (is<SPRadialGradient>(server)) {
            mode = MODE_GRADIENT_RADIAL;
#ifdef WITH_MESH
        } else if (is<SPMeshGradient>(server)) {
            mode = MODE_GRADIENT_MESH;
#endif
        } else if (is<SPPattern>(server)) {
            mode = MODE_PATTERN;
        } else if (is<SPHatch>(server)) {
            mode = MODE_HATCH;
        } else {
            g_warning("file %s: line %d: Unknown paintserver", __FILE__, __LINE__);
            mode = MODE_NONE;
        }
    } else if (target.isColor()) {
        // TODO this is no longer a valid assertion:
        mode = MODE_SOLID_COLOR; // so far only rgb can be read from svg
    } else if (target.isNone()) {
        mode = MODE_NONE;
    } else {
        g_warning("file %s: line %d: Unknown paint type", __FILE__, __LINE__);
        mode = MODE_NONE;
    }

    return mode;
}

} // namespace Widget
} // namespace UI
} // namespace Inkscape
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
