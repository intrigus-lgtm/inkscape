#define __SP_ITEM_PROPERTIES_C__

/*
 * Object properties dialog
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 1999-2002 Authors
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include <config.h>
#include <string.h>
#include <ctype.h>
#include <libnr/nr-values.h>
#include <glib.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkhscale.h>
#include <gtk/gtktable.h>
#include <gtk/gtkcheckbutton.h>
#include <gtk/gtkspinbutton.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkframe.h>
#include <gtk/gtktextview.h>
#include <gtk/gtktextbuffer.h>

#include "helper/sp-intl.h"
#include "helper/window.h"
#include "../svg/svg.h"
#include "../svg/stringstream.h"
#include "../widgets/sp-widget.h"
#include "../inkscape.h"
#include "../document.h"
#include "../desktop-handles.h"
#include "../desktop-style.h"
#include "../selection.h"
#include "../sp-item.h"
#include "../style.h"
#include "../macros.h"
#include "../verbs.h"
#include "../interface.h"

#include "dialog-events.h"
#include "../prefs-utils.h"

#include "item-properties.h"

static GtkWidget *dlg = NULL;
static win_data wd;

// impossible original values to make sure they are read from prefs
static gint x = -1000, y = -1000, w = 0, h = 0; 
static gchar *prefs_path = "dialogs.object";

static void sp_item_widget_modify_selection (SPWidget *spw, SPSelection *selection, guint flags, GtkWidget *itemw);
static void sp_item_widget_change_selection (SPWidget *spw, SPSelection *selection, GtkWidget *itemw);
static void sp_item_widget_setup (SPWidget *spw, SPSelection *selection);
static void sp_item_widget_sensitivity_toggled (GtkWidget *widget, SPWidget *spw);
static void sp_item_widget_printability_toggled (GtkWidget *widget, SPWidget *spw);
static void sp_item_widget_hidden_toggled (GtkWidget *widget, SPWidget *spw);
static void sp_item_widget_label_changed (GtkWidget *widget, SPWidget *spw);
static void sp_item_widget_transform_value_changed (GtkWidget *widget, SPWidget *spw);

static void
sp_item_dialog_destroy (GtkObject *object, gpointer data)
{
    sp_signal_disconnect_by_data (INKSCAPE, dlg);
    wd.win = dlg = NULL;
    wd.stop = 0;
}

static gboolean
sp_item_dialog_delete (GtkObject *object, GdkEvent *event, gpointer data)
{
    gtk_window_get_position ((GtkWindow *) dlg, &x, &y);
    gtk_window_get_size ((GtkWindow *) dlg, &w, &h);

    prefs_set_int_attribute (prefs_path, "x", x);
    prefs_set_int_attribute (prefs_path, "y", y);
    prefs_set_int_attribute (prefs_path, "w", w);
    prefs_set_int_attribute (prefs_path, "h", h);

    return FALSE; // which means, go ahead and destroy it

} 



/**
 * \brief  Creates new instance of item properties widget 
 *
 */
