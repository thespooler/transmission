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

#include <assert.h>
#include <string.h>

#include "transmission.h"
#include "completion.h"
#include "torrent.h"
#include "utils.h"

struct tr_completion
{
    tr_torrent * tor;

    /* do we have this block? */
    tr_bitfield * blockBitfield;

    /* do we have this piece? */
    tr_bitfield * pieceBitfield;

    /* a block is complete if and only if we have it */
    uint16_t * completeBlocks;

    uint8_t doneDirty;
    uint64_t doneHave;
    uint64_t doneTotal;
    uint64_t completeHave;
};

static void
tr_cpReset( tr_completion * cp )
{
    tr_torrent * tor = cp->tor;

    tr_bitfieldClear( cp->pieceBitfield );
    tr_bitfieldClear( cp->blockBitfield );
    memset( cp->completeBlocks, 0, sizeof(uint16_t) * tor->info.pieceCount );

    cp->doneDirty = TRUE;
    cp->doneHave = 0;
    cp->doneTotal = 0;
    cp->completeHave = 0;
}

tr_completion * tr_cpInit( tr_torrent * tor )
{
    tr_completion * cp;

    cp                   = tr_new( tr_completion, 1 );
    cp->tor              = tor;
    cp->blockBitfield    = tr_bitfieldNew( tor->blockCount );
    cp->pieceBitfield    = tr_bitfieldNew( tor->info.pieceCount );
    cp->completeBlocks   = tr_new( uint16_t, tor->info.pieceCount );

    tr_cpReset( cp );

    return cp;
}

void tr_cpClose( tr_completion * cp )
{
    tr_free(         cp->completeBlocks );
    tr_bitfieldFree( cp->pieceBitfield );
    tr_bitfieldFree( cp->blockBitfield );
    tr_free(         cp );
}

/**
***
**/

static void
tr_cpEnsureDoneValid( const tr_completion * ccp )
{
    if( ccp->doneDirty )
    {
        const tr_torrent * tor = ccp->tor;
        const tr_info * info = &tor->info;
        uint64_t have = 0;
        uint64_t total = 0;
        tr_piece_index_t i;
        tr_completion * cp ;

        /* too bad C doesn't have 'mutable' */
        cp = (tr_completion*) ccp;
        cp->doneDirty = FALSE;

        for( i=0; i<info->pieceCount; ++i ) {
            if( !info->pieces[i].dnd ) {
                total += info->pieceSize;
                have += cp->completeBlocks[ i ];
            }
        }

        have *= tor->blockSize;

        /* the last piece/block is probably smaller than the others */
        if( !info->pieces[info->pieceCount-1].dnd ) {
            total -= ( info->pieceSize - tr_torPieceCountBytes(tor,info->pieceCount-1) );
            if( tr_cpBlockIsComplete( cp, tor->blockCount-1 ) )
                have -= ( tor->blockSize - tr_torBlockCountBytes(tor,tor->blockCount-1) );
        }

        assert( have <= total );
        assert( total <= info->totalSize );

        cp->doneHave = have;
        cp->doneTotal = total;
    }
}

void
tr_cpInvalidateDND ( tr_completion * cp )
{
    cp->doneDirty = TRUE;
}

int
tr_cpPieceIsComplete( const tr_completion  * cp,
                      tr_piece_index_t       piece )
{
    assert( piece < cp->tor->info.pieceCount );
    assert( cp->completeBlocks[piece] <= tr_torPieceCountBlocks(cp->tor,piece) );

    return cp->completeBlocks[piece] == tr_torPieceCountBlocks(cp->tor,piece);
}

const tr_bitfield * tr_cpPieceBitfield( const tr_completion * cp )
{
    return cp->pieceBitfield;
}

void
tr_cpPieceAdd( tr_completion * cp, tr_piece_index_t piece )
{
    const tr_torrent * tor = cp->tor;
    const tr_block_index_t start = tr_torPieceFirstBlock(tor,piece);
    const tr_block_index_t end = start + tr_torPieceCountBlocks(tor, piece);
    tr_block_index_t i;

    for( i=start; i<end; ++i )
        tr_cpBlockAdd( cp, i );
}

void
tr_cpPieceRem( tr_completion * cp, tr_piece_index_t piece )
{
    const tr_torrent * tor = cp->tor;
    const tr_block_index_t start = tr_torPieceFirstBlock(tor,piece);
    const tr_block_index_t end = start + tr_torPieceCountBlocks(tor,piece);
    tr_block_index_t block;

    assert( cp != NULL );
    assert( piece < tor->info.pieceCount );
    assert( start < tor->blockCount );
    assert( start <= end );
    assert( end <= tor->blockCount );

    for( block=start; block<end; ++block ) {
        if( tr_cpBlockIsComplete( cp, block ) ) {
            const int blockSize = tr_torBlockCountBytes( tor, block );
            cp->completeHave -= blockSize;
            if( !tor->info.pieces[piece].dnd )
                cp->doneHave -= blockSize;
        }
    }

    cp->completeBlocks[piece] = 0;
    tr_bitfieldRemRange ( cp->blockBitfield, start, end );
    tr_bitfieldRem( cp->pieceBitfield, piece );

    assert( cp->completeHave <= tor->info.totalSize );
    assert( cp->doneHave <= tor->info.totalSize );
    assert( cp->doneHave <= cp->completeHave );
}

