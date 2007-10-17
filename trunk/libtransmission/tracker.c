/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <ctype.h> /* isalnum */
#include <limits.h> /* INT_MAX */
#include <stdio.h> /* snprintf */
#include <stdlib.h>
#include <string.h> /* strcmp, strchr */
#include <sys/queue.h> /* for evhttp */
#include <sys/types.h> /* for evhttp */

#include <event.h>
#include <evhttp.h>

#include "transmission.h"
#include "bencode.h"
#include "completion.h"
#include "net.h"
#include "ptrarray.h"
#include "publish.h"
#include "shared.h"
#include "tracker.h"
#include "trevent.h"
#include "utils.h"

#define MINUTES_TO_MSEC(N) ((N) * 60 * 1000)

/* we follow uTorrent 1.8's lead of allowing a manual reannounce
 * every MAX( 60 seconds, min_interval ) */
#define DEFAULT_MANUAL_ANNOUNCE_INTERVAL_SEC (60)

/* unless the tracker tells us otherwise, rescrape this frequently */
#define DEFAULT_SCRAPE_INTERVAL_SEC (60*15)

/* unless the tracker tells us otherwise, this is the announce interval */
#define DEFAULT_ANNOUNCE_INTERVAL_SEC (240)

/* unless the tracker tells us otherwise, this is the announce min_interval */
#define DEFAULT_MIN_ANNOUNCE_INTERVAL_SEC (30)

/* this is how long we'll leave a request hanging before timeout */
#define TIMEOUT_INTERVAL_SEC 40
#define STOPPING_TIMEOUT_INTERVAL_SEC 8

/* the value of the 'numwant' argument passed in tracker requests.
 * this should be big, but if it's *too* big trackers will ignore it */
#define NUMWANT 256

/* the length of the 'key' argument passed in tracker requests */
#define TR_KEY_LEN 10


/**
***
**/

typedef struct
{
    tr_handle * handle;

    tr_ptrArray * torrents;
    tr_ptrArray * scraping;
    tr_ptrArray * scrapeQueue;

    /* these are set from the latest scrape or tracker response */
    int announceIntervalSec;
    int minAnnounceIntervalSec;
    int scrapeIntervalSec;

    /* calculated when we get fewer scrapes
       back than we asked for */
    int multiscrapeMax;

    tr_tracker_info * redirect;
    tr_tracker_info * addresses;
    int addressIndex;
    int addressCount;
    int * tierFronts;

    char * primaryAddress;

    /* sent as the "key" argument in tracker requests
       to verify us if our IP address changes.
       This is immutable for the life of the tracker object. */
    char key_param[TR_KEY_LEN+1];

    tr_timer * scrapeTimer;
}
Tracker;

/* this is the Torrent struct, but since it's the pointer
   passed around in the public API of this tracker module,
   its *public* name is tr_tracker... wheee */
typedef struct tr_tracker
{
    tr_publisher_t * publisher;

    /* torrent hash string */
    uint8_t hash[SHA_DIGEST_LENGTH];
    char escaped[SHA_DIGEST_LENGTH * 3 + 1];
    char * name;

    /* corresponds to the peer_id sent as a tracker request parameter.
       OiNK's op TooMuchTime says: "When the same torrent is opened and
       closed and opened again without quitting Transmission ...
       change the peerid. It would help sometimes if a stopped event
       was missed to ensure that we didn't think someone was cheating. */
    char peer_id[TR_ID_LEN + 1];

    /* these are set from the latest scrape or tracker response...
       -1 means 'unknown' */
    int timesDownloaded;
    int seeders;
    int leechers;
    char * trackerID;

    /* the last tracker request we sent. (started, stopped, etc.)
       automatic announces are an empty string;
       NULL means no message has ever been sent */
    char * lastRequest;

    time_t manualAnnounceAllowedAt;

    Tracker * tracker;

    tr_timer * scrapeTimer;
    tr_timer * reannounceTimer;

    unsigned int isRunning : 1;
}
Torrent;

static int
trackerCompare( const void * va, const void * vb )
{
    const Tracker * a = ( const Tracker * ) va;
    const Tracker * b = ( const Tracker * ) vb;
    return strcmp( a->primaryAddress, b->primaryAddress );
}

static int
torrentCompare( const void * va, const void * vb )
{
    const Torrent * a = (const Torrent*) va;
    const Torrent * b = (const Torrent*) vb;
    return memcmp( a->hash, b->hash, SHA_DIGEST_LENGTH );
}

/***
****
***/