GtkWidget *
sp_item_widget_new (void)
{

    GtkWidget *spw, *vb, *t, *cb, *l, *sb, *f, *tf, *pb;
    GtkObject *a;
    GtkTextBuffer *desc_buffer;

    /* Create container widget */
    spw = sp_widget_new_global (INKSCAPE);
    gtk_signal_connect ( GTK_OBJECT (spw), "modify_selection", 
                         GTK_SIGNAL_FUNC (sp_item_widget_modify_selection), 
                         spw );
    gtk_signal_connect ( GTK_OBJECT (spw), "change_selection", 
                         GTK_SIGNAL_FUNC (sp_item_widget_change_selection), 
                         spw );

    vb = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (vb);
    gtk_container_add (GTK_CONTAINER (spw), vb);

    t = gtk_table_new (3, 4, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(t), 10);
    gtk_widget_show (t);
    gtk_box_pack_start (GTK_BOX (vb), t, TRUE, TRUE, 0);


    /* Create the label for the object id */
    l = gtk_label_new (_("Id"));
    gtk_widget_show (l);
    gtk_misc_set_alignment (GTK_MISC (l), 0, 0.5);
    gtk_table_attach ( GTK_TABLE (t), l, 0, 1, 0, 1, 
                       (GtkAttachOptions)( GTK_SHRINK | GTK_FILL ), 
                       (GtkAttachOptions)0, 0, 0 );
    gtk_object_set_data (GTK_OBJECT (spw), "id_label", l);

    /* Create the entry box for the object id */
    tf = gtk_entry_new ();
    gtk_entry_set_max_length (GTK_ENTRY (tf), 64);
    gtk_widget_show (tf);
    gtk_table_attach ( GTK_TABLE (t), tf, 1, 2, 0, 1, 
                       (GtkAttachOptions)( GTK_EXPAND | GTK_FILL ), 
                       (GtkAttachOptions)0, 0, 0 );
    gtk_object_set_data (GTK_OBJECT (spw), "id", tf);

    /* Create the button for setting the object label */
    pb = gtk_button_new_with_label (_("Update Properties"));
    gtk_table_attach ( GTK_TABLE (t), pb, 2, 3, 0, 1, 
                       (GtkAttachOptions)( GTK_SHRINK | GTK_FILL ), 
                       (GtkAttachOptions)0, 0, 0 );
    gtk_signal_connect ( GTK_OBJECT (pb), "clicked", 
                         GTK_SIGNAL_FUNC (sp_item_widget_label_changed), 
                         spw );
    gtk_widget_show (pb);


    /* Create the label for the object label */
    l = gtk_label_new (_("Label"));
    gtk_widget_show (l);
    gtk_misc_set_alignment (GTK_MISC (l), 0, 0.5);
    gtk_table_attach ( GTK_TABLE (t), l, 0, 1, 1, 2, 
                       (GtkAttachOptions)( GTK_SHRINK | GTK_FILL ), 
                       (GtkAttachOptions)0, 0, 0 );
    gtk_object_set_data (GTK_OBJECT (spw), "label_label", l);

    /* Create the entry box for the object label */
    tf = gtk_entry_new ();
    gtk_entry_set_max_length (GTK_ENTRY (tf), 64);
    gtk_widget_show (tf);
    gtk_table_attach ( GTK_TABLE (t), tf, 1, 2, 1, 2, 
                       (GtkAttachOptions)( GTK_EXPAND | GTK_FILL ), 
                       (GtkAttachOptions)0, 0, 0 );
    gtk_object_set_data (GTK_OBJECT (spw), "label", tf);


    /* Create the label for the object title */
    l = gtk_label_new (_("Title"));
    gtk_widget_show (l);
    gtk_misc_set_alignment (GTK_MISC (l), 0, 0.5);
    gtk_table_attach ( GTK_TABLE (t), l, 0, 1, 2, 3, 
                       (GtkAttachOptions)( GTK_SHRINK | GTK_FILL ), 
                       (GtkAttachOptions)0, 0, 0 );
    gtk_object_set_data (GTK_OBJECT (spw), "title_label", l);

    /* Create the entry box for the object title */
    tf = gtk_entry_new ();
    gtk_widget_set_sensitive (GTK_WIDGET (tf), FALSE);
    gtk_entry_set_max_length (GTK_ENTRY (tf), 256);
    gtk_widget_show (tf);
    gtk_table_attach ( GTK_TABLE (t), tf, 1, 3, 2, 3, 
                       (GtkAttachOptions)( GTK_EXPAND | GTK_FILL ), 
                       (GtkAttachOptions)0, 0, 0 );
    gtk_object_set_data (GTK_OBJECT (spw), "title", tf);


    /* Create the frame for the object description */
    f = gtk_frame_new (_("Description"));
    gtk_widget_show (f);
    gtk_table_attach ( GTK_TABLE (t), f, 0, 3, 3, 4, 
                       (GtkAttachOptions)( GTK_EXPAND | GTK_FILL ), 
                       (GtkAttachOptions)( GTK_EXPAND | GTK_FILL ), 0, 0 );
    gtk_object_set_data (GTK_OBJECT (spw), "desc_frame", l);

    /* Create the text view box for the object description */
    GtkWidget *textframe = gtk_frame_new(NULL);
    gtk_container_set_border_width(GTK_CONTAINER(textframe), 3);
    gtk_widget_set_sensitive (GTK_WIDGET (textframe), FALSE);
    gtk_widget_show (textframe);
    gtk_container_add (GTK_CONTAINER (f), textframe);
    gtk_frame_set_shadow_type (GTK_FRAME (textframe), GTK_SHADOW_IN);
    tf = gtk_text_view_new();
    desc_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tf));
    gtk_text_buffer_set_text(desc_buffer, "", -1);
    gtk_widget_show (tf); 
    gtk_container_add (GTK_CONTAINER (textframe), tf);
    gtk_object_set_data (GTK_OBJECT (spw), "desc", tf);


    /* Check boxes */
    t = gtk_table_new (2, 2, TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(t), 10);
    gtk_widget_show (t);
    gtk_box_pack_start (GTK_BOX (vb), t, FALSE, FALSE, 0);

    cb = gtk_check_button_new_with_label (_("Sensitive"));
    gtk_widget_show (cb);
    gtk_table_attach ( GTK_TABLE (t), cb, 0, 1, 0, 1, 
                       (GtkAttachOptions)( GTK_EXPAND | GTK_FILL ), 
                       (GtkAttachOptions)0, 0, 0 );
    gtk_signal_connect ( GTK_OBJECT (cb), "toggled", 
                         GTK_SIGNAL_FUNC (sp_item_widget_sensitivity_toggled), 
                         spw );
    gtk_object_set_data (GTK_OBJECT (spw), "sensitive", cb);

    /* Whether to explicitly mark this object with visibility:hidden. */
    cb = gtk_check_button_new_with_label(_("Hide"));
    gtk_widget_show (cb);
    gtk_table_attach ( GTK_TABLE (t), cb, 1, 2, 0, 1, 
                       (GtkAttachOptions)( GTK_EXPAND | GTK_FILL ), 
                       (GtkAttachOptions)0, 0, 0 );
    g_signal_connect (G_OBJECT(cb), "toggled", G_CALLBACK(sp_item_widget_hidden_toggled), spw);
    gtk_object_set_data(GTK_OBJECT(spw), "hidden", cb);

    cb = gtk_check_button_new_with_label (_("Active"));
    gtk_widget_set_sensitive (GTK_WIDGET (cb), FALSE);
    gtk_widget_show (cb);
    gtk_table_attach ( GTK_TABLE (t), cb, 0, 1, 1, 2, 
                       (GtkAttachOptions)( GTK_EXPAND | GTK_FILL ), 
                       (GtkAttachOptions)0, 0, 0 );
    gtk_object_set_data (GTK_OBJECT (spw), "active", cb);

    cb = gtk_check_button_new_with_label (_("Printable"));
    gtk_widget_show (cb);
    gtk_table_attach ( GTK_TABLE (t), cb, 1, 2, 1, 2, 
                       (GtkAttachOptions)( GTK_EXPAND | GTK_FILL ), 
                       (GtkAttachOptions)0, 0, 0 );
    gtk_signal_connect ( GTK_OBJECT (cb), "toggled", 
                         GTK_SIGNAL_FUNC (sp_item_widget_printability_toggled),
                         spw );
    gtk_object_set_data (GTK_OBJECT (spw), "printable", cb);


    /* Transformation matrix */
    f = gtk_frame_new (_("Transformation matrix"));
    gtk_widget_show (f);
    gtk_container_set_border_width(GTK_CONTAINER(f), 10);
    gtk_box_pack_start (GTK_BOX (vb), f, FALSE, FALSE, 0);

    t = gtk_table_new (2, 3, TRUE);
    gtk_widget_show (t);
    gtk_container_set_border_width(GTK_CONTAINER(t), 2);
    gtk_container_add (GTK_CONTAINER (f), t);

    a = gtk_adjustment_new (1.0, -NR_HUGE, NR_HUGE, 0.01, 0.1, 0.1);
    gtk_object_set_data (GTK_OBJECT (spw), "t0", a);
    sb = gtk_spin_button_new (GTK_ADJUSTMENT (a), 0.01, 2);
    gtk_widget_show (sb);
    gtk_table_attach ( GTK_TABLE (t), sb, 0, 1, 0, 1, 
                       (GtkAttachOptions)( GTK_EXPAND | GTK_FILL ), 
                       (GtkAttachOptions)0, 0, 0 );
                       
    gtk_signal_connect ( a, "value_changed", 
                         GTK_SIGNAL_FUNC (sp_item_widget_transform_value_changed), 
                         spw );

    a = gtk_adjustment_new (0.0, -NR_HUGE, NR_HUGE, 0.01, 0.1, 0.1);
    gtk_object_set_data (GTK_OBJECT (spw), "t1", a);
    sb = gtk_spin_button_new (GTK_ADJUSTMENT (a), 0.01, 2);
    gtk_widget_show (sb);
    gtk_table_attach ( GTK_TABLE (t), sb, 0, 1, 1, 2, 
                       (GtkAttachOptions)( GTK_EXPAND | GTK_FILL ), 
                       (GtkAttachOptions)0, 0, 0 );
                       
    gtk_signal_connect ( a, "value_changed", 
                         GTK_SIGNAL_FUNC 
                             (sp_item_widget_transform_value_changed), 
                         spw );

    a = gtk_adjustment_new (0.0, -NR_HUGE, NR_HUGE, 0.01, 0.1, 0.1);
    gtk_object_set_data (GTK_OBJECT (spw), "t2", a);
    sb = gtk_spin_button_new (GTK_ADJUSTMENT (a), 0.01, 2);
    gtk_widget_show (sb);
    gtk_table_attach ( GTK_TABLE (t), sb, 1, 2, 0, 1, 
                       (GtkAttachOptions)( GTK_EXPAND | GTK_FILL ), 
                       (GtkAttachOptions)0, 0, 0 );
                       
    gtk_signal_connect ( a, "value_changed", 
                         GTK_SIGNAL_FUNC (sp_item_widget_transform_value_changed), 
                         spw );

    a = gtk_adjustment_new (1.0, -NR_HUGE, NR_HUGE, 0.01, 0.1, 0.1);
    gtk_object_set_data (GTK_OBJECT (spw), "t3", a);
    sb = gtk_spin_button_new (GTK_ADJUSTMENT (a), 0.01, 2);
    gtk_widget_show (sb);
    gtk_table_attach ( GTK_TABLE (t), sb, 1, 2, 1, 2, 
                       (GtkAttachOptions)( GTK_EXPAND | GTK_FILL ), 
                       (GtkAttachOptions)0, 0, 0 );
                       
    gtk_signal_connect ( a, "value_changed", 
                         GTK_SIGNAL_FUNC 
                             (sp_item_widget_transform_value_changed), 
                         spw );

    a = gtk_adjustment_new (0.0, -NR_HUGE, NR_HUGE, 0.01, 0.1, 0.1);
    gtk_object_set_data (GTK_OBJECT (spw), "t4", a);
    sb = gtk_spin_button_new (GTK_ADJUSTMENT (a), 0.01, 2);
    gtk_widget_show (sb);
    gtk_table_attach ( GTK_TABLE (t), sb, 2, 3, 0, 1, 
                       (GtkAttachOptions)( GTK_EXPAND | GTK_FILL ), 
                       (GtkAttachOptions)0, 0, 0 );
                       
    gtk_signal_connect ( a, "value_changed", 
                         GTK_SIGNAL_FUNC 
                             (sp_item_widget_transform_value_changed), 
                         spw );

    a = gtk_adjustment_new (0.0, -NR_HUGE, NR_HUGE, 0.01, 0.1, 0.1);
    gtk_object_set_data (GTK_OBJECT (spw), "t5", a);
    sb = gtk_spin_button_new (GTK_ADJUSTMENT (a), 0.01, 2);
    gtk_widget_show (sb);
    gtk_table_attach ( GTK_TABLE (t), sb, 2, 3, 1, 2, 
                       (GtkAttachOptions)( GTK_EXPAND | GTK_FILL ), 
                       (GtkAttachOptions)0, 0, 0 );
                       
    gtk_signal_connect ( a, "value_changed", 
                         GTK_SIGNAL_FUNC 
                             (sp_item_widget_transform_value_changed), 
                         spw );

    sp_item_widget_setup (SP_WIDGET (spw), SP_DT_SELECTION (SP_ACTIVE_DESKTOP));

    return (GtkWidget *) spw;
    
} //end of sp_item_widget_new()



