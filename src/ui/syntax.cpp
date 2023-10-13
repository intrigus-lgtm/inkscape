// SPDX-License-Identifier: GPL-2.0-or-later
/** @file Syntax coloring via Gtksourceview and Pango markup.
 */
/* Authors:
 *   Rafael Siejakowski <rs@rs-math.net>
 *   Mike Kowalski
 *
 * Copyright (C) 2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "ui/syntax.h"

#include <glibmm/ustring.h>
#include <pango/pango-attributes.h>
#include <sstream>
#include <stdexcept>

#include "color.h"
#include "config.h"
#include "io/resource.h"
#include "object/sp-factory.h"
#include "util/trim.h"

#if WITH_GSOURCEVIEW
#   include <gtksourceview/gtksource.h>
#endif

namespace Inkscape::UI::Syntax {

Glib::ustring XMLFormatter::_format(Style const &style, Glib::ustring const &content) const
{
    return _format(style, content.c_str());
}

/** Get the opening tag of the Pango markup for this style. */
Glib::ustring Style::openingTag() const
{
    if (isDefault()) {
        return "";
    }

    std::ostringstream ost;
    ost << "<span";
    if (color) {
        ost << " color=\"" << color->raw() << '"';
    }
    if (background) {
        ost << " bgcolor=\"" << background->raw() << '"';
    }
    if (bold) {
        ost << " weight=\"bold\"";
    }
    if (italic) {
        ost << " font_style=\"italic\"";
    }
    if (underline) {
        ost << " underline=\"single\"";
    }

    ost << ">";
    return Glib::ustring(ost.str());
}

/** Get the closing tag of Pango markup for this style. */
Glib::ustring Style::closingTag() const
{
    return isDefault() ? "" : "</span>";
}

Glib::ustring quote(const char* text)
{
    return Glib::ustring::compose("\"%1\"", text);
}

/** Open a new XML tag with the given tag name. */
void XMLFormatter::openTag(char const *tag_name)
{
    _wip = _format(_style.angular_brackets, "<");

    // Highlight as errors unsupported tags in SVG namespace (explicit or implicit).
    bool error = false;
    std::string fully_qualified_name(tag_name);
    if (fully_qualified_name.empty()) {
        return;
    }
    bool is_svg = false;
    if (fully_qualified_name.find(':') == std::string::npos) {
        fully_qualified_name = std::string("svg:") + fully_qualified_name;
        is_svg = true;
    } else if (fully_qualified_name.find("svg:") == 0) {
        is_svg = true;
    }
    if (is_svg && !SPFactory::supportsType(fully_qualified_name)) {
        error = true;
    }
    _wip += _format(error ? _style.error : _style.tag_name, tag_name);
}

void XMLFormatter::addAttribute(char const *name, char const *value)
{
    _wip += Glib::ustring::compose(" %1%2%3",
                                   _format(_style.attribute_name, name),
                                   _format(_style.angular_brackets, "="),
                                   _format(_style.attribute_value, quote(value)));
}

Glib::ustring XMLFormatter::finishTag(bool self_close)
{
    return _wip + _format(_style.angular_brackets, self_close ? "/>" : ">");
}

Glib::ustring XMLFormatter::formatContent(char const* content, bool wrap_in_quotes) const
{
    Glib::ustring text = wrap_in_quotes ? quote(content) : content;
    return _format(_style.content, text);
}

Glib::ustring XMLFormatter::formatComment(char const* comment, bool wrap_in_marks) const
{
    if (wrap_in_marks) {
        auto wrapped = Glib::ustring::compose("<!--%1-->", comment);
        return _format(_style.comment, wrapped.c_str());
    }
    return _format(_style.comment, comment);
}

