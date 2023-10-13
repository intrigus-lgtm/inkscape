// SPDX-License-Identifier: GPL-2.0-or-later OR MPL-1.1 OR LGPL-2.1-or-later
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Eek Color Definition.
 *
 * The Initial Developer of the Original Code is
 * Jon A. Cruz.
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "paintdef.h"

#include <glibmm/ustring.h>
#include <memory>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <glibmm/i18n.h>
#include <glibmm/stringutils.h>
#include <glibmm/regex.h>

static char const *mimeOSWB_COLOR = "application/x-oswb-color";
static char const *mimeX_COLOR = "application/x-color";
static char const *mimeTEXT = "text/plain";

PaintDef::PaintDef()
    : description(C_("Paint", "None"))
    , type(NONE)
    , rgb({0, 0, 0})
{
}

PaintDef::PaintDef(std::array<unsigned, 3> const &rgb, std::string description, Glib::ustring tooltip)
    : description(std::move(description)), tooltip(std::move(tooltip))
    , type(RGB)
    , rgb(rgb)
{
}

std::string PaintDef::get_color_id() const
{
    if (type == NONE) {
        return "none";
    }
    if (!description.empty() && description[0] != '#') {
        auto name = Glib::ustring(std::move(description));
        // Convert description to ascii, strip out symbols, remove duplicate dashes and prefixes
        static auto const reg1 = Glib::Regex::create("[^[:alnum:]]");
        name = reg1->replace(name, 0, "-", static_cast<Glib::Regex::MatchFlags>(0));
        static auto const reg2 = Glib::Regex::create("-{2,}");
        name = reg2->replace(name, 0, "-", static_cast<Glib::Regex::MatchFlags>(0));
        static auto const reg3 = Glib::Regex::create("(^-|-$)");
        name = reg3->replace(name, 0, "", static_cast<Glib::Regex::MatchFlags>(0));
        // Move important numbers from the start where they are invalid xml, to the end.
        static auto const reg4 = Glib::Regex::create("^(\\d+)(-?)([^\\d]*)");
        name = reg4->replace(name, 0, "\\3\\2\\1", static_cast<Glib::Regex::MatchFlags>(0));
        return name.lowercase();
    }
    auto [r, g, b] = rgb;
    char buf[12];
    std::snprintf(buf, 12, "rgb%02x%02x%02x", r, g, b);
    return std::string(buf);
}

const Glib::ustring& PaintDef::get_tooltip() const {
    return tooltip;
}

std::vector<std::string> const &PaintDef::getMIMETypes()
{
    static std::vector<std::string> mimetypes = {mimeOSWB_COLOR, mimeX_COLOR, mimeTEXT};
    return mimetypes;
}

std::pair<std::vector<char>, int> PaintDef::getMIMEData(std::string const &type) const
{
    auto from_data = [] (void const *p, int len) {
        std::vector<char> v(len);
        std::memcpy(v.data(), p, len);
        return v;
    };

    auto [r, g, b] = rgb;

    if (type == mimeTEXT) {
        std::array<char, 8> tmp;
        std::snprintf(tmp.data(), 8, "#%02x%02x%02x", r, g, b);
        return std::make_pair(from_data(tmp.data(), 8), 8);
    } else if (type == mimeX_COLOR) {
        std::array<uint16_t, 4> tmp = {(uint16_t)((r << 8) | r), (uint16_t)((g << 8) | g), (uint16_t)((b << 8) | b), 0xffff};
        return std::make_pair(from_data(tmp.data(), 8), 16);
    } else if (type == mimeOSWB_COLOR) {
        std::string tmp("<paint>");
        switch (get_type()) {
            case PaintDef::NONE:
                tmp += "<nocolor/>";
                break;
            default:
                tmp += std::string("<color name=\"") + description + "\">";
                tmp += "<sRGB r=\"";
                tmp += Glib::Ascii::dtostr(r / 255.0);
                tmp += "\" g=\"";
                tmp += Glib::Ascii::dtostr(g / 255.0);
                tmp += "\" b=\"";
                tmp += Glib::Ascii::dtostr(b / 255.0);
                tmp += "\"/>";
                tmp += "</color>";
        }
        tmp += "</paint>";
        return std::make_pair(from_data(tmp.c_str(), tmp.size()), 8);
    } else {
        return {{}, 0};
    }
}

bool PaintDef::fromMIMEData(std::string const &type_str, char const *data, int len)
{
    if (type_str == mimeTEXT) {
        // unused
    } else if (type_str == mimeX_COLOR) {
        // unused
    } else if (type_str == mimeOSWB_COLOR) {
        std::string xml(data, len);
        if (xml.find("<nocolor/>") != std::string::npos) {
            type = PaintDef::NONE;
            rgb = {0, 0, 0};
            return true;
        } else if (auto pos = xml.find("<sRGB"); pos != std::string::npos) {
            std::string srgb = xml.substr(pos, xml.find(">", pos));
            type = PaintDef::RGB;
            if (auto numPos = srgb.find("r="); numPos != std::string::npos) {
                double dbl = Glib::Ascii::strtod(srgb.substr(numPos + 3));
                rgb[0] = static_cast<int>(255 * dbl);
            }
            if (auto numPos = srgb.find("g="); numPos != std::string::npos) {
                double dbl = Glib::Ascii::strtod(srgb.substr(numPos + 3));
                rgb[1] = static_cast<int>(255 * dbl);
            }
            if (auto numPos = srgb.find("b="); numPos != std::string::npos) {
                double dbl = Glib::Ascii::strtod(srgb.substr(numPos + 3));
                rgb[2] = static_cast<int>(255 * dbl);
            }
            if (auto pos = xml.find("<color "); pos != std::string::npos) {
                std::string colorTag = xml.substr(pos, xml.find(">", pos));
                if (auto namePos = colorTag.find("name="); namePos != std::string::npos) {
                    char quote = colorTag[namePos + 5];
                    auto endPos = colorTag.find(quote, namePos + 6);
                    description = colorTag.substr(namePos + 6, endPos - (namePos + 6));
                }
            }
            return true;
        }
    }

    return false;
}

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
