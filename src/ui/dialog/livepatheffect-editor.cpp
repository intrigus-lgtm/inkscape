// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A dialog for Live Path Effects (LPE)
 */
/* Authors:
 *   Jabiertxof
 *   Adam Belis (UX/Design)
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "livepatheffect-editor.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <map>
#include <tuple>
#include <glibmm/i18n.h>
#include <glibmm/quark.h>
#include <glibmm/value.h>
#include <giomm/simpleactiongroup.h>
#include <gtkmm/box.h>
#include <gtkmm/builder.h>
#include <gtkmm/button.h>
#include <gtkmm/dragsource.h>
#include <gtkmm/droptarget.h>
#include <gtkmm/expander.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/liststore.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/searchentry2.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/widgetpaintable.h>
#include <sigc++/adaptors/bind.h>
#include <sigc++/adaptors/hide.h>
#include <sigc++/functors/mem_fun.h>

#include "inkscape.h"
#include "live_effects/effect.h"
#include "live_effects/effect-enum.h"
#include "live_effects/lpeobject.h"
#include "live_effects/lpeobject-reference.h"
#include "io/resource.h"
#include "object/sp-flowtext.h"
#include "object/sp-item-group.h"
#include "object/sp-lpe-item.h"
#include "object/sp-path.h"
#include "object/sp-shape.h"
#include "object/sp-text.h"
#include "object/sp-tspan.h"
#include "object/sp-use.h"
#include "selection.h"
#include "svg/svg.h"
#include "ui/builder-utils.h"
#include "ui/column-menu-builder.h"
#include "ui/controller.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/menuize.h"
#include "ui/pack.h"
#include "ui/tools/node-tool.h"
#include "ui/util.h"
#include "ui/widget/custom-tooltip.h"
#include "util/optstr.h"

namespace Inkscape::UI::Dialog {

/*
* * favourites
 */

static constexpr auto favs_path = "/dialogs/livepatheffect/favs";

static bool sp_has_fav(Glib::ustring const &effect)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring favlist = prefs->getString(favs_path);
    return favlist.find(effect) != favlist.npos;
}

static void sp_add_fav(Glib::ustring const &effect)
{
    if (sp_has_fav(effect)) return;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring favlist = prefs->getString(favs_path);
    favlist.append(effect).append(";");
    prefs->setString(favs_path, favlist);
}

static void sp_remove_fav(Glib::ustring effect)
{
    if (!sp_has_fav(effect)) return;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring favlist = prefs->getString(favs_path);
    effect += ";";
    auto const pos = favlist.find(effect);
    if (pos == favlist.npos) return;

    favlist.erase(pos, effect.length());
    prefs->setString(favs_path, favlist);
}

bool sp_set_experimental(bool &_experimental) 
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool experimental = prefs->getBool("/dialogs/livepatheffect/showexperimental", false);
    if (experimental != _experimental) {
        _experimental = experimental;
        return true;
    }
    return false;
}

/*####################
 * Callback functions
 */

void LivePathEffectEditor::selectionChanged(Inkscape::Selection * selection)
{
    if (selection_changed_lock) {
        return;
    }
    onSelectionChanged(selection);
    clearMenu();
}

void LivePathEffectEditor::selectionModified(Inkscape::Selection * selection, guint flags)
{
    current_lpeitem = cast<SPLPEItem>(selection->singleItem());
    _current_use = cast<SPUse>(selection->singleItem());
    if (!selection_changed_lock && current_lpeitem && effectlist != current_lpeitem->getEffectList()) {
        onSelectionChanged(selection);
    } else if (current_lpeitem && current_lperef.first) {
        showParams(current_lperef, false);
    }
    clearMenu();
}

/**
 * Constructor
 */
LivePathEffectEditor::LivePathEffectEditor()
    : DialogBase("/dialogs/livepatheffect", "LivePathEffect"),
    _builder(create_builder("dialog-livepatheffect.glade")),
    LPEListBox(get_widget<Gtk::ListBox>(_builder, "LPEListBox")),
    _LPEContainer(get_widget<Gtk::Box>(_builder, "LPEContainer")),
    _LPEAddContainer(get_widget<Gtk::Box>(_builder, "LPEAddContainer")),
    _LPEParentBox(get_widget<Gtk::ListBox>(_builder, "LPEParentBox")),
    _LPECurrentItem(get_widget<Gtk::Box>(_builder, "LPECurrentItem")),
    _LPESelectionInfo(get_widget<Gtk::Label>(_builder, "LPESelectionInfo")),
    converter(Inkscape::LivePathEffect::LPETypeConverter)
{
    _LPEContainer.signal_map().connect(sigc::mem_fun(*this, &LivePathEffectEditor::map_handler) );

    Controller::add_click(_LPEContainer, [this](Gtk::GestureClick const &, int, double, double)
    {
        dnd = false; // hack to fix DnD freezing expander
        return Gtk::EventSequenceState::NONE;
    });

    setMenu();
    append(_LPEContainer);
    selection_info();

    _lpes_popup.get_entry().set_placeholder_text(_("Add Live Path Effect"));
    _lpes_popup.on_match_selected().connect([this](int const id)
        { onAdd(static_cast<LivePathEffect::EffectType>(id)); });
    _lpes_popup.on_button_press().connect([this]{ setMenu(); });
    _lpes_popup.on_focus().connect([this]{ setMenu(); return true; });
    UI::pack_start(_LPEAddContainer, _lpes_popup);

    sp_set_experimental(_experimental);

    set_visible(true);
}

LivePathEffectEditor::~LivePathEffectEditor()
{
    sp_clear_custom_tooltip();
}

bool
LivePathEffectEditor::can_apply(LivePathEffect::EffectType etype, Glib::ustring const &item_type,
                                bool const has_clip, bool const has_mask)
{
    if (!has_clip && etype == LivePathEffect::POWERCLIP) {
        return false;
    }

    if (!has_mask && etype == LivePathEffect::POWERMASK) {
        return false;
    }

    if (item_type == "group" && !converter.get_on_group(etype)) {
        return false;
    } else if (item_type == "shape" && !converter.get_on_shape(etype)) {
        return false;
    } else if (item_type == "path" && !converter.get_on_path(etype)) {
        return false;
    }

    return true;
}

void align(Gtk::Widget *top, int const spinbutton_width_chars)
{
    auto box = dynamic_cast<Gtk::Box*>(top);
    if (!box) return;
    box->set_spacing(2);

    // traverse container, locate n-th child in each row
    auto for_child_n = [=](int child_index, const std::function<void (Gtk::Widget*)>& action) {
        for (auto const child : UI::get_children(*box)) {
            auto const container = dynamic_cast<Gtk::Box *>(child);
            if (!container) continue;

            container->set_spacing(2);
            auto const children = UI::get_children(*container);
            if (child_index < children.size()) {
                action(children[child_index]);
            }
        }
    };

    // column 0 - labels
    int max_width = 0;
    for_child_n(0, [&](Gtk::Widget* child){
        if (auto label = dynamic_cast<Gtk::Label*>(child)) {
            label->set_xalign(0); // left-align
            int label_width = 0, dummy = 0;
            label->measure(Gtk::Orientation::HORIZONTAL, -1, dummy, label_width, dummy, dummy);
            if (label_width > max_width) {
                max_width = label_width;
            }
        }
    });
    // align
    for_child_n(0, [=](Gtk::Widget* child) {
        if (auto label = dynamic_cast<Gtk::Label*>(child)) {
            label->set_size_request(max_width);
        }
    });

    // column 1 - align spin buttons, if any
    int button_width = 0;
    for_child_n(1, [&](Gtk::Widget* child) {
        if (auto spin = dynamic_cast<Gtk::SpinButton*>(child)) {
            // selected spinbutton size by each LPE default 7
            spin->set_width_chars(spinbutton_width_chars);
            int dummy = 0;
            spin->measure(Gtk::Orientation::HORIZONTAL, -1, dummy, button_width, dummy, dummy);
        } 
    });
    // set min size for comboboxes, if any
    int combo_size = button_width > 0 ? button_width : 50; // match with spinbuttons, or just min of 50px
    for_child_n(1, [=](Gtk::Widget* child) {
        if (auto combo = dynamic_cast<Gtk::ComboBox*>(child)) {
            combo->set_size_request(combo_size);
        }
    });
}

void
LivePathEffectEditor::clearMenu()
{
    sp_clear_custom_tooltip();
    _reload_menu = true;
}

static void
set_visible_icon(Gtk::Button &button, bool const visible)
{
    auto &image = dynamic_cast<Gtk::Image &>(*button.get_child());
    auto const icon_name = visible ? "object-visible-symbolic" : "object-hidden-symbolic";
    image.set_from_icon_name(icon_name);
}

void
LivePathEffectEditor::toggleVisible(Inkscape::LivePathEffect::Effect *lpe , Gtk::Button *visbutton) {
    g_assert(lpe);
    g_assert(visbutton);

    bool visible = g_strcmp0(lpe->getRepr()->attribute("is_visible"), "true") == 0;
    visible = !visible;

    set_visible_icon(*visbutton, visible);

    if (!visible) {
        lpe->getRepr()->setAttribute("is_visible", "false");
    } else {
        lpe->getRepr()->setAttribute("is_visible", "true");
    }

    lpe->doOnVisibilityToggled(current_lpeitem);

    DocumentUndo::done(getDocument(),
                       !visible ? _("Deactivate path effect") :  _("Activate path effect"),
                       INKSCAPE_ICON("dialog-path-effects"));
}

const Glib::ustring& get_category_name(Inkscape::LivePathEffect::LPECategory category) {
    static const std::map<Inkscape::LivePathEffect::LPECategory, Glib::ustring> category_names = {
        { Inkscape::LivePathEffect::LPECategory::Favorites,     _("Favorites")    },
        { Inkscape::LivePathEffect::LPECategory::EditTools,     _("Edit/Tools")   },
        { Inkscape::LivePathEffect::LPECategory::Distort,       _("Distort")      },
        { Inkscape::LivePathEffect::LPECategory::Generate,      _("Generate")     },
        { Inkscape::LivePathEffect::LPECategory::Convert,       _("Convert")      },
        { Inkscape::LivePathEffect::LPECategory::Experimental,  _("Experimental") },
    };
    return category_names.at(category);
}

struct LivePathEffectEditor::LPEMetadata final {
    LivePathEffect::EffectType type{};
    LivePathEffect::LPECategory category{};
    Glib::ustring label, icon_name, tooltip;
    bool sensitive{};
};

