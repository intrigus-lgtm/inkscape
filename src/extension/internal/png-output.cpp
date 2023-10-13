// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * An internal raster export which passes the generated PNG output
 * to an external file. In the future this module could host more of
 * the PNG generation code that isn't needed for other raster export options.
 *
 * Authors:
 *   Martin Owens <doctormo@geek-2.com>
 *
 * Copyright (C) 2021 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "png-output.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <glibmm.h>
#include <giomm.h>

#include "clear-n_.h"

namespace Inkscape {
namespace Extension {
namespace Internal {

void PngOutput::export_raster(Inkscape::Extension::Output * /*module*/,
        const SPDocument * /*doc*/, std::string const &png_file, gchar const *filename)
{
    // We want to move the png file to the new location
    Glib::RefPtr<Gio::File> input_fn = Gio::File::create_for_path(png_file);
    Glib::RefPtr<Gio::File> output_fn = Gio::File::create_for_path(filename);
    try {
        // This file must be copied because the permissions must be created
        // based on it's target location and not the temp directory.
        input_fn->copy(output_fn, Gio::File::CopyFlags::OVERWRITE | Gio::File::CopyFlags::TARGET_DEFAULT_PERMS);
    }
    catch (const Gio::Error& e) {
        std::cerr << "Moving resource " << png_file
                  << " to "             << filename
                  << " failed: "        << e.what() << std::endl;
    }
}

void PngOutput::init()
{
    // clang-format off
    Inkscape::Extension::build_from_mem(
        "<inkscape-extension xmlns=\"" INKSCAPE_EXTENSION_URI "\">\n"
            "<name>" N_("Portable Network Graphic") "</name>\n"
            "<id>" SP_MODULE_KEY_RASTER_PNG "</id>\n"
            "<param name='png_interlacing' type='bool' gui-text='" N_("Interlacing") "'>false</param>"
            "<param name='png_bitdepth' type='optiongroup' appearance='combo' gui-text='" N_("Bit Depth") "'>"
               "<option value='99'>" N_("RGBA 8") "</option>" // First because it's the default option
               "<option value='100'>" N_("RGBA 16") "</option>"
               "<option value='67'>" N_("GrayAlpha 8") "</option>"
               "<option value='68'>" N_("GrayAlpha 16") "</option>"
               "<option value='35'>" N_("RGB 8") "</option>"
               "<option value='36'>" N_("RGB 16") "</option>"
               "<option value='0'>" N_("Gray 1") "</option>"
               "<option value='1'>" N_("Gray 2") "</option>"
               "<option value='2'>" N_("Gray 4") "</option>"
               "<option value='3'>" N_("Gray 8") "</option>"
               "<option value='4'>" N_("Gray 16") "</option>"
            "</param>"
            "<param name='png_compression' type='optiongroup' appearance='combo' gui-text='" N_("Compression") "'>"
               "<option value='0'>" N_("0 - No Compression") "</option>"
               "<option value='1'>" N_("1 - Best Speed") "</option>"
               "<option value='2'>2</option>"
               "<option value='3'>3</option>"
               "<option value='4'>4</option>"
               "<option value='5'>5</option>"
               "<option value='6'>" N_("6 - Default Compression") "</option>" // First because it's default (and broken)
               "<option value='7'>7</option>"
               "<option value='8'>8</option>"
               "<option value='9'>" N_("9 - Best Compression") "</option>"
            "</param>"
            "<param name='png_phys' gui-text='" N_("pHYs DPI") "' type='float' min='0.0' max='100000.0'>0.0</param>"
            "<param name='png_antialias' gui-text='" N_("Antialias") "' type='int' min='0' max='3'>2</param>"
            "<output raster=\"true\" priority=\"1\">\n"
                "<extension>.png</extension>\n"
                "<mimetype>image/png</mimetype>\n"
                "<filetypename>" N_("Portable Network Graphic (*.png)") "</filetypename>\n"
                "<filetypetooltip>" N_("Default raster graphic export") "</filetypetooltip>\n"
            "</output>\n"
        "</inkscape-extension>",
        std::make_unique<PngOutput>());
    // clang-format on
}

} // namespace Internal
} // namespace Extension
} // namespace Inkscape

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
