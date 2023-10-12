// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2005-2007 Authors:
 *   Ted Gould <ted@gould.cx>
 *   Johan Engelen <johan@shouraizou.nl> *
 *   Jon A. Cruz <jon@joncruz.org>
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "parameter.h"

#ifndef INK_EXTENSION_PARAMFLOAT_H_SEEN
#define INK_EXTENSION_PARAMFLOAT_H_SEEN

namespace Gtk {
class Widget;
} // namespace Gtk

namespace Inkscape {

namespace XML {
class Node;
} // namespace XML

namespace Extension {

class ParamFloat : public InxParameter {
public:
    enum AppearanceMode {
        DEFAULT, FULL
    };

    ParamFloat(Inkscape::XML::Node *xml, Inkscape::Extension::Extension *ext);

    /** Returns \c _value. */
    double get() const { return _value; }
    double set(double in);

    double max () { return _max; }
    double min () { return _min; }
    double precision () { return _precision; }

    Gtk::Widget *get_widget(sigc::signal<void ()> *changeSignal) override;

    std::string value_to_string() const override;
    void string_to_value(const std::string &in) override;

private:
    /** Internal value. */
    double _value = 0;

    /** limits */
    // TODO: do these defaults make sense or should we be unbounded by default?
    double _min = 0;
    double _max = 10;

    /** numeric precision (i.e. number of digits) */
    int _precision = 1;

    /** appearance mode **/
    AppearanceMode _mode = DEFAULT;
};

} // namespace Extension
} // namespace Inkscape

#endif /* INK_EXTENSION_PARAMFLOAT_H_SEEN */

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