static int
freeConnection( void * evcon )
{
    evhttp_connection_free( evcon );
    return FALSE;
}

static void
connectionClosedCB( struct evhttp_connection * evcon, void * handle )
{
    tr_timerNew( handle, freeConnection, evcon, 100 );
}

static struct evhttp_connection*
getConnection( Tracker * tracker, const char * address, int port )
{
    struct evhttp_connection * c = evhttp_connection_new( address, port );
    evhttp_connection_set_closecb( c, connectionClosedCB, tracker->handle );
    return c;
}

/***
****  PUBLISH
***/

static const tr_tracker_event_t emptyEvent = { 0, NULL, NULL, NULL, 0 };

static void
publishMessage( Torrent * tor, const char * msg, int type )
{
    tr_tracker_event_t event = emptyEvent;
    event.hash = tor->hash;
    event.messageType = type;
    event.text = msg;
    tr_publisherPublish( tor->publisher, tor, &event );
}

static void
publishErrorClear( Torrent * tor )
{
    publishMessage( tor, NULL, TR_TRACKER_ERROR_CLEAR );
}

static void
publishErrorMessage( Torrent * tor, const char * msg )
{
    publishMessage( tor, msg, TR_TRACKER_ERROR );
}

static void
publishWarningMessage( Torrent * tor, const char * msg )
{
    publishMessage( tor, msg, TR_TRACKER_WARNING );
}

static void
publishNewPeers( Torrent * tor, int count, uint8_t * peers )
{
    tr_tracker_event_t event = emptyEvent;
    event.hash = tor->hash;
    event.messageType = TR_TRACKER_PEERS;
    event.peerCount = count;
    event.peerCompact = peers;
    tr_inf( "Torrent \"%s\" got %d new peers", tor->name, count );
    tr_publisherPublish( tor->publisher, tor, &event );
}

/***
****  LIFE CYCLE
***/

static tr_ptrArray *
getTrackerLookupTable( void )
{
    static tr_ptrArray * myTrackers = NULL;
    if( !myTrackers )
         myTrackers = tr_ptrArrayNew( );
    return myTrackers;
}

static void
generateKeyParam( char * msg, int len )
{
    int i;
    const char * pool = "abcdefghijklmnopqrstuvwxyz0123456789";
    for( i=0; i<len; ++i )
        *msg++ = pool[tr_rand(36)];
    *msg = '\0';
}

static int onTrackerScrapeNow( void* );

static void
tr_trackerScrapeSoon( Tracker * t )
{
    /* don't start more than one scrape at once for the same tracker... */
    if( !tr_ptrArrayEmpty( t->scraping ) )
        return;

    if( !t->scrapeTimer )
         t->scrapeTimer = tr_timerNew( t->handle, onTrackerScrapeNow, t, 5000 );
}

static Tracker*
getExistingTracker( const char * primaryAddress )
{
    tr_ptrArray * trackers = getTrackerLookupTable( );
    Tracker tmp;
    assert( primaryAddress && *primaryAddress );
    tmp.primaryAddress = (char*) primaryAddress;
    return tr_ptrArrayFindSorted( trackers, &tmp, trackerCompare );
}

static Tracker*
tr_trackerGet( const tr_torrent * tor )
{
    const tr_info * info = &tor->info;
    Tracker * t = getExistingTracker( info->primaryAddress );

    if( t == NULL ) /* no such tracker.... create one */
    {
        int i, j, sum, *iwalk;
        tr_tracker_info * nwalk;
        tr_dbg( "making a new tracker for \"%s\"", info->primaryAddress );

        t = tr_new0( Tracker, 1 );
        t->handle = tor->handle;
        t->primaryAddress = tr_strdup( info->primaryAddress );
        t->scrapeIntervalSec       = DEFAULT_SCRAPE_INTERVAL_SEC;
        t->announceIntervalSec     = DEFAULT_ANNOUNCE_INTERVAL_SEC;
        t->minAnnounceIntervalSec  = DEFAULT_MIN_ANNOUNCE_INTERVAL_SEC;
        t->multiscrapeMax = INT_MAX;
        t->torrents    = tr_ptrArrayNew( );
        t->scraping    = tr_ptrArrayNew( );
        t->scrapeQueue = tr_ptrArrayNew( );
        generateKeyParam( t->key_param, TR_KEY_LEN );

        for( sum=i=0; i<info->trackerTiers; ++i )
             sum += info->trackerList[i].count;
        t->addresses = nwalk = tr_new0( tr_tracker_info, sum );
        t->addressIndex = 0;
        t->addressCount = sum;
        t->tierFronts = iwalk = tr_new0( int, sum );

        for( i=0; i<info->trackerTiers; ++i )
        {
            const int tierFront = nwalk - t->addresses;

            for( j=0; j<info->trackerList[i].count; ++j )
            {
                const tr_tracker_info * src = &info->trackerList[i].list[j];
                nwalk->address = tr_strdup( src->address );
                nwalk->port = src->port;
                nwalk->announce = tr_strdup( src->announce );
                nwalk->scrape = tr_strdup( src->scrape );
                ++nwalk;

                *iwalk++ = tierFront;
            }
        }

        assert( nwalk - t->addresses == sum );
        assert( iwalk - t->tierFronts == sum );

        tr_ptrArrayInsertSorted( getTrackerLookupTable( ), t, trackerCompare );
    }

    return t;
}

