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

#include <assert.h>
#include <stdio.h> /* snprintf */
#include <stdlib.h>
#include <string.h> /* strcmp, strchr */
#include <libgen.h> /* basename */

#include <event.h>
#include <evhttp.h>

#include "transmission.h"
#include "bencode.h"
#include "completion.h"
#include "list.h"
#include "net.h"
#include "port-forwarding.h"
#include "publish.h"
#include "torrent.h"
#include "tracker.h"
#include "trcompat.h" /* strlcpy */
#include "trevent.h"
#include "utils.h"

enum
{
    /* seconds between tracker pulses */
    PULSE_INTERVAL_MSEC = 1000,

    /* maximum number of concurrent tracker socket connections */
    MAX_TRACKER_SOCKETS = 16,

    /* maximum number of concurrent tracker socket connections during shutdown.
     * all the peer connections should be gone by now, so we can hog more 
     * connections to send `stop' messages to the trackers */
    MAX_TRACKER_SOCKETS_DURING_SHUTDOWN = 64,

    /* unless the tracker says otherwise, rescrape this frequently */
    DEFAULT_SCRAPE_INTERVAL_SEC = (60 * 15),

    /* unless the tracker says otherwise, this is the announce interval */
    DEFAULT_ANNOUNCE_INTERVAL_SEC = (60 * 4),

    /* unless the tracker says otherwise, this is the announce min_interval */
    DEFAULT_ANNOUNCE_MIN_INTERVAL_SEC = (60 * 2),

    /* this is how long we'll leave a request hanging before timeout */
    TIMEOUT_INTERVAL_SEC = 30,

    /* this is how long we'll leave a 'stop' request hanging before timeout.
       we wait less time for this so it doesn't slow down shutdowns */
    STOP_TIMEOUT_INTERVAL_SEC = 5,

    /* the value of the 'numwant' argument passed in tracker requests. */
    NUMWANT = 200,

    /* the length of the 'key' argument passed in tracker requests */
    KEYLEN = 10
};

/**
***
**/

struct tr_tracker
{
    tr_handle * handle;

    /* these are set from the latest scrape or tracker response */
    int announceIntervalSec;
    int announceMinIntervalSec;
    int scrapeIntervalSec;
    int retryScrapeIntervalSec;

    tr_tracker_info * redirect;
    tr_tracker_info * addresses;
    int addressIndex;
    int addressCount;
    int * tierFronts;

    /* sent as the "key" argument in tracker requests
       to verify us if our IP address changes.
       This is immutable for the life of the tracker object. */
    char key_param[KEYLEN+1];

    tr_publisher_t * publisher;

    /* torrent hash string */
    uint8_t hash[SHA_DIGEST_LENGTH];
    char escaped[SHA_DIGEST_LENGTH * 3 + 1];
    char * name;

    /* corresponds to the peer_id sent as a tracker request parameter.
       one tracker admin says: "When the same torrent is opened and
       closed and opened again without quitting Transmission ...
       change the peerid. It would help sometimes if a stopped event
       was missed to ensure that we didn't think someone was cheating. */
    uint8_t * peer_id;

    /* these are set from the latest tracker response... -1 is 'unknown' */
    int timesDownloaded;
    int seederCount;
    int leecherCount;
    char * trackerID;

    time_t manualAnnounceAllowedAt;
    time_t reannounceAt;
    time_t scrapeAt;

    time_t lastScrapeTime;
    char lastScrapeResponse[512];

    time_t lastAnnounceTime;
    char lastAnnounceResponse[512];

    int randOffset;

    unsigned int isRunning     : 1;
};

/**
***
**/

static void
myDebug( const char * file, int line, const tr_tracker * t, const char * fmt, ... )
{   
    FILE * fp = tr_getLog( );
    if( fp != NULL )
    {
        va_list args;
        char timestr[64];
        struct evbuffer * buf = evbuffer_new( );
        char * myfile = tr_strdup( file );

        evbuffer_add_printf( buf, "[%s] ", tr_getLogTimeStr( timestr, sizeof(timestr) ) );
        if( t != NULL )
            evbuffer_add_printf( buf, "%s ", t->name );
        va_start( args, fmt );
        evbuffer_add_vprintf( buf, fmt, args );
        va_end( args );
        evbuffer_add_printf( buf, " (%s:%d)\n", basename(myfile), line );
        fwrite( EVBUFFER_DATA(buf), 1, EVBUFFER_LENGTH(buf), fp );

        tr_free( myfile );
        evbuffer_free( buf );
    }
}

#define dbgmsg(t, fmt...) myDebug(__FILE__, __LINE__, t, ##fmt )

/***
****
***/