// populate popup with lpes and completion list for a search box
void LivePathEffectEditor::add_lpes(Inkscape::UI::Widget::CompletionPopup &popup, bool const symbolic,
                                    std::vector<LPEMetadata> &&lpes)
{
    popup.clear_completion_list();

    // 3-column menu
    // Due to when we rebuild, itʼs not so easy to only populate when the MenuButton is clicked, so
    // we remove existing children. We also want to free them, BUT theyʼre Gtk::managed()d, so… h4x
    // TODO: GTK4: Use MenuButton.set_create_popup_func() to create new menu every time, on demand?

    auto &menu = popup.get_menu();
    menu.delete_all();

    ColumnMenuBuilder<LivePathEffect::LPECategory> builder{menu, 3, Gtk::IconSize::LARGE};
    auto const tie = [](LPEMetadata const &lpe){ return std::tie(lpe.category, lpe.label); };
    std::sort(lpes.begin(), lpes.end(), [=](auto &l, auto &r){ return tie(l) < tie(r); });
    for (auto const &lpe : lpes) {
        // build popup menu
        auto const type = lpe.type;
        int const id = static_cast<int>(type);
        auto const menuitem = builder.add_item(lpe.label, lpe.category, lpe.tooltip, lpe.icon_name,
                                               lpe.sensitive, true, [=, this]{ onAdd(type); });
        menuitem->signal_query_tooltip().connect([=](int x, int y, bool kbd, const Glib::RefPtr<Gtk::Tooltip>& tooltipw){
            return sp_query_custom_tooltip(this, x, y, kbd, tooltipw, id, lpe.tooltip, lpe.icon_name);
        }, false); // before
        if (builder.new_section()) {
            builder.set_section(get_category_name(lpe.category));
        }
    }

    // build completion list
    std::sort(lpes.begin(), lpes.end(), [=](auto &l, auto &r){ return l.label < r.label; });
    for (auto const &lpe: lpes) {
        if (lpe.sensitive) {
            int const id = static_cast<int>(lpe.type);
            Glib::ustring untranslated_label = converter.get_label(lpe.type);
            Glib::ustring untranslated_description = converter.get_description(lpe.type);
            Glib::ustring search = Glib::ustring::compose("%1_%2", untranslated_label, untranslated_description);
            if (lpe.label != untranslated_label) {
                search = Glib::ustring::compose("%1_%2_%3", search, lpe.label, _(converter.get_description(lpe.type).c_str()));
            }
            popup.add_to_completion_list(id, lpe.label , lpe.icon_name + (symbolic ? "-symbolic" : ""), search);
        }
    }

    if (symbolic) {
        menu.add_css_class("symbolic");
    }
}

Glib::ustring LivePathEffectEditor::get_tooltip(LivePathEffect::EffectType const type,
                                                Glib::ustring const &untranslated_label)
{
    Glib::ustring tooltip = _(converter.get_description(type).c_str());
    if (tooltip != untranslated_label) {
        // Translators: %1 is the untranslated label. %2 is the effect type description.
        tooltip = Glib::ustring::compose("[%1] %2", untranslated_label, tooltip);
    }
    return tooltip;
}

void
LivePathEffectEditor::setMenu()
{
    if (!_reload_menu) {
        return;
    }

    _reload_menu = false;

    auto shape = cast<SPShape>(current_lpeitem);
    auto path = cast<SPPath>(current_lpeitem);
    auto group = cast<SPGroup>(current_lpeitem);
    bool has_clip = current_lpeitem && (current_lpeitem->getClipObject() != nullptr);
    bool has_mask = current_lpeitem && (current_lpeitem->getMaskObject() != nullptr);

    Glib::ustring item_type;
    if (group) {
        item_type = "group";
    } else if (path) {
        item_type = "path";
    } else if (shape) {
        item_type = "shape";
    } else if (_current_use) {
        item_type = "use";
    }

    if (!(sp_set_experimental(_experimental) || _item_type != item_type || has_clip != _has_clip || has_mask != _has_mask)) {
        return;
    }
    _item_type = item_type;
    _has_clip = has_clip;
    _has_mask = has_mask;

    bool symbolic = Inkscape::Preferences::get()->getBool("/theme/symbolicIcons", true);

    auto lpes = std::vector<LPEMetadata>{};
    lpes.reserve(converter._length);
    for (int i = 0; i < static_cast<int>(converter._length); ++i) {
        auto const * const data = &converter.data(i);
        auto const &type = data->id;
        auto const &untranslated_label = converter.get_label(type);

        auto category = converter.get_category(type);
        if (sp_has_fav(untranslated_label)) {
            category = Inkscape::LivePathEffect::LPECategory::Favorites;
        }

        if (!_experimental && category == Inkscape::LivePathEffect::LPECategory::Experimental) {
            continue;
        }

        Glib::ustring label = g_dpgettext2(0, "path effect", untranslated_label.c_str());
        auto const &icon = converter.get_icon(type);
        auto tooltip = get_tooltip(type, untranslated_label);
        auto const sensitive = can_apply(type, item_type, has_clip, has_mask);
        lpes.push_back({type, category, std::move(label), icon, std::move(tooltip), sensitive});
    }

    add_lpes(_lpes_popup, symbolic, std::move(lpes));
}

