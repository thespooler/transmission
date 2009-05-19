/*
Copyright (c) 2009 by Juliusz Chroboczek

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <event.h>

#include <dht/dht.h>

#include "transmission.h"
#include "crypto.h"
#include "peer-mgr.h"
#include "platform.h"
#include "session.h"
#include "torrent.h"
#include "trevent.h"
#include "tr-dht.h"
#include "utils.h"

static int dht_socket;
static struct event dht_event;
static tr_port dht_port;
static unsigned char myid[20];
static tr_session *session = NULL;

static void event_callback(int s, short type, void *ignore);

struct bootstrap_closure {
    tr_session *session;
    uint8_t *nodes;
    size_t len;
};

static void
dht_bootstrap(void *closure)
{
    struct bootstrap_closure *cl = closure;
    size_t i;

    if(session != cl->session)
        return;

    for(i = 0; i < cl->len; i += 6)
    {
        struct timeval tv;
        tr_port port;
        struct tr_address addr;
        int status;

        memset(&addr, 0, sizeof(addr));
        addr.type = TR_AF_INET;
        memcpy(&addr.addr.addr4, &cl->nodes[i], 4);
        memcpy(&port, &cl->nodes[i + 4], 2);
        port = ntohs(port);
        /* There's no race here -- if we uninit between the test and the
           AddNode, the AddNode will be ignored. */
        status = tr_dhtStatus(cl->session, NULL);
        if(status == TR_DHT_STOPPED || status >= TR_DHT_FIREWALLED)
            break;
        tr_dhtAddNode(cl->session, &addr, port, 1);
        tv.tv_sec = 2 + tr_cryptoWeakRandInt( 5 );
        tv.tv_usec = tr_cryptoWeakRandInt( 1000000 );
        select(0, NULL, NULL, NULL, &tv);
    }
    tr_free( cl->nodes );
    tr_free( closure );
}

int
tr_dhtInit(tr_session *ss)
{
    struct sockaddr_in sin;
    struct timeval tv;
    tr_benc benc;
    int rc;
    tr_bool have_id = FALSE;
    char * dat_file;
    uint8_t * nodes = NULL;
    const uint8_t * raw;
    size_t len;

    if(session)
        return -1;

    dht_socket = socket(PF_INET, SOCK_DGRAM, 0);
    if(dht_socket < 0)
        return -1;

    dht_port = tr_sessionGetPeerPort(ss);
    if(dht_port <= 0)
        return -1;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(dht_port);
    rc = bind(dht_socket, (struct sockaddr*)&sin, sizeof(sin));
    if(rc < 0)
        goto fail;

#ifdef DEBUG_DHT
    dht_debug = stdout;
#endif

    dat_file = tr_buildPath( ss->configDir, "dht.dat", NULL );
    rc = tr_bencLoadFile(dat_file, &benc);
    tr_free( dat_file );
    if(rc == 0) {
        if(tr_bencDictFindRaw(&benc, "id", &raw, &len)) {
            if(raw && len == 20) {
                memcpy(myid, raw, len);
                have_id = TRUE;
            }
        }
        if(tr_bencDictFindRaw(&benc, "nodes", &raw, &len)) {
            if(len % 6 == 2) {
                /* This hack allows reading of uTorrent files, which I find
                   convenient. */
                len -= 2;
            }
            nodes = tr_new( uint8_t, len );
            memcpy( nodes, raw, len );
        }
        tr_bencFree(&benc);
    }

    if(!have_id) {
        /* Note that you cannot just use your BT id -- DHT ids need to be
           distributed uniformly, so it should either be the SHA-1 of
           something, or truly random. */
        tr_cryptoRandBuf( myid, 20 );
        have_id = TRUE;
    }

    rc = dht_init(dht_socket, myid);
    if(rc < 0)
        goto fail;

    session = ss;

    if(nodes) {
        struct bootstrap_closure * cl = tr_new( struct bootstrap_closure, 1 );
        if( !cl )
            tr_free( nodes );
        else {
            cl->session = session;
            cl->nodes = nodes;
            cl->len = len;
            tr_threadNew( dht_bootstrap, cl );
        }
    }
    tv.tv_sec = 0;
    tv.tv_usec = tr_cryptoWeakRandInt( 1000000 );
    event_set( &dht_event, dht_socket, EV_READ, event_callback, NULL );
    event_add( &dht_event, &tv );

    return 1;

    fail:
    {
        const int save = errno;
        close(dht_socket);
        dht_socket = -1;
        session = NULL;
        errno = save;
    }

    return -1;
}

void
tr_dhtUninit(tr_session *ss)
{
    if(session != ss)
        return;

    event_del(&dht_event);

    /* Since we only save known good nodes, avoid erasing older data if we
       don't know enough nodes. */
    if(tr_dhtStatus(ss, NULL) >= TR_DHT_FIREWALLED) {
        tr_benc benc;
        struct sockaddr_in sins[300];
        char compact[300 * 6];
        char *dat_file;
        int n, i, j;
        n = dht_get_nodes(sins, 300);
        j = 0;
        for(i = 0; i < n; i++) {
            memcpy(compact + j, &sins[i].sin_addr, 4);
            memcpy(compact + j + 4, &sins[i].sin_port, 2);
            j += 6;
        }
        tr_bencInitDict(&benc, 2);
        tr_bencDictAddRaw(&benc, "id", myid, 20);
        tr_bencDictAddRaw(&benc, "nodes", compact, j);
        dat_file = tr_buildPath( ss->configDir, "dht.dat", NULL );
        tr_bencSaveFile( dat_file, &benc );
        tr_free( dat_file );
    }

    dht_uninit(dht_socket, 0);

    session = NULL;
}