static const tr_tracker_info *
getCurrentAddress( const tr_tracker * t )
{
    assert( t->addresses != NULL );
    assert( t->addressIndex >= 0 );
    assert( t->addressIndex < t->addressCount );

    return t->redirect ? t->redirect
                       : t->addresses + t->addressIndex;
}

static int
trackerSupportsScrape( const tr_tracker * t )
{
    const tr_tracker_info * info = getCurrentAddress( t );

    return ( info != NULL )
        && ( info->scrape != NULL )
        && ( info->scrape[0] != '\0' );
}

/***
****
***/

struct torrent_hash
{
    tr_handle * handle;
    uint8_t hash[SHA_DIGEST_LENGTH];
};

static struct torrent_hash*
torrentHashNew( tr_handle * handle, const tr_tracker * t )
{
    struct torrent_hash * data = tr_new( struct torrent_hash, 1 );
    data->handle = handle;
    memcpy( data->hash, t->hash, SHA_DIGEST_LENGTH );
    return data;
}

tr_tracker *
findTrackerFromHash( struct torrent_hash * data )
{
    tr_torrent * torrent = tr_torrentFindFromHash( data->handle, data->hash );
    return torrent ? torrent->tracker : NULL;
}

tr_tracker *
findTracker( tr_handle * handle, const uint8_t * hash )
{
    tr_torrent * torrent = tr_torrentFindFromHash( handle, hash );
    return torrent ? torrent->tracker : NULL;
}

/***
****  PUBLISH
***/

static const tr_tracker_event emptyEvent = { 0, NULL, NULL, NULL, 0, 0 };

static void
publishMessage( tr_tracker * t, const char * msg, int type )
{
    if( t != NULL )
    {
        tr_tracker_event event = emptyEvent;
        event.hash = t->hash;
        event.messageType = type;
        event.text = msg;
        tr_publisherPublish( t->publisher, t, &event );
    }
}

static void
publishErrorClear( tr_tracker * t )
{
    publishMessage( t, NULL, TR_TRACKER_ERROR_CLEAR );
}

static void
publishErrorMessageAndStop( tr_tracker * t, const char * msg )
{
    t->isRunning = 0;
    publishMessage( t, msg, TR_TRACKER_ERROR );
}

static void
publishWarning( tr_tracker * t, const char * msg )
{
    publishMessage( t, msg, TR_TRACKER_WARNING );
}

static void
publishNewPeers( tr_tracker * t, int allAreSeeds, void * compact, int compactLen )
{
    tr_tracker_event event = emptyEvent;
    event.hash = t->hash;
    event.messageType = TR_TRACKER_PEERS;
    event.allAreSeeds = allAreSeeds;
    event.compact = compact;
    event.compactLen = compactLen;
    if( compactLen )
        tr_publisherPublish( t->publisher, t, &event );
}

/***
****
***/

static void onReqDone( tr_handle * handle );

static void
onStoppedResponse( struct evhttp_request * req UNUSED, void * handle )
{
    dbgmsg( NULL, "got a response to some `stop' message" );
    onReqDone( handle );
}

static int
parseBencResponse( struct evhttp_request * req, tr_benc * setme )
{
    const unsigned char * body = EVBUFFER_DATA( req->input_buffer );
    const int bodylen = EVBUFFER_LENGTH( req->input_buffer );
    return tr_bencLoad( body, bodylen, setme, NULL );
}

static void
updateAddresses( tr_tracker * t, const struct evhttp_request * req, int * tryAgain )
{
    int moveToNextAddress = FALSE;

    if( !req ) /* tracker didn't respond */
    {
        tr_ninf( t->name, _( "Tracker hasn't responded yet.  Retrying..." ) );
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
        moveToNextAddress = TRUE;
    }

    *tryAgain = moveToNextAddress;

    if( moveToNextAddress )
    {
        if ( ++t->addressIndex >= t->addressCount ) /* we've tried them all */
        {
            *tryAgain = FALSE;
            t->addressIndex = 0;
        }
        else
        {
            const tr_tracker_info * n = getCurrentAddress( t );
            tr_ninf( t->name, _( "Trying next tracker \"%s:%d\"" ), n->address, n->port );
        }
    }
}

