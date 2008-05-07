/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007 Joshua Elsasser
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <assert.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libtransmission/bencode.h>
#include <libtransmission/ipcparse.h>
#include <libtransmission/utils.h> /* tr_free */

#include "bsdtree.h"
#include "errors.h"
#include "misc.h"
#include "server.h"
#include "torrents.h"

/* time out clients after this many seconds */
#define CLIENT_TIMEOUT          ( 60 )

struct client
{
    int                  fd;
    struct bufferevent * ev;
    struct ipc_info    * ipc;
    RB_ENTRY( client )   link;
};

RB_HEAD( allclients, client );

static void newclient( int, short, void * );
static void noop     ( struct bufferevent *, void * );
static void byebye   ( struct bufferevent *, short, void * );
static void doread   ( struct bufferevent *, void * );
static int  queuemsg ( struct client *, uint8_t *, size_t );
static int  msgresp  ( struct client *, int64_t, enum ipc_msg );
static void defmsg   ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void noopmsg  ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void addmsg1  ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void addmsg2  ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void quitmsg  ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void intmsg   ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void strmsg   ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void infomsg  ( enum ipc_msg, benc_val_t *, int64_t, void * );
static int  addinfo  ( benc_val_t *, int, int );
static int  addstat  ( benc_val_t *, int, int );
static void tormsg   ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void lookmsg  ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void prefmsg  ( enum ipc_msg, benc_val_t *, int64_t, void * );
static void supmsg   ( enum ipc_msg, benc_val_t *, int64_t, void * );
static int  clientcmp( struct client *, struct client * );

RB_GENERATE_STATIC( allclients, client, link, clientcmp )
INTCMP_FUNC( clientcmp, client, ev )

static struct event_base * gl_base    = NULL;
static struct ipc_funcs  * gl_tree    = NULL;
static int                 gl_debug   = 0;
static int                 gl_exiting = 0;
static struct allclients   gl_clients = RB_INITIALIZER( &gl_clients );

int
server_init( struct event_base * base )
{
    assert( NULL == gl_base && NULL == gl_tree );
    gl_base = base;
    gl_tree = ipc_initmsgs();
    if( NULL == gl_tree )
    {
        return -1;
    }

    ipc_addmsg( gl_tree, IPC_MSG_ADDMANYFILES, addmsg1 );
    ipc_addmsg( gl_tree, IPC_MSG_ADDONEFILE,   addmsg2 );
    ipc_addmsg( gl_tree, IPC_MSG_AUTOMAP,      intmsg  );
    ipc_addmsg( gl_tree, IPC_MSG_AUTOSTART,    intmsg  );
    ipc_addmsg( gl_tree, IPC_MSG_CRYPTO,       strmsg  );
    ipc_addmsg( gl_tree, IPC_MSG_DOWNLIMIT,    intmsg  );
    ipc_addmsg( gl_tree, IPC_MSG_DIR,          strmsg  );
    ipc_addmsg( gl_tree, IPC_MSG_GETAUTOMAP,   prefmsg );
    ipc_addmsg( gl_tree, IPC_MSG_GETAUTOSTART, prefmsg );
    ipc_addmsg( gl_tree, IPC_MSG_GETCRYPTO,    prefmsg );
    ipc_addmsg( gl_tree, IPC_MSG_GETDOWNLIMIT, prefmsg );
    ipc_addmsg( gl_tree, IPC_MSG_GETDIR,       prefmsg );
    ipc_addmsg( gl_tree, IPC_MSG_GETINFO,      infomsg );
    ipc_addmsg( gl_tree, IPC_MSG_GETINFOALL,   infomsg );
    ipc_addmsg( gl_tree, IPC_MSG_GETPEX,       prefmsg );
    ipc_addmsg( gl_tree, IPC_MSG_GETPORT,      prefmsg );
    ipc_addmsg( gl_tree, IPC_MSG_GETSTAT,      infomsg );
    ipc_addmsg( gl_tree, IPC_MSG_GETSTATALL,   infomsg );
    ipc_addmsg( gl_tree, IPC_MSG_GETUPLIMIT,   prefmsg );
    ipc_addmsg( gl_tree, IPC_MSG_GETSUP,       supmsg  );
    ipc_addmsg( gl_tree, IPC_MSG_LOOKUP,       lookmsg );
    ipc_addmsg( gl_tree, IPC_MSG_NOOP,         noopmsg );
    ipc_addmsg( gl_tree, IPC_MSG_PEX,          intmsg  );
    ipc_addmsg( gl_tree, IPC_MSG_PORT,         intmsg  );
    ipc_addmsg( gl_tree, IPC_MSG_QUIT,         quitmsg );
    ipc_addmsg( gl_tree, IPC_MSG_REMOVE,       tormsg  );
    ipc_addmsg( gl_tree, IPC_MSG_REMOVEALL,    tormsg  );
    ipc_addmsg( gl_tree, IPC_MSG_START,        tormsg  );
    ipc_addmsg( gl_tree, IPC_MSG_STARTALL,     tormsg  );
    ipc_addmsg( gl_tree, IPC_MSG_STOP,         tormsg  );
    ipc_addmsg( gl_tree, IPC_MSG_STOPALL,      tormsg  );
    ipc_addmsg( gl_tree, IPC_MSG_UPLIMIT,      intmsg  );
    ipc_addmsg( gl_tree, IPC_MSG_VERIFY,       tormsg  );

    ipc_setdefmsg( gl_tree, defmsg );

    return 0;
}

