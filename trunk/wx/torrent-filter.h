/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 */

#ifndef __XMISSION_TORRENT_FILTER_H__
#define __XMISSION_TORRENT_FILTER_H__

#include <vector>
#include <wx/intl.h>
#include <libtransmission/transmission.h>

class TorrentFilter
{
    public:

        typedef std::vector<tr_torrent_t*> torrents_v;

        enum ShowFlags
        {
            FLAG_SEEDING    = (1<<0),
            FLAG_LEECHING   = (1<<1),
            FLAG_STOPPED    = (1<<2),

            FLAG_ACTIVE     = (1<<3),
            FLAG_IDLE       = (1<<4),

            FLAG_DONE       = (1<<5),
            FLAG_NOT_DONE   = (1<<6)
        };

        enum Show {
            SEEDING, LEECHING, STOPPED,
            ACTIVE, IDLE,
            DONE, NOT_DONE,
            N_FILTERS
        };

        static int GetFlags( const tr_torrent_t * );

        static void CountHits( const torrents_v & torrents,
                               int              * counts );

        static wxString GetName( int show, int count=0 );

        static void RemoveFailures( int           show,
                                    torrents_v  &  torrents );
};


#endif