/* Convert to compact form */
static uint8_t *
parseOldPeers( tr_benc * bePeers, size_t * byteCount )
{
    int i;
    uint8_t *compact, *walk;
    const int peerCount = bePeers->val.l.count;

    assert( bePeers->type == TYPE_LIST );

    compact = tr_new( uint8_t, peerCount*6 );

    for( i=0, walk=compact; i<peerCount; ++i )
    {
        struct in_addr addr;
        tr_port_t port;
        tr_benc * val;
        tr_benc * peer = &bePeers->val.l.vals[i];

        val = tr_bencDictFind( peer, "ip" );
        if( !val || val->type!=TYPE_STR || tr_netResolve(val->val.s.s, &addr) )
            continue;

        memcpy( walk, &addr, 4 );
        walk += 4;

        val = tr_bencDictFind( peer, "port" );
        if( !val || val->type!=TYPE_INT || val->val.i<0 || val->val.i>0xffff )
            continue;

        port = htons( val->val.i );
        memcpy( walk, &port, 2 );
        walk += 2;
    }

    *byteCount = peerCount * 6;
    return compact;
}

static void
onTrackerResponse( struct evhttp_request * req, void * vhash )
{
    int tryAgain;
    int responseCode;
    struct torrent_hash * torrent_hash = (struct torrent_hash*) vhash;
    tr_tracker * t = findTrackerFromHash( torrent_hash );

    onReqDone( torrent_hash->handle );
    tr_free( torrent_hash );

    if( t == NULL ) /* tracker has been closed */
        return;

    dbgmsg( t, "got response from tracker: \"%s\"",
            ( req && req->response_code_line ) ?  req->response_code_line
                                               : "(null)" );

    *t->lastAnnounceResponse = '\0';
    if( req && req->response_code_line )
        strlcpy( t->lastAnnounceResponse, req->response_code_line, sizeof( t->lastAnnounceResponse ) );

    tr_ndbg( t->name, "tracker response: %s",
             ( req ? req->response_code_line : "(null)") );

    if( req && ( req->response_code == HTTP_OK ) )
    {
        tr_benc benc;
        const int bencLoaded = !parseBencResponse( req, &benc );

        publishErrorClear( t );

        if( bencLoaded && benc.type==TYPE_DICT )
        {
            tr_benc * tmp;
            int incomplete = -1;

            if(( tmp = tr_bencDictFind( &benc, "failure reason" ))) {
                dbgmsg( t, "got failure message [%s]", tmp->val.s.s );
                publishErrorMessageAndStop( t, tmp->val.s.s );
            }

            if(( tmp = tr_bencDictFind( &benc, "warning message" ))) {
                dbgmsg( t, "got warning message [%s]", tmp->val.s.s );
                publishWarning( t, tmp->val.s.s );
            }

            if(( tmp = tr_bencDictFind( &benc, "interval" ))) {
                dbgmsg( t, "setting interval to %d", tmp->val.i );
                t->announceIntervalSec = tmp->val.i;
            }

            if(( tmp = tr_bencDictFind( &benc, "min interval" ))) {
                dbgmsg( t, "setting min interval to %d", tmp->val.i );
                t->announceMinIntervalSec = tmp->val.i;
            }

            if(( tmp = tr_bencDictFind( &benc, "tracker id" )))
                t->trackerID = tr_strndup( tmp->val.s.s, tmp->val.s.i );

            if(( tmp = tr_bencDictFind( &benc, "complete" )))
                t->seederCount = tmp->val.i;

            if(( tmp = tr_bencDictFind( &benc, "incomplete" )))
                t->leecherCount = incomplete = tmp->val.i;

            if(( tmp = tr_bencDictFind( &benc, "peers" )))
            {
                const int allAreSeeds = incomplete == 0;

                if( tmp->type == TYPE_STR ) /* "compact" extension */
                {
                    publishNewPeers( t, allAreSeeds, tmp->val.s.s, tmp->val.s.i );
                }
                else if( tmp->type == TYPE_LIST ) /* original protocol */
                {
                    size_t byteCount = 0;
                    uint8_t * compact = parseOldPeers( tmp, &byteCount );
                    publishNewPeers( t, allAreSeeds, compact, byteCount );
                    tr_free( compact );
                }
            }
        }

        if( bencLoaded )
            tr_bencFree( &benc );
    }

    updateAddresses( t, req, &tryAgain );

    /**
    ***
    **/

    if( tryAgain )
        responseCode = 300;
    else if( req )
        responseCode = req->response_code;
    else
        responseCode = 503;

    if( 200<=responseCode && responseCode<=299 )
    {
        dbgmsg( t, "request succeeded. reannouncing in %d seconds",
                   t->announceIntervalSec );
        t->reannounceAt = time( NULL ) + t->randOffset + t->announceIntervalSec;
        t->manualAnnounceAllowedAt = time( NULL ) + t->announceMinIntervalSec;
    }
    else if( 300<=responseCode && responseCode<=399 )
    {
        const int interval = 5;
        dbgmsg( t, "got a redirect. retrying in %d seconds", interval );

        /* it's a redirect... updateAddresses() has already
         * parsed the redirect, all that's left is to retry */
        t->reannounceAt = time( NULL ) + interval;
        t->manualAnnounceAllowedAt = time( NULL ) + t->announceMinIntervalSec;
    }
    else if( 400<=responseCode && responseCode<=499 )
    {
        const char * err = req && req->response_code_line
            ? req->response_code_line
            : "Unspecified 4xx error from tracker.";
        dbgmsg( t, err );

        /* The request could not be understood by the server due to
         * malformed syntax. The client SHOULD NOT repeat the
         * request without modifications. */
        publishErrorMessageAndStop( t, err );
        t->manualAnnounceAllowedAt = ~(time_t)0;
        t->reannounceAt = 0;
    }
    else if( 500<=responseCode && responseCode<=599 )
    {
        dbgmsg( t, "Got a 5xx error... retrying in one minute." );

        /* Response status codes beginning with the digit "5" indicate
         * cases in which the server is aware that it has erred or is
         * incapable of performing the request.  So we pause a bit and
         * try again. */
        if( req && req->response_code_line )
            publishWarning( t, req->response_code_line );
        t->manualAnnounceAllowedAt = ~(time_t)0;
        t->reannounceAt = time( NULL ) + 60;
    }
    else
    {
        dbgmsg( t, "Invalid response from tracker... retrying in two minutes." );

        /* WTF did we get?? */
        if( req && req->response_code_line )
            publishWarning( t, req->response_code_line );
        t->manualAnnounceAllowedAt = ~(time_t)0;
        t->reannounceAt = time( NULL ) + t->randOffset + 120;
    }
}

