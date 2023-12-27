// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * TODO: insert short description here
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2018 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "color-icc-selector.h"

#include <array>
#include <map>
#include <set>
#include <utility>
#include <vector>
#include <glibmm/i18n.h>
#include <glibmm/refptr.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/combobox.h>
#include <gtkmm/spinbutton.h>

#include "colorspace.h"
#include "document.h"
#include "inkscape.h"
#include "profile-manager.h"
#include "ui/dialog-events.h"
#include "ui/util.h"
#include "ui/widget/color-scales.h"
#include "ui/widget/color-slider.h"

#define noDEBUG_LCMS

#include "object/color-profile.h"
#include "color/color-profile-cms-fns.h"

#ifdef DEBUG_LCMS
#include "preferences.h"
#endif // DEBUG_LCMS

#ifdef DEBUG_LCMS
extern guint update_in_progress;
#define DEBUG_MESSAGE(key, ...)                                                                                        \
    {                                                                                                                  \
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();                                                   \
        bool dump = prefs->getBool("/options/scislac/" #key);                                                          \
        bool dumpD = prefs->getBool("/options/scislac/" #key "D");                                                     \
        bool dumpD2 = prefs->getBool("/options/scislac/" #key "D2");                                                   \
        dumpD && = ((update_in_progress == 0) || dumpD2);                                                              \
        if (dump) {                                                                                                    \
            g_message(__VA_ARGS__);                                                                                    \
        }                                                                                                              \
        if (dumpD) {                                                                                                   \
            GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO,         \
                                                       GTK_BUTTONS_OK, __VA_ARGS__);                                   \
            g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);                      \
            gtk_widget_set_visible(dialog, true);                                                                      \
        }                                                                                                              \
    }
#endif // DEBUG_LCMS

static constexpr int XPAD = 4;
static constexpr int YPAD = 1;

namespace {

GtkWidget *ink_combo_box_new_with_model(GtkTreeModel *model)
{
    auto combobox = Gtk::make_managed<Gtk::ComboBox>();
    gtk_combo_box_set_model(combobox->gobj(), model);
    return combobox->Gtk::Widget::gobj();
}

size_t maxColorspaceComponentCount = 0;

/**
 * Internal variable to track all known colorspaces.
 */
std::set<cmsUInt32Number> knownColorspaces;

/**
 * Helper function to handle GTK2/GTK3 attachment #ifdef code.
 */
void attachToGridOrTable(GtkWidget *parent, GtkWidget *child, guint left, guint top, guint width, guint height,
                         bool hexpand = false, bool centered = false, guint xpadding = XPAD, guint ypadding = YPAD)
{
    gtk_widget_set_margin_start(child, xpadding);
    gtk_widget_set_margin_end(child, xpadding);
    gtk_widget_set_margin_top(child, ypadding);
    gtk_widget_set_margin_bottom(child, ypadding);

    if (hexpand) {
        gtk_widget_set_hexpand(child, TRUE);
    }

    if (centered) {
        gtk_widget_set_halign(child, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(child, GTK_ALIGN_CENTER);
    }

    gtk_grid_attach(GTK_GRID(parent), child, left, top, width, height);
}

} // namespace

/*
icSigRgbData
icSigCmykData
icSigCmyData
*/
#define SPACE_ID_RGB 0
#define SPACE_ID_CMY 1
#define SPACE_ID_CMYK 2

colorspace::Component::Component()
    : name()
    , tip()
    , scale(1)
{
}

colorspace::Component::Component(std::string name, std::string tip, guint scale)
    : name(std::move(name))
    , tip(std::move(tip))
    , scale(scale)
{
}

static cmsUInt16Number *getScratch()
{
    // bytes per pixel * input channels * width
    static std::array<cmsUInt16Number, 4 * 1024> scritch;
    return scritch.data();
}

std::vector<colorspace::Component> colorspace::getColorSpaceInfo(uint32_t space)
{
    static std::map<cmsUInt32Number, std::vector<Component> > sets;
    if (sets.empty()) {
        sets[cmsSigXYZData].emplace_back("_X", "X", 2); //  TYPE_XYZ_16
        sets[cmsSigXYZData].emplace_back("_Y", "Y", 1);
        sets[cmsSigXYZData].emplace_back("_Z", "Z", 2);

        sets[cmsSigLabData].emplace_back("_L", "L", 100); // TYPE_Lab_16
        sets[cmsSigLabData].emplace_back("_a", "a", 256);
        sets[cmsSigLabData].emplace_back("_b", "b", 256);

        // cmsSigLuvData

        sets[cmsSigYCbCrData].emplace_back("_Y", "Y", 1); // TYPE_YCbCr_16
        sets[cmsSigYCbCrData].emplace_back("C_b", "Cb", 1);
        sets[cmsSigYCbCrData].emplace_back("C_r", "Cr", 1);

        sets[cmsSigYxyData].emplace_back("_Y", "Y", 1); // TYPE_Yxy_16
        sets[cmsSigYxyData].emplace_back("_x", "x", 1);
        sets[cmsSigYxyData].emplace_back("y", "y", 1);

        sets[cmsSigRgbData].emplace_back(_("_R:"), _("Red"), 1); // TYPE_RGB_16
        sets[cmsSigRgbData].emplace_back(_("_G:"), _("Green"), 1);
        sets[cmsSigRgbData].emplace_back(_("_B:"), _("Blue"), 1);

        sets[cmsSigGrayData].emplace_back(_("G:"), _("Gray"), 1); // TYPE_GRAY_16

        sets[cmsSigHsvData].emplace_back(_("_H:"), _("Hue"), 360); // TYPE_HSV_16
        sets[cmsSigHsvData].emplace_back(_("_S:"), _("Saturation"), 1);
        sets[cmsSigHsvData].emplace_back("_V:", "Value", 1);

        sets[cmsSigHlsData].emplace_back(_("_H:"), _("Hue"), 360); // TYPE_HLS_16
        sets[cmsSigHlsData].emplace_back(_("_L:"), _("Lightness"), 1);
        sets[cmsSigHlsData].emplace_back(_("_S:"), _("Saturation"), 1);

        sets[cmsSigCmykData].emplace_back(_("_C:"), _("Cyan"), 1); // TYPE_CMYK_16
        sets[cmsSigCmykData].emplace_back(_("_M:"), _("Magenta"), 1);
        sets[cmsSigCmykData].emplace_back(_("_Y:"), _("Yellow"), 1);
        sets[cmsSigCmykData].emplace_back(_("_K:"), _("Black"), 1);

        sets[cmsSigCmyData].emplace_back(_("_C:"), _("Cyan"), 1); // TYPE_CMY_16
        sets[cmsSigCmyData].emplace_back(_("_M:"), _("Magenta"), 1);
        sets[cmsSigCmyData].emplace_back(_("_Y:"), _("Yellow"), 1);

        for (auto & set : sets) {
            knownColorspaces.insert(set.first);
            maxColorspaceComponentCount = std::max(maxColorspaceComponentCount, set.second.size());
        }
    }

    std::vector<Component> target;

    if (sets.find(space) != sets.end()) {
        target = sets[space];
    }
    return target;
}

std::vector<colorspace::Component> colorspace::getColorSpaceInfo(Inkscape::ColorProfile *prof)
{
    return getColorSpaceInfo(asICColorSpaceSig(prof->getColorSpace()));
}

namespace Inkscape::UI::Widget {

/**
 * Class containing the parts for a single color component's UI presence.
 */
class ComponentUI final {
public:
    explicit ComponentUI(colorspace::Component component = {})
        : _component(std::move(component))
        , _adj(nullptr)
        , _slider(nullptr)
        , _btn(nullptr)
        , _label(nullptr)
        , _map(4 * 1024, 0xFF)
    {
    }

    colorspace::Component _component;
    Glib::RefPtr<Gtk::Adjustment> _adj; // Component adjustment
    Inkscape::UI::Widget::ColorSlider *_slider;
    GtkWidget *_btn;   // spinbutton
    GtkWidget *_label; // Label
    std::vector<unsigned char> _map;
};

/**
 * Class that implements the internals of the selector.
 */
class ColorICCSelectorImpl final {
public:
    ColorICCSelectorImpl(ColorICCSelector *owner, SelectedColor &color);

    void _adjustmentChanged(Glib::RefPtr<Gtk::Adjustment> const &adjustment);

    void _sliderGrabbed();
    void _sliderReleased();
    void _sliderChanged();

    static void _profileSelected(GtkWidget *src, gpointer data);
    static void _fixupHit(GtkWidget *src, gpointer data);

    void _setProfile(const std::string &profile);
    void _switchToProfile(gchar const *name);
    
    void _updateSliders(gint ignore);
    void _profilesChanged(std::string const &name);

    ColorICCSelector *_owner;
    SelectedColor &_color;

    gboolean _updating : 1;
    gboolean _dragging : 1;

    guint32 _fixupNeeded;
    GtkWidget *_fixupBtn;
    GtkWidget *_profileSel;

    std::vector<ComponentUI> _compUI;

    Glib::RefPtr<Gtk::Adjustment> _adj; // Channel adjustment
    Inkscape::UI::Widget::ColorSlider *_slider;
    GtkWidget *_sbtn;  // Spinbutton
    GtkWidget *_label; // Label

    std::string _profileName;
    Inkscape::ColorProfile *_prof;
    guint _profChannelCount;
    gulong _profChangedID;
};

ColorICCSelector::ColorICCSelector(SelectedColor &color, bool no_alpha)
    : _impl{std::make_unique<ColorICCSelectorImpl>(this, color)}
{
    init(no_alpha);
    color.signal_changed.connect(sigc::mem_fun(*this, &ColorICCSelector::_colorChanged));
    color.signal_icc_changed.connect(sigc::mem_fun(*this, &ColorICCSelector::_colorChanged));
}

ColorICCSelector::~ColorICCSelector() = default;

ColorICCSelectorImpl::ColorICCSelectorImpl(ColorICCSelector *owner, SelectedColor &color)
    : _owner(owner)
    , _color(color)
    , _updating(FALSE)
    , _dragging(FALSE)
    , _fixupNeeded(0)
    , _fixupBtn(nullptr)
    , _profileSel(nullptr)
    , _compUI()
    , _adj(nullptr)
    , _slider(nullptr)
    , _sbtn(nullptr)
    , _label(nullptr)
    , _profileName()
    , _prof(nullptr)
    , _profChannelCount(0)
    , _profChangedID(0)
{
}

void ColorICCSelector::init(bool no_alpha)
{
    gint row = 0;

    _impl->_updating = FALSE;
    _impl->_dragging = FALSE;

    GtkWidget *t = GTK_WIDGET(gobj());

    _impl->_compUI.clear();

    // Create components
    row = 0;

    _impl->_fixupBtn = gtk_button_new_with_label(_("Fix"));
    g_signal_connect(G_OBJECT(_impl->_fixupBtn), "clicked", G_CALLBACK(ColorICCSelectorImpl::_fixupHit),
                     _impl.get());
    gtk_widget_set_sensitive(_impl->_fixupBtn, FALSE);
    gtk_widget_set_tooltip_text(_impl->_fixupBtn, _("Fix RGB fallback to match icc-color() value."));
    gtk_widget_set_visible(_impl->_fixupBtn, true);

    attachToGridOrTable(t, _impl->_fixupBtn, 0, row, 1, 1);

    // Combobox and store with 2 columns : label (0) and full name (1)
    GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    _impl->_profileSel = ink_combo_box_new_with_model(GTK_TREE_MODEL(store));

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(_impl->_profileSel), renderer, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(_impl->_profileSel), renderer, "text", 0, nullptr);

    GtkTreeIter iter;
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter, 0, _("<none>"), 1, "null", -1);

    gtk_widget_set_visible(_impl->_profileSel, true);
    gtk_combo_box_set_active(GTK_COMBO_BOX(_impl->_profileSel), 0);

    attachToGridOrTable(t, _impl->_profileSel, 1, row, 1, 1);

    _impl->_profChangedID = g_signal_connect(G_OBJECT(_impl->_profileSel), "changed",
                                             G_CALLBACK(ColorICCSelectorImpl::_profileSelected), _impl.get());

    row++;

    // populate the data for colorspaces and channels:
    std::vector<colorspace::Component> things = colorspace::getColorSpaceInfo(cmsSigRgbData);

    for (size_t i = 0; i < maxColorspaceComponentCount; i++) {
        if (i < things.size()) {
            _impl->_compUI.emplace_back(things[i]);
        }
        else {
            _impl->_compUI.emplace_back();
        }

        auto const labelStr = i < things.size() ? things[i].name.c_str() : "";
        _impl->_compUI[i]._label = gtk_label_new_with_mnemonic(labelStr);

        gtk_widget_set_halign(_impl->_compUI[i]._label, GTK_ALIGN_END);
        gtk_widget_set_visible(_impl->_compUI[i]._label, true);

        attachToGridOrTable(t, _impl->_compUI[i]._label, 0, row, 1, 1);

        // Adjustment
        guint scaleValue = _impl->_compUI[i]._component.scale;
        gdouble step = static_cast<gdouble>(scaleValue) / 100.0;
        gdouble page = static_cast<gdouble>(scaleValue) / 10.0;
        gint digits = (step > 0.9) ? 0 : 2;
        _impl->_compUI[i]._adj = Gtk::Adjustment::create(0.0, 0.0, scaleValue, step, page, page);

        // Slider
        _impl->_compUI[i]._slider =
            Gtk::make_managed<Inkscape::UI::Widget::ColorSlider>(_impl->_compUI[i]._adj);
        _impl->_compUI[i]._slider->set_tooltip_text((i < things.size()) ? things[i].tip.c_str() : "");
        _impl->_compUI[i]._slider->set_visible(true);

        attachToGridOrTable(t, _impl->_compUI[i]._slider->Gtk::Widget::gobj(), 1, row, 1, 1, true);

        auto const spinbutton = Gtk::make_managed<Gtk::SpinButton>(_impl->_compUI[i]._adj, step, digits);
        _impl->_compUI[i]._btn = spinbutton->Gtk::Widget::gobj();
        spinbutton->set_tooltip_text(i < things.size() ? things[i].tip.c_str() : "");
        sp_dialog_defocus_on_enter(*spinbutton);
        gtk_label_set_mnemonic_widget(GTK_LABEL(_impl->_compUI[i]._label), _impl->_compUI[i]._btn);
        gtk_widget_set_visible(_impl->_compUI[i]._btn, true);

        attachToGridOrTable(t, _impl->_compUI[i]._btn, 2, row, 1, 1, false, true);

        // Signals
        _impl->_compUI[i]._adj->signal_value_changed().connect(sigc::bind(sigc::mem_fun(*_impl, &ColorICCSelectorImpl::_adjustmentChanged), _impl->_compUI[i]._adj));

        _impl->_compUI[i]._slider->signal_grabbed.connect(sigc::mem_fun(*_impl, &ColorICCSelectorImpl::_sliderGrabbed));
        _impl->_compUI[i]._slider->signal_released.connect(
            sigc::mem_fun(*_impl, &ColorICCSelectorImpl::_sliderReleased));
        _impl->_compUI[i]._slider->signal_value_changed.connect(
            sigc::mem_fun(*_impl, &ColorICCSelectorImpl::_sliderChanged));

        row++;
    }

    // Label
    _impl->_label = gtk_label_new_with_mnemonic(_("_A:"));

    gtk_widget_set_halign(_impl->_label, GTK_ALIGN_END);
    gtk_widget_set_visible(_impl->_label, true);

    attachToGridOrTable(t, _impl->_label, 0, row, 1, 1);

    // Adjustment
    _impl->_adj = Gtk::Adjustment::create(0.0, 0.0, 100.0, 1.0, 10.0, 10.0);

    // Slider
    _impl->_slider = Gtk::make_managed<Inkscape::UI::Widget::ColorSlider>(_impl->_adj);
    _impl->_slider->set_tooltip_text(_("Alpha (opacity)"));
    _impl->_slider->set_visible(true);

    attachToGridOrTable(t, _impl->_slider->Gtk::Widget::gobj(), 1, row, 1, 1, true);

    _impl->_slider->setColors(SP_RGBA32_F_COMPOSE(1.0, 1.0, 1.0, 0.0), SP_RGBA32_F_COMPOSE(1.0, 1.0, 1.0, 0.5),
                              SP_RGBA32_F_COMPOSE(1.0, 1.0, 1.0, 1.0));

    // Spinbutton
    auto const spinbuttonalpha = Gtk::make_managed<Gtk::SpinButton>(_impl->_adj, 1.0);
    _impl->_sbtn = spinbuttonalpha->Gtk::Widget::gobj();
    spinbuttonalpha->set_tooltip_text(_("Alpha (opacity)"));
    sp_dialog_defocus_on_enter(*spinbuttonalpha);
    gtk_label_set_mnemonic_widget(GTK_LABEL(_impl->_label), _impl->_sbtn);
    gtk_widget_set_visible(_impl->_sbtn, true);

    if (no_alpha) {
        _impl->_slider->set_visible(false);
        gtk_widget_set_visible(_impl->_label, false);
        gtk_widget_set_visible(_impl->_sbtn, false);
    }

    attachToGridOrTable(t, _impl->_sbtn, 2, row, 1, 1, false, true);

    // Signals
    _impl->_adj->signal_value_changed().connect(sigc::bind(sigc::mem_fun(*_impl, &ColorICCSelectorImpl::_adjustmentChanged), _impl->_adj));

    _impl->_slider->signal_grabbed.connect(sigc::mem_fun(*_impl, &ColorICCSelectorImpl::_sliderGrabbed));
    _impl->_slider->signal_released.connect(sigc::mem_fun(*_impl, &ColorICCSelectorImpl::_sliderReleased));
    _impl->_slider->signal_value_changed.connect(sigc::mem_fun(*_impl, &ColorICCSelectorImpl::_sliderChanged));

    gtk_widget_set_visible(t, true);
}

void ColorICCSelectorImpl::_fixupHit(GtkWidget * /*src*/, gpointer data)
{
    ColorICCSelectorImpl *self = reinterpret_cast<ColorICCSelectorImpl *>(data);
    gtk_widget_set_sensitive(self->_fixupBtn, FALSE);
    self->_adjustmentChanged(self->_compUI[0]._adj);
}

void ColorICCSelectorImpl::_profileSelected(GtkWidget * /*src*/, gpointer data)
{
    ColorICCSelectorImpl *self = reinterpret_cast<ColorICCSelectorImpl *>(data);

    GtkTreeIter iter;
    if (gtk_combo_box_get_active_iter(GTK_COMBO_BOX(self->_profileSel), &iter)) {
        GtkTreeModel *store = gtk_combo_box_get_model(GTK_COMBO_BOX(self->_profileSel));
        gchar *name = nullptr;

        gtk_tree_model_get(store, &iter, 1, &name, -1);
        self->_switchToProfile(name);
        gtk_widget_set_tooltip_text(self->_profileSel, name);

        g_free(name);
    }
}

void ColorICCSelectorImpl::_switchToProfile(gchar const *name)
{
    bool dirty = false;
    SPColor tmp(_color.color());

    if (name && std::string(name) != "null") {
        if (tmp.getColorProfile() == name) {
#ifdef DEBUG_LCMS
            g_message("Already at name [%s]", name);
#endif // DEBUG_LCMS
        }
        else {
#ifdef DEBUG_LCMS
            g_message("Need to switch to profile [%s]", name);
#endif // DEBUG_LCMS

            if (auto newProf = SP_ACTIVE_DOCUMENT->getProfileManager().find(name)) {
                cmsHTRANSFORM trans = newProf->getTransfFromSRGB8();
                if (trans) {
                    guint32 val = _color.color().toRGBA32(0);
                    guchar pre[4] = {
                        static_cast<guchar>(SP_RGBA32_R_U(val)),
                        static_cast<guchar>(SP_RGBA32_G_U(val)),
                        static_cast<guchar>(SP_RGBA32_B_U(val)),
                        255};
#ifdef DEBUG_LCMS
                    g_message("Shoving in [%02x] [%02x] [%02x]", pre[0], pre[1], pre[2]);
#endif // DEBUG_LCMS
                    cmsUInt16Number post[4] = { 0, 0, 0, 0 };
                    cmsDoTransform(trans, pre, post, 1);
#ifdef DEBUG_LCMS
                    g_message("got on out [%04x] [%04x] [%04x] [%04x]", post[0], post[1], post[2], post[3]);
#endif // DEBUG_LCMS
                    guint count = cmsChannelsOf(asICColorSpaceSig(newProf->getColorSpace()));

                    std::vector<colorspace::Component> things =
                        colorspace::getColorSpaceInfo(asICColorSpaceSig(newProf->getColorSpace()));

                    std::vector<double> colors;
                    for (guint i = 0; i < count; i++) {
                        gdouble val =
                            (((gdouble)post[i]) / 65535.0) * (gdouble)((i < things.size()) ? things[i].scale : 1);
#ifdef DEBUG_LCMS
                        g_message("     scaled %d by %d to be %f", i, ((i < things.size()) ? things[i].scale : 1), val);
#endif // DEBUG_LCMS
                        colors.push_back(val);
                    }

                    cmsHTRANSFORM retrans = newProf->getTransfToSRGB8();
                    if (retrans) {
                        cmsDoTransform(retrans, post, pre, 1);
#ifdef DEBUG_LCMS
                        g_message("  back out [%02x] [%02x] [%02x]", pre[0], pre[1], pre[2]);
#endif // DEBUG_LCMS
                        tmp.set(SP_RGBA32_U_COMPOSE(pre[0], pre[1], pre[2], 0xff));
                        tmp.setColorProfile(newProf);
                        tmp.setColors(std::move(colors));
                    } else {
                        g_warning("Couldn't get sRGB from color profile.");
                    }

                    dirty = true;
                }
            }
        }
    }
    else {
#ifdef DEBUG_LCMS
        g_message("NUKE THE ICC");
#endif // DEBUG_LCMS
        if (tmp.hasColorProfile()) {
            tmp.unsetColorProfile();
            dirty = true;
            _fixupHit(nullptr, this);
        }
        else {
#ifdef DEBUG_LCMS
            g_message("No icc to nuke");
#endif // DEBUG_LCMS
        }
    }

    if (dirty) {
#ifdef DEBUG_LCMS
        g_message("+----------------");
        g_message("+   new color is [%s]", tmp.toString().c_str());
#endif // DEBUG_LCMS
        _setProfile(tmp.getColorProfile());
        _color.setColor(tmp);
#ifdef DEBUG_LCMS
        g_message("+_________________");
#endif // DEBUG_LCMS
    }
}

struct _cmp {
  bool operator()(const SPObject * const & a, const SPObject * const & b)
  {
    const Inkscape::ColorProfile &a_prof = reinterpret_cast<const Inkscape::ColorProfile &>(*a);
    const Inkscape::ColorProfile &b_prof = reinterpret_cast<const Inkscape::ColorProfile &>(*b);
    gchar *a_name_casefold = g_utf8_casefold(a_prof.name, -1 );
    gchar *b_name_casefold = g_utf8_casefold(b_prof.name, -1 );
    int result = g_strcmp0(a_name_casefold, b_name_casefold);
    g_free(a_name_casefold);
    g_free(b_name_casefold);
    return result < 0;
  }
};

template <typename From, typename To>
struct static_caster { To * operator () (From * value) const { return static_cast<To *>(value); } };

void ColorICCSelectorImpl::_profilesChanged(std::string const &name)
{
    GtkComboBox *combo = GTK_COMBO_BOX(_profileSel);

    g_signal_handler_block(G_OBJECT(_profileSel), _profChangedID);

    GtkListStore *store = GTK_LIST_STORE(gtk_combo_box_get_model(combo));
    gtk_list_store_clear(store);

    GtkTreeIter iter;
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter, 0, _("<none>"), 1, "null", -1);

    gtk_combo_box_set_active(combo, 0);

    int index = 1;
    std::vector<SPObject *> current = SP_ACTIVE_DOCUMENT->getResourceList("iccprofile");

    std::set<Inkscape::ColorProfile *> _current;
    std::transform(current.begin(),
                   current.end(),
                   std::inserter(_current, _current.begin()),
                   static_caster<SPObject, Inkscape::ColorProfile>());

    for (auto &it: _current) {
        Inkscape::ColorProfile *prof = it;

        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, ink_ellipsize_text(prof->name, 25).c_str(), 1, prof->name, -1);

        if (name == prof->name) {
            gtk_combo_box_set_active(combo, index);
            gtk_widget_set_tooltip_text(_profileSel, prof->name);
        }

        index++;
    }

    g_signal_handler_unblock(G_OBJECT(_profileSel), _profChangedID);
}

void ColorICCSelector::on_show()
{
    Gtk::Grid::on_show();
    _colorChanged();
}

// Helpers for setting color value

void ColorICCSelector::_colorChanged()
{
    _impl->_updating = TRUE;
    auto color = _impl->_color.color();
    auto name = color.getColorProfile();

#ifdef DEBUG_LCMS
    g_message("/^^^^^^^^^  %p::_colorChanged(%08x:%s)", this, color.toRGBA32(_impl->_color.alpha()), name.c_str());
#endif // DEBUG_LCMS

    _impl->_profilesChanged(name);
    ColorScales<>::setScaled(_impl->_adj, _impl->_color.alpha());

    _impl->_setProfile(name);
    _impl->_fixupNeeded = 0;
    gtk_widget_set_sensitive(_impl->_fixupBtn, FALSE);

    if (_impl->_prof) {
        if (_impl->_prof->getTransfToSRGB8()) {
            cmsUInt16Number tmp[4];
            for (guint i = 0; i < _impl->_profChannelCount; i++) {
                auto colors = color.getColors();
                gdouble val = 0.0;
                if (colors.size() > i) {
                    auto scale = static_cast<double>(_impl->_compUI[i]._component.scale);
                    if (_impl->_compUI[i]._component.scale == 256) {
                        val = (colors[i] + 128.0) / scale;
                    }
                    else {
                        val = colors[i] / scale;
                    }
                }
                tmp[i] = val * 0x0ffff;
            }
            guchar post[4] = { 0, 0, 0, 0 };
            cmsHTRANSFORM trans = _impl->_prof->getTransfToSRGB8();
            if (trans) {
                cmsDoTransform(trans, tmp, post, 1);
                guint32 other = SP_RGBA32_U_COMPOSE(post[0], post[1], post[2], 255);
                if (other != color.toRGBA32(255)) {
                    _impl->_fixupNeeded = other;
                    gtk_widget_set_sensitive(_impl->_fixupBtn, TRUE);
#ifdef DEBUG_LCMS
                    g_message("Color needs to change 0x%06x to 0x%06x", color.toRGBA32(255) >> 8, other >> 8);
#endif // DEBUG_LCMS
                }
            }
        }
    }
    _impl->_updateSliders(-1);

    _impl->_updating = FALSE;
#ifdef DEBUG_LCMS
    g_message("\\_________  %p::_colorChanged()", this);
#endif // DEBUG_LCMS
}

void ColorICCSelectorImpl::_setProfile(const std::string &profile)
{
#ifdef DEBUG_LCMS
    g_message("/^^^^^^^^^  %p::_setProfile(%s)", this, profile.c_str());
#endif // DEBUG_LCMS
    bool profChanged = false;
    if (_prof && _profileName != profile) {
        // Need to clear out the prior one
        profChanged = true;
        _profileName.clear();
        _prof = nullptr;
        _profChannelCount = 0;
    } else if (!_prof && !profile.empty()) {
        profChanged = true;
    }

    for (auto & i : _compUI) {
        gtk_widget_set_visible(i._label, false);
        i._slider->set_visible(false);
        gtk_widget_set_visible(i._btn, false);
    }

    if (!profile.empty()) {
        _prof = SP_ACTIVE_DOCUMENT->getProfileManager().find(profile.c_str());
        if (_prof && (asICColorProfileClassSig(_prof->getProfileClass()) != cmsSigNamedColorClass)) {
            _profChannelCount = _prof->getChannelCount();

            if (profChanged) {
                std::vector<colorspace::Component> things =
                    colorspace::getColorSpaceInfo(asICColorSpaceSig(_prof->getColorSpace()));
                for (size_t i = 0; (i < things.size()) && (i < _profChannelCount); ++i) {
                    _compUI[i]._component = things[i];
                }

                for (guint i = 0; i < _profChannelCount; i++) {
                    gtk_label_set_text_with_mnemonic(GTK_LABEL(_compUI[i]._label),
                                                     (i < things.size()) ? things[i].name.c_str() : "");

                    _compUI[i]._slider->set_tooltip_text((i < things.size()) ? things[i].tip.c_str() : "");
                    gtk_widget_set_tooltip_text(_compUI[i]._btn, (i < things.size()) ? things[i].tip.c_str() : "");

                    _compUI[i]._slider->setColors(SPColor(0.0, 0.0, 0.0).toRGBA32(0xff),
                                                  SPColor(0.5, 0.5, 0.5).toRGBA32(0xff),
                                                  SPColor(1.0, 1.0, 1.0).toRGBA32(0xff));
                    gtk_widget_set_visible(_compUI[i]._label, true);
                    _compUI[i]._slider->set_visible(true);
                    gtk_widget_set_visible(_compUI[i]._btn, true);
                }
                for (size_t i = _profChannelCount; i < _compUI.size(); i++) {
                    gtk_widget_set_visible(_compUI[i]._label, false);
                    _compUI[i]._slider->set_visible(false);
                    gtk_widget_set_visible(_compUI[i]._btn, false);
                }
            }
        }
        else {
            // Give up for now on named colors
            _prof = nullptr;
        }
    }

#ifdef DEBUG_LCMS
    g_message("\\_________  %p::_setProfile()", this);
#endif // DEBUG_LCMS
}