void LivePathEffectEditor::onAdd(LivePathEffect::EffectType etype)
{
    selection_changed_lock = true;
    Glib::ustring key = converter.get_key(etype);
    SPLPEItem *fromclone = clonetolpeitem();
    if (fromclone) {
        current_lpeitem = fromclone;
        _current_use = nullptr;
        if (key == "clone_original") {
            current_lpeitem->getCurrentLPE()->refresh_widgets = true;
            selection_changed_lock = false;
            DocumentUndo::done(getDocument(), _("Create and apply path effect"), INKSCAPE_ICON("dialog-path-effects"));
            return;
        }
    }
    selection_changed_lock = false;
    if (current_lpeitem) {
        LivePathEffect::Effect::createAndApply(key.c_str(), getDocument(), current_lpeitem);
        current_lpeitem->getCurrentLPE()->refresh_widgets = true;
        DocumentUndo::done(getDocument(), _("Create and apply path effect"), INKSCAPE_ICON("dialog-path-effects"));
    }
}

void
LivePathEffectEditor::map_handler()
{
    ensure_size();
}

void
LivePathEffectEditor::selection_info() 
{
    auto selection = getSelection();
    SPItem * selected = nullptr;
    _LPESelectionInfo.set_visible(false);
    if (selection && (selected = selection->singleItem()) ) {
        if (is<SPText>(selected) || is<SPFlowtext>(selected)) {
            _LPESelectionInfo.set_text(_("Text objects do not support Live Path Effects"));
            _LPESelectionInfo.set_visible(true);

            Glib::ustring labeltext = _("Convert text to paths");
            auto const selectbutton = Gtk::make_managed<Gtk::Button>();
            auto const boxc = Gtk::make_managed<Gtk::Box>();
            auto const lbl = Gtk::make_managed<Gtk::Label>(labeltext);
            auto const type = get_shape_image("group", selected->highlight_color());
            UI::pack_start(*boxc, *type, false, false);
            UI::pack_start(*boxc, *lbl, false, false);
            type->set_margin_start(4);
            type->set_margin_end(4);
            selectbutton->set_child(*boxc);
            selectbutton->signal_clicked().connect([=](){
                selection->toCurves();
            });
            _LPEParentBox.append(*selectbutton);

            Glib::ustring labeltext2 = _("Clone");
            auto const selectbutton2 = Gtk::make_managed<Gtk::Button>();
            auto const boxc2 = Gtk::make_managed<Gtk::Box>();
            auto const lbl2 = Gtk::make_managed<Gtk::Label>(labeltext2);
            auto const type2 = get_shape_image("clone", selected->highlight_color());
            UI::pack_start(*boxc2, *type2, false, false);
            UI::pack_start(*boxc2, *lbl2, false, false);
            type2->set_margin_start(4);
            type2->set_margin_end(4);
            selectbutton2->set_child(*boxc2);
            selectbutton2->signal_clicked().connect([=](){
                selection->clone();
            });
            _LPEParentBox.append(*selectbutton2);
        } else if (!is<SPLPEItem>(selected) && !is<SPUse>(selected)) {
            _LPESelectionInfo.set_text(_("Select a path, shape, clone or group"));
            _LPESelectionInfo.set_visible(true);
        } else {
            if (selected->getId()) {
                Glib::ustring labeltext = selected->label() ? selected->label() : selected->getId();
                auto const boxc = Gtk::make_managed<Gtk::Box>();
                auto const lbl = Gtk::make_managed<Gtk::Label>(labeltext);
                lbl->set_ellipsize(Pango::EllipsizeMode::END);
                auto const type = get_shape_image(selected->typeName(), selected->highlight_color());
                UI::pack_start(*boxc, *type, false, false);
                UI::pack_start(*boxc, *lbl, false, false);
                _LPECurrentItem.append(*boxc);
                UI::get_children(_LPECurrentItem).at(0)->set_halign(Gtk::Align::CENTER);
                _LPESelectionInfo.set_visible(false);
            }
            std::vector<std::pair <Glib::ustring, Glib::ustring> > newrootsatellites;
            for (auto root : selected->rootsatellites) {
                auto lpeobj = cast<LivePathEffectObject>(selected->document->getObjectById(root.second));
                Inkscape::LivePathEffect::Effect *lpe = nullptr;
                if (lpeobj) {
                    lpe = lpeobj->get_lpe();
                }
                if (lpe) {
                    const Glib::ustring label = _(converter.get_label(lpe->effectType()).c_str());
                    Glib::ustring labeltext = Glib::ustring::compose(_("Select %1 with %2 LPE"), root.first, label);
                    auto lpeitem = cast<SPLPEItem>(selected->document->getObjectById(root.first));
                    if (lpeitem && lpeitem->getLPEIndex(lpe) != Glib::ustring::npos) {
                        newrootsatellites.emplace_back(root.first, root.second);
                        auto const selectbutton = Gtk::make_managed<Gtk::Button>();
                        auto const boxc = Gtk::make_managed<Gtk::Box>();
                        auto const lbl = Gtk::make_managed<Gtk::Label>(labeltext);
                        auto const type = get_shape_image(selected->typeName(), selected->highlight_color());
                        UI::pack_start(*boxc, *type, false, false);
                        UI::pack_start(*boxc, *lbl, false, false);
                        type->set_margin_start(4);
                        type->set_margin_end(4);
                        selectbutton->set_child(*boxc);
                        selectbutton->signal_clicked().connect([=](){
                            selection->set(lpeitem);
                        });
                        _LPEParentBox.append(*selectbutton);
                    }
                }
            }
            selected->rootsatellites = newrootsatellites;
            _LPEParentBox.set_visible(true);
            _LPECurrentItem.set_visible(true);
        }
    } else if (!selection || selection->isEmpty()) {
        _LPESelectionInfo.set_text(_("Select a path, shape, clone or group"));
        _LPESelectionInfo.set_visible(true);
    } else if (selection->size() > 1) {
        _LPESelectionInfo.set_text(_("Select only one path, shape, clone or group"));
        _LPESelectionInfo.set_visible(true);
    }
}

