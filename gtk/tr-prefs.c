/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 * 
 * $Id$
 */

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <third-party/miniupnp/miniwget.h>
#include <libtransmission/transmission.h>
#include "conf.h"
#include "hig.h"
#include "tr-core.h"
#include "tr-prefs.h"
#include "util.h"

/**
 * This is where we initialize the preferences file with the default values.
 * If you add a new preferences key, you /must/ add a default value here.
 */
void
tr_prefs_init_global( void )
{
    const char * str;

    cf_check_older_configs( );

    pref_int_set_default    ( PREF_KEY_MAX_PEERS_GLOBAL, 200 );
    pref_int_set_default    ( PREF_KEY_MAX_PEERS_PER_TORRENT, 50 );

    pref_flag_set_default   ( PREF_KEY_TOOLBAR, TRUE );
    pref_flag_set_default   ( PREF_KEY_FILTER_BAR, TRUE );
    pref_flag_set_default   ( PREF_KEY_STATUS_BAR, TRUE );
    pref_string_set_default ( PREF_KEY_STATUS_BAR_STATS, "total-ratio" );

    pref_flag_set_default   ( PREF_KEY_DL_LIMIT_ENABLED, FALSE );
    pref_int_set_default    ( PREF_KEY_DL_LIMIT, 100 );
    pref_flag_set_default   ( PREF_KEY_UL_LIMIT_ENABLED, FALSE );
    pref_int_set_default    ( PREF_KEY_UL_LIMIT, 50 );
    pref_flag_set_default   ( PREF_KEY_OPTIONS_PROMPT, TRUE );

    str = NULL;
#if GLIB_CHECK_VERSION(2,14,0)
    if( !str )
        str = g_get_user_special_dir( G_USER_DIRECTORY_DOWNLOAD );
#endif
    if( !str )
        str = g_get_home_dir( );
    pref_string_set_default ( PREF_KEY_DIR_DEFAULT, str );

    pref_int_set_default    ( PREF_KEY_PORT, TR_DEFAULT_PORT );

    pref_flag_set_default   ( PREF_KEY_NOTIFY, TRUE );

    pref_flag_set_default   ( PREF_KEY_NAT, TRUE );
    pref_flag_set_default   ( PREF_KEY_PEX, TRUE );
    pref_flag_set_default   ( PREF_KEY_ASKQUIT, TRUE );
    pref_flag_set_default   ( PREF_KEY_ENCRYPTED_ONLY, FALSE );

    pref_int_set_default    ( PREF_KEY_MSGLEVEL, TR_MSG_INF );

    pref_string_set_default ( PREF_KEY_SORT_MODE, "sort-by-name" );
    pref_flag_set_default   ( PREF_KEY_SORT_REVERSED, FALSE );
    pref_flag_set_default   ( PREF_KEY_MINIMAL_VIEW, FALSE );

    pref_flag_set_default   ( PREF_KEY_START, TRUE );

    pref_save( NULL );
}

/**
***
**/

#define PREF_KEY "pref-key"

static void
response_cb( GtkDialog * dialog, int response UNUSED, gpointer unused UNUSED )
{
    gtk_widget_destroy( GTK_WIDGET(dialog) );
}

static void
toggled_cb( GtkToggleButton * w, gpointer core )
{
    const char * key = g_object_get_data( G_OBJECT(w), PREF_KEY );
    const gboolean flag = gtk_toggle_button_get_active( w );
    tr_core_set_pref_bool( TR_CORE(core), key, flag );
}

static GtkWidget*
new_check_button( const char * mnemonic, const char * key, gpointer core )
{
    GtkWidget * w = gtk_check_button_new_with_mnemonic( mnemonic );
    g_object_set_data_full( G_OBJECT(w), PREF_KEY, g_strdup(key), g_free );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(w), pref_flag_get(key) );
    g_signal_connect( w, "toggled", G_CALLBACK(toggled_cb), core );
    return w;
}

static void
spun_cb( GtkSpinButton * w, gpointer core )
{
    const char * key = g_object_get_data( G_OBJECT(w), PREF_KEY );
    const int value = gtk_spin_button_get_value_as_int( w );
    tr_core_set_pref_int( TR_CORE(core), key, value );
}

static GtkWidget*
new_spin_button( const char * key, gpointer core, int low, int high, int step )
{
    GtkWidget * w = gtk_spin_button_new_with_range( low, high, step );
    g_object_set_data_full( G_OBJECT(w), PREF_KEY, g_strdup(key), g_free );
    gtk_spin_button_set_digits( GTK_SPIN_BUTTON(w), 0 );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON(w), pref_int_get(key) );
    g_signal_connect( w, "value-changed", G_CALLBACK(spun_cb), core );
    return w;
}

