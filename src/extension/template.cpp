// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Martin Owens <doctormo@geek-2.com>
 *
 * Copyright (C) 2022 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "template.h"

#include <string>
#include <glibmm/i18n.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <glibmm/regex.h>

#include "document.h"
#include "document-undo.h"
#include "implementation/implementation.h"
#include "io/file.h"
#include "io/resource.h"
#include "xml/attribute-record.h"
#include "xml/repr.h"

using namespace Inkscape::IO::Resource;

namespace Inkscape {
namespace Extension {

/**
 * Parse the inx xml node for preset information.
 */
TemplatePreset::TemplatePreset(Template *mod, const Inkscape::XML::Node *repr, TemplatePrefs const prefs, int priority)
    : _mod(mod)
    , _prefs(prefs)
    , _name("Unnamed")
    , _label("")
    , _visibility(mod->get_visibility())
    , _priority(priority)
{
    // Default icon and priority aren't a prefs, though they may at first look like it.
    _icon = mod->get_icon();
    _category = mod->get_category();

    if (repr) {
        for (const auto &iter : repr->attributeList()) {
            std::string name = g_quark_to_string(iter.key);
            std::string value = std::string(iter.value);
            if (name == "name")
                _name = value.empty() ? "?" : value;
            else if (name == "label")
                _label = value;
            else if (name == "desc")
                _desc = value;
            else if (name == "icon")
                _icon = value;
            else if (name == "priority")
                _priority = std::stoi(value);
            else if (name == "visibility") {
                _visibility = mod->parse_visibility(value);
            } else {
                _prefs[name] = value;
            }
        }
    }
    // Generate a standard name that can be used to recall this preset.
    _key = std::string(mod->get_id()) + "." + _name;
    transform(_key.begin(), _key.end(), _key.begin(), ::tolower);
}

/*
 * Return the best full path to the icon.
 *
 *  1. Searches the template/icons folder.
 *  2. Searches the inx folder location (if any)
 *  3. Returns a default icon file path.
 */
std::string TemplatePreset::get_icon_path() const
{
    static auto default_icon = _get_icon_path("default");
    auto filename = _get_icon_path(_icon);
    return filename.empty() ? default_icon : filename;
}

std::string TemplatePreset::_get_icon_path(std::string const &name) const
{
    auto filename = name + ".svg";

    auto const filepath = Glib::build_filename("icons", filename);
    auto const fullpath = get_filename(TEMPLATES, filepath.c_str(), false, true);
    if (!fullpath.empty()) return fullpath;

    auto base = _mod->get_base_directory();
    if (!base.empty()) {
        auto base_icon = Glib::build_filename(base, "icons", filename);
        if (Glib::file_test(base_icon, Glib::FileTest::EXISTS)) {
            return base_icon;
        }
    }
    return {};
}

/**
 * Setup the preferences and ask the user to fill in the remaineder.
 *
 * @param others - populate with these prefs on top of internal prefs.
 *
 * @return True if preferences have been shown or not using GUI, False is canceled.
 *
 * Can cause a GUI popup.
 */
bool TemplatePreset::setup_prefs(const TemplatePrefs &others)
{
    _add_prefs(_prefs);
    _add_prefs(others);

    bool ret = _mod->prefs();
    for (auto pref : _prefs) {
        try {
            _mod->set_param_hidden(pref.first.c_str(), false);
        } catch (Extension::param_not_exist) {
            // pass
        }
    }
    return ret;
}

/**
 * Called by setup_prefs to save the given prefs into this extension.
 */
void TemplatePreset::_add_prefs(const TemplatePrefs &prefs)
{
    for (auto pref : prefs) {
        try {
            _mod->set_param_any(pref.first.c_str(), pref.second);
            _mod->set_param_hidden(pref.first.c_str(), true);
        } catch (Extension::param_not_exist) {
            // pass
        }
    }
}

/**
 * Generate a new document from this preset.
 *
 * Sets the preferences and then calls back to it's parent extension.
 */
SPDocument *TemplatePreset::new_from_template(const TemplatePrefs &others)
{
    if (setup_prefs(others)) {
        return _mod->new_from_template();
    }
    return nullptr;
}

/**
 * Resize the given page to however the page format requires it to be.
 */
void TemplatePreset::resize_to_template(SPDocument *doc, SPPage *page, const TemplatePrefs &others)
{
    if (_mod->can_resize() && setup_prefs(others)) {
        _mod->resize_to_template(doc, page);
    }
}

/**
 * Reverse match for templates, allowing page duplication and labeling
 */
bool TemplatePreset::match_size(double width, double height, const TemplatePrefs &others)
{
    if (is_visible(TEMPLATE_SIZE_SEARCH) || is_visible(TEMPLATE_SIZE_LIST)) {
        _add_prefs(_prefs);
        _add_prefs(others);
        return _mod->imp->match_template_size(_mod, width, height);
    }
    return false;
}

/**
    \brief    Builds a Template object from a XML description
    \param    in_repr The XML description in a Inkscape::XML::Node tree
    \param    implementation The implementation of the extension
*/
Template::Template(Inkscape::XML::Node *in_repr, ImplementationHolder implementation, std::string *base_directory)
    : Extension(in_repr, std::move(implementation), base_directory)
{
    if (!repr) {
        return;
    }
    auto const t_node = sp_repr_lookup_name(repr, INKSCAPE_EXTENSION_NS "template", 1);
    if (!t_node) {
        return;
    }
    _source = sp_repr_lookup_content(repr, INKSCAPE_EXTENSION_NS "source");
    _desc = sp_repr_lookup_content(repr, INKSCAPE_EXTENSION_NS "description");
    _category = sp_repr_lookup_content(repr, INKSCAPE_EXTENSION_NS "category", N_("Other"));

    // Remember any global/default preferences from the root node.
    TemplatePrefs prefs;
    for (const auto &iter : t_node->attributeList()) {
        std::string name = g_quark_to_string(iter.key);
        std::string value = std::string(iter.value);
        if (name == "icon") {
            _icon = value;
        } else if (name == "visibility") {
            _visibility = parse_visibility(value);
        } else if (name == "priority") {
            set_sort_priority(std::stoi(value));
        } else {
            prefs[name] = value;
        }
    }

    // Default priority will increment to keep inx order where possible.
    int priority = get_sort_priority();
    for (auto p_node : sp_repr_lookup_name_many(t_node, INKSCAPE_EXTENSION_NS "preset")) {
        auto preset = new TemplatePreset(this, p_node, prefs, priority);
        _presets.emplace_back(preset);
        priority += 1;
        // If any preset is resizable, then the module is considered to support it.
        if ( preset->is_visible(TEMPLATE_SIZE_SEARCH)
          || preset->is_visible(TEMPLATE_SIZE_LIST)) {
            _can_resize = true;
        }
    }
    // Keep presets sorted internally for simple use cases.
    std::sort(std::begin(_presets), std::end(_presets),
        [](std::shared_ptr<TemplatePreset> const &a,
           std::shared_ptr<TemplatePreset> const &b) {
        return a->get_sort_priority() < b->get_sort_priority();
    });
}

/**
 * Parse the expected value for the visibility value, turn into enum.
 */
int Template::parse_visibility(const std::string &value)
{
    int ret = 0;
    auto values = Glib::Regex::split_simple("," , value.c_str());
    for (auto const &val : values) {
        ret |= (val == "icon") * TEMPLATE_NEW_ICON;
        ret |= (val == "list") * TEMPLATE_SIZE_LIST;
        ret |= (val == "search") * TEMPLATE_SIZE_SEARCH;
        ret |= (val == "all") * TEMPLATE_ALL;
    }
    return ret;
}

/**
    \return  Whether this extension checks out
    \brief   Validate this extension

    This function checks to make sure that the template extension has
    a filename extension and a MIME type.  Then it calls the parent
    class' check function which also checks out the implementation.
*/
bool Template::check()
{
    if (_category.empty()) {
        return false;
    }
    return Extension::check();
}

/**
    \return  A new document
    \brief   This function creates a document from a template

    This function acts as the first step in creating a new document.
*/
SPDocument *Template::new_from_template()
{
    if (!loaded()) {
        set_state(Extension::STATE_LOADED);
    }
    if (!loaded()) {
        return nullptr;
    }

    SPDocument *const doc = imp->new_from_template(this);
    DocumentUndo::clearUndo(doc);
    doc->setModifiedSinceSave(false);
    return doc;
}

/**
 * Takes an existing page and resizes it to the required dimentions.
 *
 * @param doc - The active document to change
 * @param page - The select page to resize, or nullptr if not multipage.
 */
void Template::resize_to_template(SPDocument *doc, SPPage *page)
{
    if (!loaded()) {
        set_state(Extension::STATE_LOADED);
    }
    if (!loaded()) {
        return;
    }
    imp->resize_to_template(this, doc, page);
}

/**
 * Return a list of all template presets.
 */
TemplatePresets Template::get_presets(TemplateShow visibility) const
{
    auto all_presets = _presets;
    imp->get_template_presets(this, all_presets);

    TemplatePresets ret;
    for (auto preset : all_presets) {
        if (preset->is_visible(visibility)) {
            ret.push_back(preset);
        }
    }
    return ret;
}

/**
 * Return the template preset based on the key from this template class.
 */
std::shared_ptr<TemplatePreset> Template::get_preset(const std::string &key)
{
    for (auto preset : get_presets()) {
        if (preset->get_key() == key) {
            return preset;
        }
    }
    return nullptr;
}

/**
 * Matches the given page against the given page.
 */
std::shared_ptr<TemplatePreset> Template::get_preset(double width, double height)
{
    for (auto preset : get_presets()) {
        if (preset->match_size(width, height)) {
            return preset;
        }
    }
    return nullptr;
}

/**
 * Return the template preset based on the key from any template class (static method).
 */
std::shared_ptr<TemplatePreset> Template::get_any_preset(const std::string &key)
{
    Inkscape::Extension::DB::TemplateList extensions;
    Inkscape::Extension::db.get_template_list(extensions);
    for (auto tmod : extensions) {
        if (auto preset = tmod->get_preset(key)) {
            return preset;
        }
    }   
    return nullptr;
}

/**
 * Return the template preset based on the key from any template class (static method).
 */
std::shared_ptr<TemplatePreset> Template::get_any_preset(double width, double height)
{
    Inkscape::Extension::DB::TemplateList extensions;
    Inkscape::Extension::db.get_template_list(extensions);
    for (auto tmod : extensions) {
        if (!tmod->can_resize())
            continue;
        if (auto preset = tmod->get_preset(width, height)) {
            return preset;
        }
    }   
    return nullptr;
}

/**
 * Get the template filename, or return the default template
 */
Glib::RefPtr<Gio::File> Template::get_template_filename() const
{
    Glib::RefPtr<Gio::File> file;

    if (!_source.empty()) {
        auto const filename = get_filename(TEMPLATES, _source.c_str(), true);
        file = Gio::File::create_for_path(filename);
    }
    if (!file) {
        // Failure to open, so open up a new document instead.
        auto const filename = get_filename(TEMPLATES, "default.svg", true);
        file = Gio::File::create_for_path(filename);

        if (!file) {
            g_error("Can not find default.svg template!");
        }
    }
    return file;
}

/**
 * Get the raw document svg for this template (pre-processing).
 */
SPDocument *Template::get_template_document() const
{
    if (auto file = get_template_filename()) {
        return ink_file_new(file->get_path());
    }
    return nullptr;
}

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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