void
server_debug( int enable )
{
    gl_debug = enable;
}

int
server_listen( int fd )
{
    struct event * ev;
    int flags;

    assert( NULL != gl_base );

    flags = fcntl( fd, F_GETFL );
    if( 0 > flags )
    {
        errnomsg( "failed to get flags on socket" );
        return -1;
    }
    if( 0 > fcntl( fd, F_SETFL, flags | O_NONBLOCK ) )
    {
        errnomsg( "failed to set flags on socket" );
        return -1;
    }

    if( 0 > listen( fd, 5 ) )
    {
        errnomsg( "failed to listen on socket" );
        return -1;
    }

    ev = malloc( sizeof *ev );
    if( NULL == ev )
    {
        mallocmsg( sizeof *ev );
        return -1;
    }

    event_set( ev, fd, EV_READ | EV_PERSIST, newclient, ev );
    event_base_set( gl_base, ev );
    event_add( ev, NULL );

    return 0;
}

void
server_quit( void )
{
    struct client * ii, * next;

    if(gl_exiting)
        return;

    torrent_exit( 0 );
    gl_exiting = 1;

    for( ii = RB_MIN( allclients, &gl_clients ); NULL != ii; ii = next )
    {
        next = RB_NEXT( allclients, &gl_clients, ii );
        byebye( ii->ev, EVBUFFER_EOF, NULL );
    }
}

void
newclient( int fd, short event UNUSED, void * arg )
{
    struct sockaddr_un   sa;
    struct client      * client, * old;
    socklen_t            socklen;
    struct bufferevent * clev;
    int                  clfd;
    size_t               buflen;
    uint8_t            * buf;

    if( gl_exiting )
    {
        event_del( arg );
        return;
    }

    for( ;; )
    {
        client = calloc( 1, sizeof *client );
        if( NULL == client )
        {
            mallocmsg( sizeof *client );
            return;
        }

        socklen = sizeof sa;
        clfd = accept( fd, ( struct sockaddr * )&sa, &socklen );
        if( 0 > clfd )
        {
            if( EWOULDBLOCK != errno && EAGAIN != errno &&
                ECONNABORTED != errno )
            {
                errnomsg( "failed to accept ipc connection" );
            }
            free( client );
            break;
        }

        client->ipc = ipc_newcon( gl_tree );
        if( NULL == client->ipc )
        {
            close( clfd );
            free( client );
            return;
        }

        clev = bufferevent_new( clfd, doread, noop, byebye, client );
        if( NULL == clev )
        {
            errnomsg( "failed to create bufferevent" );
            close( clfd );
            ipc_freecon( client->ipc );
            free( client );
            return;
        }
        bufferevent_base_set( gl_base, clev );
        bufferevent_settimeout( clev, CLIENT_TIMEOUT, CLIENT_TIMEOUT );

        client->fd      = clfd;
        client->ev      = clev;
        old = RB_INSERT( allclients, &gl_clients, client );
        assert( NULL == old );

        if( gl_debug )
        {
            printf( "*** new client %i\n", clfd );
        }

        bufferevent_enable( clev, EV_READ );
        buf = ipc_mkvers( &buflen, "Transmission daemon " LONG_VERSION_STRING );
        if( 0 > queuemsg( client, buf, buflen ) )
        {
            free( buf );
            return;
        }
        free( buf );
    }
}

void
noop( struct bufferevent * ev UNUSED, void * arg UNUSED )
{
    /* libevent prior to 1.2 couldn't handle a NULL write callback */
}

void
byebye( struct bufferevent * ev, short what, void * arg UNUSED )
{
    struct client * client, key;

    if( !( EVBUFFER_EOF & what ) )
    {
        if( EVBUFFER_TIMEOUT & what )
        {
            errmsg( "client connection timed out" );
        }
        else if( EVBUFFER_READ & what )
        {
            errmsg( "read error on client connection" );
        }
        else if( EVBUFFER_WRITE & what )
        {
            errmsg( "write error on client connection" );
        }
        else if( EVBUFFER_ERROR & what )
        {
            errmsg( "error on client connection" );
        }
        else
        {
            errmsg( "unknown error on client connection: 0x%x", what );
        }
    }

    memset( &key, 0, sizeof key );
    key.ev = ev;
    client = RB_FIND( allclients, &gl_clients, &key );
    assert( NULL != client );
    RB_REMOVE( allclients, &gl_clients, client );
    bufferevent_free( ev );
    close( client->fd );
    ipc_freecon( client->ipc );
    if( gl_debug )
    {
        printf( "*** client %i went bye-bye\n", client->fd );
    }
    free( client );
}

void
doread( struct bufferevent * ev, void * arg )
{
    struct client * client = arg;
    ssize_t         res;
    uint8_t       * buf;
    size_t          len;

    assert( !gl_exiting );

    buf = EVBUFFER_DATA( EVBUFFER_INPUT( ev ) );
    len = EVBUFFER_LENGTH( EVBUFFER_INPUT( ev ) );

    if( gl_debug )
    {
        printf( "<<< %zu bytes from client %i: ", len, client->fd );
        fwrite( buf, 1, len, stdout );
        putc( '\n', stdout );
    }

    if( IPC_MIN_MSG_LEN > len )
    {
        return;
    }

    res = ipc_handleMessages( client->ipc, buf, len, client );

    if( gl_exiting )
    {
        return;
    }

    if( 0 > res )
    {
        switch( errno )
        {
            case EPERM:
                errmsg( "unsupported protocol version" );
                break;
            case EINVAL:
                errmsg( "protocol parse error" );
                break;
            default:
                errnomsg( "parsing failed" );
                break;
        }
        byebye( ev, EVBUFFER_ERROR, NULL );
    }
    else if( 0 < res )
    {
        evbuffer_drain( EVBUFFER_INPUT( ev ), res );
    }
}

static int
queuemsg( struct client * client, uint8_t * buf, size_t buflen )
{
    if( NULL == buf )
    {
        if( EPERM != errno )
        {
            errnomsg( "failed to build message" );
            byebye( client->ev, EVBUFFER_EOF, NULL );
        }
        return -1;
    }

    if( gl_debug )
    {
        printf( ">>> %zu bytes to client %i: ", buflen, client->fd );
        fwrite( buf, 1, buflen, stdout );
        putc( '\n', stdout );
    }

    if( 0 > bufferevent_write( client->ev, buf, buflen ) )
    {
        errnomsg( "failed to buffer %zd bytes of data for write", buflen );
        return -1;
    }

    return 0;
}

static int
queuepkmsg( struct client * client, tr_benc * pk )
{
    size_t buflen;
    uint8_t * buf = ipc_serialize( pk, &buflen );
    int ret = queuemsg( client, buf, buflen );
    tr_free( buf );
    return ret;
}

int
msgresp( struct client * client, int64_t tag, enum ipc_msg id )
{
    uint8_t * buf;
    size_t    buflen;
    int       ret;

    if( 0 >= tag )
    {
        return 0;
    }

    buf = ipc_mkempty( client->ipc, &buflen, id, tag );
    ret = queuemsg( client, buf, buflen );
    free( buf );

    return ret;
}

