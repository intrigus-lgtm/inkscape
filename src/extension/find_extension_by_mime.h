// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Find an extension by its mime type.
 */
/* Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Frank Felfe <innerspace@iname.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *   Kris De Gussem <Kris.DeGussem@gmail.com>
 *
 * Copyright (C) 2012 Kris De Gussem
 * Copyright (C) 2010 authors
 * Copyright (C) 1999-2005 authors
 * Copyright (C) 2004 David Turner
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cstring>

#include "input.h"
#include "extension/db.h"

namespace Inkscape::Extension {

inline Extension *find_by_mime(char const *mime)
{
    DB::InputList list;
    db.get_input_list(list);

    auto it = list.begin();
    while (true) {
        if (it == list.end()) return nullptr;
        if (std::strcmp((*it)->get_mimetype(), mime) == 0) return *it;
        ++it;
    }
}

} // namespace Inkscape::Extension