int
tr_cpBlockIsComplete( const tr_completion * cp, tr_block_index_t block )
{
    return tr_bitfieldHas( cp->blockBitfield, block ) ? 1 : 0;
}

void
tr_cpBlockAdd( tr_completion * cp, tr_block_index_t block )
{
    const tr_torrent * tor = cp->tor;

    if( !tr_cpBlockIsComplete( cp, block ) )
    {
        const tr_piece_index_t piece = tr_torBlockPiece( tor, block );
        const int blockSize = tr_torBlockCountBytes( tor, block );

        ++cp->completeBlocks[piece];

        if( cp->completeBlocks[piece] == tr_torPieceCountBlocks( tor, piece ) )
            tr_bitfieldAdd( cp->pieceBitfield, piece );

        tr_bitfieldAdd( cp->blockBitfield, block );

        cp->completeHave += blockSize;

        if( !tor->info.pieces[piece].dnd )
            cp->doneHave += blockSize;
    }

    assert( cp->completeHave <= tor->info.totalSize );
    assert( cp->doneHave <= tor->info.totalSize );
    assert( cp->doneHave <= cp->completeHave );
}

const tr_bitfield *
tr_cpBlockBitfield( const tr_completion * cp )
{
    assert( cp );
    assert( cp->blockBitfield );
    assert( cp->blockBitfield->bits );
    assert( cp->blockBitfield->len );

    return cp->blockBitfield;
}

tr_errno
tr_cpBlockBitfieldSet( tr_completion * cp, tr_bitfield * bitfield )
{
    tr_block_index_t i;

    assert( cp );
    assert( bitfield );
    assert( cp->blockBitfield );
    assert( cp->blockBitfield->len == bitfield->len );

    if( !cp || !bitfield || ( bitfield->len != cp->blockBitfield->len ) )
        return TR_ERROR_ASSERT;

    tr_cpReset( cp );
    for( i=0; i < cp->tor->blockCount; ++i )
        if( tr_bitfieldHas( bitfield, i ) )
            tr_cpBlockAdd( cp, i );

    return 0;
}

float
tr_cpPercentBlocksInPiece( const tr_completion * cp, tr_piece_index_t piece )
{
    assert( cp != NULL );

    return (double)cp->completeBlocks[piece] / tr_torPieceCountBlocks(cp->tor,piece);
}

int
tr_cpMissingBlocksInPiece( const tr_completion * cp, tr_piece_index_t piece )
{
    assert( cp != NULL );

    return tr_torPieceCountBlocks(cp->tor,piece) - cp->completeBlocks[piece];
}

/***
****
***/

float
tr_cpPercentComplete ( const tr_completion * cp )
{
    return (double)cp->completeHave / cp->tor->info.totalSize;
}

uint64_t
tr_cpLeftUntilComplete ( const tr_completion * cp )
{
    assert( cp->tor->info.totalSize >= cp->completeHave );

    return cp->tor->info.totalSize - cp->completeHave;
}

float
tr_cpPercentDone( const tr_completion * cp )
{
    tr_cpEnsureDoneValid( cp );

    return cp->doneTotal ? (double)cp->doneHave / cp->doneTotal : 1.0;
}

uint64_t
tr_cpLeftUntilDone ( const tr_completion * cp )
{
    tr_cpEnsureDoneValid( cp );

    return cp->doneTotal - cp->doneHave;
}

uint64_t
tr_cpSizeWhenDone( const tr_completion * cp )
{
    tr_cpEnsureDoneValid( cp );

    return cp->doneTotal;
}

cp_status_t
tr_cpGetStatus ( const tr_completion * cp )
{
    assert( cp->tor->info.totalSize >= cp->completeHave );

    if( cp->completeHave == cp->tor->info.totalSize )
        return TR_CP_COMPLETE;

    tr_cpEnsureDoneValid( cp );

    return cp->doneHave >= cp->doneTotal ? TR_CP_DONE
                                         : TR_CP_INCOMPLETE;
}

uint64_t
tr_cpHaveValid( const tr_completion * cp )
{
    uint64_t b = 0;
    const tr_torrent * tor = cp->tor;
    const tr_info * info = &tor->info;
    tr_piece_index_t i;

    for( i=0; i<info->pieceCount; ++i )
        if( tr_cpPieceIsComplete( cp, i ) )
            ++b;

    b *= info->pieceSize;

    if( tr_cpPieceIsComplete( cp, info->pieceCount-1 ) )
        b -= (info->pieceSize - (info->totalSize % info->pieceSize));

    return b;
}

uint64_t
tr_cpHaveTotal( const tr_completion * cp )
{
    return cp->completeHave;
}