void
LivePathEffectEditor::onSelectionChanged(Inkscape::Selection *sel)
{
    _reload_menu = true;
    if ( sel && !sel->isEmpty() ) {
        SPItem *item = sel->singleItem();
        if ( item ) {
            auto lpeitem = cast<SPLPEItem>(item);
            _current_use = cast<SPUse>(item);
            if (lpeitem) {
                lpeitem->update_satellites();
                current_lpeitem = lpeitem;
                _LPEAddContainer.set_sensitive(true);
                effect_list_reload(lpeitem);
                return;
            } else if (_current_use) {
                clear_lpe_list();
                _LPEAddContainer.set_sensitive(true);
                 selection_info();
                return;
            }
        }
    }
    _current_use = nullptr;
    current_lpeitem = nullptr;
    _LPEAddContainer.set_sensitive(false);
    clear_lpe_list();
    selection_info();
}

void
LivePathEffectEditor::move_list(int const origin, int const dest)
{
    Inkscape::Selection *sel = getDesktop()->getSelection();

    if ( sel && !sel->isEmpty() ) {
        SPItem *item = sel->singleItem();
        if ( item ) {
            auto lpeitem = cast<SPLPEItem>(item);
            if ( lpeitem ) {
                lpeitem->movePathEffect(origin, dest);
            }
        }
    }
}

void
LivePathEffectEditor::showParams(LPEExpander const &expanderdata, bool const changed)
{
    LivePathEffectObject *lpeobj = expanderdata.second->lpeobject;
   
    if (lpeobj) {
        Inkscape::LivePathEffect::Effect *lpe = lpeobj->get_lpe();
        if (lpe) {
            if (effectwidget && !lpe->refresh_widgets && expanderdata == current_lperef && !changed) {
                return;
            }

            if (effectwidget) {
                current_lperef.first->unset_child(); // deletes effectwidget
                effectwidget = nullptr;
            }

            effectwidget = lpe->newWidget();

            if (UI::get_children(*effectwidget).empty()) {
                auto const label = Gtk::make_managed<Gtk::Label>("", Gtk::Align::START, Gtk::Align::CENTER);
                label->set_markup(_("<small>Without parameters</small>"));
                label->set_margin_top(5);
                label->set_margin_bottom(5);
                label->set_margin_start(5);
                effectwidget = label;
            }

            expanderdata.first->set_child(*effectwidget);
            align(effectwidget, lpe->spinbutton_width_chars);

            // fixme: add resizing of dialog
            lpe->refresh_widgets = false;
            ensure_size();
        } else {
            current_lperef = std::make_pair(nullptr, nullptr);
        }
    } else {
        current_lperef = std::make_pair(nullptr, nullptr);
    }
}

bool
LivePathEffectEditor::on_drop(Gtk::Widget &widget,
                              Glib::ValueBase const &value, int pos_target)
{
    g_assert(dnd);

    int const pos_source = static_cast<Glib::Value<int> const &>(value).get();

    if (pos_target == pos_source) {
        return false;
    }

    if (pos_source > pos_target) {
        if (widget.has_css_class("after")) {
            pos_target ++;
        }
    } else if (pos_source < pos_target) {
        if (widget.has_css_class("before")) {
            pos_target --;
        }
    }

    Gtk::Widget *source = LPEListBox.get_row_at_index(pos_source);

    if (source == &widget) {
        return false;
    }

    g_object_ref(source->gobj());
    LPEListBox.remove(*source);
    LPEListBox.insert(*source, pos_target);
    g_object_unref(source->gobj());

    move_list(pos_source,pos_target);

    return true;
}

static void update_before_after_classes(Gtk::Widget &widget, bool const before)
{
    if (before) {
        widget.remove_css_class("after" );
        widget.add_css_class   ("before");
    } else {
        widget.remove_css_class("before");
        widget.add_css_class   ("after" );
    }
}

/*
 * First clears the effectlist_store, then appends all effects from the effectlist.
 */