void ColorICCSelectorImpl::_updateSliders(gint ignore)
{
    _slider->set_sensitive(false);

    if (_color.color().hasColorProfile()) {
        auto colors = _color.color().getColors();
        if (colors.size() != _profChannelCount) {
            g_warning("Can't set profile with %d colors to %d channels", (int)colors.size(), _profChannelCount);
        }
        for (guint i = 0; i < _profChannelCount; i++) {
            double val = 0.0;
            auto scale = static_cast<double>(_compUI[i]._component.scale);
            if (_compUI[i]._component.scale == 256) {
                val = (colors[i] + 128.0) / scale;
            } else {
                val = colors[i] / scale;
            }
            _compUI[i]._adj->set_value(val);
        }

        if (_prof) {
            _slider->set_sensitive(true);

            if (_prof->getTransfToSRGB8()) {
                for (guint i = 0; i < _profChannelCount; i++) {
                    if (static_cast<gint>(i) != ignore) {
                        cmsUInt16Number *scratch = getScratch();
                        cmsUInt16Number filler[4] = { 0, 0, 0, 0 };
                        for (guint j = 0; j < _profChannelCount; j++) {
                            filler[j] = 0x0ffff * ColorScales<>::getScaled(_compUI[j]._adj);
                        }

                        cmsUInt16Number *p = scratch;
                        for (guint x = 0; x < 1024; x++) {
                            for (guint j = 0; j < _profChannelCount; j++) {
                                if (j == i) {
                                    *p++ = x * 0x0ffff / 1024;
                                }
                                else {
                                    *p++ = filler[j];
                                }
                            }
                        }

                        cmsHTRANSFORM trans = _prof->getTransfToSRGB8();
                        if (trans) {
                            cmsDoTransform(trans, scratch, _compUI[i]._map.data(), 1024);
                            if (_compUI[i]._slider) {
                                _compUI[i]._slider->setMap(_compUI[i]._map.data());
                            }
                        }
                    }
                }
            }
        }
    }

    guint32 start = _color.color().toRGBA32(0x00);
    guint32 mid = _color.color().toRGBA32(0x7f);
    guint32 end = _color.color().toRGBA32(0xff);

    _slider->setColors(start, mid, end);
}