static void
chosen_cb( GtkFileChooser * w, gpointer core )
{
    const char * key = g_object_get_data( G_OBJECT(w), PREF_KEY );
    char * value = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER(w) );
    tr_core_set_pref( TR_CORE(core), key, value );
    g_free( value );
}

static GtkWidget*
new_path_chooser_button( const char * key, gpointer core )
{
    GtkWidget * w = gtk_file_chooser_button_new( NULL,
                                    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER );
    char * path = pref_string_get( key );
    g_object_set_data_full( G_OBJECT(w), PREF_KEY, g_strdup(key), g_free );
    g_signal_connect( w, "selection-changed", G_CALLBACK(chosen_cb), core );
    gtk_file_chooser_set_current_folder( GTK_FILE_CHOOSER(w), path );
    return w;
}

static void
target_cb( GtkWidget * tb, gpointer target )
{
    const gboolean b = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( tb ) );
    gtk_widget_set_sensitive( GTK_WIDGET(target), b );
}

struct test_port_data
{
    GtkWidget * label;
    gboolean * alive;
};

static gpointer
test_port( gpointer data_gpointer )
{
    struct test_port_data * data = data_gpointer;

    if( *data->alive )
    {
        GObject * o = G_OBJECT( data->label );
        GtkSpinButton * spin = g_object_get_data( o, "tr-port-spin" );
        const int port = gtk_spin_button_get_value_as_int( spin );
        int isOpen;
        int size;
        char * text;
        char url[256];

        g_usleep( G_USEC_PER_SEC * 3 ); /* give portmapping time to kick in */
        snprintf( url, sizeof(url), "http://portcheck.transmissionbt.com/%d", port );
        text = miniwget( url, &size );
        /*g_message(" got len %d, [%*.*s]", size, size, size, text );*/
        isOpen = text && *text=='1';

        if( *data->alive )
            gtk_label_set_markup( GTK_LABEL(data->label), isOpen
                ? _("Port is <b>open</b>")
                : _("Port is <b>closed</b>") );
    }

    g_free( data );
    return NULL;
}

static void
testing_port_cb( GtkWidget * unused UNUSED, gpointer l )
{
    struct test_port_data * data = g_new0( struct test_port_data, 1 );
    data->alive = g_object_get_data( G_OBJECT( l ), "alive" );
    data->label = l;
    gtk_label_set_markup( GTK_LABEL(l), _( "<i>Testing port...</i>" ) );
    g_thread_create( test_port, data, FALSE, NULL );
}

static void
dialogDestroyed( gpointer alive, GObject * dialog UNUSED )
{
    *(gboolean*)alive = FALSE;
}

static GtkWidget*
torrentPage( GObject * core )
{
    int row = 0;
    const char * s;
    GtkWidget * t;
    GtkWidget * w;

    t = hig_workarea_create( );
    hig_workarea_add_section_title( t, &row, _( "Adding" ) );

        w = new_path_chooser_button( PREF_KEY_DIR_DEFAULT, core );
        hig_workarea_add_row( t, &row, _( "Default destination _folder:" ), w, NULL );

        s = _( "Show _options dialog" );
        w = new_check_button( s, PREF_KEY_OPTIONS_PROMPT, core );
        hig_workarea_add_wide_control( t, &row, w );

        s = _( "_Start when added" );
        w = new_check_button( s, PREF_KEY_START, core );
        hig_workarea_add_wide_control( t, &row, w );

#ifdef HAVE_LIBNOTIFY
    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Notification" ) );

        s = _( "_Popup message when a torrent finishes" );
        w = new_check_button( s, PREF_KEY_NOTIFY, core );
        hig_workarea_add_wide_control( t, &row, w );
#endif

    hig_workarea_finish( t, &row );
    return t;
}

static GtkWidget*
peerPage( GObject * core )
{
    int row = 0;
    const char * s;
    GtkWidget * t;
    GtkWidget * w;

    t = hig_workarea_create( );
    hig_workarea_add_section_title (t, &row, _("Options"));
        
        s = _("Use peer e_xchange");
        w = new_check_button( s, PREF_KEY_PEX, core );
        hig_workarea_add_wide_control( t, &row, w );
        
        s = _("_Ignore unencrypted peers");
        w = new_check_button( s, PREF_KEY_ENCRYPTED_ONLY, core );
        hig_workarea_add_wide_control( t, &row, w );

    hig_workarea_add_section_divider( t, &row );
    /* section header for the "maximum number of peers" section */
    hig_workarea_add_section_title( t, &row, _( "Limits" ) );
  
        w = new_spin_button( PREF_KEY_MAX_PEERS_GLOBAL, core, 1, 3000, 5 );
        hig_workarea_add_row( t, &row, _( "Maximum peers _overall:" ), w, NULL );
        w = new_spin_button( PREF_KEY_MAX_PEERS_PER_TORRENT, core, 1, 300, 5 );
        hig_workarea_add_row( t, &row, _( "Maximum peers per _torrent:" ), w, NULL );

    hig_workarea_finish( t, &row );
    return t;
}