void
LivePathEffectEditor::effect_list_reload(SPLPEItem *lpeitem)
{
    clear_lpe_list();
    _LPEExpanders.clear();

    int counter = -1;
    Gtk::Expander *LPEExpanderCurrent = nullptr;
    effectlist = lpeitem->getEffectList();
    int const total = static_cast<int>(effectlist.size());

    if (total > 1) {
        auto &target = Controller::add_drop_target(_LPEContainer,
            { .actions = Gdk::DragAction::MOVE, .types = {G_TYPE_INT}});

        target.signal_drop().connect([this](Glib::ValueBase const &value, double /*x*/, double const y)
        {
            if (!dnd) return false;

            int pos_target = y < 90 ? 0 : UI::get_children(LPEListBox).size() - 1;
            bool const accepted = on_drop(_LPEContainer, value, pos_target);
            dnd = false;
            return accepted;
        }, false); // before

        target.signal_motion().connect([this](double /*x*/, double const y)
        {
            update_before_after_classes(_LPEContainer, y < 90);
            return Gdk::DragAction::MOVE;
        }, false); // before
    }

    Gtk::Button *LPEDrag = nullptr;

    for (auto const &lperef: effectlist) {
        if (!lperef->lpeobject) continue;

        auto const lpe = lperef->lpeobject->get_lpe();
        bool const current = lpeitem->getCurrentLPE() == lpe;
        counter++;

        if (!lpe) continue; // TODO: Should this be a warning or error?

        auto builder = create_builder("dialog-livepatheffect-item.glade");
        auto LPENameLabel     = &get_widget<Gtk::Label>   (builder, "LPENameLabel");
        auto LPEHide          = &get_widget<Gtk::Button>  (builder, "LPEHide");
        auto LPEIconImage     = &get_widget<Gtk::Image>   (builder, "LPEIconImage");
        auto LPEExpanderBox   = &get_widget<Gtk::Box>     (builder, "LPEExpanderBox");
        auto LPEEffect        = &get_widget<Gtk::Box>     (builder, "LPEEffect");
        auto LPEExpander      = &get_widget<Gtk::Expander>(builder, "LPEExpander");
        auto LPEOpenExpander  = &get_widget<Gtk::Box>     (builder, "LPEOpenExpander");
        auto LPEErase         = &get_widget<Gtk::Button>  (builder, "LPEErase");
        LPEDrag               = &get_widget<Gtk::Button>  (builder, "LPEDrag");

        LPEDrag->set_tooltip_text(_("Drag to change position in path effects stack"));
        if (current) {
            LPEExpanderCurrent = LPEExpander;
        }

        auto const effectype = lpe->effectType();
        int const id = static_cast<int>(effectype);
        auto const &untranslated_label = converter.get_label(effectype);
        auto const &icon = converter.get_icon(effectype);
        auto const tooltip = get_tooltip(effectype, untranslated_label);

        LPEIconImage->set_from_icon_name(icon);

        bool const visible = g_strcmp0(lpe->getRepr()->attribute("is_visible"), "true") == 0;
        set_visible_icon(*LPEHide, visible);

        _LPEExpanders.emplace_back(LPEExpander, lperef);
        LPEListBox.append(*LPEEffect);

        LPEDrag->set_name(Glib::ustring::compose("drag_%1", counter));

        LPEExpanderBox->signal_query_tooltip().connect([=](int x, int y, bool kbd, const Glib::RefPtr<Gtk::Tooltip>& tooltipw){
            return sp_query_custom_tooltip(this, x, y, kbd, tooltipw, id, tooltip, icon);
        }, false); // before

        // Add actions used by LPEEffectMenuButton
        add_item_actions(lperef, untranslated_label, *LPEEffect,
                         counter == 0, counter == total - 1);
        // & make it act more like ye olde GtkMenu
        UI::menuize_popover(*get_widget<Gtk::MenuButton>(builder, "LPEEffectMenuButton").get_popover());

        if (total > 1) {
            auto &source = Controller::add_drag_source(*LPEDrag, {.actions = Gdk::DragAction::MOVE});

            // TODO: GTK4: Figure out how to replicate previous 50% transparency. CSS or Paintable?
            LPEEffect->add_css_class("drag-icon");
            source.set_icon(Gtk::WidgetPaintable::create(*LPEEffect), 0, 0);
            LPEEffect->remove_css_class("drag-icon");

            source.signal_drag_begin().connect([this](Glib::RefPtr<Gdk::Drag> const &){
                dnd = true;
            });

            auto row = dynamic_cast<Gtk::ListBoxRow *>(LPEEffect->get_parent());
            g_assert(row);

            source.signal_prepare().connect([=](double, double)
            {
                Glib::Value<int> value;
                value.init(G_TYPE_INT);
                value.set(row->get_index());
                return Gdk::ContentProvider::create(value);
            }, false); // before

            source.signal_drag_end().connect([this](Glib::RefPtr<Gdk::Drag> const &, bool)
            {
                dnd = false;
            });

            auto &target = Controller::add_drop_target(*row,
                { .actions = Gdk::DragAction::MOVE, .types = {G_TYPE_INT}});

            target.signal_drop().connect([=, this](Glib::ValueBase const &value, double, double)
            {
                if (!dnd) return false;

                bool const accepted = on_drop(*row, value, row->get_index());
                dnd = false;
                return accepted;
            }, false); // before

            target.signal_motion().connect([=](double /*x*/, double const y)
            {
                int const half = row->get_allocated_height() / 2;
                update_before_after_classes(*row, y < half);
                return Gdk::DragAction::MOVE;
            }, false); // before
        }

        LPEEffect->set_name("LPEEffectItem");
        LPENameLabel->set_label(g_dpgettext2(nullptr, "path effect",
                                             lperef->lpeobject->get_lpe()->getName().c_str()));

        LPEExpander->property_expanded().signal_changed().connect(sigc::bind(
                sigc::mem_fun(*this, &LivePathEffectEditor::expanded_notify), LPEExpander));

        Controller::add_click(*LPEOpenExpander, [=, this](Gtk::GestureClick const &, int, double, double)
        {
           LPEExpander->set_expanded(!LPEExpander->get_expanded());
           return Gtk::EventSequenceState::CLAIMED;
        }, {}, Controller::Button::left);

        LPEHide->signal_clicked().connect(sigc::bind(sigc::mem_fun(*this, &LivePathEffectEditor::toggleVisible), lpe, LPEHide));
        LPEErase->signal_clicked().connect([=, this](){ removeEffect(LPEExpander);});

        Controller::add_click(*LPEDrag, [this](Gtk::GestureClick const &, int, double const x, double const y)
        {
            dndx = x;
            dndy = y;
            return Gtk::EventSequenceState::NONE;
        });

        if (total > 1) {
            LPEDrag->set_cursor("grab");
        }
    }

    if (counter == 0 && LPEDrag) {
        LPEDrag->set_visible(false);
        LPEDrag->set_tooltip_text("");
    }

    if (LPEExpanderCurrent) {
        _LPESelectionInfo.set_visible(false);
        LPEExpanderCurrent->set_expanded(true);
        if (auto const current_window = dynamic_cast<Gtk::Window *>(LPEExpanderCurrent->get_root())) {
            current_window->set_focus(*LPEExpanderCurrent);
        }
    }

    selection_info();
    ensure_size();
}

