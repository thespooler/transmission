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

static int peerCmp( tr_peer_t * peer1, tr_peer_t * peer2 )
{
    /* Wait until we got the peers' ids */
    if( peer1->status <= PEER_STATUS_HANDSHAKE ||
        peer2->status <= PEER_STATUS_HANDSHAKE )
    {
        return 1;
    }

    return memcmp( peer1->id, peer2->id, 20 );
}

static int checkPeer( tr_peer_t * peer )
{
	tr_torrent_t * tor = peer->tor;
    
	uint64_t now;
	int      idleTime, peersWanted, percentOfRange;
	int      ret;
	
	now = tr_date();
	idleTime = now - peer->date;
	
	/* assume any peer over with an idleTime lower than 
		8 seconds has not timed out */
	if ( idleTime > MIN_CON_TIMEOUT )
	{
		peersWanted = ( TR_MAX_PEER_COUNT * PERCENT_PEER_WANTED ) / 100;
		if ( tor->peerCount > peersWanted )
		{
			/* strict requirements for connecting timeout */
			if ( peer->status < PEER_STATUS_CONNECTED )
			{
				peer_dbg( "connection timeout, idled %i seconds", 
						  ( idleTime / 1000 ) );
				return TR_ERROR;
			}
			
			/* strict requirements for idle uploading timeout */
			if ( peer->inRequestCount && idleTime > MIN_UPLOAD_IDLE )
			{
				peer_dbg( "idle uploader timeout, idled %i seconds", 
						  ( idleTime / 1000 ) );
				return TR_ERROR;
			}
			
			/* strict requirements for keep-alive timeout */
			if ( idleTime > MIN_KEEP_ALIVE )
			{
				peer_dbg( "peer timeout, idled %i seconds", 
						  ( idleTime / 1000 ) );
				return TR_ERROR;
			}
		} 
		/* if we are tight for peers, relax the enforcement of timeouts */
		else 
		{
			percentOfRange = tor->peerCount / (TR_MAX_PEER_COUNT - peersWanted);
			
			/* relax requirements for connecting timeout */
			if ( peer->status < PEER_STATUS_CONNECTED && idleTime > MIN_CON_TIMEOUT + 
				 ( MAX_CON_TIMEOUT - MIN_CON_TIMEOUT ) * percentOfRange )
			{
				peer_dbg( "connection timeout, idled %i seconds", 
						  ( idleTime / 1000 ) );
				return TR_ERROR;
			} 
					
			/* relax requirements for idle uploading timeout */
			if ( peer->inRequestCount && idleTime >  MIN_UPLOAD_IDLE + 
				 ( MAX_UPLOAD_IDLE - MIN_UPLOAD_IDLE ) * percentOfRange )
			{
				peer_dbg( "idle uploader timeout, idled %i seconds", 
						  ( idleTime / 1000 ) );
				return TR_ERROR;
			}
					
			/* relax requirements for keep-alive timeout */
			if ( idleTime >  MIN_KEEP_ALIVE + 
				 ( MAX_KEEP_ALIVE - MIN_KEEP_ALIVE ) * percentOfRange )
			{
				peer_dbg( "peer timeout, idled %i seconds", 
						  ( idleTime / 1000 ) );
				return TR_ERROR;
			}
		}
	}

    if( PEER_STATUS_CONNECTED == peer->status )
    {
        /* Send keep-alive every 1 minute and 45 seconds */
        if( now > peer->keepAlive + 105000 )
        {
            sendKeepAlive( peer );
            peer->keepAlive = now;
        }

        /* Resend extended handshake if our public port changed */
        if( EXTENDED_HANDSHAKE == peer->extStatus && 
            tor->publicPort != peer->advertisedPort )
        {
            sendExtended( tor, peer, EXTENDED_HANDSHAKE_ID );
        }

        /* Send peer list */
        if( !peer->private && 0 < peer->pexStatus )
        {
            if( 0 == peer->lastPex )
            {
                /* randomize time when first pex message is sent */
                peer->lastPex = now - 1000 * tr_rand( PEX_INTERVAL );
            }
            if( now > peer->lastPex + 1000 * PEX_INTERVAL )
            {
                if( EXTENDED_HANDSHAKE == peer->extStatus )
                {
                    ret = sendExtended( tor, peer, EXTENDED_PEX_ID );
                }
                else if( peer->azproto )
                {
                    ret = sendAZPex( tor, peer );
                }
                else
                {
                    assert( 0 );
                    ret = TR_ERROR;
                }
                if( ret )
                {
                    return ret;
                }
                peer->lastPex = now + 1000 *
                    ( PEX_INTERVAL + tr_rand( PEX_INTERVAL / 10 ) );
            }
        }
    }

    return TR_OK;
}