XMLStyles build_xml_styles(const Glib::ustring& syntax_theme)
{
    XMLStyles styles;

#if WITH_GSOURCEVIEW
    auto manager = gtk_source_style_scheme_manager_get_default();
    if (auto scheme = gtk_source_style_scheme_manager_get_scheme(manager, syntax_theme.c_str())) {

        auto get_color = [](GtkSourceStyle* style, const char* prop) -> std::optional<Glib::ustring> {
            std::optional<Glib::ustring> maybe_color;
            Glib::ustring name(prop);
            gboolean set;
            gchar* color = 0;
            g_object_get(style, (name + "-set").c_str(), &set, name.c_str(), &color, nullptr);
            if (set && color && *color == '#') {
                maybe_color = Glib::ustring(color);
            }
            g_free(color);
            return maybe_color;
        };

        auto get_bool = [](GtkSourceStyle* style, const char* prop, bool def = false) -> bool {
            Glib::ustring name(prop);
            gboolean set;
            gboolean flag;
            g_object_get(style, (name + "-set").c_str(), &set, name.c_str(), &flag, nullptr);
            return set ? !!flag : def;
        };

        auto get_underline = [](GtkSourceStyle* style, bool def = false) -> bool {
            Glib::ustring name("underline");
            gboolean set;
            PangoUnderline underline;
            g_object_get(style, (name + "-set").c_str(), &set, ("pango-" + name).c_str(), &underline, nullptr);
            return set ? underline != PANGO_UNDERLINE_NONE : def;
        };

        auto to_style = [&](char const *id) -> Style {
            auto s = gtk_source_style_scheme_get_style(scheme, id);
            if (!s) {
                return Style();
            }

            Style style;

            style.color      = get_color(s, "foreground");
            style.background = get_color(s, "background");
            style.bold       = get_bool(s, "bold");
            style.italic     = get_bool(s, "italic");
            style.underline  = get_underline(s);

            return style;
        };

        styles.tag_name         = to_style("def:statement");
        styles.attribute_name   = to_style("def:number");
        styles.attribute_value  = to_style("def:string");
        styles.content          = to_style("def:string");
        styles.comment          = to_style("def:comment");
        styles.prolog           = to_style("def:warning");
        styles.angular_brackets = to_style("draw-spaces");
        styles.error            = to_style("def:error");
    }
#endif

    return styles;
}

/** @brief Reformat CSS for better readability.
 */
Glib::ustring prettify_css(Glib::ustring const &css)
{
    // Ensure that there's a space after every colon, unless there's a slash (as in a URL).
    static auto const colon_without_space = Glib::Regex::create(":([^\\s\\/])");
    auto reformatted = colon_without_space->replace(css, 0, ": \\1", Glib::Regex::MatchFlags::NOTEMPTY);
    // Ensure that there's a newline after every semicolon.
    static auto const semicolon_without_newline = Glib::Regex::create(";([^\r\n])");
    reformatted = semicolon_without_newline->replace(reformatted, 0, ";\n\\1", Glib::Regex::MatchFlags::NEWLINE_ANYCRLF);
    // If the last character is not a semicolon, append one.
    if (auto len = css.size(); len && css[len - 1] != ';') {
        reformatted += ";";
    }
    return reformatted;
}

/** Undo the CSS prettification by stripping some whitespace from CSS markup. */
Glib::ustring minify_css(Glib::ustring const &css)
{
    static auto const space_after = Glib::Regex::create("(:|;)[\\s]+");
    auto minified = space_after->replace(css, 0, "\\1", Glib::Regex::MatchFlags::NEWLINE_ANY);
    // Strip final semicolon
    if (auto const len = minified.size(); len && minified[len - 1] == ';') {
        minified = minified.erase(len - 1);
    }
    return minified;
}

/** @brief Reformat a path 'd' attibute for better readability. */
Glib::ustring prettify_svgd(Glib::ustring const &d)
{
    auto result = d;
    Util::trim(result);
    // Ensure that a non-M command is preceded only by a newline.
    static auto const space_b4_command = Glib::Regex::create("(?<=\\S)\\s*(?=[LHVCSQTAZlhvcsqtaz])");
    result = space_b4_command->replace(result, 1, "\n", Glib::Regex::MatchFlags::NEWLINE_ANY);

    // Before a non-initial M command, we want to have two newlines to visually separate the subpaths.
    static auto const space_b4_m = Glib::Regex::create("(?<=\\S)\\s*(?=[Mm])");
    result = space_b4_m->replace(result, 1, "\n\n", Glib::Regex::MatchFlags::NEWLINE_ANY);

    // Ensure that there's a space after each command letter other than Z.
    static auto const nospace = Glib::Regex::create("([MLHVCSQTAmlhvcsqta])(?=\\S)");
    return nospace->replace(result, 0, "\\1 ", Glib::Regex::MatchFlags::NEWLINE_ANY);
}

/** @brief Remove excessive space, including newlines, from a path 'd' attibute. */
Glib::ustring minify_svgd(Glib::ustring const &d)
{
    static auto const excessive_space = Glib::Regex::create("[\\s]+");
    auto result = excessive_space->replace(d, 0, " ", Glib::Regex::MatchFlags::NEWLINE_ANY);
    Util::trim(result);
    return result;
}

/** Set default options on a TextView widget used for syntax-colored editing. */
static void init_text_view(Gtk::TextView* textview)
{
    textview->set_wrap_mode(Gtk::WrapMode::WORD);
    textview->set_editable(true);
    textview->set_visible(true);
}

/// Plain text view widget without syntax coloring
class PlainTextView : public TextEditView
{
public:
    PlainTextView()
        : _textview(std::make_unique<Gtk::TextView>(Gtk::TextBuffer::create()))
    {
        init_text_view(_textview.get());
    }

