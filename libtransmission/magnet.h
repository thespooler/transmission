/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#ifndef TR_MAGNET_H
#define TR_MAGNET_H 1

#include "transmission.h"

typedef struct
{
    uint8_t hash[20];
    char * displayName;
    char ** announceURLs;
    int announceCount;
}
tr_magnet_info;

tr_magnet_info * tr_magnetParse( const char * uri );

void tr_magnetFree( tr_magnet_info * info );

#endif
