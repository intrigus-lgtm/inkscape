// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file Object properties dialog.
 */
/*
 * Inkscape, an Open Source vector graphics editor
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright (C) 2012 Kris De Gussem <Kris.DeGussem@gmail.com>
 * c++ version based on former C-version (GPL v2+) with authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Johan Engelen <goejendaagh@zonnet.nl>
 *   Abhishek Sharma
 */

#include "object-properties.h"

#include <glibmm/i18n.h>
#include <gtkmm/grid.h>
#include <gtkmm/eventcontrollerkey.h>

#include "document-undo.h"
#include "document.h"
#include "inkscape.h"
#include "selection.h"
#include "style.h"
#include "style-enums.h"
#include "object/sp-image.h"
#include "ui/icon-names.h"
#include "ui/pack.h"
#include "ui/widget/frame.h"
#include "widgets/sp-attribute-widget.h"

namespace Inkscape::UI::Dialog {

ObjectProperties::ObjectProperties()
    : DialogBase("/dialogs/object/", "ObjectProperties")
    , _blocked(false)
    , _current_item(nullptr)
    , _label_id(_("_ID:"), true)
    , _label_label(_("_Label:"), true)
    , _label_title(_("_Title:"), true)
    , _label_dpi(_("_DPI SVG:"), true)
    , _label_image_rendering(_("_Image Rendering:"), true)
    , _label_color(_("Highlight Color:"), true)
    , _highlight_color(_("Highlight Color"), "", 0xff0000ff, true)
    , _cb_hide(_("_Hide"), true)
    , _cb_lock(_("L_ock"), true)
    , _cb_aspect_ratio(_("Preserve Ratio"), true)
    , _exp_interactivity(_("_Interactivity"), true)
    , _attr_table(Gtk::make_managed<SPAttributeTable>())
{
    //initialize labels for the table at the bottom of the dialog
    _int_attrs.emplace_back("onclick");
    _int_attrs.emplace_back("onmouseover");
    _int_attrs.emplace_back("onmouseout");
    _int_attrs.emplace_back("onmousedown");
    _int_attrs.emplace_back("onmouseup");
    _int_attrs.emplace_back("onmousemove");
    _int_attrs.emplace_back("onfocusin");
    _int_attrs.emplace_back("onfocusout");
    _int_attrs.emplace_back("onload");

    _int_labels.emplace_back("onclick:");
    _int_labels.emplace_back("onmouseover:");
    _int_labels.emplace_back("onmouseout:");
    _int_labels.emplace_back("onmousedown:");
    _int_labels.emplace_back("onmouseup:");
    _int_labels.emplace_back("onmousemove:");
    _int_labels.emplace_back("onfocusin:");
    _int_labels.emplace_back("onfocusout:");
    _int_labels.emplace_back("onload:");

    _init();
}

void ObjectProperties::_init()
{
    set_spacing(0);

    auto const grid_top = Gtk::make_managed<Gtk::Grid>();
    grid_top->set_row_spacing(4);
    grid_top->set_column_spacing(0);
    grid_top->set_margin(4);

    UI::pack_start(*this, *grid_top, false, false);


    /* Create the label for the object id */
    _label_id.set_label(_label_id.get_label() + " ");
    _label_id.set_halign(Gtk::Align::START);
    _label_id.set_valign(Gtk::Align::CENTER);

    /* Create the entry box for the object id */
    _entry_id.set_tooltip_text(_("The id= attribute (only letters, digits, and the characters .-_: allowed)"));
    _entry_id.set_max_length(64);
    _entry_id.set_hexpand();
    _entry_id.set_valign(Gtk::Align::CENTER);

    _label_id.set_mnemonic_widget(_entry_id);

    // pressing enter in the id field is the same as clicking Set:
    _entry_id.signal_activate().connect(sigc::mem_fun(*this, &ObjectProperties::_labelChanged));
    // focus is in the id field initially:
    _entry_id.grab_focus();


    /* Create the label for the object label */
    _label_label.set_label(_label_label.get_label() + " ");
    _label_label.set_halign(Gtk::Align::START);
    _label_label.set_valign(Gtk::Align::CENTER);

    /* Create the entry box for the object label */
    _entry_label.set_tooltip_text(_("A freeform label for the object"));
    _entry_label.set_max_length(256);

    _entry_label.set_hexpand();
    _entry_label.set_valign(Gtk::Align::CENTER);

    _label_label.set_mnemonic_widget(_entry_label);

    // pressing enter in the label field is the same as clicking Set:
    _entry_label.signal_activate().connect(sigc::mem_fun(*this, &ObjectProperties::_labelChanged));


    /* Create the label for the object title */
    _label_title.set_label(_label_title.get_label() + " ");
    _label_title.set_halign(Gtk::Align::START);
    _label_title.set_valign(Gtk::Align::CENTER);

    /* Create the entry box for the object title */
    _entry_title.set_sensitive (FALSE);
    _entry_title.set_max_length (256);

    _entry_title.set_hexpand();
    _entry_title.set_valign(Gtk::Align::CENTER);

    _label_title.set_mnemonic_widget(_entry_title);
    // pressing enter in the label field is the same as clicking Set:
    _entry_title.signal_activate().connect(sigc::mem_fun(*this, &ObjectProperties::_labelChanged));

    _label_color.set_mnemonic_widget(_highlight_color);
    _label_color.set_halign(Gtk::Align::START);
    _highlight_color.connectChanged(sigc::mem_fun(*this, &ObjectProperties::_highlightChanged));

    /* Create the frame for the object description */
    auto const label_desc = Gtk::make_managed<Gtk::Label>(_("_Description:"), true);
    auto const frame_desc = Gtk::make_managed<UI::Widget::Frame>("", FALSE);
    frame_desc->set_label_widget(*label_desc);
    frame_desc->set_padding (0,0,0,0);
    UI::pack_start(*this, *frame_desc, true, true);

    /* Create the text view box for the object description */
    _ft_description.set_margin(4);
    _ft_description.set_sensitive(FALSE);
    frame_desc->add(_ft_description);

    _tv_description.set_wrap_mode(Gtk::WrapMode::WORD);
    _tv_description.get_buffer()->set_text("");
    _ft_description.set_child(_tv_description);
    _tv_description.add_mnemonic_label(*label_desc);

    /* Create the label for the object title */
    _label_dpi.set_label(_label_dpi.get_label() + " ");
    _label_dpi.set_halign(Gtk::Align::START);
    _label_dpi.set_valign(Gtk::Align::CENTER);

    /* Create the entry box for the SVG DPI */
    _spin_dpi.set_digits(2);
    _spin_dpi.set_range(1, 1200);

    _label_dpi.set_mnemonic_widget(_spin_dpi);

    // pressing enter in the label field is the same as clicking Set:
    auto controller = Gtk::EventControllerKey::create();
    controller->signal_key_pressed().connect([this] (unsigned keyval, unsigned, Gdk::ModifierType) {
        if (keyval == GDK_KEY_Return) {
            _labelChanged();
            return true;
        } else {
            return false;
        }
    }, true);
    controller->set_propagation_phase(Gtk::PropagationPhase::BUBBLE);
    _spin_dpi.add_controller(controller);

    /* Image rendering */
    /* Create the label for the object ImageRendering */
    _label_image_rendering.set_label(_label_image_rendering.get_label() + " ");
    _label_image_rendering.set_halign(Gtk::Align::START);
    _label_image_rendering.set_valign(Gtk::Align::CENTER);

    /* Create the combo box text for the 'image-rendering' property  */
    for (unsigned i = 0; enum_image_rendering[i].key; ++i) {
        _combo_image_rendering.append(enum_image_rendering[i].key);
    }
    _combo_image_rendering.set_tooltip_text(_("The 'image-rendering' property can influence how a bitmap is re-scaled:\n"
                                              "\t• 'auto': no preference (scaled image is usually smooth but blurred)\n"
                                              "\t• 'optimizeQuality': prefer rendering quality (usually smooth but blurred)\n"
                                              "\t• 'optimizeSpeed': prefer rendering speed (usually blocky)\n"
                                              "\t• 'crisp-edges': rescale without blurring edges (often blocky)\n"
                                              "\t• 'pixelated': render blocky\n"
                                              "Note that the specification of this property is not finalized. "
                                              "Support and interpretation of these values varies between renderers."));

    _combo_image_rendering.set_valign(Gtk::Align::CENTER);

    _label_image_rendering.set_mnemonic_widget(_combo_image_rendering);

    _combo_image_rendering.signal_changed().connect(
        sigc::mem_fun(*this, &ObjectProperties::_imageRenderingChanged)
    );


    grid_top->attach(_label_id,              0, 0, 1, 1);
    grid_top->attach(_entry_id,              1, 0, 1, 1);
    grid_top->attach(_label_label,           0, 1, 1, 1);
    grid_top->attach(_entry_label,           1, 1, 1, 1);
    grid_top->attach(_label_title,           0, 2, 1, 1);
    grid_top->attach(_entry_title,           1, 2, 1, 1);
    grid_top->attach(_label_color,           0, 3, 1, 1);
    grid_top->attach(_highlight_color,       1, 3, 1, 1);
    grid_top->attach(_label_dpi,             0, 4, 1, 1);
    grid_top->attach(_spin_dpi,              1, 4, 1, 1);
    grid_top->attach(_label_image_rendering, 0, 5, 1, 1);
    grid_top->attach(_combo_image_rendering, 1, 5, 1, 1);

    /* Check boxes */
    auto const hb_checkboxes = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
    UI::pack_start(*this, *hb_checkboxes, UI::PackOptions::shrink);

    auto const grid_cb = Gtk::make_managed<Gtk::Grid>();
    grid_cb->set_row_homogeneous();
    grid_cb->set_column_homogeneous(true);

    grid_cb->set_margin(4);
    UI::pack_start(*hb_checkboxes, *grid_cb, true, true);

    /* Hide */
    _cb_hide.set_tooltip_text (_("Check to make the object invisible"));
    _cb_hide.set_hexpand();
    _cb_hide.set_valign(Gtk::Align::CENTER);
    grid_cb->attach(_cb_hide, 0, 0, 1, 1);

    _cb_hide.signal_toggled().connect(sigc::mem_fun(*this, &ObjectProperties::_hiddenToggled));

    /* Lock */
    // TRANSLATORS: "Lock" is a verb here
    _cb_lock.set_tooltip_text(_("Check to make the object insensitive (not selectable by mouse)"));
    _cb_lock.set_hexpand();
    _cb_lock.set_valign(Gtk::Align::CENTER);
    grid_cb->attach(_cb_lock, 1, 0, 1, 1);

    _cb_lock.signal_toggled().connect(sigc::mem_fun(*this, &ObjectProperties::_sensitivityToggled));

    /* Preserve aspect ratio */
    _cb_aspect_ratio.set_tooltip_text(_("Check to preserve aspect ratio on images"));
    _cb_aspect_ratio.set_hexpand();
    _cb_aspect_ratio.set_valign(Gtk::Align::CENTER);
    grid_cb->attach(_cb_aspect_ratio, 0, 1, 1, 1);

    _cb_aspect_ratio.signal_toggled().connect(sigc::mem_fun(*this, &ObjectProperties::_aspectRatioToggled));


    /* Button for setting the object's id, label, title and description. */
    auto const btn_set = Gtk::make_managed<Gtk::Button>(_("_Set"), true);
    btn_set->set_hexpand();
    btn_set->set_valign(Gtk::Align::CENTER);
    grid_cb->attach(*btn_set, 1, 1, 1, 1);

    btn_set->signal_clicked().connect(sigc::mem_fun(*this, &ObjectProperties::_labelChanged));

    /* Interactivity options */
    _exp_interactivity.set_vexpand(false);

    UI::pack_start(*this, _exp_interactivity, UI::PackOptions::shrink);

    set_visible(true);
}

void ObjectProperties::update_entries()
{
    if (_blocked || !getDesktop()) {
        return;
    }

    auto selection = getSelection();
    if (!selection) return;

    if (!selection->singleItem()) {
        set_sensitive (false);
        _current_item = nullptr;
        //no selection anymore or multiple objects selected, means that we need
        //to close the connections to the previously selected object
        _attr_table->clear();
        _highlight_color.setRgba32(0x0);
        return;
    } else {
        set_sensitive (true);
    }

    SPItem *item = selection->singleItem();
    if (_current_item == item)
    {
        //otherwise we would end up wasting resources through the modify selection
        //callback when moving an object (endlessly setting the labels and recreating _attr_table)
        return;
    }
    _blocked = true;
    _cb_aspect_ratio.set_active(g_strcmp0(item->getAttribute("preserveAspectRatio"), "none") != 0);
    _cb_lock.set_active(item->isLocked());           /* Sensitive */
    _cb_hide.set_active(item->isExplicitlyHidden()); /* Hidden */
    _highlight_color.setRgba32(item->highlight_color());
    _highlight_color.closeWindow();

    if (item->cloned) {
        /* ID */
        _entry_id.set_text("");
        _entry_id.set_sensitive(FALSE);
        _label_id.set_text(_("Ref"));

        /* Label */
        _entry_label.set_text("");
        _entry_label.set_sensitive(FALSE);
        _label_label.set_text(_("Ref"));

    } else {
        SPObject *obj = static_cast<SPObject*>(item);

        /* ID */
        _entry_id.set_text(obj->getId() ? obj->getId() : "");
        _entry_id.set_sensitive(TRUE);
        _label_id.set_markup_with_mnemonic(_("_ID:") + Glib::ustring(" "));

        /* Label */
        char const *currentlabel = obj->label();
        char const *placeholder = "";
        if (!currentlabel) {
            currentlabel = "";
            placeholder = obj->defaultLabel();
        }
        _entry_label.set_text(currentlabel);
        _entry_label.set_placeholder_text(placeholder);
        _entry_label.set_sensitive(TRUE);

        /* Title */
        gchar *title = obj->title();
        if (title) {
            _entry_title.set_text(title);
            g_free(title);
        }
        else {
            _entry_title.set_text("");
        }
        _entry_title.set_sensitive(TRUE);

        /* Image Rendering */
        if (is<SPImage>(item)) {
            _combo_image_rendering.set_visible(true);
            _label_image_rendering.set_visible(true);
            _combo_image_rendering.set_active(obj->style->image_rendering.value);
            if (obj->getAttribute("inkscape:svg-dpi")) {
                _spin_dpi.set_value(std::stod(obj->getAttribute("inkscape:svg-dpi")));
                _spin_dpi.set_visible(true);
                _label_dpi.set_visible(true);
            } else {
                _spin_dpi.set_visible(false);
                _label_dpi.set_visible(false);
            }
        } else {
            _combo_image_rendering.set_visible(false);
            _combo_image_rendering.unset_active();
            _label_image_rendering.set_visible(false);
            _spin_dpi.set_visible(false);
            _label_dpi.set_visible(false);
        }

        /* Description */
        gchar *desc = obj->desc();
        if (desc) {
            _tv_description.get_buffer()->set_text(desc);
            g_free(desc);
        } else {
            _tv_description.get_buffer()->set_text("");
        }
        _ft_description.set_sensitive(TRUE);

        if (_current_item == nullptr) {
            _exp_interactivity.set_child(*_attr_table->set_object(obj, _int_labels, _int_attrs));
        } else {
            _attr_table->change_object(obj);
        }
    }
    _current_item = item;
    _blocked = false;
}

void ObjectProperties::_labelChanged()
{
    if (_blocked) {
        return;
    }

    SPItem *item = getSelection()->singleItem();
    g_return_if_fail (item != nullptr);

    _blocked = true;

    /* Retrieve the label widget for the object's id */
    gchar *id = g_strdup(_entry_id.get_text().c_str());
    g_strcanon(id, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.:", '_');
    if (g_strcmp0(id, item->getId()) == 0) {
        _label_id.set_markup_with_mnemonic(_("_ID:") + Glib::ustring(" "));
    } else if (!*id || !isalnum (*id)) {
        _label_id.set_text(_("Id invalid! "));
    } else if (getDocument()->getObjectById(id) != nullptr) {
        _label_id.set_text(_("Id exists! "));
    } else {
        _label_id.set_markup_with_mnemonic(_("_ID:") + Glib::ustring(" "));
        item->setAttribute("id", id);
        DocumentUndo::done(getDocument(), _("Set object ID"), INKSCAPE_ICON("dialog-object-properties"));
    }
    g_free(id);

    /* Retrieve the label widget for the object's label */
    Glib::ustring label = _entry_label.get_text();

    /* Give feedback on success of setting the drawing object's label
     * using the widget's label text
     */
    SPObject *obj = static_cast<SPObject*>(item);
    char const *currentlabel = obj->label();
    if (label.compare(currentlabel ? currentlabel : "")) {
        obj->setLabel(label.c_str());
        DocumentUndo::done(getDocument(), _("Set object label"), INKSCAPE_ICON("dialog-object-properties"));
    }

    /* Retrieve the title */
    if (obj->setTitle(_entry_title.get_text().c_str())) {
        DocumentUndo::done(getDocument(), _("Set object title"), INKSCAPE_ICON("dialog-object-properties"));
    }

    /* Retrieve the DPI */
    if (is<SPImage>(obj)) {
        Glib::ustring dpi_value = Glib::ustring::format(_spin_dpi.get_value());
        obj->setAttribute("inkscape:svg-dpi", dpi_value);
        DocumentUndo::done(getDocument(), _("Set image DPI"), INKSCAPE_ICON("dialog-object-properties"));
    }

    /* Retrieve the description */
    Gtk::TextBuffer::iterator start, end;
    _tv_description.get_buffer()->get_bounds(start, end);
    Glib::ustring desc = _tv_description.get_buffer()->get_text(start, end, TRUE);
    if (obj->setDesc(desc.c_str())) {
        DocumentUndo::done(getDocument(), _("Set object description"), INKSCAPE_ICON("dialog-object-properties"));
    }

    _blocked = false;
}

void ObjectProperties::_highlightChanged(guint rgba)
{
    if (_blocked)
        return;
    
    if (auto item = getSelection()->singleItem()) {
        item->setHighlight(rgba);
        DocumentUndo::done(getDocument(), _("Set item highlight color"), INKSCAPE_ICON("dialog-object-properties"));
    }
}

void ObjectProperties::_imageRenderingChanged()
{
    if (_blocked) {
        return;
    }

    SPItem *item = getSelection()->singleItem();
    g_return_if_fail (item != nullptr);

    _blocked = true;

    Glib::ustring scale = _combo_image_rendering.get_active_text();

    // We should unset if the parent computed value is auto and the desired value is auto.
    SPCSSAttr *css = sp_repr_css_attr_new();
    sp_repr_css_set_property(css, "image-rendering", scale.c_str());
    Inkscape::XML::Node *image_node = item->getRepr();
    if (image_node) {
        sp_repr_css_change(image_node, css, "style");
        DocumentUndo::done(getDocument(), _("Set image rendering option"), INKSCAPE_ICON("dialog-object-properties"));
    }
    sp_repr_css_attr_unref(css);

    _blocked = false;
}

void ObjectProperties::_sensitivityToggled()
{
    if (_blocked) {
        return;
    }

    SPItem *item = getSelection()->singleItem();
    g_return_if_fail(item != nullptr);

    _blocked = true;
    item->setLocked(_cb_lock.get_active());
    DocumentUndo::done(getDocument(), _cb_lock.get_active() ? _("Lock object") : _("Unlock object"), INKSCAPE_ICON("dialog-object-properties"));
    _blocked = false;
}

void ObjectProperties::_aspectRatioToggled()
{
    if (_blocked) {
        return;
    }

    SPItem *item = getSelection()->singleItem();
    g_return_if_fail(item != nullptr);

    _blocked = true;

    const char *active;
    if (_cb_aspect_ratio.get_active()) {
        active = "xMidYMid";
    }
    else {
        active = "none";
    }
    /* Retrieve the DPI */
    if (is<SPImage>(item)) {
        Glib::ustring dpi_value = Glib::ustring::format(_spin_dpi.get_value());
        item->setAttribute("preserveAspectRatio", active);
        DocumentUndo::done(getDocument(), _("Set preserve ratio"), INKSCAPE_ICON("dialog-object-properties"));
    }
    _blocked = false;
}

void ObjectProperties::_hiddenToggled()
{
    if (_blocked) {
        return;
    }

    SPItem *item = getSelection()->singleItem();
    g_return_if_fail(item != nullptr);

    _blocked = true;
    item->setExplicitlyHidden(_cb_hide.get_active());
    DocumentUndo::done(getDocument(), _cb_hide.get_active() ? _("Hide object") : _("Unhide object"), INKSCAPE_ICON("dialog-object-properties"));
    _blocked = false;
}

void ObjectProperties::selectionChanged(Selection *selection)
{
    update_entries();
}

void ObjectProperties::desktopReplaced()
{
    update_entries();
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