    void setStyle(const Glib::ustring& theme) override { /* no op */ }
    void setText(const Glib::ustring& text) override { _textview->get_buffer()->set_text(text); }

    Glib::ustring getText() const override { return _textview->get_buffer()->get_text(); }
    Gtk::TextView& getTextView() const override { return *_textview; }

private:
    std::unique_ptr<Gtk::TextView> _textview;
};

#if WITH_GSOURCEVIEW

/** @brief Return a pointer to a language manager which is aware of both
 * default and custom syntaxes.
 */
static GtkSourceLanguageManager* get_language_manager()
{
    auto ui_path = IO::Resource::get_path_string(IO::Resource::SYSTEM, IO::Resource::UIS);
    auto default_manager = gtk_source_language_manager_get_default();
    auto default_paths = gtk_source_language_manager_get_search_path(default_manager);

    std::vector<char const *> all_paths;
    for (auto path = default_paths; *path; path++) {
        all_paths.push_back(*path);
    }
    all_paths.push_back(ui_path.c_str());
    all_paths.push_back(nullptr);

    auto result = gtk_source_language_manager_new();
    gtk_source_language_manager_set_search_path(result, (gchar **)all_paths.data());
    return result;
}

class SyntaxHighlighting : public TextEditView
{
public:
    SyntaxHighlighting() = delete;
    /** @brief Construct a syntax highlighter for a given language. */
    SyntaxHighlighting(char const* const language,
                       Glib::ustring (*prettify_func)(Glib::ustring const &),
                       Glib::ustring (*minify_func)(Glib::ustring const &))
        : _prettify{prettify_func}
        , _minify{minify_func}
    {
        auto manager = get_language_manager();
        auto lang = gtk_source_language_manager_get_language(manager, language);
        _buffer = gtk_source_buffer_new_with_language(lang);
        auto view = gtk_source_view_new_with_buffer(_buffer);
        // Increment Glib's internal refcount to prevent the destruction of the
        // textview by a parent widget (if any); the textview is owned by us!
        g_object_ref(view);

        _textview = std::unique_ptr<Gtk::TextView>(Glib::wrap((GtkTextView*)view));
        if (!_textview) {
            // don't crash when sourceview cannot be created; substitute with a regular one;
            // in this case GTK has already outputted warnings
            _textview = std::make_unique<Gtk::TextView>(Gtk::TextBuffer::create());
        }
        init_text_view(_textview.get());
    }

    ~SyntaxHighlighting() override { g_object_unref(_buffer); }
private:
    GtkSourceBuffer *_buffer = nullptr; // Owned by us
    std::unique_ptr<Gtk::TextView> _textview;
    Glib::ustring (*_prettify)(Glib::ustring const &);
    Glib::ustring (*_minify)(Glib::ustring const &);

public:
    void setStyle(Glib::ustring const &theme) override
    {
        auto manager = gtk_source_style_scheme_manager_get_default();
        auto scheme = gtk_source_style_scheme_manager_get_scheme(manager, theme.c_str());
        gtk_source_buffer_set_style_scheme(_buffer, scheme);
    }

    /** @brief Set the displayed text to a prettified version of the passed string. */
    void setText(Glib::ustring const &text) override
    {
        _textview->get_buffer()->set_text(_prettify(text));
    }

    /** @brief Get a minified version of the buffer contents, suitable for inserting into XML. */
    Glib::ustring getText() const override
    {
        return _minify(_textview->get_buffer()->get_text());
    }

    Gtk::TextView &getTextView() const override { return *_textview; };
};

#endif // WITH_GSOURCEVIEW

/** Create a styled text view using the desired syntax highlighting mode. */
std::unique_ptr<TextEditView> TextEditView::create(SyntaxMode mode)
{
#if WITH_GSOURCEVIEW
    auto const no_reformat = [](auto &s) { return s; };
    switch (mode) {
        case SyntaxMode::PlainText:
            return std::make_unique<PlainTextView>();
        case SyntaxMode::InlineCss:
            return std::make_unique<SyntaxHighlighting>("inline-css", &prettify_css, &minify_css);
        case SyntaxMode::CssStyle:
            return std::make_unique<SyntaxHighlighting>("css", no_reformat, no_reformat);
        case SyntaxMode::SvgPathData:
            return std::make_unique<SyntaxHighlighting>("svgd", &prettify_svgd, &minify_svgd);
        case SyntaxMode::SvgPolyPoints:
            return std::make_unique<SyntaxHighlighting>("svgpoints", no_reformat, no_reformat);
        default:
            throw std::runtime_error("Missing case statement in TetxEditView::create()");
    }
#else
    return std::make_unique<PlainTextView>();
#endif
}

} // namespace Inkscape::UI::Syntax

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
