// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Test xml node
 */
/*
 * Authors:
 *   Ted Gould
 *
 * Copyright (C) 2020 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <list>
#include <memory>
#include <string>
#include "gtest/gtest.h"
#include "xml/repr.h"

#include <list>

TEST(XmlTest, nodeiter)
{
    auto testdoc = std::shared_ptr<Inkscape::XML::Document>(sp_repr_read_buf("<svg><g/></svg>", SP_SVG_NS_URI));
    ASSERT_TRUE(testdoc);

    auto count = 0;
    for (auto &child : *testdoc->root()) {
        ASSERT_STREQ(child.name(), "svg:g");
        count++;
    }
    ASSERT_EQ(count, 1);

    testdoc =
        std::shared_ptr<Inkscape::XML::Document>(sp_repr_read_buf("<svg><g/><g/><g><g/></g></svg>", SP_SVG_NS_URI));
    ASSERT_TRUE(testdoc);

    count = 0;
    for (auto &child : *testdoc->root()) {
        ASSERT_STREQ(child.name(), "svg:g");
        count++;
    }
    ASSERT_EQ(count, 3);

    testdoc = std::shared_ptr<Inkscape::XML::Document>(sp_repr_read_buf(R"""(
<svg>
  <g/>
  <!-- comment -->
  <g>
    <circle/>
  </g>
  <g>
    <circle id='a'/>
    <path id='b'/>
    <path id='c'/>
  </g>
</svg>
)""", SP_SVG_NS_URI));
    ASSERT_TRUE(testdoc);

    auto path = std::list<std::string>{"svg:g", "svg:path"};
    auto found = testdoc->root()->findChildPath(path);
    ASSERT_NE(found, nullptr);
    ASSERT_STREQ(found->attribute("id"), "b");

    // no such second element
    path = {"svg:g", "svg:g"};
    ASSERT_EQ(testdoc->root()->findChildPath(path), nullptr);

    // no such first element
    path = {"svg:symbol", "svg:path"};
    ASSERT_EQ(testdoc->root()->findChildPath(path), nullptr);

    // root with no children
    testdoc = std::shared_ptr<Inkscape::XML::Document>(sp_repr_read_buf("<svg/>", SP_SVG_NS_URI));
    ASSERT_EQ(testdoc->root()->findChildPath(path), nullptr);
}

TEST(XmlQuoteTest, nodeiter)
{
    auto testdoc = std::shared_ptr<Inkscape::XML::Document>(sp_repr_read_buf("<svg attr='&lt;foo&#10;bar\n&quot;amp&amp;&gt;'>\nTEXT\n&#10;NODE\n<g><![CDATA[TEST&#10;CDATA]]></g></svg>", SP_SVG_NS_URI));
    ASSERT_STREQ(testdoc->root()->attribute("attr"), "<foo\nbar \"amp&>");

    for (auto &child : *testdoc->root()) {
        ASSERT_STREQ(child.content(), "\nTEXT\n\nNODE\n");
        break;
    }

    auto content = sp_repr_save_buf(testdoc.get());
    ASSERT_STREQ(content.c_str(), R"""(<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg:svg
   attr="&lt;foo&#10;bar &quot;amp&amp;&gt;"
   xmlns:svg="http://www.w3.org/2000/svg">
TEXT

NODE
<svg:g><![CDATA[TEST&#10;CDATA]]></svg:g>
</svg:svg>
)""");
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
