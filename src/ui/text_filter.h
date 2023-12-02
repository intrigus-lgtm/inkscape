// SPDX-License-Identifier: GPL-2.0-or-later
//
// Text-matching filter

#ifndef _TEXTMATCHINGFILTER_H_
#define _TEXTMATCHINGFILTER_H_

#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/boolfilter.h>
#include <gtkmm/filter.h>

namespace Inkscape::UI {

class TextMatchingFilter {
public:
    TextMatchingFilter(std::function<Glib::ustring (const Glib::RefPtr<Glib::ObjectBase>& item)> get_text_to_match):
        _get_text(std::move(get_text_to_match)) {

        auto expression = Gtk::ClosureExpression<bool>::create([this](auto& item){
            if (_search_text.empty()) return true;

            auto text = _get_text(item).lowercase();
            return text.find(_search_text) != Glib::ustring::npos;
        });

        _filter = Gtk::BoolFilter::create(expression);
    }

    void refilter(const Glib::ustring& search) {
        _search_text = search.lowercase();
        Gtk::Filter* f = _filter.get();
        gtk_filter_changed(f->gobj(), GtkFilterChange::GTK_FILTER_CHANGE_DIFFERENT);
    }

    Glib::RefPtr<Gtk::Filter> get_filter() const {
        return _filter;
    }

private:
    std::function<Glib::ustring (const Glib::RefPtr<Glib::ObjectBase>& item)> _get_text;
    Glib::RefPtr<Gtk::BoolFilter> _filter;
    Glib::ustring _search_text;
};

} // namespace

#endif
