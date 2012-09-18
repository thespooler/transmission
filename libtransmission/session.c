/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <errno.h> /* ENOENT */
#include <stdlib.h>
#include <string.h> /* memcpy */

#include <signal.h>
#include <sys/types.h> /* stat(), umask() */
#include <sys/stat.h> /* stat(), umask() */
#include <unistd.h> /* stat */
#include <dirent.h> /* opendir */

#include <event2/dns.h> /* evdns_base_free() */
#include <event2/event.h>

#include <libutp/utp.h>

//#define TR_SHOW_DEPRECATED
#include "transmission.h"
#include "announcer.h"
#include "bandwidth.h"
#include "bencode.h"
#include "blocklist.h"
#include "cache.h"
#include "crypto.h"
#include "fdlimit.h"
#include "list.h"
#include "net.h"
#include "peer-io.h"
#include "peer-mgr.h"
#include "platform.h" /* tr_lock, tr_getTorrentDir(), tr_getFreeSpace() */
#include "port-forwarding.h"
#include "rpc-server.h"
#include "session.h"
#include "stats.h"
#include "torrent.h"
#include "tr-dht.h" /* tr_dhtUpkeep() */
#include "tr-udp.h"
#include "tr-utp.h"
#include "tr-lpd.h"
#include "trevent.h"
#include "utils.h"
#include "verify.h"
#include "version.h"
#include "web.h"

enum
{
#ifdef TR_LIGHTWEIGHT
    DEFAULT_CACHE_SIZE_MB = 2,
    DEFAULT_PREFETCH_ENABLED = false,
#else
    DEFAULT_CACHE_SIZE_MB = 4,
    DEFAULT_PREFETCH_ENABLED = true,
#endif
    SAVE_INTERVAL_SECS = 360
};


#define dbgmsg( ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            tr_deepLog( __FILE__, __LINE__, NULL, __VA_ARGS__ ); \
    } while( 0 )

static tr_port
getRandomPort( tr_session * s )
{
    return tr_cryptoWeakRandInt( s->randomPortHigh - s->randomPortLow + 1) + s->randomPortLow;
}

/* Generate a peer id : "-TRxyzb-" + 12 random alphanumeric
   characters, where x is the major version number, y is the
   minor version number, z is the maintenance number, and b
   designates beta (Azureus-style) */
void
tr_peerIdInit( uint8_t * buf )
{
    int          i;
    int          val;
    int          total = 0;
    const char * pool = "0123456789abcdefghijklmnopqrstuvwxyz";
    const int    base = 36;

    memcpy( buf, PEERID_PREFIX, 8 );

    tr_cryptoRandBuf( buf+8, 11 );
    for( i=8; i<19; ++i ) {
        val = buf[i] % base;
        total += val;
        buf[i] = pool[val];
    }

    val = total % base ? base - ( total % base ) : 0;
    buf[19] = pool[val];
    buf[20] = '\0';
}

/***
****
***/

tr_encryption_mode
tr_sessionGetEncryption( tr_session * session )
{
    assert( session );

    return session->encryptionMode;
}

void
tr_sessionSetEncryption( tr_session *       session,
                         tr_encryption_mode mode )
{
    assert( session );
    assert( mode == TR_ENCRYPTION_PREFERRED
          || mode == TR_ENCRYPTION_REQUIRED
          || mode == TR_CLEAR_PREFERRED );

    session->encryptionMode = mode;
}

/***
****
***/

struct tr_bindinfo
{
    int socket;
    tr_address addr;
    struct event * ev;
};


static void
close_bindinfo( struct tr_bindinfo * b )
{
    if( ( b != NULL ) && ( b->socket >=0 ) )
    {
        event_free( b->ev );
        b->ev = NULL;
        tr_netCloseSocket( b->socket );
    }
}

static void
close_incoming_peer_port( tr_session * session )
{
    close_bindinfo( session->public_ipv4 );
    close_bindinfo( session->public_ipv6 );
}

static void
free_incoming_peer_port( tr_session * session )
{
    close_bindinfo( session->public_ipv4 );
    tr_free( session->public_ipv4 );
    session->public_ipv4 = NULL;

    close_bindinfo( session->public_ipv6 );
    tr_free( session->public_ipv6 );
    session->public_ipv6 = NULL;
}

static void
accept_incoming_peer( int fd, short what UNUSED, void * vsession )
{
    int clientSocket;
    tr_port clientPort;
    tr_address clientAddr;
    tr_session * session = vsession;

    clientSocket = tr_netAccept( session, fd, &clientAddr, &clientPort );
    if( clientSocket > 0 ) {
        tr_deepLog( __FILE__, __LINE__, NULL, "new incoming connection %d (%s)",
                   clientSocket, tr_peerIoAddrStr( &clientAddr, clientPort ) );
        tr_peerMgrAddIncoming( session->peerMgr, &clientAddr, clientPort,
                               clientSocket, NULL );
    }
}

static void
open_incoming_peer_port( tr_session * session )
{
    struct tr_bindinfo * b;

    /* bind an ipv4 port to listen for incoming peers... */
    b = session->public_ipv4;
    b->socket = tr_netBindTCP( &b->addr, session->private_peer_port, false );
    if( b->socket >= 0 ) {
        b->ev = event_new( session->event_base, b->socket, EV_READ | EV_PERSIST, accept_incoming_peer, session );
        event_add( b->ev, NULL );
    }

    /* and do the exact same thing for ipv6, if it's supported... */
    if( tr_net_hasIPv6( session->private_peer_port ) ) {
        b = session->public_ipv6;
        b->socket = tr_netBindTCP( &b->addr, session->private_peer_port, false );
        if( b->socket >= 0 ) {
            b->ev = event_new( session->event_base, b->socket, EV_READ | EV_PERSIST, accept_incoming_peer, session );
            event_add( b->ev, NULL );
        }
    }
}

const tr_address*
tr_sessionGetPublicAddress( const tr_session * session, int tr_af_type, bool * is_default_value )
{
    const char * default_value;
    const struct tr_bindinfo * bindinfo;

    switch( tr_af_type )
    {
        case TR_AF_INET:
            bindinfo = session->public_ipv4;
            default_value = TR_DEFAULT_BIND_ADDRESS_IPV4;
            break;

        case TR_AF_INET6:
            bindinfo = session->public_ipv6;
            default_value = TR_DEFAULT_BIND_ADDRESS_IPV6;
            break;

        default:
            bindinfo = NULL;
            default_value = "";
            break;
    }

    if( is_default_value && bindinfo )
        *is_default_value = !tr_strcmp0( default_value, tr_address_to_string( &bindinfo->addr ) );

    return bindinfo ? &bindinfo->addr : NULL;
}

/***
****
***/

#ifdef TR_LIGHTWEIGHT
 #define TR_DEFAULT_ENCRYPTION   TR_CLEAR_PREFERRED
#else
 #define TR_DEFAULT_ENCRYPTION   TR_ENCRYPTION_PREFERRED
#endif

static int
parse_tos( const char *str )
{
    char *p;
    int value;

    if( !evutil_ascii_strcasecmp( str, "" ) )
        return 0;
    if( !evutil_ascii_strcasecmp( str, "default" ) )
        return 0;

    if( !evutil_ascii_strcasecmp( str, "lowcost" ) )
        return 0x10;
    if( !evutil_ascii_strcasecmp( str, "mincost" ) )
        return 0x10;

    if( !evutil_ascii_strcasecmp( str, "throughput" ) )
        return 0x08;
    if( !evutil_ascii_strcasecmp( str, "reliability" ) )
        return 0x04;
    if( !evutil_ascii_strcasecmp( str, "lowdelay" ) )
        return 0x02;

    value = strtol( str, &p, 0 );
    if( !p || ( p == str ) )
        return 0;

    return value;
}

static const char *
format_tos(int value)
{
    static char buf[8];
    switch(value) {
    case 0: return "default";
    case 0x10: return "lowcost";
    case 0x08: return "throughput";
    case 0x04: return "reliability";
    case 0x02: return "lowdelay";
    default:
        snprintf(buf, 8, "%d", value);
        return buf;
    }
}