static Torrent *
getExistingTorrent( Tracker * t, const uint8_t hash[SHA_DIGEST_LENGTH] )
{
    Torrent tmp;
    memcpy( tmp.hash, hash, SHA_DIGEST_LENGTH );
    return tr_ptrArrayFindSorted( t->torrents, &tmp, torrentCompare );
}

static void
escape( char * out, const uint8_t * in, int in_len ) /* rfc2396 */
{
    const uint8_t *end = in + in_len;
    while( in != end )
        if( isalnum(*in) )
            *out++ = (char) *in++;
        else 
            out += snprintf( out, 4, "%%%02X", (unsigned int)*in++ );
    *out = '\0';
}

static void
onTorrentFreeNow( void * vtor )
{
    Torrent * tor = (Torrent *) vtor;
    Tracker * t = tor->tracker;

    tr_ptrArrayRemoveSorted( t->torrents, tor, torrentCompare );
    tr_ptrArrayRemoveSorted( t->scrapeQueue, tor, torrentCompare );
    tr_ptrArrayRemoveSorted( t->scraping, tor, torrentCompare );

    tr_timerFree( &tor->scrapeTimer );
    tr_timerFree( &tor->reannounceTimer );
    tr_publisherFree( &tor->publisher );
    tr_free( tor->name );
    tr_free( tor->trackerID );
    tr_free( tor->lastRequest );
    tr_free( tor );

    if( tr_ptrArrayEmpty( t->torrents ) ) /* last one.. free the tracker too */
    {
        int i;
        tr_ptrArrayRemoveSorted( getTrackerLookupTable( ), t, trackerCompare );

        tr_ptrArrayFree( t->torrents, NULL );
        tr_ptrArrayFree( t->scrapeQueue, NULL );
        tr_ptrArrayFree( t->scraping, NULL );

        for( i=0; i<t->addressCount; ++i )
            tr_trackerInfoClear( &t->addresses[i] );

        if( t->redirect ) {
            tr_trackerInfoClear( t->redirect );
            tr_free( t->redirect );
        }

        tr_timerFree( &t->scrapeTimer );

        tr_free( t->primaryAddress );
        tr_free( t->addresses );
        tr_free( t->tierFronts );
        tr_free( t );
    }
}

void
tr_trackerFree( Torrent * tor )
{
    tr_runInEventThread( tor->tracker->handle, onTorrentFreeNow, tor );
}

Torrent*
tr_trackerNew( tr_torrent * torrent )
{
    Torrent * tor;
    Tracker * t = tr_trackerGet( torrent );
    assert( getExistingTorrent( t, torrent->info.hash ) == NULL );

    /* create a new Torrent and queue it for scraping */
    tor = tr_new0( Torrent, 1 );
    tor->publisher = tr_publisherNew( );
    tor->tracker = t;
    /*tor->torrent = torrent;*/
    tor->timesDownloaded = -1;
    tor->seeders = -1;
    tor->leechers = -1;
    tor->manualAnnounceAllowedAt = ~0;
    tor->name = tr_strdup( torrent->info.name );
    memcpy( tor->hash, torrent->info.hash, SHA_DIGEST_LENGTH );
    escape( tor->escaped, torrent->info.hash, SHA_DIGEST_LENGTH );
    tr_ptrArrayInsertSorted( t->torrents, tor, torrentCompare );
    tr_ptrArrayInsertSorted( t->scrapeQueue, tor, torrentCompare );
    tr_trackerScrapeSoon( t );
    return tor;
}

/***
****  UTIL
***/

static int
parseBencResponse( struct evhttp_request * req, benc_val_t * setme )
{
    const unsigned char * body = EVBUFFER_DATA( req->input_buffer );
    const int bodylen = EVBUFFER_LENGTH( req->input_buffer );
    int ret = 1;
    int i;

    for( i=0; ret && i<bodylen; ++i )
        if( !tr_bencLoad( body+i, bodylen-1, setme, NULL ) )
            ret = 0;
    return ret;
}

static char*
updateAddresses( Tracker * t, const struct evhttp_request * req )
{
    char * ret = NULL;
    int moveToNextAddress = FALSE;

    if( !req )
    {
        ret = tr_strdup( "No response from tracker -- will keep trying." );
        tr_inf( ret );

        moveToNextAddress = TRUE;
    }
    else if( req->response_code == HTTP_OK )
    {
        if( t->redirect != NULL )
        {
            /* multitracker spec: "if a connection with a tracker is
               successful, it will be moved to the front of the tier." */
            const int i = t->addressIndex;
            const int j = t->tierFronts[i];
            const tr_tracker_info swap = t->addresses[i];
            t->addresses[i] = t->addresses[j];
            t->addresses[j] = swap;
        }
    }
    else if(    ( req->response_code == HTTP_MOVEPERM )
             || ( req->response_code == HTTP_MOVETEMP ) )
    {
        const char * loc = evhttp_find_header( req->input_headers, "Location" );
        tr_tracker_info tmp;
        if( tr_trackerInfoInit( &tmp, loc, -1 ) ) /* a bad redirect? */
        {
            moveToNextAddress = TRUE;
        }
        else if( req->response_code == HTTP_MOVEPERM )
        {
            tr_tracker_info * cur = &t->addresses[t->addressIndex];
            tr_trackerInfoClear( cur );
            *cur = tmp;
        }
        else if( req->response_code == HTTP_MOVETEMP )
        {
            if( t->redirect == NULL )
                t->redirect = tr_new0( tr_tracker_info, 1 );
            else
                tr_trackerInfoClear( t->redirect );
            *t->redirect = tmp;
        }
    }
    else 
    {
        ret = tr_strdup( "No response from tracker -- will keep trying." );
        moveToNextAddress = TRUE;
    }

    if( moveToNextAddress )
        if ( ++t->addressIndex >= t->addressCount )
            t->addressIndex = 0;

    return ret;
}

static tr_tracker_info *
getCurrentAddress( const Tracker * t )
{
    assert( t->addresses != NULL );
    assert( t->addressIndex >= 0 );
    assert( t->addressIndex < t->addressCount );

    return &t->addresses[t->addressIndex];
}
static int
trackerSupportsScrape( const Tracker * t )
{
    const tr_tracker_info * info = getCurrentAddress( t );

    return ( info != NULL )
        && ( info->scrape != NULL )
        && ( info->scrape[0] != '\0' );
}


static void
addCommonHeaders( const Tracker * t,
                  struct evhttp_request * req )
{
    char buf[1024];
    tr_tracker_info * address = getCurrentAddress( t );
    snprintf( buf, sizeof(buf), "%s:%d", address->address, address->port );
    evhttp_add_header( req->output_headers, "Host", buf );
    evhttp_add_header( req->output_headers, "Connection", "close" );
    evhttp_add_header( req->output_headers, "Content-Length", "0" );
    evhttp_add_header( req->output_headers, "User-Agent",
                                         TR_NAME "/" LONG_VERSION_STRING );
}

/**
***
**/

struct torrent_hash
{
    tr_handle * handle;
    uint8_t hash[SHA_DIGEST_LENGTH];
};

static struct torrent_hash*
torrentHashNew( Torrent * tor )
{
    struct torrent_hash * data = tr_new( struct torrent_hash, 1 );
    data->handle = tor->tracker->handle;
    memcpy( data->hash, tor->hash, SHA_DIGEST_LENGTH );
    return data;
}

tr_torrent*
findTorrentFromHash( struct torrent_hash * data )
{
    return tr_torrentFindFromHash( data->handle, data->hash );
}

/***
****
****  SCRAPE
****
***/

static int
onTorrentScrapeNow( void * vhash )
{
    tr_torrent * torrent = findTorrentFromHash( vhash );
    tr_free( vhash );

    if( torrent != NULL )
    {
        Torrent * tor = torrent->tracker;
        if( trackerSupportsScrape( tor->tracker ) )
        {
            if( tr_ptrArrayFindSorted( tor->tracker->scrapeQueue, tor, torrentCompare) == NULL )
                tr_ptrArrayInsertSorted( tor->tracker->scrapeQueue, tor, torrentCompare );
            tr_trackerScrapeSoon( tor->tracker );
        }
        tor->scrapeTimer = NULL;
    }
    return FALSE;
}

