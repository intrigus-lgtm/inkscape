// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A dialog for XML attributes
 */
/* Authors:
 *   Martin Owens
 *
 * Copyright (C) Martin Owens 2018 <doctormo@gmail.com>
 *
 * Released under GNU GPLv2 or later, read the file 'COPYING' for more information
 */

#include "attrdialog.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <gdk/gdkkeysyms.h>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/regex.h>
#include <glibmm/timer.h>
#include <glibmm/ustring.h>
#include <glibmm/varianttype.h>
#include <giomm/simpleactiongroup.h>
#include <gtkmm/box.h>
#include <gtkmm/builder.h>
#include <gtkmm/button.h>
#include <gtkmm/enums.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/object.h>
#include <gtkmm/popover.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/textview.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/treeview.h>
#include <sigc++/adaptors/bind.h>
#include <sigc++/functors/mem_fun.h>

#include "config.h"
#if WITH_GSOURCEVIEW
#   include <gtksourceview/gtksource.h>
#endif

#include "document-undo.h"
#include "io/resource.h"
#include "message-context.h"
#include "message-stack.h"
#include "preferences.h"
#include "selection.h"
#include "style.h"
#include "ui/builder-utils.h"
#include "ui/controller.h"
#include "ui/dialog/inkscape-preferences.h"
#include "ui/icon-loader.h"
#include "ui/icon-names.h"
#include "ui/menuize.h"
#include "ui/pack.h"
#include "ui/popup-menu.h"
#include "ui/syntax.h"
#include "ui/widget/shapeicon.h"
#include "util/numeric/converters.h"
#include "util/trim.h"
#include "xml/attribute-record.h"

/**
 * Return true if `node` is a text or comment node
 */
static bool is_text_or_comment_node(Inkscape::XML::Node const &node)
{
    switch (node.type()) {
      case Inkscape::XML::NodeType::TEXT_NODE:
      case Inkscape::XML::NodeType::COMMENT_NODE:
            return true;
        default:
            return false;
    }
}

static Glib::ustring get_syntax_theme()
{
    return Inkscape::Preferences::get()->getString("/theme/syntax-color-theme", "-none-");
}

