// SPDX-License-Identifier: GPL-2.0-or-later
/* Authors:
 *   Martin Owens <doctormo@geek-2.com>
 *
 * Copyright (C) 2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "template-list.h"

#include <map>
#include <glib/gi18n.h>
#include <gdkmm/pixbuf.h>
#include <gtkmm/builder.h>
#include <gtkmm/iconview.h>
#include <gtkmm/liststore.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/treemodel.h>

#include "extension/db.h"
#include "extension/template.h"
#include "inkscape-application.h"
#include "io/resource.h"
#include "ui/builder-utils.h"
#include "ui/util.h"
#include "ui/icon-loader.h"
#include "ui/svg-renderer.h"

using namespace Inkscape::IO::Resource;
using Inkscape::Extension::TemplatePreset;

namespace Inkscape::UI::Widget {

class TemplateCols final : public Gtk::TreeModel::ColumnRecord
{
public:
    // These types must match those for the model in the .glade file
    TemplateCols()
    {
        this->add(this->name);
        this->add(this->label);
        this->add(this->tooltip);
        this->add(this->icon);
        this->add(this->key);
        this->add(this->priority);
    }

    Gtk::TreeModelColumn<Glib::ustring> name;
    Gtk::TreeModelColumn<Glib::ustring> label;
    Gtk::TreeModelColumn<Glib::ustring> tooltip;
    Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf>> icon;
    Gtk::TreeModelColumn<Glib::ustring> key;
    Gtk::TreeModelColumn<int> priority;
};

TemplateList::TemplateList(BaseObjectType *cobject, const Glib::RefPtr<Gtk::Builder> &refGlade)
    : Gtk::Notebook(cobject)
{
}

/**
 * Initialise this template list with categories and icons
 */
void TemplateList::init(Inkscape::Extension::TemplateShow mode)
{
    TemplateCols cols;
    std::map<std::string, Glib::RefPtr<Gtk::ListStore>> stores;

    Inkscape::Extension::DB::TemplateList extensions;
    Inkscape::Extension::db.get_template_list(extensions);

    for (auto tmod : extensions) {
        for (auto preset : tmod->get_presets(mode)) {
            auto const &cat = preset->get_category();

            if (auto it = stores.lower_bound(cat);
                it == stores.end() || it->first != cat)
            {
                try {
                    it = stores.emplace_hint(it, cat, generate_category(cat));
                    it->second->clear();
                    it->second->set_sort_column(cols.priority, Gtk::SortType::ASCENDING);
                } catch (UIBuilderError const & /*error*/) {
                    return;
                }
            }

            auto const &name = preset->get_name();
            auto const &label = preset->get_label();
            auto const &desc = preset->get_description();

            Gtk::TreeModel::Row row = *stores[cat]->append();
            row[cols.name] = _(name.c_str());
            row[cols.label] = label.empty() ? "" : _(label.c_str());
            row[cols.tooltip] = _(desc.empty() ? name.c_str() : desc.c_str());
            row[cols.icon] = icon_to_pixbuf(preset->get_icon_path());
            row[cols.key] = preset->get_key();
            row[cols.priority] = preset->get_sort_priority();
        }
    }

    reset_selection();
}

/**
 * Turn the requested template icon name into a pixbuf
 */
Glib::RefPtr<Gdk::Pixbuf> TemplateList::icon_to_pixbuf(std::string const &path)
{
    // TODO: Add some caching here.
    if (!path.empty()) {
        Inkscape::svg_renderer renderer(path.c_str());
        return renderer.render(1.0);
    }
    Glib::RefPtr<Gdk::Pixbuf> no_image;
    return no_image;
}

/**
 * Generate a new category with the given label and return it's list store.
 */
Glib::RefPtr<Gtk::ListStore> TemplateList::generate_category(std::string const &label)
{
    auto builder = create_builder("widget-new-from-template.ui");
    auto container = &get_widget<Gtk::ScrolledWindow>(builder, "container");
    auto icons     = &get_widget<Gtk::IconView>      (builder, "iconview");

    // This packing keeps the Gtk widget alive, beyond the builder's lifetime
    append_page(*container, g_dpgettext2(nullptr, "TemplateCategory", label.c_str()));

    icons->signal_selection_changed().connect([this]() { _item_selected_signal.emit(); });
    icons->signal_item_activated().connect([this](const Gtk::TreeModel::Path) { _item_activated_signal.emit(); });

    return std::dynamic_pointer_cast<Gtk::ListStore>(icons->get_model());
}

/**
 * Returns true if the template list has a visible, selected preset.
 */
bool TemplateList::has_selected_preset()
{
    return (bool)get_selected_preset();
}

/**
 * Returns the selected template preset, if one is not selected returns nullptr.
 */
std::shared_ptr<TemplatePreset> TemplateList::get_selected_preset()
{
    TemplateCols cols;
    if (auto iconview = get_iconview(get_nth_page(get_current_page()))) {
        auto items = iconview->get_selected_items();
        if (!items.empty()) {
            auto iter = iconview->get_model()->get_iter(items[0]);
            if (Gtk::TreeModel::Row row = *iter) {
                Glib::ustring key = row[cols.key];
                return Extension::Template::get_any_preset(key);
            }
        }
    }
    return nullptr;
}

/**
 * Create a new document based on the selected item and return.
 */
SPDocument *TemplateList::new_document()
{
    auto app = InkscapeApplication::instance();
    if (auto preset = get_selected_preset()) {
        if (auto doc = preset->new_from_template()) {
            // TODO: Add memory to remember this preset for next time.
            app->document_add(doc);
            return doc;
        } else {
            // Cancel pressed in options box.
            return nullptr;
        }
    }
    // Fallback to the default template (already added)!
    return app->document_new();
}

/**
 * Reset the selection, forcing the use of the default template.
 */
void TemplateList::reset_selection()
{
    // TODO: Add memory here for the new document default (see new_document).
    for (auto const widget : UI::get_children(*this)) {
        if (auto iconview = get_iconview(widget)) {
            iconview->unselect_all();
        }
    }
}

/**
 * Returns the internal iconview for the given widget.
 */
Gtk::IconView *TemplateList::get_iconview(Gtk::Widget *widget)
{
    if (!widget) return nullptr;

    for (auto const child : UI::get_children(*widget)) {
        if (auto iconview = get_iconview(child)) {
            return iconview;
        }
    }

    return dynamic_cast<Gtk::IconView *>(widget);
}

} // namespace Inkscape::UI::Widget

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