static void
onScrapeResponse( struct evhttp_request * req, void * primaryAddress )
{
    char * errmsg;
    Tracker * t;

    tr_inf( "Got scrape response from  '%s': %s",
            primaryAddress,
            ( ( req && req->response_code_line ) ? req->response_code_line : "(null)") );

    t = getExistingTracker( primaryAddress );
    tr_free( primaryAddress );
    if( t == NULL ) /* tracker has been closed... */
        return;

    if( req && ( req->response_code == HTTP_OK ) )
    {
        int numResponses = 0;
        benc_val_t benc, *files;
        const int n_scraping = tr_ptrArraySize( t->scraping );
        const int bencLoaded = !parseBencResponse( req, &benc );

        if( bencLoaded
            && (( files = tr_bencDictFind( &benc, "files" ) ))
            && ( files->type == TYPE_DICT ) )
        {
            int i;
            for( i=0; i<files->val.l.count; i+=2 )
            {
                const uint8_t* hash =
                    (const uint8_t*) files->val.l.vals[i].val.s.s;
                benc_val_t *tmp, *flags;
                benc_val_t *tordict = &files->val.l.vals[i+1];
                Torrent * tor = getExistingTorrent( t, hash );
                ++numResponses;
                   
                if( !tor )
                    continue;

                publishErrorClear( tor );

                if(( tmp = tr_bencDictFind( tordict, "complete" )))
                    tor->seeders = tmp->val.i;

                if(( tmp = tr_bencDictFind( tordict, "incomplete" )))
                    tor->leechers = tmp->val.i;

                if(( tmp = tr_bencDictFind( tordict, "downloaded" )))
                    tor->timesDownloaded = tmp->val.i;

                if(( flags = tr_bencDictFind( tordict, "flags" )))
                    if(( tmp = tr_bencDictFind( flags, "min_request_interval")))
                        t->scrapeIntervalSec = tmp->val.i;

                tr_ptrArrayRemoveSorted( t->scraping, tor, torrentCompare );

                tr_timerFree( &tor->scrapeTimer );
                tor->scrapeTimer = tr_timerNew( t->handle, onTorrentScrapeNow, torrentHashNew(tor), t->scrapeIntervalSec*1000 );
                tr_dbg( "Torrent '%s' scrape successful."
                        "  Rescraping in %d seconds",
                        tor->name, t->scrapeIntervalSec );
            }

            if( !files->val.l.count )
            {
                /* got an empty files dictionary!  This probably means the
                   torrents we're scraping have expired from the tracker,
                   so make sure they're stopped.  It also means any previous
                   changes to multiscrapeMax are suspect, so reset that. */

                int n;
                Torrent ** torrents = (Torrent**)
                    tr_ptrArrayPeek( t->scraping, &n );
                for( i=0; i<n; ++i )
                    tr_trackerStop( torrents[i] );
                tr_ptrArrayClear( t->scraping );

                t->multiscrapeMax = INT_MAX;
            }
        }

        if( bencLoaded )
            tr_bencFree( &benc );

        /* if the tracker gave us back fewer torrents than we
           thought we should get, maybe our multiscrape string
           is too big... limit it based on how many we got back */
        if( ( 0 < numResponses ) && ( numResponses < n_scraping ) )
            t->multiscrapeMax = numResponses;
    }

    if (( errmsg = updateAddresses( t, req ) ))
        tr_err( errmsg );

    if( !tr_ptrArrayEmpty( t->scraping ) )
    {
        int i, n;
        Torrent ** torrents =
            (Torrent**) tr_ptrArrayPeek( t->scraping, &n );
        for( i=0; i<n; ++i ) {
            if( errmsg != NULL )
                publishErrorMessage( torrents[i], errmsg );
            onTorrentScrapeNow( torrentHashNew(torrents[i]) );
        }
        tr_ptrArrayClear( t->scraping );
    }
    tr_free( errmsg );

    if( !tr_ptrArrayEmpty( t->scrapeQueue ) )
        tr_trackerScrapeSoon( t );
}