static void
sp_item_widget_modify_selection ( SPWidget *spw, 
                                  SPSelection *selection, 
                                  guint flags, 
                                  GtkWidget *itemw )
{
    sp_item_widget_setup (spw, selection);
}



static void
sp_item_widget_change_selection ( SPWidget *spw, 
                                  SPSelection *selection, 
                                  GtkWidget *itemw )
{
    sp_item_widget_setup (spw, selection);
}


/**
*  \param selection Selection to use; should not be NULL.
*/
static void
sp_item_widget_setup ( SPWidget *spw, SPSelection *selection )
{
    g_assert (selection != NULL);

    if (gtk_object_get_data (GTK_OBJECT (spw), "blocked"))
        return;

    if (!selection->singleItem()) {
        gtk_widget_set_sensitive (GTK_WIDGET (spw), FALSE);
        return;
        
    } else {
        gtk_widget_set_sensitive (GTK_WIDGET (spw), TRUE);
    
    }
    
    gtk_object_set_data (GTK_OBJECT (spw), "blocked", GUINT_TO_POINTER (TRUE));

    SPItem *item = SP_WIDGET_SELECTION(spw)->singleItem();

    /* Sensitive */
    GtkWidget *w = GTK_WIDGET(gtk_object_get_data (GTK_OBJECT (spw), "sensitive"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), item->sensitive);

    /* Hidden */
    w = GTK_WIDGET(gtk_object_get_data (GTK_OBJECT (spw), "hidden"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), item->isExplicitlyHidden());

    /* Printable */
    w = GTK_WIDGET(gtk_object_get_data (GTK_OBJECT (spw), "printable"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), item->printable);

    /* Transform */
    {
        char const *const names[] = {"t0", "t1", "t2", "t3", "t4", "t5"};
        for (unsigned i = 0; i < 6; ++i) {
            GtkAdjustment *adj = GTK_ADJUSTMENT(gtk_object_get_data(GTK_OBJECT(spw), names[i]));
            gtk_adjustment_set_value(adj, item->transform[i]);
        }
    }

    if (SP_OBJECT_IS_CLONED (item)) {
    
        /* ID */
        w = GTK_WIDGET(gtk_object_get_data (GTK_OBJECT (spw), "id"));
        gtk_entry_set_text (GTK_ENTRY (w), "");
        gtk_widget_set_sensitive (w, FALSE);
        w = GTK_WIDGET(gtk_object_get_data (GTK_OBJECT (spw), "id_label"));
        gtk_label_set_text (GTK_LABEL (w), _("Ref"));
    
        /* Label */
        w = GTK_WIDGET(gtk_object_get_data (GTK_OBJECT (spw), "label"));
        gtk_entry_set_text (GTK_ENTRY (w), "");
        gtk_widget_set_sensitive (w, FALSE);
        w = GTK_WIDGET(gtk_object_get_data (GTK_OBJECT (spw), "label_label"));
        gtk_label_set_text (GTK_LABEL (w), _("Ref"));
    
    } else {
        SPObject *obj = (SPObject*)item;

        /* ID */
        w = GTK_WIDGET(gtk_object_get_data (GTK_OBJECT (spw), "id"));
        gtk_entry_set_text (GTK_ENTRY (w), obj->id);
        gtk_widget_set_sensitive (w, TRUE);
        w = GTK_WIDGET(gtk_object_get_data (GTK_OBJECT (spw), "id_label"));
        gtk_label_set_text (GTK_LABEL (w), _("ID"));

        /* Label */
        w = GTK_WIDGET(gtk_object_get_data (GTK_OBJECT (spw), "label"));
        if (obj->label() != NULL) {
            gtk_entry_set_text (GTK_ENTRY (w), obj->label());
        } else {
            gtk_entry_set_text (GTK_ENTRY (w), obj->defaultLabel());
        }
        gtk_widget_set_sensitive (w, TRUE);
        w = GTK_WIDGET(gtk_object_get_data (GTK_OBJECT (spw), "label_label"));
        gtk_label_set_text (GTK_LABEL (w), _("Label"));
    }

    gtk_object_set_data (GTK_OBJECT (spw), "blocked", GUINT_TO_POINTER (FALSE));
    
    
} // end of sp_item_widget_setup()



