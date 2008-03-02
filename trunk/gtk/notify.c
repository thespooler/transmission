/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 * 
 * $Id$
 */

#include "notify.h"

#ifndef HAVE_LIBNOTIFY

void tr_notify_init( void ) { }
void tr_notify_send( TrTorrent * tor UNUSED ) { }

#else
#include <libnotify/notify.h>

void
tr_notify_init( void )
{
    notify_init( "Transmission" );
}

static void
notifyCallback( NotifyNotification * n UNUSED,
                const char         * action,
                gpointer             gdata )
{
    TrTorrent * gtor = TR_TORRENT( gdata );
    tr_torrent * tor = tr_torrent_handle( gtor );
    const tr_info * info = tr_torrent_info( gtor );

    if( !strcmp( action, "folder" ) )
    {
        char * folder = g_build_filename( tr_torrentGetFolder(tor), info->name, NULL );
        char * argv[] = { "xdg-open", folder, NULL };
        g_spawn_async( NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL ); 
        g_free( folder );
    }
    else if( !strcmp( action, "file" ) )
    { 
        char * path = g_build_filename( tr_torrentGetFolder(tor), info->files[0].name, NULL );
        char * argv[] = { "xdg-open", path, NULL }; 
        g_spawn_async( NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL ); 
        g_free( path ); 
    } 
}

void
tr_notify_send(TrTorrent *tor) 
{ 
    const tr_info * info = tr_torrent_info( tor ); 
    NotifyNotification * n = notify_notification_new( "Torrent Complete", info->name, "transmission", NULL ); 
 
    if (info->fileCount == 1) 
        notify_notification_add_action( n, "file", "Open File",
                                        NOTIFY_ACTION_CALLBACK(notifyCallback), tor, NULL); 
    notify_notification_add_action( n, "folder", "Open Containing Folder",
                                    NOTIFY_ACTION_CALLBACK(notifyCallback), tor, NULL );
    notify_notification_show( n, NULL ); 
}

#endif
