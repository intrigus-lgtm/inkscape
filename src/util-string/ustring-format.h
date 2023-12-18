// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2002 The gtkmm Development Team
 *
 * Functions to format output in one of:
 *   1. The default locale:     format_default()
 *   2. A specified locale:     format_locale()
 *   3. The classic "C" locale: format_classic()
 *
 * Based on Glib::ustring::format()  (glibmm/glib/glibmm/ustring.cc)
 * (FormatStream is a private class, thus we provide a copy here.)
 *
 * Can be replaced when Apple Clang supports std::format.
 */

#ifndef SEEN_INKSCAPE_USTRING_FORMAT_H
#define SEEN_INKSCAPE_USTRING_FORMAT_H

#include <glibmm/error.h>
#include <glibmm/ustring.h>
#include <glibmm/utility.h> // make_unique_ptr_gfree()

namespace Inkscape::ustring {

class FormatStream {

public:
    // noncopyable
    FormatStream(const FormatStream&) = delete;
    FormatStream& operator=(const FormatStream&) = delete;

private:
#ifdef GLIBMM_HAVE_WIDE_STREAM
    using StreamType = std::wostringstream;
#else
    using StreamType = std::ostringstream;
#endif
    StreamType stream_;

public:
    FormatStream() : stream_() {}
    FormatStream(const std::locale& locale) {
        stream_.imbue(locale);
    };
    ~FormatStream() noexcept {};

    template <class T>
    inline void stream(const T& value) { stream_ << value; }

    inline void stream(const char* value) { stream_ << Glib::ustring(value); }

    // This overload exists to avoid the templated stream() being called for non-const char*.
    inline void stream(char* value) { stream_ << Glib::ustring(value); }

    Glib::ustring to_string() const {
        GError* error = nullptr;

#ifdef GLIBMM_HAVE_WIDE_STREAM
        const std::wstring str = stream_.str();

#if (defined(__STDC_ISO_10646__) || defined(_LIBCPP_VERSION)) && SIZEOF_WCHAR_T == 4
        // Avoid going through iconv if wchar_t always contains UCS-4.
        glong n_bytes = 0;
        const auto buf = Glib::make_unique_ptr_gfree(g_ucs4_to_utf8(
                                                         reinterpret_cast<const gunichar*>(str.data()), str.size(), nullptr, &n_bytes, &error));
#elif defined(G_OS_WIN32) && SIZEOF_WCHAR_T == 2
        // Avoid going through iconv if wchar_t always contains UTF-16.
        glong n_bytes = 0;
        const auto buf = Glib::make_unique_ptr_gfree(g_utf16_to_utf8(
                                                         reinterpret_cast<const gunichar2*>(str.data()), str.size(), nullptr, &n_bytes, &error));
#else
        gsize n_bytes = 0;
        const auto buf = Glib::make_unique_ptr_gfree(g_convert(reinterpret_cast<const char*>(str.data()),
                                                               str.size() * sizeof(std::wstring::value_type), "UTF-8", "WCHAR_T", nullptr, &n_bytes, &error));
#endif /* !(__STDC_ISO_10646__ || G_OS_WIN32) */

#else /* !GLIBMM_HAVE_WIDE_STREAM */
        const std::string str = stream_.str();

        gsize n_bytes = 0;
        const auto buf =
            Glib::make_unique_ptr_gfree(g_locale_to_utf8(str.data(), str.size(), 0, &n_bytes, &error));
#endif /* !GLIBMM_HAVE_WIDE_STREAM */

        if (error) {
            Glib::Error::throw_exception(error);
        }

        return Glib::ustring(buf.get(), buf.get() + n_bytes);
    }
};

template <class... Ts>
inline static
Glib::ustring format_default(const Ts&... args)
{
    FormatStream buf({std::locale("")});
    (buf.stream(args), ...);
    return buf.to_string();
}

template <class... Ts>
inline static
Glib::ustring format_locale(std::string const &locale, const Ts&... args)
{
    FormatStream buf({std::locale(locale)});
    (buf.stream(args), ...);
    return buf.to_string();
}

template <class... Ts>
inline static
Glib::ustring format_classic(const Ts&... args)
{
    FormatStream buf(std::locale::classic());
    (buf.stream(args), ...);
    return buf.to_string();
}

} // namespace Inkscape::ustring

#endif // SEEN_INKSCAPE_USTRING_FORMAT_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
