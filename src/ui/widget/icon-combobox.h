// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SEEN_INKSCAPE_UI_WIDGET_ICONCOMBOBOX
#define SEEN_INKSCAPE_UI_WIDGET_ICONCOMBOBOX

#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/cellrendererpixbuf.h>
#include <gtkmm/combobox.h>
#include <gtkmm/treemodelcolumn.h>

namespace Gtk {
class ListStore;
class TreeModelFilter;
} // namespace Gtk;

namespace Inkscape::UI::Widget {

class IconComboBox : public Gtk::ComboBox
{
public:
    IconComboBox();
    ~IconComboBox() override;

    void add_row(Glib::ustring const &icon_name, Glib::ustring const &label, int id);
    void set_active_by_id(int id);
    void set_row_visible(int id, bool visible = true);
    int get_active_row_id() const;

private:
    class Columns : public Gtk::TreeModel::ColumnRecord
    {
    public:
        Columns() {
            add(icon_name);
            add(label);
            add(id);
            add(is_visible);
        }

        Gtk::TreeModelColumn<Glib::ustring> icon_name;
        Gtk::TreeModelColumn<Glib::ustring> label;
        Gtk::TreeModelColumn<int> id;
        Gtk::TreeModelColumn<bool> is_visible;
    };

    Columns _columns;
    Glib::RefPtr<Gtk::ListStore> _model;
    Glib::RefPtr<Gtk::TreeModelFilter> _filter;
    Gtk::CellRendererPixbuf _renderer;
};

} // namespace Inkscape::UI::Widget

#endif // SEEN_INKSCAPE_UI_WIDGET_ICONCOMBOBOX

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:
