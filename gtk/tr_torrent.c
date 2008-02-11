/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2008 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <libtransmission/transmission.h>

#include "tr_prefs.h"
#include "tr_torrent.h"
#include "conf.h"
#include "util.h"

struct TrTorrentPrivate
{
   tr_torrent * handle;
   char * delfile;
   gboolean seeding_cap_enabled;
   gdouble seeding_cap; /* ratio to stop seeding at */
};


static void
tr_torrent_init(GTypeInstance *instance, gpointer g_class UNUSED )
{
    TrTorrent * self = TR_TORRENT( instance );
    struct TrTorrentPrivate * p;

    p = self->priv = G_TYPE_INSTANCE_GET_PRIVATE( self,
                                                  TR_TORRENT_TYPE,
                                                  struct TrTorrentPrivate );
    p->handle = NULL;
    p->delfile = NULL;
    p->seeding_cap = 2.0;

#ifdef REFDBG
    g_message( "torrent %p init", self );
#endif
}

static int
isDisposed( const TrTorrent * self )
{
    return !self || !self->priv;
}

static void
tr_torrent_dispose( GObject * o )
{
    GObjectClass * parent = g_type_class_peek(g_type_parent(TR_TORRENT_TYPE));
    TrTorrent * self = TR_TORRENT( o );

    if( !isDisposed( self ) )
    {
        if( self->priv->handle )
            tr_torrentClose( self->priv->handle );
        g_free( self->priv->delfile );
        self->priv = NULL;
    }

    /* chain up to the parent class */
    parent->dispose( o );
}

static void
tr_torrent_class_init(gpointer g_class, gpointer g_class_data UNUSED )
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(g_class);
    gobject_class->dispose = tr_torrent_dispose;
    g_type_class_add_private( g_class, sizeof(struct TrTorrentPrivate) );
}

GType
tr_torrent_get_type(void)
{
  static GType type = 0;

  if(0 == type) {
    static const GTypeInfo info = {
      sizeof (TrTorrentClass),
      NULL,   /* base_init */
      NULL,   /* base_finalize */
      tr_torrent_class_init,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      sizeof (TrTorrent),
      0,      /* n_preallocs */
      tr_torrent_init, /* instance_init */
      NULL,
    };
    type = g_type_register_static(G_TYPE_OBJECT, "TrTorrent", &info, 0);
  }
  return type;
}

tr_torrent *
tr_torrent_handle(TrTorrent *tor)
{
    g_assert( TR_IS_TORRENT(tor) );

    return isDisposed( tor ) ? NULL : tor->priv->handle;
}

const tr_stat *
tr_torrent_stat(TrTorrent *tor)
{
    tr_torrent * handle = tr_torrent_handle( tor );
    return handle ? tr_torrentStatCached( handle ) : NULL;
}

const tr_info *
tr_torrent_info( TrTorrent * tor )
{
    tr_torrent * handle = tr_torrent_handle( tor );
    return handle ? tr_torrentInfo( handle ) : NULL;
}

void
tr_torrent_start( TrTorrent * self )
{
    tr_torrent * handle = tr_torrent_handle( self );
    if( handle )
        tr_torrentStart( handle );
}

void
tr_torrent_stop( TrTorrent * self )
{
    tr_torrent * handle = tr_torrent_handle( self );
    if( handle )
        tr_torrentStop( handle );
}

static TrTorrent *
maketorrent( tr_torrent * handle )
{
    TrTorrent * tor = g_object_new( TR_TORRENT_TYPE, NULL );
    tor->priv->handle = handle;
    return tor;
}

TrTorrent*
tr_torrent_new_preexisting( tr_torrent * tor )
{
    return maketorrent( tor );
}

TrTorrent *
tr_torrent_new( tr_handle               * handle,
                const char              * metainfo_filename,
                const char              * destination,
                enum tr_torrent_action    act,
                gboolean                  paused,
                char                   ** err )
{
  TrTorrent * ret;
  tr_torrent * tor;
  tr_ctor * ctor;
  int errcode = -1;

  g_assert( destination );

  *err = NULL;

  ctor = tr_ctorNew( handle );
  tr_ctorSetMetainfoFromFile( ctor, metainfo_filename );
  tr_ctorSetDestination( ctor, TR_FORCE, destination );
  tr_ctorSetPaused( ctor, TR_FORCE, paused );
  tr_ctorSetMaxConnectedPeers( ctor, TR_FORCE, pref_int_get( PREF_KEY_MAX_PEERS_PER_TORRENT ) );
  tor = tr_torrentNew( handle, ctor, &errcode );
  tr_ctorFree( ctor );
  
  if( tor == NULL ) {
    switch( errcode ) {
      case TR_EINVALID:
        *err = g_strdup_printf(_("%s: not a valid torrent file"), metainfo_filename );
        break;
      case TR_EDUPLICATE:
        *err = g_strdup_printf(_("%s: torrent is already open"), metainfo_filename );
        break;
      default:
        *err = g_strdup( metainfo_filename );
        break;
    }
    return NULL;
  }

  ret = maketorrent( tor );
  if( TR_TOR_MOVE == act )
    ret->priv->delfile = g_strdup( metainfo_filename );
  return ret;
}

TrTorrent *
tr_torrent_new_with_data( tr_handle    * handle,
                          uint8_t      * metainfo,
                          size_t         size,
                          const char   * destination,
                          gboolean       paused,
                          char        ** err )
{
  tr_torrent * tor;
  tr_ctor * ctor;
  int errcode = -1;  

  g_assert( destination );

  *err = NULL;

  ctor = tr_ctorNew( handle );
  tr_ctorSetMetainfo( ctor, metainfo, size );
  tr_ctorSetDestination( ctor, TR_FORCE, destination );
  tr_ctorSetPaused( ctor, TR_FORCE, paused );
  tr_ctorSetMaxConnectedPeers( ctor, TR_FORCE, pref_int_get( PREF_KEY_MAX_PEERS_PER_TORRENT ) );
  tor = tr_torrentNew( handle, ctor, &errcode );
  
  if( tor == NULL ) {
    switch( errcode ) {
      case TR_EINVALID:
        *err = g_strdup( _("not a valid torrent file") );
        break;
      case TR_EDUPLICATE:
        *err = g_strdup( _("torrent is already open") );
        break;
      default:
        *err = g_strdup( "" );
        break;
    }
    return NULL;
  }

  return maketorrent( tor );
}

void
tr_torrent_check_seeding_cap ( TrTorrent *gtor)
{
  const tr_stat * st = tr_torrent_stat( gtor );
  if ((gtor->priv->seeding_cap_enabled) && (st->ratio >= gtor->priv->seeding_cap))
    tr_torrent_stop (gtor);
}
void
tr_torrent_set_seeding_cap_ratio ( TrTorrent *gtor, gdouble ratio )
{
  gtor->priv->seeding_cap = ratio;
  tr_torrent_check_seeding_cap (gtor);
}
void
tr_torrent_set_seeding_cap_enabled ( TrTorrent *gtor, gboolean b )
{
  if ((gtor->priv->seeding_cap_enabled = b))
    tr_torrent_check_seeding_cap (gtor);
}

char *
tr_torrent_status_str ( TrTorrent * gtor )
{
    char * top = 0;

    const tr_stat * st = tr_torrent_stat( gtor );

    const int tpeers = MAX (st->peersConnected, 0);
    const int upeers = MAX (st->peersGettingFromUs, 0);
    const int eta = st->eta;
    double prog = st->percentDone * 100.0; /* [0...100] */

    switch( st->status )
    {
        case TR_STATUS_CHECK_WAIT:
            prog = st->recheckProgress * 100.0; /* [0...100] */
            top = g_strdup_printf( _("Waiting to verify local data (%.1f%% tested)"), prog );
            break;

        case TR_STATUS_CHECK:
            prog = st->recheckProgress * 100.0; /* [0...100] */
            top = g_strdup_printf( _("Verifying local data (%.1f%% tested)"), prog );
            break;

        case TR_STATUS_DOWNLOAD:
            if( eta < 0 )
                top = g_strdup_printf( _("Stalled (%.1f%%)"), prog );
            else {
                char timestr[128];
                tr_strltime( timestr, eta, sizeof( timestr ) );
                top = g_strdup_printf( _("%s remaining (%.1f%%)"), timestr, prog );
            }
            break;

        case TR_STATUS_DONE:
            top = g_strdup_printf(
                ngettext( "Uploading to %d of %d peer",
                          "Uploading to %d of %d peers", tpeers ),
                          upeers, tpeers );
            break;

        case TR_STATUS_SEED:
            top = g_strdup_printf(
                ngettext( "Seeding to %d of %d peer",
                          "Seeding to %d of %d peers", tpeers ),
                          upeers, tpeers );
            break;

        case TR_STATUS_STOPPED:
            top = g_strdup_printf( _("Stopped (%.1f%%)"), prog );
            break;

        default:
            top = g_strdup_printf( _("Unrecognized state: %d"), st->status );
            break;

    }

    return top;
}