static void
sp_item_widget_sensitivity_toggled (GtkWidget *widget, SPWidget *spw)
{
    if (gtk_object_get_data (GTK_OBJECT (spw), "blocked"))
        return;

    SPItem *item = SP_WIDGET_SELECTION(spw)->singleItem();
    g_return_if_fail (item != NULL);

    gtk_object_set_data (GTK_OBJECT (spw), "blocked", GUINT_TO_POINTER (TRUE));

    SPException ex;
    SP_EXCEPTION_INIT (&ex);
    
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget))) {
    
        sp_object_removeAttribute ( SP_OBJECT (item), 
                                    "sodipodi:insensitive", 
                                    &ex );
    
    } else {
        
        sp_object_setAttribute ( SP_OBJECT (item), 
                                 "sodipodi:insensitive", 
                                 "true", 
                                 &ex );
    }

    sp_document_maybe_done ( SP_WIDGET_DOCUMENT (spw), 
                             "ItemDialog:insensitive" );

    gtk_object_set_data ( GTK_OBJECT (spw), "blocked", 
                          GUINT_TO_POINTER (FALSE) );

}

void
sp_item_widget_hidden_toggled(GtkWidget *widget, SPWidget *spw)
{
    if (gtk_object_get_data (GTK_OBJECT (spw), "blocked"))
        return;

    SPItem *item = SP_WIDGET_SELECTION(spw)->singleItem();
    g_return_if_fail (item != NULL);

    gtk_object_set_data (GTK_OBJECT (spw), "blocked", GUINT_TO_POINTER (TRUE));

    item->setExplicitlyHidden(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)));

    sp_document_maybe_done ( SP_WIDGET_DOCUMENT (spw), 
                             "ItemDialog:visiblity" );

    gtk_object_set_data ( GTK_OBJECT (spw), "blocked", 
                          GUINT_TO_POINTER (FALSE) );
}