void LivePathEffectEditor::expanded_notify(Gtk::Expander *expander) {
    if (updating) {
        return;
    }

    if (!dnd) {
        _freezeexpander = false;
    }

    if (_freezeexpander) {
        _freezeexpander = false;
        return;
    }

    if (dnd) {
        _freezeexpander = true;
        expander->set_expanded(!expander->get_expanded());
        return;
    };

    updating = true;

    if (expander->get_expanded()) {
        for (auto const &w : _LPEExpanders) {
            if (w.first == expander) {
                w.first->set_expanded(true);
                w.first->get_parent()->get_parent()->get_parent()->set_name("currentlpe");
                current_lperef = w;
                current_lpeitem->setCurrentPathEffect(w.second);
                showParams(w, true);
            } else {
                w.first->set_expanded(false);
                w.first->get_parent()->get_parent()->get_parent()->set_name("unactive_lpe");
            }
        }
    }

    auto selection = SP_ACTIVE_DESKTOP->getSelection();
    if (selection && current_lpeitem && !selection->isEmpty()) {
        selection_changed_lock = true;
        selection->clear();
        selection->add(current_lpeitem);
        Inkscape::UI::Tools::sp_update_helperpath(getDesktop());
        selection_changed_lock = false;
    }

    updating = false; 
}

bool 
LivePathEffectEditor::lpeFlatten(PathEffectSharedPtr const &lperef)
{
    current_lpeitem->setCurrentPathEffect(lperef);
    current_lpeitem = current_lpeitem->flattenCurrentPathEffect();
    _current_use = nullptr;
    auto selection = getSelection();
    if (selection && selection->isEmpty() ) {
        selection->add(current_lpeitem);
    }
    DocumentUndo::done(getDocument(), _("Flatten path effect(s)"), INKSCAPE_ICON("dialog-path-effects"));
    return false;
}

void
LivePathEffectEditor::removeEffect(Gtk::Expander * expander) {
    bool reload = current_lperef.first != expander;
    auto current_lperef_tmp = current_lperef;
    for (auto const &w : _LPEExpanders) {
        if (w.first == expander) {
            current_lpeitem->setCurrentPathEffect(w.second);
            current_lpeitem = current_lpeitem->removeCurrentPathEffect(false);
            _current_use = nullptr;
        } 
    }
    if (current_lpeitem) {
        if (reload) {
            current_lpeitem->setCurrentPathEffect(current_lperef_tmp.second);
        }
        effect_list_reload(current_lpeitem);
    }
    DocumentUndo::done(getDocument(), _("Remove path effect"), INKSCAPE_ICON("dialog-path-effects"));
}

/*
 * Clears the effectlist
 */
void
LivePathEffectEditor::clear_lpe_list()
{
    UI::remove_all_children( LPEListBox    );
    UI::remove_all_children(_LPEParentBox  );
    UI::remove_all_children(_LPECurrentItem);
}

SPLPEItem * LivePathEffectEditor::clonetolpeitem()
{
    auto selection = getSelection();
    if (!(selection && !selection->isEmpty())) {
        return nullptr;
    }

    auto use = cast<SPUse>(selection->singleItem());
    if (!use) {
        return nullptr;
    }

    DocumentUndo::ScopedInsensitive tmp(getDocument());
    // item is a clone. do not show effectlist dialog.
    // convert to path, apply CLONE_ORIGINAL LPE, link it to the cloned path

    // test whether linked object is supported by the CLONE_ORIGINAL LPE
    SPItem *orig = use->trueOriginal();
    if (!(is<SPShape>(orig) || is<SPGroup>(orig) || is<SPText>(orig))) {
        return nullptr;
    }

    // select original
    selection->set(orig);

    // delete clone but remember its id and transform
    auto id_copy = Util::to_opt(use->getAttribute("id"));
    auto transform_use = use->get_root_transform();
    use->deleteObject(false);
    use = nullptr;

    // run sp_selection_clone_original_path_lpe
    selection->cloneOriginalPathLPE(true, true, true);

    SPItem *new_item = selection->singleItem();
    // Check that the cloning was successful. We don't want to change the ID of the original referenced path!
    if (new_item && (new_item != orig)) {
        new_item->setAttribute("id", Util::to_cstr(id_copy));
        if (transform_use != Geom::identity()) {
            // update use real transform
            new_item->transform *= transform_use;
            new_item->doWriteTransform(new_item->transform);
            new_item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
        }
        new_item->setAttribute("class", "fromclone");
    }
    
    auto *lpeitem = cast<SPLPEItem>(new_item);
    if (!lpeitem) {
        return nullptr;
    }

    sp_lpe_item_update_patheffect(lpeitem, true, true);
    return lpeitem;
}