void
defmsg( enum ipc_msg id UNUSED, benc_val_t * val UNUSED, int64_t tag,
        void * arg )
{
    struct client * client = arg;

    msgresp( client, tag, IPC_MSG_NOTSUP );
}

void
noopmsg( enum ipc_msg id UNUSED, benc_val_t * val UNUSED, int64_t tag,
         void * arg )
{
    struct client * client = arg;

    msgresp( client, tag, IPC_MSG_OK );
}

void
addmsg1( enum ipc_msg id UNUSED, benc_val_t * val, int64_t tag, void * arg )
{
    struct client * client = arg;
    benc_val_t      pk, * added;
    int             ii, tor;

    if( !tr_bencIsList( val ) )
    {
        msgresp( client, tag, IPC_MSG_BAD );
        return;
    }

    added = ipc_initval( client->ipc, IPC_MSG_INFO, tag, &pk, TYPE_LIST );
    if( NULL == added )
    {
        errnomsg( "failed to build message" );
        byebye( client->ev, EVBUFFER_EOF, NULL );
        return;
    }

    for( ii = 0; ii < val->val.l.count; ii++ )
    {
        tr_benc * file = &val->val.l.vals[ii];
        if( !tr_bencIsString( file ) )
            continue;

        /* XXX need to somehow inform client of skipped or failed files */
        tor = torrent_add_file( file->val.s.s, NULL, -1 );
        if( TORRENT_ID_VALID( tor ) )
        {
            if( 0 > ipc_addinfo( added, tor, torrent_handle( tor ), 0 ) )
            {
                errnomsg( "failed to build message" );
                tr_bencFree( &pk );
                byebye( client->ev, EVBUFFER_EOF, NULL );
                return;
            }
        }
    }

    queuepkmsg( client, &pk );
    tr_bencFree( &pk );
}

void
addmsg2( enum ipc_msg id UNUSED, benc_val_t * dict, int64_t tag, void * arg )
{
    struct client * client = arg;
    benc_val_t    * val, pk;
    int             tor, start;
    const char    * dir;

    if( !tr_bencIsDict( dict ) )
    {
        msgresp( client, tag, IPC_MSG_BAD );
        return;
    }

    val   = tr_bencDictFind( dict, "directory" );
    dir   = tr_bencIsString( val ) ? val->val.s.s : NULL;
    val   = tr_bencDictFind( dict, "autostart" );
    start = tr_bencIsInt( val ) ? (val->val.i!=0) : -1;
    val   = tr_bencDictFind( dict, "data" );
    if( tr_bencIsString( val ) )
    {
        /* XXX detect duplicates and return a message indicating so */
        tor = torrent_add_data( ( uint8_t * )val->val.s.s, val->val.s.i,
                                dir, start );
    }
    else
    {
        val = tr_bencDictFind( dict, "file" );
        if( !tr_bencIsString( val ) )
        {
            msgresp( client, tag, IPC_MSG_BAD );
            return;
        }
        /* XXX detect duplicates and return a message indicating so */
        tor = torrent_add_file( val->val.s.s, dir, start );
    }

    if( TORRENT_ID_VALID( tor ) )
    {
        val = ipc_initval( client->ipc, IPC_MSG_INFO, tag, &pk, TYPE_LIST );
        if( NULL == val )
        {
            errnomsg( "failed to build message" );
            byebye( client->ev, EVBUFFER_EOF, NULL );
            return;
        }
        if( 0 > ipc_addinfo( val, tor, torrent_handle( tor ), 0 ) )
        {
            errnomsg( "failed to build message" );
            tr_bencFree( &pk );
            byebye( client->ev, EVBUFFER_EOF, NULL );
            return;
        }

        queuepkmsg( client, &pk );
        tr_bencFree( &pk );
    }
    else
    {
        msgresp( client, tag, IPC_MSG_FAIL );
    }
}

void
quitmsg( enum ipc_msg id UNUSED, benc_val_t * val UNUSED, int64_t tag UNUSED,
         void * arg UNUSED )
{
    server_quit();
}

void
intmsg( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg )
{
    struct client * client = arg;
    int             num;

    if( !tr_bencIsInt( val ) )
    {
        msgresp( client, tag, IPC_MSG_BAD );
        return;
    }

    num = MAX( INT_MIN, MIN( INT_MAX, val->val.i ) );
    switch( id )
    {
        case IPC_MSG_AUTOMAP:
            torrent_enable_port_mapping( num ? 1 : 0 );
            break;
        case IPC_MSG_AUTOSTART:
            torrent_set_autostart( num ? 1 : 0 );
            break;
        case IPC_MSG_DOWNLIMIT:
            torrent_set_downlimit( num );
            break;
        case IPC_MSG_PEX:
            torrent_set_pex( num ? 1 : 0 );
            break;
        case IPC_MSG_PORT:
            torrent_set_port( num );
            break;
        case IPC_MSG_UPLIMIT:
            torrent_set_uplimit( num );
            break;
        default:
            assert( 0 );
            return;
    }

    msgresp( client, tag, IPC_MSG_OK );
}

void
strmsg( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg )
{
    struct client * client = arg;

    if( !tr_bencIsString( val ) )
    {
        msgresp( client, tag, IPC_MSG_BAD );
        return;
    }

    switch( id )
    {
        case IPC_MSG_CRYPTO:
            if( !strcasecmp( val->val.s.s, "required" ) )
                torrent_set_encryption( TR_ENCRYPTION_REQUIRED );
            else if( !strcasecmp( val->val.s.s, "preferred" ) )
                torrent_set_encryption( TR_ENCRYPTION_PREFERRED );
            else if( !strcasecmp( val->val.s.s, "tolerated" ) )
                torrent_set_encryption( TR_PLAINTEXT_PREFERRED );
            else {
                msgresp(client, tag, IPC_MSG_BAD);
                return;
            }
            break;

        case IPC_MSG_DIR:
            torrent_set_directory( val->val.s.s );
            break;
        default:
            assert( 0 );
            return;
    }

    msgresp( client, tag, IPC_MSG_OK );
}

void
infomsg( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg )
{
    struct client * client = arg;
    benc_val_t      pk, * pkinf, * typelist, * idlist, * idval;
    int             all, types, ii, tor;
    void          * iter;
    enum ipc_msg    respid;
    int         ( * addfunc )( benc_val_t *, int, int );

    all = 0;
    switch( id )
    {
        case IPC_MSG_GETINFOALL:
            all = 1;
            /* FALLTHROUGH; */
        case IPC_MSG_GETINFO:
            respid = IPC_MSG_INFO;
            addfunc = addinfo;
            break;
        case IPC_MSG_GETSTATALL:
            all = 1;
            /* FALLTHROUGH */
        case IPC_MSG_GETSTAT:
            respid = IPC_MSG_STAT;
            addfunc = addstat;
            break;
        default:
            assert( 0 );
            return;
    }

    /* initialize packet */
    pkinf = ipc_initval( client->ipc, respid, tag, &pk, TYPE_LIST );
    if( NULL == pkinf )
    {
        errnomsg( "failed to build message" );
        byebye( client->ev, EVBUFFER_EOF, NULL );
        return;
    }

    /* add info/status for all torrents */
    if( all )
    {
        if( !tr_bencIsList( val ) )
        {
            msgresp( client, tag, IPC_MSG_BAD );
            tr_bencFree( &pk );
            return;
        }
        types = ipc_infotypes( respid, val );
        iter = NULL;
        while( NULL != ( iter = torrent_iter( iter, &tor ) ) )
        {
            if( 0 > addfunc( pkinf, tor, types ) )
            {
                errnomsg( "failed to build message" );
                tr_bencFree( &pk );
                byebye( client->ev, EVBUFFER_EOF, NULL );
                return;
            }
        }
    }
    /* add info/status for the requested IDs */
    else
    {
        if( !tr_bencIsDict( val ) )
        {
            msgresp( client, tag, IPC_MSG_BAD );
            tr_bencFree( &pk );
            return;
        }
        typelist = tr_bencDictFind( val, "type" );
        idlist   = tr_bencDictFind( val, "id" );
        if( !tr_bencIsList(typelist) || !tr_bencIsList(idlist) )
        {
            msgresp( client, tag, IPC_MSG_BAD );
            tr_bencFree( &pk );
            return;
        }
        types = ipc_infotypes( respid, typelist );
        for( ii = 0; idlist->val.l.count > ii; ii++ )
        {
            idval = &idlist->val.l.vals[ii];
            if( TYPE_INT != idval->type || !TORRENT_ID_VALID( idval->val.i ) )
            {
                continue;
            }
            tor = idval->val.i;
            if( 0 > addfunc( pkinf, idval->val.i, types ) )
            {
                errnomsg( "failed to build message" );
                tr_bencFree( &pk );
                byebye( client->ev, EVBUFFER_EOF, NULL );
                return;
            }
        }
    }

    queuepkmsg( client, &pk );
    tr_bencFree( &pk );
}