static int isPieceInteresting( const tr_torrent_t  * tor,
                               const tr_peer_t     * peer,
                               int                   piece )
{
    if( tor->info.pieces[piece].dnd ) /* we don't want it */
        return 0;

    if( tr_cpPieceIsComplete( tor->completion, piece ) ) /* we already have it */
        return 0;

    if( !tr_bitfieldHas( peer->bitfield, piece ) ) /* peer doesn't have it */
        return 0;

    if( tr_bitfieldHas( peer->banfield, piece ) ) /* peer is banned for it */
        return 0;

    return 1;
}

/***********************************************************************
 * isInteresting
 ***********************************************************************
 * Returns 1 if 'peer' has at least one piece that we want but
 * haven't completed, or 0 otherwise.
 **********************************************************************/
static int isInteresting( const tr_torrent_t * tor, const tr_peer_t * peer )
{
    int i;
    const tr_bitfield_t * bitfield = tr_cpPieceBitfield( tor->completion );

    if( !peer->bitfield )
    {
        /* We don't know what this peer has */
        return 0;
    }

    assert( bitfield->len == peer->bitfield->len );

    for( i=0; i<tor->info.pieceCount; ++i )
        if( isPieceInteresting( tor, peer, i ) )
            return 1;

    return 0;
}

static void
updateInterest( tr_torrent_t * tor, tr_peer_t * peer )
{
    const int i = !!isInteresting( tor, peer );

    if( i != peer->isInteresting )
        sendInterest( peer, i );
}

/** utility structure used by getPreferredPieces() and comparePieces() */
typedef struct
{
    int piece;
    tr_priority_t priority;
    int missingBlockCount;
    int peerCount;
}
PieceCompareData;

/** utility function used by getPreferredPieces */
int comparePieces (const void * aIn, const void * bIn)
{
    const PieceCompareData * a = (const PieceCompareData*) aIn;
    const PieceCompareData * b = (const PieceCompareData*) bIn;

    /* if one piece has a higher priority, it goes first */
    if (a->priority != b->priority)
        return a->priority > b->priority ? -1 : 1;

    /* otherwise if one has fewer missing blocks, it goes first */
    if (a->missingBlockCount != b->missingBlockCount)
        return a->missingBlockCount < b->missingBlockCount ? -1 : 1;

    /* otherwise if one has fewer peers, it goes first */
    if (a->peerCount != b->peerCount)
        return a->peerCount < b->peerCount ? -1 : 1;

    /* otherwise go with the earlier piece */
    return a->piece - b->piece;
}

static int* getPreferredPieces( const tr_torrent_t  * tor,
                                const tr_peer_t     * peer,
                                int                 * pieceCount,
                                int                 * isEndgame )
{
    const tr_info_t * inf = &tor->info;

    int i;
    int poolSize = 0;
    int endgame = FALSE;
    int * pool = tr_new( int, inf->pieceCount );

    for( i=0; i<inf->pieceCount; ++i )
        if( isPieceInteresting( tor, peer, i ) )
            if( tr_cpMissingBlocksForPiece( tor->completion, i ) )
                pool[poolSize++] = i;

    if( !poolSize ) {
        endgame = TRUE;
        for( i=0; i<inf->pieceCount; ++i )
            if( isPieceInteresting( tor, peer, i ) )
                pool[poolSize++] = i;
    }

#if 0
fprintf (stderr, "old pool: ");
for (i=0; i<15 && i<poolSize; ++i ) fprintf (stderr, "%d, ", pool[i] );
fprintf (stderr, "\n");
#endif

    /* sort the rest from most interesting to least...
       but not in endgame, because it asks for pieces in a
       scattershot manner anyway and doesn't need them sorted */
    if( !endgame && ( poolSize > 1 ) )
    {
        PieceCompareData * p = tr_new( PieceCompareData, poolSize );

        for( i=0; i<poolSize; ++i )
        {
            int j;
            const int piece = pool[i];

            p[i].piece = piece;
            p[i].priority = inf->pieces[piece].priority;
            p[i].missingBlockCount = tr_cpMissingBlocksForPiece( tor->completion, piece );
            p[i].peerCount = 0;

            for( j=0; j<tor->peerCount; ++j )
                if( tr_bitfieldHas( tor->peers[j]->bitfield, piece ) )
                    ++p[i].peerCount;
        }

        qsort (p, poolSize, sizeof(PieceCompareData), comparePieces);

        for( i=0; i<poolSize; ++i )
            pool[i] = p[i].piece;

        tr_free( p );
    }

#if 0
fprintf (stderr, "new pool: ");
for (i=0; i<15 && i<poolSize; ++i ) fprintf (stderr, "%d, ", pool[i] );
fprintf (stderr, "\n");
#endif

    *isEndgame = endgame;
    *pieceCount = poolSize;
    return pool;
}
