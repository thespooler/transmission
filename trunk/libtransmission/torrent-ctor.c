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

#include <libgen.h> /* basename */
#include "transmission.h"
#include "bencode.h"
#include "platform.h"
#include "session.h" /* tr_sessionFindTorrentFile */
#include "utils.h"

#define DEFAULT_MAX_CONNECTED_PEERS 50

struct optional_args
{
    unsigned int isSet_paused : 1;
    unsigned int isSet_connected : 1;
    unsigned int isSet_downloadDir : 1;

    unsigned int isPaused : 1;
    uint16_t peerLimit;
    char downloadDir[MAX_PATH_LENGTH];
};

/** Opaque class used when instantiating torrents.
  * @ingroup tr_ctor */
struct tr_ctor
{
    const tr_handle * handle;
    unsigned int saveInOurTorrentsDir : 1;
    unsigned int doDelete : 1;

    unsigned int isSet_metainfo : 1;
    unsigned int isSet_delete : 1;
    tr_benc metainfo;
    char * sourceFile;

    struct optional_args optionalArgs[2];
};

/***
****
***/

static void
setSourceFile( tr_ctor * ctor, const char * sourceFile )
{
    tr_free( ctor->sourceFile );
    ctor->sourceFile = tr_strdup( sourceFile );
}

static void
clearMetainfo( tr_ctor * ctor )
{
    if( ctor->isSet_metainfo ) {
        ctor->isSet_metainfo = 0;
        tr_bencFree( &ctor->metainfo );
    }

    setSourceFile( ctor, NULL );
}

int
tr_ctorSetMetainfo( tr_ctor        * ctor,
                    const uint8_t  * metainfo,
                    size_t           len )
{
    int err;
    clearMetainfo( ctor );
    err = tr_bencLoad( metainfo, len, &ctor->metainfo, NULL );
    ctor->isSet_metainfo = !err;
    return err;
}

const char*
tr_ctorGetSourceFile( const tr_ctor * ctor )
{
    return ctor->sourceFile;
}

int
tr_ctorSetMetainfoFromFile( tr_ctor        * ctor,
                            const char     * filename )
{
    uint8_t * metainfo;
    size_t len;
    int err;

    metainfo = tr_loadFile( filename, &len );
    if( metainfo && len )
        err = tr_ctorSetMetainfo( ctor, metainfo, len );
    else {
        clearMetainfo( ctor );
        err = 1;
    }

    setSourceFile( ctor, filename );

    /* if no `name' field was set, then set it from the filename */
    if( ctor->isSet_metainfo ) {
        tr_benc * info = tr_bencDictFindType( &ctor->metainfo, "info", TYPE_DICT );
        if( info != NULL ) {
            tr_benc * name = tr_bencDictFindFirst( info, "name.utf-8", "name", NULL );
            if( name == NULL )
                name = tr_bencDictAdd( info, "name" );
            if( name->type!=TYPE_STR || !name->val.s.s || !*name->val.s.s ) {
                char * tmp = tr_strdup( filename );
                tr_bencInitStrDup( name, basename( tmp ) );
                tr_free( tmp );
            }
        }
    }

    tr_free( metainfo );
    return err;
}

int
tr_ctorSetMetainfoFromHash( tr_ctor        * ctor,
                            const char     * hashString )
{
    int err;
    const char * filename;

    if(( filename = tr_sessionFindTorrentFile( ctor->handle, hashString )))
        err = tr_ctorSetMetainfoFromFile( ctor, filename ); 
    else
        err = TR_ERROR;

    return err;
}

/***
****
***/

void
tr_ctorSetDeleteSource( tr_ctor  * ctor,
                        uint8_t    deleteSource )
{
    ctor->doDelete = deleteSource ? 1 : 0;
    ctor->isSet_delete = 1;
}

int
tr_ctorGetDeleteSource( const tr_ctor  * ctor,
                        uint8_t        * setme )
{
    int err = 0;

    if( !ctor->isSet_delete )
        err = 1;
    else if( setme )
        *setme = ctor->doDelete ? 1 : 0;

    return err;
}

/***
****
***/

void
tr_ctorSetSave( tr_ctor  * ctor,
                int        saveInOurTorrentsDir )
{
    ctor->saveInOurTorrentsDir = saveInOurTorrentsDir ? 1 : 0;
}

int
tr_ctorGetSave( const tr_ctor * ctor )
{
    return ctor && ctor->saveInOurTorrentsDir;
}

void
tr_ctorSetPaused( tr_ctor        * ctor,
                  tr_ctorMode      mode,
                  uint8_t          isPaused )
{
    struct optional_args * args = &ctor->optionalArgs[mode];
    args->isSet_paused = 1;
    args->isPaused = isPaused ? 1 : 0;
}

void
tr_ctorSetPeerLimit( tr_ctor        * ctor,
                     tr_ctorMode      mode,
                     uint16_t         peerLimit )
{
    struct optional_args * args = &ctor->optionalArgs[mode];
    args->isSet_connected = 1;
    args->peerLimit = peerLimit;
}

void
tr_ctorSetDownloadDir( tr_ctor        * ctor,
                       tr_ctorMode      mode,
                       const char     * directory )
{
    struct optional_args * args = &ctor->optionalArgs[mode];
    if( directory ) {
        args->isSet_downloadDir = 1;
        tr_strlcpy( args->downloadDir, directory, sizeof( args->downloadDir ) );
    } else {
        args->isSet_downloadDir = 0;
        *args->downloadDir = '\0';
    }
}

int
tr_ctorGetPeerLimit( const tr_ctor  * ctor,
                     tr_ctorMode      mode,
                     uint16_t       * setmeCount )
{
    int err = 0;
    const struct optional_args * args = &ctor->optionalArgs[mode];

    if( !args->isSet_connected )
        err = 1;
    else if( setmeCount )
        *setmeCount = args->peerLimit;

    return err;
}

int
tr_ctorGetPaused( const tr_ctor  * ctor,
                  tr_ctorMode      mode,
                  uint8_t        * setmeIsPaused )
{
    int err = 0;
    const struct optional_args * args = &ctor->optionalArgs[mode];

    if( !args->isSet_paused )
        err = 1;
    else if( setmeIsPaused )
        *setmeIsPaused = args->isPaused ? 1 : 0;

    return err;
}

int
tr_ctorGetDownloadDir( const tr_ctor  * ctor,
                       tr_ctorMode      mode,
                       const char    ** setmeDownloadDir )
{
    int err = 0;
    const struct optional_args * args = &ctor->optionalArgs[mode];

    if( !args->isSet_downloadDir )
        err = 1;
    else if( setmeDownloadDir )
        *setmeDownloadDir = args->downloadDir;

    return err;
}

int
tr_ctorGetMetainfo( const tr_ctor    * ctor,
                    const tr_benc   ** setme )
{
    int err = 0;

    if( !ctor->isSet_metainfo )
        err = 1;
    else if( setme )
        *setme = &ctor->metainfo;

    return err;
}

/***
****
***/

tr_ctor*
tr_ctorNew( const tr_handle * handle )
{
    tr_ctor * ctor = tr_new0( struct tr_ctor, 1 );
    ctor->handle = handle;
    tr_ctorSetPeerLimit( ctor, TR_FALLBACK, DEFAULT_MAX_CONNECTED_PEERS );
    tr_ctorSetPaused( ctor, TR_FALLBACK, FALSE );
    tr_ctorSetDownloadDir( ctor, TR_FALLBACK, handle->downloadDir );
    tr_ctorSetSave( ctor, TRUE );
    return ctor;
}

void
tr_ctorFree( tr_ctor * ctor )
{
    clearMetainfo( ctor );
    tr_free( ctor );
}
