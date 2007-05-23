/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2007 Transmission authors and contributors
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

#include "transmission.h"
#include "bencode.h"

/* XXX */
#define TR_WANT_TORRENT_PRIVATE

#include "tr_prefs.h"
#include "tr_torrent.h"
#include "util.h"

enum {
  TR_TORRENT_HANDLE = 1,
  TR_TORRENT_DIR,
  TR_TORRENT_PAUSED,
};

static void
tr_torrent_init(GTypeInstance *instance, gpointer g_class);
static void
tr_torrent_set_property(GObject *object, guint property_id,
                        const GValue *value, GParamSpec *pspec);
static void
tr_torrent_get_property(GObject *object, guint property_id,
                        GValue *value, GParamSpec *pspec);
static void
tr_torrent_class_init(gpointer g_class, gpointer g_class_data);
static void
tr_torrent_dispose(GObject *obj);
static void
tr_torrent_set_folder(TrTorrent *tor);
static gboolean
tr_torrent_paused(TrTorrent *tor);

static gpointer
tracker_boxed_fake_copy( gpointer boxed )
{
    return boxed;
}

static void
tracker_boxed_fake_free( gpointer boxed SHUTUP )
{
}

GType
tr_tracker_boxed_get_type( void )
{
    static GType type = 0;

    if( 0 == type )
    {
        type = g_boxed_type_register_static( "TrTrackerBoxed",
                                             tracker_boxed_fake_copy,
                                             tracker_boxed_fake_free );
    }

    return type;
}

GType
tr_torrent_get_type(void) {
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

static void
tr_torrent_class_init(gpointer g_class, gpointer g_class_data SHUTUP) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(g_class);
  GParamSpec *pspec;

  gobject_class->set_property = tr_torrent_set_property;
  gobject_class->get_property = tr_torrent_get_property;
  gobject_class->dispose = tr_torrent_dispose;

  pspec = g_param_spec_pointer("torrent-handle", "Torrent handle",
                               "Torrent handle from libtransmission",
                               G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
  g_object_class_install_property(gobject_class, TR_TORRENT_HANDLE, pspec);

  pspec = g_param_spec_string("download-directory", "Download directory",
                              "Directory to download files to", NULL,
                              G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
  g_object_class_install_property(gobject_class, TR_TORRENT_DIR, pspec);

  pspec = g_param_spec_boolean("paused", "Paused",
                               "Is the torrent paused or running", TRUE,
                               G_PARAM_READWRITE);
  g_object_class_install_property(gobject_class, TR_TORRENT_PAUSED, pspec);
}

static void
tr_torrent_init(GTypeInstance *instance, gpointer g_class SHUTUP) {
  TrTorrent *self = (TrTorrent *)instance;

#ifdef REFDBG
  fprintf( stderr, "torrent %p init\n", self );
#endif

  self->handle = NULL;
  self->dir = NULL;
  self->delfile = NULL;
  self->severed = FALSE;
  self->disposed = FALSE;
}

static void
tr_torrent_set_property(GObject *object, guint property_id,
                        const GValue *value, GParamSpec *pspec) {
  TrTorrent *self = (TrTorrent*)object;

  if(self->severed)
    return;

  switch(property_id) {
    case TR_TORRENT_HANDLE:
      g_assert(NULL == self->handle);
      self->handle = g_value_get_pointer(value);
      if(NULL != self->handle && NULL != self->dir)
        tr_torrent_set_folder(self);
      break;
    case TR_TORRENT_DIR:
      g_assert(NULL == self->dir);
      self->dir = g_value_dup_string(value);
      if(NULL != self->handle && NULL != self->dir)
        tr_torrent_set_folder(self);
      break;
    case TR_TORRENT_PAUSED:
      g_assert(NULL != self->handle);
      if(tr_torrent_paused(self) != g_value_get_boolean(value))
        (g_value_get_boolean(value) ? tr_torrentStop : tr_torrentStart)
          (self->handle);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void
tr_torrent_get_property(GObject *object, guint property_id,
                        GValue *value, GParamSpec *pspec) {
  TrTorrent *self = (TrTorrent*)object;

  if(self->severed)
    return;

  switch(property_id) {
    case TR_TORRENT_HANDLE:
      g_value_set_pointer(value, self->handle);
      break;
    case TR_TORRENT_DIR:
      g_value_set_string(value, (NULL != self->dir ? self->dir :
                                 tr_torrentGetFolder(self->handle)));
      break;
    case TR_TORRENT_PAUSED:
      g_value_set_boolean(value, tr_torrent_paused(self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void
tr_torrent_dispose(GObject *obj) {
  GObjectClass *parent = g_type_class_peek(g_type_parent(TR_TORRENT_TYPE));
  TrTorrent *self = (TrTorrent*)obj;

  if(self->disposed)
    return;
  self->disposed = TRUE;

#ifdef REFDBG
  fprintf( stderr, "torrent %p dispose\n", self );
#endif

  if( !self->severed )
  {
      tr_torrent_sever( self );
  }

  if(NULL != self->delfile)
    g_free(self->delfile);

  /* Chain up to the parent class */
  parent->dispose(obj);
}

void
tr_torrent_sever( TrTorrent * self )
{
    TR_IS_TORRENT( self );

    if( self->severed )
    {
        return;
    }

#ifdef REFDBG
    fprintf( stderr, "torrent %p sever\n", self );
#endif

    if( NULL == self->handle )
    {
        self->severed = TRUE;
        return;
    }

    if( !tr_torrent_paused( self ) )
    {
        tr_torrentStop( self->handle );
    }
    tr_torrentClose( self->handle );
    self->severed = TRUE;
}

tr_torrent_t *
tr_torrent_handle(TrTorrent *tor) {
  TR_IS_TORRENT(tor);

  if(tor->severed)
    return NULL;

  return tor->handle;
}

tr_stat_t *
tr_torrent_stat(TrTorrent *tor) {
  TR_IS_TORRENT(tor);

  if(tor->severed)
    return NULL;

  return tr_torrentStat(tor->handle);
}

tr_info_t *
tr_torrent_info(TrTorrent *tor) {
  TR_IS_TORRENT(tor);

  if(tor->severed)
    return NULL;

  return tr_torrentInfo(tor->handle);
}

TrTorrent *
tr_torrent_new( tr_handle_t * back, const char *torrent, const char *dir,
               guint flags, char **err) {
  TrTorrent *ret;
  tr_torrent_t *handle;
  int errcode, trflags;
  gboolean boolval;

  g_assert(NULL != dir);

  *err = NULL;

  trflags = 0;
  if((TR_TORNEW_SAVE_COPY|TR_TORNEW_SAVE_MOVE) & flags)
    trflags |= TR_FLAG_SAVE;
  errcode = -1;

  if(TR_TORNEW_LOAD_SAVED & flags)
    handle = tr_torrentInitSaved(back, torrent, 0, &errcode);
  else
    handle = tr_torrentInit(back, torrent, NULL, trflags, &errcode);

  if(NULL == handle) {
    switch(errcode) {
      case TR_EINVALID:
        *err = g_strdup_printf(_("%s: not a valid torrent file"), torrent);
        break;
      case TR_EDUPLICATE:
        *err = g_strdup_printf(_("%s: torrent is already open"), torrent);
        break;
      default:
        *err = g_strdup(torrent);
        break;
    }
    return NULL;
  }

  /* I should probably add a property for this but I've had enough
     with adding useless gtk glue to this program */
  boolval = tr_prefs_get_bool_with_default( PREF_ID_PEX );
  tr_torrentDisablePex( handle, !boolval );

  ret = g_object_new(TR_TORRENT_TYPE, "torrent-handle", handle,
                     "download-directory", dir, NULL);
  
  g_object_set(ret, "paused", (TR_TORNEW_PAUSED & flags ? TRUE : FALSE), NULL);

  if(TR_TORNEW_SAVE_MOVE & flags)
    ret->delfile = g_strdup(torrent);

  return ret;
}

TrTorrent *
tr_torrent_new_with_state( tr_handle_t * back, benc_val_t * state,
                           guint forcedflags, char ** err)
{
  int ii;
  benc_val_t *name, *data;
  char *torrent, *hash, *dir;
  gboolean hadpaused, paused;
  guint flags;

  *err = NULL;

  if(TYPE_DICT != state->type)
    return NULL;

  torrent = hash = dir = NULL;
  hadpaused = FALSE;
  paused = FALSE;               /* silence stupid compiler warning */

  for(ii = 0; ii + 1 < state->val.l.count; ii += 2) {
    name = state->val.l.vals + ii;
    data = state->val.l.vals + ii + 1;
    if(TYPE_STR == name->type &&
       (TYPE_STR == data->type || TYPE_INT == data->type)) {
      if(0 == strcmp("torrent", name->val.s.s))
        torrent = data->val.s.s;
      if(0 == strcmp("hash", name->val.s.s))
        hash = data->val.s.s;
      else if(0 == strcmp("dir", name->val.s.s))
        dir = data->val.s.s;
      else if(0 == strcmp("paused", name->val.s.s)) {
        hadpaused = TRUE;
        paused = (data->val.i ? TRUE : FALSE);
      }
    }
  }

  if((NULL != torrent && NULL != hash) ||
     (NULL == torrent && NULL == hash) || NULL == dir)
    return NULL;

  flags = 0;
  if(hadpaused)
    flags |= (paused ? TR_TORNEW_PAUSED : TR_TORNEW_RUNNING);
  if(NULL != hash) {
    flags |= TR_TORNEW_LOAD_SAVED;
    torrent = hash;
  }
  forcedflags &= TR_TORNEW_PAUSED | TR_TORNEW_RUNNING;
  if( forcedflags )
  {
      flags &= ~( TR_TORNEW_PAUSED | TR_TORNEW_RUNNING );
      flags |= forcedflags;
  }

  return tr_torrent_new( back, torrent, dir, flags, err );
}

gboolean
tr_torrent_get_state( TrTorrent * tor, benc_val_t * state )
{
    tr_info_t  * inf;

    TR_IS_TORRENT( tor );

    if( tor->severed )
    {
        return FALSE;
    }

    inf = tr_torrentInfo( tor->handle );

    tr_bencInit( state, TYPE_DICT );
    if( tr_bencDictReserve( state, 3 ) )
    {
        return FALSE;
    }

    if( TR_FLAG_SAVE & inf->flags )
    {
        tr_bencInitStr( tr_bencDictAdd( state, "hash" ),
                        inf->hashString, -1, 1 );
    }
    else
    {
        tr_bencInitStr( tr_bencDictAdd( state, "torrent" ),
                        inf->torrent, -1, 1 );
    }
    tr_bencInitStr( tr_bencDictAdd( state, "dir" ),
                    tr_torrentGetFolder( tor->handle ), -1, 1 );
    tr_bencInitInt( tr_bencDictAdd( state, "paused" ),
                    ( tr_torrent_paused( tor ) ? 1 : 0 ) );

    return TRUE;
}

/* XXX this should probably be done with a signal */
void
tr_torrent_state_saved(TrTorrent *tor) {
  TR_IS_TORRENT(tor);

  if(tor->severed)
    return;

  if(NULL != tor->delfile) {
    unlink(tor->delfile);
    g_free(tor->delfile);
    tor->delfile = NULL;
  }
}

static void
tr_torrent_set_folder(TrTorrent *tor) {
  char *wd;

  if(NULL != tor->dir)
    tr_torrentSetFolder(tor->handle, tor->dir);
  else {
    wd = g_new(char, MAX_PATH_LENGTH + 1);
    tr_torrentSetFolder(tor->handle,
                        (NULL == getcwd(wd, MAX_PATH_LENGTH + 1) ? "." : wd));
    g_free(wd);
  }
}

static gboolean
tr_torrent_paused(TrTorrent *tor) {
  tr_stat_t *st = tr_torrentStat(tor->handle);

  return (TR_STATUS_INACTIVE & st->status ? TRUE : FALSE);
}