static void
sp_item_widget_printability_toggled (GtkWidget *widget, SPWidget *spw)
{
    if (gtk_object_get_data (GTK_OBJECT (spw), "blocked")) 
        return;

    SPItem *item = SP_WIDGET_SELECTION(spw)->singleItem();
    g_return_if_fail (item != NULL);

    gtk_object_set_data (GTK_OBJECT (spw), "blocked", GUINT_TO_POINTER (TRUE));


    SPException ex;
    SP_EXCEPTION_INIT (&ex);
    
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget))) {
    
        sp_object_removeAttribute ( SP_OBJECT (item), 
                                    "sodipodi:nonprintable", 
                                    &ex );
    
    } else {
        
        sp_object_setAttribute ( SP_OBJECT (item), 
                                 "sodipodi:nonprintable", 
                                 "true", 
                                 &ex );
    }

    sp_document_maybe_done ( SP_WIDGET_DOCUMENT (spw), 
                             "ItemDialog:nonprintable" );

    gtk_object_set_data ( GTK_OBJECT (spw), 
                          "blocked", 
                          GUINT_TO_POINTER (FALSE) );

} // end of sp_item_widget_printability_toggled()



static void
sp_item_widget_label_changed (GtkWidget *widget, SPWidget *spw)
{
    if (gtk_object_get_data (GTK_OBJECT (spw), "blocked")) 
        return;

    SPItem *item = SP_WIDGET_SELECTION(spw)->singleItem();
    g_return_if_fail (item != NULL);

    gtk_object_set_data (GTK_OBJECT (spw), "blocked", GUINT_TO_POINTER (TRUE));

    /* Retrieve the label widget for the object's id */
    GtkWidget *w = GTK_WIDGET(gtk_object_get_data (GTK_OBJECT (spw), "id"));
    gchar *id = (gchar *)gtk_entry_get_text (GTK_ENTRY (w));
    w = GTK_WIDGET(gtk_object_get_data (GTK_OBJECT (spw), "id_label"));
    if (!strcmp (id, ((SPObject *) item)->id)) {
        gtk_label_set_text (GTK_LABEL (w), _("ID"));
    } else if (!*id || !isalnum (*id)) {
        gtk_label_set_text (GTK_LABEL (w), _("ID invalid"));
    } else if (SP_WIDGET_DOCUMENT(spw)->getObjectById(id) != NULL) {
        gtk_label_set_text (GTK_LABEL (w), _("ID exists"));
    } else {
        SPException ex;
        gtk_label_set_text (GTK_LABEL (w), _("ID"));
        SP_EXCEPTION_INIT (&ex);
        sp_object_setAttribute (SP_OBJECT (item), "id", id, &ex);
        sp_document_maybe_done (SP_WIDGET_DOCUMENT (spw), "ItemDialog:id");
    }


    /* Retrieve the label widget for the object's label */
    w = GTK_WIDGET(gtk_object_get_data (GTK_OBJECT (spw), "label"));
    gchar *label = (gchar *)gtk_entry_get_text (GTK_ENTRY (w));
    w = GTK_WIDGET(gtk_object_get_data (GTK_OBJECT (spw), "label_label"));
    g_assert(label != NULL);

    /* Give feedback on success of setting the drawing object's label
     * using the widget's label text 
     */
    SPObject *obj = (SPObject*)item;
    if (obj->label() && !strcmp (label, obj->label())) {
        gtk_label_set_text (GTK_LABEL (w), _("Label"));
    } else if (!*label || !isalnum (*label)) {
        gtk_label_set_text (GTK_LABEL (w), _("Label invalid"));
    } else {
        gtk_label_set_text (GTK_LABEL (w), _("Label"));
        obj->setLabel(label);
        sp_document_maybe_done (SP_WIDGET_DOCUMENT (spw), "inkscape:label");
    }


    /* Retrieve the title */
    w = GTK_WIDGET(gtk_object_get_data (GTK_OBJECT (spw), "title"));
    gchar *title = (gchar *)gtk_entry_get_text (GTK_ENTRY (w));
    if (title != NULL) {
        obj->setTitle(title);
        sp_document_maybe_done (SP_WIDGET_DOCUMENT (spw), "title");
    }

    /* Retrieve the description */
    gchar *desc = NULL; /* TODO:  get text from text buffer */
    if (desc != NULL) {
        obj->setDesc(desc);
        sp_document_maybe_done (SP_WIDGET_DOCUMENT (spw), "desc");
    }

    gtk_object_set_data (GTK_OBJECT (spw), "blocked", GUINT_TO_POINTER (FALSE));

} // end of sp_item_widget_label_changed()


