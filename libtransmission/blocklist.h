/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#ifndef TR_BLOCKLIST_H
#define TR_BLOCKLIST_H

typedef struct tr_blocklist tr_blocklist;

tr_blocklist * tr_blocklistNew( int isEnabled );

void tr_blocklistFree( tr_blocklist * );


struct tr_handle;
struct in_addr;

int tr_peerIsBlocked( const struct tr_handle *,
                      const struct in_addr   * );

#endif