static void
onScrapeResponse( struct evhttp_request * req, void * vhash )
{
    int tryAgain;
    int responseCode;
    struct torrent_hash * torrent_hash = (struct torrent_hash*) vhash;
    tr_tracker * t = findTrackerFromHash( torrent_hash );

    onReqDone( torrent_hash->handle );
    tr_free( torrent_hash );

    dbgmsg( t, "Got scrape response for '%s': %s (%d)", (t ? t->name : "(null)"), (req ? req->response_code_line : "(no line)"), (req ? req->response_code : -1) );

    if( t == NULL ) /* tracker's been closed... */
        return;

    *t->lastScrapeResponse = '\0';
    if( req && req->response_code_line )
        strlcpy( t->lastScrapeResponse, req->response_code_line, sizeof( t->lastScrapeResponse ) );

    tr_ndbg( t->name, "Got scrape response: \"%s\"",
            ( ( req && req->response_code_line ) ? req->response_code_line : "(null)") );

    if( req && ( req->response_code == HTTP_OK ) )
    {
        tr_benc benc, *files;
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
                tr_benc *tmp, *flags;
                tr_benc *tordict = &files->val.l.vals[i+1];
                if( memcmp( t->hash, hash, SHA_DIGEST_LENGTH ) )
                    continue;

                publishErrorClear( t );

                if(( tmp = tr_bencDictFind( tordict, "complete" )))
                    t->seederCount = tmp->val.i;

                if(( tmp = tr_bencDictFind( tordict, "incomplete" )))
                    t->leecherCount = tmp->val.i;

                if(( tmp = tr_bencDictFind( tordict, "downloaded" )))
                    t->timesDownloaded = tmp->val.i;

                if(( flags = tr_bencDictFind( tordict, "flags" )))
                    if(( tmp = tr_bencDictFind( flags, "min_request_interval")))
                        t->scrapeIntervalSec = tmp->val.i;

                tr_ndbg( t->name, "Scrape successful.  Rescraping in %d seconds.",
                         t->scrapeIntervalSec );

                t->retryScrapeIntervalSec = 30;
            }
        }

        if( bencLoaded )
            tr_bencFree( &benc );
    }

    updateAddresses( t, req, &tryAgain );

    /**
    ***
    **/

    if( tryAgain )
        responseCode = 300;
    else if( req )
        responseCode = req->response_code;
    else
        responseCode = 503;

    if( 200<=responseCode && responseCode<=299 )
    {
        const int interval = t->scrapeIntervalSec + t->randOffset;
        dbgmsg( t, "request succeeded. rescraping in %d seconds", interval );
        tr_ndbg( t->name, "request succeeded. rescraping in %d seconds", interval );
        t->scrapeAt = time( NULL ) + interval;
    }
    else if( 300<=responseCode && responseCode<=399 )
    {
        const int interval = 5;
        dbgmsg( t, "got a redirect. retrying in %d seconds", interval );
        t->scrapeAt = time( NULL ) + interval;
    }
    else
    {
        const int interval = t->retryScrapeIntervalSec + t->randOffset;
        dbgmsg( t, "Tracker responded to scrape with %d.  Retrying in %d seconds.", responseCode,  interval );
        t->retryScrapeIntervalSec *= 2;
        t->scrapeAt = time( NULL ) + interval;
    }
}

/***
****
***/

enum
{
    TR_REQ_STARTED,
    TR_REQ_COMPLETED,
    TR_REQ_STOPPED,
    TR_REQ_REANNOUNCE,
    TR_REQ_SCRAPE,
    TR_REQ_COUNT
};

struct tr_tracker_request
{
    int port;
    int timeout;
    int reqtype; /* TR_REQ_* */
    char * address;
    char * uri;
    struct evhttp_request * req;
    uint8_t torrent_hash[SHA_DIGEST_LENGTH];
};

static void
freeRequest( struct tr_tracker_request * req )
{
    tr_free( req->address );
    tr_free( req->uri );
    tr_free( req );
}

static void
addCommonHeaders( const tr_tracker * t,
                  struct evhttp_request * req )
{
    char buf[1024];
    const tr_tracker_info * address = getCurrentAddress( t );
    snprintf( buf, sizeof(buf), "%s:%d", address->address, address->port );
    evhttp_add_header( req->output_headers, "Host", buf );
    evhttp_add_header( req->output_headers, "Connection", "close" );
    evhttp_add_header( req->output_headers, "User-Agent",
                                         TR_NAME "/" LONG_VERSION_STRING );
}

static char*
buildTrackerRequestURI( const tr_tracker  * t,
                        const tr_torrent  * torrent,
                        const char        * eventName )
{
    const int isStopping = !strcmp( eventName, "stopped" );
    const int numwant = isStopping ? 0 : NUMWANT;
    struct evbuffer * buf = evbuffer_new( );
    char * ret;

    const char * ann = getCurrentAddress(t)->announce;
    
    evbuffer_add_printf( buf, "%s"
                              "%cinfo_hash=%s"
                              "&peer_id=%s"
                              "&port=%d"
                              "&uploaded=%"PRIu64
                              "&downloaded=%"PRIu64
                              "&corrupt=%"PRIu64
                              "&left=%"PRIu64
                              "&compact=1"
                              "&numwant=%d"
                              "&key=%s"
                              "&supportcrypto=1"
                              "&requirecrypto=%d"
                              "%s%s"
                              "%s%s",
        ann,
        strchr(ann, '?') ? '&' : '?',
        t->escaped,
        t->peer_id,
        tr_sharedGetPublicPort( t->handle->shared ),
        torrent->uploadedCur,
        torrent->downloadedCur,
        torrent->corruptCur,
        tr_cpLeftUntilComplete( torrent->completion ),
        numwant,
        t->key_param,
        ( t->handle->encryptionMode==TR_ENCRYPTION_REQUIRED ? 1 : 0 ),
        ( ( eventName && *eventName ) ? "&event=" : "" ),
        ( ( eventName && *eventName ) ? eventName : "" ),
        ( ( t->trackerID && *t->trackerID ) ? "&trackerid=" : "" ),
        ( ( t->trackerID && *t->trackerID ) ? t->trackerID : "" ) );

    ret = tr_strdup( (char*) EVBUFFER_DATA( buf ) );
    evbuffer_free( buf );
    return ret;
}

static struct tr_tracker_request*
createRequest( tr_handle * handle, const tr_tracker * tracker, int reqtype )
{
    static const char* strings[TR_REQ_COUNT] = { "started", "completed", "stopped", "", "err" };
    const tr_torrent * torrent = tr_torrentFindFromHash( handle, tracker->hash );
    const tr_tracker_info * address = getCurrentAddress( tracker );
    const int isStopping = reqtype == TR_REQ_STOPPED;
    const char * eventName = strings[reqtype];
    struct tr_tracker_request * req;

    req = tr_new0( struct tr_tracker_request, 1 );
    req->address = tr_strdup( address->address );
    req->port = address->port;
    req->uri = buildTrackerRequestURI( tracker, torrent, eventName );
    req->timeout = isStopping ? STOP_TIMEOUT_INTERVAL_SEC : TIMEOUT_INTERVAL_SEC;
    req->reqtype = reqtype;
    req->req = isStopping
        ? evhttp_request_new( onStoppedResponse, handle )
        : evhttp_request_new( onTrackerResponse, torrentHashNew(handle, tracker) );
    memcpy( req->torrent_hash, tracker->hash, SHA_DIGEST_LENGTH );
    addCommonHeaders( tracker, req->req );

    return req;
}

static struct tr_tracker_request*
createScrape( tr_handle * handle, const tr_tracker * tracker )
{
    const tr_tracker_info * a = getCurrentAddress( tracker );
    struct tr_tracker_request * req;

    req = tr_new0( struct tr_tracker_request, 1 );
    req->address = tr_strdup( a->address );
    req->port = a->port;
    req->timeout = TIMEOUT_INTERVAL_SEC;
    req->req = evhttp_request_new( onScrapeResponse, torrentHashNew( handle, tracker ) );
    req->reqtype = TR_REQ_SCRAPE;
    tr_asprintf( &req->uri, "%s%cinfo_hash=%s", a->scrape, strchr(a->scrape,'?')?'&':'?', tracker->escaped );
    memcpy( req->torrent_hash, tracker->hash, SHA_DIGEST_LENGTH );
    addCommonHeaders( tracker, req->req );

    return req;
}

struct tr_tracker_handle
{
    int socketCount;
    unsigned int isShuttingDown : 1;
    tr_timer * pulseTimer;
    tr_list * requestQueue;
    tr_list * scrapeQueue;
};

static int pulse( void * vhandle );

static void
ensureGlobalsExist( tr_handle * handle )
{
    if( handle->tracker == NULL )
    {
        handle->tracker = tr_new0( struct tr_tracker_handle, 1 );
        handle->tracker->pulseTimer = tr_timerNew( handle, pulse, handle, PULSE_INTERVAL_MSEC );
        dbgmsg( NULL, "creating tracker timer" );
    }
}

static void
freeRequest2( void * req )
{
    freeRequest( req );
}

void
tr_trackerShuttingDown( tr_handle * handle )
{
    if( handle->tracker )
    {
        /* since we're shutting down, we don't need to scrape anymore... */
        tr_list_free( &handle->tracker->scrapeQueue, freeRequest2 );

        handle->tracker->isShuttingDown = 1;
    }
}

static int
maybeFreeGlobals( tr_handle * handle )
{
    int globalsExist = handle->tracker != NULL;

    if( globalsExist
        && ( handle->tracker->socketCount < 1 )
        && ( handle->tracker->requestQueue == NULL )
        && ( handle->tracker->scrapeQueue == NULL )
        && ( handle->torrentList== NULL ) )
    {
        dbgmsg( NULL, "freeing tracker timer" );
        tr_timerFree( &handle->tracker->pulseTimer );
        tr_free( handle->tracker );
        handle->tracker = NULL;
        globalsExist = FALSE;
    }

    return globalsExist;
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
connectionClosedCB( struct evhttp_connection * evcon, void * vhandle )
{
    tr_handle * handle = vhandle;

    /* libevent references evcon right after calling this function,
       so we can't free it yet... defer it to after this call chain
       has played out */
    tr_timerNew( handle, freeConnection, evcon, 100 );
}

static struct evhttp_connection*
getConnection( tr_handle * handle, const char * address, int port )
{
    struct evhttp_connection * c = evhttp_connection_new( address, port );
    evhttp_connection_set_closecb( c, connectionClosedCB, handle );
    return c;
}

static void
invokeRequest( tr_handle * handle, const struct tr_tracker_request * req )
{
    const time_t now = time( NULL );
    struct evhttp_connection * evcon = getConnection( handle, req->address, req->port );
    tr_tracker * t = findTracker( handle, req->torrent_hash );
    dbgmsg( t, "sending '%s' to tracker %s:%d, timeout is %d", req->uri, req->address, req->port, (int)req->timeout );
    evhttp_connection_set_timeout( evcon, req->timeout );
    ++handle->tracker->socketCount;

    if( t != NULL )
    {
        if( req->reqtype == TR_REQ_SCRAPE )
        {
            t->lastScrapeTime = now;
            t->scrapeAt = 0;
        }
        else
        {
            t->lastAnnounceTime = now;
            t->reannounceAt = 0;
            t->manualAnnounceAllowedAt = 0;
        }
    }

    if( evhttp_make_request( evcon, req->req, EVHTTP_REQ_GET, req->uri ))
        (*req->req->cb)(req->req, req->req->cb_arg);
    else
        dbgmsg( t, "incremented socket count to %d", handle->tracker->socketCount );
}

static void
invokeNextInQueue( tr_handle * handle, tr_list ** list )
{
    struct tr_tracker_request * req = tr_list_pop_front( list );
    invokeRequest( handle, req );
    freeRequest( req );
}

static int
socketIsAvailable( tr_handle * handle )
{
    const int max = handle->tracker->isShuttingDown
                      ? MAX_TRACKER_SOCKETS_DURING_SHUTDOWN
                      : MAX_TRACKER_SOCKETS;
    return handle->tracker->socketCount < max;
}

static void ensureGlobalsExist( tr_handle * );

static void
enqueueScrape( tr_handle * handle, const tr_tracker * tracker )
{
    struct tr_tracker_request * req;
    ensureGlobalsExist( handle );
    req = createScrape( handle, tracker );
    tr_list_append( &handle->tracker->scrapeQueue, req );
}

static void
enqueueRequest( tr_handle * handle, const tr_tracker * tracker, int reqtype )
{
    struct tr_tracker_request * req;
    ensureGlobalsExist( handle );
    req = createRequest( handle, tracker, reqtype );
    tr_list_append( &handle->tracker->requestQueue, req );
}

static void
scrapeSoon( tr_tracker * t )
{
    if( trackerSupportsScrape( t ) )
        t->scrapeAt = time( NULL ) + t->randOffset;
}

static int
pulse( void * vhandle )
{
    tr_handle * handle = vhandle;
    struct tr_tracker_handle * th = handle->tracker;
    tr_torrent * tor;
    const time_t now = time( NULL );

    if( handle->tracker == NULL )
        return FALSE;

    if( handle->tracker->socketCount || tr_list_size(th->requestQueue) || tr_list_size(th->scrapeQueue) )
        dbgmsg( NULL, "tracker pulse... %d sockets, %d reqs left, %d scrapes left", handle->tracker->socketCount, tr_list_size(th->requestQueue), tr_list_size(th->scrapeQueue) );

    /* upkeep: queue periodic rescrape / reannounce */
    for( tor=handle->torrentList; tor; tor=tor->next )
    {
        tr_tracker * t = tor->tracker;

        if( t->scrapeAt && trackerSupportsScrape( t ) && ( now >= t->scrapeAt ) ) {
            t->scrapeAt = 0;
            enqueueScrape( handle, t );
        }

        if( t->reannounceAt && t->isRunning && ( now >= t->reannounceAt ) ) {
            t->reannounceAt = 0;
            enqueueRequest( handle, t, TR_REQ_REANNOUNCE );
        }
    }

    if( handle->tracker->socketCount || tr_list_size(th->requestQueue) || tr_list_size(th->scrapeQueue) )
        dbgmsg( NULL, "tracker pulse after upkeep... %d sockets, %d reqs left, %d scrapes left", handle->tracker->socketCount, tr_list_size(th->requestQueue), tr_list_size(th->scrapeQueue) );

    /* look for things to do... process all the requests, then process all the scrapes */
    while( th->requestQueue && socketIsAvailable( handle ) )
        invokeNextInQueue( handle, &th->requestQueue );
    while( th->scrapeQueue && socketIsAvailable( handle ) )
        invokeNextInQueue( handle, &th->scrapeQueue );

    if( handle->tracker->socketCount || tr_list_size(th->requestQueue) || tr_list_size(th->scrapeQueue) )
        dbgmsg( NULL, "tracker pulse done... %d sockets, %d reqs left, %d scrapes left", handle->tracker->socketCount, tr_list_size(th->requestQueue), tr_list_size(th->scrapeQueue) );

    return maybeFreeGlobals( handle );
}

static void
onReqDone( tr_handle * handle )
{
    if( handle->tracker )
    {
        pulse( handle );
        --handle->tracker->socketCount;
        dbgmsg( NULL, "decrementing socket count to %d", handle->tracker->socketCount );
    }
}

/***
****  LIFE CYCLE
***/

static void
generateKeyParam( char * msg, int len )
{
    int i;
    const char * pool = "abcdefghijklmnopqrstuvwxyz0123456789";
    const int poolSize = strlen( pool );
    for( i=0; i<len; ++i )
        *msg++ = pool[tr_rand(poolSize)];
    *msg = '\0';
}

static int
is_rfc2396_alnum( char ch )
{
    return ( (ch >= 'a' && ch <= 'z' )
            || (ch >= 'A' && ch <= 'Z' )
            || (ch >= '0' && ch <= '9' ) );
}

static void
escape( char * out, const uint8_t * in, int in_len ) /* rfc2396 */
{
    const uint8_t *end = in + in_len;
    while( in != end )
        if( is_rfc2396_alnum(*in) )
            *out++ = (char) *in++;
        else 
            out += snprintf( out, 4, "%%%02X", (unsigned int)*in++ );
    *out = '\0';
}

tr_tracker *
tr_trackerNew( const tr_torrent * torrent )
{
    const tr_info * info = &torrent->info;
    int i, j, sum, *iwalk;
    tr_tracker_info * nwalk;
    tr_tracker * t;

    t = tr_new0( tr_tracker, 1 );
    t->handle = torrent->handle;
    t->scrapeIntervalSec       = DEFAULT_SCRAPE_INTERVAL_SEC;
    t->retryScrapeIntervalSec  = 60;
    t->announceIntervalSec     = DEFAULT_ANNOUNCE_INTERVAL_SEC;
    t->announceMinIntervalSec  = DEFAULT_ANNOUNCE_MIN_INTERVAL_SEC;
    generateKeyParam( t->key_param, KEYLEN );

    t->publisher = tr_publisherNew( );
    t->timesDownloaded = -1;
    t->seederCount = -1;
    t->leecherCount = -1;
    t->manualAnnounceAllowedAt = ~(time_t)0;
    t->name = tr_strdup( info->name );
    t->randOffset = tr_rand( 60 );
    memcpy( t->hash, info->hash, SHA_DIGEST_LENGTH );
    escape( t->escaped, info->hash, SHA_DIGEST_LENGTH );

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

    scrapeSoon( t );

    return t;
}

static void
onTrackerFreeNow( void * vt )
{
    int i;
    tr_tracker * t = vt;

    tr_publisherFree( &t->publisher );
    tr_free( t->name );
    tr_free( t->trackerID );
    tr_free( t->peer_id );

    /* addresses... */
    for( i=0; i<t->addressCount; ++i )
        tr_trackerInfoClear( &t->addresses[i] );
    tr_free( t->addresses );
    tr_free( t->tierFronts );

    /* redirect... */
    if( t->redirect ) {
        tr_trackerInfoClear( t->redirect );
        tr_free( t->redirect );
    }

    tr_free( t );
}

/***
****  PUBLIC
***/

void
tr_trackerFree( tr_tracker * t )
{
    if( t )
        tr_runInEventThread( t->handle, onTrackerFreeNow, t );
}

tr_publisher_tag
tr_trackerSubscribe( tr_tracker          * t,
                     tr_delivery_func      func,
                     void                * user_data )
{
    return tr_publisherSubscribe( t->publisher, func, user_data );
}

void
tr_trackerUnsubscribe( tr_tracker        * t,
                       tr_publisher_tag    tag )
{
    if( t )
        tr_publisherUnsubscribe( t->publisher, tag );
}

const tr_tracker_info *
tr_trackerGetAddress( const tr_tracker   * t )
{
    return getCurrentAddress( t );
}

time_t
tr_trackerGetManualAnnounceTime( const struct tr_tracker * t )
{
    return t->isRunning ? t->manualAnnounceAllowedAt : 0;
}

int
tr_trackerCanManualAnnounce ( const tr_tracker * t)
{
    const time_t allow = tr_trackerGetManualAnnounceTime( t );
    return allow && ( allow <= time( NULL ) );
}

void
tr_trackerGetCounts( const tr_tracker  * t,
                     int               * setme_completedCount,
                     int               * setme_leecherCount,
                     int               * setme_seederCount )
{
    if( setme_completedCount )
       *setme_completedCount = t->timesDownloaded;

    if( setme_leecherCount )
       *setme_leecherCount = t->leecherCount;

    if( setme_seederCount )
       *setme_seederCount = t->seederCount;
}


void
tr_trackerStart( tr_tracker * t )
{
    if( t )
    {
        tr_free( t->peer_id );
        t->peer_id = tr_peerIdNew( );

        if( t->isRunning == 0 ) {
            t->isRunning = 1;
            enqueueRequest( t->handle, t, TR_REQ_STARTED );
        }
    }
}

void
tr_trackerReannounce( tr_tracker * t )
{
    enqueueRequest( t->handle, t, TR_REQ_REANNOUNCE );
}

void
tr_trackerCompleted( tr_tracker * t )
{
    enqueueRequest( t->handle, t, TR_REQ_COMPLETED );
}

void
tr_trackerStop( tr_tracker * t )
{
    if( t && t->isRunning ) {
        t->isRunning = 0;
        t->reannounceAt = t->manualAnnounceAllowedAt = 0;
        enqueueRequest( t->handle, t, TR_REQ_STOPPED );
    }
}

void
tr_trackerChangeMyPort( tr_tracker * t )
{
    if( t->isRunning )
        tr_trackerReannounce( t );
}

void
tr_trackerStat( const tr_tracker * t,
                struct tr_tracker_stat * setme)
{
    assert( t != NULL );
    assert( setme != NULL );

    strlcpy( setme->scrapeResponse,
             t->lastScrapeResponse,
             sizeof( setme->scrapeResponse ) );
    setme->lastScrapeTime = t->lastScrapeTime;
    setme->nextScrapeTime = t->scrapeAt;

    strlcpy( setme->announceResponse,
             t->lastAnnounceResponse,
             sizeof( setme->announceResponse ) );
    setme->lastAnnounceTime = t->lastAnnounceTime;
    setme->nextAnnounceTime = t->reannounceAt;
    setme->nextManualAnnounceTime = t->manualAnnounceAllowedAt;
}