static int
onTrackerScrapeNow( void * vt )
{
    Tracker * t = (Tracker*) vt;
    const tr_tracker_info * address = getCurrentAddress( t );

    assert( tr_ptrArrayEmpty( t->scraping ) );

    if( trackerSupportsScrape( t ) && !tr_ptrArrayEmpty( t->scrapeQueue ) )
    {
        int i, n, len, addr_len, ask_n;
        char *march, *uri;
        Torrent ** torrents =
            (Torrent**) tr_ptrArrayPeek( t->scrapeQueue, &n );
        struct evhttp_connection * evcon;
        struct evhttp_request *req;

        ask_n = n;
        if( ask_n > t->multiscrapeMax )
            ask_n = t->multiscrapeMax;

        /**
        ***  Build the scrape request
        **/

        len = addr_len = strlen( address->scrape );
        for( i=0; i<ask_n; ++i )
            len += strlen("&info_hash=") + strlen(torrents[i]->escaped);
        ++len; /* for nul */
        uri = march = tr_new( char, len );
        memcpy( march, address->scrape, addr_len ); march += addr_len;
        for( i=0; i<ask_n; ++i ) {
            const int elen = strlen( torrents[i]->escaped );
            *march++ = i?'&':'?';
            memcpy( march, "info_hash=", 10); march += 10;
            memcpy( march, torrents[i]->escaped, elen ); march += elen;
        }
        *march++ = '\0';
        assert( march - uri == len );

        /* move the first n_ask torrents from scrapeQueue to scraping */
        for( i=0; i<ask_n; ++i )
            tr_ptrArrayInsertSorted( t->scraping, torrents[i], torrentCompare );
        tr_ptrArrayErase( t->scrapeQueue, 0, ask_n );

        /* ping the tracker */
        tr_inf( "Sending scrape to tracker %s:%d: %s",
                address->address, address->port, uri );
        evcon = getConnection( t, address->address, address->port );
        evhttp_connection_set_timeout( evcon, TIMEOUT_INTERVAL_SEC );
        req = evhttp_request_new( onScrapeResponse, tr_strdup(t->primaryAddress) );
        assert( req );
        addCommonHeaders( t, req );
        tr_evhttp_make_request( t->handle, evcon, req, EVHTTP_REQ_GET, uri );
    }

    t->scrapeTimer = NULL;
    return FALSE;
}

/***
****
****  TRACKER REQUESTS
****
***/

static int
torrentIsRunning( const Torrent * tor )
{
    return tor && tor->isRunning;
}

static char*
buildTrackerRequestURI( const Torrent     * tor,
                        const tr_torrent  * torrent,
                        const char        * eventName )
{
    const int isStopping = !strcmp( eventName, "stopped" );
    const int numwant = isStopping ? 0 : NUMWANT;
    char buf[4096];

    snprintf( buf, sizeof(buf), "%s"
                                "?info_hash=%s"
                                "&peer_id=%s"
                                "&port=%d"
                                "&uploaded=%"PRIu64
                                "&downloaded=%"PRIu64
                                "&corrupt=%"PRIu64
                                "&left=%"PRIu64
                                "&compact=1"
                                "&numwant=%d"
                                "&key=%s"
                                "%s%s"
                                "%s%s",
        getCurrentAddress(tor->tracker)->announce,
        tor->escaped,
        tor->peer_id,
        tr_sharedGetPublicPort( torrent->handle->shared ),
        torrent->uploadedCur,
        torrent->downloadedCur,
        torrent->corruptCur,
        tr_cpLeftUntilComplete( torrent->completion ),
        numwant,
        tor->tracker->key_param,
        ( ( eventName && *eventName ) ? "&event=" : "" ),
        ( ( eventName && *eventName ) ? eventName : "" ),
        ( ( tor->trackerID && *tor->trackerID ) ? "&trackerid=" : "" ),
        ( ( tor->trackerID && *tor->trackerID ) ? tor->trackerID : "" ) );

    return tr_strdup( buf );
}

/* Convert to compact form */
static uint8_t *
parseOldPeers( benc_val_t * bePeers, int * peerCount )
{
    int i, count;
    uint8_t * compact;

    assert( bePeers->type == TYPE_LIST );

    compact = tr_new( uint8_t, 6 * bePeers->val.l.count );

    for( i=count=0; i<bePeers->val.l.count; ++i )
    {
        struct in_addr addr;
        tr_port_t port;
        benc_val_t * val;
        benc_val_t * peer = &bePeers->val.l.vals[i];

        val = tr_bencDictFind( peer, "ip" );
        if( !val || val->type!=TYPE_STR || tr_netResolve(val->val.s.s, &addr) )
            continue;

        memcpy( &compact[6 * count], &addr, 4 );

        val = tr_bencDictFind( peer, "port" );
        if( !val || val->type!=TYPE_INT || val->val.i<0 || val->val.i>0xffff )
            continue;

        port = htons( val->val.i );
        memcpy( &compact[6 * count + 4], &port, 2 );
        ++count;
    }

    *peerCount = count;
    return compact;
}

