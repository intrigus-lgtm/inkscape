// SPDX-License-Identifier: GPL-2.0-or-later
//
// This is an item factory for a ColumnView container. It creates an item with picture and label.
// During "bind" phase it will ask for label markup and picture image, plus tooltip, and populate item.

#ifndef _ICONVIEWITEMFACTORY_H_
#define _ICONVIEWITEMFACTORY_H_

#include <gdkmm/texture.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/centerbox.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/overlay.h>
#include <gtkmm/picture.h>
#include <gtkmm/signallistitemfactory.h>
#include <memory>

namespace Inkscape::UI {

class IconViewItemFactory {
public:
    struct ItemData {
        Glib::ustring label_markup;
        Glib::RefPtr<Gdk::Texture> image; 
        Glib::ustring tooltip;
    };

    static std::unique_ptr<IconViewItemFactory> create(std::function<ItemData (Glib::RefPtr<Glib::ObjectBase>&)> get_item) {
        return std::unique_ptr<IconViewItemFactory>(new IconViewItemFactory(std::move(get_item)));
    }

    Glib::RefPtr<Gtk::ListItemFactory> get_factory() { return _factory; }

private:
    IconViewItemFactory(std::function<ItemData (Glib::RefPtr<Glib::ObjectBase>&)> get_item):
        _get_item_data(std::move(get_item)) {

        _factory = Gtk::SignalListItemFactory::create();

        _factory->signal_setup().connect([=](const Glib::RefPtr<Gtk::ListItem>& list_item) {
            auto box = Gtk::make_managed<Gtk::CenterBox>();
            box->add_css_class("item-box");
            box->set_orientation(Gtk::Orientation::VERTICAL);
            // put picture in an overlay, so it doesn't propagete its size to the parent container;
            // that way picture widget can be freely resized to desired dimensions and it will not grow beyond them
            auto image = Gtk::make_managed<Gtk::Picture>();
            auto overlay = Gtk::make_managed<Gtk::Overlay>();
            overlay->set_halign(Gtk::Align::CENTER);
            overlay->set_valign(Gtk::Align::CENTER);
            overlay->add_overlay(*image);
            box->set_start_widget(*overlay);
            // add a label below the picture
            auto label = Gtk::make_managed<Gtk::Label>();
            label->set_vexpand();
            label->set_valign(Gtk::Align::START);
            box->set_end_widget(*label);

            list_item->set_child(*box);
        });

        _factory->signal_bind().connect([=](const Glib::RefPtr<Gtk::ListItem>& list_item) {
            auto item = list_item->get_item();

            auto box = dynamic_cast<Gtk::CenterBox*>(list_item->get_child());
            if (!box) return;
            auto overlay = dynamic_cast<Gtk::Overlay*>(box->get_start_widget());
            if (!overlay) return;
            auto image = dynamic_cast<Gtk::Picture*>(overlay->get_first_child());
            if (!image) return;
            auto label = dynamic_cast<Gtk::Label*>(box->get_end_widget());
            if (!label) return;

            auto item_data = _get_item_data(item);

            image->set_can_shrink(true);
            image->set_content_fit(Gtk::ContentFit::CONTAIN);
            auto tex = item_data.image;
            image->set_paintable(tex);
            // poor man's high dpi support here:
            auto scale = box->get_scale_factor();
            auto width = tex ? tex->get_intrinsic_width() / scale : 0;
            auto height = tex ? tex->get_intrinsic_height() / scale : 0;
            image->set_size_request(width, height);
            overlay->set_size_request(width, height);

            // label->set_text(effect->name);
            //TODO: small labels?
            // label->set_markup("<small>" + Glib::Markup::escape_text(effect->name) + "</small>");
            label->set_markup(item_data.label_markup);
            label->set_max_width_chars(12);
            label->set_wrap();
            label->set_justify(Gtk::Justification::CENTER);
            label->set_valign(Gtk::Align::START);

            box->set_tooltip_text(item_data.tooltip);
        });
    }

    std::function<ItemData (Glib::RefPtr<Glib::ObjectBase>&)> _get_item_data;
    Glib::RefPtr<Gtk::SignalListItemFactory> _factory;
};

} // namespace

#endif