void ColorICCSelectorImpl::_adjustmentChanged(Glib::RefPtr<Gtk::Adjustment> const &adjustment)
{
#ifdef DEBUG_LCMS
    g_message("/^^^^^^^^^  %p::_adjustmentChanged()", this);
#endif // DEBUG_LCMS

    ColorICCSelector *iccSelector = _owner;
    if (iccSelector->_impl->_updating) {
        return;
    }

    iccSelector->_impl->_updating = TRUE;

    gint match = -1;

    SPColor newColor(iccSelector->_impl->_color.color());
    gfloat scaled = ColorScales<>::getScaled(iccSelector->_impl->_adj);
    if (iccSelector->_impl->_adj == adjustment) {
#ifdef DEBUG_LCMS
        g_message("ALPHA");
#endif // DEBUG_LCMS
    }
    else {
        for (size_t i = 0; i < iccSelector->_impl->_compUI.size(); i++) {
            if (iccSelector->_impl->_compUI[i]._adj == adjustment) {
                match = i;
                break;
            }
        }
        if (match >= 0) {
#ifdef DEBUG_LCMS
            g_message(" channel %d", match);
#endif // DEBUG_LCMS
        }

        cmsUInt16Number tmp[4];
        for (guint i = 0; i < 4; i++) {
            tmp[i] = ColorScales<>::getScaled(iccSelector->_impl->_compUI[i]._adj) * 0x0ffff;
        }
        guchar post[4] = { 0, 0, 0, 0 };

        cmsHTRANSFORM trans = iccSelector->_impl->_prof->getTransfToSRGB8();
        if (trans) {
            cmsDoTransform(trans, tmp, post, 1);
        }

        // Set the sRGB version of the color first.
        guint32 prior = iccSelector->_impl->_color.color().toRGBA32(255);
        guint32 newer = SP_RGBA32_U_COMPOSE(post[0], post[1], post[2], 255);

        if (prior != newer) {
#ifdef DEBUG_LCMS
            g_message("Transformed color from 0x%08x to 0x%08x", prior, newer);
            g_message("      ~~~~ FLIP");
#endif // DEBUG_LCMS

            // Be careful to always set() and then setColors() to retain ICC data.
            newColor.set(newer);
            if (iccSelector->_impl->_color.color().hasColorProfile()) {
                std::vector<double> colors;
                for (guint i = 0; i < iccSelector->_impl->_profChannelCount; i++) {
                    double val = ColorScales<>::getScaled(iccSelector->_impl->_compUI[i]._adj);
                    val *= iccSelector->_impl->_compUI[i]._component.scale;
                    if (iccSelector->_impl->_compUI[i]._component.scale == 256) {
                        val -= 128;
                    }
                    colors.push_back(val);
                }
                newColor.setColors(std::move(colors));
            }
        }
    }
    iccSelector->_impl->_color.setColorAlpha(newColor, scaled);
    iccSelector->_impl->_updateSliders(match);

    iccSelector->_impl->_updating = FALSE;
#ifdef DEBUG_LCMS
    g_message("\\_________  %p::_adjustmentChanged()", this);
#endif // DEBUG_LCMS
}

void ColorICCSelectorImpl::_sliderGrabbed()
{
}

void ColorICCSelectorImpl::_sliderReleased()
{
}

void ColorICCSelectorImpl::_sliderChanged()
{
}

Gtk::Widget *ColorICCSelectorFactory::createWidget(Inkscape::UI::SelectedColor &color, bool no_alpha) const
{
    return Gtk::make_managed<ColorICCSelector>(color, no_alpha);
}

Glib::ustring ColorICCSelectorFactory::modeName() const
{
    return _("CMS");
}

} // namespace Inkscape::UI::widget

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