tr_bool
tr_dhtEnabled(tr_session *ss)
{
    return ss && ss == session;
}

struct getstatus_closure
{
    sig_atomic_t status;
    sig_atomic_t count;
};

static void
getstatus(void *closure)
{
    struct getstatus_closure *ret = (struct getstatus_closure*)closure;
    int good, dubious, incoming;

    dht_nodes(&good, &dubious, NULL, &incoming);

    if( good < 4 || good + dubious <= 8 )
        ret->status = TR_DHT_BROKEN;
    else if( good < 40 )
        ret->status = TR_DHT_POOR;
    else if( incoming < 8 )
        ret->status = TR_DHT_FIREWALLED;
    else
        ret->status = TR_DHT_GOOD;

    ret->count = good + dubious;
}

int
tr_dhtStatus(tr_session *ss, int *nodes_return )
{
    struct getstatus_closure ret = { -1, - 1 };

    if( !tr_dhtEnabled( ss ) )
        return TR_DHT_STOPPED;

    tr_runInEventThread( ss, getstatus, &ret );
    while( ret.status < 0 )
        tr_wait( 10 /*msec*/ );

    if( nodes_return )
        *nodes_return = ret.count;

    return ret.status;
}

tr_port
tr_dhtPort(tr_session *ss)
{
    return tr_dhtEnabled( ss ) ? dht_port : 0;
}

int
tr_dhtAddNode(tr_session *ss, tr_address *address, tr_port port, tr_bool bootstrap)
{
    struct sockaddr_in sin;

    if( !tr_dhtEnabled( ss ) )
        return 0;

    if( address->type != TR_AF_INET )
        return 0;

    /* Since we don't want to abuse our bootstrap nodes, we don't ping them
       if the DHT is in a good state. */
    if(bootstrap) {
        if(tr_dhtStatus(ss, NULL) >= TR_DHT_FIREWALLED)
            return 0;
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    memcpy(&sin.sin_addr, &address->addr.addr4, 4);
    sin.sin_port = htons(port);
    dht_ping_node(dht_socket, &sin);

    return 1;
}

static void
callback(void *ignore UNUSED, int event,
         unsigned char *info_hash, void *data, size_t data_len)
{
    if(event == DHT_EVENT_VALUES) {
        tr_torrent *tor;
        tr_pex *pex;
        size_t i, n;
        pex = tr_peerMgrCompactToPex(data, data_len, NULL, 0, &n);
        tr_globalLock(session);
        tor = tr_torrentFindFromHash(session, info_hash);
        if(tor && tr_torrentAllowsDHT(tor)) {
            for(i = 0; i < n; i++)
                tr_peerMgrAddPex(tor, TR_PEER_FROM_DHT, pex + i);
        }
        tr_globalUnlock(session);
        tr_free(pex);
    } else if(event == DHT_EVENT_SEARCH_DONE) {
        tr_torrent * tor = tr_torrentFindFromHash( session, info_hash );
        if(tor)
            tor->dhtAnnounceInProgress = 0;
    }
}

int
tr_dhtAnnounce(tr_torrent *tor, tr_bool announce)
{
    if( !tr_torrentAllowsDHT( tor ) )
        return -1;

    if( tr_dhtStatus( tor->session, NULL ) < TR_DHT_POOR )
        return 0;

    dht_search( dht_socket, tor->info.hash,
                announce ? tr_sessionGetPeerPort(session) : 0,
                callback, NULL);

    tor->dhtAnnounceInProgress = TRUE;
    return 1;
}

static void
event_callback(int s, short type, void *ignore UNUSED )
{
    int rc;
    time_t tosleep;
    struct timeval tv;

    rc = dht_periodic(s, type == EV_READ, &tosleep, callback, NULL);
    if(rc < 0) {
        if(errno == EINTR) {
            tosleep = 0;
        } else {
            perror("dht_periodic");
            if(rc == EINVAL || rc == EFAULT)
                    abort();
            tosleep = 1;
        }
    }

    /* Being slightly late is fine, and has the added benefit of adding
       some jitter. */
    tv.tv_sec = tosleep;
    tv.tv_usec = tr_cryptoWeakRandInt( 1000000 );
    event_add(&dht_event, &tv);
}

void
dht_hash(void *hash_return, int hash_size,
         const void *v1, int len1,
         const void *v2, int len2,
         const void *v3, int len3)
{
    unsigned char sha1[20];
    tr_sha1(sha1, v1, len1, v2, len2, v3, len3, NULL);
    if(hash_size > 20) {
        memset((char*)hash_return + 20, 0, hash_size - 20);
    }
    memcpy(hash_return, sha1, hash_size > 20 ? 20 : hash_size);
}