static void
sp_item_widget_transform_value_changed ( GtkWidget *widget, SPWidget *spw )
{
    if (gtk_object_get_data (GTK_OBJECT (spw), "blocked"))
        return;

    SPItem *item = SP_WIDGET_SELECTION(spw)->singleItem();
    g_return_if_fail (item != NULL);

    gtk_object_set_data (GTK_OBJECT (spw), "blocked", GUINT_TO_POINTER (TRUE));

    NRMatrix t;
    for (unsigned i = 0; i < 6; i++) {
        gchar c[8];
        g_snprintf (c, 8, "t%u", i);
        t.c[i] = 
            GTK_ADJUSTMENT (gtk_object_get_data (GTK_OBJECT (spw), c))->value;
    }

    gchar c[64];
    sp_svg_transform_write (c, 64, &t);
    SPException ex;
    SP_EXCEPTION_INIT (&ex);
    sp_object_setAttribute (SP_OBJECT (item), "transform", c, &ex);

    sp_document_maybe_done (SP_WIDGET_DOCUMENT (spw), "ItemDialog:transform");

    gtk_object_set_data (GTK_OBJECT (spw), "blocked", GUINT_TO_POINTER (FALSE));
} // end of sp_item_widget_transform_value_changed()



/**
 * \brief  Dialog 
 *
 */
