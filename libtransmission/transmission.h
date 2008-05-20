/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
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

#ifndef TR_TRANSMISSION_H
#define TR_TRANSMISSION_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include "version.h"

#include <inttypes.h> /* uintN_t */
#ifndef PRId64
# define PRId64 "lld"
#endif
#ifndef PRIu64
# define PRIu64 "llu"
#endif
#include <time.h> /* time_t */

#define SHA_DIGEST_LENGTH 20
#ifdef __BEOS__
# include <StorageDefs.h>
# define MAX_PATH_LENGTH  B_FILE_NAME_LENGTH
#else
# define MAX_PATH_LENGTH  1024
#endif

typedef uint32_t tr_file_index_t;
typedef uint32_t tr_piece_index_t;
typedef uint64_t tr_block_index_t;

enum
{
    TR_PEER_FROM_INCOMING  = 0,  /* connections made to the listening port */
    TR_PEER_FROM_TRACKER   = 1,  /* peers received from a tracker */
    TR_PEER_FROM_CACHE     = 2,  /* peers read from the peer cache */
    TR_PEER_FROM_PEX       = 3,  /* peers discovered via PEX */
    TR_PEER_FROM__MAX
};

/***********************************************************************
 * tr_sessionInit
 ***********************************************************************
 * Initializes and returns an opaque libtransmission handle
 * to be passed to functions below. The tag argument is a short string
 * unique to the program invoking tr_sessionInit(), it is currently used
 * as part of saved torrent files' names to prevent one frontend from
 * deleting a torrent used by another. The following tags are used:
 *   beos cli daemon gtk macosx wx
 **********************************************************************/

typedef struct tr_handle tr_handle;

const char* tr_getDefaultConfigDir( void );

#define TR_DEFAULT_CONFIG_DIR                  tr_getDefaultConfigDir()
#define TR_DEFAULT_PEX_ENABLED                 1
#define TR_DEFAULT_PORT_FORWARDING_ENABLED     0
#define TR_DEFAULT_PORT                        51413
#define TR_DEFAULT_GLOBAL_PEER_LIMIT           200
#define TR_DEFAULT_PEER_SOCKET_TOS             8
#define TR_DEFAULT_BLOCKLIST_ENABLED           0
#define TR_DEFAULT_RPC_ENABLED                 0
#define TR_DEFAULT_RPC_PORT                    9091
#define TR_DEFAULT_RPC_PORT_STR                "9091"
#define TR_DEFAULT_RPC_ACL                     "+127.0.0.1"

tr_handle * tr_sessionInitFull( const char * configDir,
                                const char * downloadDir,
                                const char * tag,
                                int          isPexEnabled,
                                int          isPortForwardingEnabled,
                                int          publicPort,
                                int          encryptionMode,
                                int          isUploadLimitEnabled,
                                int          uploadLimit,
                                int          isDownloadLimitEnabled,
                                int          downloadLimit,
                                int          globalPeerLimit,
                                int          messageLevel,
                                int          isMessageQueueingEnabled,
                                int          isBlocklistEnabled,
                                int          peerSocketTOS,
                                int          rpcIsEnabled,
                                int          rpcPort,
                                const char * rpcACL );

/**
 * Like tr_sessionInitFull() but with default values supplied.
 */ 
tr_handle * tr_sessionInit( const char * configDir,
                            const char * downloadDir,
                            const char * tag );

/**
 * Shut down a libtransmission instance created by tr_sessionInit*()
 */
void tr_sessionClose( tr_handle * );

void tr_sessionSetDownloadDir( tr_handle *, const char * downloadDir );

const char * tr_sessionGetDownloadDir( const tr_handle * );

void tr_sessionSetRPCEnabled( tr_handle *, int isEnabled );

int tr_sessionIsRPCEnabled( const tr_handle * handle );

void tr_sessionSetRPCPort( tr_handle *, int port );

int tr_sessionGetRPCPort( const tr_handle * );

/**
 * Specify access control list (ACL). ACL is a comma separated list
 * of IP subnets, each subnet is prepended by ’-’ or ’+’ sign. Plus
 * means allow, minus means deny. If subnet mask is omitted, like
 * "-1.2.3.4", then it means single IP address. Mask may vary from 0
 * to 32 inclusive.
 *
 * The default string is "+127.0.0.1"
 *
 * IMPORTANT: a malformed ACL is likely to cause Transmission to crash.
 * Client applications need to validate user input, or better yet
 * generate it from a higher-level interface that doesn't allow user error,
 * before calling this function.
 */
void tr_sessionSetRPCACL( tr_handle *, const char * acl );

const char* tr_sessionGetRPCACL( const tr_handle * );

typedef enum
{
    TR_RPC_TORRENT_ADDED,
    TR_RPC_TORRENT_STARTED,
    TR_RPC_TORRENT_STOPPED,
    TR_RPC_TORRENT_CLOSING,
    TR_RPC_TORRENT_REMOVING,
    TR_RPC_TORRENT_CHANGED,
    TR_RPC_SESSION_CHANGED
}
tr_rpc_callback_type;

struct tr_torrent;

typedef void ( *tr_rpc_func )( tr_handle            * handle,
                               tr_rpc_callback_type   type,
                               struct tr_torrent    * tor_or_null,
                               void                 * user_data );

void tr_sessionSetRPCCallback( tr_handle    * handle,
                               tr_rpc_func    func,
                               void         * user_data );


/**
***
**/

typedef struct tr_session_stats
{
    uint64_t uploadedBytes;   /* total up */
    uint64_t downloadedBytes; /* total down */
    double ratio;             /* TR_RATIO_INF, TR_RATIO_NA, or total up/down */
    uint64_t filesAdded;      /* number of files added */
    uint64_t sessionCount;    /* program started N times */
    uint64_t secondsActive;   /* how long Transmisson's been running */
}
tr_session_stats;

/* stats from the current session. */
void tr_getSessionStats( const tr_handle   * handle,
                         tr_session_stats  * setme );

/* stats from the current and past sessions. */
void tr_getCumulativeSessionStats( const tr_handle   * handle,
                                   tr_session_stats  * setme );

void tr_clearSessionStats( tr_handle * handle );


/**
***
**/

typedef enum
{
    TR_PLAINTEXT_PREFERRED,
    TR_ENCRYPTION_PREFERRED,
    TR_ENCRYPTION_REQUIRED
}
tr_encryption_mode;

tr_encryption_mode tr_sessionGetEncryption( tr_handle * handle );

void tr_sessionSetEncryption( tr_handle * handle, tr_encryption_mode mode );

/***********************************************************************
 * tr_getPrefsDirectory
 ***********************************************************************
 * Returns the full path to a directory which can be used to store
 * preferences. The string belongs to libtransmission, do not free it.
 **********************************************************************/
const char * tr_sessionGetConfigDir( const tr_handle * );




/***********************************************************************
** Message Logging
*/

enum
{
    TR_MSG_ERR = 1,
    TR_MSG_INF = 2,
    TR_MSG_DBG = 3
};
void tr_setMessageLevel( int );
int tr_getMessageLevel( void );

typedef struct tr_msg_list
{
    /* TR_MSG_ERR, TR_MSG_INF, or TR_MSG_DBG */
    uint8_t level;

    /* Time the message was generated */
    time_t when;

    /* The torrent associated with this message,
     * or a module name such as "Port Forwarding" for non-torrent messages,
     * or NULL. */
    char * name;

    /* The message */
    char * message;

    /* The source file where this message originated */
    const char * file;

    /* The line number in the source file where this message originated */
    int line;

    /* linked list of messages */
    struct tr_msg_list * next;
}
tr_msg_list;

void tr_setMessageQueuing( int enable );

tr_msg_list * tr_getQueuedMessages( void );
void tr_freeMessageList( tr_msg_list * freeme );

/***********************************************************************
** Incoming Peer Connections Port
*/

void tr_sessionSetPortForwardingEnabled( tr_handle *, int enable );

int tr_sessionIsPortForwardingEnabled( const tr_handle * );

void tr_sessionSetPublicPort( tr_handle *, int );

int tr_sessionGetPublicPort( const tr_handle * );

typedef enum
{
    TR_NAT_TRAVERSAL_ERROR,
    TR_NAT_TRAVERSAL_UNMAPPED,
    TR_NAT_TRAVERSAL_UNMAPPING,
    TR_NAT_TRAVERSAL_MAPPING,
    TR_NAT_TRAVERSAL_MAPPED
}
tr_nat_traversal_status;

typedef struct tr_handle_status
{
    tr_nat_traversal_status natTraversalStatus;
    int publicPort;
}
tr_handle_status;

const tr_handle_status * tr_handleStatus( tr_handle * );


/***********************************************************************
***
***  TORRENTS
**/

typedef struct tr_torrent tr_torrent;

int tr_torrentCount( const tr_handle * h );

/**
 * Iterate through the torrents.
 * Pass in in a NULL pointer to get the first torrent.
 */
tr_torrent* tr_torrentNext( tr_handle *, tr_torrent * );

/***********************************************************************
*** Speed Limits
**/

enum { TR_UP, TR_DOWN };

typedef enum
{
    TR_SPEEDLIMIT_GLOBAL,    /* only follow the overall speed limit */
    TR_SPEEDLIMIT_SINGLE,    /* only follow the per-torrent limit */
    TR_SPEEDLIMIT_UNLIMITED  /* no limits at all */
}
tr_speedlimit;

void tr_torrentSetSpeedMode( tr_torrent   * tor,
                             int            up_or_down,
                             tr_speedlimit  mode );

tr_speedlimit tr_torrentGetSpeedMode( const tr_torrent  * tor,
                                      int                 up_or_down);

void tr_torrentSetSpeedLimit( tr_torrent   * tor,
                              int            up_or_down,
                              int            KiB_sec );

int tr_torrentGetSpeedLimit( const tr_torrent  * tor,
                             int                 up_or_down );

void tr_sessionSetSpeedLimitEnabled( tr_handle   * session,
                                     int           up_or_down,
                                     int           isEnabled );

int tr_sessionIsSpeedLimitEnabled( const tr_handle   * session,
                                   int                 up_or_down );

void tr_sessionSetSpeedLimit( tr_handle   * session,
                              int           up_or_down,
                              int           KiB_sec );

int tr_sessionGetSpeedLimit( const tr_handle   * session,
                             int                 up_or_down );


/***********************************************************************
***  Peer Limits
**/

void tr_torrentSetPeerLimit( tr_torrent  * tor,
                             uint16_t      peerLimit );

uint16_t tr_torrentGetPeerLimit( const tr_torrent  * tor );

void tr_sessionSetPeerLimit( tr_handle * handle,
                             uint16_t    maxGlobalPeers );

uint16_t tr_sessionGetPeerLimit( const tr_handle * handle );


/**
 * Specify a range of IPs for Transmission to block.
 *
 * filename must be an uncompressed ascii file,
 * using the same format as the bluetack level1 file.
 *
 * libtransmission does not keep a handle to `filename'
 * after this call returns, so the caller is free to
 * keep or delete `filename' as it wishes.
 * libtransmission makes its own copy of the file
 * massaged into a format easier to search.
 *
 * The caller only needs to invoke this when the blocklist
 * has changed.
 *
 * Passing NULL for a filename will clear the blocklist.
 */
int tr_blocklistSetContent( tr_handle  * handle,
                            const char * filename );

int tr_blocklistGetRuleCount( tr_handle * handle );

int tr_blocklistExists( const tr_handle * handle );

int tr_blocklistIsEnabled( const tr_handle * handle );

void tr_blocklistSetEnabled( tr_handle * handle,
                             int         isEnabled );

struct in_addr;

int tr_blocklistHasAddress( tr_handle             * handle,
                            const struct in_addr  * addr);


/***********************************************************************
 * Torrent Priorities
 **********************************************************************/

enum
{
    TR_PRI_LOW    = -1,
    TR_PRI_NORMAL =  0, /* since NORMAL is 0, memset initializes nicely */
    TR_PRI_HIGH   =  1
};

typedef int8_t tr_priority_t;

/* set a batch of files to a particular priority.
 * priority must be one of TR_PRI_NORMAL, _HIGH, or _LOW */
void tr_torrentSetFilePriorities( tr_torrent        * tor,
                                  tr_file_index_t   * files,
                                  tr_file_index_t     fileCount,
                                  tr_priority_t       priority );

/* returns a malloc()ed array of tor->info.fileCount items,
 * each holding a value of TR_PRI_NORMAL, _HIGH, or _LOW.
   free the array when done. */
tr_priority_t* tr_torrentGetFilePriorities( const tr_torrent * );

/* single-file form of tr_torrentGetFilePriorities.
 * returns one of TR_PRI_NORMAL, _HIGH, or _LOW. */
tr_priority_t tr_torrentGetFilePriority( const tr_torrent *, tr_file_index_t file );

/* returns true if the file's `download' flag is set */
int tr_torrentGetFileDL( const tr_torrent *, tr_file_index_t file );

/* set a batch of files to be downloaded or not. */
void tr_torrentSetFileDLs ( tr_torrent      * tor,
                            tr_file_index_t * files,
                            tr_file_index_t   fileCount,
                            int               do_download );

/***********************************************************************
 * tr_torrentRates
 ***********************************************************************
 * Gets the total download and upload rates
 **********************************************************************/
void tr_torrentRates( tr_handle *, float *, float * );



/**
 *  Torrent Instantiation
 *
 *  Instantiating a tr_torrent had gotten more complicated as features were
 *  added.  At one point there were four functions to check metainfo and five
 *  to create tr_torrent.
 *
 *  To remedy this, a Torrent Constructor (struct tr_ctor) has been introduced:
 *  + Simplifies the API down to two (non-deprecated) functions.
 *  + You can set the fields you want; the system sets defaults for the rest.
 *  + You can specify whether or not your fields should supercede resume's.
 *  + We can add new features to tr_ctor without breaking tr_torrentNew()'s API.
 *
 *  All the tr_ctor{Get,Set}*() functions with a return value return 
 *  an error number, or zero if no error occurred.
 *
 *  You must call one of the SetMetainfo() functions before creating
 *  a torrent with a tr_ctor.  The other functions are optional.
 *
 *  You can reuse a single tr_ctor to create a batch of torrents --
 *  just call one of the SetMetainfo() functions between each
 *  tr_torrentNew() call.
 *
 *  Every call to tr_ctorSetMetainfo*() frees the previous metainfo.
 */

typedef enum
{
    TR_FALLBACK, /* indicates the ctor value should be used only
                    in case of missing resume settings */

    TR_FORCE, /* indicates the ctor value should be used
                 regardless of what's in the resume settings */
}
tr_ctorMode;

typedef struct tr_ctor tr_ctor;
struct tr_benc;

tr_ctor* tr_ctorNew                    ( const tr_handle  * handle);

void     tr_ctorFree                   ( tr_ctor        * ctor );

void     tr_ctorSetSave                ( tr_ctor        * ctor,
                                         int     saveMetadataInOurTorrentsDir );

void     tr_ctorSetDeleteSource       ( tr_ctor        * ctor,
                                         uint8_t          doDelete );

int      tr_ctorSetMetainfo            ( tr_ctor        * ctor,
                                         const uint8_t  * metainfo,
                                         size_t           len );

int      tr_ctorSetMetainfoFromFile    ( tr_ctor        * ctor,
                                         const char     * filename );

int      tr_ctorSetMetainfoFromHash    ( tr_ctor        * ctor,
                                         const char     * hashString );

void     tr_ctorSetPeerLimit           ( tr_ctor        * ctor,
                                         tr_ctorMode      mode,
                                         uint16_t         peerLimit  );

void     tr_ctorSetDownloadDir         ( tr_ctor        * ctor,
                                         tr_ctorMode      mode,
                                         const char     * directory );

void     tr_ctorSetPaused              ( tr_ctor        * ctor,
                                         tr_ctorMode      mode,
                                         uint8_t          isPaused );

int      tr_ctorGetPeerLimit           ( const tr_ctor  * ctor,
                                         tr_ctorMode      mode,
                                         uint16_t       * setmeCount );

int      tr_ctorGetPaused              ( const tr_ctor  * ctor,
                                         tr_ctorMode      mode,
                                         uint8_t        * setmeIsPaused );

int      tr_ctorGetDownloadDir         ( const tr_ctor  * ctor,
                                         tr_ctorMode      mode,
                                         const char    ** setmeDownloadDir );

int      tr_ctorGetMetainfo            ( const tr_ctor  * ctor,
                                         const struct tr_benc ** setme );

int      tr_ctorGetSave                ( const tr_ctor  * ctor );
 
int      tr_ctorGetDeleteSource        ( const tr_ctor  * ctor,
                                         uint8_t        * setmeDoDelete );

/* returns NULL if tr_ctorSetMetainfoFromFile() wasn't used */
const char* tr_ctorGetSourceFile       ( const tr_ctor  * ctor );

/**
 * Returns this torrent's unique ID.
 * IDs are allocated when the torrent is constructed and are
 * good until tr_sessionClose() is called.
 */
int tr_torrentId( const tr_torrent * );

typedef struct tr_info tr_info;

/**
 * Parses the specified metainfo.
 * Returns TR_OK if it parsed and can be added to Transmission.
 * Returns TR_EINVALID if it couldn't be parsed.
 * Returns TR_EDUPLICATE if it parsed but can't be added. 
 *     "download-dir" must be set to test for TR_EDUPLICATE.
 *
 * If setme_info is non-NULL and parsing is successful
 * (that is, if TR_EINVALID is not returned), then the parsed
 * metainfo is stored in setme_info and should be freed by the
 * caller via tr_metainfoFree().
 */
int tr_torrentParse( const tr_handle  * handle,
                     const tr_ctor    * ctor,
                     tr_info          * setme_info_or_NULL );

/**
 * Instantiate a single torrent.
 */
#define TR_EINVALID     1
#define TR_EDUPLICATE   2
tr_torrent * tr_torrentNew( tr_handle      * handle,
                            const tr_ctor  * ctor,
                            int            * setmeError );


/**
 *  Load all the torrents in tr_getTorrentDir().
 *  This can be used at startup to kickstart all the torrents
 *  from the previous session.
 */
tr_torrent ** tr_sessionLoadTorrents ( tr_handle  * h,
                                       tr_ctor    * ctor,
                                       int        * setmeCount );



/**
 * Set whether or not torrents are allowed to do peer exchanges.
 * PEX is always disabled in private torrents regardless of this.
 * In public torrents, PEX is enabled by default.
 */
void tr_sessionSetPexEnabled( tr_handle *, int isEnabled );

int tr_sessionIsPexEnabled( const tr_handle * );

const tr_info * tr_torrentInfo( const tr_torrent * );

void tr_torrentSetDownloadDir( tr_torrent *, const char * );

const char * tr_torrentGetDownloadDir( const tr_torrent * );

void tr_torrentStart( tr_torrent * );

void tr_torrentStop( tr_torrent * );


/**
***
**/

typedef enum
{
    TR_CP_INCOMPLETE,   /* doesn't have all the desired pieces */
    TR_CP_DONE,         /* has all the pieces but the DND ones */
    TR_CP_COMPLETE      /* has every piece */
}
cp_status_t;

typedef void (tr_torrent_status_func)(tr_torrent   * torrent,
                                      cp_status_t    status,
                                      void         * user_data );

/**
 * Register to be notified whenever a torrent's state changes.
 *
 * func is invoked FROM LIBTRANSMISSION'S THREAD!
 * This means func must be fast (to avoid blocking peers),
 * shouldn't call libtransmission functions (to avoid deadlock),
 * and shouldn't modify client-level memory without using a mutex!
 */
void tr_torrentSetStatusCallback( tr_torrent             * torrent,
                                  tr_torrent_status_func   func,
                                  void                   * user_data );

void tr_torrentClearStatusCallback( tr_torrent * torrent );



/**
***
**/

typedef void (tr_torrent_active_func)(tr_torrent   * torrent,
                                      int            isRunning,
                                      void         * user_data );

/**
 * Register to be notified whenever a torrent starts or stops.
 *
 * func is invoked FROM LIBTRANSMISSION'S THREAD!
 * This means func must be fast (to avoid blocking peers),
 * shouldn't call libtransmission functions (to avoid deadlock),
 * and shouldn't modify client-level memory without using a mutex!
 */
void tr_torrentSetActiveCallback( tr_torrent             * torrent,
                                  tr_torrent_active_func   func,
                                  void                   * user_data );

void tr_torrentClearActiveCallback( tr_torrent * torrent );


/**
 * MANUAL ANNOUNCE
 *
 * Trackers usually set an announce interval of 15 or 30 minutes.
 * Users can send one-time announce requests that override this
 * interval by calling tr_manualUpdate().
 *
 * The wait interval for tr_manualUpdate() is much smaller.
 * You can test whether or not a manual update is possible
 * (for example, to desensitize the button) by calling
 * tr_torrentCanManualUpdate().
 */

void tr_manualUpdate( tr_torrent * );

int tr_torrentCanManualUpdate( const tr_torrent * );

/***********************************************************************
 * tr_torrentStat
 ***********************************************************************
 * Returns a pointer to an tr_stat structure with updated information
 * on the torrent. The structure belongs to libtransmission (do not
 * free it) and is guaranteed to be unchanged until the next call to
 * tr_torrentStat.
 * The interface should call this function every second or so in order
 * to update itself.
 **********************************************************************/
typedef struct tr_stat tr_stat;
const tr_stat * tr_torrentStat( tr_torrent * );
const tr_stat * tr_torrentStatCached( tr_torrent * );

/***********************************************************************
 * tr_torrentPeers
 ***********************************************************************/
typedef struct tr_peer_stat tr_peer_stat;
tr_peer_stat * tr_torrentPeers( const tr_torrent *, int * peerCount );
void tr_torrentPeersFree( tr_peer_stat *, int peerCount );

typedef struct tr_file_stat tr_file_stat;
tr_file_stat * tr_torrentFiles( const tr_torrent *, tr_file_index_t * fileCount );
void tr_torrentFilesFree( tr_file_stat *, tr_file_index_t fileCount );


/***********************************************************************
 * tr_torrentAvailability
 ***********************************************************************
 * Use this to draw an advanced progress bar which is 'size' pixels
 * wide. Fills 'tab' which you must have allocated: each byte is set
 * to either -1 if we have the piece, otherwise it is set to the number
 * of connected peers who have the piece.
 **********************************************************************/
void tr_torrentAvailability( const tr_torrent *, int8_t * tab, int size );

void tr_torrentAmountFinished( const tr_torrent * tor, float * tab, int size );

/***********************************************************************
 * tr_torrentRemoveSaved
 ***********************************************************************
 * delete's Transmission's copy of the torrent's metadata from
 * tr_getTorrentDir().
 **********************************************************************/
void tr_torrentRemoveSaved( tr_torrent * );

void tr_torrentVerify( tr_torrent * );

/**
 * Frees memory allocated by tr_torrentNew().
 * Running torrents are stopped first.
 */
void tr_torrentClose( tr_torrent * );

/**
 * Like tr_torrentClose() but also deletes
 * the resume file and our copy of the
 * torrent file
 */
void tr_torrentRemove( tr_torrent * );

/***********************************************************************
 * tr_info
 **********************************************************************/

typedef struct tr_file
{
    uint64_t          length;      /* Length of the file, in bytes */
    char            * name;        /* Path to the file */
    int8_t            priority;    /* TR_PRI_HIGH, _NORMAL, or _LOW */
    int8_t            dnd;         /* nonzero if the file shouldn't be downloaded */
    tr_piece_index_t  firstPiece;  /* We need pieces [firstPiece... */
    tr_piece_index_t  lastPiece;   /* ...lastPiece] to dl this file */
    uint64_t          offset;      /* file begins at the torrent's nth byte */
}
tr_file;

typedef struct tr_piece
{
    uint8_t  hash[SHA_DIGEST_LENGTH];  /* pieces hash */
    int8_t   priority;                 /* TR_PRI_HIGH, _NORMAL, or _LOW */
    int8_t   dnd;                      /* nonzero if the piece shouldn't be downloaded */
}
tr_piece;
    
typedef struct tr_tracker_info
{
    int    tier;
    char * announce;
    char * scrape;
}
tr_tracker_info;

struct tr_info
{
    /* Path to torrent */
    char               * torrent;

    /* General info */
    uint8_t              hash[SHA_DIGEST_LENGTH];
    char                 hashString[2*SHA_DIGEST_LENGTH+1];
    char               * name;

    /* Flags */
    unsigned int isPrivate : 1;
    unsigned int isMultifile : 1;

    /* these trackers are sorted by tier */
    tr_tracker_info    * trackers;
    int                  trackerCount;

    /* Torrent info */
    char               * comment;
    char               * creator;
    time_t               dateCreated;

    /* Pieces info */
    uint32_t             pieceSize;
    tr_piece_index_t     pieceCount;
    uint64_t             totalSize;
    tr_piece           * pieces;

    /* Files info */
    tr_file_index_t      fileCount;
    tr_file            * files;
};

typedef enum
{
    TR_STATUS_CHECK_WAIT   = (1<<0), /* Waiting in queue to check files */
    TR_STATUS_CHECK        = (1<<1), /* Checking files */
    TR_STATUS_DOWNLOAD     = (1<<2), /* Downloading */
    TR_STATUS_SEED         = (1<<3), /* Seeding */
    TR_STATUS_STOPPED      = (1<<4)  /* Torrent is stopped */
}
tr_torrent_status;

#define TR_STATUS_IS_ACTIVE(s) ((s) != TR_STATUS_STOPPED)

/**
 * Transmission error codes
 * errors are always negative, and 0 refers to no error.
 */
typedef enum tr_errno
{
    TR_OK = 0,

    /* general errors */
    TR_ERROR = -100,
    TR_ERROR_ASSERT,

    /* io errors */
    TR_ERROR_IO_PARENT = -200,
    TR_ERROR_IO_PERMISSIONS,
    TR_ERROR_IO_SPACE,
    TR_ERROR_IO_FILE_TOO_BIG,
    TR_ERROR_IO_OPEN_FILES,
    TR_ERROR_IO_DUP_DOWNLOAD,
    TR_ERROR_IO_CHECKSUM,
    TR_ERROR_IO_OTHER,

    /* tracker errors */
    TR_ERROR_TC_ERROR = -300,
    TR_ERROR_TC_WARNING,

    /* peer errors */
    TR_ERROR_PEER_MESSAGE = -400
}
tr_errno;

#define TR_ERROR_IS_IO(e) (TR_ERROR_IO_PARENT<=(e) && (e)<=TR_ERROR_IO_OTHER)
#define TR_ERROR_IS_TC(e) (TR_ERROR_TC_ERROR<=(e) && (e)<=TR_ERROR_TC_WARNING)

struct tr_tracker_stat
{
    /* This is the unmodified string returned by the tracker in response
     * to the torrent's most recent scrape request.  If no request was
     * sent or there was no response, this string is empty. */
    char scrapeResponse[256];

    /* The unmodified string returned by the tracker in response
     * to the torrent's most recent scrape request.  If no request was
     * sent or there was no response, this string is empty. */
    char announceResponse[256];

    /* Time the most recent scrape request was sent,
     * or zero if one hasn't been sent yet. */
    time_t lastScrapeTime;

    /* Time when the next scrape request will be sent.
     * This value is always a valid time. */
    time_t nextScrapeTime;

    /* Time the most recent announce request was sent,
     * or zero if one hasn't been sent yet. */
    time_t lastAnnounceTime;

    /* Time when the next reannounce request will be sent,
     * or zero if the torrent is stopped. */
    time_t nextAnnounceTime;

    /* When the tracker will allow a human-driven "manual" announce to be sent,
     * derived from the "min interval" field given by the tracker.
     * This value is 0 when the torrent is stopped.
     * This value is ~(time_t)0 if the tracker returned a serious error.
     * Otherwise, the value is a valid time.
     * @see tr_manualUpdate( tr_torrent * );
     * @see tr_torrentCanManualUpdate( const tr_torrent * ); */
    time_t nextManualAnnounceTime;
};

tr_torrent_status tr_torrentGetStatus( tr_torrent * );

struct tr_stat
{
    tr_torrent_status status;

    struct tr_tracker_stat trackerStat;
    const tr_tracker_info * tracker;

    tr_errno error;
    char errorString[128];

    /* [0..1] */
    float recheckProgress;

    /* [0..1] */
    float percentComplete;

    /* [0..1] */
    float percentDone;

    /* KiB/s */
    float rateDownload;

    /* KiB/s */
    float rateUpload;

 #define TR_ETA_NOT_AVAIL -1
 #define TR_ETA_UNKNOWN -2
    /* seconds */
    int eta;

    int peersKnown;
    int peersConnected;
    int peersFrom[TR_PEER_FROM__MAX];
    int peersSendingToUs;
    int peersGettingFromUs;
    int seeders;
    int leechers;
    int completedFromTracker;

    /* if the torrent is running, this is the time at which
     * the client can manually ask the torrent's tracker
     * for more peers.  otherwise, the value is zero. */
    time_t manualAnnounceTime;

    /* Byte count of all the piece data we'll have downloaded when we're done.
     * whether or not we have it yet. [0...tr_info.totalSize] */
    uint64_t sizeWhenDone;

    /* Byte count of how much data is left to be downloaded until
     * we're done -- that is, until we've got all the pieces we wanted.
     * [0...tr_info.sizeWhenDone] */
    uint64_t leftUntilDone;

    /* Byte count of all the piece data we want and don't have yet,
     * but that a connected peer does have. [0...leftUntilDone] */
    uint64_t desiredAvailable;

    /* Byte count of all the corrupt data you've ever downloaded for
     * this torrent.  If you're on a poisoned torrent, this number can
     * grow very large. */
    uint64_t corruptEver;

    /* Byte count of all data you've ever uploaded for this torrent. */
    uint64_t uploadedEver;

    /* Byte count of all the non-corrupt data you've ever downloaded
     * for this torrent.  If you deleted the files and downloaded a second time,
     * this will be 2*totalSize.. */
    uint64_t downloadedEver;

    /* Byte count of all the checksum-verified data we have for this torrent. */
    uint64_t haveValid;

    /* Byte count of all the partial piece data we have for this torrent.
     * As pieces become complete, this value may decrease as portions of it are
     * moved to `corrupt' or `haveValid'. */
    uint64_t haveUnchecked;

    float swarmSpeed;

#define TR_RATIO_NA  -1
#define TR_RATIO_INF -2
    /* TR_RATIO_INF, TR_RATIO_NA, or a regular ratio */
    float ratio;
    
    uint64_t startDate;
    uint64_t activityDate;
};

struct tr_file_stat
{
    uint64_t bytesCompleted;
    float progress;
};

struct tr_peer_stat
{
    char addr[16];
    char client[80];
    
    unsigned int isEncrypted : 1;
    unsigned int isDownloadingFrom : 1;
    unsigned int isUploadingTo : 1;

    unsigned int peerIsChoked : 1;
    unsigned int peerIsInterested : 1;
    unsigned int clientIsChoked : 1;
    unsigned int clientIsInterested : 1;
    unsigned int isIncoming : 1;

    char flagStr[32];

    uint8_t  from;
    uint16_t port;
    
    float progress;
    float downloadFromRate;
    float uploadToRate;
};


#ifdef __TRANSMISSION__
#  include "session.h"
#endif

#ifdef __cplusplus
}
#endif

#endif