/* handle braindead trackers whose minimums is higher
   than the interval. */
static void
setAnnounceInterval( Tracker  * t,
                     int        minimum,
                     int        interval )
{
    assert( t != NULL );

    if( minimum > 0 )
        t->minAnnounceIntervalSec = minimum;

    if( interval > 0 )
        t->announceIntervalSec = interval;

    if( t->announceIntervalSec < t->minAnnounceIntervalSec )
        t->announceIntervalSec = t->minAnnounceIntervalSec;
}

static int onReannounceNow( void * vtor );

static void
onStoppedResponse( struct evhttp_request * req UNUSED, void * handle UNUSED )
{
}

static int
getManualReannounceIntervalSecs( const Tracker * tracker )
{
    return tracker
        ? MAX( tracker->minAnnounceIntervalSec, DEFAULT_MANUAL_ANNOUNCE_INTERVAL_SEC )
        : DEFAULT_MANUAL_ANNOUNCE_INTERVAL_SEC;
}

static void
onTrackerResponse( struct evhttp_request * req, void * torrent_hash )
{
    char * errmsg;
    Torrent * tor;
    int isStopped;
    int reannounceIntervalSecs;
    tr_torrent * t;

    t = findTorrentFromHash( torrent_hash );
    tr_free( torrent_hash );
    if( t == NULL ) /* torrent has been closed */
        return;

    tor = t->tracker;
    isStopped = !torrentIsRunning( tor );

    tr_inf( "Torrent \"%s\" tracker response: %s",
            tor->name,
            ( req ? req->response_code_line : "(null)") );

    if( req && ( req->response_code == HTTP_OK ) )
    {
        benc_val_t benc;
        const int bencLoaded = !parseBencResponse( req, &benc );

        publishErrorClear( tor );

        if( bencLoaded && benc.type==TYPE_DICT )
        {
            benc_val_t * tmp;

            if(( tmp = tr_bencDictFind( &benc, "failure reason" )))
                publishErrorMessage( tor, tmp->val.s.s );

            if(( tmp = tr_bencDictFind( &benc, "warning message" )))
                publishWarningMessage( tor, tmp->val.s.s );

            if(( tmp = tr_bencDictFind( &benc, "interval" )))
                setAnnounceInterval( tor->tracker, -1, tmp->val.i * 1000 );

            if(( tmp = tr_bencDictFind( &benc, "min interval" )))
                setAnnounceInterval( tor->tracker, tmp->val.i * 1000, -1 );

            if(( tmp = tr_bencDictFind( &benc, "tracker id" )))
                tor->trackerID = tr_strndup( tmp->val.s.s, tmp->val.s.i );

            if(( tmp = tr_bencDictFind( &benc, "complete" )))
                tor->seeders = tmp->val.i;

            if(( tmp = tr_bencDictFind( &benc, "incomplete" )))
                tor->leechers = tmp->val.i;

            if(( tmp = tr_bencDictFind( &benc, "peers" )))
            {
                int peerCount = 0;
                uint8_t * peerCompact = NULL;

                if( tmp->type == TYPE_LIST ) /* original protocol */
                {
                    if( tmp->val.l.count > 0 )
                        peerCompact = parseOldPeers( tmp, &peerCount );
                }
                else if( tmp->type == TYPE_STR ) /* "compact" extension */
                {
                    if( tmp->val.s.i >= 6 )
                    {
                        peerCount = tmp->val.s.i / 6;
                        peerCompact = tr_new( uint8_t, tmp->val.s.i );
                        memcpy( peerCompact, tmp->val.s.s, tmp->val.s.i );
                    }
                }

                publishNewPeers( tor, peerCount, peerCompact );
                tr_free( peerCompact );
            }
        }

        reannounceIntervalSecs = isStopped
            ? -1
            : tor->tracker->announceIntervalSec;

        if( bencLoaded )
            tr_bencFree( &benc );
    }
    else
    {
        tr_inf( "Bad response from tracker '%s' on request '%s' "
                "for torrent '%s'... trying again in 30 seconds",
                tor->tracker->primaryAddress,
                tor->lastRequest,
                tor->name );

        reannounceIntervalSecs = 30;
    }

    if (( errmsg = updateAddresses( tor->tracker, req ) )) {
        publishErrorMessage( tor, errmsg );
        tr_err( errmsg );
        tr_free( errmsg );
    }

    if( !isStopped && reannounceIntervalSecs>0 ) {
        tr_dbg( "torrent '%s' reannouncing in %d seconds",
                tor->name, (reannounceIntervalSecs) );
        tr_timerFree( &tor->reannounceTimer );
        tor->reannounceTimer = tr_timerNew( tor->tracker->handle, onReannounceNow, tor, reannounceIntervalSecs*1000 );
        tor->manualAnnounceAllowedAt = time(NULL) + getManualReannounceIntervalSecs( tor->tracker );
    }
}

static int
sendTrackerRequest( void * vt, const char * eventName )
{
    Torrent * t = (Torrent *) vt;
    const int isStopping = eventName && !strcmp( eventName, "stopped" );
    const tr_tracker_info * address = getCurrentAddress( t->tracker );
    char * uri;
    struct evhttp_connection * evcon;
    const tr_torrent * tor;

    tor = tr_torrentFindFromHash( t->tracker->handle, t->hash );
    if( tor == NULL )
        return FALSE;    

    uri = buildTrackerRequestURI( t, tor, eventName );

    tr_inf( "Torrent \"%s\" sending '%s' to tracker %s:%d: %s",
            t->name,
            (eventName ? eventName : "periodic announce"),
            address->address, address->port,
            uri );

    /* kill any pending requests */
    tr_timerFree( &t->reannounceTimer );

    evcon = getConnection( t->tracker, address->address, address->port );
    if ( !evcon ) {
        tr_err( "Can't make a connection to %s:%d", address->address, address->port );
        tr_free( uri );
    } else {
        struct evhttp_request * httpReq;
        tr_free( t->lastRequest );
        t->lastRequest = tr_strdup( eventName );
        if( isStopping ) {
            evhttp_connection_set_timeout( evcon, STOPPING_TIMEOUT_INTERVAL_SEC );
            httpReq = evhttp_request_new( onStoppedResponse, t->tracker->handle );
        } else {
            evhttp_connection_set_timeout( evcon, TIMEOUT_INTERVAL_SEC );
            httpReq = evhttp_request_new( onTrackerResponse, torrentHashNew(t) );
        }
        addCommonHeaders( t->tracker, httpReq );
        tr_evhttp_make_request( t->tracker->handle, evcon,
                                httpReq, EVHTTP_REQ_GET, uri );
    }

    return FALSE;
}

static int
onReannounceNow( void * vtor )
{
    Torrent * tor = (Torrent *) vtor;
    sendTrackerRequest( tor, "" );
    tor->reannounceTimer = NULL;
    return FALSE;
}

/***
****  PUBLIC
***/

tr_publisher_tag
tr_trackerSubscribe( Torrent             * tor,
                     tr_delivery_func      func,
                     void                * user_data )
{
    return tr_publisherSubscribe( tor->publisher, func, user_data );
}

void
tr_trackerUnsubscribe( Torrent           * tor,
                       tr_publisher_tag    tag )
{
    tr_publisherUnsubscribe( tor->publisher, tag );
}

const tr_tracker_info *
tr_trackerGetAddress( const Torrent * tor )
{
    return getCurrentAddress( tor->tracker );
}

int
tr_trackerCanManualAnnounce ( const Torrent * tor )
{
    /* return true if this torrent's currently running
       and it's been long enough since the last announce */
    return ( torrentIsRunning( tor ) )
        && ( time(NULL) >= tor->manualAnnounceAllowedAt );
}

void
tr_trackerGetCounts( const Torrent       * tor,
                     int                 * setme_completedCount,
                     int                 * setme_leecherCount,
                     int                 * setme_seederCount )
{
    if( setme_completedCount )
       *setme_completedCount = tor->timesDownloaded;

    if( setme_leecherCount )
       *setme_leecherCount = tor->leechers;

    if( setme_seederCount )
       *setme_seederCount = tor->seeders;
}

void
tr_trackerStart( Torrent * tor )
{
    tr_peerIdNew( tor->peer_id, sizeof(tor->peer_id) );

    if( !tor->reannounceTimer && !tor->isRunning )
    {
        tor->isRunning = 1;
        sendTrackerRequest( tor, "started" );
    }
}

void
tr_trackerReannounce( Torrent * tor )
{
    sendTrackerRequest( tor, "started" );
}

void
tr_trackerCompleted( Torrent * tor )
{
    sendTrackerRequest( tor, "completed" );
}

void
tr_trackerStop( Torrent * tor )
{
    if( tor->isRunning )
    {
        tor->isRunning = 0;
        sendTrackerRequest( tor, "stopped" );
    }
}

void
tr_trackerChangeMyPort( Torrent * tor )
{
    if( torrentIsRunning( tor ) )
        tr_trackerReannounce( tor );
}