void
tr_sessionGetDefaultSettings( tr_benc * d )
{
    assert( tr_bencIsDict( d ) );

    tr_bencDictReserve( d, 60 );
    tr_bencDictAddBool( d, TR_PREFS_KEY_BLOCKLIST_ENABLED,               false );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_BLOCKLIST_URL,                   "http://www.example.com/blocklist" );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_MAX_CACHE_SIZE_MB,               DEFAULT_CACHE_SIZE_MB );
    tr_bencDictAddBool( d, TR_PREFS_KEY_DHT_ENABLED,                     true );
    tr_bencDictAddBool( d, TR_PREFS_KEY_UTP_ENABLED,                     true );
    tr_bencDictAddBool( d, TR_PREFS_KEY_LPD_ENABLED,                     false );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_DOWNLOAD_DIR,                    tr_getDefaultDownloadDir( ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_DSPEED_KBps,                     100 );
    tr_bencDictAddBool( d, TR_PREFS_KEY_DSPEED_ENABLED,                  false );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ENCRYPTION,                      TR_DEFAULT_ENCRYPTION );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_IDLE_LIMIT,                      30 );
    tr_bencDictAddBool( d, TR_PREFS_KEY_IDLE_LIMIT_ENABLED,              false );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_INCOMPLETE_DIR,                  tr_getDefaultDownloadDir( ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_INCOMPLETE_DIR_ENABLED,          false );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_MSGLEVEL,                        TR_MSG_INF );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_DOWNLOAD_QUEUE_SIZE,             5 );
    tr_bencDictAddBool( d, TR_PREFS_KEY_DOWNLOAD_QUEUE_ENABLED,          true );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_LIMIT_GLOBAL,               atoi( TR_DEFAULT_PEER_LIMIT_GLOBAL_STR ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_LIMIT_TORRENT,              atoi( TR_DEFAULT_PEER_LIMIT_TORRENT_STR ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_PORT,                       atoi( TR_DEFAULT_PEER_PORT_STR ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_PEER_PORT_RANDOM_ON_START,       false );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_PORT_RANDOM_LOW,            49152 );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_PORT_RANDOM_HIGH,           65535 );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_PEER_SOCKET_TOS,                 TR_DEFAULT_PEER_SOCKET_TOS_STR );
    tr_bencDictAddBool( d, TR_PREFS_KEY_PEX_ENABLED,                     true );
    tr_bencDictAddBool( d, TR_PREFS_KEY_PORT_FORWARDING,                 true );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PREALLOCATION,                   TR_PREALLOCATE_SPARSE );
    tr_bencDictAddBool( d, TR_PREFS_KEY_PREFETCH_ENABLED,                DEFAULT_PREFETCH_ENABLED );
    tr_bencDictAddBool( d, TR_PREFS_KEY_QUEUE_STALLED_ENABLED,           true );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_QUEUE_STALLED_MINUTES,           30 );
    tr_bencDictAddReal( d, TR_PREFS_KEY_RATIO,                           2.0 );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RATIO_ENABLED,                   false );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RENAME_PARTIAL_FILES,            true );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RPC_AUTH_REQUIRED,               false );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_RPC_BIND_ADDRESS,                "0.0.0.0" );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RPC_ENABLED,                     false );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_RPC_PASSWORD,                    "" );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_RPC_USERNAME,                    "" );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_RPC_WHITELIST,                   TR_DEFAULT_RPC_WHITELIST );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RPC_WHITELIST_ENABLED,           true );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_RPC_PORT,                        atoi( TR_DEFAULT_RPC_PORT_STR ) );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_RPC_URL,                         TR_DEFAULT_RPC_URL_STR );
    tr_bencDictAddBool( d, TR_PREFS_KEY_SCRAPE_PAUSED_TORRENTS,          true );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_FILENAME,    "" );
    tr_bencDictAddBool( d, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_ENABLED,     false );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_SEED_QUEUE_SIZE,                 10 );
    tr_bencDictAddBool( d, TR_PREFS_KEY_SEED_QUEUE_ENABLED,              false );
    tr_bencDictAddBool( d, TR_PREFS_KEY_ALT_SPEED_ENABLED,               false );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_UP_KBps,               50 ); /* half the regular */
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_DOWN_KBps,             50 ); /* half the regular */
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_TIME_BEGIN,            540 ); /* 9am */
    tr_bencDictAddBool( d, TR_PREFS_KEY_ALT_SPEED_TIME_ENABLED,          false );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_TIME_END,              1020 ); /* 5pm */
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_TIME_DAY,              TR_SCHED_ALL );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_USPEED_KBps,                     100 );
    tr_bencDictAddBool( d, TR_PREFS_KEY_USPEED_ENABLED,                  false );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_UMASK,                           022 );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_UPLOAD_SLOTS_PER_TORRENT,        14 );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_BIND_ADDRESS_IPV4,               TR_DEFAULT_BIND_ADDRESS_IPV4 );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_BIND_ADDRESS_IPV6,               TR_DEFAULT_BIND_ADDRESS_IPV6 );
    tr_bencDictAddBool( d, TR_PREFS_KEY_START,                           true );
    tr_bencDictAddBool( d, TR_PREFS_KEY_TRASH_ORIGINAL,                  false );
}

void
tr_sessionGetSettings( tr_session * s, struct tr_benc * d )
{
    assert( tr_bencIsDict( d ) );

    tr_bencDictReserve( d, 60 );
    tr_bencDictAddBool( d, TR_PREFS_KEY_BLOCKLIST_ENABLED,                tr_blocklistIsEnabled( s ) );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_BLOCKLIST_URL,                    tr_blocklistGetURL( s ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_MAX_CACHE_SIZE_MB,                tr_sessionGetCacheLimit_MB( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_DHT_ENABLED,                      s->isDHTEnabled );
    tr_bencDictAddBool( d, TR_PREFS_KEY_UTP_ENABLED,                      s->isUTPEnabled );
    tr_bencDictAddBool( d, TR_PREFS_KEY_LPD_ENABLED,                      s->isLPDEnabled );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_DOWNLOAD_DIR,                     s->downloadDir );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_DOWNLOAD_QUEUE_SIZE,              tr_sessionGetQueueSize( s, TR_DOWN ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_DOWNLOAD_QUEUE_ENABLED,           tr_sessionGetQueueEnabled( s, TR_DOWN ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_DSPEED_KBps,                      tr_sessionGetSpeedLimit_KBps( s, TR_DOWN ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_DSPEED_ENABLED,                   tr_sessionIsSpeedLimited( s, TR_DOWN ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ENCRYPTION,                       s->encryptionMode );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_IDLE_LIMIT,                       tr_sessionGetIdleLimit( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_IDLE_LIMIT_ENABLED,               tr_sessionIsIdleLimited( s ) );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_INCOMPLETE_DIR,                   tr_sessionGetIncompleteDir( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_INCOMPLETE_DIR_ENABLED,           tr_sessionIsIncompleteDirEnabled( s ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_MSGLEVEL,                         tr_getMessageLevel( ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_LIMIT_GLOBAL,                s->peerLimit );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_LIMIT_TORRENT,               s->peerLimitPerTorrent );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_PORT,                        tr_sessionGetPeerPort( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_PEER_PORT_RANDOM_ON_START,        s->isPortRandom );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_PORT_RANDOM_LOW,             s->randomPortLow );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_PORT_RANDOM_HIGH,            s->randomPortHigh );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_PEER_SOCKET_TOS,                  format_tos(s->peerSocketTOS) );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_PEER_CONGESTION_ALGORITHM,        s->peer_congestion_algorithm );
    tr_bencDictAddBool( d, TR_PREFS_KEY_PEX_ENABLED,                      s->isPexEnabled );
    tr_bencDictAddBool( d, TR_PREFS_KEY_PORT_FORWARDING,                  tr_sessionIsPortForwardingEnabled( s ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PREALLOCATION,                    s->preallocationMode );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PREFETCH_ENABLED,                 s->isPrefetchEnabled );
    tr_bencDictAddBool( d, TR_PREFS_KEY_QUEUE_STALLED_ENABLED,            tr_sessionGetQueueStalledEnabled( s ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_QUEUE_STALLED_MINUTES,            tr_sessionGetQueueStalledMinutes( s ) );
    tr_bencDictAddReal( d, TR_PREFS_KEY_RATIO,                            s->desiredRatio );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RATIO_ENABLED,                    s->isRatioLimited );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RENAME_PARTIAL_FILES,             tr_sessionIsIncompleteFileNamingEnabled( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RPC_AUTH_REQUIRED,                tr_sessionIsRPCPasswordEnabled( s ) );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_RPC_BIND_ADDRESS,                 tr_sessionGetRPCBindAddress( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RPC_ENABLED,                      tr_sessionIsRPCEnabled( s ) );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_RPC_PASSWORD,                     tr_sessionGetRPCPassword( s ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_RPC_PORT,                         tr_sessionGetRPCPort( s ) );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_RPC_URL,                          tr_sessionGetRPCUrl( s ) );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_RPC_USERNAME,                     tr_sessionGetRPCUsername( s ) );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_RPC_WHITELIST,                    tr_sessionGetRPCWhitelist( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RPC_WHITELIST_ENABLED,            tr_sessionGetRPCWhitelistEnabled( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_SCRAPE_PAUSED_TORRENTS,           s->scrapePausedTorrents );
    tr_bencDictAddBool( d, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_ENABLED,      tr_sessionIsTorrentDoneScriptEnabled( s ) );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_FILENAME,     tr_sessionGetTorrentDoneScript( s ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_SEED_QUEUE_SIZE,                  tr_sessionGetQueueSize( s, TR_UP ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_SEED_QUEUE_ENABLED,               tr_sessionGetQueueEnabled( s, TR_UP ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_ALT_SPEED_ENABLED,                tr_sessionUsesAltSpeed( s ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_UP_KBps,                tr_sessionGetAltSpeed_KBps( s, TR_UP ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_DOWN_KBps,              tr_sessionGetAltSpeed_KBps( s, TR_DOWN ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_TIME_BEGIN,             tr_sessionGetAltSpeedBegin( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_ALT_SPEED_TIME_ENABLED,           tr_sessionUsesAltSpeedTime( s ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_TIME_END,               tr_sessionGetAltSpeedEnd( s ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_TIME_DAY,               tr_sessionGetAltSpeedDay( s ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_USPEED_KBps,                      tr_sessionGetSpeedLimit_KBps( s, TR_UP ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_USPEED_ENABLED,                   tr_sessionIsSpeedLimited( s, TR_UP ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_UMASK,                            s->umask );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_UPLOAD_SLOTS_PER_TORRENT,         s->uploadSlotsPerTorrent );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_BIND_ADDRESS_IPV4,                tr_address_to_string( &s->public_ipv4->addr ) );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_BIND_ADDRESS_IPV6,                tr_address_to_string( &s->public_ipv6->addr ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_START,                            !tr_sessionGetPaused( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_TRASH_ORIGINAL,                   tr_sessionGetDeleteSource( s ) );
}

bool
tr_sessionLoadSettings( tr_benc * d, const char * configDir, const char * appName )
{
    int err = 0;
    char * filename;
    tr_benc fileSettings;
    tr_benc sessionDefaults;
    tr_benc tmp;
    bool success = false;

    assert( tr_bencIsDict( d ) );

    /* initializing the defaults: caller may have passed in some app-level defaults.
     * preserve those and use the session defaults to fill in any missing gaps. */
    tr_bencInitDict( &sessionDefaults, 0 );
    tr_sessionGetDefaultSettings( &sessionDefaults );
    tr_bencMergeDicts( &sessionDefaults, d );
    tmp = *d; *d = sessionDefaults; sessionDefaults = tmp;

    /* if caller didn't specify a config dir, use the default */
    if( !configDir || !*configDir )
        configDir = tr_getDefaultConfigDir( appName );

    /* file settings override the defaults */
    filename = tr_buildPath( configDir, "settings.json", NULL );
    err = tr_bencLoadFile( &fileSettings, TR_FMT_JSON, filename );
    if( !err ) {
        tr_bencMergeDicts( d, &fileSettings );
        tr_bencFree( &fileSettings );
    }

    /* cleanup */
    tr_bencFree( &sessionDefaults );
    tr_free( filename );
    success = (err==0) || (err==ENOENT);
    return success;
}

void
tr_sessionSaveSettings( tr_session    * session,
                        const char    * configDir,
                        const tr_benc * clientSettings )
{
    tr_benc settings;
    char * filename = tr_buildPath( configDir, "settings.json", NULL );

    assert( tr_bencIsDict( clientSettings ) );

    tr_bencInitDict( &settings, 0 );

    /* the existing file settings are the fallback values */
    {
        tr_benc fileSettings;
        const int err = tr_bencLoadFile( &fileSettings, TR_FMT_JSON, filename );
        if( !err )
        {
            tr_bencMergeDicts( &settings, &fileSettings );
            tr_bencFree( &fileSettings );
        }
    }

    /* the client's settings override the file settings */
    tr_bencMergeDicts( &settings, clientSettings );

    /* the session's true values override the file & client settings */
    {
        tr_benc sessionSettings;
        tr_bencInitDict( &sessionSettings, 0 );
        tr_sessionGetSettings( session, &sessionSettings );
        tr_bencMergeDicts( &settings, &sessionSettings );
        tr_bencFree( &sessionSettings );
    }

    /* save the result */
    tr_bencToFile( &settings, TR_FMT_JSON, filename );

    /* cleanup */
    tr_free( filename );
    tr_bencFree( &settings );
}

/***
****
***/

/**
 * Periodically save the .resume files of any torrents whose
 * status has recently changed. This prevents loss of metadata
 * in the case of a crash, unclean shutdown, clumsy user, etc.
 */
static void
onSaveTimer( int foo UNUSED, short bar UNUSED, void * vsession )
{
    tr_torrent * tor = NULL;
    tr_session * session = vsession;

    if( tr_cacheFlushDone( session->cache ) )
        tr_err( "Error while flushing completed pieces from cache" );

    while(( tor = tr_torrentNext( session, tor )))
        tr_torrentSave( tor );

    tr_statsSaveDirty( session );

    tr_timerAdd( session->saveTimer, SAVE_INTERVAL_SECS, 0 );
}

/***
****
***/

static void tr_sessionInitImpl( void * );

struct init_data
{
    tr_session  * session;
    const char  * configDir;
    bool          done;
    bool          messageQueuingEnabled;
    tr_benc     * clientSettings;
};

tr_session *
tr_sessionInit( const char  * tag,
                const char  * configDir,
                bool          messageQueuingEnabled,
                tr_benc     * clientSettings )
{
    int64_t i;
    tr_session * session;
    struct init_data data;

    assert( tr_bencIsDict( clientSettings ) );

    tr_timeUpdate( time( NULL ) );

    /* initialize the bare skeleton of the session object */
    session = tr_new0( tr_session, 1 );
    session->udp_socket = -1;
    session->udp6_socket = -1;
    session->lock = tr_lockNew( );
    session->cache = tr_cacheNew( 1024*1024*2 );
    session->tag = tr_strdup( tag );
    session->magicNumber = SESSION_MAGIC_NUMBER;
    tr_bandwidthConstruct( &session->bandwidth, session, NULL );
    tr_peerIdInit( session->peer_id );
    tr_bencInitList( &session->removedTorrents, 0 );

    /* nice to start logging at the very beginning */
    if( tr_bencDictFindInt( clientSettings, TR_PREFS_KEY_MSGLEVEL, &i ) )
        tr_setMessageLevel( i );

    /* start the libtransmission thread */
    tr_netInit( ); /* must go before tr_eventInit */
    tr_eventInit( session );
    assert( session->events != NULL );

    /* run the rest in the libtransmission thread */
    data.done = false;
    data.session = session;
    data.configDir = configDir;
    data.messageQueuingEnabled = messageQueuingEnabled;
    data.clientSettings = clientSettings;
    tr_runInEventThread( session, tr_sessionInitImpl, &data );
    while( !data.done )
        tr_wait_msec( 100 );

    return session;
}

static void turtleCheckClock( tr_session * s, struct tr_turtle_info * t );

static void
onNowTimer( int foo UNUSED, short bar UNUSED, void * vsession )
{
    int usec;
    const int min = 100;
    const int max = 999999;
    struct timeval tv;
    tr_torrent * tor = NULL;
    tr_session * session = vsession;
    const time_t now = time( NULL );

    assert( tr_isSession( session ) );
    assert( session->nowTimer != NULL );

    /**
    ***  tr_session things to do once per second
    **/

    tr_timeUpdate( now );

    tr_dhtUpkeep( session );

    if( session->turtle.isClockEnabled )
        turtleCheckClock( session, &session->turtle );

    while(( tor = tr_torrentNext( session, tor ))) {
        if( tor->isRunning ) {
            if( tr_torrentIsSeed( tor ) )
                ++tor->secondsSeeding;
            else
                ++tor->secondsDownloading;
        }
    }

    /**
    ***  Set the timer
    **/

    /* schedule the next timer for right after the next second begins */
    gettimeofday( &tv, NULL );
    usec = 1000000 - tv.tv_usec;
    if( usec > max ) usec = max;
    if( usec < min ) usec = min;
    tr_timerAdd( session->nowTimer, 0, usec );
    /* fprintf( stderr, "time %zu sec, %zu microsec\n", (size_t)tr_time(), (size_t)tv.tv_usec ); */
}

static void loadBlocklists( tr_session * session );

static void
tr_sessionInitImpl( void * vdata )
{
    tr_benc settings;
    struct init_data * data = vdata;
    tr_benc * clientSettings = data->clientSettings;
    tr_session * session = data->session;

    assert( tr_amInEventThread( session ) );
    assert( tr_bencIsDict( clientSettings ) );

    dbgmsg( "tr_sessionInit: the session's top-level bandwidth object is %p",
            &session->bandwidth );

    tr_bencInitDict( &settings, 0 );
    tr_sessionGetDefaultSettings( &settings );
    tr_bencMergeDicts( &settings, clientSettings );

    assert( session->event_base != NULL );
    session->nowTimer = evtimer_new( session->event_base, onNowTimer, session );
    onNowTimer( 0, 0, session );

#ifndef WIN32
    /* Don't exit when writing on a broken socket */
    signal( SIGPIPE, SIG_IGN );
#endif

    tr_setMessageQueuing( data->messageQueuingEnabled );

    tr_setConfigDir( session, data->configDir );

    session->peerMgr = tr_peerMgrNew( session );

    session->shared = tr_sharedInit( session );

    /**
    ***  Blocklist
    **/

    {
        char * filename = tr_buildPath( session->configDir, "blocklists", NULL );
        tr_mkdirp( filename, 0777 );
        tr_free( filename );
        loadBlocklists( session );
    }

    assert( tr_isSession( session ) );

    session->saveTimer = evtimer_new( session->event_base, onSaveTimer, session );
    tr_timerAdd( session->saveTimer, SAVE_INTERVAL_SECS, 0 );

    tr_announcerInit( session );

    /* first %s is the application name
       second %s is the version number */
    tr_inf( _( "%s %s started" ), TR_NAME, LONG_VERSION_STRING );

    tr_statsInit( session );

    tr_webInit( session );

    tr_sessionSet( session, &settings );

    tr_udpInit( session );

    if( session->isLPDEnabled )
        tr_lpdInit( session, &session->public_ipv4->addr );

    /* cleanup */
    tr_bencFree( &settings );
    data->done = true;
}

static void turtleBootstrap( tr_session *, struct tr_turtle_info * );
static void setPeerPort( tr_session * session, tr_port port );

static void
sessionSetImpl( void * vdata )
{
    int64_t i;
    double  d;
    bool boolVal;
    const char * str;
    struct tr_bindinfo b;
    struct init_data * data = vdata;
    tr_session * session = data->session;
    tr_benc * settings = data->clientSettings;
    struct tr_turtle_info * turtle = &session->turtle;

    assert( tr_isSession( session ) );
    assert( tr_bencIsDict( settings ) );
    assert( tr_amInEventThread( session ) );

    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_MSGLEVEL, &i ) )
        tr_setMessageLevel( i );

    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_UMASK, &i ) ) {
        session->umask = (mode_t)i;
        umask( session->umask );
    }

    /* misc features */
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_MAX_CACHE_SIZE_MB, &i ) )
        tr_sessionSetCacheLimit_MB( session, i );
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_PEER_LIMIT_TORRENT, &i ) )
        tr_sessionSetPeerLimitPerTorrent( session, i );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_PEX_ENABLED, &boolVal ) )
        tr_sessionSetPexEnabled( session, boolVal );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_DHT_ENABLED, &boolVal ) )
        tr_sessionSetDHTEnabled( session, boolVal );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_UTP_ENABLED, &boolVal ) )
        tr_sessionSetUTPEnabled( session, boolVal );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_LPD_ENABLED, &boolVal ) )
        tr_sessionSetLPDEnabled( session, boolVal );
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_ENCRYPTION, &i ) )
        tr_sessionSetEncryption( session, i );
    if( tr_bencDictFindStr( settings, TR_PREFS_KEY_PEER_SOCKET_TOS, &str ) )
        session->peerSocketTOS = parse_tos( str );
    if( tr_bencDictFindStr( settings, TR_PREFS_KEY_PEER_CONGESTION_ALGORITHM, &str ) )
        session->peer_congestion_algorithm = tr_strdup(str);
    else
        session->peer_congestion_algorithm = tr_strdup("");
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_BLOCKLIST_ENABLED, &boolVal ) )
        tr_blocklistSetEnabled( session, boolVal );
    if( tr_bencDictFindStr( settings, TR_PREFS_KEY_BLOCKLIST_URL, &str ) )
        tr_blocklistSetURL( session, str );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_START, &boolVal ) )
        tr_sessionSetPaused( session, !boolVal );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_TRASH_ORIGINAL, &boolVal) )
        tr_sessionSetDeleteSource( session, boolVal );

    /* torrent queues */
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_QUEUE_STALLED_MINUTES, &i ) )
        tr_sessionSetQueueStalledMinutes( session, i );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_QUEUE_STALLED_ENABLED, &boolVal ) )
        tr_sessionSetQueueStalledEnabled( session, boolVal );
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_DOWNLOAD_QUEUE_SIZE, &i ) )
        tr_sessionSetQueueSize( session, TR_DOWN, i );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_DOWNLOAD_QUEUE_ENABLED, &boolVal ) )
        tr_sessionSetQueueEnabled( session, TR_DOWN, boolVal );
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_SEED_QUEUE_SIZE, &i ) )
        tr_sessionSetQueueSize( session, TR_UP, i );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_SEED_QUEUE_ENABLED, &boolVal ) )
        tr_sessionSetQueueEnabled( session, TR_UP, boolVal );

    /* files and directories */
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_PREFETCH_ENABLED, &boolVal ) )
        session->isPrefetchEnabled = boolVal;
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_PREALLOCATION, &i ) )
        session->preallocationMode = i;
    if( tr_bencDictFindStr( settings, TR_PREFS_KEY_DOWNLOAD_DIR, &str ) )
        tr_sessionSetDownloadDir( session, str );
    if( tr_bencDictFindStr( settings, TR_PREFS_KEY_INCOMPLETE_DIR, &str ) )
        tr_sessionSetIncompleteDir( session, str );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_INCOMPLETE_DIR_ENABLED, &boolVal ) )
        tr_sessionSetIncompleteDirEnabled( session, boolVal );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_RENAME_PARTIAL_FILES, &boolVal ) )
        tr_sessionSetIncompleteFileNamingEnabled( session, boolVal );

    /* rpc server */
    if( session->rpcServer != NULL ) /* close the old one */
        tr_rpcClose( &session->rpcServer );
    session->rpcServer = tr_rpcInit( session, settings );

    /* public addresses */

    free_incoming_peer_port( session );

    str = TR_PREFS_KEY_BIND_ADDRESS_IPV4;
    tr_bencDictFindStr( settings, TR_PREFS_KEY_BIND_ADDRESS_IPV4, &str );
    if( !tr_address_from_string( &b.addr, str ) || ( b.addr.type != TR_AF_INET ) )
        b.addr = tr_inaddr_any;
    b.socket = -1;
    session->public_ipv4 = tr_memdup( &b, sizeof( struct tr_bindinfo ) );

    str = TR_PREFS_KEY_BIND_ADDRESS_IPV6;
    tr_bencDictFindStr( settings, TR_PREFS_KEY_BIND_ADDRESS_IPV6, &str );
    if( !tr_address_from_string( &b.addr, str ) || ( b.addr.type != TR_AF_INET6 ) )
        b.addr = tr_in6addr_any;
    b.socket = -1;
    session->public_ipv6 = tr_memdup( &b, sizeof( struct tr_bindinfo ) );

    /* incoming peer port */
    if( tr_bencDictFindInt ( settings, TR_PREFS_KEY_PEER_PORT_RANDOM_LOW, &i ) )
        session->randomPortLow = i;
    if( tr_bencDictFindInt ( settings, TR_PREFS_KEY_PEER_PORT_RANDOM_HIGH, &i ) )
        session->randomPortHigh = i;
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_PEER_PORT_RANDOM_ON_START, &boolVal ) )
        tr_sessionSetPeerPortRandomOnStart( session, boolVal );
    if( !tr_bencDictFindInt( settings, TR_PREFS_KEY_PEER_PORT, &i ) )
        i = session->private_peer_port;
    setPeerPort( session, boolVal ? getRandomPort( session ) : i );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_PORT_FORWARDING, &boolVal ) )
        tr_sessionSetPortForwardingEnabled( session, boolVal );

    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_PEER_LIMIT_GLOBAL, &i ) )
        session->peerLimit = i;

    /**
    **/

    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_UPLOAD_SLOTS_PER_TORRENT, &i ) )
        session->uploadSlotsPerTorrent = i;

    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_USPEED_KBps, &i ) )
        tr_sessionSetSpeedLimit_KBps( session, TR_UP, i );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_USPEED_ENABLED, &boolVal ) )
        tr_sessionLimitSpeed( session, TR_UP, boolVal );

    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_DSPEED_KBps, &i ) )
        tr_sessionSetSpeedLimit_KBps( session, TR_DOWN, i );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_DSPEED_ENABLED, &boolVal ) )
        tr_sessionLimitSpeed( session, TR_DOWN, boolVal );

    if( tr_bencDictFindReal( settings, TR_PREFS_KEY_RATIO, &d ) )
        tr_sessionSetRatioLimit( session, d );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_RATIO_ENABLED, &boolVal ) )
        tr_sessionSetRatioLimited( session, boolVal );

    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_IDLE_LIMIT, &i ) )
        tr_sessionSetIdleLimit( session, i );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_IDLE_LIMIT_ENABLED, &boolVal ) )
        tr_sessionSetIdleLimited( session, boolVal );

    /**
    ***  Turtle Mode
    **/

    /* update the turtle mode's fields */
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_ALT_SPEED_UP_KBps, &i ) )
        turtle->speedLimit_Bps[TR_UP] = toSpeedBytes( i );
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_ALT_SPEED_DOWN_KBps, &i ) )
        turtle->speedLimit_Bps[TR_DOWN] = toSpeedBytes( i );
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_ALT_SPEED_TIME_BEGIN, &i ) )
        turtle->beginMinute = i;
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_ALT_SPEED_TIME_END, &i ) )
        turtle->endMinute = i;
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_ALT_SPEED_TIME_DAY, &i ) )
        turtle->days = i;
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_ALT_SPEED_TIME_ENABLED, &boolVal ) )
        turtle->isClockEnabled = boolVal;
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_ALT_SPEED_ENABLED, &boolVal ) )
        turtle->isEnabled = boolVal;
    turtleBootstrap( session, turtle );

    /**
    ***  Scripts
    **/

    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_ENABLED, &boolVal ) )
        tr_sessionSetTorrentDoneScriptEnabled( session, boolVal );
    if( tr_bencDictFindStr( settings, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_FILENAME, &str ) )
        tr_sessionSetTorrentDoneScript( session, str );


    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_SCRAPE_PAUSED_TORRENTS, &boolVal ) )
        session->scrapePausedTorrents = boolVal;

    data->done = true;
}

void
tr_sessionSet( tr_session * session, struct tr_benc  * settings )
{
    struct init_data data;
    data.done = false;
    data.session = session;
    data.clientSettings = settings;

    /* run the rest in the libtransmission thread */
    tr_runInEventThread( session, sessionSetImpl, &data );
    while( !data.done )
        tr_wait_msec( 100 );
}

/***
****
***/

void
tr_sessionSetDownloadDir( tr_session * session, const char * dir )
{
    assert( tr_isSession( session ) );

    if( session->downloadDir != dir )
    {
        tr_free( session->downloadDir );
        session->downloadDir = tr_strdup( dir );
    }
}

const char *
tr_sessionGetDownloadDir( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->downloadDir;
}

int64_t
tr_sessionGetDownloadDirFreeSpace( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_getFreeSpace( session->downloadDir );
}

/***
****
***/

void
tr_sessionSetIncompleteFileNamingEnabled( tr_session * session, bool b )
{
    assert( tr_isSession( session ) );
    assert( tr_isBool( b ) );

    session->isIncompleteFileNamingEnabled = b;
}

bool
tr_sessionIsIncompleteFileNamingEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->isIncompleteFileNamingEnabled;
}

/***
****
***/


void
tr_sessionSetIncompleteDir( tr_session * session, const char * dir )
{
    assert( tr_isSession( session ) );

    if( session->incompleteDir != dir )
    {
        tr_free( session->incompleteDir );

        session->incompleteDir = tr_strdup( dir );
    }
}

const char*
tr_sessionGetIncompleteDir( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->incompleteDir;
}

void
tr_sessionSetIncompleteDirEnabled( tr_session * session, bool b )
{
    assert( tr_isSession( session ) );
    assert( tr_isBool( b ) );

    session->isIncompleteDirEnabled = b;
}

bool
tr_sessionIsIncompleteDirEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->isIncompleteDirEnabled;
}

/***
****
***/

void
tr_sessionLock( tr_session * session )
{
    assert( tr_isSession( session ) );

    tr_lockLock( session->lock );
}

void
tr_sessionUnlock( tr_session * session )
{
    assert( tr_isSession( session ) );

    tr_lockUnlock( session->lock );
}

bool
tr_sessionIsLocked( const tr_session * session )
{
    return tr_isSession( session ) && tr_lockHave( session->lock );
}

/***********************************************************************
 * tr_setBindPort
 ***********************************************************************
 *
 **********************************************************************/

static void
peerPortChanged( void * session )
{
    tr_torrent * tor = NULL;

    assert( tr_isSession( session ) );

    close_incoming_peer_port( session );
    open_incoming_peer_port( session );
    tr_sharedPortChanged( session );

    while(( tor = tr_torrentNext( session, tor )))
        tr_torrentChangeMyPort( tor );
}

static void
setPeerPort( tr_session * session, tr_port port )
{
    session->private_peer_port = port;
    session->public_peer_port = port;

    tr_runInEventThread( session, peerPortChanged, session );
}

void
tr_sessionSetPeerPort( tr_session * session, tr_port port )
{
    if( tr_isSession( session ) && ( session->private_peer_port != port ) )
    {
        setPeerPort( session, port );
    }
}

tr_port
tr_sessionGetPeerPort( const tr_session * session )
{
    return tr_isSession( session ) ? session->private_peer_port : 0;
}

tr_port
tr_sessionSetPeerPortRandom( tr_session * session )
{
    assert( tr_isSession( session ) );

    tr_sessionSetPeerPort( session, getRandomPort( session ) );
    return session->private_peer_port;
}

void
tr_sessionSetPeerPortRandomOnStart( tr_session * session,
                                    bool random )
{
    assert( tr_isSession( session ) );

    session->isPortRandom = random;
}

bool
tr_sessionGetPeerPortRandomOnStart( tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->isPortRandom;
}

tr_port_forwarding
tr_sessionGetPortForwarding( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_sharedTraversalStatus( session->shared );
}

/***
****
***/

void
tr_sessionSetRatioLimited( tr_session * session, bool isLimited )
{
    assert( tr_isSession( session ) );

    session->isRatioLimited = isLimited;
}

void
tr_sessionSetRatioLimit( tr_session * session, double desiredRatio )
{
    assert( tr_isSession( session ) );

    session->desiredRatio = desiredRatio;
}

bool
tr_sessionIsRatioLimited( const tr_session  * session )
{
    assert( tr_isSession( session ) );

    return session->isRatioLimited;
}

double
tr_sessionGetRatioLimit( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->desiredRatio;
}

/***
****
***/

void
tr_sessionSetIdleLimited( tr_session * session, bool isLimited )
{
    assert( tr_isSession( session ) );

    session->isIdleLimited = isLimited;
}

void
tr_sessionSetIdleLimit( tr_session * session, uint16_t idleMinutes )
{
    assert( tr_isSession( session ) );

    session->idleLimitMinutes = idleMinutes;
}

bool
tr_sessionIsIdleLimited( const tr_session  * session )
{
    assert( tr_isSession( session ) );

    return session->isIdleLimited;
}

uint16_t
tr_sessionGetIdleLimit( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->idleLimitMinutes;
}

/***
****
****  SPEED LIMITS
****
***/

bool
tr_sessionGetActiveSpeedLimit_Bps( const tr_session * session, tr_direction dir, unsigned int * setme_Bps )
{
    int isLimited = true;

    if( !tr_isSession( session ) )
        return false;

    if( tr_sessionUsesAltSpeed( session ) )
        *setme_Bps = tr_sessionGetAltSpeed_Bps( session, dir );
    else if( tr_sessionIsSpeedLimited( session, dir ) )
        *setme_Bps = tr_sessionGetSpeedLimit_Bps( session, dir );
    else
        isLimited = false;

    return isLimited;
}
bool
tr_sessionGetActiveSpeedLimit_KBps( const tr_session  * session,
                                    tr_direction        dir,
                                    double            * setme_KBps )
{
    unsigned int Bps = 0;
    const bool is_active = tr_sessionGetActiveSpeedLimit_Bps( session, dir, &Bps );
    *setme_KBps = toSpeedKBps( Bps );
    return is_active;
}

static void
updateBandwidth( tr_session * session, tr_direction dir )
{
    unsigned int limit_Bps = 0;
    const bool isLimited = tr_sessionGetActiveSpeedLimit_Bps( session, dir, &limit_Bps );
    const bool zeroCase = isLimited && !limit_Bps;

    tr_bandwidthSetLimited( &session->bandwidth, dir, isLimited && !zeroCase );

    tr_bandwidthSetDesiredSpeed_Bps( &session->bandwidth, dir, limit_Bps );
}

enum
{
    MINUTES_PER_HOUR = 60,
    MINUTES_PER_DAY = MINUTES_PER_HOUR * 24,
    MINUTES_PER_WEEK = MINUTES_PER_DAY * 7
};

static void
turtleUpdateTable( struct tr_turtle_info * t )
{
    int day;
    tr_bitfield * b = &t->minutes;

    tr_bitfieldSetHasNone( b );

    for( day=0; day<7; ++day )
    {
        if( t->days & (1<<day) )
        {
            int i;
            const time_t begin = t->beginMinute;
            time_t end = t->endMinute;

            if( end <= begin )
                end += MINUTES_PER_DAY;

            for( i=begin; i<end; ++i )
                tr_bitfieldAdd( b, (i+day*MINUTES_PER_DAY) % MINUTES_PER_WEEK );
        }
    }
}

static void
altSpeedToggled( void * vsession )
{
    tr_session * session = vsession;
    struct tr_turtle_info * t = &session->turtle;

    assert( tr_isSession( session ) );

    updateBandwidth( session, TR_UP );
    updateBandwidth( session, TR_DOWN );

    if( t->callback != NULL )
        (*t->callback)( session, t->isEnabled, t->changedByUser, t->callbackUserData );
}

static void
useAltSpeed( tr_session * s, struct tr_turtle_info * t,
             bool enabled, bool byUser )
{
    assert( tr_isSession( s ) );
    assert( t != NULL );
    assert( tr_isBool( enabled ) );
    assert( tr_isBool( byUser ) );

    if( t->isEnabled != enabled )
    {
        t->isEnabled = enabled;
        t->changedByUser = byUser;
        tr_runInEventThread( s, altSpeedToggled, s );
    }
}

/**
 * @return whether turtle should be on/off according to the scheduler
 */
static bool
getInTurtleTime( const struct tr_turtle_info * t )
{
    struct tm tm;
    size_t minute_of_the_week;
    const time_t now = tr_time( );

    tr_localtime_r( &now, &tm );

    minute_of_the_week = tm.tm_wday * MINUTES_PER_DAY
                       + tm.tm_hour * MINUTES_PER_HOUR
                       + tm.tm_min;
    if( minute_of_the_week >= MINUTES_PER_WEEK ) /* leap minutes? */
        minute_of_the_week = MINUTES_PER_WEEK - 1;

    return tr_bitfieldHas( &t->minutes, minute_of_the_week );
}

static inline tr_auto_switch_state_t
autoSwitchState( bool enabled )
{
    return enabled ? TR_AUTO_SWITCH_ON : TR_AUTO_SWITCH_OFF;
}

static void
turtleCheckClock( tr_session * s, struct tr_turtle_info * t )
{
    bool enabled;
    bool alreadySwitched;
    tr_auto_switch_state_t newAutoTurtleState;

    assert( t->isClockEnabled );

    enabled = getInTurtleTime( t );
    newAutoTurtleState = autoSwitchState( enabled );
    alreadySwitched = ( t->autoTurtleState == newAutoTurtleState );

    if( !alreadySwitched )
    {
        tr_inf( "Time to turn %s turtle mode!", (enabled?"on":"off") );
        t->autoTurtleState = newAutoTurtleState;
        useAltSpeed( s, t, enabled, false );
    }
}

/* Called after the turtle's fields are loaded from an outside source.
 * It initializes the implementation fields
 * and turns on turtle mode if the clock settings say to. */
static void
turtleBootstrap( tr_session * session, struct tr_turtle_info * turtle )
{
    turtle->changedByUser = false;
    turtle->autoTurtleState = TR_AUTO_SWITCH_UNUSED;

    tr_bitfieldConstruct( &turtle->minutes, MINUTES_PER_WEEK );

    turtleUpdateTable( turtle );

    if( turtle->isClockEnabled )
    {
        turtle->isEnabled = getInTurtleTime( turtle );
        turtle->autoTurtleState = autoSwitchState( turtle->isEnabled );
    }

    altSpeedToggled( session );

}

/***
****  Primary session speed limits
***/

void
tr_sessionSetSpeedLimit_Bps( tr_session * s, tr_direction d, unsigned int Bps )
{
    assert( tr_isSession( s ) );
    assert( tr_isDirection( d ) );

    s->speedLimit_Bps[d] = Bps;

    updateBandwidth( s, d );
}
void
tr_sessionSetSpeedLimit_KBps( tr_session * s, tr_direction d, unsigned int KBps )
{
    tr_sessionSetSpeedLimit_Bps( s, d, toSpeedBytes( KBps ) );
}

unsigned int
tr_sessionGetSpeedLimit_Bps( const tr_session * s, tr_direction d )
{
    assert( tr_isSession( s ) );
    assert( tr_isDirection( d ) );

    return s->speedLimit_Bps[d];
}
unsigned int
tr_sessionGetSpeedLimit_KBps( const tr_session * s, tr_direction d )
{
    return toSpeedKBps( tr_sessionGetSpeedLimit_Bps( s, d ) );
}

void
tr_sessionLimitSpeed( tr_session * s, tr_direction d, bool b )
{
    assert( tr_isSession( s ) );
    assert( tr_isDirection( d ) );
    assert( tr_isBool( b ) );

    s->speedLimitEnabled[d] = b;

    updateBandwidth( s, d );
}

bool
tr_sessionIsSpeedLimited( const tr_session * s, tr_direction d )
{
    assert( tr_isSession( s ) );
    assert( tr_isDirection( d ) );

    return s->speedLimitEnabled[d];
}

/***
****  Alternative speed limits that are used during scheduled times
***/

void
tr_sessionSetAltSpeed_Bps( tr_session * s, tr_direction d, unsigned int Bps )
{
    assert( tr_isSession( s ) );
    assert( tr_isDirection( d ) );

    s->turtle.speedLimit_Bps[d] = Bps;

    updateBandwidth( s, d );
}

void
tr_sessionSetAltSpeed_KBps( tr_session * s, tr_direction d, unsigned int KBps )
{
    tr_sessionSetAltSpeed_Bps( s, d, toSpeedBytes( KBps ) );
}

unsigned int
tr_sessionGetAltSpeed_Bps( const tr_session * s, tr_direction d )
{
    assert( tr_isSession( s ) );
    assert( tr_isDirection( d ) );

    return s->turtle.speedLimit_Bps[d];
}
unsigned int
tr_sessionGetAltSpeed_KBps( const tr_session * s, tr_direction d )
{
    return toSpeedKBps( tr_sessionGetAltSpeed_Bps( s, d ) );
}

static void
userPokedTheClock( tr_session * s, struct tr_turtle_info * t )
{
    tr_dbg( "Refreshing the turtle mode clock due to user changes" );

    t->autoTurtleState = TR_AUTO_SWITCH_UNUSED;

    turtleUpdateTable( t );

    if( t->isClockEnabled )
    {
        const bool enabled = getInTurtleTime( t );
        useAltSpeed( s, t, enabled, true );
        t->autoTurtleState = autoSwitchState( enabled );
    }
}

void
tr_sessionUseAltSpeedTime( tr_session * s, bool b )
{
    struct tr_turtle_info * t = &s->turtle;

    assert( tr_isSession( s ) );
    assert( tr_isBool ( b ) );

    if( t->isClockEnabled != b ) {
        t->isClockEnabled = b;
        userPokedTheClock( s, t );
    }
}

bool
tr_sessionUsesAltSpeedTime( const tr_session * s )
{
    assert( tr_isSession( s ) );

    return s->turtle.isClockEnabled;
}

void
tr_sessionSetAltSpeedBegin( tr_session * s, int minute )
{
    assert( tr_isSession( s ) );
    assert( 0<=minute && minute<(60*24) );

    if( s->turtle.beginMinute != minute ) {
        s->turtle.beginMinute = minute;
        userPokedTheClock( s, &s->turtle );
    }
}

int
tr_sessionGetAltSpeedBegin( const tr_session * s )
{
    assert( tr_isSession( s ) );

    return s->turtle.beginMinute;
}

void
tr_sessionSetAltSpeedEnd( tr_session * s, int minute )
{
    assert( tr_isSession( s ) );
    assert( 0<=minute && minute<(60*24) );

    if( s->turtle.endMinute != minute ) {
        s->turtle.endMinute = minute;
        userPokedTheClock( s, &s->turtle );
    }
}

int
tr_sessionGetAltSpeedEnd( const tr_session * s )
{
    assert( tr_isSession( s ) );

    return s->turtle.endMinute;
}

void
tr_sessionSetAltSpeedDay( tr_session * s, tr_sched_day days )
{
    assert( tr_isSession( s ) );

    if( s->turtle.days != days ) {
        s->turtle.days = days;
        userPokedTheClock( s, &s->turtle );
    }
}

tr_sched_day
tr_sessionGetAltSpeedDay( const tr_session * s )
{
    assert( tr_isSession( s ) );

    return s->turtle.days;
}

void
tr_sessionUseAltSpeed( tr_session * session, bool enabled )
{
    useAltSpeed( session, &session->turtle, enabled, true );
}

bool
tr_sessionUsesAltSpeed( const tr_session * s )
{
    assert( tr_isSession( s ) );

    return s->turtle.isEnabled;
}

void
tr_sessionSetAltSpeedFunc( tr_session       * session,
                           tr_altSpeedFunc    func,
                           void             * userData )
{
    assert( tr_isSession( session ) );

    session->turtle.callback = func;
    session->turtle.callbackUserData = userData;
}

void
tr_sessionClearAltSpeedFunc( tr_session * session )
{
    tr_sessionSetAltSpeedFunc( session, NULL, NULL );
}

/***
****
***/

void
tr_sessionSetPeerLimit( tr_session * session, uint16_t n )
{
    assert( tr_isSession( session ) );

    session->peerLimit = n;
}

uint16_t
tr_sessionGetPeerLimit( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->peerLimit;
}

void
tr_sessionSetPeerLimitPerTorrent( tr_session  * session, uint16_t n )
{
    assert( tr_isSession( session ) );

    session->peerLimitPerTorrent = n;
}

uint16_t
tr_sessionGetPeerLimitPerTorrent( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->peerLimitPerTorrent;
}

/***
****
***/

void
tr_sessionSetPaused( tr_session * session, bool isPaused )
{
    assert( tr_isSession( session ) );

    session->pauseAddedTorrent = isPaused;
}

bool
tr_sessionGetPaused( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->pauseAddedTorrent;
}

void
tr_sessionSetDeleteSource( tr_session * session, bool deleteSource )
{
    assert( tr_isSession( session ) );

    session->deleteSourceTorrent = deleteSource;
}

bool
tr_sessionGetDeleteSource( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->deleteSourceTorrent;
}

/***
****
***/

unsigned int
tr_sessionGetPieceSpeed_Bps( const tr_session * session, tr_direction dir )
{
    return tr_isSession( session ) ? tr_bandwidthGetPieceSpeed_Bps( &session->bandwidth, 0, dir ) : 0;
}

unsigned int
tr_sessionGetRawSpeed_Bps( const tr_session * session, tr_direction dir )
{
    return tr_isSession( session ) ? tr_bandwidthGetRawSpeed_Bps( &session->bandwidth, 0, dir ) : 0;
}
double
tr_sessionGetRawSpeed_KBps( const tr_session * session, tr_direction dir )
{
    return toSpeedKBps( tr_sessionGetRawSpeed_Bps( session, dir ) );
}


int
tr_sessionCountTorrents( const tr_session * session )
{
    return tr_isSession( session ) ? session->torrentCount : 0;
}

static int
compareTorrentByCur( const void * va, const void * vb )
{
    const tr_torrent * a = *(const tr_torrent**)va;
    const tr_torrent * b = *(const tr_torrent**)vb;
    const uint64_t     aCur = a->downloadedCur + a->uploadedCur;
    const uint64_t     bCur = b->downloadedCur + b->uploadedCur;

    if( aCur != bCur )
        return aCur > bCur ? -1 : 1; /* close the biggest torrents first */

    return 0;
}

static void closeBlocklists( tr_session * );

static void
sessionCloseImpl( void * vsession )
{
    tr_session *  session = vsession;
    tr_torrent *  tor;
    int           i, n;
    tr_torrent ** torrents;

    assert( tr_isSession( session ) );

    free_incoming_peer_port( session );

    if( session->isLPDEnabled )
        tr_lpdUninit( session );

    tr_utpClose( session );
    tr_dhtUninit( session );

    event_free( session->saveTimer );
    session->saveTimer = NULL;

    event_free( session->nowTimer );
    session->nowTimer = NULL;

    tr_verifyClose( session );
    tr_sharedClose( session );
    tr_rpcClose( &session->rpcServer );

    /* Close the torrents. Get the most active ones first so that
     * if we can't get them all closed in a reasonable amount of time,
     * at least we get the most important ones first. */
    tor = NULL;
    n = session->torrentCount;
    torrents = tr_new( tr_torrent *, session->torrentCount );
    for( i = 0; i < n; ++i )
        torrents[i] = tor = tr_torrentNext( session, tor );
    qsort( torrents, n, sizeof( tr_torrent* ), compareTorrentByCur );
    for( i = 0; i < n; ++i )
        tr_torrentFree( torrents[i] );
    tr_free( torrents );

    /* Close the announcer *after* closing the torrents
       so that all the &event=stopped messages will be
       queued to be sent by tr_announcerClose() */
    tr_announcerClose( session );

    /* and this goes *after* announcer close so that
       it won't be idle until the announce events are sent... */
    tr_webClose( session, TR_WEB_CLOSE_WHEN_IDLE );

    tr_cacheFree( session->cache );
    session->cache = NULL;

    /* gotta keep udp running long enough to send out all
       the &event=stopped UDP tracker messages */
    while( !tr_tracker_udp_is_idle( session ) ) {
        tr_tracker_udp_upkeep( session );
        tr_wait_msec( 100 );
    }

    /* we had to wait until UDP trackers were closed before closing these: */
    evdns_base_free( session->evdns_base, 0 );
    session->evdns_base = NULL;
    tr_tracker_udp_close( session );
    tr_udpUninit( session );

    tr_statsClose( session );
    tr_peerMgrFree( session->peerMgr );

    closeBlocklists( session );

    tr_fdClose( session );

    session->isClosed = true;
}

static int
deadlineReached( const time_t deadline )
{
    return time( NULL ) >= deadline;
}

#define SHUTDOWN_MAX_SECONDS 20

void
tr_sessionClose( tr_session * session )
{
    const time_t deadline = time( NULL ) + SHUTDOWN_MAX_SECONDS;

    assert( tr_isSession( session ) );

    dbgmsg( "shutting down transmission session %p... now is %zu, deadline is %zu", session, (size_t)time(NULL), (size_t)deadline );

    /* close the session */
    tr_runInEventThread( session, sessionCloseImpl, session );
    while( !session->isClosed && !deadlineReached( deadline ) )
    {
        dbgmsg( "waiting for the libtransmission thread to finish" );
        tr_wait_msec( 100 );
    }

    /* "shared" and "tracker" have live sockets,
     * so we need to keep the transmission thread alive
     * for a bit while they tell the router & tracker
     * that we're closing now */
    while( ( session->shared || session->web || session->announcer || session->announcer_udp )
           && !deadlineReached( deadline ) )
    {
        dbgmsg( "waiting on port unmap (%p) or announcer (%p)... now %zu deadline %zu",
                session->shared, session->announcer, (size_t)time(NULL), (size_t)deadline );
        tr_wait_msec( 100 );
    }

    tr_webClose( session, TR_WEB_CLOSE_NOW );

    /* close the libtransmission thread */
    tr_eventClose( session );
    while( session->events != NULL )
    {
        static bool forced = false;
        dbgmsg( "waiting for libtransmission thread to finish... now %zu deadline %zu", (size_t)time(NULL), (size_t)deadline );
        tr_wait_msec( 500 );
        if( deadlineReached( deadline ) && !forced )
        {
            dbgmsg( "calling event_loopbreak()" );
            forced = true;
            event_base_loopbreak( session->event_base );
        }
        if( deadlineReached( deadline+3 ) )
        {
            dbgmsg( "deadline+3 reached... calling break...\n" );
            break;
        }
    }

    /* free the session memory */
    tr_bencFree( &session->removedTorrents );
    tr_bandwidthDestruct( &session->bandwidth );
    tr_bitfieldDestruct( &session->turtle.minutes );
    tr_lockFree( session->lock );
    if( session->metainfoLookup ) {
        tr_bencFree( session->metainfoLookup );
        tr_free( session->metainfoLookup );
    }
    tr_free( session->torrentDoneScript );
    tr_free( session->tag );
    tr_free( session->configDir );
    tr_free( session->resumeDir );
    tr_free( session->torrentDir );
    tr_free( session->downloadDir );
    tr_free( session->incompleteDir );
    tr_free( session->blocklist_url );
    tr_free( session->peer_congestion_algorithm );
    tr_free( session );
}

struct sessionLoadTorrentsData
{
    tr_session * session;
    tr_ctor * ctor;
    int * setmeCount;
    tr_torrent ** torrents;
    bool done;
};

static void
sessionLoadTorrents( void * vdata )
{
    int i;
    int n = 0;
    struct stat sb;
    DIR * odir = NULL;
    tr_list * l = NULL;
    tr_list * list = NULL;
    struct sessionLoadTorrentsData * data = vdata;
    const char * dirname = tr_getTorrentDir( data->session );

    assert( tr_isSession( data->session ) );

    tr_ctorSetSave( data->ctor, false ); /* since we already have them */

    if( !stat( dirname, &sb )
      && S_ISDIR( sb.st_mode )
      && ( ( odir = opendir ( dirname ) ) ) )
    {
        struct dirent *d;
        for( d = readdir( odir ); d != NULL; d = readdir( odir ) )
        {
            if( tr_str_has_suffix( d->d_name, ".torrent" ) )
            {
                tr_torrent * tor;
                char * path = tr_buildPath( dirname, d->d_name, NULL );
                tr_ctorSetMetainfoFromFile( data->ctor, path );
                if(( tor = tr_torrentNew( data->ctor, NULL )))
                {
                    tr_list_prepend( &list, tor );
                    ++n;
                }
                tr_free( path );
            }
        }
        closedir( odir );
    }

    data->torrents = tr_new( tr_torrent *, n );
    for( i = 0, l = list; l != NULL; l = l->next )
        data->torrents[i++] = (tr_torrent*) l->data;
    assert( i == n );

    tr_list_free( &list, NULL );

    if( n )
        tr_inf( _( "Loaded %d torrents" ), n );

    if( data->setmeCount )
        *data->setmeCount = n;

    data->done = true;
}

tr_torrent **
tr_sessionLoadTorrents( tr_session * session,
                        tr_ctor    * ctor,
                        int        * setmeCount )
{
    struct sessionLoadTorrentsData data;

    data.session = session;
    data.ctor = ctor;
    data.setmeCount = setmeCount;
    data.torrents = NULL;
    data.done = false;

    tr_runInEventThread( session, sessionLoadTorrents, &data );
    while( !data.done )
        tr_wait_msec( 100 );

    return data.torrents;
}

/***
****
***/

void
tr_sessionSetPexEnabled( tr_session * session, bool enabled )
{
    assert( tr_isSession( session ) );

    session->isPexEnabled = enabled != 0;
}

bool
tr_sessionIsPexEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->isPexEnabled;
}

bool
tr_sessionAllowsDHT( const tr_session * session )
{
    return tr_sessionIsDHTEnabled( session );
}

bool
tr_sessionIsDHTEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->isDHTEnabled;
}

static void
toggleDHTImpl(  void * data )
{
    tr_session * session = data;
    assert( tr_isSession( session ) );

    tr_udpUninit( session );
    session->isDHTEnabled = !session->isDHTEnabled;
    tr_udpInit( session );
}

void
tr_sessionSetDHTEnabled( tr_session * session, bool enabled )
{
    assert( tr_isSession( session ) );
    assert( tr_isBool( enabled ) );

    if( ( enabled != 0 ) != ( session->isDHTEnabled != 0 ) )
        tr_runInEventThread( session, toggleDHTImpl, session );
}

/***
****
***/

bool
tr_sessionIsUTPEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

#ifdef WITH_UTP
    return session->isUTPEnabled;
#else
    return false;
#endif
}

static void
toggle_utp(  void * data )
{
    tr_session * session = data;
    assert( tr_isSession( session ) );

    session->isUTPEnabled = !session->isUTPEnabled;

    tr_udpSetSocketBuffers( session );

    /* But don't call tr_utpClose -- see reset_timer in tr-utp.c for an
       explanation. */
}

void
tr_sessionSetUTPEnabled( tr_session * session, bool enabled )
{
    assert( tr_isSession( session ) );
    assert( tr_isBool( enabled ) );

    if( ( enabled != 0 ) != ( session->isUTPEnabled != 0 ) )
        tr_runInEventThread( session, toggle_utp, session );
}

/***
****
***/

static void
toggleLPDImpl(  void * data )
{
    tr_session * session = data;
    assert( tr_isSession( session ) );

    if( session->isLPDEnabled )
        tr_lpdUninit( session );

    session->isLPDEnabled = !session->isLPDEnabled;

    if( session->isLPDEnabled )
        tr_lpdInit( session, &session->public_ipv4->addr );
}

void
tr_sessionSetLPDEnabled( tr_session * session, bool enabled )
{
    assert( tr_isSession( session ) );
    assert( tr_isBool( enabled ) );

    if( ( enabled != 0 ) != ( session->isLPDEnabled != 0 ) )
        tr_runInEventThread( session, toggleLPDImpl, session );
}

bool
tr_sessionIsLPDEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->isLPDEnabled;
}

bool
tr_sessionAllowsLPD( const tr_session * session )
{
    return tr_sessionIsLPDEnabled( session );
}

/***
****
***/

void
tr_sessionSetCacheLimit_MB( tr_session * session, int max_bytes )
{
    assert( tr_isSession( session ) );

    tr_cacheSetLimit( session->cache, toMemBytes( max_bytes ) );
}

int
tr_sessionGetCacheLimit_MB( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return toMemMB( tr_cacheGetLimit( session->cache ) );
}

/***
****
***/

struct port_forwarding_data
{
    bool enabled;
    struct tr_shared * shared;
};

static void
setPortForwardingEnabled( void * vdata )
{
    struct port_forwarding_data * data = vdata;
    tr_sharedTraversalEnable( data->shared, data->enabled );
    tr_free( data );
}

void
tr_sessionSetPortForwardingEnabled( tr_session  * session, bool enabled )
{
    struct port_forwarding_data * d;
    d = tr_new0( struct port_forwarding_data, 1 );
    d->shared = session->shared;
    d->enabled = enabled;
    tr_runInEventThread( session, setPortForwardingEnabled, d );
}

bool
tr_sessionIsPortForwardingEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_sharedTraversalIsEnabled( session->shared );
}

/***
****
***/

static int
tr_stringEndsWith( const char * str, const char * end )
{
    const size_t slen = strlen( str );
    const size_t elen = strlen( end );

    return slen >= elen && !memcmp( &str[slen - elen], end, elen );
}

static void
loadBlocklists( tr_session * session )
{
    int         binCount = 0;
    int         newCount = 0;
    struct stat sb;
    char      * dirname;
    DIR *       odir = NULL;
    tr_list *   list = NULL;
    const bool  isEnabled = session->isBlocklistEnabled;

    /* walk through the directory and find blocklists */
    dirname = tr_buildPath( session->configDir, "blocklists", NULL );
    if( !stat( dirname,
               &sb ) && S_ISDIR( sb.st_mode )
      && ( ( odir = opendir( dirname ) ) ) )
    {
        struct dirent *d;
        for( d = readdir( odir ); d; d = readdir( odir ) )
        {
            char * filename;

            if( !d->d_name || d->d_name[0] == '.' ) /* skip dotfiles, ., and ..
                                                      */
                continue;

            filename = tr_buildPath( dirname, d->d_name, NULL );

            if( tr_stringEndsWith( filename, ".bin" ) )
            {
                /* if we don't already have this blocklist, add it */
                if( !tr_list_find( list, filename,
                                   (TrListCompareFunc)strcmp ) )
                {
                    tr_list_append( &list,
                                   _tr_blocklistNew( filename, isEnabled ) );
                    ++binCount;
                }
            }
            else
            {
                /* strip out the file suffix, if there is one, and add ".bin"
                  instead */
                tr_blocklist * b;
                const char *   dot = strrchr( d->d_name, '.' );
                const int      len = dot ? dot - d->d_name
                                         : (int)strlen( d->d_name );
                char         * tmp = tr_strdup_printf(
                                        "%s" TR_PATH_DELIMITER_STR "%*.*s.bin",
                                        dirname, len, len, d->d_name );
                b = _tr_blocklistNew( tmp, isEnabled );
                _tr_blocklistSetContent( b, filename );
                tr_list_append( &list, b );
                ++newCount;
                tr_free( tmp );
            }

            tr_free( filename );
        }

        closedir( odir );
    }

    session->blocklists = list;

    if( binCount )
        tr_dbg( "Found %d blocklists in \"%s\"", binCount, dirname );
    if( newCount )
        tr_dbg( "Found %d new blocklists in \"%s\"", newCount, dirname );

    tr_free( dirname );
}

static void
closeBlocklists( tr_session * session )
{
    tr_list_free( &session->blocklists,
                  (TrListForeachFunc)_tr_blocklistFree );
}

void
tr_sessionReloadBlocklists( tr_session * session )
{
    closeBlocklists( session );
    loadBlocklists( session );

    tr_peerMgrOnBlocklistChanged( session->peerMgr );
}

int
tr_blocklistGetRuleCount( const tr_session * session )
{
    int       n = 0;
    tr_list * l;

    assert( tr_isSession( session ) );

    for( l = session->blocklists; l; l = l->next )
        n += _tr_blocklistGetRuleCount( l->data );
    return n;
}

bool
tr_blocklistIsEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->isBlocklistEnabled;
}

void
tr_blocklistSetEnabled( tr_session * session, bool isEnabled )
{
    tr_list * l;

    assert( tr_isSession( session ) );

    session->isBlocklistEnabled = isEnabled != 0;

    for( l=session->blocklists; l!=NULL; l=l->next )
        _tr_blocklistSetEnabled( l->data, isEnabled );
}

bool
tr_blocklistExists( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->blocklists != NULL;
}

int
tr_blocklistSetContent( tr_session * session, const char * contentFilename )
{
    tr_list * l;
    int ruleCount;
    tr_blocklist * b;
    const char * defaultName = DEFAULT_BLOCKLIST_FILENAME;
    tr_sessionLock( session );

    for( b = NULL, l = session->blocklists; !b && l; l = l->next )
        if( tr_stringEndsWith( _tr_blocklistGetFilename( l->data ),
                               defaultName ) )
            b = l->data;

    if( !b )
    {
        char * path = tr_buildPath( session->configDir, "blocklists", defaultName, NULL );
        b = _tr_blocklistNew( path, session->isBlocklistEnabled );
        tr_list_append( &session->blocklists, b );
        tr_free( path );
    }

    ruleCount = _tr_blocklistSetContent( b, contentFilename );
    tr_sessionUnlock( session );
    return ruleCount;
}

bool
tr_sessionIsAddressBlocked( const tr_session * session,
                            const tr_address * addr )
{
    tr_list * l;

    assert( tr_isSession( session ) );

    for( l = session->blocklists; l; l = l->next )
        if( _tr_blocklistHasAddress( l->data, addr ) )
            return true;
    return false;
}

void
tr_blocklistSetURL( tr_session * session, const char * url )
{
    if( session->blocklist_url != url )
    {
        tr_free( session->blocklist_url );
        session->blocklist_url = tr_strdup( url );
    }
}

const char *
tr_blocklistGetURL ( const tr_session * session )
{
    return session->blocklist_url;
}


/***
****
***/

static void
metainfoLookupInit( tr_session * session )
{
    struct stat  sb;
    const char * dirname = tr_getTorrentDir( session );
    DIR *        odir = NULL;
    tr_ctor *    ctor = NULL;
    tr_benc * lookup;
    int n = 0;

    assert( tr_isSession( session ) );

    /* walk through the directory and find the mappings */
    lookup = tr_new0( tr_benc, 1 );
    tr_bencInitDict( lookup, 0 );
    ctor = tr_ctorNew( session );
    tr_ctorSetSave( ctor, false ); /* since we already have them */
    if( !stat( dirname, &sb ) && S_ISDIR( sb.st_mode ) && ( ( odir = opendir( dirname ) ) ) )
    {
        struct dirent *d;
        while(( d = readdir( odir )))
        {
            if( tr_str_has_suffix( d->d_name, ".torrent" ) )
            {
                tr_info inf;
                char * path = tr_buildPath( dirname, d->d_name, NULL );
                tr_ctorSetMetainfoFromFile( ctor, path );
                if( !tr_torrentParse( ctor, &inf ) )
                {
                    ++n;
                    tr_bencDictAddStr( lookup, inf.hashString, path );
                }
                tr_free( path );
            }
        }
        closedir( odir );
    }
    tr_ctorFree( ctor );

    session->metainfoLookup = lookup;
    tr_dbg( "Found %d torrents in \"%s\"", n, dirname );
}

const char*
tr_sessionFindTorrentFile( const tr_session * session,
                           const char       * hashString )
{
    const char * filename = NULL;
    if( !session->metainfoLookup )
        metainfoLookupInit( (tr_session*)session );
    tr_bencDictFindStr( session->metainfoLookup, hashString, &filename );
    return filename;
}

void
tr_sessionSetTorrentFile( tr_session * session,
                          const char * hashString,
                          const char * filename )
{
    /* since we walk session->configDir/torrents/ to build the lookup table,
     * and tr_sessionSetTorrentFile() is just to tell us there's a new file
     * in that same directory, we don't need to do anything here if the
     * lookup table hasn't been built yet */
    if( session->metainfoLookup )
        tr_bencDictAddStr( session->metainfoLookup, hashString, filename );
}

/***
****
***/

void
tr_sessionSetRPCEnabled( tr_session * session, bool isEnabled )
{
    assert( tr_isSession( session ) );

    tr_rpcSetEnabled( session->rpcServer, isEnabled );
}

bool
tr_sessionIsRPCEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_rpcIsEnabled( session->rpcServer );
}

void
tr_sessionSetRPCPort( tr_session * session,
                      tr_port      port )
{
    assert( tr_isSession( session ) );

    tr_rpcSetPort( session->rpcServer, port );
}

tr_port
tr_sessionGetRPCPort( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_rpcGetPort( session->rpcServer );
}

void
tr_sessionSetRPCUrl( tr_session * session,
                     const char * url )
{
    assert( tr_isSession( session ) );

    tr_rpcSetUrl( session->rpcServer, url );
}

const char*
tr_sessionGetRPCUrl( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_rpcGetUrl( session->rpcServer );
}

void
tr_sessionSetRPCCallback( tr_session * session,
                          tr_rpc_func  func,
                          void *       user_data )
{
    assert( tr_isSession( session ) );

    session->rpc_func = func;
    session->rpc_func_user_data = user_data;
}

void
tr_sessionSetRPCWhitelist( tr_session * session,
                           const char * whitelist )
{
    assert( tr_isSession( session ) );

    tr_rpcSetWhitelist( session->rpcServer, whitelist );
}

const char*
tr_sessionGetRPCWhitelist( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_rpcGetWhitelist( session->rpcServer );
}

void
tr_sessionSetRPCWhitelistEnabled( tr_session * session, bool isEnabled )
{
    assert( tr_isSession( session ) );

    tr_rpcSetWhitelistEnabled( session->rpcServer, isEnabled );
}

bool
tr_sessionGetRPCWhitelistEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_rpcGetWhitelistEnabled( session->rpcServer );
}


void
tr_sessionSetRPCPassword( tr_session * session,
                          const char * password )
{
    assert( tr_isSession( session ) );

    tr_rpcSetPassword( session->rpcServer, password );
}

const char*
tr_sessionGetRPCPassword( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_rpcGetPassword( session->rpcServer );
}

void
tr_sessionSetRPCUsername( tr_session * session,
                          const char * username )
{
    assert( tr_isSession( session ) );

    tr_rpcSetUsername( session->rpcServer, username );
}

const char*
tr_sessionGetRPCUsername( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_rpcGetUsername( session->rpcServer );
}

void
tr_sessionSetRPCPasswordEnabled( tr_session * session, bool isEnabled )
{
    assert( tr_isSession( session ) );

    tr_rpcSetPasswordEnabled( session->rpcServer, isEnabled );
}

bool
tr_sessionIsRPCPasswordEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_rpcIsPasswordEnabled( session->rpcServer );
}

const char *
tr_sessionGetRPCBindAddress( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_rpcGetBindAddress( session->rpcServer );
}

/****
*****
****/

bool
tr_sessionIsTorrentDoneScriptEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->isTorrentDoneScriptEnabled;
}

void
tr_sessionSetTorrentDoneScriptEnabled( tr_session * session, bool isEnabled )
{
    assert( tr_isSession( session ) );
    assert( tr_isBool( isEnabled ) );

    session->isTorrentDoneScriptEnabled = isEnabled;
}

const char *
tr_sessionGetTorrentDoneScript( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->torrentDoneScript;
}

void
tr_sessionSetTorrentDoneScript( tr_session * session, const char * scriptFilename )
{
    assert( tr_isSession( session ) );

    if( session->torrentDoneScript != scriptFilename )
    {
        tr_free( session->torrentDoneScript );
        session->torrentDoneScript = tr_strdup( scriptFilename );
    }
}

/***
****
***/

void
tr_sessionSetQueueSize( tr_session * session, tr_direction dir, int n )
{
    assert( tr_isSession( session ) );
    assert( tr_isDirection( dir ) );

    session->queueSize[dir] = n;
}

int
tr_sessionGetQueueSize( const tr_session * session, tr_direction dir )
{
    assert( tr_isSession( session ) );
    assert( tr_isDirection( dir ) );

    return session->queueSize[dir];
}

void
tr_sessionSetQueueEnabled( tr_session * session, tr_direction dir, bool is_enabled )
{
    assert( tr_isSession( session ) );
    assert( tr_isDirection( dir ) );
    assert( tr_isBool( is_enabled ) );

    session->queueEnabled[dir] = is_enabled;
}

bool
tr_sessionGetQueueEnabled( const tr_session * session, tr_direction dir )
{
    assert( tr_isSession( session ) );
    assert( tr_isDirection( dir ) );

    return session->queueEnabled[dir];
}

void
tr_sessionSetQueueStalledMinutes( tr_session * session, int minutes )
{
    assert( tr_isSession( session ) );
    assert( minutes > 0 );

    session->queueStalledMinutes = minutes;
}

void
tr_sessionSetQueueStalledEnabled( tr_session * session, bool is_enabled )
{
    assert( tr_isSession( session ) );
    assert( tr_isBool( is_enabled ) );

    session->stalledEnabled = is_enabled;
}

bool
tr_sessionGetQueueStalledEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->stalledEnabled;
}

int
tr_sessionGetQueueStalledMinutes( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->queueStalledMinutes;
}

tr_torrent *
tr_sessionGetNextQueuedTorrent( tr_session * session, tr_direction direction )
{
    tr_torrent * tor = NULL;
    tr_torrent * best_tor = NULL;
    int best_position = INT_MAX;

    assert( tr_isSession( session ) );
    assert( tr_isDirection( direction ) );

    while(( tor = tr_torrentNext( session, tor )))
    {
        int position;

        if( !tr_torrentIsQueued( tor ) )
            continue;
        if( direction != tr_torrentGetQueueDirection( tor ) )
            continue;

        position = tr_torrentGetQueuePosition( tor );
        if( best_position > position ) {
            best_position = position;
            best_tor = tor;
        }
    }

    return best_tor;
}

int
tr_sessionCountQueueFreeSlots( tr_session * session, tr_direction dir )
{
    tr_torrent * tor;
    int active_count;
    const int max = tr_sessionGetQueueSize( session, dir );
    const tr_torrent_activity activity = dir == TR_UP ? TR_STATUS_SEED : TR_STATUS_DOWNLOAD;

    if( !tr_sessionGetQueueEnabled( session, dir ) )
        return INT_MAX;

    tor = NULL;
    active_count = 0;
    while(( tor = tr_torrentNext( session, tor )))
        if( !tr_torrentIsStalled( tor ) )
            if( tr_torrentGetActivity( tor ) == activity )
                ++active_count;

    if( active_count >= max )
        return 0;

    return max - active_count;
}