void
sp_item_dialog (void)
{
    if (dlg == NULL) {

        gchar title[500];
        sp_ui_dialog_title_string (Inkscape::Verb::get(SP_VERB_DIALOG_ITEM), title);

        dlg = sp_window_new (title, TRUE);
        if (x == -1000 || y == -1000) {
            x = prefs_get_int_attribute (prefs_path, "x", 0);
            y = prefs_get_int_attribute (prefs_path, "y", 0);
        }
        
        if (w ==0 || h == 0) {
            w = prefs_get_int_attribute (prefs_path, "w", 0);
            h = prefs_get_int_attribute (prefs_path, "h", 0);
        }
        
        if (x != 0 || y != 0) {
            gtk_window_move ((GtkWindow *) dlg, x, y);
        } else {
            gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_CENTER);
        }
        
        if (w && h) {
            gtk_window_resize ((GtkWindow *) dlg, w, h);
        }
        
        sp_transientize (dlg);
        wd.win = dlg;
        wd.stop = 0;
        
        g_signal_connect   ( G_OBJECT (INKSCAPE), "activate_desktop", G_CALLBACK (sp_transientize_callback), &wd);
        gtk_signal_connect ( GTK_OBJECT (dlg), "event", GTK_SIGNAL_FUNC (sp_dialog_event_handler), dlg);
        gtk_signal_connect ( GTK_OBJECT (dlg), "destroy", G_CALLBACK (sp_item_dialog_destroy), dlg);
        gtk_signal_connect ( GTK_OBJECT (dlg), "delete_event", G_CALLBACK (sp_item_dialog_delete), dlg);
        g_signal_connect   ( G_OBJECT (INKSCAPE), "shut_down", G_CALLBACK (sp_item_dialog_delete), dlg);
        g_signal_connect   ( G_OBJECT (INKSCAPE), "dialogs_hide", G_CALLBACK (sp_dialog_hide), dlg);
        g_signal_connect   ( G_OBJECT (INKSCAPE), "dialogs_unhide", G_CALLBACK (sp_dialog_unhide), dlg);

        // Dialog-specific stuff
        GtkWidget *itemw = sp_item_widget_new ();
        gtk_widget_show (itemw);
        gtk_container_add (GTK_CONTAINER (dlg), itemw);
      
    }

    gtk_window_present ((GtkWindow *) dlg);
} 


/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=c++:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
