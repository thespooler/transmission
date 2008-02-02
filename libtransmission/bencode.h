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

#ifndef TR_BENCODE_H
#define TR_BENCODE_H 1

#include <inttypes.h> /* for int64_t */
#include <string.h> /* for memset */

typedef struct benc_val_s
{
#define TYPE_INT  1
#define TYPE_STR  2
#define TYPE_LIST 4
#define TYPE_DICT 8
    char   type;
    union
    {
        int64_t i;
        struct
        {
            int    i;
            int    nofree;
            char * s;
        } s;
        struct
        {
            int alloc;
            int count;
            struct benc_val_s * vals;
        } l;
    } val;
} benc_val_t;


int          tr_bencParse( const void      * buf,
                           const void      * bufend,
                           benc_val_t      * setme_benc,
                           const uint8_t  ** setme_end );

int          tr_bencLoad( const void  * buf,
                          int           buflen,
                          benc_val_t  * setme_benc,
                          char       ** setme_end );

void         tr_bencPrint( benc_val_t * val );
void         tr_bencFree( benc_val_t * val );
benc_val_t * tr_bencDictFind( benc_val_t * val, const char * key );
benc_val_t * tr_bencDictFindType( benc_val_t * val, const char * key, int type );
benc_val_t * tr_bencDictFindFirst( benc_val_t * val, ... );

/* marks a string as 'do not free' and returns it */
char *       tr_bencStealStr( benc_val_t * val );

/* convenience functions for building benc_val_t structures */

static inline void tr_bencInit( benc_val_t * val, int type )
{
    memset( val, 0, sizeof( *val ) );
    val->type = type;
}

#define tr_bencInitStr( a, b, c, d ) \
    _tr_bencInitStr( (a), ( char * )(b), (c), (d) )
void   _tr_bencInitStr( benc_val_t * val, char * str, int len, int nofree );
int    tr_bencInitStrDup( benc_val_t * val, const char * str );
void   tr_bencInitInt( benc_val_t * val, int64_t num );
int   tr_bencListReserve( benc_val_t * list, int count );
/* note that for one key-value pair, count should be 1, not 2 */
int   tr_bencDictReserve( benc_val_t * dict, int count );
benc_val_t * tr_bencListAdd( benc_val_t * list );
/* note: key must not be freed or modified while val is in use */
benc_val_t * tr_bencDictAdd( benc_val_t * dict, const char * key );

char*  tr_bencSave( const benc_val_t * val, int * len );

int64_t  tr_bencGetInt ( const benc_val_t * val );


/**
***  Treat these as private -- they're only made public here
***  so that the unit tests can find them
**/

int  tr_bencParseInt( const uint8_t  * buf,
                      const uint8_t  * bufend,
                      const uint8_t ** setme_end, 
                      int64_t        * setme_val );

int  tr_bencParseStr( const uint8_t  * buf,
                      const uint8_t  * bufend,
                      const uint8_t ** setme_end, 
                      uint8_t       ** setme_str,
                      size_t         * setme_strlen );

/**
***
**/

benc_val_t* tr_bencListGetNthChild( benc_val_t * val, int i );




#endif
