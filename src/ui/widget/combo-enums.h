// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Nicholas Bishop <nicholasbishop@gmail.com>
 *   Johan Engelen <j.b.c.engelen@ewi.utwente.nl>
 *
 * Copyright (C) 2007 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_WIDGET_COMBO_ENUMS_H
#define INKSCAPE_UI_WIDGET_COMBO_ENUMS_H

#include <glibmm/i18n.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/combobox.h>
#include <gtkmm/liststore.h>
#include <gtkmm/treemodel.h>
#include <sigc++/functors/mem_fun.h>

#include "attr-widget.h"
#include "ui/widget/labelled.h"
#include "util/enums.h"

namespace Inkscape::UI::Widget {

/**
 * Simplified management of enumerations in the UI as combobox.
 */
template <typename E> class ComboBoxEnum
    : public Gtk::ComboBox
    , public AttrWidget
{
public:
    [[nodiscard]] ComboBoxEnum(E const default_value, const Util::EnumDataConverter<E>& c,
                               SPAttr const a = SPAttr::INVALID, bool const sort = true,
                               const char* translation_context = nullptr)
        : ComboBoxEnum{c, a, sort, translation_context, static_cast<unsigned>(default_value)}
    {
        set_active_by_id(default_value);
        sort_items();
    }

    [[nodiscard]] ComboBoxEnum(Util::EnumDataConverter<E> const &c,
                               SPAttr const a = SPAttr::INVALID, bool const sort = true,
                               const char * const translation_context = nullptr)
        : ComboBoxEnum{c, a, sort, translation_context, 0u}
    {
        set_active(0);
        sort_items();
    }

private:
    [[nodiscard]] int on_sort_compare(Gtk::TreeModel::const_iterator const &a,
                                      Gtk::TreeModel::const_iterator const &b) const
    {
        auto const &an = a->get_value(_columns.label);
        auto const &bn = b->get_value(_columns.label);
        return an.compare(bn);
    }

    bool _sort = true;

    [[nodiscard]] ComboBoxEnum(Util::EnumDataConverter<E> const &c,
                               SPAttr const a, bool const sort,
                               const char * const translation_context,
                               unsigned const default_value)
        : AttrWidget(a, default_value)
        , setProgrammatically(false)
        , _converter(c)
        ,_sort{sort}
    {
        signal_changed().connect(signal_attr_changed().make_slot());

        _model = Gtk::ListStore::create(_columns);
        set_model(_model);

        pack_start(_columns.label);

        for (int i = 0; i < static_cast<int>(_converter._length); ++i) {
            auto row = *_model->append();

            auto const data = &_converter.data(i);
            row.set_value(_columns.data, data);

            auto const &label = _converter.get_label(data->id);
            Glib::ustring translated = translation_context ?
                g_dpgettext2(nullptr, translation_context, label.c_str()) :
                gettext(label.c_str());
            row.set_value(_columns.label, std::move(translated));

            row.set_value(_columns.is_separator, _converter.get_key(data->id) == "-");
        }

        set_row_separator_func(sigc::mem_fun(*this, &ComboBoxEnum<E>::combo_separator_func));
    }

    void sort_items() {
        if (_sort) {
            _model->set_default_sort_func(sigc::mem_fun(*this, &ComboBoxEnum<E>::on_sort_compare));
            _model->set_sort_column(_columns.label, Gtk::SortType::ASCENDING);
        }
    }

public:
    [[nodiscard]] Glib::ustring get_as_attribute() const final
    {
        return get_active_data()->key;
    }

    void set_from_attribute(SPObject * const o) final
    {
        setProgrammatically = true;

        if (auto const val = attribute_value(o)) {
            set_active_by_id(_converter.get_id_from_key(val));
        } else {
            set_active(get_default()->as_uint());
        }
    }
    
    [[nodiscard]] Util::EnumData<E> const *get_active_data() const
    {
        if (auto const i = this->get_active()) {
            return i->get_value(_columns.data);
        }
        return nullptr;
    }

    void add_row(const Glib::ustring& s)
    {
        auto row = *_model->append();
        row.set_value(_columns.data, 0);
        row.set_value(_columns.label, s);
    }

    void remove_row(E id) {
        auto &&children = _model->children();
        auto const e = children.end();
        for (auto i = children.begin(); i != e; ++i) {
            if (auto const data = i->get_value(_columns.data); data->id == id) {
                _model->erase(i);
                return;
            }
        }
    }

    void set_active_by_id(E id) {
        setProgrammatically = true;
        auto index = get_active_by_id(id);
        if (index >= 0) {
            set_active(index);
        }
    };

    void set_active_by_key(const Glib::ustring& key) {
        setProgrammatically = true;
        set_active_by_id( _converter.get_id_from_key(key) );
    };

    bool combo_separator_func(Glib::RefPtr<Gtk::TreeModel> const &model,
                              Gtk::TreeModel::const_iterator const &iter) const
    {
        return iter->get_value(_columns.is_separator);
    };

    bool setProgrammatically = false;

private:
    [[nodiscard]] int get_active_by_id(E const id) const
    {
        int index = 0;
        for (auto const &child : _model->children()) {
            if (auto const data = child.get_value(_columns.data); data->id == id) {
                return index;
            }
            ++index;
        }
        return -1;
    };

    class Columns final : public Gtk::TreeModel::ColumnRecord
    {
    public:
        Columns()
        {
            add(data);
            add(label);
            add(is_separator);
        }

        Gtk::TreeModelColumn<const Util::EnumData<E>*> data;
        Gtk::TreeModelColumn<Glib::ustring> label;
        Gtk::TreeModelColumn<bool> is_separator;
    };

    Columns _columns;
    Glib::RefPtr<Gtk::ListStore> _model;
    const Util::EnumDataConverter<E>& _converter;
};


/**
 * Simplified management of enumerations in the UI as combobox, plus the functionality of Labelled.
 */
template <typename E> class LabelledComboBoxEnum
    : public Labelled
{
public:
    [[nodiscard]] LabelledComboBoxEnum(Glib::ustring const &label,
                                       Glib::ustring const &tooltip,
                                       Util::EnumDataConverter<E> const &c,
                                       Glib::ustring const &icon = {},
                                       bool const mnemonic = true,
                                       bool const sort = true)
        : Labelled{label, tooltip,
                   Gtk::make_managed<ComboBoxEnum<E>>(c, SPAttr::INVALID, sort),
                   icon, mnemonic}
    { 
    }

    [[nodiscard]] ComboBoxEnum<E> *getCombobox()
    {
        return static_cast<ComboBoxEnum<E> *>(getWidget());
    }
};

} // namespace Inkscape::UI::Widget

#endif // INKSCAPE_UI_WIDGET_COMBO_ENUMS_H

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
