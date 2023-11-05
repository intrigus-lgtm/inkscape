// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * SVG Extension test
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2020 Authors
 *
 * Released under GNU GPL version 2 or later, read the file 'COPYING' for more information
 */

#include <gtest/gtest.h>

#include <glib/gstdio.h>

#include "document.h"
#include "inkscape.h"

#include "actions/actions-svg-processing.h"
#include "extension/db.h"
#include "extension/input.h"
#include "extension/internal/svg.h"
#include "object/sp-text.h"
#include "object/sp-tspan.h"

using namespace Inkscape;
using namespace Inkscape::Extension;
using namespace Inkscape::Extension::Internal;

class SvgExtensionTest : public ::testing::Test {
  public:
    static std::string create_file(const std::string &filename, const std::string &content)
    {
        std::stringstream path_builder;
        path_builder << "SvgExtensionTest_" << _files.size() << "_" << filename;
        std::string path = path_builder.str();
        GError *error = nullptr;
        if (!g_file_set_contents(path.c_str(), content.c_str(), content.size(), &error)) {
            std::stringstream msg;
            msg << "SvgExtensionTest::create_file failed: GError(" << error->domain << ", " << error->code << ", "
                << error->message << ")";
            g_error_free(error);
            throw std::runtime_error(msg.str());
        }
        _files.insert(path);
        return path;
    }

    static std::set<std::string> _files;

  protected:
    void SetUp() override
    {
        // setup hidden dependency
        Application::create(false);
    }

    static void TearDownTestCase()
    {
        for (auto file : _files) {
            if (g_remove(file.c_str())) {
                std::cout << "SvgExtensionTest was unable to remove file: " << file << std::endl;
            }
        }
    }
};

std::set<std::string> SvgExtensionTest::_files;

TEST_F(SvgExtensionTest, openingAsLinkInImageASizelessSvgFileReturnsNull)
{
    std::string sizeless_svg_file =
        create_file("sizeless.svg",
                    "<svg><path d=\"M 71.527648,186.14229 A 740.48715,740.48715 0 0 0 696.31258,625.8041 Z\"/></svg>");
    
    Svg::init();
    Input *svg_input_extension(dynamic_cast<Input *>(db.get(SP_MODULE_KEY_INPUT_SVG))); 
    
    Preferences *prefs = Preferences::get();
    prefs->setBool("/options/onimport", true);
    prefs->setBool("/dialogs/import/ask_svg", false);
    prefs->setString("/dialogs/import/import_mode_svg", "link");

    ASSERT_EQ(svg_input_extension->open(sizeless_svg_file.c_str()), nullptr);
}

TEST_F(SvgExtensionTest, hiddenSvg2TextIsSaved)
{
    char const *docString = R"""(
<svg width="100" height="200">
  <defs>
    <rect id="rect1" x="0" y="0"   width="100" height="100" />
    <rect id="rect2" x="0" y="100" width="100" height="100" />
  </defs>
  <g>
    <text id="text1" style="shape-inside:url(#rect1);display:inline;">
      <tspan id="tspan1" x="0" y="0">foo</tspan>
    </text>
    <text id="text2" style="shape-inside:url(#rect2);display:none;"  >
      <tspan id="tspan2" x="0" y="0">bar</tspan>
    </text>
  </g>
</svg>
)""";
    SPDocument *doc = SPDocument::createNewDocFromMem(docString, static_cast<int>(strlen(docString)), false);
    ASSERT_TRUE(doc);

    std::map<std::string,std::string> textMap;
    textMap["text1"] = "foo";
    textMap["text2"] = "bar";

    // otherwise the layout reports a size of 0
    for (const auto& kv : textMap) {
        auto textElement = cast<SPText>(doc->getObjectById(kv.first));
        ASSERT_TRUE(textElement);
        textElement->rebuildLayout();
    }

    Inkscape::XML::Document *rdoc = doc->getReprDoc();
    ASSERT_TRUE(rdoc);

    insert_text_fallback(rdoc->root(), doc);

    for(const auto& kv : textMap) {
        auto textElement = doc->getObjectById(kv.first);
        ASSERT_TRUE(textElement);
        auto tspanElement = textElement->firstChild();
        ASSERT_TRUE(tspanElement);
        auto stringElement = cast<SPString>(tspanElement->firstChild());
        ASSERT_TRUE(stringElement);
        ASSERT_EQ(kv.second, stringElement->string.raw());
    }
}
