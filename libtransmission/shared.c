/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
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

#include "shared.h"
#include "peer.h"

/* Maximum number of peers that we keep in our local list */
#define MAX_PEER_COUNT 42

struct tr_shared_s
{
    tr_handle_t  * h;
    volatile int die;
    tr_thread_t  thread;
    tr_lock_t    lock;

    /* Incoming connections */
    int          publicPort;
    int          bindPort;
    int          bindSocket;
    int          peerCount;
    tr_peer_t    * peers[MAX_PEER_COUNT];

    /* NAT-PMP/UPnP */
    tr_natpmp_t  * natpmp;
    tr_upnp_t    * upnp;

    /* Choking */
    tr_choking_t * choking;
};

/***********************************************************************
 * Local prototypes
 **********************************************************************/
static void SharedLoop( void * );
static void SetPublicPort( tr_shared_t *, int );
static void AcceptPeers( tr_shared_t * );
static void ReadPeers( tr_shared_t * );
static void DispatchPeers( tr_shared_t * );


/***********************************************************************
 * tr_sharedInit
 ***********************************************************************
 *
 **********************************************************************/
tr_shared_t * tr_sharedInit( tr_handle_t * h )
{
    tr_shared_t * s = calloc( 1, sizeof( tr_shared_t ) );

    s->h = h;
    tr_lockInit( &s->lock );

    s->publicPort = -1;
    s->bindPort   = -1;
    s->bindSocket = -1;
    s->natpmp     = tr_natpmpInit();
    s->upnp       = tr_upnpInit();
    s->choking    = tr_chokingInit( h );

    /* Launch the thread */
    s->die = 0;
    tr_threadCreate( &s->thread, SharedLoop, s, "shared" );

    return s;
}

/***********************************************************************
 * tr_sharedClose
 ***********************************************************************
 *
 **********************************************************************/
void tr_sharedClose( tr_shared_t * s )
{
    int ii;

    /* Stop the thread */
    s->die = 1;
    tr_threadJoin( &s->thread );

    /* Clean up */
    for( ii = 0; ii < s->peerCount; ii++ )
    {
        tr_peerDestroy( s->peers[ii] );
    }
    if( s->bindSocket > -1 )
    {
        tr_netClose( s->bindSocket );
    }
    tr_lockClose( &s->lock );
    tr_natpmpClose( s->natpmp );
    tr_upnpClose( s->upnp );
    tr_chokingClose( s->choking );
    free( s );
}

/***********************************************************************
 * tr_sharedLock, tr_sharedUnlock
 ***********************************************************************
 *
 **********************************************************************/
void tr_sharedLock( tr_shared_t * s )
{
    tr_lockLock( &s->lock );
}
void tr_sharedUnlock( tr_shared_t * s )
{
    tr_lockUnlock( &s->lock );
}

/***********************************************************************
 * tr_sharedSetPort
 ***********************************************************************
 *
 **********************************************************************/
void tr_sharedSetPort( tr_shared_t * s, int port )
{
#ifdef BEOS_NETSERVER
    /* BeOS net_server seems to be unable to set incoming connections
     * to non-blocking. Too bad. */
    return;
#endif

    tr_sharedLock( s );

    if( port == s->bindPort )
    {
        tr_sharedUnlock( s );
        return;
    }
    s->bindPort = port;

    /* Close the previous accept socket, if any */
    if( s->bindSocket > -1 )
    {
        tr_netClose( s->bindSocket );
    }

    /* Create the new one */
    /* XXX should handle failure here in a better way */
    s->bindSocket = tr_netBindTCP( port );
    if( s->bindSocket >= 0 )
    {
        tr_inf( "Bound listening port %d", port );
        listen( s->bindSocket, 5 );
    }

    /* Notify the trackers */
    if( port != s->publicPort )
    {
        SetPublicPort( s, port );
    }

    /* Forward the new port */
    tr_natpmpForwardPort( s->natpmp, port );
    tr_upnpForwardPort( s->upnp, port );

    tr_sharedUnlock( s );
}

/***********************************************************************
 * tr_sharedGetPublicPort
 ***********************************************************************
 *
 **********************************************************************/
int tr_sharedGetPublicPort( tr_shared_t * s )
{
    return s->publicPort;
}

/***********************************************************************
 * tr_sharedTraversalEnable, tr_natTraversalStatus
 ***********************************************************************
 *
 **********************************************************************/
void tr_sharedTraversalEnable( tr_shared_t * s, int enable )
{
    if( enable )
    {
        tr_natpmpStart( s->natpmp );
        tr_upnpStart( s->upnp );
    }
    else
    {
        tr_natpmpStop( s->natpmp );
        tr_upnpStop( s->upnp );
    }
}

int tr_sharedTraversalStatus( tr_shared_t * s )
{
    int statuses[] = {
        TR_NAT_TRAVERSAL_MAPPED,
        TR_NAT_TRAVERSAL_MAPPING,
        TR_NAT_TRAVERSAL_UNMAPPING,
        TR_NAT_TRAVERSAL_ERROR,
        TR_NAT_TRAVERSAL_NOTFOUND,
        TR_NAT_TRAVERSAL_DISABLED,
        -1,
    };
    int natpmp, upnp, ii;

    natpmp = tr_natpmpStatus( s->natpmp );
    upnp = tr_upnpStatus( s->upnp );

    for( ii = 0; 0 <= statuses[ii]; ii++ )
    {
        if( statuses[ii] == natpmp || statuses[ii] == upnp )
        {
            return statuses[ii];
        }
    }

    assert( 0 );

    return TR_NAT_TRAVERSAL_ERROR;

}

/***********************************************************************
 * tr_sharedSetLimit
 **********************************************************************/
void tr_sharedSetLimit( tr_shared_t * s, int limit )
{
    tr_chokingSetLimit( s->choking, limit );
}


/***********************************************************************
 * Local functions
 **********************************************************************/

/***********************************************************************
 * SharedLoop
 **********************************************************************/
static void SharedLoop( void * _s )
{
    tr_shared_t * s = _s;
    uint64_t      date1, date2, lastchoke = 0;

    tr_sharedLock( s );

    while( !s->die )
    {
        date1 = tr_date();

        /* NAT-PMP and UPnP pulses */
        tr_natpmpPulse( s->natpmp );
        tr_upnpPulse( s->upnp );

        /* Handle incoming connections */
        AcceptPeers( s );
        ReadPeers( s );
        DispatchPeers( s );

        /* Update choking every second */
        if( date1 > lastchoke + 1000 )
        {
            tr_chokingPulse( s->choking );
            lastchoke = date1;
        }

        /* Wait up to 20 ms */
        date2 = tr_date();
        if( date2 < date1 + 20 )
        {
            tr_sharedUnlock( s );
            tr_wait( date1 + 20 - date2 );
            tr_sharedLock( s );
        }
    }

    tr_sharedUnlock( s );
}

/***********************************************************************
 * SetPublicPort
 **********************************************************************/
static void SetPublicPort( tr_shared_t * s, int port )
{
    tr_handle_t * h = s->h;
    tr_torrent_t * tor;

    s->publicPort = port;

    for( tor = h->torrentList; tor; tor = tor->next )
    {
        tr_lockLock( &tor->lock );
        tor->publicPort = port;
        tr_lockUnlock( &tor->lock );
    }
}

/***********************************************************************
 * AcceptPeers
 ***********************************************************************
 * Check incoming connections and add the peers to our local list
 **********************************************************************/
static void AcceptPeers( tr_shared_t * s )
{
    int socket;
    in_port_t port;
    struct in_addr addr;

    for( ;; )
    {
        if( s->bindSocket < 0 || s->peerCount >= MAX_PEER_COUNT )
        {
            break;
        }

        socket = tr_netAccept( s->bindSocket, &addr, &port );
        if( socket < 0 )
        {
            break;
        }
        s->peers[s->peerCount++] = tr_peerInit( addr, port, socket );
    }
}

/***********************************************************************
 * ReadPeers
 ***********************************************************************
 * Try to read handshakes
 **********************************************************************/
static void ReadPeers( tr_shared_t * s )
{
    int ii;

    for( ii = 0; ii < s->peerCount; )
    {
        if( tr_peerRead( s->peers[ii] ) )
        {
            tr_peerDestroy( s->peers[ii] );
            s->peerCount--;
            memmove( &s->peers[ii], &s->peers[ii+1],
                     ( s->peerCount - ii ) * sizeof( tr_peer_t * ) );
            continue;
        }
        ii++;
    }
}

/***********************************************************************
 * DispatchPeers
 ***********************************************************************
 * If we got a handshake, try to find the torrent for this peer
 **********************************************************************/
static void DispatchPeers( tr_shared_t * s )
{
    tr_handle_t * h = s->h;
    tr_torrent_t * tor;
    uint8_t * hash;
    int ii;
    uint64_t now = tr_date();

    for( ii = 0; ii < s->peerCount; )
    {
        hash = tr_peerHash( s->peers[ii] );

        if( !hash && now > tr_peerDate( s->peers[ii] ) + 10000 )
        {
            /* 10 seconds and no handshake, drop it */
            tr_peerDestroy( s->peers[ii] );
            goto removePeer;
        }
        if( hash )
        {
            for( tor = h->torrentList; tor; tor = tor->next )
            {
                tr_lockLock( &tor->lock );
                if( tor->status & TR_STATUS_INACTIVE )
                {
                    tr_lockUnlock( &tor->lock );
                    continue;
                }

                if( 0 == memcmp( tor->info.hash, hash,
                            SHA_DIGEST_LENGTH ) )
                {
                    /* Found it! */
                    tr_torrentAttachPeer( tor, s->peers[ii] );
                    tr_lockUnlock( &tor->lock );
                    goto removePeer;
                }
                tr_lockUnlock( &tor->lock );
            }

            /* Couldn't find a torrent, we probably removed it */
            tr_peerDestroy( s->peers[ii] );
            goto removePeer;
        }
        ii++;
        continue;

removePeer:
        s->peerCount--;
        memmove( &s->peers[ii], &s->peers[ii+1],
                ( s->peerCount - ii ) * sizeof( tr_peer_t * ) );
    }
}

