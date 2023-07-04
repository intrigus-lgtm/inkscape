// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   Ted Gould <ted@gould.cx>
 *   Abhishek Sharma
 *   Sushant A.A. <sushant.co19@gmail.com>
 *
 * Copyright (C) 2002-2007 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "effect.h"

#include <string>

#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include "execution-env.h"
#include "inkscape-application.h"
#include "inkscape.h"
#include "streq.h"
#include "timer.h"

#include "actions/actions-effect.h"
#include "extension/internal/filter/filter.h"  // for Filter
#include "implementation/implementation.h"
#include "io/sys.h"
#include "prefdialog/prefdialog.h"
#include "xml/node.h"

/* Inkscape::Extension::Effect */

namespace Inkscape {
namespace Extension {

Effect * Effect::_last_effect = nullptr;

Effect::Effect(Inkscape::XML::Node *in_repr, ImplementationHolder implementation, std::string *base_directory, std::string* file_name)
    : Extension(in_repr, std::move(implementation), base_directory)
    , _menu_node(nullptr)
    , _prefDialog(nullptr)
{
    // can't use document level because it is not defined
    static auto app = InkscapeApplication::instance();

    if (!app) {
        // This happens during tests.
        return;
    }

    if (!Inkscape::Application::exists()) {
        return;
    }

    // This is a weird hack
    if (!strcmp(this->get_id(), "org.inkscape.filter.dropshadow"))
        return;

    if (file_name) {
        _file_name = *file_name;
    }

    no_doc = false;
    no_live_preview = false;

    if (repr != nullptr) {
        for (Inkscape::XML::Node *child = repr->firstChild(); child != nullptr; child = child->next()) {
            // look for "effect"
            if (strcmp(child->name(), INKSCAPE_EXTENSION_NS "effect")) continue;

            if (child->attribute("needs-document") && !strcmp(child->attribute("needs-document"), "false")) {
                no_doc = true;
            }
            if (child->attribute("needs-live-preview") && !strcmp(child->attribute("needs-live-preview"), "false")) {
                no_live_preview = true;
            }
            if (child->attribute("implements-custom-gui") && !strcmp(child->attribute("implements-custom-gui"), "true")) {
                _workingDialog = false;
                ignore_stderr = true;
            }
            for (auto effect_child = child->firstChild(); effect_child != nullptr; effect_child = effect_child->next()) {
                if (!strcmp(effect_child->name(), INKSCAPE_EXTENSION_NS "effects-menu")) {
                    _local_effects_menu = effect_child->firstChild();
                    if (effect_child->attribute("hidden") && !strcmp(effect_child->attribute("hidden"), "true")) {
                        _hidden_from_menu = true;
                    }
                }
                if (!strcmp(effect_child->name(), INKSCAPE_EXTENSION_NS "menu-tip") ||
                        !strcmp(effect_child->name(), INKSCAPE_EXTENSION_NS "_menu-tip")) {
                    _menu_tip = effect_child->firstChild()->content();
                }
                if (streq(effect_child->name(), INKSCAPE_EXTENSION_NS "icon")) {
                    _icon_path = effect_child->firstChild()->content();
                }
            } // children of "effect"
            break; // there can only be one effect
        } // children of "inkscape-extension"
    } // if we have an XML file

    _filter_effect = dynamic_cast<Internal::Filter::Filter *>(imp.get()) != nullptr;
}

/** Sanitizes the id and returns. If an invalid character is found in the ID, a warning
 *  is printed to stderr. All invalid characters are replaced with an 'X'.
 */
std::string Effect::get_sanitized_id() const
{
    std::string id = get_id();
    auto allowed = [] (char ch) {
        // Note: std::isalnum() is locale-dependent
        if ('A' <= ch && ch <= 'Z') return true;
        if ('a' <= ch && ch <= 'z') return true;
        if ('0' <= ch && ch <= '9') return true;
        if (ch == '.' || ch == '-') return true;
        return false;
    };

    // Silently replace any underscores with dashes.
    std::replace(id.begin(), id.end(), '_', '-');

    // Detect remaining invalid characters and print a warning if found
    bool errored = false;
    for (auto &ch : id) {
        if (!allowed(ch)) {
            if (!errored) {
                auto message = std::string{"Invalid extension action ID found: \""} + id + "\".";
                g_warn_message("Inkscape", __FILE__, __LINE__, "Effect::_sanitizeId()", message.c_str());
                errored = true;
            }
            ch = 'X';
        }
    }
    return id;
}


void Effect::get_menu(Inkscape::XML::Node * pattern, std::list<Glib::ustring>& sub_menu_list) const
{
    if (!pattern) {
        return;
    }

    Glib::ustring merge_name;

    gchar const *menu_name = pattern->attribute("name");
    if (!menu_name) {
        menu_name = pattern->attribute("_name");
    }
    if (!menu_name) {
        return;
    }

    if (_translation_enabled) {
        merge_name = get_translation(menu_name);
    } else {
        merge_name = _(menu_name);
    }

    // Making sub menu string
    sub_menu_list.push_back(merge_name);

    get_menu(pattern->firstChild(), sub_menu_list);
}

void
Effect::deactivate()
{
    /* FIXME: https://gitlab.com/inkscape/inkscape/-/issues/4381
     * Effects don't have actions anymore, so this is not possible:
    if (action)
        action->set_enabled(false);
    if (action_noprefs)
        action_noprefs->set_enabled(false);
    */
    Extension::deactivate();
}

Effect::~Effect ()
{
    if (get_last_effect() == this)
        set_last_effect(nullptr);
    if (_menu_node) {
        if (_menu_node->parent()) {
            _menu_node->parent()->removeChild(_menu_node);
        }
        Inkscape::GC::release(_menu_node);
    }
    return;
}

bool
Effect::prefs (SPDesktop * desktop)
{
    if (_prefDialog != nullptr) {
        _prefDialog->present();
        return true;
    }

    if (!widget_visible_count()) {
        effect(desktop);
        return true;
    }

    if (!loaded())
        set_state(Extension::STATE_LOADED);
    if (!loaded()) return false;

    Glib::ustring name = this->get_name();
    _prefDialog = new PrefDialog(name, nullptr, this);
    _prefDialog->set_visible(true);

    return true;
}

/**
    \brief  The function that 'does' the effect itself
    \param  desktop  The desktop containing the document to do the effect on

    This function first insures that the extension is loaded, and if not,
    loads it.  It then calls the implementation to do the actual work.  It
    also resets the last effect pointer to be this effect.  Finally, it
    executes a \c SPDocumentUndo::done to commit the changes to the undo
    stack.
*/
void Effect::effect(SPDesktop *desktop, SPDocument *document)
{
    //printf("Execute effect\n");
    if (!loaded())
        set_state(Extension::STATE_LOADED);
    if (!loaded()) return;
    ExecutionEnv executionEnv(this, desktop, nullptr, _workingDialog, true);
    execution_env = &executionEnv;
    if (document)
        execution_env->set_document(document);
    timer->lock();
    executionEnv.run();
    if (executionEnv.wait()) {
        executionEnv.commit();
    } else {
        executionEnv.cancel();
    }
    timer->unlock();

    return;
}

/** \brief  Sets which effect was called last
    \param in_effect  The effect that has been called

    This function sets the static variable \c _last_effect
*/
void
Effect::set_last_effect (Effect * in_effect)
{
    _last_effect = in_effect;
    if (in_effect) {
        enable_effect_actions(InkscapeApplication::instance(), true);
    }
}

Inkscape::XML::Node *
Effect::find_menu (Inkscape::XML::Node * menustruct, const gchar *name)
{
    if (menustruct == nullptr) return nullptr;
    for (Inkscape::XML::Node * child = menustruct;
            child != nullptr;
            child = child->next()) {
        if (!strcmp(child->name(), name)) {
            return child;
        }
        Inkscape::XML::Node * firstchild = child->firstChild();
        if (firstchild != nullptr) {
            Inkscape::XML::Node *found = find_menu (firstchild, name);
            if (found) {
                return found;
            }
        }
    }
    return nullptr;
}


Gtk::Box *
Effect::get_info_widget()
{
    return Extension::get_info_widget();
}

PrefDialog *
Effect::get_pref_dialog ()
{
    return _prefDialog;
}

void
Effect::set_pref_dialog (PrefDialog * prefdialog)
{
    _prefDialog = prefdialog;
    return;
}

// Try locating effect's thumbnail file using:
// <icon> path
// or extension's file name
// or extension's ID
std::string Effect::find_icon_file(const std::string& default_dir) const {
    auto& dir = _base_directory.empty() ? default_dir : _base_directory;

    if (!dir.empty()) {
        // icon path provided?
        if (!_icon_path.empty()) {
            auto path = Glib::build_filename(dir, _icon_path);
            if (Glib::file_test(path, Glib::FileTest::IS_REGULAR)) {
                return path;
            }
        }
        else {
            // fallback 1: try the same name as extension file, but with ".svg" instead of ".inx"
            if (!_file_name.empty()) {
                auto ext = Inkscape::IO::get_file_extension(_file_name);
                auto filename = _file_name.substr(0, _file_name.size() - ext.size());
                auto path = Glib::build_filename(dir, filename + ".svg");
                if (Glib::file_test(path, Glib::FileTest::IS_REGULAR)) {
                    return path;
                }
            }
            // fallback 2: look for icon in extension's folder, inside "icons", this time using extension ID as a name
            std::string id = get_id();
            auto path = Glib::build_filename(dir, "icons", id + ".svg");
            if (Glib::file_test(path, Glib::FileTest::IS_REGULAR)) {
                return path;
            }
        }
    }

    return std::string();
}

bool Effect::hidden_from_menu() const {
    return _hidden_from_menu;
}

bool Effect::takes_input() const {
    return widget_visible_count() > 0;
}

bool Effect::is_filter_effect() const {
    return _filter_effect;
}

const Glib::ustring& Effect::get_menu_tip() const {
    return _menu_tip;
}

std::list<Glib::ustring> Effect::get_menu_list() const {
    std::list<Glib::ustring> menu;
    if (_local_effects_menu) {
        get_menu(_local_effects_menu, menu);

        // remove "Filters" from sub menu hierarchy to keep it the same as extension effects
        if (_filter_effect) menu.pop_front();
    }
    return menu;
}

bool Effect::apply_filter(SPItem* item) {
    return get_imp()->apply_filter(this, item);
}

} }  /* namespace Inkscape, Extension */

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