namespace Inkscape::UI::Dialog {

// arbitrarily selected size limits
constexpr int MAX_POPOVER_HEIGHT = 450;
constexpr int MAX_POPOVER_WIDTH = 520;
constexpr int TEXT_MARGIN = 3;

std::unique_ptr<Syntax::TextEditView> AttrDialog::init_text_view(AttrDialog* owner, Syntax::SyntaxMode coloring, bool map)
{
    auto edit = Syntax::TextEditView::create(coloring);
    auto& textview = edit->getTextView();
    textview.set_wrap_mode(Gtk::WrapMode::WORD);

    // this actually sets padding rather than margin and extends textview's background color to the sides
    textview.set_top_margin(TEXT_MARGIN);
    textview.set_left_margin(TEXT_MARGIN);
    textview.set_right_margin(TEXT_MARGIN);
    textview.set_bottom_margin(TEXT_MARGIN);

    if (map) {
        textview.signal_map().connect([owner](){
            // this is not effective: text view recalculates its size on idle, so it's too early to call on 'map';
            // (note: there's no signal on a TextView to tell us that formatting has been done)
            // delay adjustment; this will work if UI is fast enough, but at the cost of popup jumping,
            // but at least it will be sized properly
            owner->_adjust_size = Glib::signal_timeout().connect([=](){ owner->adjust_popup_edit_size(); return false; }, 50);
        });
    }

    return edit;
}

/**
 * Constructor
 * A treeview whose each row corresponds to an XML attribute of a selected node
 * New attribute can be added by clicking '+' at bottom of the attr pane. '-'
 */
AttrDialog::AttrDialog()
    : DialogBase("/dialogs/attr", "AttrDialog")
    , _builder(create_builder("attribute-edit-component.glade"))
    , _scrolled_text_view(get_widget<Gtk::ScrolledWindow>(_builder, "scroll-wnd"))
    , _content_sw(get_widget<Gtk::ScrolledWindow>(_builder, "content-sw"))
    , _scrolled_window(get_widget<Gtk::ScrolledWindow>(_builder, "scrolled-wnd"))
    , _treeView(get_widget<Gtk::TreeView>(_builder, "tree-view"))
    , _popover(&get_widget<Gtk::Popover>(_builder, "popup"))
    , _status_box(get_widget<Gtk::Box>(_builder, "status-box"))
    , _status(get_widget<Gtk::Label>(_builder, "status-label"))
{
    // Attribute value editing (with syntax highlighting).
    using namespace Syntax;
    _css_edit = init_text_view(this, SyntaxMode::InlineCss, true);
    _svgd_edit = init_text_view(this, SyntaxMode::SvgPathData, true);
    _points_edit = init_text_view(this, SyntaxMode::SvgPolyPoints, true);
    _attr_edit = init_text_view(this, SyntaxMode::PlainText, true);

    // string content editing
    _text_edit = init_text_view(this, SyntaxMode::PlainText, false);
    _style_edit = init_text_view(this, SyntaxMode::CssStyle, false);

    set_size_request(20, 15);

    // For text and comment nodes: update XML on the fly, as users type
    for (auto tv : {&_text_edit->getTextView(), &_style_edit->getTextView()}) {
        tv->get_buffer()->signal_end_user_action().connect([=, this]() {
            if (_repr) {
                _repr->setContent(tv->get_buffer()->get_text().c_str());
                setUndo(_("Type text"));
            }
        });
    }

    _store = Gtk::ListStore::create(_attrColumns);
    _treeView.set_model(_store);

    // high-res aware icon renderer for a trash can
    auto const delete_renderer = Gtk::make_managed<UI::Widget::CellRendererItemIcon>();
    delete_renderer->property_shape_type().set_value("edit-delete");
    delete_renderer->signal_activated().connect(sigc::mem_fun(*this, &AttrDialog::onAttrDelete));
    _treeView.append_column("", *delete_renderer);

    if (auto const col = _treeView.get_column(0)) {
        auto add_icon = Gtk::manage(sp_get_icon_image("list-add", Gtk::IconSize::NORMAL));
        col->set_clickable(true);
        col->set_widget(*add_icon);
        add_icon->set_tooltip_text(_("Add a new attribute"));
        add_icon->set_visible(true);
        col->signal_clicked().connect(sigc::mem_fun(*this, &AttrDialog::onCreateClicked), false);
    }

    Controller::add_key<&AttrDialog::onTreeViewKeyPressed, &AttrDialog::onTreeViewKeyReleased>
                       (_treeView, *this);

    _nameRenderer = Gtk::make_managed<Gtk::CellRendererText>();
    _nameRenderer->property_editable() = true;
    _nameRenderer->property_placeholder_text().set_value(_("Attribute Name"));
    _nameRenderer->signal_edited().connect(sigc::mem_fun(*this, &AttrDialog::nameEdited));
    _nameRenderer->signal_editing_started().connect(sigc::mem_fun(*this, &AttrDialog::startNameEdit));
    _treeView.append_column(_("Name"), *_nameRenderer);
    _nameCol = _treeView.get_column(1);
    if (_nameCol) {
        _nameCol->set_resizable(true);
        _nameCol->add_attribute(_nameRenderer->property_text(), _attrColumns._attributeName);
    }

    _message_stack = std::make_shared<Inkscape::MessageStack>();
    _message_context = std::make_unique<Inkscape::MessageContext>(_message_stack);
    _message_changed_connection = _message_stack->connectChanged([this](MessageType, const char* message) {
        _status.set_markup(message ? message : "");
    });

    _valueRenderer = Gtk::make_managed<Gtk::CellRendererText>();
    _valueRenderer->property_editable() = true;
    _valueRenderer->property_placeholder_text().set_value(_("Attribute Value"));
    _valueRenderer->property_ellipsize().set_value(Pango::EllipsizeMode::END);
    _valueRenderer->signal_edited().connect(sigc::mem_fun(*this, &AttrDialog::valueEdited));
    _valueRenderer->signal_editing_started().connect(sigc::mem_fun(*this, &AttrDialog::startValueEdit), true);
    _treeView.append_column(_("Value"), *_valueRenderer);
    _valueCol = _treeView.get_column(2);
    if (_valueCol) {
        _valueCol->add_attribute(_valueRenderer->property_text(), _attrColumns._attributeValueRender);
    }

    set_current_textedit(_attr_edit.get());
    _scrolled_text_view.set_max_content_height(MAX_POPOVER_HEIGHT);

    auto& apply = get_widget<Gtk::Button>(_builder, "btn-ok");
    apply.signal_clicked().connect([this]{ valueEditedPop(); });

    auto& cancel = get_widget<Gtk::Button>(_builder, "btn-cancel");
    cancel.signal_clicked().connect([this]{
        if (!_value_editing.empty()) {
            _activeTextView().get_buffer()->set_text(_value_editing);
        }
        _popover->popdown();
    });

    _popover->set_relative_to(*this);
    _popover->signal_closed().connect([this]{ popClosed(); });
    Controller::add_key<&AttrDialog::onPopoverKeyPressed>(*_popover, *this, Gtk::PropagationPhase::CAPTURE);

    get_widget<Gtk::Button>(_builder, "btn-truncate").signal_clicked().connect([this]{ truncateDigits(); });

    const int N = 5;
    _rounding_precision = Inkscape::Preferences::get()->getIntLimited("/dialogs/attrib/precision", 2, 0, N);
    setPrecision(_rounding_precision);
    auto group = Gio::SimpleActionGroup::create();
    auto action = group->add_action_radio_integer("precision", _rounding_precision);
    action->property_state().signal_changed().connect([=, this]{ int n; action->get_state(n);
                                                                 setPrecision(n); });
    insert_action_group("attrdialog", std::move(group));
    UI::menuize_popover(*get_widget<Gtk::MenuButton>(_builder, "btn-menu").get_popover());

    attr_reset_context(0);
    UI::pack_start(*this, get_widget<Gtk::Box>(_builder, "main-box"), UI::PackOptions::expand_widget);
    _updating = false;
}

AttrDialog::~AttrDialog()
{
    _current_text_edit = nullptr;
    _popover->set_visible(false);

    // remove itself from the list of node observers
    setRepr(nullptr);
}

static int fmt_number(_GMatchInfo const *match, _GString *ret, void *prec)
{
    auto number = g_match_info_fetch(match, 1);

    char *end;
    double val = g_ascii_strtod(number, &end);
    if (*number && (end == nullptr || end > number)) {
        auto precision = *static_cast<int*>(prec);
        auto fmt = Util::format_number(val, precision);
        g_string_append(ret, fmt.c_str());
    } else {
        g_string_append(ret, number);
    }

    auto text = g_match_info_fetch(match, 2);
    g_string_append(ret, text);

    g_free(number);
    g_free(text);

    return false;
}

Glib::ustring AttrDialog::round_numbers(const Glib::ustring& text, int precision)
{
    // match floating point number followed by something else (not a number); repeat
    static const auto numbers = Glib::Regex::create("([-+]?(?:(?:\\d+\\.?\\d*)|(?:\\.\\d+))(?:[eE][-+]?\\d*)?)([^+\\-0-9]*)", Glib::REGEX_MULTILINE);

    return numbers->replace_eval(text, text.size(), 0, Glib::Regex::MatchFlags::NOTEMPTY, &fmt_number, &precision);
}

/** Round the selected floating point numbers in the attribute edit popover. */
void AttrDialog::truncateDigits() const
{
    if (!_current_text_edit) {
        return;
    }

    auto buffer = _current_text_edit->getTextView().get_buffer();
    auto start = buffer->begin();
    auto end = buffer->end();

    bool const had_selection = buffer->get_has_selection();
    int start_idx = 0, end_idx = 0;
    if (had_selection) {
        buffer->get_selection_bounds(start, end);
        start_idx = start.get_offset();
        end_idx = end.get_offset();
    }

    auto text = buffer->get_text(start, end);
    auto ret = round_numbers(text, _rounding_precision);
    buffer->erase(start, end);
    buffer->insert_at_cursor(ret);

    if (had_selection) {
        // Restore selection but note that its length may have decreased.
        end_idx -= text.size() - ret.size();
        if (end_idx < start_idx) {
            end_idx = start_idx;
        }
        buffer->select_range(buffer->get_iter_at_offset(start_idx), buffer->get_iter_at_offset(end_idx));
    }
}

void AttrDialog::set_current_textedit(Syntax::TextEditView* edit)
{
    _current_text_edit = edit ? edit : _attr_edit.get();
    _scrolled_text_view.remove();
    _scrolled_text_view.add(_current_text_edit->getTextView());
    _scrolled_text_view.show_all();
}

void AttrDialog::adjust_popup_edit_size()
{
    auto vscroll = _scrolled_text_view.get_vadjustment();
    int height = vscroll->get_upper() + 2 * TEXT_MARGIN;
    if (height < MAX_POPOVER_HEIGHT) {
        _scrolled_text_view.set_min_content_height(height);
        vscroll->set_value(vscroll->get_lower());
    } else {
        _scrolled_text_view.set_min_content_height(MAX_POPOVER_HEIGHT);
    }
}

bool AttrDialog::onPopoverKeyPressed(GtkEventControllerKey const * /*controller*/,
                                     unsigned keyval, unsigned /*keycode*/,
                                     GdkModifierType state)
{
    if (!_popover->is_visible()) return false;

    switch (keyval) {
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if (Controller::has_flag(state, GDK_SHIFT_MASK)) {
                valueEditedPop();
                return true;
            }

            // as we type and content grows, resize the popup to accommodate it
            _adjust_size = Glib::signal_timeout().connect([this]{ adjust_popup_edit_size(); return false; }, 50);
    }

    return false;
}

/**
 * Prepare value string suitable for display in a Gtk::CellRendererText
 *
 * Value is truncated at the first new line character (if any) and a visual indicator and ellipsis is added.
 * Overall length is limited as well to prevent performance degradation for very long values.
 *
 * @param value Raw attribute value as UTF-8 encoded string
 * @return Single-line string with fixed maximum length
 */
static Glib::ustring prepare_rendervalue(const char *value)
{
    constexpr int MAX_LENGTH = 500; // maximum length of string before it's truncated for performance reasons
                                    // ~400 characters fit horizontally on a WQHD display, so 500 should be plenty
    Glib::ustring renderval;

    // truncate to MAX_LENGTH
    if (g_utf8_strlen(value, -1) > MAX_LENGTH) {
        renderval = Glib::ustring(value, MAX_LENGTH) + "…";
    } else {
        renderval = value;
    }

    // truncate at first newline (if present) and add a visual indicator
    auto ind = renderval.find('\n');
    if (ind != Glib::ustring::npos) {
        renderval.replace(ind, Glib::ustring::npos, " ⏎ …");
    }

    return renderval;
}

void set_mono_class(Gtk::Widget* widget, bool mono)
{
    if (!widget) {
        return;
    }
    Glib::ustring class_name = "mono-font";
    auto style = widget->get_style_context();
    auto has_class = style->has_class(class_name);

    if (mono && !has_class) {
        style->add_class(class_name);
    } else if (!mono && has_class) {
        style->remove_class(class_name);
    }
}

void AttrDialog::set_mono_font(bool mono)
{
    set_mono_class(&_treeView, mono);
}

void AttrDialog::startNameEdit(Gtk::CellEditable *cell, const Glib::ustring &path)
{
    Gtk::Entry *entry = dynamic_cast<Gtk::Entry *>(cell);
    setEditingEntry(entry, false);
}

Gtk::TextView &AttrDialog::_activeTextView() const
{
    return _current_text_edit->getTextView();
}

void AttrDialog::startValueEdit(Gtk::CellEditable *cell, const Glib::ustring &path)
{
    _value_path = path;

    if (!_repr || !cell) {
        return;
    }

    auto const iter = _store->get_iter(path);
    if (!iter) {
        return;
    }

    auto &row = *iter;

    // popover in GTK3 is clipped to dialog window (in a floating dialog); limit size:
    const int dlg_width = get_allocated_width() - 10;
    _popover->set_size_request(std::min(MAX_POPOVER_WIDTH, dlg_width), -1);

    auto const &attribute = row.get_value(_attrColumns._attributeName);
    bool edit_in_popup =
#if WITH_GSOURCEVIEW
    true;
#else
    false;
#endif
    bool enable_rouding = false;

    if (attribute == "style") {
        set_current_textedit(_css_edit.get());
    } else if (attribute == "d" || attribute == "inkscape:original-d") {
        enable_rouding = true;
        set_current_textedit(_svgd_edit.get());
    } else if (attribute == "points") {
        enable_rouding = true;
        set_current_textedit(_points_edit.get());
    } else {
        set_current_textedit(_attr_edit.get());
        edit_in_popup = false;
    }

    // number rounding functionality
    get_widget<Gtk::Box>(_builder, "rounding-box").set_visible(enable_rouding);

    _activeTextView().set_size_request(std::min(MAX_POPOVER_WIDTH - 10, dlg_width), -1);

    auto theme = get_syntax_theme();

    auto entry = dynamic_cast<Gtk::Entry*>(cell);
    int width, height;
    entry->get_layout()->get_pixel_size(width, height);
    int colwidth = _valueCol->get_width();

    if (row[_attrColumns._attributeValue] != row[_attrColumns._attributeValueRender] ||
        edit_in_popup || colwidth - 10 < width)
    {
        _value_editing = entry->get_text();

        Gdk::Rectangle rect;
        _treeView.get_cell_area((Gtk::TreeModel::Path)iter, *_valueCol, rect);
        if (_popover->get_position() == Gtk::PositionType::BOTTOM) {
            rect.set_y(rect.get_y() + 20);
        }
        if (rect.get_x() >= dlg_width) {
            rect.set_x(dlg_width - 1);
        }

        auto current_value = row[_attrColumns._attributeValue];
        _current_text_edit->setStyle(theme);
        _current_text_edit->setText(current_value);

        // close in-line entry
        cell->property_editing_canceled() = true;
        cell->remove_widget();
        // cannot dismiss it right away without warning from GTK, so delay it
        Glib::signal_timeout().connect_once([=](){
            cell->editing_done(); // only this call will actually remove in-line edit widget
            cell->remove_widget();
        }, 0);
        // and show popup edit instead
        Glib::signal_timeout().connect_once([=, this]{ UI::popup_at(*_popover, _treeView, rect); },
                                            10);
    } else {
        setEditingEntry(entry, true);
    }
}

void AttrDialog::popClosed()
{
    if (!_current_text_edit) {
        return;
    }

    _activeTextView().get_buffer()->set_text("");

    // delay this resizing, so it is not visible as popover fades out
    _close_popup = Glib::signal_timeout().connect(
        [this]{ _scrolled_text_view.set_min_content_height(20); return false; }, 250);
}

/**
 * @brief AttrDialog::setRepr
 * Set the internal xml object that I'm working on right now.
 */
void AttrDialog::setRepr(Inkscape::XML::Node * repr)
{
    if (repr == _repr) {
        return;
    }
    if (_repr) {
        _store->clear();
        _repr->removeObserver(*this);
        Inkscape::GC::release(_repr);
        _repr = nullptr;
    }
    _repr = repr;
    if (repr) {
        Inkscape::GC::anchor(_repr);
        _repr->addObserver(*this);

        // show either attributes or content
        bool show_content = is_text_or_comment_node(*_repr);
        if (show_content) {
            _content_sw.remove();
            auto type = repr->name();
            auto elem = repr->parent();
            if (type && strcmp(type, "string") == 0 && elem && elem->name() && strcmp(elem->name(), "svg:style") == 0) {
                // editing embedded CSS style
                _style_edit->setStyle(get_syntax_theme());
                _content_sw.add(_style_edit->getTextView());
            } else {
                _content_sw.add(_text_edit->getTextView());
            }
        }

        _repr->synthesizeEvents(*this);
        _scrolled_window.set_visible(!show_content);
        _content_sw.set_visible(show_content);
    }
}

void AttrDialog::setUndo(Glib::ustring const &event_description)
{
    DocumentUndo::done(getDocument(), event_description, INKSCAPE_ICON("dialog-xml-editor"));
}

void AttrDialog::createAttribute()
{
    auto const iter = _store->prepend();
    auto const path = static_cast<Gtk::TreeModel::Path>(iter);
    _treeView.set_cursor(path, *_nameCol, true);
    grab_focus();
}

void AttrDialog::deleteAttribute(Gtk::TreeRow &row)
{
    auto const name = row.get_value(_attrColumns._attributeName);
    _store->erase(row);
    _repr->removeAttribute(name);
    setUndo(_("Delete attribute"));
}

void AttrDialog::setEditingEntry(Gtk::Entry * const entry, bool const embedNewline)
{
    g_assert(!(entry == nullptr && embedNewline));

    _editingEntry = entry;
    _embedNewline = embedNewline;

    if (_editingEntry == nullptr) return;

    _editingEntry->signal_editing_done().connect([this]{ setEditingEntry(nullptr, false); });
}

/**
 * Sets the AttrDialog status bar, depending on which attr is selected.
 */
void AttrDialog::attr_reset_context(gint attr)
{
    if (attr == 0) {
        _message_context->set(Inkscape::NORMAL_MESSAGE, _("<b>Click</b> attribute to edit."));
    } else {
        const gchar *name = g_quark_to_string(attr);
        _message_context->setF(
            Inkscape::NORMAL_MESSAGE,
            _("Attribute <b>%s</b> selected. Press <b>Ctrl+Enter</b> when done editing to commit changes."), name);
    }
}

/**
 * @brief AttrDialog::notifyAttributeChanged
 * This is called when the XML has an updated attribute
 */
void AttrDialog::notifyAttributeChanged(XML::Node & /*node*/, GQuark name_,
                                        Util::ptr_shared /*old_value*/, Util::ptr_shared new_value)
{
    if (_updating) {
        return;
    }

    auto const name = g_quark_to_string(name_);

    Glib::ustring renderval;
    if (new_value) {
        renderval = prepare_rendervalue(new_value.pointer());
    }
    for (auto&& iter : _store->children()) {
        Gtk::TreeModel::Row row = *iter;
        Glib::ustring col_name = row[_attrColumns._attributeName];
        if (name == col_name) {
            if (new_value) {
                row[_attrColumns._attributeValue] = new_value.pointer();
                row[_attrColumns._attributeValueRender] = renderval;
                new_value = Util::ptr_shared(); // Don't make a new one
            } else {
                _store->erase(iter);
            }
            break;
        }
    }
    if (new_value) {
        Gtk::TreeModel::Row row = *_store->prepend();
        row[_attrColumns._attributeName] = name;
        row[_attrColumns._attributeValue] = new_value.pointer();
        row[_attrColumns._attributeValueRender] = renderval;
    }
}

/**
 * @brief AttrDialog::onCreateClicked
 * This function is a slot to signal_clicked for '+' button panel.
 */
void AttrDialog::onCreateClicked()
{
    if (_repr) {
        createAttribute();
    }
}

/**
 * @brief AttrDialog::onAttrDelete
 * @param event
 * @return true
 * Delete the attribute from the xml
 */
void AttrDialog::onAttrDelete(Glib::ustring const &path)
{
    Gtk::TreeModel::Row row = *_store->get_iter(path);
    if (row) {
        deleteAttribute(row);
    }
}

void AttrDialog::notifyContentChanged(XML::Node & /*repr*/, Util::ptr_shared /*old_content*/,
                                      Util::ptr_shared new_content)
{
    auto textview = dynamic_cast<Gtk::TextView *>(_content_sw.get_child());
    if (!textview) {
        return;
    }
    auto buffer = textview->get_buffer();
    if (!buffer->get_modified()) {
        auto str = new_content.pointer();
        buffer->set_text(str ? str : "");
    }
    buffer->set_modified(false);
}

/**
 * @brief AttrDialog::onTreeViewKeyPressed
 * Delete or create elements based on key presses
 */
bool AttrDialog::onTreeViewKeyPressed(GtkEventControllerKey const * /*controller*/,
                                      unsigned keyval, unsigned /*keycode*/, GdkModifierType state)
{
    if (!_repr) {
        return false;
    }

    switch (keyval) {
        case GDK_KEY_Delete:
        case GDK_KEY_KP_Delete: {
            if (auto const selection = _treeView.get_selection()) {
                auto row = *selection->get_selected();
                deleteAttribute(row);
            }
            return true;
        }

        case GDK_KEY_plus:
        case GDK_KEY_Insert:
            createAttribute();
            return true;

        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if (_popover->is_visible() && Controller::has_flag(state, GDK_SHIFT_MASK)) {
                valueEditedPop();
                return true;
            }
    }

    return false;
}

bool AttrDialog::onTreeViewKeyReleased(GtkEventControllerKey const * /*controller*/,
                                       unsigned keyval, unsigned /*keycode*/, GdkModifierType state)
{
    if (_editingEntry == nullptr) return false;

    switch (keyval) {
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if (_embedNewline && Controller::has_flag(state, GDK_SHIFT_MASK)) {
                auto pos = _editingEntry->get_position();
                _editingEntry->insert_text("\n", 1, pos);
                _editingEntry->set_position(pos + 1);
                return true;
            }
    }

    return false;
}

void AttrDialog::storeMoveToNext(Gtk::TreeModel::Path modelpath)
{
    auto selection = _treeView.get_selection();
    auto iter = *(selection->get_selected());
    auto path = static_cast<Gtk::TreeModel::Path>(iter);
    Gtk::TreeViewColumn *focus_column;
    _treeView.get_cursor(path, focus_column);
    if (path == modelpath && focus_column == _treeView.get_column(1)) {
        _treeView.set_cursor(modelpath, *_valueCol, true);
    }
}

/**
 * Called when the name is edited in the TreeView editable column
 */
void AttrDialog::nameEdited (const Glib::ustring& path, const Glib::ustring& name)
{
    Gtk::TreeModel::iterator iter = *_store->get_iter(path);
    auto modelpath = static_cast<Gtk::TreeModel::Path>(iter);
    Gtk::TreeModel::Row row = *iter;
    if(row && this->_repr) {
        Glib::ustring old_name = row[_attrColumns._attributeName];
        if (old_name == name) {
            Glib::signal_timeout().connect_once([=, this]{ storeMoveToNext(modelpath); }, 50);
            grab_focus();
            return;
        }
        // Do not allow empty name (this would delete the attribute)
        if (name.empty()) {
            return;
        }
        // Do not allow duplicate names
        const auto children = _store->children();
        for (const auto &child : children) {
            if (name == child[_attrColumns._attributeName]) {
                return;
            }
        }
        if(std::any_of(name.begin(), name.end(), isspace)) {
            return;
        }
        // Copy old value and remove old name
        Glib::ustring value;
        if (!old_name.empty()) {
            value = row[_attrColumns._attributeValue];
            _updating = true;
            _repr->removeAttribute(old_name);
            _updating = false;
        }

        // Do the actual renaming and set new value
        row[_attrColumns._attributeName] = name;
        grab_focus();
        _updating = true;
        _repr->setAttributeOrRemoveIfEmpty(name, value); // use char * overload (allows empty attribute values)
        _updating = false;
        Glib::signal_timeout().connect_once([=, this]{ storeMoveToNext(modelpath); }, 50);
        setUndo(_("Rename attribute"));
    }
}

void AttrDialog::valueEditedPop()
{
    valueEdited(_value_path, _current_text_edit->getText());
    _value_editing.clear();
    _popover->popdown();
}

/**
 * @brief AttrDialog::valueEdited
 * @param event
 * @return
 * Called when the value is edited in the TreeView editable column
 */
void AttrDialog::valueEdited (const Glib::ustring& path, const Glib::ustring& value)
{
    if (!getDesktop()) {
        return;
    }

    Gtk::TreeModel::Row row = *_store->get_iter(path);
    if (row && _repr) {
        Glib::ustring name = row[_attrColumns._attributeName];
        Glib::ustring old_value = row[_attrColumns._attributeValue];
        if (old_value == value || name.empty()) {
            return;
        }

        _repr->setAttributeOrRemoveIfEmpty(name, value);

        if (!value.empty()) {
            row[_attrColumns._attributeValue] = value;
            Glib::ustring renderval = prepare_rendervalue(value.c_str());
            row[_attrColumns._attributeValueRender] = renderval;
        }
        setUndo(_("Change attribute value"));
    }
}

void AttrDialog::setPrecision(int const n)
{
    _rounding_precision = n;
    auto &menu_button = get_widget<Gtk::MenuButton>(_builder, "btn-menu");
    auto const menu = menu_button.get_menu_model();
    auto const section = menu->get_item_link(0, Gio::MENU_LINK_SECTION);
    auto const type = Glib::VariantType{g_variant_type_new("s")};
    auto const variant = section->get_item_attribute(n, Gio::MENU_ATTRIBUTE_LABEL, type);
    auto const label = ' ' + static_cast<Glib::Variant<Glib::ustring> const &>(variant).get();
    get_widget<Gtk::Label>(_builder, "precision").set_label(label);
    Inkscape::Preferences::get()->setInt("/dialogs/attrib/precision", n);
    menu_button.set_active(false);
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
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:
