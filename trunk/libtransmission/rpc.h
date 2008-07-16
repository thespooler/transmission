/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef TR_RPC_H
#define TR_RPC_H

/***
****  RPC processing
***/

enum
{
    TR_RPC_TORRENT_ACTIVITY        = (1<<0),
    TR_RPC_TORRENT_ANNOUNCE        = (1<<1),
    TR_RPC_TORRENT_ERROR           = (1<<2),
    TR_RPC_TORRENT_FILES           = (1<<3),
    TR_RPC_TORRENT_HISTORY         = (1<<4),
    TR_RPC_TORRENT_ID              = (1<<5),
    TR_RPC_TORRENT_INFO            = (1<<6),
    TR_RPC_TORRENT_LIMITS          = (1<<7),
    TR_RPC_TORRENT_PEERS           = (1<<8),
    TR_RPC_TORRENT_PEER_STATS      = (1<<9),
    TR_RPC_TORRENT_PRIORITIES      = (1<<10),
    TR_RPC_TORRENT_SCRAPE          = (1<<11),
    TR_RPC_TORRENT_SIZE            = (1<<12),
    TR_RPC_TORRENT_TRACKER_STATS   = (1<<13),
    TR_RPC_TORRENT_TRACKERS        = (1<<14),
    TR_RPC_TORRENT_WEBSEEDS        = (1<<15)
};

struct tr_benc;
struct tr_handle;

/* http://www.json.org/ */
char*
tr_rpc_request_exec_json( struct tr_handle  * handle,
                          const void        * request_json,
                          int                 request_len,
                          int               * response_len );

/* see the RPC spec's "Request URI Notation" section */
char*
tr_rpc_request_exec_uri( struct tr_handle  * handle,
                         const void        * request_uri,
                         int                 request_len,
                         int               * response_len );

void
tr_rpc_parse_list_str( struct tr_benc  * setme,
                       const char      * list_str,
                       size_t            list_str_len );


#endif