int
addinfo( benc_val_t * list, int id, int types )
{
    tr_torrent * tor = torrent_handle( id );
    return tor ? ipc_addinfo( list, id, tor, types ) : 0;
}

int
addstat( benc_val_t * list, int id, int types )
{
    tr_torrent * tor = torrent_handle( id );
    return tor ? ipc_addstat( list, id, tor, types ) : 0;
}

void
tormsg( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg )
{
    struct client * client = arg;
    benc_val_t    * idval;
    int             ii, all;
    void          * iter;
    void        ( * func )( int );

    all = 0;
    switch( id )
    {
        case IPC_MSG_REMOVEALL:
            all = 1;
            /* FALLTHROUGH */
        case IPC_MSG_REMOVE:
            func = torrent_remove;
            break;
        case IPC_MSG_STARTALL:
            all = 1;
            /* FALLTHROUGH */
        case IPC_MSG_START:
            func = torrent_start;
            break;
        case IPC_MSG_STOPALL:
            all = 1;
            /* FALLTHROUGH */
        case IPC_MSG_STOP:
            func = torrent_stop;
            break;
        case IPC_MSG_VERIFY:
            all = 0;
            func = torrent_verify;
            break;
        default:
            assert( 0 );
            return;
    }

    /* remove/start/stop all torrents */
    if( all )
    {
        iter = NULL;
        while( NULL != ( iter = torrent_iter( iter, &ii ) ) )
        {
            func( ii );
            if( torrent_remove == func )
            {
                iter = NULL;
            }
        }
    }
    /* remove/start/stop requested list of torrents */
    else
    {
        if( !tr_bencIsList( val ) )
        {
            msgresp( client, tag, IPC_MSG_BAD );
            return;
        }
        for( ii = 0; val->val.l.count > ii; ii++ )
        {
            idval = &val->val.l.vals[ii];
            if( TYPE_INT != idval->type || !TORRENT_ID_VALID( idval->val.i ) )
            {
                continue;
            }
            func( idval->val.i );
        }
    }

    msgresp( client, tag, IPC_MSG_OK );
}

void
lookmsg( enum ipc_msg id UNUSED, benc_val_t * val, int64_t tag, void * arg )
{
    struct client * client = arg;
    int             ii;
    benc_val_t    * hash, pk, * pkinf;
    int64_t         found;

    if( !tr_bencIsList( val ) )
    {
        msgresp( client, tag, IPC_MSG_BAD );
        return;
    }

    pkinf = ipc_initval( client->ipc, IPC_MSG_INFO, tag, &pk, TYPE_LIST );
    if( NULL == pkinf )
    {
        errnomsg( "failed to build message" );
        byebye( client->ev, EVBUFFER_EOF, NULL );
        return;
    }

    for( ii = 0; val->val.l.count > ii; ii++ )
    {
        hash = &val->val.l.vals[ii];
        if( !tr_bencIsString(hash) || SHA_DIGEST_LENGTH * 2 != hash->val.s.i )
        {
            tr_bencFree( &pk );
            msgresp( client, tag, IPC_MSG_BAD );
            return;
        }
        found = torrent_lookup( ( uint8_t * )hash->val.s.s );
        if( !TORRENT_ID_VALID( found ) )
        {
            continue;
        }
        if( 0 > ipc_addinfo( pkinf, found, torrent_handle( found ), IPC_INF_HASH ) )
        {
            errnomsg( "failed to build message" );
            tr_bencFree( &pk );
            byebye( client->ev, EVBUFFER_EOF, NULL );
            return;
        }
    }

    queuepkmsg( client, &pk );
    tr_bencFree( &pk );
}