static GtkWidget*
networkPage( GObject * core, gpointer alive )
{
    int row = 0;
    const char * s;
    GtkWidget * t;
    GtkWidget * w, * w2;
    GtkWidget * l;
    GtkWidget * h;

    t = hig_workarea_create( );

    hig_workarea_add_section_title (t, &row, _("Bandwidth"));

        s = _("Limit _upload speed (KB/s):");
        w = new_check_button( s, PREF_KEY_UL_LIMIT_ENABLED, core );
        w2 = new_spin_button( PREF_KEY_UL_LIMIT, core, 0, INT_MAX, 5 );
        gtk_widget_set_sensitive( GTK_WIDGET(w2), pref_flag_get( PREF_KEY_UL_LIMIT_ENABLED ) );
        g_signal_connect( w, "toggled", G_CALLBACK(target_cb), w2 );
        hig_workarea_add_row_w( t, &row, w, w2, NULL );

        s = _("Limit _download speed (KB/s):");
        w = new_check_button( s, PREF_KEY_DL_LIMIT_ENABLED, core );
        w2 = new_spin_button( PREF_KEY_DL_LIMIT, core, 0, INT_MAX, 5 );
        gtk_widget_set_sensitive( GTK_WIDGET(w2), pref_flag_get( PREF_KEY_DL_LIMIT_ENABLED ) );
        g_signal_connect( w, "toggled", G_CALLBACK(target_cb), w2 );
        hig_workarea_add_row_w( t, &row, w, w2, NULL );

    hig_workarea_add_section_title (t, &row, _("Ports"));
        
        s = _("_Forward port from router with UPnP or NAT-PMP" );
        w = new_check_button( s, PREF_KEY_NAT, core );
        hig_workarea_add_wide_control( t, &row, w );

        h = gtk_hbox_new( FALSE, GUI_PAD );
        w2 = new_spin_button( PREF_KEY_PORT, core, 1, INT_MAX, 1 );
        gtk_box_pack_start( GTK_BOX(h), w2, FALSE, FALSE, 0 );
        l = gtk_label_new( NULL );
        gtk_misc_set_alignment( GTK_MISC(l), 0.0f, 0.5f );
        gtk_box_pack_start( GTK_BOX(h), l, FALSE, FALSE, 0 );
        hig_workarea_add_row( t, &row, _("Incoming TCP _port"), h, w );

        g_object_set_data( G_OBJECT(l), "tr-port-spin", w2 );
        g_object_set_data( G_OBJECT(l), "alive", alive );
        testing_port_cb( NULL, l );

        g_signal_connect( w, "toggled", G_CALLBACK(testing_port_cb), l );
        g_signal_connect( w2, "value-changed", G_CALLBACK(testing_port_cb), l );

    hig_workarea_finish( t, &row );
    return t;
}

GtkWidget *
tr_prefs_dialog_new( GObject * core, GtkWindow * parent )
{
    GtkWidget * d;
    GtkWidget * n;
    gboolean * alive;

    alive = g_new( gboolean, 1 );
    *alive = TRUE;

    d = gtk_dialog_new_with_buttons( _("Preferences"), parent,
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                     NULL );
    gtk_window_set_role( GTK_WINDOW(d), "transmission-preferences-dialog" );
    gtk_dialog_set_has_separator( GTK_DIALOG( d ), FALSE );
    gtk_container_set_border_width( GTK_CONTAINER( d ), GUI_PAD );
    g_object_weak_ref( G_OBJECT( d ), dialogDestroyed, alive );

    n = gtk_notebook_new( );

    gtk_notebook_append_page( GTK_NOTEBOOK( n ),
                              torrentPage( core ),
                              gtk_label_new (_("Torrents")) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ),
                              peerPage( core ),
                              gtk_label_new (_("Peers")) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ),
                              networkPage( core, alive ),
                              gtk_label_new (_("Network")) );

    g_signal_connect( d, "response", G_CALLBACK(response_cb), core );
    gtk_box_pack_start_defaults( GTK_BOX(GTK_DIALOG(d)->vbox), n );
    gtk_widget_show_all( GTK_DIALOG(d)->vbox );
    return d;
}
