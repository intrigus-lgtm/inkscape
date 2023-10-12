// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef COLUMN_MENU_BUILDER_INCLUDED
#define COLUMN_MENU_BUILDER_INCLUDED

#include <cassert>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>
#include <sigc++/slot.h>
#include <glibmm/ustring.h>
#include <gtkmm/enums.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/separator.h>
#include "ui/widget/popover-menu.h"
#include "ui/widget/popover-menu-item.h"

namespace Inkscape::UI {

// TODO: GTK4: Can we use Gtk::GridView? 4.12 has section/headings, so if they can span columns, OK

template <typename SectionData = std::nullptr_t> // nullptr_t means no sections.
class ColumnMenuBuilder {
public:
    ColumnMenuBuilder(Widget::PopoverMenu& menu, int columns,
                      Gtk::IconSize icon_size = Gtk::IconSize::NORMAL,
                      int const first_row = 0)
        : _menu(menu)
        , _row(first_row)
        , _columns(columns)
        , _icon_size{icon_size}
    {
        assert(_row >= 0);
        assert(_columns >= 1);
    }

    void add_item(Widget::PopoverMenuItem &item,
                  std::optional<SectionData> const &section = {})
    {
        _new_section = false;
        _section = nullptr;
        if (section && (!_last_section || *_last_section != *section)) {
            g_assert(!(std::is_same_v<SectionData, std::nullptr_t>));
            _new_section = true;
        }

        if (_new_section) {
            if (_col > 0) _row++;

            // add separator
            if (_row > 0) {
                auto const separator = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
                separator->set_visible(true);
                _menu.attach(*separator, 0, _columns, _row, _row + 1);
                _row++;
            }

            _last_section = section;

            auto const sep = Gtk::make_managed<Widget::PopoverMenuItem>();
            sep->add_css_class("menu-category");
            sep->set_sensitive(false);
            sep->set_halign(Gtk::Align::START);
            _menu.attach(*sep, 0, _columns, _row, _row + 1);
            _section = sep;
            _row++;
            _col = 0;
        }

        _menu.attach(item, _col, _col + 1, _row, _row + 1);

        _col++;
        if (_col >= _columns) {
            _col = 0;
            _row++;
        }
    }

    Widget::PopoverMenuItem *add_item(
        Glib::ustring const &label, std::optional<SectionData> const &section,
        Glib::ustring const &tooltip, Glib::ustring const &icon_name,
        bool const sensitive, bool const customtooltip,
        sigc::slot<void ()> callback)
    {
        auto const item = Gtk::make_managed<Widget::PopoverMenuItem>(label, true,
                                                                     icon_name, _icon_size);
        if (!customtooltip) {
            item->set_tooltip_markup(tooltip);
        }
        item->set_sensitive(sensitive);
        item->signal_activate().connect(std::move(callback));

        add_item(*item, section);

        return item;
    }

    Widget::PopoverMenuItem *add_item(
        Glib::ustring const &label,
        Glib::ustring const &tooltip, Glib::ustring const &icon_name,
        bool const sensitive, bool const customtooltip,
        sigc::slot<void ()> callback)
    {
        return add_item(label, std::nullopt, tooltip, icon_name,
                        sensitive, customtooltip, std::move(callback));
    }

    bool new_section() const {
        return _new_section;
    }

    void set_section(Glib::ustring const &name) {
        // name lastest section
        if (_section) {
            _section->set_label(name.uppercase());
        }
    }

private:
    int _row = 0;
    int _col = 0;
    int _columns;
    Widget::PopoverMenu &_menu;
    bool _new_section = false;
    std::optional<SectionData> _last_section;
    Widget::PopoverMenuItem *_section = nullptr;
    Gtk::IconSize _icon_size;
};

} // namespace Inkscape::UI

#endif // COLUMN_MENU_BUILDER_INCLUDED

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