void
prefmsg( enum ipc_msg id, benc_val_t * val UNUSED, int64_t tag, void * arg )
{
    struct client * client = arg;
    uint8_t       * buf;
    size_t          buflen;
    const char    * strval;

    switch( id )
    {
        case IPC_MSG_GETAUTOMAP:
            buf = ipc_mkint( client->ipc, &buflen, IPC_MSG_AUTOMAP, tag,
                             torrent_get_port_mapping() );
            break;
        case IPC_MSG_GETAUTOSTART:
            buf = ipc_mkint( client->ipc, &buflen, IPC_MSG_AUTOSTART, tag,
                             torrent_get_autostart() );
            break;
        case IPC_MSG_GETCRYPTO:
            switch(torrent_get_encryption()) {
                case TR_ENCRYPTION_REQUIRED:  strval = "required"; break;
                case TR_ENCRYPTION_PREFERRED: strval = "preferred"; break;
                case TR_PLAINTEXT_PREFERRED:  strval = "tolerated"; break;
                default: assert(0); return;
            }
            buf = ipc_mkstr(client->ipc, &buflen, IPC_MSG_CRYPTO, tag, strval);
            break;
        case IPC_MSG_GETDIR:
            buf = ipc_mkstr( client->ipc, &buflen, IPC_MSG_DIR, tag,
                             torrent_get_directory() );
            break;
        case IPC_MSG_GETDOWNLIMIT:
            buf = ipc_mkint( client->ipc, &buflen, IPC_MSG_DOWNLIMIT, tag,
                             torrent_get_downlimit() );
            break;
        case IPC_MSG_GETPEX:
            buf = ipc_mkint( client->ipc, &buflen, IPC_MSG_PEX, tag,
                             torrent_get_pex() );
            break;
        case IPC_MSG_GETPORT:
            buf = ipc_mkint( client->ipc, &buflen, IPC_MSG_PORT, tag,
                             torrent_get_port() );
            break;
        case IPC_MSG_GETUPLIMIT:
            buf = ipc_mkint( client->ipc, &buflen, IPC_MSG_UPLIMIT, tag,
                             torrent_get_uplimit() );
            break;
        default:
            assert( 0 );
            return;
    }

    queuemsg( client, buf, buflen );
    free( buf );
}

void
supmsg( enum ipc_msg id UNUSED, benc_val_t * val, int64_t tag, void * arg )
{
    struct client  * client = arg;
    int              ii;
    benc_val_t       pk, *pkval;
    enum ipc_msg     found;

    if( !tr_bencIsList( val ) )
    {
        msgresp( client, tag, IPC_MSG_BAD );
        return;
    }

    pkval = ipc_initval( client->ipc, IPC_MSG_SUP, tag, &pk, TYPE_LIST );
    if( NULL == pkval )
    {
        errnomsg( "failed to build message" );
        byebye( client->ev, EVBUFFER_EOF, NULL );
        return;
    }
    /* XXX look at other initval to make sure we free pk */
    if( tr_bencListReserve( pkval, val->val.l.count ) )
    {
        errnomsg( "failed to build message" );
        tr_bencFree( &pk );
        byebye( client->ev, EVBUFFER_EOF, NULL );
        return;
    }

    for( ii = 0; val->val.l.count > ii; ii++ )
    {
        tr_benc * name = &val->val.l.vals[ii];
        if( !tr_bencIsString( name ) )
        {
            tr_bencFree( &pk );
            msgresp( client, tag, IPC_MSG_BAD );
            return;
        }
        found = ipc_msgid( client->ipc, name->val.s.s );
        if( IPC__MSG_COUNT == found || !ipc_ishandled( client->ipc, found ) )
        {
            continue;
        }
        tr_bencInitStr( tr_bencListAdd( pkval ),
                        name->val.s.s, name->val.s.i, 1 );
    }

    queuepkmsg( client, &pk );
    tr_bencFree( &pk );
}
