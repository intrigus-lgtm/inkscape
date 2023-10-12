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

#ifndef LIVEPATHEFFECTEDITOR_H
#define LIVEPATHEFFECTEDITOR_H

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>

#include "preferences.h"
#include "live_effects/effect-enum.h"
#include "object/sp-lpe-item.h"       // PathEffectList
#include "ui/dialog/dialog-base.h"
#include "ui/widget/completion-popup.h"

namespace Glib {
class ValueBase;
} // namespace Glib

namespace Gtk {
class Box;
class Builder;
class Button;
class Expander;
class Label;
class ListBox;
class ListStore;
class Widget;
} // namespace Gtk

namespace Inkscape {
class Selection;
} // namespace Inkscape

namespace Inkscape::LivePathEffect {
class Effect;
class LPEObjectReference;
} // namespace Inkscape::LivePathEffect

class SPLPEItem;

namespace Inkscape::UI::Dialog {

/*
 * @brief The LivePathEffectEditor class
 */
class LivePathEffectEditor final : public DialogBase
{
public:
    LivePathEffectEditor();
    ~LivePathEffectEditor() final;

    LivePathEffectEditor(LivePathEffectEditor const &d) = delete;
    LivePathEffectEditor operator=(LivePathEffectEditor const &d) = delete;

    static LivePathEffectEditor &getInstance() { return *new LivePathEffectEditor(); }
    void move_list(int origin, int dest);

    using LPEExpander = std::pair<Gtk::Expander *, PathEffectSharedPtr>;
    void showParams(LPEExpander const &expanderdata, bool changed);

private:
    std::vector<LPEExpander> _LPEExpanders;
    bool updating = false;
    SPLPEItem *current_lpeitem = nullptr;
    SPUse *_current_use = nullptr;
    LPEExpander current_lperef = {nullptr, nullptr};
    bool selection_changed_lock = false;
    bool dnd = false;
    Glib::RefPtr<Gtk::Builder> _builder;
    Gtk::ListBox& LPEListBox;
    int dndx = 0;
    int dndy = 0;

    struct LPEMetadata;
    void add_lpes(UI::Widget::CompletionPopup &, bool symbolic, std::vector<LPEMetadata> &&lpes);
    Glib::ustring get_tooltip(LivePathEffect::EffectType, Glib::ustring const &untranslated_label);

    void clear_lpe_list();
    void selectionChanged (Inkscape::Selection *selection                ) final;
    void selectionModified(Inkscape::Selection *selection, unsigned flags) final;
    void onSelectionChanged(Inkscape::Selection *selection);
    void expanded_notify(Gtk::Expander *expander);
    void onAdd(Inkscape::LivePathEffect::EffectType etype);
    bool showWarning(std::string const &msg);
    void toggleVisible(Inkscape::LivePathEffect::Effect *lpe, Gtk::Button *visbutton);
    bool can_apply(LivePathEffect::EffectType, Glib::ustring const &item_type, bool has_clip, bool has_mask);
    void removeEffect(Gtk::Expander * expander);
    [[nodiscard]] bool on_drop(Gtk::Widget &widget,
                               Glib::ValueBase const &value, int pos_target);
    void effect_list_reload(SPLPEItem *lpeitem);
    void selection_info();
    void map_handler();
    void clearMenu();
    void setMenu();
    bool lpeFlatten(PathEffectSharedPtr const &lperef);
    SPLPEItem * clonetolpeitem();

    void add_item_actions(PathEffectSharedPtr const &lperef,
                          Glib::ustring const &, Gtk::Widget &item,
                          bool is_first, bool is_last);
    void enable_item_action(Gtk::Widget &item, Glib::ustring const &name, bool enabled);
    void enable_fav_actions(Gtk::Widget &item, bool has_fav);
    void do_item_action_undoable(PathEffectSharedPtr const &lperef,
                                 void (SPLPEItem::* const method)(),
                                 Glib::ustring const &description);
    void do_item_action_defaults(PathEffectSharedPtr const &lpreref,
                                 void (LivePathEffect::Effect::* const method)());
    void do_item_action_favorite(PathEffectSharedPtr const &lpreref,
                                 Glib::ustring const &untranslated_label, Gtk::Widget &item,
                                 bool has_fav);

    Inkscape::UI::Widget::CompletionPopup _lpes_popup;
    Gtk::Box& _LPEContainer;
    Gtk::Box& _LPEAddContainer;
    Gtk::Label&_LPESelectionInfo;
    Gtk::ListBox&_LPEParentBox;
    Gtk::Box&_LPECurrentItem;
    PathEffectList effectlist;
    Glib::RefPtr<Gtk::ListStore> _LPEList;
    Glib::RefPtr<Gtk::ListStore> _LPEListFilter;
    const LivePathEffect::EnumEffectDataConverter<LivePathEffect::EffectType> &converter;
    Gtk::Widget *effectwidget = nullptr;
    Gtk::Widget *popupwidg = nullptr;
    GtkWidget *currentdrag = nullptr;
    bool _reload_menu = false;
    int _buttons_width = 0;
    bool _freezeexpander = false;
    Glib::ustring _item_type;
    bool _has_clip = false;
    bool _has_mask = false;
    bool _experimental = false;
};

} // namespace Inkscape::UI::Dialog

#endif // LIVEPATHEFFECTEDITOR_H

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
