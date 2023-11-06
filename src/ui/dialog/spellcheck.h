// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief  Spellcheck dialog
 */
/* Authors:
 *   bulia byak <bulia@users.sf.net>
 *
 * Copyright (C) 2009 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_SPELLCHECK_H
#define SEEN_SPELLCHECK_H

#ifdef HAVE_CONFIG_H
# include "config.h"  // only include where actually required!
#endif

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>
#include <gtkmm/treeview.h>
#include <set>
#include <vector>

#include "text-editing.h"
#include "ui/dialog/dialog-base.h"
#include "display/control/canvas-item-ptr.h"

#if WITH_GSPELL
#include <gspell/gspell.h>
#endif  /* WITH_GSPELL */

class SPObject;
class SPItem;
class SPCanvasItem;

namespace Inkscape {
class Preferences;
class CanvasItemRect;

namespace UI {
namespace Dialog {

using LanguagePair = std::pair<std::string, std::string>;

/**
 *
 * A dialog widget to checking spelling of text elements in the document
 * Uses gspell and one of the languages set in the users preference file
 *
 */
class SpellCheck : public DialogBase
{
public:
    SpellCheck();
    ~SpellCheck() override;

    static std::vector<LanguagePair> get_available_langs();

private:
    void documentReplaced() override;

    /**
     * Remove the highlight rectangle form the canvas
     */
    void clearRects();

    /**
     * Release handlers to the selected item
     */
    void disconnect();

    /**
     * Returns a list of all the text items in the SPObject
     */
    void allTextItems (SPObject *r, std::vector<SPItem *> &l, bool hidden, bool locked);

    /**
     * Is text inside the SPOject's tree
     */
    bool textIsValid (SPObject *root, SPItem *text);

    /**
     * Compare the visual bounds of 2 SPItems referred to by a and b
     */
    static bool compareTextBboxes(SPItem const *i1, SPItem const *i2);
    SPItem *getText (SPObject *root);
    void    nextText ();

    /**
     * Cleanup after spellcheck is finished
     */
    void    finished ();

    /**
     * Find the next word to spell check
     */
    bool    nextWord();
    void    deleteLastRect ();
    void    doSpellcheck ();

    /**
     * Update speller from language combobox
     * @return true if update was successful
     */
    bool updateSpeller();
    void deleteSpeller();

    /**
     * Accept button clicked
     */
    void    onAccept ();

    /**
     * Ignore button clicked
     */
    void    onIgnore ();

    /**
     * Ignore once button clicked
     */
    void    onIgnoreOnce ();

    /**
     * Add button clicked
     */
    void    onAdd ();

    /**
     * Stop button clicked
     */
    void    onStop ();

    /**
     * Start button clicked
     */
    void    onStart ();

    /**
     * Language selection changed
     */
    void    onLanguageChanged();

    /**
     * Selected object modified on canvas
     */
    void    onObjModified (SPObject* /* blah */, unsigned int /* bleh */);

    /**
     * Selected object removed from canvas
     */
    void    onObjReleased (SPObject* /* blah */);

    /**
     * Selection in suggestions text view changed
     */
    void onTreeSelectionChange();

    SPObject *_root;

#if WITH_GSPELL
    GspellChecker *_checker = nullptr;
#endif  /* WITH_GSPELL */

    /**
     * list of canvasitems (currently just rects) that mark misspelled things on canvas
     */
    std::vector<CanvasItemPtr<CanvasItemRect>> _rects;

    /**
     * list of text objects we have already checked in this session
     */
    std::set<SPItem *> _seen_objects;

    /**
     *  the object currently being checked
     */
    SPItem *_text;

    /**
     * current objects layout
     */
    Inkscape::Text::Layout const *_layout;

    /**
     *  iterators for the start and end of the current word
     */
    Inkscape::Text::Layout::iterator _begin_w;
    Inkscape::Text::Layout::iterator _end_w;

    /**
     *  the word we're checking
     */
    Glib::ustring _word;

    /**
     *  counters for the number of stops and dictionary adds
     */
    int _stops;
    int _adds;

    /**
     *  true if we are in the middle of a check
     */
    bool _working;

    /**
     *  connect to the object being checked in case it is modified or deleted by user
     */
    sigc::connection _modified_connection;
    sigc::connection _release_connection;

    /**
     *  true if the spell checker dialog has changed text, to suppress modified callback
     */
    bool _local_change;

    Inkscape::Preferences *_prefs;

    std::vector<LanguagePair> _langs;

    /*
     *  Dialogs widgets
     */
    Gtk::Label          banner_label;
    Gtk::Button         banner_hbox;
    Gtk::ScrolledWindow scrolled_window;
    Gtk::TreeView       tree_view;
    Glib::RefPtr<Gtk::ListStore> model;

    Gtk::Box        suggestion_hbox;
    Gtk::Box        changebutton_vbox;
    Gtk::Button     accept_button;
    Gtk::Button     ignoreonce_button;
    Gtk::Button     ignore_button;

    Gtk::Button     add_button;
    Gtk::Button     pref_button;
    Gtk::Label      dictionary_label;
    Gtk::ComboBoxText dictionary_combo;
    Gtk::Box        dictionary_hbox;
    Gtk::Separator  action_sep;
    Gtk::Button     stop_button;
    Gtk::Button     start_button;
    Gtk::Box        actionbutton_hbox;

    class TreeColumns : public Gtk::TreeModel::ColumnRecord
    {
      public:
        TreeColumns()
        {
            add(suggestions);
        }
        ~TreeColumns() override = default;
        Gtk::TreeModelColumn<Glib::ustring> suggestions;
    };
    TreeColumns tree_columns;
};
}
}
}

#endif /* !SEEN_SPELLCHECK_H */

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