/*
* * Actions
 */

static constexpr auto item_action_group_name = "lpe-item";
static const auto item_action_group_quark = Glib::Quark{item_action_group_name};

template <typename Method, typename ...Args>
static void add_action(Glib::RefPtr<Gio::SimpleActionGroup> const &group,
                       Glib::ustring const &name, bool const enable,
                       LivePathEffectEditor &self, Method const method, Args ...args)
{
    auto slot = sigc::hide_return(sigc::bind(sigc::mem_fun(self, method), std::move(args)...));
    auto const action = group->add_action(name, std::move(slot));
    action->set_enabled(enable);
}

void LivePathEffectEditor::add_item_actions(PathEffectSharedPtr const &lperef,
                                            Glib::ustring const &untranslated_label,
                                            Gtk::Widget &item,
                                            bool const is_first, bool const is_last)
{
    using Self = LivePathEffectEditor;
    auto const has_defs = lperef->lpeobject->get_lpe()->hasDefaultParameters();
    auto const has_fav = sp_has_fav(untranslated_label);
    auto group = Gio::SimpleActionGroup::create();
    add_action(group, "duplicate" ,  true    , *this , &Self::do_item_action_undoable, lperef,
               &SPLPEItem::duplicateCurrentPathEffect, _("Duplicate path effect")            );
    add_action(group, "move-up"   , !is_first, *this , &Self::do_item_action_undoable, lperef,
               &SPLPEItem::upCurrentPathEffect       , _("Move path effect up")              );
    add_action(group, "move-down" , !is_last , *this , &Self::do_item_action_undoable, lperef,
               &SPLPEItem::downCurrentPathEffect     , _("Move path effect down")            );
    add_action(group, "flatten"   ,  true    , *this , &Self::lpeFlatten             , lperef);
    add_action(group, "set-def"   , !has_defs, *this , &Self::do_item_action_defaults, lperef,
               &LivePathEffect::Effect::setDefaultParameters                                 );
    add_action(group, "forget-def",  has_defs, *this , &Self::do_item_action_defaults, lperef,
               &LivePathEffect::Effect::resetDefaultParameters                               );
    add_action(group, "set-fav"   , !has_fav , *this , &Self::do_item_action_favorite, lperef,
               untranslated_label , std::ref(item)   , true                                  );
    add_action(group, "unset-fav" ,  has_fav , *this , &Self::do_item_action_favorite, lperef,
               untranslated_label , std::ref(item)   , false                                 );
    item.set_data(item_action_group_quark, &*group);
    item.insert_action_group(item_action_group_name, std::move(group));
}

void LivePathEffectEditor::enable_item_action(Gtk::Widget &item,
                                              Glib::ustring const &action_name, bool const enabled)
{
    auto const data = item.get_data(item_action_group_quark);
    g_assert(data);
    auto &simple_group = *static_cast<Gio::SimpleActionGroup *>(data);
    auto const action = simple_group.lookup_action(action_name);
    auto const simple_action = std::dynamic_pointer_cast<Gio::SimpleAction>(action);
    g_assert(simple_action);
    simple_action->set_enabled(enabled);
}

void LivePathEffectEditor::enable_fav_actions(Gtk::Widget &item, bool const has_fav)
{
    enable_item_action(item, "set-fav"  , !has_fav);
    enable_item_action(item, "unset-fav",  has_fav);
}

void LivePathEffectEditor::do_item_action_undoable(PathEffectSharedPtr const &lperef,
                                                   void (SPLPEItem::* const method)(),
                                                   Glib::ustring const &description)
{
    current_lpeitem->setCurrentPathEffect(lperef);
    (current_lpeitem->*method)();
    effect_list_reload(current_lpeitem);
    DocumentUndo::done(getDocument(), description, INKSCAPE_ICON("dialog-path-effects"));
}

void LivePathEffectEditor::do_item_action_defaults(PathEffectSharedPtr const &lperef,
                                                   void (LivePathEffect::Effect::* const method)())
{
    (lperef->lpeobject->get_lpe()->*method)();
    effect_list_reload(current_lpeitem);
}

void LivePathEffectEditor::do_item_action_favorite(PathEffectSharedPtr const &,
                                                   Glib::ustring const &untranslated_label,
                                                   Gtk::Widget &item, bool const has_fav)
{
    if (has_fav) sp_add_fav(untranslated_label);
    else      sp_remove_fav(untranslated_label);

    enable_fav_actions(item, has_fav);

    _reload_menu = true;
    _item_type = ""; // here we force reload even with the same tipe item selected
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
